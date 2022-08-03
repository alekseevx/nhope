#pragma once

#include <mutex>
#include <list>
#include <thread>
#include <utility>

#include "nhope/async/ao-context-close-handler.h"
#include "nhope/async/ao-context-error.h"
#include "nhope/async/detail/ao-context-state.h"
#include "nhope/async/detail/make-strand.h"
#include "nhope/async/executor.h"

#include "nhope/utils/detail/ref-ptr.h"
#include "nhope/utils/scope-exit.h"
#include "nhope/utils/stack-set.h"

namespace nhope::detail {

class AOContextImpl;

template<typename Fn>
void tryCall(Fn&& fn) noexcept
{
    try {
        fn();
    } catch (...) {
    }
}

template<>
struct RefCounterT<AOContextImpl>
{
    using Type = AOContextImpl;
};

class AOContextImpl final : public AOContextCloseHandler
{
    friend class RefPtr<AOContextImpl>;
    friend RefPtr<AOContextImpl> makeRefPtr<AOContextImpl, AOContextImpl*>(AOContextImpl*&&);
    friend RefPtr<AOContextImpl> makeRefPtr<AOContextImpl, Executor&>(Executor&);
    friend RefPtr<AOContextImpl> refPtrFromRawPtr<AOContextImpl>(AOContextImpl*);
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

        /* In case m_executorHolder->exec is called synchronously. */
        WorkingInThisThreadSet::Item thisGroup(m_groupId);

        m_executorHolder->exec(
          [work = std::move(work), self = refPtrFromRawPtr(this, notAddRef)]() mutable {
              if (self->aoContextWorkInThisThread()) {
                  /* m_executorHolder->exec was called synchronously. */
                  tryCall(std::forward<Work>(work));
                  return;
              }

              /* m_executorHolder->exec was called asynchronously,
                 so we need to block closing of the AOContext again and
                 indicate that we working in this thread. */
              if (!self->m_state.blockClose()) {
                  return;
              }
              WorkingInThisThreadSet::Item thisGroup(self->m_groupId);

              tryCall(std::forward<Work>(work));

              /* Take the reference from self to atomically 
                 remove the reference and unblock close. */
              auto* s = self.take();
              s->unblockCloseAndRelease();
          },
          mode);
    }

