#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/detail/future.h"
#include "nhope/async/future.h"
#include "nhope/async/async-invoke.h"

namespace nhope {

class CallQueue final
{
public:
    explicit CallQueue(AOContext& ctx)
      : m_nextWork(makeReadyFuture())
      , m_ctx(ctx.executor())   // TODO Сделать дочерний ао контекст
    {}

    template<typename Fn, typename... Args>
    auto push(Fn&& fn, Args&&... args)
    {
        auto binded = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);

        using Rt = typename std::invoke_result_t<decltype(binded)>;
        using ResultType = typename UnwrapFuture<Rt>::Type;

        auto promise = std::make_shared<Promise<ResultType>>();
        auto result = promise->future();

        asyncInvoke(m_ctx, [this, promise, fn = std::move(binded)]() mutable {
            auto complete = m_nextWork.then(m_ctx, [this, fn = std::move(fn)]() mutable {
                return fn();
            });

            resolve(std::move(complete), std::move(promise));
        });

        return result;
    }

private:
    template<typename T>
    void resolve(Future<T> f, std::shared_ptr<Promise<T>> p)
    {
        if constexpr (std::is_void_v<T>) {
            m_nextWork = f.then(m_ctx,
                                [p] {
                                    p->setValue();
                                })
                           .fail(m_ctx, [p](auto ex) {
                               p->setException(ex);
                           });
        } else {
            m_nextWork = f.then(m_ctx,
                                [p](T v) {
                                    p->setValue(std::move(v));
                                })
                           .fail(m_ctx, [p](auto ex) {
                               p->setException(ex);
                           });
        }
    }

    Future<void> m_nextWork;
    AOContext m_ctx;
};

}   // namespace nhope