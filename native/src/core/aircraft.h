// vp_spec 3.1 type tags
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fxa {

enum class Family { Unknown, A319, A320, A321, A330, A340 };

struct AircraftType {
  Family family = Family::Unknown;
  bool neo = false;
  std::vector<std::string> tags;  // eg a321 a21n
  std::string specific;           // most specific one
  float typeFactor = 1.0f;        // cabin check and deboarding scale 4.1
  std::string label;
};

const char* familyName(Family f);
// acf_icao and/or the acf name
Family familyFrom(std::string_view acfIcao, std::string_view acfFile);
// pwg/lea pw /leap neo cfm/iae ceo
int engineIsNeo(std::string_view engineCode);  // 1 neo 0 ceo -1 unknown
// the cfmst thing at the start of toliss livery folders
std::string liveryStringCode(std::string_view liveryName);
AircraftType makeAircraftType(Family f, bool neo);

// icao type designator check
bool isAircraftTypeTag(std::string_view tag);

}  // namespace fxa
