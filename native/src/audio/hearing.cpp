#include "audio/hearing.h"

#include <algorithm>
#include <cmath>

namespace fxa {

namespace {

constexpr float kKnobFullDeg = 270.0f;  // toliss acp knob is 270 deg for full
constexpr float kMinInterphone = 0.15f;

float c01(float v) {
  return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

}  // namespace

float acpKnobVolume(float raw) {
  if (!std::isfinite(raw)) return 0.0f;
  return c01(raw > 1.5f ? raw / kKnobFullDeg : raw);
}

float doorOpenFraction(const Settings& set, const SimState& s) {
  if (set.useTolissDoorBlend && s.doorBlend >= 0) return c01(s.doorBlend);
  return c01(s.cockpitDoor);
}

float tolissVolume(const Settings& set, const SimState& s) {
  if (!set.followTolissVolume || s.tolissVolOverride != 1 ||
      s.tolissVolInternal < 0)
    return 1.0f;
  return c01(s.tolissVolInternal);
}

Hearing cabinHearing(const Settings& set, const SimState& s) {
  Hearing h;
  if (!set.realisticHearing) {
    h.clear = 1.0f;
    return h;
  }
  if (s.viewExternal) {
    h.clear = c01(set.exteriorGain);
    return h;
  }
  if (s.cameraInCabin) {
    h.clear = 1.0f;
    return h;
  }
  const float open = doorOpenFraction(set, s);
  const float floor = c01(set.hearingFloor);
  h.clear = c01(set.doorLeak) * open;
  if (s.paOn) h.clear = std::max(h.clear, c01(s.paVol));
  if (set.muffle) {
    // closed door lets a dull quiet copy through fades out as it opens 4.5 d14
    h.muffled = floor * (1.0f - open);
  } else {
    h.clear = std::max(h.clear, floor);
  }
  return h;
}

float soundGain(SoundKind kind, const Settings& set, const SimState& s,
                bool muted) {
  if (muted) return 0.0f;
  const float master = std::max(0.0f, set.masterVolume) * tolissVolume(set, s);
  switch (kind) {
    case SoundKind::Pa:
      return master * set.paVolume * cabinHearing(set, s).clear;
    case SoundKind::Music:
      return master * set.musicVolume * cabinHearing(set, s).clear;
    case SoundKind::Interphone:
      return master * set.interphoneVolume *
             (s.cabOn ? std::max(kMinInterphone, s.cabVol) : 1.0f);
    case SoundKind::Chime:
      return master * set.chimeVolume;
  }
  return 0.0f;
}

float muffledGain(SoundKind kind, const Settings& set, const SimState& s,
                  bool muted) {
  if (muted || !set.muffle) return 0.0f;
  const float master = std::max(0.0f, set.masterVolume) * tolissVolume(set, s);
  switch (kind) {
    case SoundKind::Pa:
      return master * set.paVolume * cabinHearing(set, s).muffled;
    case SoundKind::Music:
      return master * set.musicVolume * cabinHearing(set, s).muffled;
    default:
      return 0.0f;
  }
}

}  // namespace fxa
