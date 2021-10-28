#include <array>
#include <cstddef>
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

constexpr auto decoded = std::array{
  ""sv, "1"sv, "12"sv, "123"sv, "1234"sv, "<p>Hello?</p>"sv,
};

constexpr auto encoded = std::array{
  ""sv, "MQ=="sv, "MTI="sv, "MTIz"sv, "MTIzNA=="sv, "PHA+SGVsbG8/PC9wPg=="sv,
};

static_assert(decoded.size() == encoded.size());

}   // namespace

TEST(Base64, decode)   // NOLINT
{
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const auto data = fromBase64(encoded.at(i), false);
        const auto strData = std::string(data.begin(), data.end());
        EXPECT_EQ(strData, decoded.at(i));
    }

    EXPECT_EQ(fromBase64("PHA+SGVsbG8\n/PC\r9wP g\t==", true), fromBase64("PHA+SGVsbG8/PC9wPg=="sv, false));
}

TEST(Base64, decode_fail)   // NOLINT
{
    EXPECT_THROW(fromBase64("MQ"), Base64ParseError);             // NOLINT
    EXPECT_THROW(fromBase64("MQ!="), Base64ParseError);           // NOLINT
    EXPECT_THROW(fromBase64("MQ ==", false), Base64ParseError);   // NOLINT
}

TEST(Base64, encode)   // NOLINT
{
    for (std::size_t i = 0; i < decoded.size(); ++i) {
        const auto& data = decoded.at(i);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto base64 = toBase64({reinterpret_cast<const std::uint8_t*>(data.data()), data.size()});
        EXPECT_EQ(base64, encoded.at(i));
    }
}

TEST(Base64, bigdata)   // NOLINT
{
    constexpr auto bigSize{4096};
    auto data = std::vector<std::uint8_t>(bigSize);
    for (std::size_t i = 0; i < bigSize; ++i) {
        data[i] = static_cast<std::uint8_t>(i);
    }
    const auto base64 = toBase64(data);
    EXPECT_GT(base64.size(), data.size());

    EXPECT_EQ(data, fromBase64(base64));
}