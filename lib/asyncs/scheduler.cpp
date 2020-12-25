#include <algorithm>
#include <memory>

#include <nhope/asyncs/async-invoke.h>
#include <nhope/asyncs/ao-context.h>
#include <nhope/asyncs/thread-executor.h>
#include <nhope/asyncs/scheduler.h>
#include <nhope/asyncs/future.h>
#include <optional>

namespace nhope::asyncs {

namespace {}   // namespace

class Scheduler::Impl
{
    struct Task : boost::noncopyable
    {
        Task(TaskId id, std::unique_ptr<ManageableTask>&& ptr, int pr) noexcept
          : id(id)
          , task(std::move(ptr))
          , priority(pr)
        {}
        ~Task()
        {
            resolvePromises(stopPromises);
            resolvePromises(waitPromises);
        }

        TaskId id{};
        std::unique_ptr<ManageableTask> task;
        int priority{};

        std::list<Promise<void>> stopPromises;
        std::list<Promise<void>> waitPromises;

        [[nodiscard]] bool wasCancelled() const
        {
            return !stopPromises.empty();
        }

        bool operator<(const Task& r) const noexcept
        {
            return priority < r.priority;
        }

        Future<void> cancelLater()
        {
            return stopPromises.emplace_back().future();
        }
    };

public:
    Impl() noexcept
      : m_ao(m_executor)
    {}

    ~Impl()
    {
        assert(m_queue.empty());   // NOLINT
    };

    TaskId push(int priority, ManageableTask::TaskFunction&& task)
    {
        auto newTask = createTask(priority, std::move(task));
        auto res{newTask->id};

        if (m_activeTask == nullptr) {
            assert(m_queue.empty());   // NOLINT

            m_activeTask = std::move(newTask);
            m_activeTask->task->resume();

        } else if (m_activeTask->priority < priority) {
            m_activeTask->task->pause();
            m_queue.emplace_back(std::move(m_activeTask));

            m_activeTask = std::move(newTask);
            m_activeTask->task->resume();
        } else {
            auto taskPtrComparator = [](const std::unique_ptr<Task>& lhs, const std::unique_ptr<Task>& rhs) {
                return *lhs < *rhs;
            };
            auto it = std::lower_bound(m_queue.begin(), m_queue.end(), newTask, taskPtrComparator);
            m_queue.insert(it, std::move(newTask));
        }

        return res;
    }

    [[nodiscard]] std::optional<TaskId> getActiveTaskId() const noexcept
    {
        if (m_activeTask != nullptr) {
            return m_activeTask->id;
        }
        return std::nullopt;
    }

    Future<void> makeWaitAllPromise()
    {
        if (m_activeTask == nullptr) {
            return makeReadyFuture();
        }
        return m_waitStopPromises.emplace_back().future();
    }

    Future<void> waitTask(TaskId id)
    {
        if (m_activeTask != nullptr && m_activeTask->id == id) {
            return m_activeTask->waitPromises.emplace_back().future();
        }
        if (auto* task = findTask(id); task != nullptr) {
            return task->waitPromises.emplace_back().future();
        }
        return makeReadyFuture();
    }

    Future<void> cancelTask(TaskId id)
    {
        if (m_activeTask != nullptr && m_activeTask->id == id) {
            m_activeTask->task->asyncStop();
            return m_activeTask->task->asyncWaitForStopped();
        }
        if (auto* task = findTask(id); task != nullptr) {
            return task->cancelLater();
        }

        return makeReadyFuture();
    }

    Future<void> clear()
    {
        Future<void> future = makeReadyFuture();
        if (m_activeTask != nullptr) {
            future = m_activeTask->task->asyncWaitForStopped();
            m_activeTask->task->asyncStop();
        }

        for (auto it = m_queue.rbegin(); it != m_queue.rend(); it++) {
            std::unique_ptr<ManageableTask>& task = (*it)->task;
            future = future.thenValue(m_ao, [&task]() mutable {
                task->asyncStop();
                return task->asyncWaitForStopped();
            });
        };

        return future;
    }

private:
    friend class Scheduler;
    using TaskList = std::list<std::unique_ptr<Task>>;

