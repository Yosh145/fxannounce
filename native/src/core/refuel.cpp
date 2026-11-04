#include "core/refuel.h"

namespace fxa {

void RefuelDetector::reset() {
  sampleT_ = -1;
  sampleKg_ = 0;
  until_ = -1;
}

double RefuelDetector::update(const SimState& s, const Settings& set) {
  if (s.paused) return -1;
  if (sampleT_ < 0 || s.t < sampleT_) {
    sampleT_ = s.t;
    sampleKg_ = s.fuelKg;
    return -1;
  }
  const double dt = s.t - sampleT_;
  if (dt < 1.0) return -1;
  const double rate = (s.fuelKg - sampleKg_) / dt;
  sampleT_ = s.t;
  sampleKg_ = s.fuelKg;
  if (!s.onGround || rate <= set.refuelRateKgs || rate >= set.refuelMaxKgs)
    return -1;
  const bool fresh = until_ < s.t;
  until_ = s.t + 60;
  return fresh ? rate : -1;
}

}  // namespace fxa
