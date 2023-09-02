#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>

#include <nhope/async/async-invoke.h>
#include <nhope/async/ao-context.h>
#include <nhope/async/thread-executor.h>
#include <nhope/async/scheduler.h>
#include <nhope/async/future.h>
#include <nhope/utils/noncopyable.h>

namespace nhope {

class Scheduler::Impl
{
    struct Task final : Noncopyable
    {
        Task(TaskId id, std::unique_ptr<ManageableTask> ptr, int pr) noexcept
          : id(id)
          , taskController(std::move(ptr))
          , priority(pr)
        {}

        ~Task()
        {
            resolvePromises(pausePromises);
            resolvePromises(resumePromises);
            resolvePromises(stopPromises);
            resolvePromises(waitPromises);
        }

        [[nodiscard]] ManageableTask::State state() const noexcept
        {
            return taskController->state();
        }

        TaskId id{};
        std::unique_ptr<ManageableTask> taskController;
        int priority{};
        bool isAlreadyStarted{};

        std::list<Promise<void>> pausePromises;
        std::list<Promise<void>> resumePromises;
        std::list<Promise<void>> stopPromises;
        std::list<Promise<void>> waitPromises;

        void resume()
        {
            isAlreadyStarted = true;
            if (wasCancelled()) {
                taskController->stop();
            } else {
                taskController->resume();
                resolvePromises(resumePromises);
            }
        }

        void pause()
        {
            taskController->pause();
            resolvePromises(pausePromises);
        }

        [[nodiscard]] bool wasCancelled() const
        {
            return !stopPromises.empty();
        }

        [[nodiscard]] bool wasPaused() const
        {
            return !pausePromises.empty();
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
        assert(m_waitedTasks.empty());    // NOLINT
        assert(m_delayedTasks.empty());   // NOLINT
    };

    TaskId push(int priority, ManageableTask::TaskFunction task)
    {
        auto newTask = createTask(priority, std::move(task));
        auto res{newTask->id};

        schedule(std::move(newTask));

        return res;
    }

    [[nodiscard]] std::optional<TaskId> getActiveTaskId() const noexcept
    {
        if (m_activeTask == nullptr) {
            return std::nullopt;
        }
        return m_activeTask->id;
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
        if (auto&& [task, _it] = findTaskById(m_waitedTasks, id); task != nullptr) {
            return task->waitPromises.emplace_back().future();
        }
        if (auto&& [task, _it] = findTaskById(m_delayedTasks, id); task != nullptr) {
            return task->waitPromises.emplace_back().future();
        }
        return makeReadyFuture();
    }

    Future<void> cancelTask(TaskId id)
    {
        if (m_activeTask != nullptr && m_activeTask->id == id) {
            m_activeTask->taskController->asyncStop();
            return m_activeTask->taskController->asyncWaitForStopped();
        }
        if (auto&& [task, wIt] = findTaskById(m_waitedTasks, id); task != nullptr) {
            auto future = task->cancelLater();
            // если задача находится в списке на запуск и еще не запускалась, она сразу удаляется
            if (!task->isAlreadyStarted) {
                m_waitedTasks.erase(wIt);
            }
            return future;
        }
        if (auto&& [task, it] = findTaskById(m_delayedTasks, id); task != nullptr) {
            auto cancellingTask = std::move(task);
            m_delayedTasks.erase(it);
            auto future = cancellingTask->cancelLater();
            schedule(std::move(cancellingTask));

            return future;
        }
        return makeReadyFuture();
    }

    Future<void> clear()
    {
        Future<void> future = makeReadyFuture();
        if (m_activeTask != nullptr) {
            future = m_activeTask->taskController->asyncWaitForStopped();
            m_activeTask->taskController->asyncStop();
        }

        while (!m_delayedTasks.empty()) {
            queueTask(std::move(m_delayedTasks.back()));
            m_delayedTasks.pop_back();
        }

        for (auto it = m_waitedTasks.rbegin(); it != m_waitedTasks.rend(); it++) {
            std::unique_ptr<ManageableTask>& task = (*it)->taskController;
            future = future.then(m_ao, [&task]() mutable {
                task->asyncStop();
                return task->asyncWaitForStopped();
            });
        };
        future = future.then(m_ao, [this] {
            m_waitedTasks.clear();
        });

        return future;
    }

    Future<void> pause(TaskId id)
    {
        auto future = makeReadyFuture();
        if (m_activeTask != nullptr && m_activeTask->id == id) {
            m_activeTask->pause();
            m_delayedTasks.emplace_back(std::move(m_activeTask));
            m_activeTask = nullptr;

            resumeNextTask();

        } else if (auto&& [waitedTask, _itw] = findTaskById(m_waitedTasks, id); waitedTask != nullptr) {
            future = waitedTask->pausePromises.emplace_back().future();
        }
        return future;
    }

    Future<void> resume(TaskId id)
    {
        if (auto&& [task, _it] = findTaskById(m_waitedTasks, id); task != nullptr) {
            return task->resumePromises.emplace_back().future();
        }

        if (auto&& [task, it] = findTaskById(m_delayedTasks, id); task != nullptr) {
            auto resumingTask = std::move(task);
            m_delayedTasks.erase(it);
            auto ret = resumingTask->resumePromises.emplace_back().future();
            schedule(std::move(resumingTask));
            return ret;
        }
        return makeReadyFuture();
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_waitedTasks.size() + m_delayedTasks.size() + (m_activeTask == nullptr ? 0 : 1);
    }

