// is it a toliss and which one vp_spec 3.1
#pragma once

#include <string>

#include "core/aircraft.h"

namespace fxa {

struct AircraftInfo {
  bool toliss = false;
  std::string acfPath;
  std::string acfDir;  // trailing sep
  std::string acfFile;
  std::string acfIcao;
  std::string uiName;  // for the dormant status card
  std::string author;
  std::string liveryPath;  // empty default livery
  std::string liveryName;
  Family family = Family::Unknown;
  bool supported = false;  // a319/a320/a321 for now
};

// toliss airbusfbw/apphase or toliss_airbus/performance/v1 registered
AircraftInfo detectAircraft();

}  // namespace fxa
