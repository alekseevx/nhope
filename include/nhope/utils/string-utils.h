#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace nhope {

std::string removeWhitespaces(std::string_view s);

/*!
 * @brief Converts a plain text string to an HTML string with HTML
 * metacharacters <, >, &, and " replaced by HTML entities.
 * Example:
 *   auto plain = "#include <header>"sv;
 *   auto html = plain.toHtmlEscaped();
 *   assert(html == "#include &lt;header&gt;");
 */
std::string toHtmlEscaped(std::string_view s);

}   // namespace nhope