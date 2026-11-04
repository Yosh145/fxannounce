// cockpit buzzer pcm at rate
#pragma once

#include <memory>

#include "audio/decoder.h"

namespace fxa {

std::shared_ptr<PcmBuffer> makeCockpitBuzzer(int rate = 22050);

}  // namespace fxa
