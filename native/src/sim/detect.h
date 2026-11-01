// Is the user's aircraft a ToLiss, and which one (VP_SPEC.md § 3.1, ARCHITECTURE.md § 4).
#pragma once

#include "core/aircraft.h"

#include <string>

namespace fxa {

struct AircraftInfo {
    bool toliss = false;
    std::string acfPath;        // full path of the .acf
    std::string acfDir;         // folder of the .acf, with trailing separator
    std::string acfFile;        // "a321.acf"
    std::string acfIcao;        // acf_ICAO
    std::string author;
    std::string liveryPath;     // with trailing separator, or "" for the default livery
    std::string liveryName;     // last folder of liveryPath
    Family family = Family::Unknown;
    bool supported = false;     // a ToLiss model this version runs on (A319/A320/A321; VP_SPEC scope)
};

// Reads the user aircraft's identity. `toliss` needs a ToLiss systems dataref (AirbusFBW/APPhase or
// toliss_airbus/performance/V1) to be registered and good.
AircraftInfo detectAircraft();

} // namespace fxa
