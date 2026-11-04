// iata to icao for flight numbers airline names to icao for liveries
// vp_spec 1.4
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fxa {

struct Airline {
  std::string icao;
  std::string iata;
  std::vector<std::string> names;  // normalized
  std::string displayName;         // as written for the ui
  std::vector<std::string> regPrefixes;
};

class AirlineTable {
 public:
  // compiled in from data/airlines.tsv
  static AirlineTable builtin();
  // bad lines skipped
  static AirlineTable parse(std::string_view tsv);
  void prepend(const AirlineTable& front);

  // icao to display name empty if unknown
  std::string nameFor(std::string_view icao) const;
  std::vector<std::string> icaoForIata(std::string_view iata) const;
  // longest name wins then a matching registration prefix then table order
  std::vector<std::string> icaoForLiveryName(std::string_view liveryName) const;

  size_t size() const { return rows_.size(); }

 private:
  std::vector<Airline> rows_;
};

std::string normalizeWords(std::string_view s);
// upper case
std::vector<std::string> findRegistrations(std::string_view s);

}  // namespace fxa
