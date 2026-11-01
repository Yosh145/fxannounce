// A normalized snapshot of the sim, built by the aircraft adapter every tick (ARCHITECTURE.md § 2). The purser's
// only input; recorded traces replay through the same struct.
#pragma once

#include "core/announcement.h"

#include <optional>
#include <string>

namespace fxa {

struct SimState {
    double t = 0;                 // purser clock, s (advances only while not paused)
    bool paused = false;
    bool replay = false;

    bool onGround = true;
    float gsKt = 0, iasKt = 0, aglFt = 0, mslFt = 0, vsFpm = 0;
    double lat = 0, lon = 0;

    std::optional<Phase> fmgsPhase;   // from the ToLiss FMGS; nullopt if unavailable
    int fmgsPhaseRaw = -1;
    float cruiseAltFt = 0;

    bool beacon = false;
    bool seatbelt = false;
    bool landingLights = false;
    float speedbrake = 0;             // ratio; <= 0.01 stowed (negative = armed)
    bool parkBrake = false;
    bool chocks = false;
    int enginesRunning = 0;
    int engineCount = 2;

    float doorMax = 0;                // most open passenger door, 0..1
    float doorFwd = 0;                // forward left door, 0..1
    int doorCount = 0;
    bool slidesArmed = false;
    bool slidesKnown = false;

    int pax = 0;
    float fuelKg = 0;
    bool acPower = true;              // true when unknown
    bool paxMasks = false;
    float cabinAltFt = 0;

    bool paOn = false;                // ACP PA reception knob pulled on any ACP
    float paVol = 0;                  // its volume 0..1
    bool cabOn = false;               // ACP CAB reception knob pulled
    float cabVol = 0;
    float cockpitDoor = 0;            // 0 closed .. 1 open

    bool viewExternal = false;
    bool cameraInCabin = false;
    float sunPitchDeg = 45;
    float localTimeSec = 43200;

    std::string flightNumber;
    std::string acfIcao;
    bool satcom = false;

    // ToLiss cross-check signals (VP_SPEC § 4.2, docs/TOLISS_DATAREFS.md); -1 = unknown
    int ecamPhase = -1;               // AirbusFBW/ECAMFlightPhase 1..10
    int qpacPhase = -1;               // AirbusFBW/QPACFlightPhase 1..14
    float throttleMax = kUnknownLever;   // highest thrust lever position (1 TOGA, 0.88 FLX/MCT, < 0 reverse)
    int groundSpoilers = -1;          // 1 out, 0 retracted
    int gearLeverDown = -1;
    float flapLever = -1;             // 0..1 (x4 = detent)
    float tolissVsFpm = 0;
    float cargoDoorMax = 0;
    float distToDestNm = -1;
    double zuluSec = -1;              // seconds since 00:00 UTC (X-Plane clock)

    // ToLiss sound mix (VP_SPEC § 4.5)
    float tolissVolInternal = -1;     // ISCS internal volume 0..1
    int tolissVolOverride = -1;       // ISCS "Ovrd XP Int/Ext Vol": 1 on, 0 off
    float doorBlend = -1;             // cockpit-door sound blend 0 closed .. 1 open

    static constexpr float kUnknownLever = -9.0f;
    bool throttleKnown() const { return throttleMax > -1.5f; }
};

} // namespace fxa
