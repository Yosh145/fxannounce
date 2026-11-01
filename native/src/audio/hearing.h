// How loud each sound is in the cockpit (VP_SPEC.md § 4.5). Pure function of the settings and the sim snapshot.
#pragma once

#include "core/config.h"
#include "core/sim_state.h"

namespace fxa {

enum class SoundKind { Pa, Music, Interphone, Chime, Preview };

// How far the cockpit door lets cabin sound through, 0..1 (ToLiss's door blend or the door angle).
float doorOpenFraction(const Settings& set, const SimState& s);
// ToLiss's ISCS internal volume when its override is on and followed, else 1.
float tolissVolume(const Settings& set, const SimState& s);
// The cabin PA level as heard at the camera, 0..1 before the per-kind volume.
float cabinHearing(const Settings& set, const SimState& s);
// Final channel gain for a sound of this kind (0 when muted).
float soundGain(SoundKind kind, const Settings& set, const SimState& s, bool muted);

} // namespace fxa
