#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/ao-handler.h"
#include "nhope/async/detail/ao-handler-id.h"
#include "nhope/async/executor.h"
#include "nhope/utils/scope-exit.h"
#include "nhope/utils/stack-set.h"

#include "ao-handler-storage.h"
#include "make-strand.h"

namespace nhope {

namespace {

using namespace detail;

class AOContextState final
{
public:
    enum Flags : std::uint64_t
    {
        PreparingForClosing = 1 << 0,
        Closing = 1 << 1,
        Closed = 1 << 2,
    };
    static constexpr auto flagsMask = std::uint64_t(0xFF);
    static constexpr auto refCounterOffset = 8;
    static constexpr auto refCounterMask = std::uint64_t(0xFF'FF'FF'FF) << refCounterOffset;
    static constexpr auto blockCloseCounterOffset = 40;
    static constexpr auto blockCloseCounterMask = std::uint64_t(0xFF'FF'FF) << blockCloseCounterOffset;

    static constexpr auto oneRef = std::uint64_t(1) << refCounterOffset;
    static constexpr auto oneBlockClose = std::uint64_t(1) << blockCloseCounterOffset;

    AOContextState() = default;

    [[nodiscard]] bool blockClose() noexcept
    {
        const auto oldState = m_state.fetch_add(oneBlockClose, std::memory_order_relaxed);
        if ((oldState & Flags::PreparingForClosing) != 0) {
            this->unblockClose();
            return false;
        };

        return true;
    }

    [[nodiscard]] bool blockCloseAndAddRef() noexcept
    {
        const auto oldState = m_state.fetch_add(oneBlockClose | oneRef, std::memory_order_relaxed);
        if ((oldState & Flags::PreparingForClosing) != 0) {
            this->unblockCloseAndRemoveRef();
            return false;
        };

        return true;
    }

    inline void addRef() noexcept
    {
        m_state.fetch_add(oneRef, std::memory_order_relaxed);
    }

    inline bool removeRef() noexcept
    {
        const auto oldState = m_state.fetch_sub(oneRef, std::memory_order_acq_rel);
        return (oldState & refCounterMask) == oneRef;
    }

    inline void unblockClose() noexcept
    {
        m_state.fetch_sub(oneBlockClose, std::memory_order_acq_rel);
    }

    inline bool unblockCloseAndRemoveRef() noexcept
    {
        const auto oldState = m_state.fetch_sub(oneBlockClose | oneRef, std::memory_order_acq_rel);
        return (oldState & refCounterMask) == oneRef;
    }

    // Returns true if we are the first to call startClose
    inline bool startClose() noexcept
    {
        const auto oldState = m_state.fetch_or(Flags::PreparingForClosing, std::memory_order_relaxed);
        return (oldState & Flags::PreparingForClosing) == 0;
    }

    [[nodiscard]] bool isOpen() const noexcept
    {
        return (m_state.load(std::memory_order_relaxed) & Flags::PreparingForClosing) == 0;
    }

    void waitForClosed() const noexcept
    {
        while ((m_state.load(std::memory_order_acq_rel) & Flags::Closed) == 0) {
            std::this_thread::yield();   // FIXME: Use atomic::wait from C++20
        }
    }

    [[nodiscard]] bool isClosed() const noexcept
    {
        return (m_state.load(std::memory_order_relaxed) & Flags::Closed) != 0;
    }

    void setClosingFlag() noexcept
    {
        [[maybe_unused]] const auto oldState = m_state.fetch_or(Flags::Closing, std::memory_order_acq_rel);
        assert((oldState & Flags::Closing) == 0);   // NOLINT
    }

    void setClosedFlag() noexcept
    {
        [[maybe_unused]] const auto oldState = m_state.fetch_or(Flags::Closed, std::memory_order_acq_rel);
        assert((oldState & Flags::Closed) == 0);   // NOLINT
    }

    [[nodiscard]] std::size_t getBlockCloseCounter() const noexcept
    {
        const auto state = m_state.load(std::memory_order_relaxed);
        const auto blockCloseCounter = (state & blockCloseCounterMask) >> blockCloseCounterOffset;
        return static_cast<std::size_t>(blockCloseCounter);
    }

private:
    std::atomic<std::uint64_t> m_state = oneRef;
};

class AutoRelease final
{
public:
    AutoRelease(const AutoRelease& other) noexcept;

