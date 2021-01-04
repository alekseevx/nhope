#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

// FIXME: get rid of boost::exception_detail::clone_base
#include <boost/exception/exception.hpp>

namespace nhope {

class ThreadExecutor;

class AsyncOperationWasCancelled
  : public std::runtime_error
  // FIXME: get rid of boost::exception_detail::clone_base
  , public virtual boost::exception_detail::clone_base
{
public:
    AsyncOperationWasCancelled();

public:   // FIXME: get rid of boost::exception_detail::clone_base
    [[nodiscard]] AsyncOperationWasCancelled* clone() const override;
    void rethrow() const override;
};

/**
 * @class AOContext
 *
 * @brief Контекст для выполнения асинхронных операций на заданном ThreadExecutor
 * 
 * AOContext решает следующие задачи:
 * - обеспечивает вызов CompletionHandler в ThreadExecutor
 * - гарантирует, что при уничтожении контекста все асинхронные операции, запущенные
 *   на контексте, будут отменены а их CompletionHandler вызваны не будут
 *
 */
class AOContext final
{
public:
    template<typename... Args>
    using CompletionHandler = std::function<void(Args...)>;
    using CancelHandler = std::function<void()>;

    AOContext(const AOContext&) = delete;
    AOContext& operator=(const AOContext&) = delete;

    AOContext(AOContext&&) = default;
    AOContext& operator=(AOContext&&) = delete;

    explicit AOContext(ThreadExecutor& executor);
    ~AOContext();

    ThreadExecutor& executor();

    /**
     * @brief Функция для создания асинхронной операции
     * 
     * @param completionHandler пользовательский обработчик окончания асинхронной операции.
                                Вызывается только в потоке ThreadExecutor-а.
     * @param cancelHandler     обработчик отмены асинхронной операции. Вызывается в том потоке, где уничтожается контекст.
     *
     * @retval функциональный объект, который должен быть вызван по завершении асинхронной операции.
     */
    template<typename... CompletionArgs>
    CompletionHandler<CompletionArgs...> newAsyncOperation(CompletionHandler<CompletionArgs...> completionHandler,
                                                           CancelHandler cancelHandler)
    {
        auto id = makeAsyncOperation(m_d, std::move(cancelHandler));
        return [id, d = this->m_d, ch = std::move(completionHandler)](CompletionArgs... args) mutable {
            auto packedCH = std::bind(std::move(ch), std::forward<CompletionArgs>(args)...);
            asyncOperationFinished(d, id, std::move(packedCH));
        };
    }

private:
    class Impl;
    using AsyncOperationId = std::uint64_t;

    static AsyncOperationId makeAsyncOperation(std::shared_ptr<Impl>& d, CancelHandler);
    static void asyncOperationFinished(std::shared_ptr<Impl>& d, AsyncOperationId id,
                                       std::function<void()> completionHandler);

    std::shared_ptr<Impl> m_d;
};

}   // namespace nhope
