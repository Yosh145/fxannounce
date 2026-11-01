#include "core/announcement.h"

#include "core/strings.h"

#include <cmath>

namespace fxa {

namespace {

const char* kTypeNames[kAnnTypeCount] = {
    "BoardingMusic", "BoardingWelcome", "BoardingComplete", "ArmDoors", "DisarmDoors", "PreSafetyBriefing",
    "SafetyBriefing", "CrewTakeSeats", "CabinDim", "CabinSecured", "AfterTakeoff", "SeatbeltSignOn",
    "SeatbeltSignOff", "CabinService", "DescentStarted", "AfterLanding", "DeboardingStarted",
    "DeboardingComplete", "Decompression",
};

struct Alias {
    const char* name;
    AnnType type;
    CallContext context;
};

// VP_SPEC.md § 1.3 (D-11).
const Alias kAliases[] = {
    { "CrewSeatsTakeoff", AnnType::CrewTakeSeats, CallContext::Takeoff },
    { "CrewSeatsLanding", AnnType::CrewTakeSeats, CallContext::Landing },
    { "CabinDimTakeoff", AnnType::CabinDim, CallContext::Takeoff },
    { "CabinDimLanding", AnnType::CabinDim, CallContext::Landing },
    { "CallCabinSecureTakeoff", AnnType::CabinSecured, CallContext::Takeoff },
    { "CallCabinSecureLanding", AnnType::CabinSecured, CallContext::Landing },
    { "DescentSeatbelts", AnnType::DescentStarted, CallContext::Any },
    { "DescentSeatbelt", AnnType::DescentStarted, CallContext::Any },
    { "FastenSeatbelt", AnnType::SeatbeltSignOn, CallContext::Any },
    { "DisembarkStarted", AnnType::DeboardingStarted, CallContext::Any },
};

const char* kPhaseNames[] = { "PreFlight", "Takeoff", "Climb", "Cruise", "Descent", "Approach", "GA", "Done" };
const char* kTimeNames[] = { "Morning", "Afternoon", "Evening", "Night" };
const char* kModeNames[] = { "Manual", "Prerecorded", "Interphone" };

} // namespace

const char* annTypeName(AnnType t)
{
    const int k = static_cast<int>(t);
    return (k >= 0 && k < kAnnTypeCount) ? kTypeNames[k] : "?";
}

const char* modeName(Mode m) { return kModeNames[static_cast<int>(m)]; }

const char* contextName(CallContext c)
{
    switch (c) {
    case CallContext::Takeoff: return "takeoff";
    case CallContext::Landing: return "landing";
    default: return "any";
    }
}

const char* phaseName(Phase p) { return kPhaseNames[static_cast<int>(p)]; }
const char* timeOfDayName(TimeOfDay t) { return kTimeNames[static_cast<int>(t)]; }

Mode defaultMode(AnnType t)
{
    switch (t) {
    case AnnType::SafetyBriefing:
    case AnnType::AfterTakeoff:
    case AnnType::CabinService:
    case AnnType::Decompression:
        return Mode::Prerecorded;
    case AnnType::CabinSecured:
        return Mode::Interphone;
    default:
        return Mode::Manual;
    }
}

std::optional<TypeName> resolveTypeName(std::string_view name)
{
    name = trim(name);
    for (int k = 0; k < kAnnTypeCount; ++k)
        if (iequals(name, kTypeNames[k])) return TypeName{ static_cast<AnnType>(k), CallContext::Any, false };
    for (const Alias& a : kAliases)
        if (iequals(name, a.name)) return TypeName{ a.type, a.context, true };
    return std::nullopt;
}

std::optional<Phase> phaseFromTag(std::string_view tag)
{
    for (int k = 0; k < 8; ++k)
        if (iequals(tag, kPhaseNames[k])) return static_cast<Phase>(k);
    return std::nullopt;
}

std::optional<TimeOfDay> timeOfDayFromTag(std::string_view tag)
{
    for (int k = 0; k < 4; ++k)
        if (iequals(tag, kTimeNames[k])) return static_cast<TimeOfDay>(k);
    return std::nullopt;
}

std::optional<Mode> modeFromTag(std::string_view tag)
{
    for (int k = 0; k < 3; ++k)
        if (iequals(tag, kModeNames[k])) return static_cast<Mode>(k);
    return std::nullopt;
}

TimeOfDay timeOfDayAt(double sec)
{
    if (!std::isfinite(sec)) sec = 0;
    sec = std::fmod(sec, 86400.0);
    if (sec < 0) sec += 86400.0;
    const double h = sec / 3600.0;
    if (h < 6) return TimeOfDay::Night;
    if (h < 12) return TimeOfDay::Morning;
    if (h < 18) return TimeOfDay::Afternoon;
    return TimeOfDay::Evening;
}

} // namespace fxa
