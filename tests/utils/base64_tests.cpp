#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "gsl/span"
#include "nhope/utils/base64.h"
#include "nhope/utils/bytes.h"

namespace {

using namespace std::literals;
using namespace nhope;

const auto testEncoded = "PHA+SGVsbG8/PC9wPg=="s;
const auto testDecoded = "<p>Hello?</p>"s;

}   // namespace

TEST(Base64, decode)   // NOLINT
{
    const auto decoded = fromBase64(testEncoded);
    EXPECT_EQ(std::string(decoded.begin(), decoded.end()), testDecoded);

    EXPECT_TRUE(fromBase64("$"sv).empty());
}

TEST(Base64, encode)   // NOLINT
{
    gsl::span<const uint8_t> dx((uint8_t*)testDecoded.data(), testDecoded.size());
    const auto encoded = toBase64(dx);
    EXPECT_EQ(encoded, testEncoded);

    EXPECT_TRUE(toBase64({}).empty());

    constexpr auto bigSize{4096};
    std::vector<uint8_t> buf(bigSize);
    for (size_t i = 0; i < bigSize; ++i) {
        buf[i] = i % std::numeric_limits<uint8_t>::max();
    }
    const auto data = toBase64(buf);
    EXPECT_GT(data.size(), bigSize);
}