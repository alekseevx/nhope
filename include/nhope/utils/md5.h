#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <array>
#include <filesystem>
#include <istream>
#include <string_view>
#include <vector>

#include <gsl/span>

namespace nhope::utils {

// steeled from POCO(http://pocoproject.org/)
class MD5 final
{
public:
    static constexpr std::size_t digestSize = 16;
    static constexpr std::size_t blockSize = 64;

    using Digest = std::array<std::uint8_t, digestSize>;

    MD5() = default;
    ~MD5() = default;

    void reset();
    MD5& update(gsl::span<const std::uint8_t> data);

    Digest digest();

    static Digest digest(gsl::span<const std::uint8_t> data);
    static Digest digest(std::istream& stream);
    static Digest fileDigest(const std::filesystem::path& filePath);

private:
    using Block = std::array<std::uint8_t, blockSize>;
    using State = std::array<std::uint32_t, 4>;

    struct Context
    {
        Context();

        State state;           // state (ABCD)
        std::uint64_t count;   // number of bits, modulo 2^64 (lsb first)
        Block buffer;          // input buffer
    };

    Context m_context;
    Digest m_digest{};
};

}   // namespace nhope::utils
