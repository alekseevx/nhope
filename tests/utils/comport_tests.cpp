#include <filesystem>
#include <gtest/gtest.h>

#include "nhope/utils/com-discover.h"

TEST(Comport, GetAvailable)   // NOLINT
{
    auto ports = nhope::utils::getAvailableComs();
    for (const auto& portName : ports) {
        EXPECT_TRUE(std::filesystem::exists(portName));
    }
}
