#pragma once

#include <mutex>
#include <list>
#include <thread>

#include "nhope/async/ao-context-close-handler.h"
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
    friend RefPtr<AOContextImpl> makeRefPtr<AOContextImpl, AOContextImpl*>(AOContextImpl*&&);
    friend RefPtr<AOContextImpl> makeRefPtr<AOContextImpl, Executor&>(Executor&);
    friend RefPtr<AOContextImpl> refPtrFromRawPtr<AOContextImpl>(AOContextImpl*, NotAddRefTag);

public:
    static RefPtr<AOContextImpl> makeRoot(Executor& executor)
    {
        return makeRefPtr<AOContextImpl>(executor);
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

        return makeRefPtr<AOContextImpl>(this);
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
            if (m_parent != nullptr) {
                m_parent->removeCloseHandler(&m_parentCloseHandler);
            }

            this->waitForClosing();
            m_state.setClosingFlag();

            this->cancelAOHandlers();
            this->callCloseHandlers();

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

    void addCloseHandler(AOContextCloseHandler* closeHandler)
    {
        assert(this->isOpen());                    // NOLINT
        assert(closeHandler != nullptr);           // NOLINT
        assert(closeHandler->m_next == nullptr);   // NOLINT
        assert(closeHandler->m_prev == nullptr);   // NOLINT

        m_state.lockCloseHandlerList();

        if (m_closeHandlerList != nullptr) {
            assert(m_closeHandlerList->m_prev == nullptr);   // NOLINT
            m_closeHandlerList->m_prev = closeHandler;
        }

        closeHandler->m_next = m_closeHandlerList;
        m_closeHandlerList = closeHandler;

        m_state.unlockCloseHandlerList();
    }

    void removeCloseHandler(AOContextCloseHandler* closeHandler)
    {
        m_state.lockCloseHandlerList();

        if (closeHandler == m_closeHandlerList) {
            // Removal from the head
            m_closeHandlerList = m_closeHandlerList->m_next;
            if (m_closeHandlerList != nullptr) {
                m_closeHandlerList->m_prev = nullptr;
            }

            m_state.unlockCloseHandlerList();
            return;
        }

        if (closeHandler->m_prev != nullptr) {
            // Removal from the middle or tail
            closeHandler->m_prev->m_next = closeHandler->m_next;
            if (closeHandler->m_next != nullptr) {
                closeHandler->m_next->m_prev = closeHandler->m_prev;
            }

            m_state.unlockCloseHandlerList();
            return;
        }

        // closeHandler is not in the list, so must have been removed by a call to callCloseHandler
        if (m_closerThreadId == std::this_thread::get_id()) {
            /* closeHandler is destroyed from callCloseHandler */
            if (closeHandler->m_destroyed != nullptr) {
                *closeHandler->m_destroyed = true;
            }

            m_state.unlockCloseHandlerList();
            return;
        }

        m_state.unlockCloseHandlerList();

        while (!closeHandler->m_done) {
            std::this_thread::yield();   // FIXME: Use atomic::wait from C++20
        }
    }

private:
    explicit AOContextImpl(Executor& executor)
      : m_groupId(reinterpret_cast<AOContextGroupId>(this))   // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
      , m_executorHolder(makeStrand(executor))
    {}

    explicit AOContextImpl(AOContextImpl* parent)
      : m_groupId(parent->m_groupId)
      , m_executorHolder(makeStrand(parent->executor()))
      , m_parent(parent)
      , m_parentCloseHandler(this)
    {
        m_parent->addCloseHandler(&m_parentCloseHandler);
    }

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

    void callCloseHandlers()
    {
        m_state.lockCloseHandlerList();

        m_closerThreadId = std::this_thread::get_id();
        while (m_closeHandlerList != nullptr) {
            auto* curCloseHandler = m_closeHandlerList;
            m_closeHandlerList = m_closeHandlerList->m_next;

            bool destroyed = false;
            curCloseHandler->m_destroyed = &destroyed;

            m_state.unlockCloseHandlerList();
            curCloseHandler->aoContextClose();
            m_state.lockCloseHandlerList();

            if (!destroyed) {
                curCloseHandler->m_done = true;
            }
        }

        m_closerThreadId = std::thread::id();
        m_state.unlockCloseHandlerList();
    }

    [[nodiscard]] std::size_t blockCloseCountMadeFromThisThread() const noexcept
    {
        return WorkingInThisThreadSet::count(m_groupId);
    }

    class ParentCloseHandler final : public AOContextCloseHandler
    {
    public:
        ParentCloseHandler() = default;
        explicit ParentCloseHandler(AOContextImpl* self)
          : m_self(self)
        {}

        void aoContextClose() noexcept override
        {
            assert(m_self != nullptr);   // NOLINT
            m_self->close();
        }

    private:
        AOContextImpl* m_self = nullptr;
    };

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
    ParentCloseHandler m_parentCloseHandler;

    std::thread::id m_closerThreadId;
    AOContextCloseHandler* m_closeHandlerList = nullptr;
};

}   // namespace nhope::detail
