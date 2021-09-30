#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>

#include <fmt/format.h>
#include <gsl/span>

#include <ios>
#include <istream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <nhope/utils/bytes.h>
#include <nhope/utils/md5.h>

namespace {

using namespace nhope;

/* Constants for MD5Transform routine. */
constexpr std::uint32_t s11 = 7;
constexpr std::uint32_t s12 = 12;
constexpr std::uint32_t s13 = 17;
constexpr std::uint32_t s14 = 22;
constexpr std::uint32_t s21 = 5;
constexpr std::uint32_t s22 = 9;
constexpr std::uint32_t s23 = 14;
constexpr std::uint32_t s24 = 20;
constexpr std::uint32_t s31 = 4;
constexpr std::uint32_t s32 = 11;
constexpr std::uint32_t s33 = 16;
constexpr std::uint32_t s34 = 23;
constexpr std::uint32_t s41 = 6;
constexpr std::uint32_t s42 = 10;
constexpr std::uint32_t s43 = 15;
constexpr std::uint32_t s44 = 21;

/* F, G, H and I are basic MD5 functions. */

constexpr std::uint32_t f(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    return (x & y) | (~x & z);
}

constexpr std::uint32_t g(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    return (x & z) | (y & ~z);
}

constexpr std::uint32_t h(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    return x ^ y ^ z;
}

constexpr std::uint32_t i(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    return y ^ (x | ~z);
}

/* ROTATE_LEFT rotates x left n bits. */
constexpr std::uint32_t rotateLeft(std::uint32_t x, std::uint32_t n)
{
    return ((x << n) | (x >> (32 - n)));   // NOLINT(readability-magic-numbers)
}

/* FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4.
   Rotation is separate from addition to prevent recomputation. */

constexpr void ff(std::uint32_t& a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t x, std::uint32_t s,
                  std::uint32_t ac)
{
    a += f(b, c, d) + x + ac;
    a = rotateLeft(a, s);
    a += b;
}

constexpr void gg(std::uint32_t& a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t x, std::uint32_t s,
                  std::uint32_t ac)
{
    a += g(b, c, d) + x + ac;
    a = rotateLeft(a, s);
    a += (b);
}

constexpr void hh(std::uint32_t& a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t x, std::uint32_t s,
                  std::uint32_t ac)
{
    a += h(b, c, d) + x + ac;
    a = rotateLeft(a, s);
    a += b;
}

constexpr void ii(std::uint32_t& a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t x, std::uint32_t s,
                  std::uint32_t ac)
{
    a += i(b, c, d) + x + ac;
    a = rotateLeft(a, s);
    a += b;
}

// NOLINTNEXTLINE(readability-magic-numbers)
constexpr void encode(gsl::span<std::uint8_t, 8> output, std::uint64_t input)
{
    nhope::toBytes(input, output, nhope::Endian::Little);
}

constexpr void encode(gsl::span<std::uint8_t, 4> output, gsl::span<const std::uint32_t, 1> input)
{
    nhope::toBytes(input[0], output, nhope::Endian::Little);
}

template<std::size_t N, std::size_t M>
constexpr void encode(gsl::span<std::uint8_t, N> output, gsl::span<const std::uint32_t, M> input)
{
    static_assert(output.size_bytes() == input.size_bytes());

    encode(output.template first<4>(), input.template first<1>());
    encode(output.template last<N - 4>(), input.template last<M - 1>());
}

constexpr void decode(gsl::span<std::uint32_t, 1> output, gsl::span<const std::uint8_t, 4> input)
{
    nhope::fromBytes(output[0], input, nhope::Endian::Little);
}

template<std::size_t N, std::size_t M>
constexpr void decode(gsl::span<std::uint32_t, N> output, gsl::span<const std::uint8_t, M> input)
{
    static_assert(output.size_bytes() == input.size_bytes());

    decode(output.template first<1>(), input.template first<4>());
    decode(output.template last<N - 1>(), input.template last<M - 4>());
}

constexpr void transform(gsl::span<std::uint32_t, 4> state, gsl::span<const std::uint8_t, MD5::blockSize> block)
{
    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::array<std::uint32_t, 16> x{};   // NOLINT(readability-magic-numbers)

    decode(gsl::span(x), block);

    /* Round 1 */
    ff(a, b, c, d, x[0], s11, 0xd76aa478); /* 1 */     // NOLINT(readability-magic-numbers)
    ff(d, a, b, c, x[1], s12, 0xe8c7b756); /* 2 */     // NOLINT(readability-magic-numbers)
    ff(c, d, a, b, x[2], s13, 0x242070db); /* 3 */     // NOLINT(readability-magic-numbers)
    ff(b, c, d, a, x[3], s14, 0xc1bdceee); /* 4 */     // NOLINT(readability-magic-numbers)
    ff(a, b, c, d, x[4], s11, 0xf57c0faf); /* 5 */     // NOLINT(readability-magic-numbers)
    ff(d, a, b, c, x[5], s12, 0x4787c62a); /* 6 */     // NOLINT(readability-magic-numbers)
    ff(c, d, a, b, x[6], s13, 0xa8304613); /* 7 */     // NOLINT(readability-magic-numbers)
    ff(b, c, d, a, x[7], s14, 0xfd469501); /* 8 */     // NOLINT(readability-magic-numbers)
    ff(a, b, c, d, x[8], s11, 0x698098d8); /* 9 */     // NOLINT(readability-magic-numbers)
    ff(d, a, b, c, x[9], s12, 0x8b44f7af); /* 10 */    // NOLINT(readability-magic-numbers)s
    ff(c, d, a, b, x[10], s13, 0xffff5bb1); /* 11 */   // NOLINT(readability-magic-numbers)
    ff(b, c, d, a, x[11], s14, 0x895cd7be); /* 12 */   // NOLINT(readability-magic-numbers)
    ff(a, b, c, d, x[12], s11, 0x6b901122); /* 13 */   // NOLINT(readability-magic-numbers)
    ff(d, a, b, c, x[13], s12, 0xfd987193); /* 14 */   // NOLINT(readability-magic-numbers)
    ff(c, d, a, b, x[14], s13, 0xa679438e); /* 15 */   // NOLINT(readability-magic-numbers)
    ff(b, c, d, a, x[15], s14, 0x49b40821); /* 16 */   // NOLINT(readability-magic-numbers)

    /* Round 2 */
    gg(a, b, c, d, x[1], s21, 0xf61e2562); /* 17 */    // NOLINT(readability-magic-numbers)
    gg(d, a, b, c, x[6], s22, 0xc040b340); /* 18 */    // NOLINT(readability-magic-numbers)
    gg(c, d, a, b, x[11], s23, 0x265e5a51); /* 19 */   // NOLINT(readability-magic-numbers)
    gg(b, c, d, a, x[0], s24, 0xe9b6c7aa); /* 20 */    // NOLINT(readability-magic-numbers)
    gg(a, b, c, d, x[5], s21, 0xd62f105d); /* 21 */    // NOLINT(readability-magic-numbers)
    gg(d, a, b, c, x[10], s22, 0x2441453); /* 22 */    // NOLINT(readability-magic-numbers)
    gg(c, d, a, b, x[15], s23, 0xd8a1e681); /* 23 */   // NOLINT(readability-magic-numbers)
    gg(b, c, d, a, x[4], s24, 0xe7d3fbc8); /* 24 */    // NOLINT(readability-magic-numbers)
    gg(a, b, c, d, x[9], s21, 0x21e1cde6); /* 25 */    // NOLINT(readability-magic-numbers)
    gg(d, a, b, c, x[14], s22, 0xc33707d6); /* 26 */   // NOLINT(readability-magic-numbers)
    gg(c, d, a, b, x[3], s23, 0xf4d50d87); /* 27 */    // NOLINT(readability-magic-numbers)
    gg(b, c, d, a, x[8], s24, 0x455a14ed); /* 28 */    // NOLINT(readability-magic-numbers)
    gg(a, b, c, d, x[13], s21, 0xa9e3e905); /* 29 */   // NOLINT(readability-magic-numbers)
    gg(d, a, b, c, x[2], s22, 0xfcefa3f8); /* 30 */    // NOLINT(readability-magic-numbers)
    gg(c, d, a, b, x[7], s23, 0x676f02d9); /* 31 */    // NOLINT(readability-magic-numbers)
    gg(b, c, d, a, x[12], s24, 0x8d2a4c8a); /* 32 */   // NOLINT(readability-magic-numbers)

    /* Round 3 */
    hh(a, b, c, d, x[5], s31, 0xfffa3942); /* 33 */    // NOLINT(readability-magic-numbers)
    hh(d, a, b, c, x[8], s32, 0x8771f681); /* 34 */    // NOLINT(readability-magic-numbers)
    hh(c, d, a, b, x[11], s33, 0x6d9d6122); /* 35 */   // NOLINT(readability-magic-numbers)
    hh(b, c, d, a, x[14], s34, 0xfde5380c); /* 36 */   // NOLINT(readability-magic-numbers)
    hh(a, b, c, d, x[1], s31, 0xa4beea44); /* 37 */    // NOLINT(readability-magic-numbers)
    hh(d, a, b, c, x[4], s32, 0x4bdecfa9); /* 38 */    // NOLINT(readability-magic-numbers)
    hh(c, d, a, b, x[7], s33, 0xf6bb4b60); /* 39 */    // NOLINT(readability-magic-numbers)
    hh(b, c, d, a, x[10], s34, 0xbebfbc70); /* 40 */   // NOLINT(readability-magic-numbers)
    hh(a, b, c, d, x[13], s31, 0x289b7ec6); /* 41 */   // NOLINT(readability-magic-numbers)
    hh(d, a, b, c, x[0], s32, 0xeaa127fa); /* 42 */    // NOLINT(readability-magic-numbers)
    hh(c, d, a, b, x[3], s33, 0xd4ef3085); /* 43 */    // NOLINT(readability-magic-numbers)
    hh(b, c, d, a, x[6], s34, 0x4881d05); /* 44 */     // NOLINT(readability-magic-numbers)
    hh(a, b, c, d, x[9], s31, 0xd9d4d039); /* 45 */    // NOLINT(readability-magic-numbers)
    hh(d, a, b, c, x[12], s32, 0xe6db99e5); /* 46 */   // NOLINT(readability-magic-numbers)
    hh(c, d, a, b, x[15], s33, 0x1fa27cf8); /* 47 */   // NOLINT(readability-magic-numbers)
    hh(b, c, d, a, x[2], s34, 0xc4ac5665); /* 48 */    // NOLINT(readability-magic-numbers)

    /* Round 4 */
    ii(a, b, c, d, x[0], s41, 0xf4292244); /* 49 */    // NOLINT(readability-magic-numbers)
    ii(d, a, b, c, x[7], s42, 0x432aff97); /* 50 */    // NOLINT(readability-magic-numbers)
    ii(c, d, a, b, x[14], s43, 0xab9423a7); /* 51 */   // NOLINT(readability-magic-numbers)
    ii(b, c, d, a, x[5], s44, 0xfc93a039); /* 52 */    // NOLINT(readability-magic-numbers)
    ii(a, b, c, d, x[12], s41, 0x655b59c3); /* 53 */   // NOLINT(readability-magic-numbers)
    ii(d, a, b, c, x[3], s42, 0x8f0ccc92); /* 54 */    // NOLINT(readability-magic-numbers)
    ii(c, d, a, b, x[10], s43, 0xffeff47d); /* 55 */   // NOLINT(readability-magic-numbers)
    ii(b, c, d, a, x[1], s44, 0x85845dd1); /* 56 */    // NOLINT(readability-magic-numbers)
    ii(a, b, c, d, x[8], s41, 0x6fa87e4f); /* 57 */    // NOLINT(readability-magic-numbers)
    ii(d, a, b, c, x[15], s42, 0xfe2ce6e0); /* 58 */   // NOLINT(readability-magic-numbers)
    ii(c, d, a, b, x[6], s43, 0xa3014314); /* 59 */    // NOLINT(readability-magic-numbers)
    ii(b, c, d, a, x[13], s44, 0x4e0811a1); /* 60 */   // NOLINT(readability-magic-numbers)
    ii(a, b, c, d, x[4], s41, 0xf7537e82); /* 61 */    // NOLINT(readability-magic-numbers)
    ii(d, a, b, c, x[11], s42, 0xbd3af235); /* 62 */   // NOLINT(readability-magic-numbers)
    ii(c, d, a, b, x[2], s43, 0x2ad7d2bb); /* 63 */    // NOLINT(readability-magic-numbers)
    ii(b, c, d, a, x[9], s44, 0xeb86d391); /* 64 */    // NOLINT(readability-magic-numbers)
    /* Zeroize sensitive information. */
    // x.fill(0x0);
    for (auto& v : x) {
        v = 0;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

std::size_t read(std::istream& stream, gsl::span<std::uint8_t> buf)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    stream.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    return static_cast<std::size_t>(stream.gcount());
}

}   // namespace

MD5::Context::Context()
  : state()
  , count()
  , buffer()
{
    state[0] = 0x67452301;   // NOLINT(readability-magic-numbers)
    state[1] = 0xefcdab89;   // NOLINT(readability-magic-numbers)
    state[2] = 0x98badcfe;   // NOLINT(readability-magic-numbers)
    state[3] = 0x10325476;   // NOLINT(readability-magic-numbers)
}

void MD5::reset()
{
    m_context = Context();
}

MD5::Digest MD5::digest()
{
    constexpr Block padding = {0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                               0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                               0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    std::array<std::uint8_t, 8> bits{};   // NOLINT(readability-magic-numbers)

    /* Save number of bits */
    encode(bits, m_context.count);

    /* Pad out to 56 mod 64. */
    auto index = static_cast<std::size_t>((m_context.count >> 3) & 0x3f);   // NOLINT(readability-magic-numbers)
    auto padLen = (index < 56) ? (56 - index) : (120 - index);              // NOLINT(readability-magic-numbers)
    update(gsl::span(padding.data(), padLen));

    /* Append length (before padding) */
    update(bits);

    /* Store state in digest */
    Digest digest{};
    encode<digestSize, 4>(digest, m_context.state);

    this->reset();
    return digest;
}

MD5& MD5::update(gsl::span<const std::uint8_t> data)
{
    /* Compute number of bytes mod 64 */
    auto index = static_cast<std::size_t>((m_context.count >> 3) & 0x3f);   // NOLINT(readability-magic-numbers)

    /* Update number of bits */
    m_context.count += static_cast<std::uint64_t>(data.size()) << 3;

    const auto partLen = static_cast<std::size_t>(blockSize - index);

    /* Transform as many times as possible. */
    if (data.size() >= partLen) {
        std::memcpy(&m_context.buffer[index], data.data(), partLen);
        data = data.subspan(partLen);

        transform(m_context.state, m_context.buffer);
        while (data.size() >= blockSize) {
            transform(m_context.state, data.first<blockSize>());
            data = data.subspan(blockSize);
        }

        index = 0;
    }

    /* Buffer remaining input */
    if (!data.empty()) {
        std::memcpy(&m_context.buffer[index], data.data(), data.size());
    }

    return *this;
}

MD5::Digest MD5::digest(gsl::span<const std::uint8_t> data)
{
    MD5 md5;
    md5.update(data);
    return md5.digest();
}

MD5::Digest MD5::digest(std::istream& stream)
{
    constexpr std::size_t bufSize = 4096;

    MD5 md5;

    std::array<std::uint8_t, bufSize> buf{};
    std::size_t n = read(stream, buf);
    while (n > 0) {
        md5.update(gsl::span(buf.data(), n));
        n = read(stream, buf);
    }

    if (!stream.eof()) {
        throw std::system_error(errno, std::system_category());
    }

    return md5.digest();
}

MD5::Digest MD5::fileDigest(const std::filesystem::path& filePath)
{
    std::ifstream stream;
    stream.open(filePath, std::ios_base::binary);
    if (!stream.is_open()) {
        auto msg = fmt::format("Failed to open '{}'", filePath.string());
        throw std::system_error(errno, std::system_category(), msg);
    }

    return MD5::digest(stream);
}
