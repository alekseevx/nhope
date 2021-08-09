#pragma once

#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

#include "nhope/async/ao-handler.h"
#include "nhope/async/future.h"

namespace nhope::detail {

template<typename Result, typename Invoke>
class AsyncInvokeAOHandler final : public AOHandler
{
public:
    AsyncInvokeAOHandler(Promise<Result> promise, Invoke invoke)
      : m_promise(std::move(promise))
      , m_invoke(std::move(invoke))
    {}

    void call() override
    {
        try {
            if constexpr (std::is_void_v<Result>) {
                m_invoke();
                m_promise.setValue();
            } else {
                m_promise.setValue(m_invoke());
            }

        } catch (...) {
            m_promise.setException(std::current_exception());
        }
    }

    void cancel() override
    {
        auto exPtr = std::make_exception_ptr(AsyncOperationWasCancelled());
        m_promise.setException(std::move(exPtr));
    }

private:
    nhope::Promise<Result> m_promise;
    Invoke m_invoke;
};

template<typename Result, typename Invoke>
std::unique_ptr<AOHandler> makeAsyncInvokeAOHandler(Promise<Result> promise, Invoke invoke)
{
    return std::make_unique<AsyncInvokeAOHandler<Result, Invoke>>(std::move(promise), std::move(invoke));
}

}   // namespace nhope::detail
