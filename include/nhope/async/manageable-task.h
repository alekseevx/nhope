#pragma once

#include <exception>
#include <functional>
#include <future>
#include <memory>

#include "nhope/async/future.h"

namespace nhope {

class ManageableTaskCtx
{
public:
    virtual ~ManageableTaskCtx() = default;

    virtual void setBeforePause(std::function<bool()> beforePause) = 0;
    virtual void setAfterPause(std::function<void()> afterPause) = 0;

    virtual bool checkPoint() = 0;
};

class ManageableTask
{
public:
    enum class State
    {
        Waiting,
        Running,
        Pausing,
        Paused,
        Resuming,
        Stopping,
        Stopped,
    };

    using TaskFunction = std::function<void(ManageableTaskCtx& ctx)>;

    virtual ~ManageableTask() = default;

    [[nodiscard]] virtual State state() const = 0;

    virtual Future<void> asyncPause() = 0;
    virtual Future<void> asyncResume() = 0;
    virtual void asyncStop() = 0;
    virtual Future<void> asyncWaitForStopped() = 0;

    [[nodiscard]] virtual std::exception_ptr getError() const = 0;

    void pause();
    void resume();
    void stop();
    void waitForStopped();

    static std::unique_ptr<ManageableTask> start(TaskFunction function);

    // create task on pause
    static std::unique_ptr<ManageableTask> create(TaskFunction function);
};

}   // namespace nhope
