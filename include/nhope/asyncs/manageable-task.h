#pragma once

#include <exception>
#include <functional>
#include <future>
#include <memory>

#include <boost/thread/future.hpp>

namespace nhope::asyncs {

class ManageableTaskCtx
{
public:
    virtual ~ManageableTaskCtx() = default;

    virtual void setBeforePause(std::function<bool()>&& beforePause) = 0;
    virtual void setAfterPause(std::function<void()>&& afterPause) = 0;
    virtual void resetAllHandlers() = 0;

    virtual bool checkPoint() = 0;
    [[nodiscard]] virtual bool wasPause() const = 0;
};

class ManageableTask
{
public:
    enum class State
    {
        Running,
        Pausing,
        Paused,
        Resuming,
        Stopping,
        Stopped,
    };

    using TaskFunction = std::function<void(ManageableTaskCtx& ctx)>;

public:
    virtual ~ManageableTask() = default;

    [[nodiscard]] virtual State state() const = 0;

    virtual std::future<void> asyncPause() = 0;
    virtual std::future<void> asyncResume() = 0;
    virtual void asyncStop() = 0;
    virtual std::future<void> asyncWaitForStopped() = 0;

    [[nodiscard]] virtual std::exception_ptr getError() const = 0;

    void pause();
    void resume();
    void stop();
    void waitForStopped();

public:
    static std::unique_ptr<ManageableTask> start(TaskFunction&& function);

    // create task on pause
    static std::unique_ptr<ManageableTask> create(TaskFunction&& function);
};

}   // namespace nhope::asyncs
