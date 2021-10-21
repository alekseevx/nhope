#pragma once

#include <string>
#include <vector>

#include "gsl/span"

namespace nhope {

std::vector<uint8_t> fromBase64(std::string_view str);
std::string toBase64(gsl::span<const uint8_t> plainSeq);

}   // namespace nhope
