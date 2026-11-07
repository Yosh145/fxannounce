// everything between xpluginenable and xplugindisable plugin.cpp just forwards
// here thru guarded
#pragma once

#include <XPLMMenus.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#include "audio/decode_cache.h"
#include "audio/player.h"
#include "core/airlines.h"
#include "core/config.h"
#include "core/pack_library.h"
#include "core/pack_resolver.h"
#include "core/purser.h"
#include "core/recorder.h"
#include "core/refuel.h"
#include "core/sim_state.h"
#include "core/simbrief.h"
#include "sim/detect.h"
#include "sim/simbrief_source.h"
#include "sim/toliss_adapter.h"
#include "ui/model.h"

namespace fxa {

class App {
 public:
  static App& get();
  ~App();  // only at library unload never calls xplm

  void start();  // paths log settings
  void enable();
  void disable();
  void stop();
  void onMessage(int msg, void* param);

  // commands menu
  void toggleWindow();
  void skip();
  void advance();
  void toggleMute();
  void toggleOverwater();
  void rescan();
  void reenable();

  // flight loops
  float detectTick();
  float mainTick(float elapsed);

  ui::UiModel* model() { return enabled_ ? &model_ : nullptr; }
  // window calls this before drawing while dormant nothing else refreshes the
  // model or picks up a scan
  void uiRefresh();

 private:
  App() = default;
  void scheduleDetection(float seconds);
  void activate(const AircraftInfo& info);
  void deactivate(const char* why);
  void enterSafeMode();
  void buildMenu();
  void destroyMenu();
  void updateMenuChecks();
  void registerCommands(bool on);
  void loadSettingsFile();
  void saveSettingsFile();
  void applySettings();
  void refreshModel();
  void fillFlight(ui::UiModel::Flight& f) const;

  // packs
  void startScan();
  void stopScan();
  void pollScan();
  void loadLiveryConfig();
  void updateAircraftType();
  void resolvePackNow(const char* why);
  void updateVariant(bool reroll);
  std::vector<std::string> customTags() const;
  SelectContext selectContext() const;
  void resetFlight(const char* why);
  void trackRefuel();
  void trackPosition();

  // simbrief 4.1a 4.2a
  void readOfp();
  void checkOfpHere(bool force);
  void updateBoardingPlan();
  // the ofp callsign picks the pack while the ofp passes its check or while it
  // is this flights latched ofp
  std::string ofpCallsign() const {
    return ofp_ && (ofpOk_ || ofpTracker_.currentIsLatched()) ? ofp_->callsign
                                                              : std::string();
  }

  // native paths trailing sep
  std::string xpRoot_, pluginsDir_, outputDir_, prefsDir_, packsDir_;
  std::string settingsPath_;

  Settings settings_;
  Ini settingsIni_;
  ui::UiModel model_;

  bool enabled_ = false;
  bool active_ = false;
  bool muted_ = false;
  bool overwater_ = false;
  AircraftInfo aircraft_;
  AircraftType type_;
  Ini tlscfg_;  // informational neo fallback

  // sim
  TolissAdapter adapter_;
  SimState sim_;
  Recorder recorder_;
  std::map<std::string, std::string>
      rawLast_;  // last raw values written to the trace
  double rawAt_ = -1;

  // library_ swapped whole when a scan finishes main thread only
  std::shared_ptr<const PackLibrary> library_;
  AirlineTable airlines_;
  LiveryConfig liveryCfg_;
  Resolution resolution_;
  std::string resolvedFlightNumber_;
  std::string activePackName_;
  int variant_ = 0;
  std::mt19937 rng_{std::random_device{}()};

  // created at enable destroyed purser then player then cache
  std::unique_ptr<DecodeCache> cache_;
  std::unique_ptr<Player> player_;
  std::unique_ptr<Purser> purser_;

  // refuel teleport 4.2 4.4
  RefuelDetector refuel_;
  bool havePos_ = false;
  double lastLat_ = 0, lastLon_ = 0;

  std::thread scanThread_;
  std::atomic<bool> scanCancel_{false};
  std::atomic<bool> scanDone_{false};
  std::mutex scanMu_;
  std::shared_ptr<PackLibrary> scanResult_;
  bool scanning_ = false;

  // simbrief_hub
  SimbriefSource sbh_;
  std::optional<Ofp> ofp_;
  std::string ofpWhy_;  // empty when ofp_ is set
  bool ofpOk_ = false;
  std::string ofpCheck_;
  OfpPlace originPlace_, destPlace_;
  int originSeqno_ = -1;
  OfpTracker ofpTracker_;  // en route returning arrived
  bool lastTookOff_ = false;
  bool packWasLocked_ = false;  // next flight unlocks it resolve again
  double nextOfpCheck_ = -1;
  BoardingPlan plan_;
  double leadJitter_ = 0;
  int jitterSeqno_ = -1;
  int planPax_ = -1;
  Family planFamily_ = Family::Unknown;
  std::string resolvedCallsign_;
  std::string planLogged_;
  int ofpLoggedSeqno_ = -1;
  std::string simbriefStatus(int* tone) const;

  AircraftInfo loaded_;             // any aircraft for the dormant card
  bool hiddenWhenDormant_ = false;  // reopen on the next toliss
  std::string packPathsScanned_;    // changed paths rescan
  std::string unsupportedLogged_;

  XPLMFlightLoopID detectLoop_ = nullptr;
  XPLMFlightLoopID mainLoop_ = nullptr;
  double detectUntil_ = 0;

  XPLMMenuID menu_ = nullptr;
  int menuItem_ = -1;
  int miWindow_ = -1, miMute_ = -1, miOverwater_ = -1, miRescan_ = -1,
      miReenable_ = -1;

  struct Cmd {
    const char* name;
    const char* desc;
    void (App::*fn)();
    XPLMCommandRef ref = nullptr;
  };
  Cmd cmds_[6] = {
      {"fxannounce/window_toggle", "FXAnnounce: show/hide the window",
       &App::toggleWindow},
      {"fxannounce/skip", "FXAnnounce: skip the current announcement",
       &App::skip},
      {"fxannounce/advance", "FXAnnounce: fire the next pending event now",
       &App::advance},
      {"fxannounce/mute_toggle", "FXAnnounce: mute/unmute", &App::toggleMute},
      {"fxannounce/overwater_toggle", "FXAnnounce: toggle overwater flight",
       &App::toggleOverwater},
      {"fxannounce/rescan", "FXAnnounce: rescan Airline Packs", &App::rescan},
  };
  bool commandsRegistered_ = false;
  static int commandHandler(XPLMCommandRef cmd, XPLMCommandPhase phase,
                            void* refcon);

  // purser call buttons pass through handlers 4.3a architecture 4.4
  struct WatchedCmd {
    const char* name;
    bool cabinCall;  // false mech log only
    XPLMCommandRef ref = nullptr;
  };
  WatchedCmd purserCmds_[6] = {
      {"AirbusFBW/purser/fwd", true},  {"AirbusFBW/purser/aft", true},
      {"AirbusFBW/purser/all", true},  {"AirbusFBW/purser/mid", true},
      {"AirbusFBW/purser/exit", true}, {"AirbusFBW/purser/mech", false},
  };
  bool purserWatched_ = false;
  void watchPurserCalls(bool on);
  static int purserCallHandler(XPLMCommandRef cmd, XPLMCommandPhase phase,
                               void* refcon);
  static void menuHandler(void* menuRef, void* itemRef);
};

}  // namespace fxa
