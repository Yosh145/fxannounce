#include "audio/hearing.h"

#include <algorithm>

namespace fxa {

float doorOpenFraction(const Settings& set, const SimState& s)
{
    // ToLiss's own cockpit-door sound blend when it has one, else the door angle (VP_SPEC § 4.5).
    if (set.useTolissDoorBlend && s.doorBlend >= 0) return std::clamp(s.doorBlend, 0.0f, 1.0f);
    return std::clamp(s.cockpitDoor, 0.0f, 1.0f);
}

float tolissVolume(const Settings& set, const SimState& s)
{
    if (!set.followTolissVolume || s.tolissVolOverride != 1 || s.tolissVolInternal < 0) return 1.0f;
    return std::clamp(s.tolissVolInternal, 0.0f, 1.0f);
}

float cabinHearing(const Settings& set, const SimState& s)
{
    if (!set.realisticHearing) return 1.0f;
    if (s.viewExternal) return std::clamp(set.exteriorGain, 0.0f, 1.0f);
    float g = std::clamp(set.hearingFloor, 0.0f, 1.0f);
    g = std::max(g, std::clamp(set.doorLeak, 0.0f, 1.0f) * doorOpenFraction(set, s));
    if (s.paOn) g = std::max(g, std::clamp(s.paVol, 0.0f, 1.0f));
    if (s.cameraInCabin) g = 1.0f;
    return g;
}

float soundGain(SoundKind kind, const Settings& set, const SimState& s, bool muted)
{
    if (muted && kind != SoundKind::Preview) return 0.0f;
    const float master = std::max(0.0f, set.masterVolume) * (kind == SoundKind::Preview ? 1.0f : tolissVolume(set, s));
    switch (kind) {
    case SoundKind::Pa: return master * set.paVolume * cabinHearing(set, s);
    case SoundKind::Music: return master * set.musicVolume * cabinHearing(set, s);
    case SoundKind::Interphone: return master * set.interphoneVolume * (s.cabOn ? std::max(0.15f, s.cabVol) : 1.0f);
    case SoundKind::Chime: return master * set.chimeVolume;
    case SoundKind::Preview: return master;
    }
    return 0.0f;
}

} // namespace fxa
