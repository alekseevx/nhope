#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include "nhope/utils/scope-exit.h"

TEST(ScopeExit, simpleExit)   // NOLINT
{
    bool flag{};

    {
        nhope::ScopeExit scopeExit([&] {
            flag = true;
        });
    }
    ASSERT_TRUE(flag);
}

TEST(ScopeExit, exceptionExit)   // NOLINT
{
    bool flag{};

    {
        nhope::ScopeExit scopeExit([&] {
            throw std::runtime_error("something go wrong");
            flag = true;
        });
    }
    ASSERT_FALSE(flag);
}
