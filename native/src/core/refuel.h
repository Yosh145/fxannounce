// Refuelling detection (VP_SPEC.md § 4.2): on the ground, total fuel rising faster than refuel_rate_kgs and slower
// than refuel_max_kgs (faster is an instant ISCS load); held for 60 s. Pure C++.
#pragma once

#include "core/config.h"
#include "core/sim_state.h"

namespace fxa {

class RefuelDetector {
public:
    void reset();
    // Feed every tick. Returns the measured rate (kg/s) when refuelling was newly detected on this call, else a
    // negative number.
    double update(const SimState& s, const Settings& set);
    bool refueling(double t) const { return t < until_; }

private:
    double sampleT_ = -1;
    float sampleKg_ = 0;
    double until_ = -1;
};

} // namespace fxa
