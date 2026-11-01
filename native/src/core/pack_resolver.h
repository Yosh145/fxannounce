// Which Airline Pack is active for the loaded livery and flight number (VP_SPEC.md § 1.4).
#pragma once

#include "core/airlines.h"
#include "core/config.h"
#include "core/pack_library.h"

#include <optional>
#include <string>
#include <vector>

namespace fxa {

// Per-livery settings from <livery>/airlinepack.cfg or a matching [glob] section of AirlinePacks/liveries.ini.
struct LiveryConfig {
    std::string airline;                  // pack name
    std::vector<std::string> customTags;
    std::optional<bool> overwater;
    std::string source;                   // "airlinepack.cfg", "liveries.ini [British Airways*]", or ""
};

// Reads the keys from any section of a sidecar file (Fenix spellings accepted).
LiveryConfig liveryConfigFromSidecar(const Ini& sidecar);
// First section of liveries.ini whose glob matches the livery folder name (keys merged from that section only).
LiveryConfig liveryConfigFromCentral(const Ini& central, const std::string& liveryName);
// Sidecar wins key by key over the central mapping.
LiveryConfig mergeLiveryConfig(const LiveryConfig& sidecar, const LiveryConfig& central);

struct ResolveInput {
    std::string liveryName;     // folder name, "" for the default livery
    std::string flightNumber;   // MCDU FLT NBR, may be ""
    LiveryConfig livery;
    std::string ofpCallsign;    // SimBrief OFP callsign ("DLH400") when the OFP passed its check, else ""
};

struct Resolution {
    const Pack* pack = nullptr;  // nullptr: no pack and no Default installed (silence)
    std::string reason;          // "livery mapping (airlinepack.cfg)", "SimBrief OFP DLH400", "flight number DLH400", ...
};

Resolution resolvePack(const PackLibrary& lib, const AirlineTable& airlines, const ResolveInput& in);

// The airline part of a flight number: "DLH400" → {"DLH"}, "LH400" → IATA "LH" (resolved through the table).
std::vector<std::string> airlineCodesFromFlightNumber(const AirlineTable& airlines, const std::string& flightNumber);

} // namespace fxa
