#include <cassert>

#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>
#include <utility>

#include "nhope/async/future.h"
#include "nhope/async/reverse-lock.h"
#include "nhope/async/manageable-task.h"

namespace {
using namespace nhope;

class ManageableTaskImpl final
  : public ManageableTask
  , public ManageableTaskCtx
{
public:
    ~ManageableTaskImpl() override
    {
        this->asyncStop();
        m_workThread.join();
    }

    explicit ManageableTaskImpl(TaskFunction&& function)
      : m_workThread([this, function = std::move(function)]() mutable {
          this->run(std::move(function));
      })
    {}

    void run(TaskFunction&& function)
    {
        {
            std::unique_lock lock(m_mutex);
            m_stateChangedCV.wait(lock, [this] {
                return m_state == State::Running || m_state == State::Stopping || m_state == State::Pausing;
            });
        }
        std::exception_ptr error;
        try {
            if (checkPoint()) {
                function(*this);
            }
        } catch (...) {
            error = std::current_exception();
        }
        stopped(std::move(error));
    }

    void stopped(std::exception_ptr&& error)
    {
        std::list<Promise<void>> promises;

        {
            std::scoped_lock lock(m_mutex);

            m_state = State::Stopped;
            m_error = error;

            promises.splice(promises.end(), m_pausePromises);
            promises.splice(promises.end(), m_resumePromises);
            promises.splice(promises.end(), m_stopPromises);
        }

        resolvePromises(promises);
    }

    // ManageableTask
    State state() const override
    {
        std::scoped_lock lock(m_mutex);
        return m_state;
    }

    Future<void> asyncPause() override
    {
        Future<void> ret;
        std::list<Promise<void>> outdatedPromises;

        {
            std::scoped_lock lock(m_mutex);

            switch (m_state) {
            case State::Running:
                m_state = State::Pausing;
                m_stateChangedCV.notify_one();
                ret = m_pausePromises.emplace_back().future();
                break;

            case State::Resuming:
                m_state = State::Paused;
                outdatedPromises = std::move(m_resumePromises);
                ret = makeReadyFuture();
                break;

            case State::Pausing:
            case State::Stopping:
                ret = m_pausePromises.emplace_back().future();
                break;

            case State::Paused:
            case State::Waiting:
            case State::Stopped:
                ret = makeReadyFuture();
                break;
            }
        }

        resolvePromises(outdatedPromises);
        return ret;
    }

    Future<void> asyncResume() override
    {
        Future<void> ret;
        std::list<Promise<void>> outdatedPromises;

        {
            std::scoped_lock lock(m_mutex);

            switch (m_state) {
            case State::Stopping:
            case State::Running:
            case State::Stopped:
                ret = makeReadyFuture();
                break;

            case State::Pausing:
                m_state = State::Running;
                outdatedPromises = std::move(m_pausePromises);
                ret = makeReadyFuture();
                break;

            case State::Resuming:
                ret = m_resumePromises.emplace_back().future();
                break;

            case State::Waiting:
                ret = makeReadyFuture();
                m_state = State::Running;
                m_stateChangedCV.notify_one();
                break;

            case State::Paused:
                m_state = State::Resuming;
                m_stateChangedCV.notify_one();
                ret = m_resumePromises.emplace_back().future();
                break;
            }
        }

        resolvePromises(outdatedPromises);
        return ret;
    }

    void asyncStop() override
    {
        std::scoped_lock lock(m_mutex);

        switch (m_state) {
        case State::Waiting:
        case State::Running:
        case State::Pausing:
        case State::Resuming:
        case State::Paused:
            m_state = State::Stopping;
            m_stateChangedCV.notify_one();
            return;

        case State::Stopping:
        case State::Stopped:
            return;
        }
    }

    Future<void> asyncWaitForStopped() override
    {
        std::scoped_lock lock(m_mutex);

        if (m_state == State::Stopped) {
            return makeReadyFuture();
        }

        return m_stopPromises.emplace_back().future();
    }

    std::exception_ptr getError() const override
    {
        std::scoped_lock lock(m_mutex);
        return m_error;
    }

    // ManageableTaskCtx
    void setBeforePause(std::function<bool()> beforePause) override
    {
        m_beforePause = std::move(beforePause);
    }

    void setAfterPause(std::function<void()> afterPause) override
    {
        m_afterPause = std::move(afterPause);
    }

    bool checkPoint() override
    {
        std::unique_lock lock(m_mutex);

        switch (m_state) {
        case State::Running:
            return true;

        case State::Pausing:
            if (this->pauseAllowed()) {
                this->doPause(lock);
            }
            return m_state != State::Stopping;

        case State::Stopping:
            return false;

        default:
            throw std::runtime_error("Invalid state");
        }
    }

    bool pauseAllowed()
    {
        if (!m_beforePause) {
            return true;
        }

        return m_beforePause();
    }

    void doPause(std::unique_lock<std::mutex>& lock)
    {
        assert(lock.owns_lock());   // NOLINT

        this->beginPause(lock);
        while (m_state == State::Paused) {
            m_stateChangedCV.wait(lock);
        }
        this->endPause(lock);
    }

    void beginPause(std::unique_lock<std::mutex>& lock)
    {
        assert(lock.owns_lock());            // NOLINT
        assert(m_state == State::Pausing);   // NOLINT

        m_state = State::Paused;

        auto pausePromises = std::move(m_pausePromises);
        {
            ReverseLock unlock(lock);
            resolvePromises(pausePromises);
        }
    }

    void endPause(std::unique_lock<std::mutex>& lock)
    {
        assert(lock.owns_lock());                                           // NOLINT
        assert(m_state == State::Resuming || m_state == State::Stopping);   // NOLINT

        if (m_state == State::Resuming) {
            m_state = State::Running;
        }

        auto resumePromises = std::move(m_resumePromises);
        {
            ReverseLock unlock(lock);
            resolvePromises(resumePromises);
        }

        if (m_afterPause) {
            m_afterPause();
        }
    }

private:
    std::function<bool()> m_beforePause;
    std::function<void()> m_afterPause;

    mutable std::mutex m_mutex;
    State m_state = State::Waiting;
    std::condition_variable m_stateChangedCV;
    std::list<Promise<void>> m_pausePromises;
    std::list<Promise<void>> m_resumePromises;
    std::list<Promise<void>> m_stopPromises;
    std::exception_ptr m_error;
    std::thread m_workThread;
};

}   // namespace

void ManageableTask::pause()
{
    this->asyncPause().wait();
}

void ManageableTask::resume()
{
    this->asyncResume().wait();
}

void ManageableTask::stop()
{
    this->asyncStop();
    this->waitForStopped();
}

void ManageableTask::waitForStopped()
{
    this->asyncWaitForStopped().wait();
}

std::unique_ptr<ManageableTask> ManageableTask::start(TaskFunction function)
{
    auto task = create(std::move(function));
    task->resume();
    return task;
}

std::unique_ptr<ManageableTask> ManageableTask::create(TaskFunction function)
{
    return std::make_unique<ManageableTaskImpl>(std::move(function));
}
