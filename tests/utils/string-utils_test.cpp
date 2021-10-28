#include <array>
#include <gtest/gtest.h>

#include <nhope/utils/string-utils.h>

using namespace nhope;
using namespace std::literals;

TEST(String, toHtmlEscaped)   // NOLINT
{
    constexpr auto plain = R"(&include "<header>)";
    auto html = toHtmlEscaped(plain);
    EXPECT_EQ(html, "&amp;include &quot;&lt;header&gt;");
}