    explicit AutoRelease(AOContextImpl* aoImpl) noexcept
      : m_aoImpl(aoImpl)
    {}

    AutoRelease(AutoRelease&& other) noexcept
      : m_aoImpl(other.m_aoImpl)
    {
        other.m_aoImpl = nullptr;
    }

    ~AutoRelease();

    [[nodiscard]] AOContextImpl* get() const noexcept
    {
        return m_aoImpl;
    }

    [[nodiscard]] AOContextImpl* take() noexcept
    {
        return std::exchange(m_aoImpl, nullptr);
    }

private:
    AOContextImpl* m_aoImpl = nullptr;
};

}   // namespace

namespace detail {

class AOContextImpl final
{
public:
    explicit AOContextImpl(Executor& executor)
      : m_executorHolder(makeStrand(executor))
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      , m_groupId(reinterpret_cast<AOContextGroupId>(this))
    {}

    explicit AOContextImpl(AOContextImpl* parent)
      : m_executorHolder(makeStrand(parent->executor()))
      , m_parent(parent)
      , m_groupId(parent->m_groupId)
    {}

    void addRef() noexcept
    {
        m_state.addRef();
    }

    void release() noexcept
    {
        if (m_state.removeRef()) {
            delete this;
        }
    }

    [[nodiscard]] bool isOpen() const noexcept
    {
        return m_state.isOpen();
    }

    template<typename Work>
    void exec(Work&& work, Executor::ExecMode mode)
    {
        if (!m_state.blockCloseAndAddRef()) {
            return;
        }

        ScopeExit unblock([this] {
            m_state.unblockClose();
        });

        /* Note that we are working in this thread.
           Now you can work with the internal storage only from this thread.  */
        WorkingInThisThreadSet::Item thisAOContexItem(m_groupId);
        m_executorHolder->exec(
          [work = std::move(work), autoRelease = AutoRelease(this)]() mutable {
              /* Take the reference to the AOContextImpl from autoRelease,
                 now have to remove the reference ourselves. */
              auto* self = autoRelease.take();

              /* m_executorHolder->exec could be started asynchronously,
                 so we need to block closing of the AOContext again and
                 indicate that we working in this thread. */
              if (!self->m_state.blockClose()) {
                  self->release();
                  return;
              }
              WorkingInThisThreadSet::Item thisAOContexItem(self->m_groupId);

              try {
                  work(self);
              } catch (...) {
              }

              self->unblockCloseAndRelease();
          },
          mode);
    }

    AOContextImpl* makeChild()
    {
        if (!m_state.blockClose()) {
            throw AOContextClosed();
        }

        ScopeExit unblock([this] {
            m_state.unblockClose();
        });

        std::scoped_lock lock(m_childrenMutex);
        auto child = AutoRelease(new AOContextImpl(this));
        m_children.emplace_back(child.get());
        return child.take();
    }

    [[nodiscard]] AOHandlerId putAOHandler(std::unique_ptr<AOHandler> handler)
    {
        if (!m_state.blockClose()) {
            throw AOContextClosed();
        }

        ScopeExit unblock([this] {
            m_state.unblockClose();
        });

        return this->putAOHandlerImpl(std::move(handler));
    }

    void callAOHandler(AOHandlerId id, Executor::ExecMode mode)
    {
        this->exec(
          [id](auto* self) {
              self->callAOHandlerImpl(id);
          },
          mode);
    }

    void close()
    {
        if (m_state.startClose()) {
            this->waitForClosing();
            m_state.setClosingFlag();

            this->cancelAOHandlers();
            this->closeChildren();

            if (m_parent != nullptr) {
                m_parent->childClosed(this);
                m_parent = nullptr;
            }

            m_executorHolder.reset();
            m_state.setClosedFlag();
        } else {
            /* Someone has already started the closing AOContext,
               we'll just wait until the closing is over. */
            this->waitForClosed();
        }
    }

    [[nodiscard]] bool aoContextWorkInThisThread() const noexcept
    {
        return WorkingInThisThreadSet::contains(m_groupId);
    }

    SequenceExecutor& executor()
    {
        return *m_executorHolder;
    }

private:
    ~AOContextImpl() = default;

