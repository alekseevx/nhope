#pragma once

namespace nhope {

#if __clang__
constexpr auto isClang = true;
#else
constexpr auto isClang = false;
#endif

#if __GNUC__
constexpr auto isGcc = true;
#else
constexpr auto isGcc = false;
#endif

#if __clang__
#define NHOPE_SANITIZE_THREAD __has_feature(thread_sanitizer)   // NOLINT
#else
#define NHOPE_SANITIZE_THREAD __SANITIZE_THREAD__
#endif

#if NHOPE_SANITIZE_THREAD
constexpr auto isThreadSanitizer = true;
#else
constexpr auto isThreadSanitizer = false;
#endif

}   // namespace nhope
