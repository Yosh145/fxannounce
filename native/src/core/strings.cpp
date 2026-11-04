#include "core/strings.h"

#include <algorithm>
#include <cstring>

namespace fxa {

namespace {
char lowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
char upperAscii(char c) {
  return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}
bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
         c == '\v';
}
}  // namespace

std::string toLower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = lowerAscii(c);
  return out;
}

std::string toUpper(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = upperAscii(c);
  return out;
}

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t k = 0; k < a.size(); ++k)
    if (lowerAscii(a[k]) != lowerAscii(b[k])) return false;
  return true;
}

bool istartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() &&
         iequals(s.substr(0, prefix.size()), prefix);
}

bool icontains(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) return true;
  if (needle.size() > haystack.size()) return false;
  for (size_t k = 0; k + needle.size() <= haystack.size(); ++k)
    if (iequals(haystack.substr(k, needle.size()), needle)) return true;
  return false;
}

std::string_view trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && isSpace(s[a])) ++a;
  while (b > a && isSpace(s[b - 1])) --b;
  return s.substr(a, b - a);
}

std::vector<std::string> splitList(std::string_view s, std::string_view seps) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t k = 0; k <= s.size(); ++k) {
    if (k == s.size() || seps.find(s[k]) != std::string_view::npos) {
      std::string_view part = trim(s.substr(start, k - start));
      if (!part.empty()) out.emplace_back(part);
      start = k + 1;
    }
  }
  return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (size_t k = 0; k < parts.size(); ++k) {
    if (k) out += sep;
    out += parts[k];
  }
  return out;
}

bool globMatch(std::string_view p, std::string_view t) {
  // backtracks to the last
  size_t pi = 0, ti = 0, star = std::string_view::npos, mark = 0;
  while (ti < t.size()) {
    if (pi < p.size() &&
        (p[pi] == '?' || lowerAscii(p[pi]) == lowerAscii(t[ti]))) {
      ++pi;
      ++ti;
    } else if (pi < p.size() && p[pi] == '*') {
      star = pi++;
      mark = ti;
    } else if (star != std::string_view::npos) {
      pi = star + 1;
      ti = ++mark;
    } else {
      return false;
    }
  }
  while (pi < p.size() && p[pi] == '*') ++pi;
  return pi == p.size();
}

std::string cleanCString(const char* data, size_t maxLen) {
  if (!data) return {};
  size_t n = 0;
  while (n < maxLen && data[n] != '\0') ++n;
  return std::string(trim(std::string_view(data, n)));
}

}  // namespace fxa
