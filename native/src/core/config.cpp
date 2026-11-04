#include "core/config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/strings.h"

namespace fxa {

// ini

Ini Ini::parse(std::string_view text) {
  Ini ini;
  std::string current;
  size_t pos = 0;
  // skip a utf-8 bom
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF)
    pos = 3;
  while (pos <= text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = trim(text.substr(pos, end - pos));
    pos = end + 1;
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    if (line.front() == '[' && line.back() == ']') {
      current = std::string(trim(line.substr(1, line.size() - 2)));
      ini.sectionMut(current);
      continue;
    }
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    std::string key(trim(line.substr(0, eq)));
    std::string_view value = trim(line.substr(eq + 1));
    // strip an inline comment that follows whitespace key value note
    for (size_t k = 1; k < value.size(); ++k) {
      if ((value[k] == '#' || value[k] == ';') &&
          (value[k - 1] == ' ' || value[k - 1] == '\t')) {
        value = trim(value.substr(0, k));
        break;
      }
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
      value = value.substr(1, value.size() - 2);
    if (!key.empty()) ini.set(current, key, std::string(value));
    if (end == text.size()) break;
  }
  return ini;
}

std::string Ini::serialize() const {
  std::string out;
  for (const auto& [name, sect] : sections_) {
    if (sect.empty() && name.empty()) continue;
    if (!name.empty()) {
      if (!out.empty()) out += '\n';
      out += "[" + name + "]\n";
    }
    for (const auto& [k, v] : sect) out += k + " = " + v + "\n";
  }
  return out;
}

const Ini::Section* Ini::section(std::string_view name) const {
  for (const auto& [n, s] : sections_)
    if (iequals(n, name)) return &s;
  return nullptr;
}

const std::string* Ini::get(std::string_view sect, std::string_view key) const {
  const Section* s = section(sect);
  if (!s) return nullptr;
  for (const auto& [k, v] : *s)
    if (iequals(k, key)) return &v;
  return nullptr;
}

Ini::Section& Ini::sectionMut(std::string_view name) {
  for (auto& [n, s] : sections_)
    if (iequals(n, name)) return s;
  sections_.emplace_back(std::string(name), Section{});
  return sections_.back().second;
}

void Ini::set(std::string_view sect, std::string_view key, std::string value) {
  Section& s = sectionMut(sect);
  for (auto& [k, v] : s)
    if (iequals(k, key)) {
      v = std::move(value);
      return;
    }
  s.emplace_back(std::string(key), std::move(value));
}

bool parseBool(std::string_view v, bool fallback) {
  v = trim(v);
  if (iequals(v, "1") || iequals(v, "yes") || iequals(v, "true") ||
      iequals(v, "on"))
    return true;
  if (iequals(v, "0") || iequals(v, "no") || iequals(v, "false") ||
      iequals(v, "off"))
    return false;
  return fallback;
}

// settings

bool Settings::eventEnabled(std::string_view type) const {
  for (const auto& e : disabledEvents)
    if (iequals(e, type)) return false;
  return true;
}

BoardingStart Settings::boarding() const {
  return static_cast<BoardingStart>(std::clamp(boardingStart, 0, 2));
}

namespace {

SettingDef base(const char* sec, const char* key, const char* label,
                const char* help, SettingType type, float lo = 0,
                float hi = 0) {
  SettingDef d;
  d.section = sec;
  d.key = key;
  d.label = label;
  d.help = help;
  d.type = type;
  d.min = lo;
  d.max = hi;
  return d;
}
SettingDef B(const char* sec, const char* key, const char* label,
             const char* help, bool Settings::* m, bool hidden = false) {
  SettingDef d = base(sec, key, label, help, SettingType::Bool);
  d.b = m;
  d.hidden = hidden;
  return d;
}
SettingDef I(const char* sec, const char* key, const char* label,
             const char* help, int Settings::* m, float lo, float hi,
             bool hidden = false) {
  SettingDef d = base(sec, key, label, help, SettingType::Int, lo, hi);
  d.i = m;
  d.hidden = hidden;
  return d;
}
SettingDef F(const char* sec, const char* key, const char* label,
             const char* help, float Settings::* m, float lo, float hi) {
  SettingDef d = base(sec, key, label, help, SettingType::Float, lo, hi);
  d.f = m;
  return d;
}
SettingDef S(const char* sec, const char* key, const char* label,
             const char* help, std::string Settings::* m) {
  SettingDef d = base(sec, key, label, help, SettingType::String);
  d.s = m;
  return d;
}
SettingDef C(const char* sec, const char* key, const char* label,
             const char* help, int Settings::* m,
             std::vector<const char*> names, std::vector<const char*> labels) {
  SettingDef d = base(sec, key, label, help, SettingType::Int, 0,
                      static_cast<float>(names.size() - 1));
  d.i = m;
  d.choices = std::move(names);
  d.choiceLabels = std::move(labels);
  return d;
}

std::vector<SettingDef> buildDefs() {
  using St = Settings;
  return {
      I("general", "settings_version", "", "", &St::settingsVersion, 0, 0,
        true),
      B("general", "enabled", "Announcements enabled",
        "Master switch for automatic announcements.", &St::enabled),
      F("general", "master_volume", "Master volume",
        "Scales every sound FXAnnounce plays.", &St::masterVolume, 0, 2),
      S("general", "pack_paths", "Extra pack folders",
        "Additional folders that contain Airline Packs, separated by ';'. "
        "Resources/plugins/AirlinePacks is always scanned.",
        &St::packPaths),
      B("ui", "show_log", "Show log",
        "Shows the Log tab (with the live sim inputs) and, on the Packs tab, "
        "what would play "
        "now and why.",
        &St::showLog),
      I("general", "log_level", "Log detail",
        "0 errors, 1 warnings, 2 info, 3 debug (FXAnnounce.log).",
        &St::logLevel, 0, 3),
      B("general", "recorder", "Record flight trace",
        "Writes the sim state and purser events to "
        "Output/FXAnnounce/*.fxtrace.csv for debugging.",
        &St::recorder),

      F("hearing", "pa_volume", "PA volume", "Cabin announcement volume.",
        &St::paVolume, 0, 2),
      F("hearing", "music_volume", "Boarding music volume",
        "Boarding music level in the cabin.", &St::musicVolume, 0, 2),
      F("hearing", "interphone_volume", "Interphone volume",
        "Cabin-to-cockpit call level (times the CAB knob).",
        &St::interphoneVolume, 0, 2),
      F("hearing", "chime_volume", "Cockpit call buzzer",
        "Level of the buzzer when the cabin calls the cockpit.",
        &St::chimeVolume, 0, 2),
      F("hearing", "exterior_gain", "Exterior level",
        "Level of cabin PA in external views.", &St::exteriorGain, 0, 1),
      B("hearing", "realistic", "Realistic hearing",
        "On: cabin PA is heard muffled through the closed cockpit door, clear "
        "through the open door, the ACP PA knob "
        "or from the cabin. Off: always at full PA volume.",
        &St::realisticHearing),
      B("hearing", "muffle", "Muffle through the door",
        "On: through the closed door the PA sounds muffled (low-passed), and "
        "clear when the door opens, the PA knob is "
        "pulled or you are in the cabin. Off: one unfiltered sound, never "
        "below the closed-door level.",
        &St::muffle),
      F("hearing", "floor", "Through the closed door",
        "Level of cabin PA in the cockpit with the door closed and the PA knob "
        "in. Announcements never go below it.",
        &St::hearingFloor, 0, 1),
      F("hearing", "door_leak", "Cockpit door leak",
        "Level heard through a fully open cockpit door.", &St::doorLeak, 0, 1),
      F("hearing", "muffle_cutoff_hz", "Muffle cutoff (Hz)",
        "Low-pass cutoff of the muffled sound. Lower is duller. Applies to "
        "sounds loaded after the change.",
        &St::muffleCutoffHz, 200, 4000),
      B("hearing", "use_toliss_door_blend", "Door leak from ToLiss",
        "Use ToLiss's own cockpit-door sound blend for the door leak when the "
        "aircraft provides it (otherwise the door "
        "angle).",
        &St::useTolissDoorBlend),
      B("hearing", "follow_toliss_volume", "Follow ToLiss volume",
        "When ToLiss's ISCS 'Ovrd XP Int/Ext Vol' is on, scale every cabin "
        "sound by ToLiss's internal volume. Needs "
        "the override dataref (Settings > ToLiss > Volume override dataref).",
        &St::followTolissVolume),
      I("hearing", "pa_bus", "PA audio bus",
        "X-Plane audio bus for cabin PA and music. 7 = interior (default), 8 = "
        "UI.",
        &St::paBus, 0, 10),
      I("hearing", "interphone_bus", "Interphone audio bus",
        "X-Plane audio bus for the interphone and the call buzzer. 2 = pilot "
        "headset (default), 7 = interior.",
        &St::interphoneBus, 0, 10),

      C("cabin", "boarding_start", "Boarding starts",
        "SimBrief schedule: at the ETD of the simbrief_hub plan minus the lead "
        "time (Boarding section); after a late "
        "load or without a valid plan, when a door has been open at the gate. "
        "Doors open: when a passenger door has "
        "been open at the gate. Cabin call only: when you press FWD, AFT, "
        "ALL... on the overhead CALLS panel. A cabin "
        "call at the gate always starts boarding.",
        &St::boardingStart, {"schedule", "doors", "call"},
        {"SimBrief schedule", "Doors open", "Cabin call only"}),
      F("cabin", "expedite_s", "Cabin call expedites check to (s)",
        "A cabin call after ArmDoors or after the descent starts, or an early "
        "line-up or seats-for-landing signal, "
        "shortens the cabin safety check to this (Fenix).",
        &St::expediteSeconds, 0, 120),
      F("cabin", "boarding_expedite_s", "Cabin call ends boarding in (s)",
        "A cabin call during boarding completes it this soon.",
        &St::boardingExpediteS, 0, 600),
      B("cabin", "turnaround", "Turnaround",
        "On: after deboarding the purser prepares the next leg at this gate. "
        "Off: the flight is complete after "
        "deboarding; a new SimBrief OFP from this airport, a situation load or "
        "an aircraft reload starts the next one.",
        &St::turnaround),
      F("boarding", "a319_min", "A319 lead min (min)",
        "Boarding starts this long before the ETD with an empty cabin.",
        &St::a319LeadMin, 0, 120),
      F("boarding", "a319_max", "A319 lead max (min)",
        "... and this long before it with a full cabin.", &St::a319LeadMax, 0,
        120),
      I("boarding", "a319_seats", "A319 seats",
        "Seats when the SimBrief plan doesn't give them.", &St::a319Seats, 1,
        400),
      F("boarding", "a320_min", "A320 lead min (min)",
        "Boarding starts this long before the ETD with an empty cabin.",
        &St::a320LeadMin, 0, 120),
      F("boarding", "a320_max", "A320 lead max (min)",
        "... and this long before it with a full cabin.", &St::a320LeadMax, 0,
        120),
      I("boarding", "a320_seats", "A320 seats",
        "Seats when the SimBrief plan doesn't give them.", &St::a320Seats, 1,
        400),
      F("boarding", "a321_min", "A321 lead min (min)",
        "Boarding starts this long before the ETD with an empty cabin.",
        &St::a321LeadMin, 0, 120),
      F("boarding", "a321_max", "A321 lead max (min)",
        "... and this long before it with a full cabin.", &St::a321LeadMax, 0,
        120),
      I("boarding", "a321_seats", "A321 seats",
        "Seats when the SimBrief plan doesn't give them.", &St::a321Seats, 1,
        400),
      F("boarding", "jitter_s", "Lead time jitter (s)",
        "Random +/- added once per flight.", &St::leadJitterS, 0, 600),
      F("boarding", "ofp_origin_nm", "OFP origin distance (NM)",
        "A SimBrief plan is used only when its origin airport is this close to "
        "the aircraft.",
        &St::ofpOriginNm, 1, 50),
      F("boarding", "ofp_return_nm", "Returning to the origin (NM)",
        "In flight, the Status shows 'returning' once the aircraft is this "
        "much closer to the OFP origin than at its "
        "farthest and this much farther from the destination than at its "
        "closest.",
        &St::ofpReturnNm, 1, 200),
      F("boarding", "end_before_etd_s", "Boarding complete before ETD (s)",
        "With a SimBrief plan, boarding aims to complete this long before the "
        "ETD (shortened when the ETD is close).",
        &St::endBeforeEtdS, 0, 3600),
      F("boarding", "max_wait_min", "Longest wait for the boarding time (min)",
        "A SimBrief boarding time further ahead than this means the X-Plane "
        "clock isn't set to the flight's time: "
        "the doors rule is used instead.",
        &St::maxWaitMin, 5, 720),

      S("tags", "satcom_tags", "SatCom custom tags",
        "Custom tags switched on when the ToLiss SatCom antenna is fitted "
        "(comma-separated).",
        &St::satcomTags),
      B("tags", "overwater_default", "Overwater by default",
        "Starting value of the per-flight Overwater toggle when the livery "
        "doesn't set it.",
        &St::overwaterDefault),

      F("timing", "check_multiplier", "Cabin check duration x",
        "Scales every simulated cabin check.", &St::checkMultiplier, 0.1f, 3),
      F("timing", "welcome_min_s", "Welcome interval min (s)",
        "Shortest gap between boarding welcomes.", &St::welcomeMin, 30, 900),
      F("timing", "welcome_max_s", "Welcome interval max (s)",
        "Longest gap between boarding welcomes.", &St::welcomeMax, 30, 1200),
      F("timing", "boarding_door_settle_s", "Boarding start delay (s)",
        "Doors rule: boarding starts once there has been AC power this long "
        "with a door open (or opened earlier at "
        "this gate).",
        &St::boardingDoorSettle, 0, 120),
      F("timing", "boarding_base_s", "Boarding base time (s)",
        "Boarding takes this plus the per-passenger time, times the aircraft "
        "type factor (+/- 15 %).",
        &St::boardingBaseS, 0, 3600),
      F("timing", "boarding_per_pax_s", "Boarding per passenger (s)",
        "Boarding time per passenger.", &St::boardingPerPaxS, 0, 60),
      F("timing", "boarding_min_s", "Shortest boarding (s)",
        "Boarding never takes less than this, even when the SimBrief ETD is "
        "close or has passed.",
        &St::boardingMinS, 30, 3600),
      F("timing", "arm_doors_wait_s", "Arm doors wait (s)",
        "How long ArmDoors waits for open doors to close.", &St::armDoorsWait,
        0, 600),
      F("timing", "presafety_delay_s", "Safety briefing after ArmDoors (s)",
        "The safety briefing never starts sooner than this after ArmDoors.",
        &St::preSafetyDelay, 0, 600),
      F("timing", "presafety_after_move_min_s",
        "Safety briefing after pushback min (s)",
        "The safety briefing starts this long after the aircraft first moves "
        "once boarding is complete (minimum).",
        &St::preSafetyAfterMoveMin, 0, 600),
      F("timing", "presafety_after_move_max_s",
        "Safety briefing after pushback max (s)", "... (maximum).",
        &St::preSafetyAfterMoveMax, 0, 600),
      F("timing", "ns_flick_max_s", "NO SMOKING flick (s)",
        "NO SMOKING switched off and on again within this time is the pilots' "
        "signal to the cabin.",
        &St::nsFlickMaxS, 1, 60),
      F("timing", "chain_gap_s", "Gap between calls (s)",
        "Pause between announcements that follow each other.", &St::chainGap, 0,
        30),
      F("timing", "after_takeoff_min_s", "After takeoff min (s)",
        "Delay after acceleration altitude (minimum).", &St::afterTakeoffMin, 0,
        600),
      F("timing", "after_takeoff_max_s", "After takeoff max (s)",
        "Delay after acceleration altitude (maximum).", &St::afterTakeoffMax, 0,
        900),
      F("timing", "cabin_service_min_s", "Cabin service min (s)",
        "Delay after top of climb (minimum).", &St::cabinServiceMin, 0, 3600),
      F("timing", "cabin_service_max_s", "Cabin service max (s)",
        "Delay after top of climb (maximum).", &St::cabinServiceMax, 0, 3600),
      F("timing", "descent_min_s", "Descent call min (s)",
        "Delay after the descent starts (minimum).", &St::descentMin, 0, 900),
      F("timing", "descent_max_s", "Descent call max (s)",
        "Delay after the descent starts (maximum).", &St::descentMax, 0, 900),
      F("timing", "landing_ready_agl_ft",
        "Cabin secured for landing below (ft AGL)",
        "The landing CabinSecured call waits for the approach phase or this "
        "height.",
        &St::landingReadyAglFt, 1000, 20000),
      F("timing", "landing_drop_agl_ft", "Drop landing calls below (ft AGL)",
        "Pending landing calls are dropped below this.", &St::landingDropAglFt,
        0, 5000),
      F("timing", "after_landing_fallback_s", "After landing fallback (s)",
        "If spoilers or landing lights stay out, the after-landing call comes "
        "this long after touchdown.",
        &St::afterLandingFallback, 10, 900),
      F("timing", "deboard_fallback_s", "Deboarding fallback (s)",
        "Deboarding starts this long after DisarmDoors when no door opens.",
        &St::deboardFallback, 10, 900),
      F("timing", "deboard_per_pax_s", "Deboarding per passenger (s)",
        "Deboarding time per passenger.", &St::deboardPerPax, 0, 10),
      F("timing", "deboard_min_s", "Deboarding minimum (s)",
        "Shortest deboarding.", &St::deboardMin, 0, 1800),
      F("timing", "turnaround_gap_s", "Turnaround gap (s)",
        "After deboarding, the next boarding starts this long later (if a door "
        "has been open since), or when a door "
        "closes and opens again.",
        &St::turnaroundGap, 0, 3600),
      F("timing", "seatbelt_debounce_s", "Seatbelt sign debounce (s)",
        "The sign must hold its new state this long.", &St::seatbeltDebounce, 0,
        60),
      F("timing", "interphone_retry_s", "Cabin call retry (s)",
        "Time before an unanswered cabin call buzzes again.",
        &St::interphoneRetry, 5, 120),
      I("timing", "interphone_retries", "Cabin call retries",
        "Retries before the cabin gives up.", &St::interphoneRetries, 0, 10),
      F("timing", "refuel_rate_kgs", "Refuel detection (kg/s)",
        "Fuel rising faster than this on the ground = refuelling.",
        &St::refuelRateKgs, 0.1f, 50),
      F("timing", "refuel_max_kgs", "Refuel detection max (kg/s)",
        "Faster rises are instant fuel loads (ISCS), not refuelling.",
        &St::refuelMaxKgs, 5, 2000),
      F("timing", "no_fmgs_after_s", "No-FMGS phase estimate after (s)",
        "If the FMGS phase still reads PreFlight this long after lift-off, the "
        "phase is estimated.",
        &St::noFmgsAfterS, 5, 600),
      F("timing", "dark_sun_deg", "Darkness below sun (deg)",
        "Sun elevation below which the cabin is dimmed.", &St::darkSunDeg, -18,
        10),

      I("toliss", "acp_pa_index", "ACP PA knob index",
        "Index of the PA reception knob in the ToLiss ACP arrays (see "
        "docs/TOLISS_DATAREFS.md).",
        &St::acpPaIndex, 0, 15),
      I("toliss", "acp_cab_index", "ACP CAB knob index",
        "Index of the CAB (interphone) reception knob.", &St::acpCabIndex, 0,
        15),
      F("toliss", "cabin_view_aft_m", "Cabin view distance (m)",
        "The camera counts as in the cabin when it is this far aft of the "
        "pilot's eye point.",
        &St::cabinViewAftM, 0.5f, 20),
      F("toliss", "throttle_flx_detent", "Takeoff thrust lever",
        "A thrust lever at or above this (ToLiss throttle_input: 0.88 FLX/MCT, "
        "1.0 TOGA) counts as takeoff thrust.",
        &St::throttleFlxDetent, 0.5f, 1.0f),
      I("toliss", "landing_light_source", "Landing light source",
        "Which dataref says the landing lights are on (see "
        "docs/TOLISS_DATAREFS.md): 0 sim landing_lights_on, 1 sim "
        "landing_lights_switch, 2 ToLiss left/right extended.",
        &St::landingLightSource, 0, 2),
      I("toliss", "strobe_switch_index", "Strobe switch index",
        "Entry of the strobe switch in AirbusFBW/OHPLightSwitches (0 off, 1 "
        "auto, 2 on; see docs/TOLISS_DATAREFS.md).",
        &St::strobeSwitchIndex, 0, 31),
      S("toliss", "volume_override_ref", "Volume override dataref",
        "Dataref that holds ToLiss's ISCS 'Ovrd XP Int/Ext Vol' switch, found "
        "by hand (docs/TOLISS_DATAREFS.md). "
        "Empty: unknown.",
        &St::volumeOverrideRef),

      B("ui", "visible", "", "", &St::windowVisible, true),
      I("ui", "win_left", "", "", &St::winLeft, 0, 0, true),
      I("ui", "win_top", "", "", &St::winTop, 0, 0, true),
      I("ui", "win_right", "", "", &St::winRight, 0, 0, true),
      I("ui", "win_bottom", "", "", &St::winBottom, 0, 0, true),
      I("ui", "tab", "", "", &St::tab, 0, 0, true),
      C("ui", "theme", "Theme",
        "Dark or White (also the switch in the titlebar).", &St::theme,
        {"dark", "white"}, {"Dark", "White"}),
      F("ui", "font_scale", "Text size", "Scales all text in the window.",
        &St::fontScale, 0.6f, 2.0f),
      B("ui", "color_blind", "Colour-blind colours",
        "Okabe-Ito colours for played / problem / error.", &St::colorBlind),
      B("ui", "show_timeline", "Show Timeline",
        "Shows the Timeline tab (also the checkbox in the titlebar).",
        &St::showTimeline),
  };
}

// settings tab page of each setting architecture.md 7 by ini section with the
// exceptions below
SettingPage pageFor(std::string_view sec, std::string_view key) {
  if (sec == "general") {
    if (key == "enabled") return SettingPage::Announcements;
    if (key == "master_volume") return SettingPage::Sound;
    return SettingPage::Advanced;
  }
  if (sec == "hearing")
    return key == "pa_bus" || key == "interphone_bus" ? SettingPage::Advanced
                                                      : SettingPage::Sound;
  if (sec == "cabin") {
    if (key == "expedite_s") return SettingPage::Timing;
    if (key == "boarding_expedite_s") return SettingPage::Boarding;
    return SettingPage::Announcements;
  }
  if (sec == "boarding") return SettingPage::Boarding;
  if (sec == "tags") return SettingPage::Announcements;
  if (sec == "timing") {
    for (const char* k :
         {"welcome_min_s", "welcome_max_s", "boarding_door_settle_s",
          "boarding_base_s", "boarding_per_pax_s", "boarding_min_s"})
      if (key == k) return SettingPage::Boarding;
    return SettingPage::Timing;
  }
  if (sec == "toliss") return SettingPage::ToLiss;
  if (sec == "ui")
    return key == "show_log" ? SettingPage::Advanced : SettingPage::Display;
  return SettingPage::Advanced;
}

std::string formatFloat(float v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.4g", static_cast<double>(v));
  return buf;
}

}  // namespace

const char* settingPageName(SettingPage p) {
  static const char* names[kSettingPageCount] = {
      "Announcements", "Sound",   "Boarding", "Timing",
      "ToLiss",        "Display", "Advanced"};
  const int k = static_cast<int>(p);
  return k >= 0 && k < kSettingPageCount ? names[k] : "?";
}

const std::vector<SettingDef>& settingDefs() {
  static const std::vector<SettingDef> defs = [] {
    std::vector<SettingDef> v = buildDefs();
    for (SettingDef& d : v) d.page = pageFor(d.section, d.key);
    return v;
  }();
  return defs;
}

void loadSettings(const Ini& ini, Settings& s) {
  for (const SettingDef& d : settingDefs()) {
    const std::string* v = ini.get(d.section, d.key);
    if (!v) continue;
    switch (d.type) {
      case SettingType::Bool:
        s.*d.b = parseBool(*v, s.*d.b);
        break;
      case SettingType::Int: {
        bool named = false;
        for (size_t k = 0; k < d.choices.size(); ++k)
          if (iequals(trim(*v), d.choices[k])) {
            s.*d.i = static_cast<int>(k);
            named = true;
          }
        if (named) break;
        char* end = nullptr;
        long n = std::strtol(v->c_str(), &end, 10);
        if (end != v->c_str()) {
          if (d.max > d.min)
            n = std::clamp<long>(n, static_cast<long>(d.min),
                                 static_cast<long>(d.max));
          s.*d.i = static_cast<int>(n);
        }
        break;
      }
      case SettingType::Float: {
        char* end = nullptr;
        double x = std::strtod(v->c_str(), &end);
        if (end != v->c_str() && std::isfinite(x)) {
          float fx = static_cast<float>(x);
          if (d.max > d.min) fx = std::clamp(fx, d.min, d.max);
          s.*d.f = fx;
        }
        break;
      }
      case SettingType::String:
        s.*d.s = *v;
        break;
    }
  }
  if (!ini.get("cabin", "boarding_start"))
    if (const std::string* v = ini.get("cabin", "boarding_on_call"))
      s.boardingStart = static_cast<int>(
          parseBool(*v, true) ? BoardingStart::Schedule : BoardingStart::Doors);
  if (const std::string* v = ini.get("events", "disabled")) {
    s.disabledEvents.clear();
    for (auto& e : splitList(*v)) s.disabledEvents.insert(e);
  }
  s.fontScale = std::clamp(s.fontScale, 0.6f, 2.0f);
  if (s.tab == 4 || s.tab < 0 || s.tab > 5)
    s.tab = 0;  // 4 was the inspector removed in 0.3.0
  if (s.welcomeMax < s.welcomeMin) std::swap(s.welcomeMin, s.welcomeMax);
  if (s.a319LeadMax < s.a319LeadMin) std::swap(s.a319LeadMin, s.a319LeadMax);
  if (s.a320LeadMax < s.a320LeadMin) std::swap(s.a320LeadMin, s.a320LeadMax);
  if (s.a321LeadMax < s.a321LeadMin) std::swap(s.a321LeadMin, s.a321LeadMax);
  if (s.refuelMaxKgs < s.refuelRateKgs) s.refuelMaxKgs = s.refuelRateKgs;
}

std::vector<std::string> migrateSettings(const Ini& ini, Settings& s) {
  std::vector<std::string> notes;
  int version = 1;  // files before 0.2.1 have no settings_version
  if (const std::string* v = ini.get("general", "settings_version"))
    version = std::atoi(v->c_str());
  if (version < 2) {
    if (const std::string* v = ini.get("toliss", "acp_pa_index");
        v && trim(*v) == "7") {
      s.acpPaIndex = 15;
      notes.emplace_back(
          "[toliss] acp_pa_index 7 (ILS, the old default) -> 15 (PA)");
    }
    if (const std::string* v = ini.get("hearing", "floor");
        v && std::fabs(std::strtod(v->c_str(), nullptr) - 0.25) < 1e-6) {
      s.hearingFloor = 0.10f;
      notes.emplace_back(
          "[hearing] floor 0.25 (the old default) -> 0.1 "
          "(through the closed door, muffled)");
    }
  }
  s.settingsVersion = kSettingsVersion;
  return notes;
}

void storeSettings(const Settings& s, Ini& ini) {
  for (const SettingDef& d : settingDefs()) {
    switch (d.type) {
      case SettingType::Bool:
        ini.set(d.section, d.key, s.*d.b ? "yes" : "no");
        break;
      case SettingType::Int:
        if (!d.choices.empty())
          ini.set(d.section, d.key,
                  d.choices[static_cast<size_t>(std::clamp<int>(
                      s.*d.i, 0, static_cast<int>(d.choices.size()) - 1))]);
        else
          ini.set(d.section, d.key, std::to_string(s.*d.i));
        break;
      case SettingType::Float:
        ini.set(d.section, d.key, formatFloat(s.*d.f));
        break;
      case SettingType::String:
        ini.set(d.section, d.key, s.*d.s);
        break;
    }
  }
  ini.set("events", "disabled",
          join(std::vector<std::string>(s.disabledEvents.begin(),
                                        s.disabledEvents.end()),
               ","));
}

}  // namespace fxa
