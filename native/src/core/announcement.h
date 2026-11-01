// The Fenix announcement vocabulary (VP_SPEC.md § 1.3, § 2, § 3.1): announcement types, default modes,
// legacy names, flight phases and times of day.
#pragma once

#include <optional>
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
// Which call a legacy file belongs to (CrewSeatsTakeoff vs CrewSeatsLanding); Any for Fenix names.
enum class CallContext { Any, Takeoff, Landing };
enum class Phase { PreFlight, Takeoff, Climb, Cruise, Descent, Approach, GA, Done };
enum class TimeOfDay { Morning, Afternoon, Evening, Night };

const char* annTypeName(AnnType t);
const char* modeName(Mode m);
const char* contextName(CallContext c);
const char* phaseName(Phase p);
const char* timeOfDayName(TimeOfDay t);

Mode defaultMode(AnnType t);

struct TypeName {
    AnnType type;
    CallContext context = CallContext::Any;
    bool legacy = false;
};
// Resolves a file's type name (Fenix name or legacy alias, case-insensitive).
std::optional<TypeName> resolveTypeName(std::string_view name);

std::optional<Phase> phaseFromTag(std::string_view tag);
std::optional<TimeOfDay> timeOfDayFromTag(std::string_view tag);
std::optional<Mode> modeFromTag(std::string_view tag);
// Fenix spans in local time: Morning 06-12, Afternoon 12-18, Evening 18-24, Night 00-06.
TimeOfDay timeOfDayAt(double localSecondsSinceMidnight);

} // namespace fxa
