// ascii only helpers pack and tag names are ascii anyway
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fxa {

std::string toLower(std::string_view s);
std::string toUpper(std::string_view s);
bool iequals(std::string_view a, std::string_view b);
bool istartsWith(std::string_view s, std::string_view prefix);
bool icontains(std::string_view haystack, std::string_view needle);
std::string_view trim(std::string_view s);
// trims drops empties
std::vector<std::string> splitList(std::string_view s,
                                   std::string_view seps = ",;");
std::string join(const std::vector<std::string>& parts, std::string_view sep);
// any run one char
bool globMatch(std::string_view pattern, std::string_view text);
// dataref byte arrays end in nuls
std::string cleanCString(const char* data, size_t maxLen);

}  // namespace fxa
