#include <gtest/gtest.h>

#include "nhope/async/ao-context.h"
#include "nhope/async/future.h"
#include "nhope/async/task.h"
#include "nhope/async/thread-executor.h"

using namespace nhope;

TEST(task, all)   // NOLINT
{
    ThreadExecutor executor;
    AOContext ao(executor);

    auto future = run(ao, {
                            [](AOContext&) {
                                return toThread([] {});
                            },
                            [](AOContext&) {
                                return toThread([] {});
                            },
                            [](AOContext&) {
                                return toThread([] {});
                            },
                          });

    EXPECT_NO_THROW(future.get());   // NOLINT
}
