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
#include "nhope/async/executor.h"
#include "nhope/utils/stack-set.h"

#include "ao-handler-storage.h"
#include "make-strand.h"

namespace nhope {

namespace {

using namespace detail;

constexpr auto flagsWidth = std::numeric_limits<size_t>::digits;

class AOContextState final
{
public:
    enum Flags : std::size_t
    {
        PreparingForClosing = static_cast<size_t>(1) << (flagsWidth - 1),
        Closing = static_cast<size_t>(1) << (flagsWidth - 2),
        Closed = static_cast<size_t>(1) << (flagsWidth - 3),
    };
    static constexpr auto flagsMask = Flags::PreparingForClosing;
    static constexpr auto blockCloseCounterMask = ~flagsMask;

    [[nodiscard]] bool blockClose() noexcept
    {
        const auto oldState = m_state.fetch_add(1, std::memory_order_acquire);
        if ((oldState & Flags::PreparingForClosing) != 0) {
            this->unblockClose();
            return false;
        };

        return true;
    }

    void unblockClose() noexcept
    {
        const auto oldState = m_state.fetch_sub(1, std::memory_order_release);

        if ((oldState & Flags::PreparingForClosing) == 0) {
            return;
        }

        const auto blockCloseCounter = oldState & blockCloseCounterMask;
        if (blockCloseCounter == 1) {
            m_state.fetch_or(Flags::Closing, std::memory_order_relaxed);
        }
    }

    bool startClose() noexcept
    {
        const auto oldState = m_state.fetch_or(Flags::PreparingForClosing, std::memory_order_relaxed);
        if ((oldState & Flags::PreparingForClosing) != 0) {
            /* Someone has already started the closing AOContext. */
            return false;
        }

        const auto blockCloseCounter = oldState & blockCloseCounterMask;
        if (blockCloseCounter == 0) {
            m_state.fetch_or(Flags::Closing, std::memory_order_acquire);
        }
        return true;
    }

    [[nodiscard]] bool isOpen() const noexcept
    {
        return (m_state.load(std::memory_order_relaxed) & Flags::PreparingForClosing) == 0;
    }

    void waitForClosing() const noexcept
    {
        while ((m_state.load(std::memory_order_acquire) & Flags::Closing) == 0) {
            std::this_thread::yield();   // FIXME: Use atomic::wait from C++20
        }
    }

    void waitForClosed() const noexcept
    {
        while ((m_state.load(std::memory_order_acquire) & Flags::Closed) == 0) {
            std::this_thread::yield();   // FIXME: Use atomic::wait from C++20
        }
    }

    [[nodiscard]] bool isClosed() const noexcept
    {
        return (m_state.load(std::memory_order_relaxed) & Flags::Closed) != 0;
    }

    void closed() noexcept
    {
        m_state.fetch_or(Flags::Closed, std::memory_order_release);
    }

private:
    std::atomic<std::size_t> m_state = 0;
};

class BlockClose final
{
public:
    explicit BlockClose(AOContextState& state) noexcept
      : m_state(state)
      , m_closeBlocked(state.blockClose())
    {}

    ~BlockClose()
    {
        if (m_closeBlocked) {
            m_state.unblockClose();
        }
    }

    operator bool() const noexcept
    {
        return m_closeBlocked;
    }

private:
    AOContextState& m_state;
    const bool m_closeBlocked;
};

}   // namespace

namespace detail {

class AOContextImpl final : public std::enable_shared_from_this<AOContextImpl>
{
public:
    explicit AOContextImpl(Executor& executor)
      : m_executorHolder(makeStrand(executor))
    {}

    explicit AOContextImpl(std::shared_ptr<AOContextImpl> parent)
      : m_executorHolder(makeStrand(parent->executor()))
      , m_parent(std::move(parent))
    {}

    [[nodiscard]] bool isOpen() const noexcept
    {
        return m_state.isOpen();
    }

    template<typename Work>
    void exec(Work&& work, Executor::ExecMode mode)
    {
        if (const auto blockClose = BlockClose(m_state)) {
            m_executorHolder->exec(
              [work = std::forward<Work>(work), weakSelf = weak_from_this()] {
                  if (auto self = weakSelf.lock()) {
                      self->doWork(work);
                  }
              },
              mode);
        }
    }

