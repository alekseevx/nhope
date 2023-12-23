#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/future.h"

namespace nhope {

class CallQueue final
{
public:
    explicit CallQueue()
      : m_callChain(makeReadyFuture())
    {}

    ~CallQueue()
    {
        m_callChain.cancel();
    }

    template<typename Fn, typename... Args>
    auto push(AOContext& ctx, Fn&& fn, Args&&... args)
    {
        using Result = typename UnwrapFuture<std::invoke_result_t<Fn, Args...>>::Type;
        auto resultPromise = std::make_shared<Promise<Result>>();
        auto callFuture = m_callChain.then(ctx, std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));
        if constexpr (std::is_void_v<Result>) {
            m_callChain = callFuture.then(ctx, [resultPromise] {
                resultPromise->setValue();
            });
        } else {
            m_callChain = callFuture.then(ctx, [resultPromise](auto&& value) {
                resultPromise->setValue(std::move(value));
            });
        }
        m_callChain = m_callChain.fail(ctx, [resultPromise](auto&& ex) {
            resultPromise->setException(std::move(ex));
        });

        return resultPromise->future();
    }

private:
    Future<void> m_callChain;
};

}   // namespace nhope
