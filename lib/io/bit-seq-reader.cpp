#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

#include "nhope/io/bit-seq-reader.h"

namespace nhope {

namespace {

class BitSeqReaderImpl final : public BitSeqReader
{
public:
    BitSeqReaderImpl(AOContext& parent, std::vector<bool> bits)
      : m_bits(std::move(bits))
      , m_aoCtx(parent)
    {}

    ~BitSeqReaderImpl() override
    {
        m_aoCtx.close();
    }

    void read(gsl::span<std::uint8_t> buf, IOHandler handler) override
    {
        m_aoCtx.exec([this, buf, handler = std::move(handler)] {
            for (std::uint8_t& byte : buf) {
                byte = this->nextBit() << 0;
                for (int n = 1; n < std::numeric_limits<std::uint8_t>::digits; ++n) {
                    byte |= this->nextBit() << n;
                }
            }

            handler(nullptr, buf.size());
        });
    }

private:
    std::uint8_t nextBit()
    {
        const auto bit = m_bits[m_pos];
        m_pos = (m_pos + 1) % m_bits.size();
        return bit ? 1 : 0;
    }

    const std::vector<bool> m_bits;
    std::size_t m_pos = 0;

    AOContext m_aoCtx;
};

}   // namespace

BitSeqReaderPtr BitSeqReader::create(AOContext& aoCtx, std::vector<bool> bits)
{
    return std::make_unique<BitSeqReaderImpl>(aoCtx, std::move(bits));
}

BitSeqReaderPtr BitSeqReader::create(AOContext& aoCtx, gsl::span<const uint8_t> psp, std::size_t bitCount)
{
    const auto reserved = psp.size() * std::numeric_limits<uint8_t>::digits;

    assert(bitCount <= reserved);   //NOLINT

    std::vector<bool> temp;
    temp.reserve(bitCount);

    for (auto byte : psp) {
        for (int n = 0; n < std::numeric_limits<uint8_t>::digits; ++n) {
            temp.push_back(((byte >> n) & 1) != 0);
            if (temp.size() == bitCount) {
                goto end;
            }
        }
    }
end:
    return std::make_unique<BitSeqReaderImpl>(aoCtx, std::move(temp));
}

}   // namespace nhope
