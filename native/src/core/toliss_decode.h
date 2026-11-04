// toliss array decoders for adapter replay tests no xplm
#pragma once

#include <cmath>
#include <vector>

namespace fxa {

// sdspoilerarray 0 retracted 1 extended 2 no hydraulics entries 0/1 are spoiler
// 1 l/r which only ground spoilers use roll speedbrake are 2-9 1 out 0 in -1
// unknown
inline int groundSpoilersFromSd(const std::vector<float>& sd) {
  if (sd.size() < 2 || !std::isfinite(sd[0]) || !std::isfinite(sd[1]))
    return -1;
  const long a = std::lround(sd[0]), b = std::lround(sd[1]);
  if (a == 2 || b == 2) return -1;
  return (a == 1 && b == 1) ? 1 : 0;
}

// ohplightswitches strobe 0 off 1 auto 2 on -1 when the entry is missing
inline int switchAt(const std::vector<float>& switches, int index,
                    int position) {
  if (index < 0 || index >= static_cast<int>(switches.size()) ||
      !std::isfinite(switches[static_cast<size_t>(index)]))
    return -1;
  return std::lround(switches[static_cast<size_t>(index)]) == position ? 1 : 0;
}

}  // namespace fxa
