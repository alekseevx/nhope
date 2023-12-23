#include <cctype>
#include <string>

#include "nhope/utils/string-utils.h"

namespace nhope {

std::string removeWhitespaces(std::string_view s)
{
    std::string result;
    result.reserve(s.size());
    for (char symbol : s) {
        if (isspace(symbol) == 0) {
            result += symbol;
        }
    }
    result.shrink_to_fit();
    return result;
}

std::string toHtmlEscaped(std::string_view s)
{
    std::string rich;
    //NOLINTNEXTLINE (readability-magic-numbers)
    rich.reserve(int(static_cast<double>(s.size()) * 1.1));
    for (auto c : s) {
        switch (c) {
        case '<':
            rich += "&lt;";
            break;
        case '>':
            rich += "&gt;";
            break;
        case '&':
            rich += "&amp;";
            break;
        case '"':
            rich += "&quot;";

            break;
        default:
            rich += c;
            break;
        }
    }
    rich.shrink_to_fit();
    return rich;
}

}   // namespace nhope
