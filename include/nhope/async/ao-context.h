#pragma once

#include <memory>
#include <stdexcept>
#include <utility>
#include <string_view>
#include <functional>

namespace nhope {

class Executor;
class SequenceExecutor;

class AsyncOperationWasCancelled final : public std::runtime_error
{
public:
    AsyncOperationWasCancelled();
    explicit AsyncOperationWasCancelled(std::string_view errMessage);
};

/**
 * @class AOContext
 *
 * @brief Контекст для выполнения асинхронных операций на заданном Executor
 * 
 * AOContext решает следующие задачи:
 * - обеспечивает вызов обработчика асинхронной операции в заданном Executor-e
 * - гарантирует, что все обработчики асинхронных операций, запущенные на одном AOContext-е,
 *   будут выполнены последовательно
 * - гарантирует, что при уничтожении контекста все асинхронные операции, запущенные
 *   на контексте, будут отменены а их обработчики вызваны не будут
 *
 */
class AOContext final
{
    template<typename... Args>
    using CompletionHandler = std::function<void(Args...)>;
    using CancelHandler = std::function<void()>;
    using AsyncOperationId = std::uint64_t;

public:
    AOContext(const AOContext&) = delete;
    AOContext& operator=(const AOContext&) = delete;

    AOContext(AOContext&&) noexcept = default;
    AOContext& operator=(AOContext&&) = delete;

    explicit AOContext(Executor& executor);
    ~AOContext();

    SequenceExecutor& executor();

    /**
     * @brief Функция для создания асинхронной операции
     * 
     * @param completionHandler пользовательский обработчик окончания асинхронной операции.
                                Вызывается только на заданном Executor-е.
     * @param cancelHandler     обработчик отмены асинхронной операции. Вызывается в том потоке, где уничтожается контекст.
     *
     * @retval функциональный объект, который должен быть вызван по завершении асинхронной операции.
     */
    template<typename... CompletionArgs>
    CompletionHandler<CompletionArgs...> newAsyncOperation(CompletionHandler<CompletionArgs...> completionHandler,
                                                           CancelHandler cancelHandler)
    {
        auto id = AOContext::makeAsyncOperation(*m_d, std::move(cancelHandler));
        return [id, d = this->m_d, ch = std::move(completionHandler)](CompletionArgs... args) mutable {
            auto packedCH = std::bind(std::move(ch), std::forward<CompletionArgs>(args)...);
            AOContext::asyncOperationFinished(*d, id, std::move(packedCH));
        };
    }

private:
    class Impl;
    static AsyncOperationId makeAsyncOperation(Impl& d, CancelHandler&& cancelHandler);
    static void asyncOperationFinished(Impl& d, AsyncOperationId id, std::function<void()> completionHandler);

private:
    std::shared_ptr<Impl> m_d;
};

}   // namespace nhope
