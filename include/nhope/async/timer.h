#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <system_error>
#include <utility>

#include "nhope/async/future.h"
#include "nhope/utils/detail/ref-ptr.h"
#include "nhope/async/detail/future-state.h"

namespace nhope {

void setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout,
                std::function<void(const std::error_code&)> handler);

Future<void> setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout);

template<typename T>
Future<T> setTimeout(AOContext& aoCtx, Future<T> future, std::chrono::nanoseconds timeout)
{
    auto timedPromise = std::make_shared<Promise<T>>();
    auto f = timedPromise->future();
    setTimeout(aoCtx, timeout)
      .then([srcFutureState = future.shareState()] {
          srcFutureState->cancel();
      })
      .fail([srcFutureState = future.shareState()](auto ex) {
          try {
              std::rethrow_exception(ex);
          } catch (const AsyncOperationWasCancelled&) {
              // aoCtx was closed
              srcFutureState->cancel();
          }
      });

    if constexpr (std::is_void_v<T>) {
        future
          .then([timedPromise] {
              timedPromise->setValue();
          })
          .fail([timedPromise](auto e) {
              timedPromise->setException(e);
          });
    } else {
        future
          .then([timedPromise](auto v) {
              timedPromise->setValue(std::forward<decltype(v)>(v));
          })
          .fail([timedPromise](auto e) {
              timedPromise->setException(e);
          });
    }

    return f;
}

/**
 * Starts a periodic timer, which is triggered at a specified time interval.
 * 
 * The timer will stop in one of the following cases:
 * - Destroying AOContext;
 * - handler returned false;
 * - timer was broken (check error_code)
 */
void setInterval(AOContext& aoCtx, std::chrono::nanoseconds interval,
                 std::function<bool(const std::error_code& err)> handler);

}   // namespace nhope
