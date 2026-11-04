// fenix vocab types modes legacy names phases time of day vp_spec 1.3 2 3.1
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace fxa {

enum class AnnType {
  BoardingMusic,
  BoardingWelcome,
  BoardingComplete,
  ArmDoors,
  DisarmDoors,
  PreSafetyBriefing,
  SafetyBriefing,
  CrewTakeSeats,
  CabinDim,
  CabinSecured,
  AfterTakeoff,
  SeatbeltSignOn,
  SeatbeltSignOff,
  CabinService,
  DescentStarted,
  AfterLanding,
  DeboardingStarted,
  DeboardingComplete,
  Decompression,
  Count
};
constexpr int kAnnTypeCount = static_cast<int>(AnnType::Count);

enum class Mode { Manual, Prerecorded, Interphone };
// legacy files say which call crewseatstakeoff vs landing any for fenix names
enum class CallContext { Any, Takeoff, Landing };
enum class Phase {
  PreFlight,
  Takeoff,
  Climb,
  Cruise,
  Descent,
  Approach,
  GA,
  Done
};
enum class TimeOfDay { Morning, Afternoon, Evening, Night };

const char* annTypeName(AnnType t);
const char* modeName(Mode m);
const char* contextName(CallContext c);
const char* phaseName(Phase p);
const char* timeOfDayName(TimeOfDay t);

Mode defaultMode(AnnType t);

// ui names in plain english fenix name in the tooltip and on disk
const char* annTypeLabel(AnnType t);
std::string annLabel(AnnType t, CallContext c);
std::string annFenixLabel(AnnType t, CallContext c);
// vp_spec 2
const char* heardOn(AnnType t, Mode m);

struct TypeName {
  AnnType type;
  CallContext context = CallContext::Any;
  bool legacy = false;
};
// case insensitive legacy aliases too
std::optional<TypeName> resolveTypeName(std::string_view name);
// other tools names that arent fenix types boardingstarted to boardingwelcome
// etc
const char* fenixRenameHint(std::string_view name);

std::optional<Phase> phaseFromTag(std::string_view tag);
std::optional<TimeOfDay> timeOfDayFromTag(std::string_view tag);
std::optional<Mode> modeFromTag(std::string_view tag);
// local time 06-12 morning 12-18 afternoon 18-24 evening else night
TimeOfDay timeOfDayAt(double localSecondsSinceMidnight);

}  // namespace fxa
