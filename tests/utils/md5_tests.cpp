#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nhope/utils/md5.h>

namespace {
using namespace nhope;
using namespace std::literals;

auto makeTestStream()
{
    constexpr MD5::Digest digest = {0x85, 0x7c, 0x2d, 0x90, 0x3c, 0x8d, 0x05, 0xaf,
                                    0x7a, 0x07, 0x09, 0xd6, 0xd2, 0x64, 0x7a, 0x02};

    constexpr auto iterCount = 1000;
    constexpr auto bufSize = 1000;
    std::array<std::uint8_t, bufSize> buf{};

    std::stringstream stream;
    for (std::size_t i = 0; i < iterCount; ++i) {
        buf.fill(static_cast<std::uint8_t>(i));
        stream.write(reinterpret_cast<char*>(buf.data()), buf.size());   // NOLINT
    }

    return std::pair{std::stringstream(stream.str()), digest};
}

}   // namespace

TEST(Md5, simpleCalc)   // NOLINT
{
    constexpr MD5::Digest etalon = {0xa6, 0xe7, 0xd3, 0xb4, 0x6f, 0xdf, 0xaf, 0x0b,
                                    0xde, 0x2a, 0x1f, 0x83, 0x2a, 0x00, 0xd2, 0xde};

    // 0xa6e7d3b46fdfaf0bde2a1f832a00d2de
    const std::vector<uint8_t> testData{0, 1, 2, 3, 4, 5, 6, 7, 8};
    const auto res = MD5::digest(testData);

    EXPECT_EQ(etalon, res);
}

TEST(Md5, calcStream)   // NOLINT
{
    auto [stream, etalonDigest] = makeTestStream();
    const auto res = MD5::digest(stream);

    EXPECT_EQ(etalonDigest, res);
}

TEST(Md5, calcFile)   // NOLINT
{
    EXPECT_NO_THROW(MD5::fileDigest(__FILE__));   // NOLINT
}

TEST(Md5, calcInvalidFile)   // NOLINT
{
    EXPECT_THROW(MD5::fileDigest("someFile"sv), std::system_error);   // NOLINT
}

TEST(Md5, emptyBuffer)   // NOLINT
{
    constexpr MD5::Digest etalonDigest = {0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
                                          0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e};

    const auto res = MD5::digest(std::vector<uint8_t>());
    EXPECT_EQ(etalonDigest, res);
}
