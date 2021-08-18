#pragma once

#include <cstdint>

namespace nhope::detail {

using AOHandlerId = std::uint64_t;
inline constexpr AOHandlerId invalidAOHandlerId = UINT64_MAX;

}   // namespace nhope::detail
