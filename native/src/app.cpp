#include "app.h"

#include "core/guard.h"
#include "core/log.h"
#include "core/strings.h"
#include "ui/window.h"

#include <XPLMDataAccess.h>
#include <XPLMPlugin.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fxa {

namespace {

constexpr float kDetectInterval = 0.5f;   // s between ToLiss probes after a plane load
constexpr float kDetectWindow = 12.0f;    // s to wait for the ToLiss plugin to register its datarefs
constexpr float kMainInterval = 0.25f;    // s, purser tick (VP_SPEC § 4)

double simSeconds()
{
    static XPLMDataRef t = XPLMFindDataRef("sim/time/total_running_time_sec");
    return t ? XPLMGetDataf(t) : 0.0;
}

float detectCallback(float, float, int, void*)
{
    return guarded("detect", 0.0f, [] { return App::get().detectTick(); });
}

float mainCallback(float elapsed, float, int, void*)
{
    return guarded("tick", 0.0f, [&] { return App::get().mainTick(elapsed); });
}

std::string withSep(std::string p)
{
    if (!p.empty() && p.back() != '/' && p.back() != '\\') p += XPLMGetDirectorySeparator();
    return p;
}

std::string readFile(const std::string& path)
{
    std::error_code ec;
    const auto p = std::filesystem::u8path(path);
    if (!std::filesystem::is_regular_file(p, ec) || std::filesystem::file_size(p, ec) > (4u << 20)) return {};
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const char* onOff(bool b) { return b ? "on" : "off"; }

std::string fmt(const char* f, double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), f, v);
    return buf;
}

} // namespace

App& App::get()
{
    static App app;
    return app;
}

App::~App()
{
    // Normally XPluginDisable already stopped everything. If not, avoid std::terminate on a joinable thread and
    // never call into X-Plane from static destruction: the player and purser are leaked, the decoder joins.
    if (scanThread_.joinable()) {
        scanCancel_ = true;
        scanThread_.join();
    }
    (void)purser_.release();
    (void)player_.release();
    if (cache_) cache_->shutdown();
}

// ---- lifecycle ------------------------------------------------------------------

void App::start()
{
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
    char buf[2048] = {};
    XPLMGetSystemPath(buf);
    xpRoot_ = withSep(buf);
    const char* sep = XPLMGetDirectorySeparator();
    pluginsDir_ = xpRoot_ + "Resources" + sep + "plugins" + sep;
    outputDir_ = xpRoot_ + "Output" + sep;
    char prefs[2048] = {};
    XPLMGetPrefsPath(prefs);
    std::string pp = prefs;
    const size_t cut = pp.find_last_of("/\\");
    prefsDir_ = cut == std::string::npos ? outputDir_ + "preferences" + sep : pp.substr(0, cut + 1);
    packsDir_ = pluginsDir_ + "AirlinePacks" + sep;
    settingsPath_ = prefsDir_ + "FXAnnounce.ini";

    Log::get().open(outputDir_ + "FXAnnounce.log");
    Log::get().setSummarySink([](const char* line) { XPLMDebugString(line); });
    logSummary("FXAnnounce %s starting (packs: %s)", FXA_VERSION, packsDir_.c_str());
    loadSettingsFile();
    FaultGuard::get().setSafeModeHook([this] { enterSafeMode(); });
}

void App::enable()
{
    enabled_ = true;
    model_.version = FXA_VERSION;
    model_.settings = &settings_;
    ui::setModelProvider([] { return App::get().model(); });

    XPLMCreateFlightLoop_t d{ sizeof(XPLMCreateFlightLoop_t), xplm_FlightLoop_Phase_AfterFlightModel, detectCallback, nullptr };
    detectLoop_ = XPLMCreateFlightLoop(&d);
    XPLMCreateFlightLoop_t m{ sizeof(XPLMCreateFlightLoop_t), xplm_FlightLoop_Phase_AfterFlightModel, mainCallback, nullptr };
    mainLoop_ = XPLMCreateFlightLoop(&m);
    for (Cmd& c : cmds_) c.ref = XPLMCreateCommand(c.name, c.desc);

    cache_ = std::make_unique<DecodeCache>();
    player_ = std::make_unique<Player>(*cache_);
    purser_ = std::make_unique<Purser>(*player_);
    purser_->setEventSink([this](const TimelineRow& r) {
        recorder_.event(r.t, std::string(annTypeName(r.type)) + " " + outcomeName(r.outcome) + (r.detail.empty() ? "" : ": " + r.detail));
    });
    purser_->setNoteSink([this](double t, const std::string& text) { recorder_.event(t, "purser " + text); });
    scheduleDetection(kDetectWindow);
}

void App::disable()
{
    deactivate("plugin disabled");
    ui::shutdownUi();
    stopScan();
    purser_.reset();
    player_.reset();
    if (cache_) cache_->shutdown();
    cache_.reset();
    if (detectLoop_) XPLMDestroyFlightLoop(detectLoop_);
    if (mainLoop_) XPLMDestroyFlightLoop(mainLoop_);
    detectLoop_ = mainLoop_ = nullptr;
    saveSettingsFile();
    ui::setModelProvider(nullptr);
    library_.reset();
    enabled_ = false;
}

void App::stop()
{
    releaseParkedAudio();
    logSummary("stopped");
    Log::get().setSummarySink(nullptr);
    Log::get().close();
}

