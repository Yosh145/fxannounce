// closed door as a filter 4.5 2nd order butterworth lowpass on mono int16 runs
// on the decode worker
#pragma once

#include <cstdint>
#include <vector>

namespace fxa {

// cutoff clamped 20hz to 0.45 x rate clipped to int16
std::vector<int16_t> lowPass(const std::vector<int16_t>& in, int rate,
                             float cutoffHz);

}  // namespace fxa
