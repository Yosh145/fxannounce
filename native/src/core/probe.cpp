#include "core/probe.h"

#include "core/strings.h"

#include <cmath>
#include <cstdio>

namespace fxa {

namespace {

std::string context(const SimState& s)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s, %.0f kt, %.0f ft AGL, %+.0f fpm", s.onGround ? "ground" : "air",
                  static_cast<double>(s.gsKt), static_cast<double>(s.aglFt), static_cast<double>(s.vsFpm));
    return buf;
}

std::string indexList(const std::set<int>& v)
{
    std::string out;
    for (int k : v) out += (out.empty() ? "" : ", ") + std::to_string(k);
    return out.empty() ? "-" : out;
}

float at(const std::vector<float>& v, size_t k) { return k < v.size() ? v[k] : 0.0f; }

bool volumeLike(const std::string& name)
{
    return icontains(name, "vol") || icontains(name, "ovrd") || icontains(name, "override");
}

} // namespace

ProbeChecklist::ProbeChecklist() { reset(); }

void ProbeChecklist::reset()
{
    *this = ProbeChecklist(0);
}

ProbeChecklist::ProbeChecklist(int)
{
    auto add = [&](const char* id, const char* title, const char* instruction = nullptr) {
        ProbeItem it;
        it.id = id;
        it.title = title;
        it.step = instruction != nullptr;
        if (instruction) it.instruction = instruction;
        items_.push_back(it);
    };
    add("power", "AC power off and on (ACBusVoltages)");
    add("doors", "Passenger doors closed and open (PaxDoorArray indices)");
    add("acp_pa", "ACP PA reception knob index",
        "Press Start, then pull the PA reception knob on ACP 1 (captain's ACP) within 20 s.");
    add("acp_cab", "ACP CAB reception knob index",
        "Press Start, then pull the CAB reception knob on ACP 1 within 20 s.");
    add("signs", "Seatbelt signs on and off (SeatBeltSignsOn)");
    add("slides", "Slides armed and disarmed (SlideArmedArray)");
    add("satcom", "SatCom antenna (SatComObjInhibit values)");
    add("flight_no", "MCDU flight number (toliss_airbus/init/flight_no)");
    add("simbrief", "SimBrief OFP from simbrief_hub");
    add("landing", "Landing light source",
        "Press Start, then switch the landing lights ON (or OFF) within 20 s.");
    add("volume", "ToLiss volume override switch",
        "Press Start, then toggle ISCS > Sound > 'Ovrd XP Int/Ext Vol' within 20 s. The probe then compares every "
        "ToLiss dataref.");
    add("cockpit_door", "Cockpit door (CockpitDoorAngle, ckpt/door, FwdCockpitDoorBlend)");
    add("apphase", "FMGS phase values (APPhase 0-7)");
    add("ecam", "ECAM flight phase values (ECAMFlightPhase 1-10)");
    add("qpac", "QPAC flight phase values (QPACFlightPhase 1-14)");
    add("throttle", "Thrust lever position during the takeoff roll (throttle_input)");
    add("spoilers", "Ground spoilers out and stowed after landing (SDSpoilerArray)");
    add("masks", "Passenger oxygen masks (PaxOxyMask)");
}

ProbeItem& ProbeChecklist::item(const char* id)
{
    for (ProbeItem& it : items_)
        if (it.id == id) return it;
    return items_.front();   // not reached: every id is created in the constructor
}

int ProbeChecklist::doneCount() const
{
    int n = 0;
    for (const ProbeItem& it : items_) n += it.done ? 1 : 0;
    return n;
}

void ProbeChecklist::sighting(const char* id, std::map<int, std::string>& seen, int value, const SimState& s, int needed,
                              const std::set<int>& required)
{
    if (value < 0 || seen.count(value)) return;
    seen[value] = context(s);
    ProbeItem& it = item(id);
    std::string f;
    for (const auto& [v, ctx] : seen) f += (f.empty() ? "" : "; ") + std::to_string(v) + " (" + ctx + ")";
    it.finding = f;
    bool all = static_cast<int>(seen.size()) >= needed;
    for (int r : required) all = all && seen.count(r);
    it.done = all;
}

void ProbeChecklist::setSimbrief(bool haveOfp, const std::string& line)
{
    ProbeItem& it = item("simbrief");
    it.finding = line;
    if (haveOfp) it.done = true;
}