void App::onMessage(int msg, void* param)
{
    switch (msg) {
    case XPLM_MSG_PLANE_LOADED:
    case XPLM_MSG_LIVERY_LOADED:
        // param is the plane index; 0 is the user's aircraft.
        if (reinterpret_cast<intptr_t>(param) == 0) {
            if (msg == XPLM_MSG_PLANE_LOADED) deactivate("aircraft reloaded");
            scheduleDetection(kDetectWindow);
        }
        break;
    case XPLM_MSG_PLANE_UNLOADED:
        if (reinterpret_cast<intptr_t>(param) == 0) deactivate("aircraft unloaded");
        break;
    case XPLM_MSG_WILL_WRITE_PREFS:
        saveSettingsFile();
        break;
    case XPLM_MSG_AIRPORT_LOADED:
        if (active_) resetFlight("new airport or situation loaded");
        break;
    case XPLM_MSG_FMOD_BANK_UNLOADING:
        if (player_) player_->stopAll();   // X-Plane is rebuilding its sound banks; our channels go away
        break;
    default:
        break;
    }
}

// ---- detection and activation ------------------------------------------------------

void App::scheduleDetection(float seconds)
{
    if (!detectLoop_ || FaultGuard::get().safeMode()) return;
    detectUntil_ = simSeconds() + seconds;
    XPLMScheduleFlightLoop(detectLoop_, kDetectInterval, 1);
}

float App::detectTick()
{
    const AircraftInfo info = detectAircraft();
    if (info.toliss && !info.supported) {
        // A ToLiss we don't run on yet (A330-900, A340-600): dormant, said once per aircraft (VP_SPEC scope).
        if (active_) deactivate("not a supported ToLiss model");
        if (unsupportedLogged_ != info.acfPath) {
            unsupportedLogged_ = info.acfPath;
            logSummary("dormant: ToLiss %s (%s) is not supported yet; this version runs on the A319, A320 and A321",
                       info.acfFile.c_str(), familyName(info.family));
        }
        return 0.0f;
    }
    if (info.toliss) {
        if (!active_ || info.acfPath != aircraft_.acfPath || info.liveryPath != aircraft_.liveryPath) activate(info);
        return 0.0f;   // done until the next plane/livery load
    }
    if (simSeconds() < detectUntil_) return kDetectInterval;
    if (active_) deactivate("not a ToLiss aircraft");
    logInfo("dormant: %s (%s) is not a supported ToLiss aircraft", info.acfFile.c_str(), info.acfIcao.c_str());
    return 0.0f;
}

void App::activate(const AircraftInfo& info)
{
    const bool wasActive = active_;
    const bool liveryChanged = info.liveryPath != aircraft_.liveryPath;
    aircraft_ = info;
    active_ = true;
    logSummary("active on %s (%s, %s), livery '%s'", info.acfFile.c_str(), info.acfIcao.c_str(), familyName(info.family),
               info.liveryName.empty() ? "default" : info.liveryName.c_str());
    adapter_.resolve();
    loadLiveryConfig();
    overwater_ = liveryCfg_.overwater.value_or(settings_.overwaterDefault);
    if (liveryChanged && wasActive) resetFlight("livery changed");
    updateAircraftType();
    if (!wasActive) {
        sbh_.reset();
        ofp_.reset();
        ofpOk_ = false;
        ofpCheck_.clear();
        originSeqno_ = jitterSeqno_ = ofpLoggedSeqno_ = -1;
        nextOfpCheck_ = -1;
        plan_ = BoardingPlan{};
        planLogged_.clear();
        buildMenu();
        registerCommands(true);
        watchPurserCalls(true);
        if (!library_ && !scanning_) startScan();
        if (!FaultGuard::get().safeMode()) XPLMScheduleFlightLoop(mainLoop_, kMainInterval, 1);
        if (settings_.windowVisible) ui::showWindow(true);
    }
    resolvePackNow(wasActive ? "livery changed" : "activated");
    refreshModel();
}

void App::deactivate(const char* why)
{
    if (!active_) return;
    logInfo("dormant: %s", why);
    active_ = false;
    if (mainLoop_) XPLMScheduleFlightLoop(mainLoop_, 0, 0);
    registerCommands(false);
    watchPurserCalls(false);
    const bool visible = ui::windowVisible();
    ui::destroyWindow();
    settings_.windowVisible = visible;   // reopen on the next ToLiss
    destroyMenu();
    recorder_.close();
    if (player_) player_->stopAll();
    if (purser_) purser_->reset();
    aircraft_ = AircraftInfo{};
    resolution_ = Resolution{};
    activePackName_.clear();
    havePos_ = false;
    probe_.reset();
    census_ = ProbeCensus{};
    censusChanged_.clear();
    refreshModel();
}

void App::enterSafeMode()
{
    // Called from FaultGuard: stop everything that runs on its own. The menu and window stay for Re-enable.
    if (mainLoop_) XPLMScheduleFlightLoop(mainLoop_, 0, 0);
    if (detectLoop_) XPLMScheduleFlightLoop(detectLoop_, 0, 0);
    recorder_.close();
    if (player_) player_->stopAll();
    updateMenuChecks();
}

void App::reenable()
{
    if (!FaultGuard::get().safeMode()) return;
    FaultGuard::get().reset();
    updateMenuChecks();
    scheduleDetection(kDetectWindow);
    if (active_) XPLMScheduleFlightLoop(mainLoop_, kMainInterval, 1);
}

// ---- packs ---------------------------------------------------------------------

void App::startScan()
{
    stopScan();
    std::vector<std::string> roots = { packsDir_ };
    for (const std::string& extra : splitList(settings_.packPaths, ";")) roots.push_back(extra);
    scanCancel_ = false;
    scanDone_ = false;
    scanning_ = true;
    logInfo("scanning Airline Packs in %s", join(roots, "; ").c_str());
    scanThread_ = std::thread([this, roots] {
        // Worker: filesystem only, never XPLM (ARCHITECTURE § 3).
        std::shared_ptr<PackLibrary> lib;
        try {
            lib = std::make_shared<PackLibrary>(scanPacks(roots, &scanCancel_));
        } catch (const std::exception& e) {
            lib = std::make_shared<PackLibrary>();
            lib->warnings.push_back(std::string("scan failed: ") + e.what());
        } catch (...) {
            lib = std::make_shared<PackLibrary>();
            lib->warnings.push_back("scan failed");
        }
        std::lock_guard<std::mutex> lock(scanMu_);
        scanResult_ = std::move(lib);
        scanDone_ = true;
    });
}

