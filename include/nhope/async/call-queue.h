#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/async-invoke.h"
#include "nhope/async/future.h"

namespace nhope {

class CallQueue final
{
public:
    explicit CallQueue(AOContext& parentCtx)
      : m_callChain(makeReadyFuture())
      , m_ctx(parentCtx)
    {}

    ~CallQueue()
    {
        m_ctx.close();
    }

    template<typename Fn, typename... Args>
    auto push(Fn&& fn, Args&&... args)
    {
        using Result = typename UnwrapFuture<std::invoke_result_t<Fn, Args...>>::Type;

        auto binded = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);
        return asyncInvoke(m_ctx, [this, fn = std::move(binded)]() mutable {
            auto resultPromise = std::make_shared<Promise<Result>>();

            auto callFuture = m_callChain.then(m_ctx, std::move(fn));
            if constexpr (std::is_void_v<Result>) {
                m_callChain = callFuture.then(m_ctx, [resultPromise] {
                    resultPromise->setValue();
                });
            } else {
                m_callChain = callFuture.then(m_ctx, [resultPromise](auto value) {
                    resultPromise->setValue(std::move(value));
                });
            }

            m_callChain = m_callChain.fail(m_ctx, [resultPromise](auto ex) {
                resultPromise->setException(std::move(ex));
            });

            return resultPromise->future();
        });
    }

private:
    Future<void> m_callChain;
    AOContext m_ctx;
};

}   // namespace nhope