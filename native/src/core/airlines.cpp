#include "core/airlines.h"

#include <algorithm>
#include <cctype>

#include "core/strings.h"

namespace fxa {

// embedded by cmake into build/generated
extern const char* const kAirlinesTsv;

namespace {
bool isAlnum(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0;
}
bool isUpperOrDigit(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
}  // namespace

std::string normalizeWords(std::string_view s) {
  std::string out;
  bool space = false;
  for (char c : s) {
    if (isAlnum(c)) {
      if (space && !out.empty()) out += ' ';
      space = false;
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (static_cast<unsigned char>(c) >= 0x80) {
      out += c;  // utf8 letters stay part of the word aeromexico
      space = false;
    } else {
      space = true;
    }
  }
  return out;
}

std::vector<std::string> findRegistrations(std::string_view s) {
  std::vector<std::string> out;
  const std::string u = toUpper(s);
  const size_t n = u.size();
  for (size_t k = 0; k < n; ++k) {
    if (k > 0 && isAlnum(u[k - 1])) continue;
    // prefix dash style g-uzhh 9h-aef
    size_t a = k;
    while (a < n && a - k < 2 && isUpperOrDigit(u[a])) ++a;
    if (a > k && a < n && u[a] == '-') {
      size_t b = a + 1;
      while (b < n && isUpperOrDigit(u[b])) ++b;
      if (b - a - 1 >= 2 && b - a - 1 <= 5 && (b == n || !isAlnum(u[b]))) {
        out.push_back(u.substr(k, b - k));
        k = b;
        continue;
      }
    }
    // us style n digits up to 2 letters
    if (u[k] == 'N' && k + 1 < n &&
        std::isdigit(static_cast<unsigned char>(u[k + 1]))) {
      size_t b = k + 1;
      while (b < n && b - k <= 5 &&
             std::isdigit(static_cast<unsigned char>(u[b])))
        ++b;
      size_t c = b;
      while (c < n && c - b < 2 && u[c] >= 'A' && u[c] <= 'Z') ++c;
      if (c == n || !isAlnum(u[c])) out.push_back(u.substr(k, c - k));
    }
  }
  return out;
}

AirlineTable AirlineTable::parse(std::string_view tsv) {
  AirlineTable t;
  size_t pos = 0;
  while (pos < tsv.size()) {
    size_t end = tsv.find('\n', pos);
    if (end == std::string_view::npos) end = tsv.size();
    std::string_view line = tsv.substr(pos, end - pos);
    pos = end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (trim(line).empty() || trim(line)[0] == '#') continue;
    std::vector<std::string_view> cols;
    size_t c = 0;
    while (true) {
      const size_t tab = line.find('\t', c);
      cols.push_back(line.substr(
          c, tab == std::string_view::npos ? std::string_view::npos : tab - c));
      if (tab == std::string_view::npos) break;
      c = tab + 1;
    }
    if (cols.size() < 3) continue;
    Airline a;
    a.icao = toUpper(trim(cols[0]));
    a.iata = toUpper(trim(cols[1]));
    if (a.icao.size() != 3) continue;
    for (const std::string& n : splitList(cols[2], "|")) {
      if (a.displayName.empty()) a.displayName = std::string(trim(n));
      const std::string w = normalizeWords(n);
      if (w.size() >= 3)
        a.names.push_back(w);  // 2 letter names match random words
    }
    if (cols.size() >= 4)
      for (const std::string& r : splitList(cols[3], ","))
        a.regPrefixes.push_back(toUpper(r));
    t.rows_.push_back(std::move(a));
  }
  return t;
}

std::string AirlineTable::nameFor(std::string_view icao) const {
  for (const Airline& a : rows_)
    if (iequals(a.icao, icao)) return a.displayName;
  return {};
}

AirlineTable AirlineTable::builtin() { return parse(kAirlinesTsv); }

void AirlineTable::prepend(const AirlineTable& front) {
  std::vector<Airline> rows = front.rows_;
  rows.insert(rows.end(), rows_.begin(), rows_.end());
  rows_ = std::move(rows);
}

std::vector<std::string> AirlineTable::icaoForIata(
    std::string_view iata) const {
  std::vector<std::string> out;
  for (const Airline& a : rows_)
    if (!a.iata.empty() && iequals(a.iata, iata) &&
        std::find(out.begin(), out.end(), a.icao) == out.end())
      out.push_back(a.icao);
  return out;
}

std::vector<std::string> AirlineTable::icaoForLiveryName(
    std::string_view liveryName) const {
  const std::string hay = " " + normalizeWords(liveryName) + " ";
  const std::vector<std::string> regs = findRegistrations(liveryName);
  struct Hit {
    std::string icao;
    size_t len;
    bool regMatch;
    size_t order;
  };
  std::vector<Hit> hits;
  for (size_t k = 0; k < rows_.size(); ++k) {
    const Airline& a = rows_[k];
    size_t best = 0;
    for (const std::string& n : a.names)
      if (hay.find(" " + n + " ") != std::string::npos)
        best = std::max(best, n.size());
    if (!best) continue;
    bool reg = false;
    for (const std::string& p : a.regPrefixes)
      for (const std::string& r : regs)
        if (r.rfind(p, 0) == 0) reg = true;
    // restricted rows lose to unrestricted/matching ones
    hits.push_back({a.icao, best, reg || a.regPrefixes.empty(), k});
  }
  std::stable_sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) {
    if (x.len != y.len) return x.len > y.len;
    if (x.regMatch != y.regMatch) return x.regMatch;
    return x.order < y.order;
  });
  std::vector<std::string> out;
  for (const Hit& h : hits)
    if (std::find(out.begin(), out.end(), h.icao) == out.end())
      out.push_back(h.icao);
  return out;
}

}  // namespace fxa