    [[nodiscard]] std::optional<ManageableTask::State> state(TaskId id) const noexcept
    {
        if (m_activeTask != nullptr && m_activeTask->id == id) {
            return m_activeTask->state();
        }
        if (auto&& [task, _it] = findTaskById(m_waitedTasks, id); task != nullptr) {
            return task->state();
        }

        if (auto&& [task, it] = findTaskById(m_delayedTasks, id); task != nullptr) {
            return task->state();
        }
        return std::nullopt;
    }

private:
    friend class Scheduler;
    using TaskList = std::list<std::unique_ptr<Task>>;

    static std::pair<std::unique_ptr<Task>&, TaskList::const_iterator> findTaskById(const TaskList& list, TaskId id)
    {
        static std::unique_ptr<Task> nulTask = nullptr;
        if (const auto it = std::find_if(list.begin(), list.end(),
                                         [id](const auto& t) {
                                             return t->id == id;
                                         });
            it != list.end()) {
            //NOLINTNEXTLINE (cppcoreguidelines-pro-type-const-cast)
            return {const_cast<std::unique_ptr<Task>&>(*it), it};
        }
        return {nulTask, list.end()};
    }

    void schedule(std::unique_ptr<Task> task)
    {
        if (m_activeTask == nullptr) {
            assert(m_waitedTasks.empty());   // NOLINT

            m_activeTask = std::move(task);
            m_activeTask->resume();

        } else if (m_activeTask->priority < task->priority) {
            m_activeTask->pause();
            m_waitedTasks.emplace_back(std::move(m_activeTask));

            m_activeTask = std::move(task);
            m_activeTask->resume();
        } else {
            queueTask(std::move(task));
        }
    }

    void queueTask(std::unique_ptr<Task> task)
    {
        auto taskPtrComparator = [](const std::unique_ptr<Task>& lhs, const std::unique_ptr<Task>& rhs) {
            return *lhs < *rhs;
        };
        auto it = std::lower_bound(m_waitedTasks.begin(), m_waitedTasks.end(), task, taskPtrComparator);
        m_waitedTasks.insert(it, std::move(task));
    }

    std::unique_ptr<Task> createTask(int priority, ManageableTask::TaskFunction task)
    {
        auto newTask = std::make_unique<Task>(m_idCounter++, ManageableTask::create(std::move(task)), priority);

        // finished task processing
        auto taskFinished = newTask->taskController->asyncWaitForStopped();
        taskFinished.then(m_ao, [this, finishedId = newTask->id] {
            if (m_activeTask == nullptr || finishedId != m_activeTask->id) {
                eraseTask(finishedId);
            } else {
                assert(m_activeTask->taskController->state() == ManageableTask::State::Stopped);   //NOLINT
                m_activeTask = nullptr;
                resumeNextTask();
            }
        });
        return newTask;
    }

    void eraseTask(TaskId id)
    {
        auto&& [task, it] = findTaskById(m_waitedTasks, id);
        if (task != nullptr) {
            assert(task->state() == ManageableTask::State::Stopped);   // NOLINT
            m_waitedTasks.erase(it);
            return;
        }
        auto&& [delayedTask, delayedIt] = findTaskById(m_delayedTasks, id);
        if (delayedTask != nullptr) {
            assert(delayedTask->state() == ManageableTask::State::Stopped);   // NOLINT
            m_delayedTasks.erase(delayedIt);
        }
    }

    void resumeNextTask()
    {
        assert(m_activeTask == nullptr);   // NOLINT

        if (!m_waitedTasks.empty()) {
            m_activeTask = std::move(m_waitedTasks.back());
            m_waitedTasks.pop_back();
            if (m_activeTask->wasPaused()) {
                m_activeTask->pause();
                m_delayedTasks.emplace_back(std::move(m_activeTask));
                m_activeTask = nullptr;
                resumeNextTask();
            } else {
                m_activeTask->resume();
            }
        } else if (m_delayedTasks.empty()) {
            resolvePromises(m_waitStopPromises);
        }
    }

    TaskList m_waitedTasks;    // запланированные задачи
    TaskList m_delayedTasks;   // задачи в состоянии пауза, останутся в этом списке пока их не активируют
    std::unique_ptr<Task> m_activeTask;
    TaskId m_idCounter{0};
    std::list<Promise<void>> m_waitStopPromises;

    ThreadExecutor m_executor;
    mutable AOContext m_ao;
};

Scheduler::Scheduler() = default;

Scheduler::~Scheduler()
{
    clear();
}

Scheduler::TaskId Scheduler::push(ManageableTask::TaskFunction task, int priority)
{
    return invoke(m_impl->m_ao, [this, priority, task = std::move(task)]() mutable {
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

Future<void> Scheduler::asyncDeactivate(TaskId id)
{
    return asyncInvoke(m_impl->m_ao, [this, id] {
        return m_impl->pause(id);
    });
}

void Scheduler::deactivate(TaskId id)
{
    asyncDeactivate(id).get();
}

Future<void> Scheduler::asyncActivate(TaskId id)
{
    return asyncInvoke(m_impl->m_ao, [this, id] {
        return m_impl->resume(id);
    });
}

void Scheduler::activate(TaskId id)
{
    asyncActivate(id).get();
}

std::size_t Scheduler::size() const noexcept
{
    return invoke(m_impl->m_ao, [this] {
        return m_impl->size();
    });
}

std::optional<ManageableTask::State> Scheduler::getState(TaskId id) const noexcept
{
    return invoke(m_impl->m_ao, [this, id] {
        return m_impl->state(id);
    });
}

}   // namespace nhope
