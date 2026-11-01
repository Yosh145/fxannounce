// Settings (FXAnnounce.ini) and a small INI reader/writer also used for liveries.ini and airlinepack.cfg.
// Every setting is described once in settingDefs(), which the loader, the writer and the Settings tab share.
#pragma once

#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fxa {

// Ordered INI document: sections and keys keep file order; keys and section names compare case-insensitively.
// Lines before the first [section] belong to section "". '#' and ';' start comments.
class Ini {
public:
    using Section = std::vector<std::pair<std::string, std::string>>;

    static Ini parse(std::string_view text);
    std::string serialize() const;

    const Section* section(std::string_view name) const;
    const std::string* get(std::string_view sect, std::string_view key) const;
    void set(std::string_view sect, std::string_view key, std::string value);
    const std::vector<std::pair<std::string, Section>>& sections() const { return sections_; }

private:
    Section& sectionMut(std::string_view name);
    std::vector<std::pair<std::string, Section>> sections_;
};

bool parseBool(std::string_view v, bool fallback);

// When boarding starts (VP_SPEC § 4.1a).
enum class BoardingStart : int {
    Schedule = 0,   // SimBrief boarding time; late load or no valid OFP: the doors rule
    Doors = 1,      // a passenger door open for boarding_door_settle_s at the gate
    Call = 2,       // only a cabin call button
};

struct Settings {
    // [general]
    bool enabled = true;
    float masterVolume = 1.0f;
    std::string packPaths;          // extra pack roots, ';'-separated
    int logLevel = 2;               // 0 error .. 3 debug
    bool recorder = false;

    // [hearing]
    bool realisticHearing = true;
    float paVolume = 1.0f;
    float hearingFloor = 0.25f;
    float doorLeak = 0.6f;
    float exteriorGain = 0.0f;
    float musicVolume = 0.6f;
    float interphoneVolume = 1.0f;
    float chimeVolume = 0.8f;
    int paBus = 7;                  // XPLMAudioBus: 7 interior
    int interphoneBus = 2;          // 2 pilot headset
    bool useTolissDoorBlend = true; // door leak from ToLiss's cockpit-door sound blend when available
    bool followTolissVolume = true; // scale by ToLiss's internal volume when its ISCS override is on

    // [tags]
    std::string satcomTags = "WiFi,USB";
    bool overwaterDefault = false;

    // [cabin]
    int boardingStart = static_cast<int>(BoardingStart::Schedule);   // BoardingStart (a cabin call always starts it)
    float expediteSeconds = 15;     // a cabin call shortens the cabin safety check to this (VP_SPEC § 4.3a)

    // [boarding] SimBrief lead time before the ETD (VP_SPEC § 4.1a, D-19), minutes; seats when the OFP has none
    float a319LeadMin = 30, a319LeadMax = 34;
    int a319Seats = 144;
    float a320LeadMin = 30, a320LeadMax = 37;
    int a320Seats = 180;
    float a321LeadMin = 34, a321LeadMax = 40;
    int a321Seats = 220;
    float leadJitterS = 60;         // +/- random seconds, rolled once per flight
    float ofpOriginNm = 5;          // the OFP origin must be this close to the aircraft

    // [timing], seconds unless noted
    float checkMultiplier = 1.0f;
    float welcomeMin = 120, welcomeMax = 360;
    float boardingDoorSettle = 15;
    float boardingClosedHold = 10;
    float armDoorsWait = 120;
    float preSafetyDelay = 60;
    float chainGap = 4;
    float afterTakeoffMin = 20, afterTakeoffMax = 60;
    float cabinServiceMin = 180, cabinServiceMax = 600;
    float descentMin = 30, descentMax = 90;
    float landingReadyAglFt = 10000;
    float landingDropAglFt = 1000;
    float afterLandingFallback = 120;
    float deboardFallback = 90;
    float deboardPerPax = 1.2f;
    float deboardMin = 60;
    float turnaroundGap = 300;
    float seatbeltDebounce = 5;
    float interphoneRetry = 30;
    int interphoneRetries = 3;
    float refuelRateKgs = 2.0f;
    float refuelMaxKgs = 150.0f;    // faster rises are instant ISCS loads, not refuelling
    float darkSunDeg = 0.0f;
    float noFmgsAfterS = 30;        // APPhase still PreFlight this long after lift-off: estimate the phase (D-20)

    // [toliss] dataref interpretation (docs/TOLISS_DATAREFS.md)
    int acpPaIndex = 7;             // ACP reception knob index of PA
    int acpCabIndex = 6;            // ACP reception knob index of CAB
    float cabinViewAftM = 2.5f;     // camera this far aft of the default eye point = in the cabin
    float throttleFlxDetent = 0.85f;   // thrust lever at or above this = FLX/MCT or TOGA (takeoff thrust)
    int landingLightSource = 0;     // index into TolissAdapter's landing light candidates (probe step)
    std::string volumeOverrideRef;  // dataref holding ToLiss's ISCS "Ovrd XP Int/Ext Vol" switch (probe step)

    // [events]
    std::set<std::string> disabledEvents;

    // [ui]
    bool windowVisible = false;
    int winLeft = 0, winTop = 0, winRight = 0, winBottom = 0;
    float fontScale = 1.0f;
    int tab = 0;
    bool colorBlind = false;

    bool eventEnabled(std::string_view type) const;
    BoardingStart boarding() const;
};

enum class SettingType { Bool, Int, Float, String };

struct SettingDef {
    const char* section = "";
    const char* key = "";
    const char* label = "";
    const char* help = "";
    SettingType type = SettingType::Bool;
    float min = 0, max = 0;          // Int/Float range for the UI
    bool Settings::*b = nullptr;
    int Settings::*i = nullptr;
    float Settings::*f = nullptr;
    std::string Settings::*s = nullptr;
    bool hidden = false;             // persisted but not shown in the Settings tab (window geometry...)
    // Int settings with named values: stored by name in the ini, shown as a combo box. Index = value.
    std::vector<const char*> choices;       // ini names ("schedule", "doors", "call")
    std::vector<const char*> choiceLabels;  // UI labels
};

const std::vector<SettingDef>& settingDefs();

// Reads every known key from the ini into s (unknown keys are ignored, bad values keep the default).
void loadSettings(const Ini& ini, Settings& s);
// Writes every key of s into ini (keeps unrelated sections and keys).
void storeSettings(const Settings& s, Ini& ini);

} // namespace fxa
