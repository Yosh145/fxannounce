// settings tiny ini reader/writer also liveries.ini and airlinepack.cfg
// settingdefs is the one list loader writer and settings tab all use
#pragma once

#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fxa {

// keeps file order case insensitive keys lines before the first section go in
// section and are comments
class Ini {
 public:
  using Section = std::vector<std::pair<std::string, std::string>>;

  static Ini parse(std::string_view text);
  std::string serialize() const;

  const Section* section(std::string_view name) const;
  const std::string* get(std::string_view sect, std::string_view key) const;
  void set(std::string_view sect, std::string_view key, std::string value);
  const std::vector<std::pair<std::string, Section>>& sections() const {
    return sections_;
  }

 private:
  Section& sectionMut(std::string_view name);
  std::vector<std::pair<std::string, Section>> sections_;
};

bool parseBool(std::string_view v, bool fallback);

// vp_spec 4.1a
enum class BoardingStart : int {
  Schedule = 0,  // simbrief time late load or bad ofp falls back to doors
  Doors = 1,
  Call = 2,
};

// bump when the layout changes migratesettings upgrades old files
constexpr int kSettingsVersion = 2;

struct Settings {
  // general
  int settingsVersion = kSettingsVersion;
  bool enabled = true;
  float masterVolume = 1.0f;
  std::string packPaths;  // separated
  int logLevel = 2;       // 0 error to 3 debug
  bool recorder = false;

  // hearing
  bool realisticHearing = true;
  float paVolume = 1.0f;
  float hearingFloor = 0.10f;  // closed door level the muffled voice 4.5
  float doorLeak = 0.6f;
  bool muffle = true;  // lowpass behind the door off one plain voice never
                       // under the floor
  float muffleCutoffHz = 800;
  float exteriorGain = 0.0f;
  float musicVolume = 0.6f;
  float interphoneVolume = 1.0f;
  float chimeVolume = 0.8f;  // buzzer ini key still says chime
  int paBus = 7;             // 7 interior
  int interphoneBus = 2;     // 2 pilot headset
  bool useTolissDoorBlend = true;
  bool followTolissVolume = true;  // only when the iscs override is on

  // tags
  std::string satcomTags = "WiFi,USB";
  bool overwaterDefault = false;

  // cabin
  int boardingStart = static_cast<int>(
      BoardingStart::Schedule);  // a cabin call always starts boarding anyway
  float expediteSeconds = 15;    // 4.3a
  float boardingExpediteS = 60;  // d21
  bool turnaround = false;       // d26

  // boarding lead before etd in minutes 4.1a d19 seats used when the ofp has
  // none
  float a319LeadMin = 30, a319LeadMax = 34;
  int a319Seats = 144;
  float a320LeadMin = 30, a320LeadMax = 37;
  int a320Seats = 180;
  float a321LeadMin = 34, a321LeadMax = 40;
  int a321Seats = 220;
  float leadJitterS = 60;  // rolled once per flight
  float ofpOriginNm = 5;
  float ofpReturnNm = 10;     // returning threshold 4.2a
  float endBeforeEtdS = 600;  // 4.1b
  float maxWaitMin = 60;      // past this the xp clock is probably wrong

  // timing seconds unless noted
  float checkMultiplier = 1.0f;
  float welcomeMin = 120, welcomeMax = 360;
  float boardingDoorSettle = 15;
  float boardingBaseS = 300;  // base and per pax times type factor 4.1b
  float boardingPerPaxS = 6;
  float boardingMinS = 300;
  float armDoorsWait = 120;
  float preSafetyDelay = 60;  // d22
  float preSafetyAfterMoveMin = 20,
        preSafetyAfterMoveMax = 40;  // and this long after first movement
  float nsFlickMaxS = 10;            // ns flick window 4.2
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
  float refuelMaxKgs = 150.0f;  // faster instant iscs load not refuelling
  float darkSunDeg = 0.0f;
  float noFmgsAfterS = 30;  // d20

  // toliss dataref interpretation see toliss_datarefs.md
  int acpPaIndex = 15;  // pa knob index 7 is ils not pa
  int acpCabIndex = 6;
  float cabinViewAftM = 2.5f;       // camera this far aft in the cabin
  float throttleFlxDetent = 0.85f;  // flx/mct or toga
  int landingLightSource = 0;       // which landing light candidate
  int strobeSwitchIndex =
      7;  // ohplightswitches index of the strobe 0 off 1 auto 2 on
  std::string volumeOverrideRef;  // iscs ovrd xp vol switch unknown until
                                  // someone finds it

  // events
  std::set<std::string> disabledEvents;

  // ui
  bool windowVisible = false;
  int winLeft = 0, winTop = 0, winRight = 0, winBottom = 0;
  float fontScale = 1.0f;
  int tab = 0;  // stable ids 4 used to be inspector
  bool colorBlind = false;
  int theme = 0;  // 0 dark 1 white
  bool showTimeline = true;
  bool showLog = false;

  bool eventEnabled(std::string_view type) const;
  BoardingStart boarding() const;
};

enum class SettingType { Bool, Int, Float, String };

// settings tab page has nothing to do with the ini section
enum class SettingPage {
  Announcements,
  Sound,
  Boarding,
  Timing,
  ToLiss,
  Display,
  Advanced,
  Count
};
constexpr int kSettingPageCount = static_cast<int>(SettingPage::Count);
const char* settingPageName(SettingPage p);

struct SettingDef {
  const char* section = "";
  const char* key = "";
  const char* label = "";
  const char* help = "";
  SettingType type = SettingType::Bool;
  float min = 0, max = 0;
  bool Settings::* b = nullptr;
  int Settings::* i = nullptr;
  float Settings::* f = nullptr;
  std::string Settings::* s = nullptr;
  bool hidden = false;  // window geometry etc
  SettingPage page = SettingPage::Advanced;
  // named ints stored by name combo in the ui index value
  std::vector<const char*> choices;
  std::vector<const char*> choiceLabels;
};

const std::vector<SettingDef>& settingDefs();

// unknown keys ignored bad values keep the default
void loadSettings(const Ini& ini, Settings& s);
// keeps unrelated stuff in the ini
void storeSettings(const Settings& s, Ini& ini);
// fixes old wrong defaults in a whole file not for single key updates returns
// log lines
std::vector<std::string> migrateSettings(const Ini& ini, Settings& s);

}  // namespace fxa
