// Airline table: IATA → ICAO for flight numbers, airline names → ICAO for livery folder names (VP_SPEC.md § 1.4).
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fxa {

struct Airline {
    std::string icao;
    std::string iata;
    std::vector<std::string> names;        // normalized ("british airways")
    std::vector<std::string> regPrefixes;  // "G-", "OE-"
};

class AirlineTable {
public:
    // The table compiled into the plugin (native/data/airlines.tsv).
    static AirlineTable builtin();
    // Parses the TSV format; malformed lines are skipped.
    static AirlineTable parse(std::string_view tsv);
    // Rows of `front` take priority over this table's rows.
    void prepend(const AirlineTable& front);

    // ICAO codes for an IATA code, in table order.
    std::vector<std::string> icaoForIata(std::string_view iata) const;
    // ICAO codes whose name appears as whole words in the livery name, best first: longest name, then rows whose
    // registration prefix matches a registration found in the name, then table order.
    std::vector<std::string> icaoForLiveryName(std::string_view liveryName) const;

    size_t size() const { return rows_.size(); }

private:
    std::vector<Airline> rows_;
};

// Lower-case words separated by single spaces ("[LEA] EasyJet (G-UZHH)" → "lea easyjet g uzhh").
std::string normalizeWords(std::string_view s);
// Registration marks found in a livery name ("G-UZHH", "N975UY"), upper case.
std::vector<std::string> findRegistrations(std::string_view s);

} // namespace fxa
