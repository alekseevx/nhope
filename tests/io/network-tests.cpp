#include <string>
#include <vector>

#include "nhope/io/network.h"
#include <gtest/gtest.h>

using namespace nhope;

TEST(Network, LocalIp)   // NOLINT
{
    EXPECT_FALSE(nhope::getLocalIp().empty());
}

TEST(Network, Ifaces)   // NOLINT
{
    EXPECT_FALSE(nhope::addressEntries().empty());
}