void App::stopScan()
{
    if (scanThread_.joinable()) {
        scanCancel_ = true;
        scanThread_.join();
    }
    scanning_ = false;
}

void App::pollScan()
{
    if (!scanning_ || !scanDone_) return;
    if (scanThread_.joinable()) scanThread_.join();
    scanning_ = false;
    std::shared_ptr<PackLibrary> lib;
    {
        std::lock_guard<std::mutex> lock(scanMu_);
        lib = std::move(scanResult_);
    }
    if (!lib) return;
    // Airline table: built-in rows, with the user's AirlinePacks/airlines.tsv in front.
    airlines_ = AirlineTable::builtin();
    const std::string userTable = readFile(packsDir_ + "airlines.tsv");
    if (!userTable.empty()) airlines_.prepend(AirlineTable::parse(userTable));
    for (const std::string& w : lib->warnings) logWarn("%s", w.c_str());
    for (const Pack& p : lib->packs) {
        logInfo("pack %s: %d sounds%s%s", p.name.c_str(), static_cast<int>(p.files.size()),
                p.wrapper.empty() ? "" : (" (in " + p.wrapper + ")").c_str(), p.mappingOnly ? " (mapping only)" : "");
        for (const std::string& w : p.warnings) logDebug("pack %s: %s", p.name.c_str(), w.c_str());
    }
    logSummary("%d Airline Packs, %d sounds", static_cast<int>(lib->packs.size()), static_cast<int>(lib->soundCount()));
    // Pack pointers point into the library: re-point a locked pack to its copy in the new scan before the old
    // library goes away (or drop the lock if the pack disappeared).
    const std::string lockedName = resolution_.pack ? resolution_.pack->name : std::string();
    if (player_) player_->stopAll();
    resolution_.pack = nullptr;
    library_ = std::move(lib);
    if (purser_ && purser_->packLocked() && !lockedName.empty()) {
        if (const Pack* same = library_->find(lockedName)) {
            resolution_.pack = same;
            return;
        }
    }
    resolvePackNow("packs scanned");
}

void App::loadLiveryConfig()
{
    const Ini sidecar = aircraft_.liveryPath.empty() ? Ini{} : Ini::parse(readFile(aircraft_.liveryPath + "airlinepack.cfg"));
    const Ini central = Ini::parse(readFile(packsDir_ + "liveries.ini"));
    liveryCfg_ = mergeLiveryConfig(liveryConfigFromSidecar(sidecar), liveryConfigFromCentral(central, aircraft_.liveryName));
    tlscfg_ = aircraft_.liveryPath.empty() ? Ini{} : Ini::parse(readFile(aircraft_.liveryPath + "livery.tlscfg"));
    if (!liveryCfg_.source.empty()) logInfo("livery config from %s", liveryCfg_.source.c_str());
}

void App::updateAircraftType()
{
    // NEO: the live acf_ICAO first (ToLiss may switch it with the engine option), then livery.tlscfg, then the
    // livery-string code ("[LEA] ..."). docs/TOLISS_DATAREFS.md.
    const std::string icao = toUpper(sim_.acfIcao.empty() ? aircraft_.acfIcao : sim_.acfIcao);
    int neo = -1;
    if (icao == "A20N" || icao == "A21N" || icao == "A19N") neo = 1;
    else if (icao == "A320" || icao == "A321" || icao == "A319") neo = 0;
    if (neo < 0) {
        if (const std::string* e = tlscfg_.get("", "eng_type")) neo = engineIsNeo(*e);
    }
    if (neo < 0) neo = engineIsNeo(liveryStringCode(aircraft_.liveryName));
    const Family fam = familyFrom(icao, aircraft_.acfFile);
    const AircraftType t = makeAircraftType(fam == Family::Unknown ? aircraft_.family : fam, neo == 1);
    if (t.tags != type_.tags) logInfo("aircraft type tags: %s", join(t.tags, ", ").c_str());
    type_ = t;
}

std::vector<std::string> App::customTags() const
{
    std::vector<std::string> tags = liveryCfg_.customTags;
    if (sim_.satcom)
        for (const std::string& t : splitList(settings_.satcomTags))
            if (std::none_of(tags.begin(), tags.end(), [&](const std::string& x) { return iequals(x, t); })) tags.push_back(t);
    return tags;
}

SelectContext App::selectContext() const
{
    SelectContext c;
    c.aircraft = type_;
    c.refueling = refuel_.refueling(sim_.t);
    c.time = timeOfDayAt(sim_.localTimeSec);
    c.phase = purser_ ? purser_->phase() : Phase::PreFlight;
    c.overwater = overwater_;
    c.customTags = customTags();
    c.variant = variant_;
    return c;
}

void App::resetFlight(const char* why)
{
    logInfo("flight reset: %s", why);
    if (player_) player_->stopAll();
    if (purser_) purser_->reset();
    havePos_ = false;
    refuel_.reset();
    nextOfpCheck_ = -1;   // the aircraft may be at another airport now
    resolvePackNow(why);
}

void App::trackRefuel()
{
    // VP_SPEC § 4.2: on the ground, fuel rising between refuel_rate_kgs and refuel_max_kgs (held for 60 s).
    const double rate = refuel_.update(sim_, settings_);
    if (rate > 0) logInfo("refuelling detected (%.1f kg/s)", rate);
}

// ---- SimBrief ------------------------------------------------------------------