void ProbeChecklist::update(const SimState& s, const ProbeRaw& raw)
{
    // Power
    (s.acPower ? powerOn_ : powerOff_) = true;
    {
        ProbeItem& it = item("power");
        it.done = powerOn_ && powerOff_;
        it.finding = std::string(powerOff_ ? "seen unpowered" : "not yet seen unpowered") + ", " +
                     (powerOn_ ? "seen powered" : "not yet seen powered");
    }
    // Doors
    for (size_t k = 0; k < raw.paxDoors.size(); ++k)
        if (raw.paxDoors[k] > 0.5f) doorsOpen_.insert(static_cast<int>(k));
    if (!raw.paxDoors.empty()) {
        if (s.doorMax < 0.05f) doorsClosedSeen_ = true;
        if (s.doorMax > 0.5f) doorsOpenSeen_ = true;
        ProbeItem& it = item("doors");
        it.done = doorsClosedSeen_ && doorsOpenSeen_;
        it.finding = "indices seen open: " + indexList(doorsOpen_) + (doorsClosedSeen_ ? "; seen all closed" : "");
    }
    // Signs
    (s.seatbelt ? beltOn_ : beltOff_) = true;
    item("signs").done = beltOn_ && beltOff_;
    item("signs").finding = std::string(beltOn_ ? "on seen" : "on not seen") + ", " + (beltOff_ ? "off seen" : "off not seen");
    // Slides
    for (size_t k = 0; k < raw.slides.size(); ++k)
        if (std::fabs(raw.slides[k]) > 0.01f) slidesArmed_.insert(static_cast<int>(k));
    if (s.slidesKnown) {
        if (s.slidesArmed && !slidesWere_) slidesOn_ = true;
        if (!s.slidesArmed && slidesWere_) slidesOff_ = true;
        slidesWere_ = s.slidesArmed;
        ProbeItem& it = item("slides");
        it.done = slidesOn_ && slidesOff_;
        it.finding = "indices seen armed: " + indexList(slidesArmed_) + (slidesOn_ ? "; armed seen" : "") +
                     (slidesOff_ ? "; disarmed seen" : "");
    }
    // SatCom
    if (raw.satcomInhibit >= 0) {
        satcomSeen_.insert(raw.satcomInhibit);
        ProbeItem& it = item("satcom");
        it.finding = "values seen: " + indexList(satcomSeen_) + " (0 = antenna fitted, assumed)";
        it.done = satcomSeen_.size() >= 2;
    }
    // Flight number
    if (!s.flightNumber.empty()) {
        ProbeItem& it = item("flight_no");
        it.done = true;
        it.finding = "\"" + s.flightNumber + "\"";
    }
    // Cockpit door sources
    const float src[3] = { raw.doorAngle, raw.ckptDoor, raw.doorBlend };
    for (int k = 0; k < 3; ++k) doorMax_[k] = std::max(doorMax_[k], src[k]);
    {
        ProbeItem& it = item("cockpit_door");
        char buf[160];
        std::snprintf(buf, sizeof(buf), "max seen: CockpitDoorAngle %.2f, ckpt/door %.2f, FwdCockpitDoorBlend %.2f",
                      static_cast<double>(doorMax_[0]), static_cast<double>(doorMax_[1]), static_cast<double>(doorMax_[2]));
        it.finding = buf;
        it.done = doorMax_[0] > 0.3f || doorMax_[1] > 0.3f || doorMax_[2] > 0.3f;
    }
    // Flight phases: every value with its first context. APPhase: all but GA (6) needed.
    if (s.fmgsPhaseRaw >= 0 && (apOrder_.empty() || apOrder_.back() != s.fmgsPhaseRaw)) {
        if (!apOrder_.empty()) {
            const int prev = apOrder_.back(), now = s.fmgsPhaseRaw;
            const bool ok = now > prev || prev == 6 || prev == 7 || now == 0;
            if (!ok) apWarning_ = "unexpected order: " + std::to_string(prev) + " -> " + std::to_string(now);
        }
        apOrder_.push_back(s.fmgsPhaseRaw);
    }
    sighting("apphase", apSeen_, s.fmgsPhaseRaw, s, 7, { 0, 1, 2, 3, 4, 5, 7 });
    sighting("ecam", ecamSeen_, s.ecamPhase, s, 9, { 3, 5, 8 });
    sighting("qpac", qpacSeen_, s.qpacPhase, s, 10, { 6, 11 });
    // Thrust levers on the takeoff roll
    if (s.onGround && s.gsKt > 40 && s.throttleKnown()) {
        takeoffRoll_ = true;
        maxThrottle_ = std::max(maxThrottle_, s.throttleMax);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "highest %.2f (FLX/MCT 0.88, TOGA 1.0 expected)", static_cast<double>(maxThrottle_));
        item("throttle").finding = buf;
        item("throttle").done = true;
    }
    // Ground spoilers
    if (s.groundSpoilers == 1) spoilersOut_ = true;
    if (spoilersOut_ && s.groundSpoilers == 0) spoilersIn_ = true;
    if (s.groundSpoilers >= 0) {
        item("spoilers").done = spoilersOut_ && spoilersIn_;
        item("spoilers").finding = std::string(spoilersOut_ ? "out seen" : "out not seen") + (spoilersIn_ ? ", stowed after" : "");
    }
    // Masks
    if (s.paxMasks) masks_ = true;
    item("masks").done = masks_;
    item("masks").finding = masks_ ? "deployed seen" : "not seen deployed";

    // Guided steps
    if (step_ != ProbeStep::None) finishStep(s, raw);
}

