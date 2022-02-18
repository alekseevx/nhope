#include <utility>

#include "nhope/async/all.h"
#include "nhope/async/task.h"

namespace nhope {

Future<void> run(AOContext& ctx, Tasks tasks)
{
    return all(
      ctx,
      [](AOContext& ctx, const Task& task) {
          return task(ctx);
      },
      std::move(tasks));
}

}   // namespace nhope
