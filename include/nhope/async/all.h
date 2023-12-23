#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "nhope/async/ao-context.h"
#include "nhope/async/future.h"

namespace nhope {

namespace detail {

template<typename ResT, typename ArgT>
class AllOpForVec final : public std::enable_shared_from_this<AllOpForVec<ResT, ArgT>>
{
public:
    explicit AllOpForVec(AOContext& parent)
      : m_ctx(parent)
    {}

    template<typename Fn>
    Future<std::vector<ResT>> start(Fn fn, std::vector<ArgT> args)
    {
        auto future = m_promise.future();
        if (args.empty()) {
            m_promise.setValue(std::vector<ResT>{});
            return future;
        }

        m_result.resize(args.size());
        try {
            for (size_t i = 0; i < args.size(); ++i) {
                fn(m_ctx, args[i])
                  .then(m_ctx,
                        [i, self = this->shared_from_this()](auto r) {
                            self->taskFinished(i, std::move(r));
                        })
                  .fail(m_ctx, [self = this->shared_from_this()](auto e) {
                      self->taskFailed(std::move(e));
                  });
            }
        } catch (...) {
            m_ctx.close();
            return makeExceptionalFuture<std::vector<ResT>>(std::current_exception());
        }

        return future;
    }

    static std::shared_ptr<AllOpForVec> create(AOContext& parent)
    {
        return std::make_shared<AllOpForVec>(parent);
    }

private:
    void taskFinished(std::size_t i, ResT&& r)
    {
        m_result[i] = std::move(r);
        if (++m_taskFinishedCount == m_result.size()) {
            m_promise.setValue(std::move(m_result));
        }
    }

    void taskFailed(std::exception_ptr e)
    {
        m_promise.setException(std::move(e));
        m_ctx.close();
    }

    Promise<std::vector<ResT>> m_promise;
    std::vector<ResT> m_result{};
    std::size_t m_taskFinishedCount = 0;
    AOContext m_ctx;
};

template<typename ArgT>
class AllOpForVec<void, ArgT> final : public std::enable_shared_from_this<AllOpForVec<void, ArgT>>
{
public:
    explicit AllOpForVec(AOContext& parent)
      : m_ctx(parent)
    {}

    template<typename Fn>
    Future<void> start(Fn fn, std::vector<ArgT> args)
    {
        auto future = m_promise.future();
        if (args.empty()) {
            m_promise.setValue();
            return future;
        }

        m_taskCount = args.size();
        try {
            for (size_t i = 0; i < args.size(); ++i) {
                fn(m_ctx, args[i])
                  .then(m_ctx,
                        [self = this->shared_from_this()] {
                            self->taskFinished();
                        })
                  .fail(m_ctx, [self = this->shared_from_this()](auto e) {
                      self->taskFailed(std::move(e));
                  });
            }
        } catch (...) {
            m_ctx.close();
            return makeExceptionalFuture(std::current_exception());
        }

        return future;
    }

    static std::shared_ptr<AllOpForVec> create(AOContext& parent)
    {
        return std::make_shared<AllOpForVec>(parent);
    }

private:
    void taskFinished()
    {
        if (++m_taskFinishedCount == m_taskCount) {
            m_promise.setValue();
        }
    }

    void taskFailed(std::exception_ptr e)
    {
        m_promise.setException(std::move(e));
        m_ctx.close();
    }

