// Dataref census for the probe's Snapshot / What changed? (docs/TOLISS_DATAREFS.md): enumerates every dataref with
// XPLMCountDataRefs / XPLMGetDataRefsByIndex (XPLM 4.0+) and keeps the AirbusFBW/, toliss_airbus/, ckpt/ and sbh/
// ones. Read-only, main thread, only on a button press (never in the flight loop).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace fxa {

class ProbeCensus {
public:
    // Records every matching dataref's current value. Returns how many were recorded.
    size_t snapshot();
    // Names whose value now differs from the snapshot (or that are new), sorted. The snapshot is kept.
    std::vector<std::string> changed() const;
    bool haveSnapshot() const { return !base_.empty(); }
    size_t size() const { return base_.size(); }

private:
    static std::map<std::string, std::string> read();
    std::map<std::string, std::string> base_;
};

} // namespace fxa
