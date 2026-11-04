// how loud stuff is in the cockpit 4.5 pure function of settings snapshot
#pragma once

#include "core/config.h"
#include "core/sim_state.h"

namespace fxa {

enum class SoundKind { Pa, Music, Interphone, Chime };

// cabin at the camera 0 to 1 clear open door pa knob in the cabin and muffled
// thru the closed door
struct Hearing {
  float clear = 0;
  float muffled = 0;
};

// toliss door blend or the door angle
float doorOpenFraction(const Settings& set, const SimState& s);
// toliss iscs internal vol when override is on else 1
float tolissVolume(const Settings& set, const SimState& s);
// muffling off muffled 0 and clear never under the floor
Hearing cabinHearing(const Settings& set, const SimState& s);
// 0 when muted pa/music the clear voice
float soundGain(SoundKind kind, const Settings& set, const SimState& s,
                bool muted);
// muffled twin gain 0 for anything else
float muffledGain(SoundKind kind, const Settings& set, const SimState& s,
                  bool muted);
// ckpt/oh/ chan acp /anim is degrees 0-270 up to 1.5 we treat as a ratio
// already
float acpKnobVolume(float raw);

}  // namespace fxa
