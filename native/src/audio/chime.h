// The cabin call chime, generated in code (VP_SPEC.md D-7): two bell-like tones, high then low.
#pragma once

#include "audio/decoder.h"

#include <memory>

namespace fxa {

std::shared_ptr<PcmBuffer> makeCabinChime(int rate = 22050);

} // namespace fxa
