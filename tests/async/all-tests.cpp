#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "nhope/async/all.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/future.h"
#include "nhope/async/thread-executor.h"

using namespace nhope;
using namespace std::literals;

TEST(all, vector)   // NOLINT
{
    ThreadExecutor executor;
    AOContext ao(executor);
    {
        auto res = all(
                     ao,
                     [](AOContext&, int x) {
                         return toThread([x] {
                             return x + 2;
                         });
                     },
                     std::vector<int>{})
                     .get();

        EXPECT_TRUE(res.empty());
    }

    const std::vector<int> input{1, 2, 3, 4, 5};
    {
        const std::vector<int> expect{3, 4, 5, 6, 7};

        auto res = all(
                     ao,
                     [](AOContext&, int x) {
                         return toThread([x] {
                             return x + 2;
                         });
                     },
                     input)
                     .get();

        EXPECT_EQ(res, expect);   //NOLINT
    }
    {
        auto res = all(
          ao,
          [](AOContext&, int arg) {
              return toThread([arg] {
                  if (arg == 2) {
                      std::this_thread::sleep_for(10ms);
                      throw std::invalid_argument("some problem");
                  }
                  return std::to_string(arg);
              });
          },
          input);
        EXPECT_THROW(res.get(), std::invalid_argument);   // NOLINT
    }

    {
        auto res = all(
          ao,
          [](AOContext&, int arg) {
              if (arg == 2) {
                  throw std::invalid_argument("some problem");
              }
              return makeReadyFuture<std::string>(std::to_string(arg));
          },
          input);
        EXPECT_THROW(res.get(), std::invalid_argument);   // NOLINT
    }
}

TEST(all, vectorAndRetvalVoid)   // NOLINT
{
    ThreadExecutor executor;
    AOContext ao(executor);
    {
        Future<void> future = all(
          ao,
          [](AOContext&, int) {
              return toThread([] {});
          },
          std::vector<int>{});

        EXPECT_NO_THROW(future.get());   // NOLINT
    }

    const std::vector<int> input{1, 2, 3, 4, 5};
    {
        Future<void> future = all(
          ao,
          [](AOContext&, int) {
              return toThread([] {});
          },
          input);

        EXPECT_NO_THROW(future.get());   //NOLINT
    }

    {
        Future<void> future = all(
          ao,
          [](AOContext&, int arg) {
              return toThread([arg] {
                  if (arg == 2) {
                      std::this_thread::sleep_for(10ms);
                      throw std::invalid_argument("some problem");
                  }
              });
          },
          input);
        EXPECT_THROW(future.get(), std::invalid_argument);   // NOLINT
    }

    {
        Future<void> future = all(
          ao,
          [](AOContext&, int arg) {
              if (arg == 2) {
                  throw std::invalid_argument("some problem");
              }
              return makeReadyFuture<void>();
          },
          input);
        EXPECT_THROW(future.get(), std::invalid_argument);   // NOLINT
    }
}

TEST(all, tuple)   // NOLINT
{
    nhope::ThreadExecutor executor;
    nhope::AOContext ao(executor);
    {   // Empty
        [[maybe_unused]] const std::tuple<> res = all<>(ao).get();
    }

    {
        const auto res = all<int, std::string>(
                           ao,
                           [](AOContext&) {
                               return toThread([] {
                                   return 2 + 2;
                               });
                           },
                           [](AOContext&) {
                               return toThread([] {
                                   return "4"s;
                               });
                           })
                           .get();
        EXPECT_EQ(res, std::make_tuple(4, "4"s));
    }

    {
        auto res = all<int, std::string>(
          ao,
          [](AOContext&) {
              return toThread([]() -> int {
                  std::this_thread::sleep_for(10ms);
                  throw std::invalid_argument("some problem");
              });
          },
          [](AOContext&) {
              return toThread([] {
                  std::this_thread::sleep_for(20ms);
                  return "4"s;
              });
          });

        EXPECT_THROW(res.get(), std::invalid_argument);   // NOLINT
    }

    {
        auto res = all<int, std::string>(
          ao,
          [](AOContext&) {
              return toThread([] {
                  std::this_thread::sleep_for(10ms);
                  return 4;
              });
          },
          [](AOContext&) -> Future<std::string> {
              throw std::invalid_argument("some problem");
          });

        EXPECT_THROW(res.get(), std::invalid_argument);   // NOLINT
    }
}

TEST(all, nonCopyableVectorArgs)   // NOLINT
{
    ThreadExecutor executor;
    AOContext ao(executor);
    using NonCopy = std::unique_ptr<int>;
    std::vector<NonCopy> values;
    values.emplace_back(std::make_unique<int>(1));
    values.emplace_back(std::make_unique<int>(2));
    values.emplace_back(std::make_unique<int>(3));

    auto res = all(
                 ao,
                 [](AOContext&, const NonCopy& val) {
                     return makeReadyFuture<int>(*val * 2);
                 },
                 values)
                 .get();

    ASSERT_EQ(res.size(), values.size());
    EXPECT_EQ(res.at(0), 2);
    EXPECT_EQ(res.at(1), 4);
    EXPECT_EQ(res.at(2), 6);
}
