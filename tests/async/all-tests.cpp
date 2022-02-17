#include <chrono>
#include <functional>
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

TEST(all, tuple)   // NOLINT
{
    nhope::ThreadExecutor executor;
    nhope::AOContext ao(executor);
    {   // Empty
        const std::tuple<> res = all<>(ao).get();
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
