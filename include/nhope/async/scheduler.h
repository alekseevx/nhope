#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <boost/noncopyable.hpp>

#include <nhope/async/manageable-task.h>

namespace nhope {

class Scheduler : boost::noncopyable
{
public:
    using TaskId = uint64_t;

    Scheduler();
    ~Scheduler();

    TaskId push(ManageableTask::TaskFunction&& task, int priority = 0);

    [[nodiscard]] std::optional<TaskId> getActiveTaskId() const noexcept;

    void wait(TaskId id);
    Future<void> asyncWait(TaskId id);

    void waitAll();
    Future<void> asyncWaitAll();

    /*!
     * Отменяет задание с указанным идентификатором
     * Отмена произойдет когда придет очередь указанного задания
     */
    void cancel(TaskId id);
    Future<void> asyncCancel(TaskId id);

    /*!
     * Очищает текущую очередь заданий
     */
    void clear();
    Future<void> asyncClear();

    /*!
     * отложить задачу
     */
    void deactivate(TaskId id);
    Future<void> asyncDeactivate(TaskId id);

    /*!
     * Планирует восстановление задачи
     */
    void activate(TaskId id);
    Future<void> asyncActivate(TaskId id);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}   // namespace nhope