    template<typename StartFn>
    void startCancellableTask(StartFn&& start, AOContextCloseHandler* closeHandler)
    {
        if (!m_state.blockClose()) {
            throw AOContextClosed();
        }

        ScopeExit unblock([this] {
            m_state.unblockClose();
        });

        this->startCancellableTaskNonBlockClose(std::forward<StartFn>(start), closeHandler);
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

    void close()
    {
        if (m_state.startClose()) {
            ClosingInThisThreadSet::Item thisAOContexItem(this);

            // CloseHandlers can dstroy the AOContext
            // The anchor will protected as from permature destruction
            const auto anchor = refPtrFromRawPtr<AOContextImpl>(this);

            this->waitForClosing();
            m_state.setClosingFlag();

            this->callCloseHandlers();

            if (m_parent != nullptr) {
                m_parent->removeCloseHandler(this);
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

    void addCloseHandler(AOContextCloseHandler* closeHandler)
    {
        if (!m_state.blockClose()) {
            throw AOContextClosed();
        }

        this->addCloseHandlerNonBlockClose(closeHandler);

        m_state.unblockClose();
    }

    void removeCloseHandler(AOContextCloseHandler* closeHandler) noexcept
    {
        m_state.lockCloseHandlerList();

        if (closeHandler == m_closeHandlerList) {
            // Removal from the head
            m_closeHandlerList = m_closeHandlerList->m_next;
            if (m_closeHandlerList != nullptr) {
                m_closeHandlerList->m_prev = nullptr;
            }

            closeHandler->m_next = nullptr;
            m_state.unlockCloseHandlerList();
            return;
        }

        if (closeHandler->m_prev != nullptr) {
            // Removal from the middle or tail
            closeHandler->m_prev->m_next = closeHandler->m_next;
            if (closeHandler->m_next != nullptr) {
                closeHandler->m_next->m_prev = closeHandler->m_prev;
            }

            closeHandler->m_next = nullptr;
            closeHandler->m_prev = nullptr;
            m_state.unlockCloseHandlerList();
            return;
        }

        // closeHandler is not in the list, so must have been removed by a call to callCloseHandler
        if (ClosingInThisThreadSet::contains(this)) {
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
    /* Defines a group of AOContextGroup that work strictly sequentially
       (through SequenceExecutor ot the root AOContext)  */
    using AOContextGroupId = std::uintptr_t;
    using WorkingInThisThreadSet = StackSet<AOContextGroupId>;

    using ClosingInThisThreadSet = StackSet<const AOContextImpl*>;

    explicit AOContextImpl(Executor& executor)
      : m_groupId(reinterpret_cast<AOContextGroupId>(this))   // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
      , m_executorHolder(makeStrand(executor))
    {}

    explicit AOContextImpl(AOContextImpl* parent)
      : m_groupId(parent->m_groupId)
      , m_executorHolder(makeStrand(parent->executor()))
      , m_parent(parent)
    {
        parent->addCloseHandler(this);
    }

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
        if (ClosingInThisThreadSet::contains(this)) {
            // We can't wait, close was called recursively.
            return;
        }

        if (this->aoContextWorkInThisThread()) {
            // We can't wait, closing is done from exec and we will get a deadlock.
            return;
        }

        this->m_state.waitForClosed();
    }

    void callCloseHandlers()
    {
        m_state.lockCloseHandlerList();

        while (m_closeHandlerList != nullptr) {
            auto* curCloseHandler = m_closeHandlerList;
            m_closeHandlerList = m_closeHandlerList->m_next;
            if (m_closeHandlerList != nullptr) {
                m_closeHandlerList->m_prev = nullptr;
            }

            curCloseHandler->m_next = nullptr;
            bool destroyed = false;
            curCloseHandler->m_destroyed = &destroyed;

            m_state.unlockCloseHandlerList();
            curCloseHandler->aoContextClose();
            m_state.lockCloseHandlerList();

            if (!destroyed) {
                curCloseHandler->m_destroyed = nullptr;
                curCloseHandler->m_done = true;
            }
        }

        m_state.unlockCloseHandlerList();
    }

    [[nodiscard]] std::size_t blockCloseCountMadeFromThisThread() const noexcept
    {
        return WorkingInThisThreadSet::count(m_groupId);
    }

    void aoContextClose() noexcept override
    {
        /* Parent was closed */
        assert(m_parent != nullptr);   // NOLINT

        // Fix for deadlock reproducible by "AOContext.ConcurentCloseChildAndParent"
        //
        // How to get the deadlock:
        // Thread1:
        // parent.close -> child.aoContextClose -> child.close -> wait for child.close finished
        //
        // Thread2:
        // child.close -> parent.removeCloseHandler(this) -> wait for child.aoContextClose finished
        //
        // parent.removeCloseHandler waits for end of child.aoContextClose to exclude the deletion
        // of child while it used in child.aoContextClose.
        //
        // To avoid deadlock, we allow not to wait for the compeltion of child.aoContextClose
        // (this->m_done = true)
        // The anchor will protected as from permature destruction
        const auto anchor = refPtrFromRawPtr<AOContextImpl>(this);
        *this->m_destroyed = true;
        this->m_done = true;

        this->close();
    }

    void addCloseHandlerNonBlockClose(AOContextCloseHandler* closeHandler) noexcept
    {
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

    template<typename StartFn>
    void startCancellableTaskNonBlockClose(StartFn&& start, AOContextCloseHandler* closeHandler)
    {
        this->addCloseHandlerNonBlockClose(closeHandler);
        try {
            start();
        } catch (...) {
            this->removeCloseHandler(closeHandler);
            throw;
        }
    }

    AOContextState m_state;
    const AOContextGroupId m_groupId;

    SequenceExecutorHolder m_executorHolder;

    AOContextCloseHandler* m_closeHandlerList = nullptr;
    AOContextImpl* m_parent = nullptr;
};

}   // namespace nhope::detail
