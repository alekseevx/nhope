#pragma once

#include <functional>
#include <vector>

#include "nhope/async/ao-context.h"
#include "nhope/async/future.h"

namespace nhope {

using Task = std::function<Future<void>(AOContext& ctx)>;
using Tasks = std::vector<Task>;

/*!
 * @brief Запускает все переданные задачи и асинхронно дожидается их завершения.
 */
Future<void> run(AOContext& ctx, Tasks tasks);

}   // namespace nhope
