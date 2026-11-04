// which pack is active vp_spec 1.4
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/airlines.h"
#include "core/config.h"
#include "core/pack_library.h"

namespace fxa {

// airlinepack.cfg next to the livery or a glob in liveries.ini
struct LiveryConfig {
  std::string airline;
  std::vector<std::string> customTags;
  std::optional<bool> overwater;
  std::string source;
};

// any section fenix spellings ok
LiveryConfig liveryConfigFromSidecar(const Ini& sidecar);
// first glob that matches wins
LiveryConfig liveryConfigFromCentral(const Ini& central,
                                     const std::string& liveryName);
// sidecar beats central per key
LiveryConfig mergeLiveryConfig(const LiveryConfig& sidecar,
                               const LiveryConfig& central);

struct ResolveInput {
  std::string liveryName;  // empty default livery
  std::string flightNumber;
  LiveryConfig livery;
  std::string ofpCallsign;  // only when the ofp passed its check
};

struct Resolution {
  const Pack* pack = nullptr;  // nullptr nothing installed silence
  std::string reason;
};

Resolution resolvePack(const PackLibrary& lib, const AirlineTable& airlines,
                       const ResolveInput& in);

// dlh400 to dlh lh400 to iata lh
std::vector<std::string> airlineCodesFromFlightNumber(
    const AirlineTable& airlines, const std::string& flightNumber);

}  // namespace fxa
