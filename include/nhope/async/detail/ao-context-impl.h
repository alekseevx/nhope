#pragma once

#include <mutex>
#include <list>

#include "nhope/async/ao-context-error.h"
#include "nhope/async/detail/ao-context-state.h"
#include "nhope/async/detail/ao-handler-id.h"
#include "nhope/async/detail/ao-handler-storage.h"
#include "nhope/async/detail/make-strand.h"
#include "nhope/async/executor.h"

#include "nhope/utils/detail/ref-ptr.h"
#include "nhope/utils/scope-exit.h"
#include "nhope/utils/stack-set.h"

namespace nhope::detail {

class AOContextImpl;

template<>
struct RefCounterT<AOContextImpl>
{
    using Type = AOContextImpl;
};

class AOContextImpl final
{
    friend class RefPtr<AOContextImpl>;
    friend RefPtr<AOContextImpl> refPtrFromRawPtr<AOContextImpl>(AOContextImpl*, NotAddRefTag);

    template<typename T, typename... Args>
    friend RefPtr<T> makeRefPtr(Args&&... args);   // NOLINT

public:
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
          [work = std::move(work), self = refPtrFromRawPtr(this, notAddRef)]() mutable {
              /* m_executorHolder->exec could be started asynchronously,
                 so we need to block closing of the AOContext again and
                 indicate that we working in this thread. */
              if (!self->m_state.blockClose()) {
                  return;
              }
              WorkingInThisThreadSet::Item thisAOContexItem(self->m_groupId);

              try {
                  work(self.get());
              } catch (...) {
              }

              /* Take the reference from self to atomically 
                 remove the reference and unblock close. */
              auto* s = self.take();
              s->unblockCloseAndRelease();
          },
          mode);
    }

    RefPtr<AOContextImpl> makeChild()
    {
        if (!m_state.blockClose()) {
            throw AOContextClosed();
        }

        ScopeExit unblock([this] {
            m_state.unblockClose();
        });

        std::scoped_lock lock(m_childrenMutex);
        return m_children.emplace_back(makeRefPtr<AOContextImpl>(this));
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

    ~AOContextImpl() = default;

    void addRef() noexcept
    {
        m_state.addRef();
    }

    bool release() noexcept
    {
        return m_state.removeRef();
    }

    void unblockCloseAndRelease()
    {
        if (m_state.unblockCloseAndRemoveRef()) {
            delete this;
        }
    }

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
        std::list<RefPtr<AOContextImpl>> children;
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
    std::list<RefPtr<AOContextImpl>> m_children;
};

}   // namespace nhope::detail