void App::readOfp()
{
    std::string why;
    ofp_ = parseOfp(sbh_.text(), &why);
    ofpWhy_ = ofp_ ? std::string() : why;
    nextOfpCheck_ = -1;   // check it here on this tick
    if (!ofp_) {
        logInfo("SimBrief: %s", why.c_str());
        return;
    }
    logInfo("SimBrief OFP #%d: %s %s-%s, ETD %s%s", ofp_->seqno, ofp_->callsign.empty() ? "(no callsign)" : ofp_->callsign.c_str(),
            ofp_->origin.c_str(), ofp_->destination.c_str(), zuluLabel(static_cast<double>(ofp_->estOut)).c_str(),
            ofp_->stale ? " (simbrief_hub: last download failed)" : "");
    const std::string icao = toUpper(sim_.acfIcao.empty() ? aircraft_.acfIcao : sim_.acfIcao);
    if (!ofp_->aircraftIcao.empty() && !icao.empty() && familyFrom(ofp_->aircraftIcao, "") != familyFrom(icao, ""))
        logInfo("SimBrief OFP is planned for a %s; flying a %s", ofp_->aircraftIcao.c_str(), icao.c_str());
}

void App::checkOfpHere(bool force)
{
    if (!ofp_) {
        ofpOk_ = false;
        ofpCheck_.clear();
        return;
    }
    if (!force && nextOfpCheck_ >= 0 && sim_.t < nextOfpCheck_) return;
    nextOfpCheck_ = sim_.t + 10;
    if (sim_.lat == 0 && sim_.lon == 0) return;   // no position yet
    if (originSeqno_ != ofp_->seqno) {   // one nav-database search per OFP
        originFound_ = SimbriefSource::findAirport(ofp_->origin, sim_.lat, sim_.lon, originLat_, originLon_);
        originSeqno_ = ofp_->seqno;
    }
    const std::string why = checkOfp(*ofp_, originFound_, originLat_, originLon_, sim_.lat, sim_.lon, settings_);
    const bool ok = why.empty();
    if (ok != ofpOk_ || ofpLoggedSeqno_ != ofp_->seqno) {
        ofpLoggedSeqno_ = ofp_->seqno;
        if (ok) logInfo("SimBrief OFP accepted: the aircraft is at %s", ofp_->origin.c_str());
        else logInfo("SimBrief OFP ignored: %s", why.c_str());
        recorder_.event(sim_.t, std::string("simbrief ") + (ok ? "accepted" : "ignored: " + why));
    }
    ofpOk_ = ok;
    ofpCheck_ = why;
}

void App::updateBoardingPlan()
{
    BoardingPlan p;
    if (ofp_ && ofpOk_) {
        if (jitterSeqno_ != ofp_->seqno) {
            // Rolled once per OFP (one OFP per flight), VP_SPEC § 4.1a.
            std::uniform_real_distribution<double> d(-settings_.leadJitterS, settings_.leadJitterS);
            leadJitter_ = settings_.leadJitterS > 0 ? d(rng_) : 0.0;
            jitterSeqno_ = ofp_->seqno;
        }
        p = makeBoardingPlan(*ofp_, type_.family, sim_.pax, settings_, leadJitter_);
    } else if (!sbh_.installed()) {
        p.summary = "simbrief_hub not installed";
    } else if (!ofp_) {
        p.summary = ofpWhy_;
    } else {
        p.summary = "OFP ignored: " + ofpCheck_;
    }
    plan_ = p;
    if (plan_.summary != planLogged_) {
        planLogged_ = plan_.summary;
        if (plan_.valid) logInfo("boarding plan: %s", plan_.summary.c_str());
        recorder_.event(sim_.t, "boarding plan: " + plan_.summary);
    }
}

std::string App::simbriefStatus(int* tone) const
{
    const char* mode = settings_.boarding() == BoardingStart::Doors ? " (not used for boarding: Boarding starts = Doors open)"
                     : settings_.boarding() == BoardingStart::Call ? " (not used for boarding: Boarding starts = Cabin call only)"
                                                                   : "";
    if (!sbh_.installed()) {
        *tone = 2;
        return "simbrief_hub not installed: boarding starts when a door has been open at the gate";
    }
    if (!ofp_) {
        *tone = 2;
        return ofpWhy_.empty() ? std::string("no OFP") : ofpWhy_;
    }
    if (!ofpOk_) {
        *tone = 1;
        return "OFP ignored: " + (ofpCheck_.empty() ? std::string("not checked yet") : ofpCheck_) + "; doors rule";
    }
    *tone = 0;
    std::string line = plan_.summary;
    if (sim_.zuluSec >= 0) {
        const double until = secondsUntil(plan_.boardingAtSec, sim_.zuluSec);
        if (until > 0) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), " (boarding in %d:%02d)", static_cast<int>(until) / 60, static_cast<int>(until) % 60);
            line += buf;
        }
    }
    return line + mode;
}

// ---- probe ---------------------------------------------------------------------

void App::probeStart(const std::string& id)
{
    ProbeStep step = ProbeStep::None;
    if (id == "acp_pa") step = ProbeStep::AcpPa;
    else if (id == "acp_cab") step = ProbeStep::AcpCab;
    else if (id == "landing") step = ProbeStep::LandingLights;
    else if (id == "volume") step = ProbeStep::VolumeOverride;
    if (step == ProbeStep::None || !active_) return;
    if (step == ProbeStep::VolumeOverride) {
        const size_t n = census_.snapshot();
        logInfo("probe: census snapshot of %d ToLiss datarefs; toggle the ISCS switch now", static_cast<int>(n));
    }
    probe_.startStep(step, sim_, adapter_.probeRaw());
    logInfo("probe: step %s started", id.c_str());
}