    std::shared_ptr<AOContextImpl> makeChild()
    {
        const auto blockClose = BlockClose(m_state);
        if (!blockClose) {
            throw AOContextClosed();
        }

        std::scoped_lock lock(m_childrenMutex);
        return m_children.emplace_back(std::make_shared<AOContextImpl>(shared_from_this()));
    }

    [[nodiscard]] AOHandlerId putAOHandler(std::unique_ptr<AOHandler> handler)
    {
        const auto blockClose = BlockClose(m_state);
        if (!blockClose) {
            throw AOContextClosed();
        }
        return this->putAOHandlerImpl(std::move(handler));
    }

    void callAOHandler(AOHandlerId id, Executor::ExecMode mode)
    {
        this->exec(
          [id, this] {
              this->callAOHandlerImpl(id);
          },
          mode);
    }

    void close()
    {
        if (m_state.startClose()) {
            this->waitForClosing();

            this->cancelAOHandlers();
            this->closeChildren();

            m_state.closed();

            if (m_parent != nullptr) {
                m_parent->childClosed(this);
            }
        } else {
            /* Someone has already started the closing AOContext,
               we'll just wait until the closing is over. */
            this->waitForClosed();
        }
    }

    [[nodiscard]] bool aoContextWorkInThisThread() const noexcept
    {
        return WorkingInThisThreadSet::contains(this->key());
    }

    SequenceExecutor& executor()
    {
        return *m_executorHolder;
    }

private:
    using AOContextKey = std::uintptr_t;
    using WorkingInThisThreadSet = StackSet<AOContextKey>;

    AOContextKey key() const noexcept
    {
        /*
         * Using a pointer to executor as key allows to optimize
         * the work of related AOContext, since they all have the same
         * executor
         */
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<AOContextKey>(m_executorHolder.get());
    }

    template<typename Work>
    void doWork(const Work& work)
    {
        if (const auto blockClose = BlockClose(m_state)) {
            /* Note that we are working in this thread.
               Now you can work with the internal storage only from this thread.  */
            WorkingInThisThreadSet::Item thisAOContexItem(this->key());
            try {
                work();
            } catch (...) {
            }
        }
    }

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

    void waitForClosing() const noexcept
    {
        if (this->aoContextWorkInThisThread()) {
            // We can't wait, closing is done from AOHandler and we will get a deadlock.
            return;
        }

        this->m_state.waitForClosing();
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
        std::list<std::shared_ptr<AOContextImpl>> children;
        {
            std::scoped_lock lock(m_childrenMutex);
            children = std::move(m_children);
        }

        for (auto& child : children) {
            child->close();
        }
    }

    void childClosed(AOContextImpl* child)
    {
        std::scoped_lock lock(m_childrenMutex);
        for (auto it = m_children.begin(); it != m_children.end(); ++it) {
            if (it->get() == child) {
                m_children.erase(it);
                return;
            }
        }
    }

    AOContextState m_state;

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

    std::shared_ptr<AOContextImpl> m_parent;
    std::mutex m_childrenMutex;
    std::list<std::shared_ptr<AOContextImpl>> m_children;
};

}   // namespace detail

AOHandlerCall::AOHandlerCall(AOHandlerId id, AOContextImplPtr aoImpl)
  : m_id(id)
  , m_aoImpl(std::move(aoImpl))
{}

void AOHandlerCall::operator()(Executor::ExecMode mode)
{
    m_aoImpl->callAOHandler(m_id, mode);
}

AOContext::AOContext(Executor& executor)
  : m_aoImpl(std::make_shared<detail::AOContextImpl>(executor))
{}

AOContext::AOContext(AOContext& parent)
  : m_aoImpl(parent.m_aoImpl->makeChild())
{}

AOContext::~AOContext()
{
    this->close();
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

AOContextRef::AOContextRef(AOContext& aoCtx)
  : m_aoImpl(aoCtx.m_aoImpl)
{}

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
