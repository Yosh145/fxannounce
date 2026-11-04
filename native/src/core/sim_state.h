// sim snapshot from the adapter every tick only thing the purser sees traces
// replay through this
#pragma once

#include <optional>
#include <string>

#include "core/announcement.h"

namespace fxa {

struct SimState {
  double t = 0;  // purser clock stops while paused
  bool paused = false;
  bool replay = false;

  bool onGround = true;
  float gsKt = 0, iasKt = 0, aglFt = 0, mslFt = 0, vsFpm = 0;
  double lat = 0, lon = 0;

  std::optional<Phase> fmgsPhase;  // nullopt no fmgs
  int fmgsPhaseRaw = -1;
  float cruiseAltFt = 0;

  bool beacon = false;
  bool seatbelt = false;
  bool landingLights = false;  // on not just extended
  int strobeOn = -1;           // 1 on 0 not -1 unknown auto doesnt count
  int noSmoking = -1;          // 1 lit 0 unlit -1 unknown
  float speedbrake = 0;        // at most 0.01 stowed negative armed
  bool parkBrake = false;
  bool chocks = false;
  int enginesRunning = 0;
  int engineCount = 2;

  float doorMax = 0;  // 0 to 1
  float doorFwd = 0;  // l1
  int doorCount = 0;
  bool slidesArmed = false;
  bool slidesKnown = false;

  int pax = 0;
  float fuelKg = 0;
  bool acPower = true;
  bool paxMasks = false;
  float cabinAltFt = 0;

  bool paOn = false;  // any acp
  float paVol = 0;
  bool cabOn = false;
  float cabVol = 0;
  float cockpitDoor = 0;

  bool viewExternal = false;
  bool cameraInCabin = false;
  float sunPitchDeg = 45;
  float localTimeSec = 43200;

  std::string flightNumber;
  std::string acfIcao;
  bool satcom = false;

  // toliss cross checks 4.2 -1 unknown
  int ecamPhase = -1;
  int qpacPhase = -1;
  float throttleMax = kUnknownLever;  // 1 toga 0.88 flx/mct negative reverse
  int groundSpoilers = -1;            // -1 no hydraulics
  int gearLeverDown = -1;
  float flapLever = -1;  // x4 detent
  float tolissVsFpm = 0;
  float cargoDoorMax = 0;
  float distToDestNm = -1;
  double zuluSec = -1;

  // toliss sound mix 4.5
  float tolissVolInternal = -1;
  int tolissVolOverride = -1;  // ovrd xp int/ext vol
  float doorBlend = -1;        // 0 closed 1 open

  static constexpr float kUnknownLever = -9.0f;
  bool throttleKnown() const { return throttleMax > -1.5f; }
};

}  // namespace fxa
