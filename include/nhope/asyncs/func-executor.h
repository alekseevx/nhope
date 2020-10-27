#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>
#include <stdexcept>

#include <boost/thread/exceptional_ptr.hpp>
#include <boost/thread/future.hpp>

namespace nhope::asyncs {

class FuncExecutorWasStopped : std::runtime_error
{
public:
    FuncExecutorWasStopped()
      : std::runtime_error("FuncExecutorWasStopped")
    {}
};

template<typename Fn, typename... Args>
using CallResult = std::invoke_result_t<Fn, Args...>;

template<typename Fn, typename... Args>
using AsyncCallResult = boost::future<CallResult<Fn, Args...>>;

template<typename Fn, typename... Args>
using AsyncCallPromise = boost::promise<CallResult<Fn, Args...>>;

template<typename Executor>
class FuncExecutor final
{
public:
    explicit FuncExecutor(Executor& executor)
      : m_executor(executor)
    {
        m_stopped = std::make_shared<std::atomic<bool>>(false);
    }

    ~FuncExecutor()
    {
        *m_stopped = true;
    }

    template<typename Fn, typename... Args>
    CallResult<Fn, Args...> call(Fn&& fn, Args&&... args)
    {
        auto future = this->asyncCall<Fn, Args...>(std::forward<Fn>(fn), std::forward<Args>(args)...);
        if constexpr (std::is_void_v<CallResult<Fn, Args...>>) {
            future.get();
            return;
        } else {
            return future.get();
        }
    }

    template<typename Fn, typename... Args>
    AsyncCallResult<Fn, Args...> asyncCall(Fn&& fn, Args&&... args)
    {
        auto promise = std::make_shared<AsyncCallPromise<Fn, Args...>>();
        auto future = promise->get_future();

        auto func = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);
        m_executor.post([stopped = m_stopped, promise = std::move(promise), func = std::move(func)] {
            if (*stopped) {
                auto exPtr = boost::make_exceptional(FuncExecutorWasStopped());
                promise->set_exception(exPtr);
                return;
            }

            try {
                if constexpr (std::is_void_v<CallResult<Fn, Args...>>) {
                    func();
                    promise->set_value();
                } else {
                    promise->set_value(func());
                }
            } catch (...) {
                auto exPtr = boost::current_exception();
                promise->set_exception(exPtr);
            }
        });

        return future;
    }

private:
    Executor& m_executor;
    std::shared_ptr<std::atomic<bool>> m_stopped;
};

}   // namespace nhope::asyncs
