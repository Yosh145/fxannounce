// Aircraft model identity and its type tags (VP_SPEC.md § 3.1).
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fxa {

enum class Family { Unknown, A319, A320, A321, A330, A340 };

struct AircraftType {
    Family family = Family::Unknown;
    bool neo = false;
    std::vector<std::string> tags;   // e.g. { "A321", "A21N" }
    std::string specific;            // the most specific tag, e.g. "A21N"
    float typeFactor = 1.0f;         // cabin check / deboarding scale (VP_SPEC § 4.1)
    std::string label;               // "A321neo"
};

const char* familyName(Family f);
// From acf_ICAO and/or the .acf file name (a319.acf, a321_StdDef.acf, A330-900.acf...).
Family familyFrom(std::string_view acfIcao, std::string_view acfFile);
// ToLiss engine codes: PWG/LEA (and PW*/LEAP*) are NEO engines; CFM/IAE are CEO. Empty: unknown.
int engineIsNeo(std::string_view engineCode);   // 1 neo, 0 ceo, -1 unknown
// The "[CFMST]"-style code at the start of a ToLiss livery folder name, or "".
std::string liveryStringCode(std::string_view liveryName);
AircraftType makeAircraftType(Family f, bool neo);

// True if the tag is an ICAO aircraft type designator (A320, A21N, B738, B77W, E190, CRJ9, DH8D, AT76...).
bool isAircraftTypeTag(std::string_view tag);

} // namespace fxa