    /* Defines a group of AOContextGroup that work strictly sequentially
       (through SequenceExecutor ot the root AOContext)  */
    using AOContextGroupId = std::uintptr_t;
    using WorkingInThisThreadSet = StackSet<AOContextGroupId>;

    std::unique_ptr<AOHandler> getAOHandler(AOHandlerId id)
    {
        assert(this->aoContextWorkInThisThread());   // NOLINT

        if (isExternalId(id)) {
            std::scoped_lock lock(m_externalAOHandlersMutex);
            return this->m_externalAOHandlers.get(id);
        }
        return this->m_internalAOHandlers.get(id);
    }

    AOHandlerId putAOHandlerImpl(std::unique_ptr<AOHandler> handler)
    {
        AOHandlerId id{};
        if (this->aoContextWorkInThisThread()) {
            id = this->nextInternalId();
            this->m_internalAOHandlers.put(id, std::move(handler));
        } else {
            std::unique_lock lock(this->m_externalAOHandlersMutex);
            id = this->nextExternalId(lock);
            this->m_externalAOHandlers.put(id, std::move(handler));
        }

        return id;
    }

    void callAOHandlerImpl(AOHandlerId id)
    {
        assert(this->aoContextWorkInThisThread());   // NOLINT

        if (auto handler = this->getAOHandler(id)) {
            handler->call();
        }
    }

    AOHandlerId nextInternalId() noexcept
    {
        assert(this->aoContextWorkInThisThread());   // NOLINT
        return 2 * this->m_internalAOHandlerCounter++;
    }

    AOHandlerId nextExternalId([[maybe_unused]] std::unique_lock<std::mutex>& lock) noexcept
    {
        assert(!this->aoContextWorkInThisThread());   // NOLINT
        assert(lock.owns_lock());                     // NOLINT
        return 2 * this->m_externalAOHandlerCounter++ + 1;
    }

    static bool isExternalId(AOHandlerId id) noexcept
    {
        return (id & 1) != 0;
    }

    void waitForClosing() noexcept
    {
        /* Wait until other threads remove their blockClose */
        const auto myBlockCount = this->blockCloseCountMadeFromThisThread();
        while (m_state.getBlockCloseCounter() > myBlockCount) {
            ;
        }
    }

    void waitForClosed() const noexcept
    {
        if (this->aoContextWorkInThisThread()) {
            // We can't wait, closing is done from AOHandler and we will get a deadlock.
            return;
        }

        this->m_state.waitForClosed();
    }

    void cancelAOHandlers() noexcept
    {
        AOHandlerStorage internalAOHandlers = std::move(m_internalAOHandlers);
        AOHandlerStorage externalAOHandlers;
        {
            std::scoped_lock lock(m_externalAOHandlersMutex);
            externalAOHandlers = std::move(m_externalAOHandlers);
        }

        internalAOHandlers.cancelAll();
        externalAOHandlers.cancelAll();
    }

    void closeChildren()
    {
        std::list<AOContextImpl*> children;
        {
            std::scoped_lock lock(m_childrenMutex);
            children = std::move(m_children);
        }

        for (auto& child : children) {
            child->close();
            child->release();
        }
    }

    void childClosed(AOContextImpl* child)
    {
        std::scoped_lock lock(m_childrenMutex);
        for (auto it = m_children.begin(); it != m_children.end(); ++it) {
            if (*it == child) {
                (*it)->release();
                m_children.erase(it);
                return;
            }
        }
    }

    void unblockCloseAndRelease()
    {
        if (m_state.unblockCloseAndRemoveRef()) {
            delete this;
        }
    }

    [[nodiscard]] std::size_t blockCloseCountMadeFromThisThread() const noexcept
    {
        return WorkingInThisThreadSet::count(m_groupId);
    }

    AOContextState m_state;
    const AOContextGroupId m_groupId;

    /* External storage can be accessed from any thread,
       but mutex protection is required */
    std::mutex m_externalAOHandlersMutex;
    AOHandlerId m_externalAOHandlerCounter = 0;
    AOHandlerStorage m_externalAOHandlers;

    /* The internal storage can only be accessed from the thread in which AOContex is running,
       but mutex protection is not required */
    AOHandlerId m_internalAOHandlerCounter = 0;
    AOHandlerStorage m_internalAOHandlers;

