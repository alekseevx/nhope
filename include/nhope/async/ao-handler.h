#pragma once

#include <climits>
#include <cstdint>

namespace nhope {

using AOHandlerId = std::uint64_t;
inline constexpr AOHandlerId invalidId = UINT64_MAX;

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

}   // namespace nhope
