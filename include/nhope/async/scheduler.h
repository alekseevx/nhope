#pragma once

#include <cstdint>
#include <optional>

#include <nhope/async/manageable-task.h>
#include <nhope/utils/noncopyable.h>
#include "nhope/utils/detail/fast-pimpl.h"

namespace nhope {

class Scheduler final : Noncopyable
{
public:
    using TaskId = uint64_t;

    Scheduler();
    ~Scheduler();

    TaskId push(ManageableTask::TaskFunction task, int priority = 0);

    [[nodiscard]] std::optional<TaskId> getActiveTaskId() const noexcept;

    [[nodiscard]] std::optional<ManageableTask::State> getState(TaskId id) const noexcept;

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

    [[nodiscard]] std::size_t size() const noexcept;

private:
    class Impl;
    static constexpr std::size_t implSize{176};
    detail::FastPimpl<Impl, implSize> m_impl;
};

}   // namespace nhope