void ProbeChecklist::startStep(ProbeStep step, const SimState& s, const ProbeRaw& raw)
{
    step_ = step;
    stepEnd_ = s.t + kStepSeconds;
    stepStart_ = raw;
}

void ProbeChecklist::finishStep(const SimState& s, const ProbeRaw& raw)
{
    const bool timeUp = s.t >= stepEnd_;
    switch (step_) {
    case ProbeStep::AcpPa:
    case ProbeStep::AcpCab: {
        ProbeItem& it = item(step_ == ProbeStep::AcpPa ? "acp_pa" : "acp_cab");
        for (int a = 0; a < 3; ++a) {
            const auto& before = stepStart_.acpKnob[static_cast<size_t>(a)];
            const auto& now = raw.acpKnob[static_cast<size_t>(a)];
            for (size_t k = 0; k < now.size(); ++k) {
                if (std::fabs(now[k] - at(before, k)) < 0.5f) continue;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "ACP%d KnobPush[%d] %.0f -> %.0f", a + 1, static_cast<int>(k),
                              static_cast<double>(at(before, k)), static_cast<double>(now[k]));
                it.finding = buf;
                it.done = true;
                it.applyKey = step_ == ProbeStep::AcpPa ? "acp_pa_index" : "acp_cab_index";
                it.applyValue = std::to_string(k);
                step_ = ProbeStep::None;
                return;
            }
        }
        if (timeUp) {
            it.finding = "no KnobPush entry moved within 20 s; press Start and try again";
            step_ = ProbeStep::None;
        }
        return;
    }
    case ProbeStep::LandingLights: {
        ProbeItem& it = item("landing");
        std::string moved;
        int first = -1;
        for (size_t k = 0; k < raw.landingCandidates.size(); ++k) {
            const float b = at(stepStart_.landingCandidates, k), n = raw.landingCandidates[k];
            if (b < 0 || n < 0 || std::fabs(n - b) < 0.3f) continue;
            if (first < 0) first = static_cast<int>(k);
            moved += (moved.empty() ? "" : ", ") + std::to_string(k) + " " +
                     (k < raw.landingNames.size() ? raw.landingNames[k] : std::string("?"));
        }
        if (first >= 0) {
            it.finding = "follows the switch: " + moved;
            it.done = true;
            it.applyKey = "landing_light_source";
            it.applyValue = std::to_string(first);
            step_ = ProbeStep::None;
        } else if (timeUp) {
            it.finding = "no candidate moved within 20 s; press Start and try again";
            step_ = ProbeStep::None;
        }
        return;
    }
    case ProbeStep::VolumeOverride:
        return;   // ends when the app passes the census diff (censusResult)
    case ProbeStep::None:
        return;
    }
}

void ProbeChecklist::censusResult(const std::vector<std::string>& changed)
{
    if (step_ != ProbeStep::VolumeOverride) return;
    step_ = ProbeStep::None;
    ProbeItem& it = item("volume");
    std::vector<std::string> likely, other;
    for (const std::string& n : changed) (volumeLike(n) ? likely : other).push_back(n);
    if (!likely.empty()) {
        it.finding = "changed: " + join(likely, ", ");
        if (!other.empty()) it.finding += " (and " + std::to_string(other.size()) + " others)";
        it.done = true;
        it.applyKey = "volume_override_ref";
        it.applyValue = likely.front();
    } else if (!other.empty()) {
        std::vector<std::string> some(other.begin(), other.begin() + static_cast<long>(std::min<size_t>(other.size(), 8)));
        it.finding = "no volume-like dataref changed; others: " + join(some, ", ") + (other.size() > 8 ? ", ..." : "");
    } else {
        it.finding = "nothing changed; press Start and toggle the ISCS switch within 20 s";
    }
}

std::string ProbeChecklist::summary(const std::string& header) const
{
    std::string out = header + "\n\n| Item | Done | Seen |\n|---|---|---|\n";
    for (const ProbeItem& it : items_) {
        std::string f = it.finding.empty() ? "-" : it.finding;
        if (it.id == "apphase" && !apWarning_.empty()) f += " (" + apWarning_ + ")";
        for (char& c : f)
            if (c == '|') c = '/';
        out += "| " + it.title + " | " + (it.done ? "yes" : "no") + " | " + f + " |\n";
    }
    std::string order;
    for (int v : apOrder_) order += (order.empty() ? "" : " > ") + std::to_string(v);
    if (!order.empty()) out += "\nAPPhase sequence: " + order + "\n";
    return out;
}

} // namespace fxa