    Task* findTask(TaskId id)
    {
        if (auto it = std::find_if(m_queue.begin(), m_queue.end(),
                                   [id](const auto& t) {
                                       return t->id == id;
                                   });
            it != m_queue.end()) {
            return (*it).get();
        }
        return nullptr;
    }

    std::unique_ptr<Task> createTask(int priority, ManageableTask::TaskFunction&& task)
    {
        auto newTask = std::make_unique<Task>(m_idCounter++, ManageableTask::create(std::move(task)), priority);

        // finished task processing
        auto taskFinished = newTask->task->asyncWaitForStopped();
        taskFinished.thenValue(m_ao, [this, finishedId = newTask->id] {
            if (finishedId != m_activeTask->id) {
                eraseTask(finishedId);

            } else {
                assert(m_activeTask->task->state() == ManageableTask::State::Stopped);   //NOLINT
                m_activeTask = nullptr;
                resumeNextTask();
            }
        });
        return newTask;
    }

    void eraseTask(TaskId id)
    {
        if (auto it = std::find_if(m_queue.begin(), m_queue.end(),
                                   [id](const auto& t) {
                                       return t->id == id;
                                   });
            it != m_queue.end()) {
            assert((*it)->task->state() == ManageableTask::State::Stopped);   // NOLINT
            m_queue.erase(it);
        }
    }

    void resumeNextTask()
    {
        assert(m_activeTask == nullptr);   // NOLINT

        if (!m_queue.empty()) {
            m_activeTask = std::move(m_queue.back());
            m_queue.pop_back();
            if (m_activeTask->wasCancelled()) {
                m_activeTask->task->stop();

            } else {
                m_activeTask->task->resume();
            }
        } else {
            resolvePromises(m_waitStopPromises);
        }
    }

    TaskList m_queue;
    std::unique_ptr<Task> m_activeTask;
    TaskId m_idCounter{0};
    std::list<Promise<void>> m_waitStopPromises;

    ThreadExecutor m_executor;
    AOContext m_ao;
};

Scheduler::Scheduler()
  : m_impl(std::make_unique<Scheduler::Impl>())
{}

Scheduler::~Scheduler()
{
    clear();
}

Scheduler::TaskId Scheduler::push(ManageableTask::TaskFunction&& task, int priority)
{
    return invoke(m_impl->m_ao, [this, priority, &task] {
        return m_impl->push(priority, std::move(task));
    });
}

std::optional<Scheduler::TaskId> Scheduler::getActiveTaskId() const noexcept
{
    return invoke(m_impl->m_ao, [this] {
        return m_impl->getActiveTaskId();
    });
}

Future<void> Scheduler::asyncWait(TaskId id)
{
    return asyncInvoke(m_impl->m_ao, [this, id] {
        return m_impl->waitTask(id);
    });
}

void Scheduler::wait(TaskId id)
{
    asyncWait(id).get();
}

Future<void> Scheduler::asyncWaitAll()
{
    return asyncInvoke(m_impl->m_ao, [this] {
        return m_impl->makeWaitAllPromise();
    });
}

void Scheduler::waitAll()
{
    asyncWaitAll().get();
}

Future<void> Scheduler::asyncCancel(TaskId id)
{
    return asyncInvoke(m_impl->m_ao, [this, id] {
        return m_impl->cancelTask(id);
    });
}

void Scheduler::cancel(TaskId id)
{
    asyncCancel(id).get();
}

Future<void> Scheduler::asyncClear()
{
    return asyncInvoke(m_impl->m_ao, [this] {
        return m_impl->clear();
    });
}

void Scheduler::clear()
{
    asyncClear().get();
}

}   // namespace nhope::asyncs
