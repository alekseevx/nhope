#include <gtest/gtest.h>
#include <string>
#include "nhope/utils/filesystem.h"

using namespace nhope;
using namespace std::literals;

TEST(FileSystem, tempDir)   // NOLINT
{
    const std::string prefix{"nhope-test"};
    auto path = makeTemporaryDirectory(prefix).string();

    EXPECT_TRUE(path.find(prefix) != std::string::npos);
}
