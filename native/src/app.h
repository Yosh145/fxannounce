// App: everything the plugin does between XPluginEnable and XPluginDisable. plugin.cpp only forwards the
// XPLM entry points here, each wrapped in fxa::guarded (ARCHITECTURE.md § 4).
#pragma once

#include "audio/decode_cache.h"
#include "audio/player.h"
#include "core/airlines.h"
#include "core/config.h"
#include "core/pack_library.h"
#include "core/pack_resolver.h"
#include "core/probe.h"
#include "core/purser.h"
#include "core/recorder.h"
#include "core/refuel.h"
#include "core/sim_state.h"
#include "core/simbrief.h"
#include "sim/detect.h"
#include "sim/probe_census.h"
#include "sim/simbrief_source.h"
#include "sim/toliss_adapter.h"
#include "ui/model.h"

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

namespace fxa {

class App {
public:
    static App& get();
    ~App();   // only reached at library unload; never calls XPLM (see app.cpp)

    void start();     // XPluginStart: paths, log, settings
    void enable();    // XPluginEnable
    void disable();   // XPluginDisable
    void stop();      // XPluginStop
    void onMessage(int msg, void* param);

    // Commands and menu actions
    void toggleWindow();
    void skip();
    void advance();
    void toggleMute();
    void toggleOverwater();
    void rescan();
    void reenable();

    // Flight loop bodies
    float detectTick();
    float mainTick(float elapsed);

    ui::UiModel* model() { return enabled_ ? &model_ : nullptr; }

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

    // Packs
    void startScan();
    void stopScan();
    void pollScan();
    void loadLiveryConfig();
    void updateAircraftType();
    void resolvePackNow(const char* why);
    std::vector<std::string> customTags() const;
    SelectContext selectContext() const;
    void resetFlight(const char* why);
    void trackRefuel();
    void trackPosition();

    // SimBrief (VP_SPEC § 4.1a, § 4.2a)
    void readOfp();
    void checkOfpHere(bool force);
    void updateBoardingPlan();
    std::string ofpCallsign() const { return ofpOk_ && ofp_ ? ofp_->callsign : std::string(); }

    // Probe (Inspector tab)
    void probeStart(const std::string& id);
    void probeApply(const std::string& key, const std::string& value);
    void probeSummary();

    // Paths (native, with trailing separator)
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
    Ini tlscfg_;                 // <livery>/livery.tlscfg (informational, NEO fallback)

    // Sim
    TolissAdapter adapter_;
    SimState sim_;
    Recorder recorder_;
    std::map<std::string, std::string> rawLast_;   // last raw ToLiss values written to the trace
    double rawAt_ = -1;

    // Packs: library_ is replaced as a whole when a scan finishes (main thread only).
    std::shared_ptr<const PackLibrary> library_;
    AirlineTable airlines_;
    LiveryConfig liveryCfg_;
    Resolution resolution_;
    std::string resolvedFlightNumber_;
    std::string activePackName_;  // pack the session variant was rolled for
    int variant_ = 0;
    std::mt19937 rng_{ std::random_device{}() };

    // Purser and audio (created at enable, destroyed at disable: purser, then player, then cache)
    std::unique_ptr<DecodeCache> cache_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Purser> purser_;

    // Refuel detection and teleport detection (VP_SPEC § 4.2, § 4.4)
    RefuelDetector refuel_;
    bool havePos_ = false;
    double lastLat_ = 0, lastLon_ = 0;

    std::thread scanThread_;
    std::atomic<bool> scanCancel_{ false };
    std::atomic<bool> scanDone_{ false };
    std::mutex scanMu_;
    std::shared_ptr<PackLibrary> scanResult_;
    bool scanning_ = false;

    // SimBrief through simbrief_hub
    SimbriefSource sbh_;
    std::optional<Ofp> ofp_;
    std::string ofpWhy_;          // why there is no usable OFP ("" when ofp_ is set)
    bool ofpOk_ = false;          // ofp_ passed its check here
    std::string ofpCheck_;        // the check's reason when it failed
    bool originFound_ = false;
    double originLat_ = 0, originLon_ = 0;
    int originSeqno_ = -1;        // OFP the origin was looked up for
    double nextOfpCheck_ = -1;
    BoardingPlan plan_;
    double leadJitter_ = 0;
    int jitterSeqno_ = -1;
    int planPax_ = -1;
    Family planFamily_ = Family::Unknown;
    std::string resolvedCallsign_;
    std::string planLogged_;      // last plan summary written to the log and trace
    int ofpLoggedSeqno_ = -1;
    std::string simbriefStatus(int* tone) const;

    // Probe
    ProbeChecklist probe_;
    ProbeCensus census_;
    std::vector<std::string> censusChanged_;
    std::string unsupportedLogged_;   // acf path of an unsupported ToLiss already logged

    XPLMFlightLoopID detectLoop_ = nullptr;
    XPLMFlightLoopID mainLoop_ = nullptr;
    double detectUntil_ = 0;

    XPLMMenuID menu_ = nullptr;
    int menuItem_ = -1;
    int miWindow_ = -1, miMute_ = -1, miOverwater_ = -1, miRescan_ = -1, miReenable_ = -1;

    struct Cmd {
        const char* name;
        const char* desc;
        void (App::*fn)();
        XPLMCommandRef ref = nullptr;
    };
    Cmd cmds_[6] = {
        { "fxannounce/window_toggle", "FXAnnounce: show/hide the window", &App::toggleWindow },
        { "fxannounce/skip", "FXAnnounce: skip the current announcement", &App::skip },
        { "fxannounce/advance", "FXAnnounce: fire the next pending event now", &App::advance },
        { "fxannounce/mute_toggle", "FXAnnounce: mute/unmute", &App::toggleMute },
        { "fxannounce/overwater_toggle", "FXAnnounce: toggle overwater flight", &App::toggleOverwater },
        { "fxannounce/rescan", "FXAnnounce: rescan Airline Packs", &App::rescan },
    };
    bool commandsRegistered_ = false;
    static int commandHandler(XPLMCommandRef cmd, XPLMCommandPhase phase, void* refcon);

    // ToLiss purser call buttons, observed with pass-through handlers (VP_SPEC § 4.3a, ARCHITECTURE § 4.4)
    struct WatchedCmd {
        const char* name;
        bool cabinCall;   // false: MECH (logged only)
        XPLMCommandRef ref = nullptr;
    };
    WatchedCmd purserCmds_[6] = {
        { "AirbusFBW/purser/fwd", true }, { "AirbusFBW/purser/aft", true }, { "AirbusFBW/purser/all", true },
        { "AirbusFBW/purser/mid", true }, { "AirbusFBW/purser/exit", true }, { "AirbusFBW/purser/mech", false },
    };
    bool purserWatched_ = false;
    void watchPurserCalls(bool on);
    static int purserCallHandler(XPLMCommandRef cmd, XPLMCommandPhase phase, void* refcon);
    static void menuHandler(void* menuRef, void* itemRef);
};

} // namespace fxa