void App::probeApply(const std::string& key, const std::string& value)
{
    for (const SettingDef& d : settingDefs()) {
        if (key != d.key) continue;
        Ini one;
        one.set(d.section, d.key, value);
        loadSettings(one, settings_);   // same parsing and clamping as the ini
        logInfo("probe: applied [%s] %s = %s", d.section, d.key, value.c_str());
        applySettings();
        saveSettingsFile();
        return;
    }
    logWarn("probe: unknown setting %s", key.c_str());
}

void App::probeSummary()
{
    const std::string header = "### Probe summary: FXAnnounce " + std::string(FXA_VERSION) + ", ToLiss " + type_.label + " (" +
                               aircraft_.acfFile + ", " + sim_.acfIcao + ")";
    const std::string text = probe_.summary(header);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        logInfo("probe | %s", text.substr(pos, end - pos).c_str());
        pos = end + 1;
    }
    logSummary("probe summary written to FXAnnounce.log (%d of %d items seen)", probe_.doneCount(),
               static_cast<int>(probe_.items().size()));
}

void App::trackPosition()
{
    // VP_SPEC § 4.4: a jump of more than 1 NM in one tick is a teleport or a situation load.
    if (sim_.lat == 0 && sim_.lon == 0) return;
    if (havePos_) {
        const double dLat = (sim_.lat - lastLat_) * 60.0;
        const double dLon = (sim_.lon - lastLon_) * 60.0 * std::cos(sim_.lat * 3.14159265358979 / 180.0);
        if (std::sqrt(dLat * dLat + dLon * dLon) > 1.0) {
            lastLat_ = sim_.lat;
            lastLon_ = sim_.lon;
            resetFlight("aircraft moved more than 1 NM at once");
            havePos_ = true;
            return;
        }
    }
    havePos_ = true;
    lastLat_ = sim_.lat;
    lastLon_ = sim_.lon;
}

void App::resolvePackNow(const char* why)
{
    resolvedFlightNumber_ = sim_.flightNumber;
    if (!library_) return;
    if (purser_ && purser_->packLocked() && resolution_.pack) {
        logDebug("pack locked until the turnaround; not re-resolving (%s)", why);
        return;
    }
    resolvedCallsign_ = ofpCallsign();
    const Resolution r = resolvePack(*library_, airlines_,
                                     ResolveInput{ aircraft_.liveryName, sim_.flightNumber, liveryCfg_, resolvedCallsign_ });
    const bool changed = r.pack != resolution_.pack;
    resolution_ = r;
    if (changed || std::string(why) == "activated")
        logSummary("airline pack: %s (%s)", r.pack ? r.pack->name.c_str() : "none", r.reason.c_str());
    const std::string name = r.pack ? r.pack->name : std::string();
    if (name != activePackName_) {
        // VP_SPEC § 3.4: one random variant per session, re-rolled when another pack becomes active.
        activePackName_ = name;
        variant_ = r.pack ? pickVariant(*r.pack, rng_) : 0;
        if (r.pack && r.pack->maxVariant > 0) logInfo("pack %s: variant %d of 0-%d", name.c_str(), variant_, r.pack->maxVariant);
    }
}

// ---- main tick ---------------------------------------------------------------

float App::mainTick(float elapsed)
{
    if (!active_ || FaultGuard::get().safeMode()) return 0.0f;
    pollScan();
    const std::string oldIcao = sim_.acfIcao;
    sim_ = adapter_.read(settings_, elapsed);
    if (sim_.acfIcao != oldIcao) updateAircraftType();
    if (sim_.flightNumber != resolvedFlightNumber_) resolvePackNow("flight number changed");
    trackRefuel();
    trackPosition();

    // SimBrief through simbrief_hub: the OFP, its check here, the boarding plan and the pack (VP_SPEC § 4.1a, § 1.4).
    if (sbh_.poll(sim_.t)) readOfp();
    checkOfpHere(false);
    updateBoardingPlan();
    if (ofpCallsign() != resolvedCallsign_) resolvePackNow("SimBrief OFP changed");

    // Purser and audio. The player selects from the active pack with the current tags.
    player_->setSource(resolution_.pack, selectContext(), &settings_, &sim_, muted_ || !settings_.enabled);
    purser_->setAircraft(type_);
    purser_->setBoardingPlan(plan_);
    purser_->tick(sim_, settings_);

    // Probe checklist (Inspector tab)
    probe_.update(sim_, adapter_.probeRaw());
    {
        int tone = 2;
        probe_.setSimbrief(ofp_.has_value(), simbriefStatus(&tone));
    }
    if (probe_.wantsCensusDiff(sim_.t)) probe_.censusResult(census_.changed());
    if (auto next = purser_->nextQueued()) player_->prefetch(next->first, next->second);
    player_->update(elapsed);

    if (settings_.recorder && !recorder_.isOpen()) {
        char name[64];
        const std::time_t now = std::time(nullptr);
        std::strftime(name, sizeof(name), "%Y%m%d-%H%M%S.fxtrace.csv", std::localtime(&now));
        const std::string path = outputDir_ + "FXAnnounce" + XPLMGetDirectorySeparator() + name;
        if (recorder_.open(path)) {
            logInfo("recording trace to %s", path.c_str());
            planLogged_.clear();      // put the current boarding plan and OFP check into the new trace
            ofpLoggedSeqno_ = -1;
            nextOfpCheck_ = -1;
        } else {
            logWarn("could not open %s; recorder off", path.c_str());
            settings_.recorder = false;
        }
    } else if (!settings_.recorder && recorder_.isOpen()) {
        recorder_.close();
    }
    recorder_.state(sim_);
    if (recorder_.isOpen() && sim_.t - rawAt_ >= 1.0) {
        // Raw ToLiss values, only when they change: the probe trace then shows which index moved (ACP knobs, doors).
        rawAt_ = sim_.t;
        for (const auto& [name, value] : adapter_.rawSnapshot()) {
            auto it = rawLast_.find(name);
            if (it != rawLast_.end() && it->second == value) continue;
            recorder_.event(sim_.t, "raw " + name + " = " + value);
            rawLast_[name] = value;
        }
    } else if (!recorder_.isOpen()) {
        rawLast_.clear();
    }

    refreshModel();
    return kMainInterval;
}