    SequenceExecutorHolder m_executorHolder;

    AOContextImpl* m_parent = nullptr;
    std::mutex m_childrenMutex;
    std::list<AOContextImpl*> m_children;
};

}   // namespace detail

namespace {

AutoRelease::AutoRelease(const AutoRelease& other) noexcept
  : m_aoImpl(other.m_aoImpl)
{
    if (m_aoImpl != nullptr) {
        m_aoImpl->addRef();
    }
}

AutoRelease::~AutoRelease()
{
    if (m_aoImpl != nullptr) {
        m_aoImpl->release();
    }
}

}   // namespace

AOHandlerCall::AOHandlerCall(AOHandlerId id, AOContextImpl* aoImpl)
  : m_id(id)
  , m_aoImpl(aoImpl)
{
    m_aoImpl->addRef();
}

AOHandlerCall::AOHandlerCall(AOHandlerCall&& other) noexcept
  : m_id(other.m_id)
  , m_aoImpl(other.m_aoImpl)
{
    other.m_aoImpl = nullptr;
}

AOHandlerCall::~AOHandlerCall()
{
    this->reset();
}

AOHandlerCall& AOHandlerCall::operator=(AOHandlerCall&& other) noexcept
{
    this->reset();

    std::swap(m_aoImpl, other.m_aoImpl);
    std::swap(m_id, other.m_id);

    return *this;
}

void AOHandlerCall::operator()(Executor::ExecMode mode)
{
    m_aoImpl->callAOHandler(m_id, mode);
}

void AOHandlerCall::reset()
{
    if (m_aoImpl != nullptr) {
        m_aoImpl->release();
        m_aoImpl = nullptr;
    }
    m_id = invalidAOHandlerId;
}

AOContext::AOContext(Executor& executor)
  : m_aoImpl(new detail::AOContextImpl(executor))
{}

AOContext::AOContext(AOContext& parent)
  : m_aoImpl(parent.m_aoImpl->makeChild())
{
    m_aoImpl->addRef();
}

AOContext::~AOContext()
{
    m_aoImpl->close();
    m_aoImpl->release();
}

bool AOContext::isOpen() const noexcept
{
    return m_aoImpl->isOpen();
}

void AOContext::close()
{
    m_aoImpl->close();
}

AOHandlerCall AOContext::putAOHandler(std::unique_ptr<AOHandler> handler)
{
    const auto id = m_aoImpl->putAOHandler(std::move(handler));
    return AOHandlerCall(id, m_aoImpl);
}

void AOContext::callAOHandler(std::unique_ptr<AOHandler> handler, Executor::ExecMode mode)
{
    const auto id = m_aoImpl->putAOHandler(std::move(handler));
    m_aoImpl->callAOHandler(id, mode);
}

[[nodiscard]] bool AOContext::workInThisThread() const
{
    return m_aoImpl->aoContextWorkInThisThread();
}

SequenceExecutor& AOContext::executor()
{
    return m_aoImpl->executor();
}

AOContextRef::AOContextRef(AOContext& aoCtx) noexcept
  : m_aoImpl(aoCtx.m_aoImpl)
{
    m_aoImpl->addRef();
}

AOContextRef::AOContextRef(const AOContextRef& other) noexcept
  : m_aoImpl(other.m_aoImpl)
{
    if (m_aoImpl != nullptr) {
        m_aoImpl->addRef();
    }
}

AOContextRef::AOContextRef(AOContextRef&& other) noexcept
  : m_aoImpl(other.m_aoImpl)
{
    other.m_aoImpl = nullptr;
}

AOContextRef::~AOContextRef()
{
    if (m_aoImpl != nullptr) {
        m_aoImpl->release();
    }
}

AOHandlerCall AOContextRef::putAOHandler(std::unique_ptr<AOHandler> handler)
{
    const auto id = m_aoImpl->putAOHandler(std::move(handler));
    return AOHandlerCall(id, m_aoImpl);
}

void AOContextRef::callAOHandler(std::unique_ptr<AOHandler> handler, Executor::ExecMode mode)
{
    const auto id = m_aoImpl->putAOHandler(std::move(handler));
    m_aoImpl->callAOHandler(id, mode);
}

}   // namespace nhope