    Promise<void> m_promise;
    std::size_t m_taskCount = 0;
    std::size_t m_taskFinishedCount = 0;
    AOContext m_ctx;
};

}   // namespace detail

/*!
 * @brief Вызывает пользовательскую функцию для каждого параметра из args
 *
 * возвращает Future<вектор с полученными результатами> или Future<void>,
 * если Fn возвращает Future<void>.
 *
 * @tparam Fn Пользовательская функция должна возвращать Future<T>
 * @tparam ArgT Тип аргументов
 * @param args вектор с аргументами для вызова пользовательской функции
 *
 * @return Future<std::vector<FnRetValType>> или Future<void>, если Fn возвращает Future<void>.
 */
template<typename Fn, typename ArgT>
auto all(AOContext& ctx, Fn&& fn, std::vector<ArgT> args)
{
    using FnProps = FunctionProps<decltype(std::function(std::declval<Fn>()))>;

    using FutureType = typename FnProps::ReturnType;
    static_assert(isFuture<FutureType>, "function must return future");

    using ResT = typename FutureType::Type;
    static_assert(std::is_invocable_v<Fn, AOContext&, ArgT>, "Fn must accept AOContext and ArgT");

    auto op = detail::AllOpForVec<ResT, ArgT>::create(ctx);
    return op->start(std::forward<Fn>(fn), std::move(args));
}

namespace detail {

template<typename... T>
class AllOpForTuple final : public std::enable_shared_from_this<AllOpForTuple<T...>>
{
public:
    explicit AllOpForTuple(AOContext& parent)
      : m_ctx(parent)
    {}

    Future<std::tuple<T...>> start(std::function<Future<T>(AOContext&)>... fn)
    {
        try {
            auto future = m_promise.future();
            this->startTasks<0>(std::move(fn)...);
            return future;
        } catch (...) {
            return makeExceptionalFuture<std::tuple<T...>>(std::current_exception());
        }
    }

    static std::shared_ptr<AllOpForTuple<T...>> create(AOContext& parent)
    {
        return std::make_shared<AllOpForTuple<T...>>(parent);
    }

private:
    static constexpr auto taskCount = sizeof...(T);

    template<std::size_t i, typename HeadT, typename... TailT>
    void startTasks(std::function<nhope::Future<HeadT>(AOContext&)> head,
                    std::function<nhope::Future<TailT>(AOContext&)>... tail)
    {
        this->startTask<i, HeadT>(std::move(head));
        this->startTasks<i + 1>(std::move(tail)...);
    }

    template<std::size_t i, typename LastT>
    void startTasks(std::function<nhope::Future<LastT>(AOContext&)> last)
    {
        this->startTask<i, LastT>(std::move(last));
    }

    template<std::size_t i, typename Tp>
    void startTask(std::function<nhope::Future<Tp>(AOContext&)> fn)
    {
        fn(m_ctx)
          .then(m_ctx,
                [self = this->shared_from_this()](auto value) {
                    self->template taskFinished<i, Tp>(std::move(value));
                })
          .fail(m_ctx, [self = this->shared_from_this()](auto ex) {
              self->taskFailed(std::move(ex));
          });
    }

    template<std::size_t i, typename Tp>
    void taskFinished(Tp r)
    {
        std::get<i>(m_result) = std::move(r);

        if (++m_taskFinishedCount == taskCount) {
            m_promise.setValue(std::move(m_result));
        }
    }

    void taskFailed(std::exception_ptr e)
    {
        m_ctx.close();
        m_promise.setException(std::move(e));
    }

    std::size_t m_taskFinishedCount = 0;
    std::tuple<T...> m_result;
    Promise<std::tuple<T...>> m_promise;
    AOContext m_ctx;
};

}   // namespace detail

/*!
 * @brief Запускает на параллельное выполнение переданные функции, дожидается их завершения и возвращает 
 *        tuple с полученными результатами.
 *
 * @return Future<std::tuple<T...>>
 */
template<typename... T>
Future<std::tuple<T...>> all(AOContext& ctx, std::function<Future<T>(AOContext&)>... fn)
{
    constexpr auto taskCount = sizeof...(T);
    if constexpr (taskCount == 0) {
        return makeReadyFuture<std::tuple<>>();
    } else {
        auto op = detail::AllOpForTuple<T...>::create(ctx);
        return op->start(std::move(fn)...);
    }
}

}   // namespace nhope