void App::refreshModel()
{
    model_.active = active_;
    model_.safeMode = FaultGuard::get().safeMode();
    model_.lastFault = FaultGuard::get().lastFault();
    model_.muted = muted_;
    model_.overwater = overwater_;
    model_.library = library_.get();
    model_.scanning = scanning_;
    model_.packsDir = packsDir_;
    if (active_) {
        model_.aircraft = "ToLiss " + type_.label + " (" + aircraft_.acfFile + ")";
        model_.typeTags = join(type_.tags, ", ");
        model_.livery = aircraft_.liveryName.empty() ? "(default)" : aircraft_.liveryName;
        model_.pack = resolution_.pack ? resolution_.pack->name : (library_ ? "none" : "scanning...");
        model_.packReason = resolution_.reason;
        model_.simbrief = simbriefStatus(&model_.simbriefTone);
        model_.customTags = join(customTags(), ", ");
        const SimState& s = sim_;
        const bool estimated = purser_ && purser_->phaseEstimated();
        model_.status = {
            { "FMGS phase", s.fmgsPhase ? std::string(phaseName(*s.fmgsPhase)) + " (" + std::to_string(s.fmgsPhaseRaw) + ")"
                                        : "unknown (" + std::to_string(s.fmgsPhaseRaw) + ")" },
            { "Purser phase", purser_ ? std::string(phaseName(purser_->phase())) + (estimated ? " (estimated: FMGS not initialized)" : "") : "-" },
            { "ECAM / QPAC phase", (s.ecamPhase >= 0 ? std::to_string(s.ecamPhase) : std::string("-")) + " / " +
                                       (s.qpacPhase >= 0 ? std::to_string(s.qpacPhase) : std::string("-")) },
            { "Thrust levers", s.throttleKnown() ? fmt("%.2f", s.throttleMax) : "-" },
            { "Flight number", s.flightNumber.empty() ? "-" : s.flightNumber },
            { "On ground", s.onGround ? "yes" : "no" },
            { "Speed / height", fmt("%.0f kt GS", s.gsKt) + ", " + fmt("%.0f ft AGL", s.aglFt) },
            { "Beacon / seatbelts", std::string(onOff(s.beacon)) + " / " + onOff(s.seatbelt) },
            { "Engines running", std::to_string(s.enginesRunning) + " of " + std::to_string(s.engineCount) },
            { "Passenger doors", fmt("most open %.0f%%", s.doorMax * 100) + fmt(", L1 %.0f%%", s.doorFwd * 100) },
            { "Slides", s.slidesKnown ? (s.slidesArmed ? "armed" : "disarmed") : "unknown" },
            { "Passengers", std::to_string(s.pax) },
            { "AC power", s.acPower ? "yes" : "no" },
            { "ACP PA / CAB", std::string(s.paOn ? fmt("PA %.0f%%", s.paVol * 100) : "PA in") + " / " +
                                  (s.cabOn ? fmt("CAB %.0f%%", s.cabVol * 100) : "CAB in") },
            { "Cockpit door", fmt("%.0f%% open", s.cockpitDoor * 100) +
                                  (s.doorBlend >= 0 ? fmt(" (ToLiss sound blend %.0f%%)", s.doorBlend * 100) : std::string()) },
            { "ToLiss volume", s.tolissVolInternal < 0 ? std::string("-")
                                   : fmt("internal %.0f%%", s.tolissVolInternal * 100) +
                                         (s.tolissVolOverride == 1 ? ", ISCS override on"
                                          : s.tolissVolOverride == 0 ? ", ISCS override off" : ", override switch unknown (probe)") },
            { "Zulu time", s.zuluSec >= 0 ? zuluLabel(s.zuluSec) : std::string("-") },
            { "Camera", s.viewExternal ? "external" : (s.cameraInCabin ? "in the cabin" : "cockpit") },
            { "Local time", fmt("%02.0f:", std::floor(s.localTimeSec / 3600)) + fmt("%02.0f", std::fmod(std::floor(s.localTimeSec / 60), 60)) +
                                std::string(" (") + timeOfDayName(timeOfDayAt(s.localTimeSec)) + ")" },
            { "Sun elevation", fmt("%.1f deg", s.sunPitchDeg) },
            { "Recorder", recorder_.isOpen() ? recorder_.path() : "off" },
        };
        model_.inspector = adapter_.inspect();
        model_.probe.clear();
        for (const ProbeItem& it : probe_.items())
            model_.probe.push_back({ it.id, it.title, it.instruction, it.finding, it.applyKey, it.applyValue, it.step, it.done });
        switch (probe_.activeStep()) {
        case ProbeStep::AcpPa: model_.probeStep = "acp_pa"; break;
        case ProbeStep::AcpCab: model_.probeStep = "acp_cab"; break;
        case ProbeStep::LandingLights: model_.probeStep = "landing"; break;
        case ProbeStep::VolumeOverride: model_.probeStep = "volume"; break;
        case ProbeStep::None: model_.probeStep.clear(); break;
        }
        model_.probeStepLeft = static_cast<float>(probe_.stepRemaining(sim_.t));
        model_.censusSize = static_cast<int>(census_.size());
        model_.censusChanged = censusChanged_;
        if (purser_ && player_) {
            model_.stage = purser_->stageLabel();
            model_.next = purser_->nextLabel();
            model_.pending = purser_->pendingLabels();
            model_.calling = purser_->calling();
            model_.nowPlaying = player_->nowPlaying();
            if (model_.nowPlaying.empty() && purser_->calling()) model_.nowPlaying = purser_->nowLabel();
            model_.progress = player_->progress();
            model_.music = player_->musicLabel();
            model_.previewing = player_->previewLabel();
            model_.events.clear();
            const auto& tl = purser_->timeline();
            for (auto r = tl.rbegin(); r != tl.rend() && model_.events.size() < 200; ++r) {
                ui::UiModel::EventRow e;
                e.t = r->t;
                e.what = std::string(annTypeName(r->type)) + (r->context != CallContext::Any ? std::string(" (") + contextName(r->context) + ")" : "");
                e.outcome = outcomeName(r->outcome);
                e.detail = r->detail;
                e.tone = (r->outcome == Outcome::Played || r->outcome == Outcome::Playing) ? 0
                       : (r->outcome == Outcome::NoSound || r->outcome == Outcome::Unanswered || r->outcome == Outcome::Expired) ? 1 : 2;
                model_.events.push_back(std::move(e));
            }
            model_.status.insert(model_.status.begin(), { "Variant", std::to_string(variant_) });
            model_.status.insert(model_.status.begin(), { "Time-of-day tag", timeOfDayName(timeOfDayAt(sim_.localTimeSec)) });
            model_.status.insert(model_.status.begin(), { "Refuelling", refuel_.refueling(sim_.t) ? "yes" : "no" });
        }
    } else {
        model_.aircraft.clear();
        model_.typeTags.clear();
        model_.livery.clear();
        model_.pack.clear();
        model_.packReason.clear();
        model_.customTags.clear();
        model_.status.clear();
        model_.inspector.clear();
        model_.simbrief.clear();
        model_.probe.clear();
        model_.probeStep.clear();
        model_.censusChanged.clear();
        model_.stage.clear();
        model_.next.clear();
        model_.pending.clear();
        model_.nowPlaying.clear();
        model_.music.clear();
        model_.events.clear();
        model_.calling = false;
    }
    model_.log = Log::get().recent();
    model_.onSkip = [] { App::get().skip(); };
    model_.onAdvance = [] { App::get().advance(); };
    model_.onToggleMute = [] { App::get().toggleMute(); };
    model_.onToggleOverwater = [] { App::get().toggleOverwater(); };
    model_.onRescan = [] { App::get().rescan(); };
    model_.onReenable = [] { App::get().reenable(); };
    model_.onPreview = [](const std::string& path, const std::string& label) {
        if (App::get().player_) App::get().player_->preview(path, label);
    };
    model_.onStopPreview = [] {
        if (App::get().player_) App::get().player_->stopPreview();
    };
    model_.explain = [](int type, int ctx) {
        App& a = App::get();
        std::vector<std::string> lines;
        if (!a.resolution_.pack) return std::vector<std::string>{ "No active pack." };
        if (type < 0 || type >= kAnnTypeCount) return lines;
        SelectContext c = a.selectContext();
        c.context = static_cast<CallContext>(std::clamp(ctx, 0, 2));
        Rng rng(1);
        const Selection sel = selectSound(*a.resolution_.pack, static_cast<AnnType>(type), c, rng);
        lines.push_back("Pack " + a.resolution_.pack->name + ", " + c.aircraft.label + " [" + join(c.aircraft.tags, ",") + "], " +
                        timeOfDayName(c.time) + ", phase " + phaseName(c.phase) + ", variant " + std::to_string(c.variant) +
                        (c.refueling ? ", refuelling" : "") + (c.overwater ? ", overwater" : "") +
                        (c.customTags.empty() ? "" : ", tags " + join(c.customTags, ",")));
        if (sel.file)
            lines.push_back("-> plays " + sel.file->relPath + fmt(" (score %.1f)", sel.score) +
                            (sel.fellBackToVariant0 ? ", variant 0 fallback" : ""));
        else
            lines.push_back("-> nothing eligible: the announcement is skipped");
        for (const Candidate& cd : sel.candidates)
            lines.push_back(std::string(cd.eligible ? "  ok   " : "  no   ") + cd.file->relPath + ": " + cd.why);
        if (sel.candidates.empty()) lines.push_back("  (the pack has no file of this type)");
        return lines;
    };
    model_.onProbeStart = [](const std::string& id) { App::get().probeStart(id); };
    model_.onProbeApply = [](const std::string& key, const std::string& value) { App::get().probeApply(key, value); };
    model_.onProbeSummary = [] { App::get().probeSummary(); };
    model_.onProbeReset = [] { App::get().probe_.reset(); };
    model_.onCensusSnapshot = [] {
        App& a = App::get();
        if (!a.active_) return;
        a.censusChanged_.clear();
        logInfo("probe: census snapshot of %d ToLiss datarefs", static_cast<int>(a.census_.snapshot()));
    };
    model_.onCensusDiff = [] {
        App& a = App::get();
        if (!a.active_) return;
        a.censusChanged_ = a.census_.changed();
        logInfo("probe: %d datarefs changed since the snapshot", static_cast<int>(a.censusChanged_.size()));
        for (const std::string& n : a.censusChanged_) logInfo("probe: changed %s", n.c_str());
    };
    model_.onSettingsChanged = [] {
        App::get().applySettings();
        App::get().saveSettingsFile();
    };
}

