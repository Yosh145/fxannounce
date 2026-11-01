// The guided probe (docs/TOLISS_DATAREFS.md, "Probe session checklist"): watches the SimState and raw ToLiss values
// each tick and ticks off what it has seen, so a probe flight verifies the assumed encodings. Pure C++; the dataref
// census behind "What changed?" lives in sim/probe_census (XPLM).
#pragma once

#include "core/sim_state.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fxa {

// Raw values the adapter hands the probe each tick (empty / -1 = missing).
struct ProbeRaw {
    std::array<std::vector<float>, 3> acpKnob;   // ACP1..3 KnobPush
    std::vector<float> paxDoors;
    std::vector<float> slides;
    std::vector<float> landingCandidates;        // one value per landing light candidate source; -1 = missing
    std::vector<std::string> landingNames;       // their dataref names
    float doorAngle = -1, ckptDoor = -1, doorBlend = -1;
    int satcomInhibit = -1;
};

struct ProbeItem {
    std::string id;
    std::string title;
    bool step = false;            // has a Start button (guided step)
    std::string instruction;      // what to do for a step
    bool done = false;
    std::string finding;          // what was seen
    std::string applyKey;         // FXAnnounce setting a finding can set ("acp_cab_index"), or ""
    std::string applyValue;
};

enum class ProbeStep { None, AcpPa, AcpCab, LandingLights, VolumeOverride };

class ProbeChecklist {
public:
    ProbeChecklist();
    void reset();
    // Every tick while active.
    void update(const SimState& s, const ProbeRaw& raw);
    // Fed by the app: the SimBrief state line and whether an OFP was read.
    void setSimbrief(bool haveOfp, const std::string& line);

    // Guided steps: the user does the action within kStepSeconds of Start.
    static constexpr double kStepSeconds = 20;
    void startStep(ProbeStep step, const SimState& s, const ProbeRaw& raw);
    ProbeStep activeStep() const { return step_; }
    double stepRemaining(double t) const { return step_ == ProbeStep::None ? 0 : std::max(0.0, stepEnd_ - t); }
    // The VolumeOverride step diffs the dataref census: the app passes the names that changed.
    void censusResult(const std::vector<std::string>& changed);
    bool wantsCensusDiff(double t) const { return step_ == ProbeStep::VolumeOverride && t >= stepEnd_; }

    const std::vector<ProbeItem>& items() const { return items_; }
    int doneCount() const;
    // Markdown for docs/TOLISS_DATAREFS.md.
    std::string summary(const std::string& header) const;

private:
    explicit ProbeChecklist(int);   // builds the item list; reset() assigns a fresh one
    ProbeItem& item(const char* id);
    void sighting(const char* id, std::map<int, std::string>& seen, int value, const SimState& s, int needed,
                  const std::set<int>& required);
    void finishStep(const SimState& s, const ProbeRaw& raw);

    std::vector<ProbeItem> items_;
    // auto items
    std::map<int, std::string> apSeen_, ecamSeen_, qpacSeen_;
    std::vector<int> apOrder_;
    std::string apWarning_;          // an APPhase change in an unexpected direction
    std::set<int> doorsOpen_, slidesArmed_;
    bool doorsClosedSeen_ = false, doorsOpenSeen_ = false;
    bool powerOff_ = false, powerOn_ = false;
    bool beltOn_ = false, beltOff_ = false, nsOn_ = false;
    bool slidesWere_ = false, slidesOn_ = false, slidesOff_ = false;
    std::set<int> satcomSeen_;
    float maxThrottle_ = SimState::kUnknownLever;
    bool takeoffRoll_ = false;
    bool spoilersOut_ = false, spoilersIn_ = false;
    float doorMax_[3] = { 0, 0, 0 };   // angle, ckpt/door, blend
    bool masks_ = false;
    // steps
    ProbeStep step_ = ProbeStep::None;
    double stepEnd_ = 0;
    ProbeRaw stepStart_;
};

} // namespace fxa
