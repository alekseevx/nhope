#pragma once

#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace nhope {

class Executor;
class SequenceExecutor;

class AsyncOperationWasCancelled final : public std::runtime_error
{
public:
    AsyncOperationWasCancelled();
    explicit AsyncOperationWasCancelled(std::string_view errMessage);
};

class AOContextClosed final : public std::runtime_error
{
public:
    AOContextClosed();
};

using AOHandlerId = std::uint64_t;

/**
 * @brief Обработчик асинхронной  операции.
 */
class AOHandler
{
public:
    virtual ~AOHandler() = default;

    /**
     * @brief Обработчик асинхронной операции.
     */
    virtual void call() = 0;

    /**
     * @brief Вызывается при уничтожении AOContext-а.
     */
    virtual void cancel() = 0;
};

namespace detail {
class AOContextImpl;
}

/**
 * @brief Класс для вызова AOHandler в контексте AOContext.
 */
class AOHandlerCall final
{
    friend class detail::AOContextImpl;

public:
    AOHandlerCall() = default;

    operator bool() const;

    /**
     * @brief Вызов AOHandler-а в контексте AOContext-а.
     * @note Вызов можно произвести только один раз.
     */
    void operator()();

private:
    using AOContextImplWPtr = std::weak_ptr<detail::AOContextImpl>;

    AOHandlerCall(AOHandlerId id, AOContextImplWPtr aoImpl);

    AOHandlerId m_id{};
    AOContextImplWPtr m_aoImpl;
};

/**
 * @class AOContext
 *
 * @brief Контекст для выполнения асинхронных операций на заданном Executor
 * 
 * AOContext решает следующие задачи:
 * - обеспечивает вызов обработчика асинхронной операции (#AOHandler::call) в заданном Executor-e
 * - гарантирует, что все обработчики асинхронных операций, запущенные на одном AOContext-е,
 *   будут выполнены последовательно
 * - гарантирует, что при уничтожении контекста все асинхронные операции, запущенные
 *   на контексте, будут отменены (#AOHandler::cancel) а их обработчики вызваны не будут
 *   (#AOHandler::call)
 */
class AOContext final
{
    friend class AOContextWeekRef;

public:
    AOContext(const AOContext&) = delete;
    AOContext& operator=(const AOContext&) = delete;

    AOContext(AOContext&&) noexcept = default;
    AOContext& operator=(AOContext&&) = delete;

    explicit AOContext(Executor& executor);
    ~AOContext();

    SequenceExecutor& executor();

    /**
     * Помещает AOHandler в AOContext.
     */
    AOHandlerCall putAOHandler(std::unique_ptr<AOHandler> handler);

private:
    using AOContextImplPtr = std::shared_ptr<detail::AOContextImpl>;
    AOContextImplPtr m_d;
};

class AOContextWeekRef final
{
public:
    explicit AOContextWeekRef(AOContext& aoCtx);

    AOHandlerCall putAOHandler(std::unique_ptr<AOHandler> handler);

private:
    using AOContextImplWPtr = std::weak_ptr<detail::AOContextImpl>;
    AOContextImplWPtr m_aoImpl;
};

}   // namespace nhope