// ---- actions -------------------------------------------------------------------

void App::toggleWindow() { ui::toggleWindow(); }
void App::skip()
{
    if (purser_) purser_->skip();
}

void App::advance()
{
    if (purser_) purser_->advance();
}

void App::toggleMute()
{
    muted_ = !muted_;
    logInfo("%s", muted_ ? "muted" : "unmuted");
    updateMenuChecks();
}

void App::toggleOverwater()
{
    overwater_ = !overwater_;
    logInfo("overwater flight: %s", overwater_ ? "yes" : "no");
    updateMenuChecks();
}

void App::rescan()
{
    if (!active_) return;
    loadLiveryConfig();
    startScan();
}

// ---- settings --------------------------------------------------------------------

void App::loadSettingsFile()
{
    settingsIni_ = Ini::parse(readFile(settingsPath_));
    loadSettings(settingsIni_, settings_);
    applySettings();
    logInfo("settings: %s", settingsPath_.c_str());
}

void App::saveSettingsFile()
{
    storeSettings(settings_, settingsIni_);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(prefsDir_), ec);
    std::ofstream out(std::filesystem::u8path(settingsPath_), std::ios::binary | std::ios::trunc);
    if (!out) {
        logWarn("could not write %s", settingsPath_.c_str());
        return;
    }
    out << "# FXAnnounce settings (edited from the FXAnnounce window; see docs/VP_SPEC.md)\n" << settingsIni_.serialize();
}

void App::applySettings()
{
    Log::get().setLevel(static_cast<LogLevel>(std::clamp(settings_.logLevel, 0, 3)));
}

// ---- menu and commands -----------------------------------------------------------

void App::menuHandler(void*, void* itemRef)
{
    guarded("menu", [&] {
        App& a = App::get();
        const intptr_t item = reinterpret_cast<intptr_t>(itemRef);
        switch (item) {
        case 1: a.toggleMute(); break;
        case 2: a.toggleOverwater(); break;
        case 3: a.rescan(); break;
        case 4: a.reenable(); break;
        default: break;
        }
    });
}

void App::buildMenu()
{
    if (menu_) return;
    menuItem_ = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "FXAnnounce", nullptr, 0);
    menu_ = XPLMCreateMenu("FXAnnounce", XPLMFindPluginsMenu(), menuItem_, menuHandler, nullptr);
    miWindow_ = XPLMAppendMenuItemWithCommand(menu_, "Show/hide window", cmds_[0].ref);
    miMute_ = XPLMAppendMenuItem(menu_, "Mute", reinterpret_cast<void*>(intptr_t{ 1 }), 0);
    miOverwater_ = XPLMAppendMenuItem(menu_, "Overwater flight", reinterpret_cast<void*>(intptr_t{ 2 }), 0);
    miRescan_ = XPLMAppendMenuItem(menu_, "Rescan Airline Packs", reinterpret_cast<void*>(intptr_t{ 3 }), 0);
    XPLMAppendMenuSeparator(menu_);
    miReenable_ = XPLMAppendMenuItem(menu_, "Re-enable after fault", reinterpret_cast<void*>(intptr_t{ 4 }), 0);
    updateMenuChecks();
}

void App::destroyMenu()
{
    if (!menu_) return;
    XPLMDestroyMenu(menu_);
    XPLMRemoveMenuItem(XPLMFindPluginsMenu(), menuItem_);
    menu_ = nullptr;
    menuItem_ = -1;
}

void App::updateMenuChecks()
{
    if (!menu_) return;
    XPLMCheckMenuItem(menu_, miMute_, muted_ ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    XPLMCheckMenuItem(menu_, miOverwater_, overwater_ ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    XPLMEnableMenuItem(menu_, miReenable_, FaultGuard::get().safeMode() ? 1 : 0);
}

int App::commandHandler(XPLMCommandRef, XPLMCommandPhase phase, void* refcon)
{
    if (phase != xplm_CommandBegin) return 1;
    guarded("command", [&] {
        App& a = App::get();
        auto fn = a.cmds_[reinterpret_cast<intptr_t>(refcon)].fn;
        (a.*fn)();
    });
    return 1;
}

void App::registerCommands(bool on)
{
    if (on == commandsRegistered_) return;
    for (intptr_t k = 0; k < static_cast<intptr_t>(std::size(cmds_)); ++k) {
        if (!cmds_[k].ref) continue;
        if (on)
            XPLMRegisterCommandHandler(cmds_[k].ref, commandHandler, 1, reinterpret_cast<void*>(k));
        else
            XPLMUnregisterCommandHandler(cmds_[k].ref, commandHandler, 1, reinterpret_cast<void*>(k));
    }
    commandsRegistered_ = on;
}

void App::watchPurserCalls(bool on)
{
    if (on == purserWatched_) return;
    std::string missing;
    for (intptr_t k = 0; k < static_cast<intptr_t>(std::size(purserCmds_)); ++k) {
        WatchedCmd& c = purserCmds_[k];
        if (on) {
            c.ref = XPLMFindCommand(c.name);
            if (c.ref) XPLMRegisterCommandHandler(c.ref, purserCallHandler, 1, reinterpret_cast<void*>(k));
            else missing += std::string(missing.empty() ? "" : ", ") + c.name;
        } else if (c.ref) {
            XPLMUnregisterCommandHandler(c.ref, purserCallHandler, 1, reinterpret_cast<void*>(k));
            c.ref = nullptr;
        }
    }
    if (on && !missing.empty()) logInfo("purser call commands not on this aircraft: %s", missing.c_str());
    purserWatched_ = on;
}

int App::purserCallHandler(XPLMCommandRef, XPLMCommandPhase phase, void* refcon)
{
    // Observe only: always return 1 so ToLiss handles the press as usual.
    if (phase != xplm_CommandBegin) return 1;
    guarded("purser call", [&] {
        App& a = App::get();
        const intptr_t k = reinterpret_cast<intptr_t>(refcon);
        if (k < 0 || k >= static_cast<intptr_t>(std::size(a.purserCmds_))) return;
        const WatchedCmd& c = a.purserCmds_[k];
        logInfo("cabin call button: %s", c.name);
        a.recorder_.event(a.sim_.t, std::string("call button ") + c.name);
        if (c.cabinCall && a.purser_ && a.active_ && !FaultGuard::get().safeMode()) a.purser_->cabinCall();
    });
    return 1;
}

} // namespace fxa
