#include "app.h"

#include <XPLMDataAccess.h>
#include <XPLMPlugin.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "audio/hearing.h"
#include "core/guard.h"
#include "core/log.h"
#include "core/strings.h"
#include "ui/window.h"

namespace fxa {

namespace {

constexpr float kDetectInterval = 0.5f;
constexpr float kDetectWindow =
    12.0f;  // wait for toliss to register its datarefs
constexpr float kMainInterval = 0.25f;  // purser tick

double simSeconds() {
  static XPLMDataRef t = XPLMFindDataRef("sim/time/total_running_time_sec");
  return t ? XPLMGetDataf(t) : 0.0;
}

float detectCallback(float, float, int, void*) {
  return guarded("detect", 0.0f, [] { return App::get().detectTick(); });
}

float mainCallback(float elapsed, float, int, void*) {
  return guarded("tick", 0.0f, [&] { return App::get().mainTick(elapsed); });
}

std::string withSep(std::string p) {
  if (!p.empty() && p.back() != '/' && p.back() != '\\')
    p += XPLMGetDirectorySeparator();
  return p;
}

std::string readFile(const std::string& path) {
  std::error_code ec;
  const auto p = std::filesystem::u8path(path);
  if (!std::filesystem::is_regular_file(p, ec) ||
      std::filesystem::file_size(p, ec) > (4u << 20))
    return {};
  std::ifstream in(p, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

const char* onOff(bool b) { return b ? "on" : "off"; }

std::string fmt(const char* f, double v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), f, v);
  return buf;
}

}  // namespace

App& App::get() {
  static App app;
  return app;
}

App::~App() {
  // normally disable already stopped everything if not dont std terminate on a
  // joinable thread and dont touch xp from static dtors leak player purser
  if (scanThread_.joinable()) {
    scanCancel_ = true;
    scanThread_.join();
  }
  (void)purser_.release();
  (void)player_.release();
  if (cache_) cache_->shutdown();
}

// lifecycle

void App::start() {
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
  prefsDir_ = cut == std::string::npos ? outputDir_ + "preferences" + sep
                                       : pp.substr(0, cut + 1);
  packsDir_ = pluginsDir_ + "AirlinePacks" + sep;
  settingsPath_ = prefsDir_ + "FXAnnounce.ini";

  Log::get().open(outputDir_ + "FXAnnounce.log");
  Log::get().setSummarySink([](const char* line) { XPLMDebugString(line); });
  logSummary("FXAnnounce %s starting (packs: %s)", FXA_VERSION,
             packsDir_.c_str());
  loadSettingsFile();
  FaultGuard::get().setSafeModeHook([this] { enterSafeMode(); });
}

void App::enable() {
  enabled_ = true;
  model_.version = FXA_VERSION;
  model_.settings = &settings_;
  ui::setModelProvider([] {
    App& a = App::get();
    a.uiRefresh();
    return a.model();
  });

  XPLMCreateFlightLoop_t d{sizeof(XPLMCreateFlightLoop_t),
                           xplm_FlightLoop_Phase_AfterFlightModel,
                           detectCallback, nullptr};
  detectLoop_ = XPLMCreateFlightLoop(&d);
  XPLMCreateFlightLoop_t m{sizeof(XPLMCreateFlightLoop_t),
                           xplm_FlightLoop_Phase_AfterFlightModel, mainCallback,
                           nullptr};
  mainLoop_ = XPLMCreateFlightLoop(&m);
  for (Cmd& c : cmds_) c.ref = XPLMCreateCommand(c.name, c.desc);

  cache_ = std::make_unique<DecodeCache>();
  applySettings();  // muffle cutoff for the new cache
  player_ = std::make_unique<Player>(*cache_);
  purser_ = std::make_unique<Purser>(*player_);
  purser_->setEventSink([this](const TimelineRow& r) {
    recorder_.event(r.t, std::string(annTypeName(r.type)) + " " +
                             outcomeName(r.outcome) +
                             (r.detail.empty() ? "" : ": " + r.detail));
  });
  purser_->setNoteSink([this](double t, const std::string& text) {
    recorder_.event(t, "purser " + text);
  });
  // menu commands and window work on any aircraft scan once now so packs shows
  // up dormant too worker fs only
  buildMenu();
  registerCommands(true);
  startScan();
  refreshModel();
  scheduleDetection(kDetectWindow);
}

void App::disable() {
  deactivate("plugin disabled");
  const bool reopen = ui::windowVisible() || hiddenWhenDormant_;
  registerCommands(false);
  destroyMenu();
  ui::shutdownUi();
  settings_.windowVisible =
      reopen;  // else destroying the hidden window records it as closed
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

void App::stop() {
  releaseParkedAudio();
  logSummary("stopped");
  Log::get().setSummarySink(nullptr);
  Log::get().close();
}

void App::onMessage(int msg, void* param) {
  switch (msg) {
    case XPLM_MSG_PLANE_LOADED:
    case XPLM_MSG_LIVERY_LOADED:
      // 0 users plane
      if (reinterpret_cast<intptr_t>(param) == 0) {
        if (msg == XPLM_MSG_PLANE_LOADED) deactivate("aircraft reloaded");
        scheduleDetection(kDetectWindow);
      }
      break;
    case XPLM_MSG_PLANE_UNLOADED:
      if (reinterpret_cast<intptr_t>(param) == 0)
        deactivate("aircraft unloaded");
      break;
    case XPLM_MSG_WILL_WRITE_PREFS:
      saveSettingsFile();
      break;
    case XPLM_MSG_AIRPORT_LOADED:
      if (active_) resetFlight("new airport or situation loaded");
      break;
    case XPLM_MSG_FMOD_BANK_UNLOADING:
      if (player_)
        player_->stopAll();  // xp rebuilding sound banks our channels are gone
      break;
    default:
      break;
  }
}

// detection activation

void App::scheduleDetection(float seconds) {
  if (!detectLoop_ || FaultGuard::get().safeMode()) return;
  detectUntil_ = simSeconds() + seconds;
  XPLMScheduleFlightLoop(detectLoop_, kDetectInterval, 1);
}

float App::detectTick() {
  const AircraftInfo info = detectAircraft();
  loaded_ = info;
  if (info.toliss && !info.supported) {
    // toliss we dont support yet a339 a346 log once per aircraft
    if (active_) deactivate("not a supported ToLiss model");
    if (unsupportedLogged_ != info.acfPath) {
      unsupportedLogged_ = info.acfPath;
      logSummary(
          "dormant: ToLiss %s (%s) is not supported yet; this version runs on "
          "the A319, A320 and A321",
          info.acfFile.c_str(), familyName(info.family));
    }
    return 0.0f;
  }
  if (info.toliss) {
    if (!active_ || info.acfPath != aircraft_.acfPath ||
        info.liveryPath != aircraft_.liveryPath)
      activate(info);
    return 0.0f;  // done till next plane/livery load
  }
  if (simSeconds() < detectUntil_) return kDetectInterval;
  if (active_) deactivate("not a ToLiss aircraft");
  logInfo("dormant: %s (%s) is not a supported ToLiss aircraft",
          info.acfFile.c_str(), info.acfIcao.c_str());
  return 0.0f;
}

void App::activate(const AircraftInfo& info) {
  const bool wasActive = active_;
  const bool liveryChanged = info.liveryPath != aircraft_.liveryPath;
  aircraft_ = info;
  active_ = true;
  logSummary("active on %s (%s, %s), livery '%s'", info.acfFile.c_str(),
             info.acfIcao.c_str(), familyName(info.family),
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
    ofpTracker_.reset();
    originSeqno_ = jitterSeqno_ = ofpLoggedSeqno_ = -1;
    nextOfpCheck_ = -1;
    plan_ = BoardingPlan{};
    planLogged_.clear();
    watchPurserCalls(true);
    if (!library_ && !scanning_) startScan();
    if (!FaultGuard::get().safeMode())
      XPLMScheduleFlightLoop(mainLoop_, kMainInterval, 1);
    if (settings_.windowVisible) ui::showWindow(true);
    hiddenWhenDormant_ = false;
  }
  resolvePackNow(wasActive ? "livery changed" : "activated");
  updateMenuChecks();
  refreshModel();
}

void App::deactivate(const char* why) {
  if (!active_) return;
  logInfo("dormant: %s", why);
  active_ = false;
  if (mainLoop_) XPLMScheduleFlightLoop(mainLoop_, 0, 0);
  watchPurserCalls(false);
  // hide on leaving toliss reopen on the next one menu stays for the dormant
  // status
  const bool visible = ui::windowVisible();
  ui::showWindow(false);
  settings_.windowVisible = visible;
  hiddenWhenDormant_ = visible;
  recorder_.close();
  if (player_) player_->stopAll();
  if (purser_) purser_->reset();
  aircraft_ = AircraftInfo{};
  resolution_ = Resolution{};
  activePackName_.clear();
  havePos_ = false;
  updateMenuChecks();
  refreshModel();
}

void App::enterSafeMode() {
  // from faultguard stop anything that runs on its own keep menu window for
  // re-enable
  if (mainLoop_) XPLMScheduleFlightLoop(mainLoop_, 0, 0);
  if (detectLoop_) XPLMScheduleFlightLoop(detectLoop_, 0, 0);
  recorder_.close();
  if (player_) player_->stopAll();
  updateMenuChecks();
}

void App::reenable() {
  if (!FaultGuard::get().safeMode()) return;
  FaultGuard::get().reset();
  updateMenuChecks();
  scheduleDetection(kDetectWindow);
  if (active_) XPLMScheduleFlightLoop(mainLoop_, kMainInterval, 1);
}

// packs

void App::startScan() {
  stopScan();
  std::vector<std::string> roots = {packsDir_};
  for (const std::string& extra : splitList(settings_.packPaths, ";"))
    roots.push_back(extra);
  scanCancel_ = false;
  scanDone_ = false;
  scanning_ = true;
  packPathsScanned_ = settings_.packPaths;
  logInfo("scanning Airline Packs in %s", join(roots, "; ").c_str());
  scanThread_ = std::thread([this, roots] {
    // fs only never xplm
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

void App::stopScan() {
  if (scanThread_.joinable()) {
    scanCancel_ = true;
    scanThread_.join();
  }
  scanning_ = false;
}

void App::pollScan() {
  if (!scanning_ || !scanDone_) return;
  if (scanThread_.joinable()) scanThread_.join();
  scanning_ = false;
  std::shared_ptr<PackLibrary> lib;
  {
    std::lock_guard<std::mutex> lock(scanMu_);
    lib = std::move(scanResult_);
  }
  if (!lib) return;
  // builtin the users airlinepacks/airlines.tsv in front
  airlines_ = AirlineTable::builtin();
  const std::string userTable = readFile(packsDir_ + "airlines.tsv");
  if (!userTable.empty()) airlines_.prepend(AirlineTable::parse(userTable));
  for (const std::string& w : lib->warnings) logWarn("%s", w.c_str());
  for (const Pack& p : lib->packs) {
    // count custom files so the log shows them
    const std::vector<std::string> custom = p.customTypes();
    int notPlayed = 0;
    for (const SoundFile& f : p.files) notPlayed += f.type ? 0 : 1;
    const std::string extra =
        custom.empty()
            ? std::string()
            : ", " + std::to_string(notPlayed) +
                  " not played (not Fenix types: " + join(custom, ", ") + ")";
    logInfo("pack %s: %d sounds%s%s%s", p.name.c_str(),
            static_cast<int>(p.files.size()), extra.c_str(),
            p.wrapper.empty() ? "" : (" (in " + p.wrapper + ")").c_str(),
            p.mappingOnly ? " (mapping only)" : "");
    for (const std::string& w : p.warnings)
      logDebug("pack %s: %s", p.name.c_str(), w.c_str());
  }
  logSummary("%d Airline Packs, %d sounds", static_cast<int>(lib->packs.size()),
             static_cast<int>(lib->soundCount()));
  // locked pack pointer points into the old library repoint it before that goes
  // away or drop it
  const std::string lockedName =
      resolution_.pack ? resolution_.pack->name : std::string();
  if (player_) player_->stopAll();
  resolution_.pack = nullptr;
  library_ = std::move(lib);
  if (!active_) return;
  if (purser_ && purser_->packLocked() && !lockedName.empty()) {
    if (const Pack* same = library_->find(lockedName)) {
      resolution_.pack = same;
      updateVariant(false);
      return;
    }
  }
  resolvePackNow("packs scanned");
}

void App::loadLiveryConfig() {
  const Ini sidecar =
      aircraft_.liveryPath.empty()
          ? Ini{}
          : Ini::parse(readFile(aircraft_.liveryPath + "airlinepack.cfg"));
  const Ini central = Ini::parse(readFile(packsDir_ + "liveries.ini"));
  liveryCfg_ =
      mergeLiveryConfig(liveryConfigFromSidecar(sidecar),
                        liveryConfigFromCentral(central, aircraft_.liveryName));
  tlscfg_ = aircraft_.liveryPath.empty()
                ? Ini{}
                : Ini::parse(readFile(aircraft_.liveryPath + "livery.tlscfg"));
  if (!liveryCfg_.source.empty())
    logInfo("livery config from %s", liveryCfg_.source.c_str());
}

void App::updateAircraftType() {
  // neo live acf_icao first engine option can flip it then livery.tlscfg then
  // the lea code
  const std::string icao =
      toUpper(sim_.acfIcao.empty() ? aircraft_.acfIcao : sim_.acfIcao);
  int neo = -1;
  if (icao == "A20N" || icao == "A21N" || icao == "A19N")
    neo = 1;
  else if (icao == "A320" || icao == "A321" || icao == "A319")
    neo = 0;
  if (neo < 0) {
    if (const std::string* e = tlscfg_.get("", "eng_type"))
      neo = engineIsNeo(*e);
  }
  if (neo < 0) neo = engineIsNeo(liveryStringCode(aircraft_.liveryName));
  const Family fam = familyFrom(icao, aircraft_.acfFile);
  const AircraftType t = makeAircraftType(
      fam == Family::Unknown ? aircraft_.family : fam, neo == 1);
  if (t.tags != type_.tags)
    logInfo("aircraft type tags: %s", join(t.tags, ", ").c_str());
  type_ = t;
}

std::vector<std::string> App::customTags() const {
  std::vector<std::string> tags = liveryCfg_.customTags;
  if (sim_.satcom)
    for (const std::string& t : splitList(settings_.satcomTags))
      if (std::none_of(tags.begin(), tags.end(),
                       [&](const std::string& x) { return iequals(x, t); }))
        tags.push_back(t);
  return tags;
}

SelectContext App::selectContext() const {
  SelectContext c;
  c.aircraft = type_;
  c.refueling = refuel_.refueling(sim_.t);
  c.time = timeOfDayAt(sim_.localTimeSec);
  if (std::isfinite(sim_.localTimeSec)) {
    c.localHour = std::fmod(sim_.localTimeSec / 3600.0, 24.0);
    if (c.localHour < 0) c.localHour += 24.0;
  }
  c.phase = purser_ ? purser_->phase() : Phase::PreFlight;
  c.overwater = overwater_;
  c.customTags = customTags();
  c.variant = variant_;
  return c;
}

void App::resetFlight(const char* why) {
  logInfo("flight reset: %s", why);
  if (player_) player_->stopAll();
  if (purser_) purser_->reset();
  havePos_ = false;
  refuel_.reset();
  nextOfpCheck_ = -1;  // might be at another airport now
  ofpTracker_.reset();
  resolvePackNow(why);
}

void App::trackRefuel() {
  // 4.2
  const double rate = refuel_.update(sim_, settings_);
  if (rate > 0) logInfo("refuelling detected (%.1f kg/s)", rate);
}

// simbrief

void App::readOfp() {
  std::string why;
  ofp_ = parseOfp(sbh_.text(), &why);
  ofpWhy_ = ofp_ ? std::string() : why;
  nextOfpCheck_ = -1;  // check it this tick
  if (!ofp_) {
    logInfo("SimBrief: %s", why.c_str());
    return;
  }
  logInfo("SimBrief OFP #%d: %s %s-%s, ETD %s%s", ofp_->seqno,
          ofp_->callsign.empty() ? "(no callsign)" : ofp_->callsign.c_str(),
          ofp_->origin.c_str(), ofp_->destination.c_str(),
          zuluLabel(static_cast<double>(ofp_->estOut)).c_str(),
          ofp_->stale ? " (simbrief_hub: last download failed)" : "");
  const std::string icao =
      toUpper(sim_.acfIcao.empty() ? aircraft_.acfIcao : sim_.acfIcao);
  if (!ofp_->aircraftIcao.empty() && !icao.empty() &&
      familyFrom(ofp_->aircraftIcao, "") != familyFrom(icao, ""))
    logInfo("SimBrief OFP is planned for a %s; flying a %s",
            ofp_->aircraftIcao.c_str(), icao.c_str());
}

void App::checkOfpHere(bool force) {
  // 4.2a origin check till takeoff then the tracker follows the latched ofp
  const bool tookOff = purser_ && purser_->tookOff();
  if (tookOff != lastTookOff_) {
    lastTookOff_ = tookOff;
    force = true;  // latch while still at the origin
  }
  if (!ofp_) {
    ofpTracker_.update(std::nullopt, {}, {}, sim_.lat, sim_.lon, sim_.onGround,
                       tookOff, settings_);
    ofpOk_ = false;
    ofpCheck_.clear();
    return;
  }
  if (!force && nextOfpCheck_ >= 0 && sim_.t < nextOfpCheck_) return;
  nextOfpCheck_ = sim_.t + 10;
  if (sim_.lat == 0 && sim_.lon == 0) return;
  if (originSeqno_ != ofp_->seqno) {  // one navdb search per airport per ofp
    originPlace_ = destPlace_ = OfpPlace{};
    originPlace_.found = SimbriefSource::findAirport(
        ofp_->origin, sim_.lat, sim_.lon, originPlace_.lat, originPlace_.lon);
    if (!ofp_->destination.empty())
      destPlace_.found =
          SimbriefSource::findAirport(ofp_->destination, sim_.lat, sim_.lon,
                                      destPlace_.lat, destPlace_.lon);
    originSeqno_ = ofp_->seqno;
  }
  const bool wasLatched = ofpTracker_.latched();
  const OfpTracker::Stage before = ofpTracker_.stage();
  ofpTracker_.update(ofp_, originPlace_, destPlace_, sim_.lat, sim_.lon,
                     sim_.onGround, tookOff, settings_);
  const bool ok = ofpTracker_.usable();
  const std::string why = ofpTracker_.why();
  if (ofpTracker_.latched()) {
    // only on stage change distance changes constantly
    if (!wasLatched || ofpTracker_.stage() != before) {
      logInfo("SimBrief: %s", ofpTracker_.status().c_str());
      recorder_.event(sim_.t, "simbrief " + ofpTracker_.status());
    }
    if (!ofpTracker_.currentIsLatched() &&
        (ofpLoggedSeqno_ != ofp_->seqno || ok != ofpOk_)) {
      ofpLoggedSeqno_ = ofp_->seqno;
      logInfo("SimBrief OFP #%d is for the next leg: %s", ofp_->seqno,
              ok ? "accepted here" : why.c_str());
    }
  } else if (ok != ofpOk_ || ofpLoggedSeqno_ != ofp_->seqno || wasLatched) {
    ofpLoggedSeqno_ = ofp_->seqno;
    if (ok)
      logInfo("SimBrief OFP accepted: the aircraft is at %s",
              ofp_->origin.c_str());
    else
      logInfo("SimBrief OFP ignored: %s", why.c_str());
    recorder_.event(sim_.t, std::string("simbrief ") +
                                (ok ? "accepted" : "ignored: " + why));
  }
  ofpOk_ = ok;
  ofpCheck_ = why;
}

void App::updateBoardingPlan() {
  BoardingPlan p;
  if (ofp_ && ofpOk_) {
    if (jitterSeqno_ != ofp_->seqno) {
      // once per ofp 4.1a
      std::uniform_real_distribution<double> d(-settings_.leadJitterS,
                                               settings_.leadJitterS);
      leadJitter_ = settings_.leadJitterS > 0 ? d(rng_) : 0.0;
      jitterSeqno_ = ofp_->seqno;
    }
    p = makeBoardingPlan(*ofp_, type_.family, sim_.pax, settings_, leadJitter_);
  } else if (ofpTracker_.latched()) {
    p.summary =
        "the OFP is in use for this flight";  // status line has the details
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

std::string App::simbriefStatus(int* tone) const {
  const char* mode =
      settings_.boarding() == BoardingStart::Doors
          ? " (not used for boarding: Boarding starts = Doors open)"
      : settings_.boarding() == BoardingStart::Call
          ? " (not used for boarding: Boarding starts = Cabin call only)"
          : "";
  if (!sbh_.installed()) {
    *tone = 2;
    return "simbrief_hub not installed: boarding starts when a door has been "
           "open at the gate";
  }
  if (!ofp_) {
    *tone = 2;
    return ofpWhy_.empty() ? std::string("no OFP") : ofpWhy_;
  }
  if (ofpTracker_.latched() && !ofpOk_) {
    *tone = 0;
    return ofpTracker_.status();
  }
  if (!ofpOk_) {
    *tone = 1;
    return "OFP ignored: " +
           (ofpCheck_.empty() ? std::string("not checked yet") : ofpCheck_) +
           "; doors rule";
  }
  *tone = 0;
  std::string line = plan_.summary;
  const std::string note = purser_ ? purser_->boardingNote() : std::string();
  if (sim_.zuluSec >= 0 && note.empty()) {
    const double until = secondsUntil(plan_.boardingAtSec, sim_.zuluSec);
    if (until > 0) {
      char buf[48];
      std::snprintf(buf, sizeof(buf), " (boarding in %d:%02d)",
                    static_cast<int>(until) / 60, static_cast<int>(until) % 60);
      line += buf;
    }
  }
  if (!note.empty()) {
    // shortened boarding or clock mismatch
    *tone = 1;
    return line + mode + ". " + note;
  }
  return line + mode;
}

void App::trackPosition() {
  // 4.4 1 nm in one tick teleport/situation load
  if (sim_.lat == 0 && sim_.lon == 0) return;
  if (havePos_) {
    const double dLat = (sim_.lat - lastLat_) * 60.0;
    const double dLon = (sim_.lon - lastLon_) * 60.0 *
                        std::cos(sim_.lat * 3.14159265358979 / 180.0);
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

void App::resolvePackNow(const char* why) {
  resolvedFlightNumber_ = sim_.flightNumber;
  // grab it before the lock check so a callsign change while locked shows up
  // once not every tick
  resolvedCallsign_ = ofpCallsign();
  if (!library_) return;
  if (purser_ && purser_->packLocked() && resolution_.pack) {
    logDebug("pack locked until the next flight; not re-resolving (%s)", why);
    return;
  }
  const Resolution r =
      resolvePack(*library_, airlines_,
                  ResolveInput{aircraft_.liveryName, sim_.flightNumber,
                               liveryCfg_, resolvedCallsign_});
  const bool changed = r.pack != resolution_.pack;
  resolution_ = r;
  if (changed || std::string(why) == "activated")
    logSummary("airline pack: %s (%s)", r.pack ? r.pack->name.c_str() : "none",
               r.reason.c_str());
  const std::string name = r.pack ? r.pack->name : std::string();
  const bool packChanged = name != activePackName_;
  activePackName_ = name;
  updateVariant(packChanged);
}

void App::updateVariant(bool reroll) {
  const Pack* pack = resolution_.pack;
  const std::vector<int> choices =
      pack ? pack->variantChoices() : std::vector<int>{0};
  if (!reroll &&
      std::find(choices.begin(), choices.end(), variant_) != choices.end())
    return;
  variant_ = pack ? pickVariant(*pack, rng_) : 0;
  if (pack && (choices.size() > 1 || variant_ != 0)) {
    std::string list;
    for (int v : choices)
      list += (list.empty() ? "" : ", ") + std::to_string(v);
    logInfo("pack %s: variant %d (the pack has %s)", pack->name.c_str(),
            variant_, list.c_str());
  }
}

// main tick

float App::mainTick(float elapsed) {
  if (!active_ || FaultGuard::get().safeMode()) return 0.0f;
  pollScan();
  const std::string oldIcao = sim_.acfIcao;
  sim_ = adapter_.read(settings_, elapsed);
  if (sim_.acfIcao != oldIcao) updateAircraftType();
  if (sim_.flightNumber != resolvedFlightNumber_)
    resolvePackNow("flight number changed");
  trackRefuel();
  trackPosition();

  // ofp its check here boarding plan pack 4.1a 1.4
  if (sbh_.poll(sim_.t)) readOfp();
  checkOfpHere(false);
  updateBoardingPlan();
  if (ofpCallsign() != resolvedCallsign_)
    resolvePackNow("SimBrief OFP changed");

  // purser and audio the player selects from the active pack with the current
  // tags
  player_->setSource(resolution_.pack, selectContext(), &settings_, &sim_,
                     muted_ || !settings_.enabled);
  purser_->setAircraft(type_);
  purser_->setBoardingPlan(plan_);
  purser_->tick(sim_, settings_);
  {
    // 1.4 next flight unlocks the pack
    const bool locked = purser_->packLocked();
    if (packWasLocked_ && !locked) resolvePackNow("next flight");
    packWasLocked_ = locked;
  }

  if (auto next = purser_->nextQueued())
    player_->prefetch(next->first, next->second);
  player_->update(elapsed);

  if (settings_.recorder && !recorder_.isOpen()) {
    char name[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(name, sizeof(name), "%Y%m%d-%H%M%S.fxtrace.csv",
                  std::localtime(&now));
    const std::string path =
        outputDir_ + "FXAnnounce" + XPLMGetDirectorySeparator() + name;
    if (recorder_.open(path)) {
      logInfo("recording trace to %s", path.c_str());
      // new trace starts w the current check plan
      if (ofp_)
        recorder_.event(sim_.t,
                        std::string("simbrief ") +
                            (ofpOk_ ? "accepted" : "ignored: " + ofpCheck_));
      if (!planLogged_.empty())
        recorder_.event(sim_.t, "boarding plan: " + planLogged_);
    } else {
      logWarn("could not open %s; recorder off", path.c_str());
      settings_.recorder = false;
    }
  } else if (!settings_.recorder && recorder_.isOpen()) {
    recorder_.close();
  }
  recorder_.state(sim_);
  if (recorder_.isOpen() && sim_.t - rawAt_ >= 1.0) {
    // raw values only on change so the trace shows which index moved
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

void App::uiRefresh() {
  if (active_ || !enabled_) return;  // main loop refreshes it while active
  pollScan();
  refreshModel();
}

void App::fillFlight(ui::UiModel::Flight& f) const {
  f = {};
  if (!sbh_.installed()) {
    f.reason =
        "simbrief_hub is not installed: boarding starts when a door has been "
        "open at the gate.";
    return;
  }
  if (!ofp_) {
    f.reason =
        ofpWhy_.empty() ? std::string("No OFP in simbrief_hub yet.") : ofpWhy_;
    return;
  }
  f.callsign = ofp_->callsign;
  f.origin = ofp_->origin;
  f.destination = ofp_->destination;
  f.etd = zuluLabel(secondOfDay(static_cast<double>(ofp_->estOut)));
  // airline from the callsign and the great circle route bar positions are the
  // current ofps so only trust them while it passes or is latched
  if (f.callsign.size() >= 3 &&
      std::isalpha(static_cast<unsigned char>(f.callsign[0])) &&
      std::isalpha(static_cast<unsigned char>(f.callsign[1])) &&
      std::isalpha(static_cast<unsigned char>(f.callsign[2])))
    f.airline = airlines_.nameFor(f.callsign.substr(0, 3));
  if ((ofpOk_ || ofpTracker_.currentIsLatched()) &&
      originSeqno_ == ofp_->seqno && originPlace_.found && destPlace_.found) {
    const double total = distanceNm(originPlace_.lat, originPlace_.lon,
                                    destPlace_.lat, destPlace_.lon);
    const double toGo =
        distanceNm(sim_.lat, sim_.lon, destPlace_.lat, destPlace_.lon);
    if (total > 1.0) {
      f.toGoNm = toGo;
      f.routeFrac =
          static_cast<float>(std::clamp(1.0 - toGo / total, 0.0, 1.0));
    }
  }
  if (ofpTracker_.latched() && !ofpOk_) {
    f.state = 1;
    f.line = ofpTracker_.status();
    return;
  }
  if (!ofpOk_) {
    f.state = 2;
    f.reason =
        "Not used here: " +
        (ofpCheck_.empty() ? std::string("not checked yet") : ofpCheck_) +
        ". Boarding starts when a door has been open at the gate.";
    return;
  }
  f.state = 1;
  if (plan_.valid) {
    f.etd = zuluLabel(plan_.etdSec);
    f.boarding = zuluLabel(plan_.boardingAtSec);
  }
  if (settings_.boarding() != BoardingStart::Schedule) {
    f.line = settings_.boarding() == BoardingStart::Doors
                 ? "Not used for boarding (Boarding starts: Doors open)"
                 : "Not used for boarding (Boarding starts: Cabin call only)";
  } else if (plan_.valid && sim_.zuluSec >= 0 &&
             (!purser_ || purser_->boardingNote().empty())) {
    const double until = secondsUntil(plan_.boardingAtSec, sim_.zuluSec);
    if (until > 0) {
      char buf[48];
      std::snprintf(buf, sizeof(buf), "Boarding in %d:%02d",
                    static_cast<int>(until) / 60, static_cast<int>(until) % 60);
      f.line = buf;
    }
  }
}

void App::refreshModel() {
  model_.active = active_;
  model_.safeMode = FaultGuard::get().safeMode();
  model_.lastFault = FaultGuard::get().lastFault();
  model_.muted = muted_;
  model_.overwater = overwater_;
  model_.library = library_.get();
  model_.scanning = scanning_;
  model_.packsDir = packsDir_;
  model_.recorderPath = recorder_.isOpen() ? recorder_.path() : std::string();
  model_.log = Log::get().recent();
  if (active_) {
    model_.loadedAircraft.clear();
    model_.aircraft = "ToLiss " + type_.label;
    model_.livery = aircraft_.liveryName.empty() ? "(default livery)"
                                                 : aircraft_.liveryName;
    model_.pack = resolution_.pack ? resolution_.pack->name
                                   : (library_ ? "none" : "scanning...");
    model_.customTags = join(customTags(), ", ");
    fillFlight(model_.flight);
    const SimState& s = sim_;
    const bool estimated = purser_ && purser_->phaseEstimated();
    const Hearing heard = cabinHearing(settings_, s);
    model_.liveInputs = {
        {"FMGS phase",
         s.fmgsPhase ? std::string(phaseName(*s.fmgsPhase)) + " (" +
                           std::to_string(s.fmgsPhaseRaw) + ")"
                     : "unknown (" + std::to_string(s.fmgsPhaseRaw) + ")"},
        {"Purser phase",
         purser_ ? std::string(phaseName(purser_->phase())) +
                       (estimated ? " (estimated: FMGS not initialized)" : "")
                 : "-"},
        {"ECAM / QPAC phase",
         (s.ecamPhase >= 0 ? std::to_string(s.ecamPhase) : std::string("-")) +
             " / " +
             (s.qpacPhase >= 0 ? std::to_string(s.qpacPhase)
                               : std::string("-"))},
        {"Thrust levers", s.throttleKnown() ? fmt("%.2f", s.throttleMax) : "-"},
        {"Flight number", s.flightNumber.empty() ? "-" : s.flightNumber},
        {"On ground", s.onGround ? "yes" : "no"},
        {"Speed / height",
         fmt("%.0f kt GS", s.gsKt) + ", " + fmt("%.0f ft AGL", s.aglFt)},
        {"Beacon / seatbelts",
         std::string(onOff(s.beacon)) + " / " + onOff(s.seatbelt)},
        {"Engines running", std::to_string(s.enginesRunning) + " of " +
                                std::to_string(s.engineCount)},
        {"Passenger doors", fmt("most open %.0f%%", s.doorMax * 100) +
                                fmt(", L1 %.0f%%", s.doorFwd * 100)},
        {"Slides",
         s.slidesKnown ? (s.slidesArmed ? "armed" : "disarmed") : "unknown"},
        {"Passengers", std::to_string(s.pax)},
        {"AC power", s.acPower ? "yes" : "no"},
        {"ACP PA / CAB",
         std::string(s.paOn ? fmt("PA %.0f%%", s.paVol * 100) : "PA in") +
             " / " + (s.cabOn ? fmt("CAB %.0f%%", s.cabVol * 100) : "CAB in")},
        {"Cockpit door",
         fmt("%.0f%% open", s.cockpitDoor * 100) +
             (s.doorBlend >= 0
                  ? fmt(" (ToLiss sound blend %.0f%%)", s.doorBlend * 100)
                  : std::string())},
        {"Cabin PA heard",
         fmt("clear %.0f%%", heard.clear * 100) +
             (settings_.muffle && settings_.realisticHearing
                  ? fmt(", through the door %.0f%%", heard.muffled * 100)
                  : std::string())},
        {"ToLiss volume",
         s.tolissVolInternal < 0
             ? std::string("-")
             : fmt("internal %.0f%%", s.tolissVolInternal * 100) +
                   (s.tolissVolOverride == 1 ? ", ISCS override on"
                    : s.tolissVolOverride == 0
                        ? ", ISCS override off"
                        : ", override switch unknown (Settings > ToLiss)")},
        {"Zulu time", s.zuluSec >= 0 ? zuluLabel(s.zuluSec) : std::string("-")},
        {"Camera", s.viewExternal
                       ? "external"
                       : (s.cameraInCabin ? "in the cabin" : "cockpit")},
        {"Local time",
         fmt("%02.0f:", std::floor(s.localTimeSec / 3600)) +
             fmt("%02.0f", std::fmod(std::floor(s.localTimeSec / 60), 60)) +
             std::string(" (") + timeOfDayName(timeOfDayAt(s.localTimeSec)) +
             ")"},
        {"Sun elevation", fmt("%.1f deg", s.sunPitchDeg)},
        {"Recorder", recorder_.isOpen() ? recorder_.path() : "off"},
    };
    if (purser_ && player_) {
      model_.liveInputs.insert(
          model_.liveInputs.begin(),
          {{"Variant", std::to_string(variant_)},
           {"Time-of-day tag", timeOfDayName(timeOfDayAt(sim_.localTimeSec))},
           {"Refuelling", refuel_.refueling(sim_.t) ? "yes" : "no"},
           {"Overwater", overwater_ ? "yes" : "no"}});
      model_.stage = purser_->stageLabel();
      model_.calling = purser_->calling();
      model_.callAttempt = purser_->callAttempt();
      model_.boardingNote = purser_->boardingNote();
      model_.boardingFrac = -1;
      model_.boardingLeftSec = -1;
      const double from = purser_->boardingStartedAt(),
                   until = purser_->boardingEndsAt();
      if (from >= 0 && until > from) {
        model_.boardingFrac = static_cast<float>(
            std::clamp((sim_.t - from) / (until - from), 0.0, 1.0));
        model_.boardingLeftSec = std::max(0.0, until - sim_.t);
        model_.boardingPax =
            plan_.valid && plan_.pax > 0 ? plan_.pax : sim_.pax;
      }
      if (const auto cur = purser_->current()) {
        model_.playing = annLabel(cur->first, cur->second);
        model_.playingFenix = annFenixLabel(cur->first, cur->second);
      } else {
        model_.playing.clear();
        model_.playingFenix.clear();
      }
      model_.pending.clear();
      for (const PendingItem& p : purser_->pending()) {
        ui::UiModel::Pending row;
        row.name = p.type ? annLabel(*p.type, p.context) : p.what;
        row.note = p.note;
        row.fenix = p.type ? annFenixLabel(*p.type, p.context) : std::string();
        row.inSec = p.inSec;
        row.condition = p.condition;
        model_.pending.push_back(std::move(row));
      }
      const Player::NowPlaying now = player_->nowPlaying();
      model_.now.file = now.label;
      model_.now.pos = now.pos;
      model_.now.dur = now.dur;
      model_.now.decoding = now.decoding;
      model_.now.paused = now.paused;
      model_.now.interphone = now.interphone;
      model_.music = player_->musicLabel();
      model_.events.clear();
      const auto& tl = purser_->timeline();
      for (auto r = tl.rbegin(); r != tl.rend() && model_.events.size() < 200;
           ++r) {
        ui::UiModel::EventRow e;
        e.zuluSec = r->zuluSec;
        e.localSec = r->localSec;
        e.name = annLabel(r->type, r->context);
        e.fenix = annFenixLabel(r->type, r->context);
        e.outcome = outcomeName(r->outcome);
        e.detail = r->detail;
        e.tone =
            (r->outcome == Outcome::Played || r->outcome == Outcome::Playing)
                ? 0
            : (r->outcome == Outcome::NoSound ||
               r->outcome == Outcome::Unanswered ||
               r->outcome == Outcome::Expired)
                ? 1
                : 2;
        model_.events.push_back(std::move(e));
      }
    }
  } else {
    // dormant name whatever got loaded
    const AircraftInfo& d = loaded_;
    std::string name = !d.uiName.empty() ? d.uiName : d.acfIcao;
    if (!d.acfFile.empty())
      name += name.empty() ? d.acfFile : " (" + d.acfFile + ")";
    if (d.toliss && !d.supported)
      name += ": a ToLiss model this version doesn't support yet";
    model_.loadedAircraft = name;
    model_.aircraft.clear();
    model_.livery.clear();
    model_.pack.clear();
    model_.customTags.clear();
    model_.flight = {};
    model_.liveInputs.clear();
    model_.stage.clear();
    model_.calling = false;
    model_.boardingNote.clear();
    model_.boardingFrac = -1;
    model_.playing.clear();
    model_.playingFenix.clear();
    model_.pending.clear();
    model_.now = {};
    model_.music.clear();
    model_.events.clear();
  }
  model_.onSkip = [] { App::get().skip(); };
  model_.onAdvance = [] { App::get().advance(); };
  model_.onToggleMute = [] { App::get().toggleMute(); };
  model_.onToggleOverwater = [] { App::get().toggleOverwater(); };
  model_.onRescan = [] { App::get().rescan(); };
  model_.onReenable = [] { App::get().reenable(); };
  model_.explain = [](int type, int ctx) {
    App& a = App::get();
    std::vector<std::string> lines;
    if (!a.resolution_.pack) return std::vector<std::string>{"No active pack."};
    if (type < 0 || type >= kAnnTypeCount) return lines;
    SelectContext c = a.selectContext();
    c.context = static_cast<CallContext>(std::clamp(ctx, 0, 2));
    Rng rng(1);
    const Selection sel =
        selectSound(*a.resolution_.pack, static_cast<AnnType>(type), c, rng);
    lines.push_back(
        "Pack " + a.resolution_.pack->name + ", " + c.aircraft.label + " [" +
        join(c.aircraft.tags, ",") + "], " + timeOfDayName(c.time) +
        ", phase " + phaseName(c.phase) + ", variant " +
        std::to_string(c.variant) + (c.refueling ? ", refuelling" : "") +
        (c.overwater ? ", overwater" : "") +
        (c.customTags.empty() ? "" : ", tags " + join(c.customTags, ",")));
    if (sel.file)
      lines.push_back(
          "-> plays " + sel.file->relPath + fmt(" (score %.1f)", sel.score) +
          (sel.variantFallback ? ", variant " + std::to_string(sel.variant) +
                                     " (nothing fits in variant " +
                                     std::to_string(c.variant) + ")"
                               : std::string()) +
          (sel.timeFallback ? std::string(", nearest time of day ") +
                                  timeOfDayName(sel.time) + " (nothing for " +
                                  timeOfDayName(c.time) + ")"
                            : std::string()));
    else
      lines.push_back("-> nothing eligible: the announcement is skipped");
    for (const Candidate& cd : sel.candidates)
      lines.push_back(std::string(cd.eligible ? "  ok   " : "  no   ") +
                      cd.file->relPath + ": " + cd.why);
    if (sel.candidates.empty())
      lines.push_back("  (the pack has no file of this type)");
    return lines;
  };
  model_.onSettingsChanged = [] {
    App& a = App::get();
    a.applySettings();
    a.saveSettingsFile();
    if (a.settings_.packPaths != a.packPathsScanned_)
      a.rescan();  // pack folders changed
  };
}

// actions

void App::toggleWindow() {
  hiddenWhenDormant_ = false;
  ui::toggleWindow();
}
void App::skip() {
  if (active_ && purser_) purser_->skip();
}

void App::advance() {
  if (active_ && purser_) purser_->advance();
}

void App::toggleMute() {
  muted_ = !muted_;
  logInfo("%s", muted_ ? "muted" : "unmuted");
  updateMenuChecks();
}

void App::toggleOverwater() {
  if (!active_) return;  // livery config sets it per flight
  overwater_ = !overwater_;
  logInfo("overwater flight: %s", overwater_ ? "yes" : "no");
  updateMenuChecks();
}

void App::rescan() {
  if (active_) loadLiveryConfig();
  startScan();
}

// settings

void App::loadSettingsFile() {
  settingsIni_ = Ini::parse(readFile(settingsPath_));
  loadSettings(settingsIni_, settings_);
  const std::vector<std::string> migrated =
      migrateSettings(settingsIni_, settings_);
  applySettings();
  logInfo("settings: %s", settingsPath_.c_str());
  for (const std::string& m : migrated)
    logInfo("settings: updated %s", m.c_str());
  if (!migrated.empty()) saveSettingsFile();
}

void App::saveSettingsFile() {
  storeSettings(settings_, settingsIni_);
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::u8path(prefsDir_), ec);
  std::ofstream out(std::filesystem::u8path(settingsPath_),
                    std::ios::binary | std::ios::trunc);
  if (!out) {
    logWarn("could not write %s", settingsPath_.c_str());
    return;
  }
  out << "# FXAnnounce settings (edited from the FXAnnounce window; see "
         "docs/VP_SPEC.md)\n"
      << settingsIni_.serialize();
}

void App::applySettings() {
  Log::get().setLevel(
      static_cast<LogLevel>(std::clamp(settings_.logLevel, 0, 3)));
  // only need the muffled copies when the door actually muffles 4.5
  if (cache_)
    cache_->setMuffleCutoff(settings_.muffle && settings_.realisticHearing
                                ? settings_.muffleCutoffHz
                                : 0.0f);
}

// menu commands

void App::menuHandler(void*, void* itemRef) {
  guarded("menu", [&] {
    App& a = App::get();
    const intptr_t item = reinterpret_cast<intptr_t>(itemRef);
    switch (item) {
      case 1:
        a.toggleMute();
        break;
      case 2:
        a.toggleOverwater();
        break;
      case 3:
        a.rescan();
        break;
      case 4:
        a.reenable();
        break;
      default:
        break;
    }
  });
}

void App::buildMenu() {
  if (menu_) return;
  menuItem_ =
      XPLMAppendMenuItem(XPLMFindPluginsMenu(), "FXAnnounce", nullptr, 0);
  menu_ = XPLMCreateMenu("FXAnnounce", XPLMFindPluginsMenu(), menuItem_,
                         menuHandler, nullptr);
  miWindow_ =
      XPLMAppendMenuItemWithCommand(menu_, "Show/hide window", cmds_[0].ref);
  miMute_ = XPLMAppendMenuItem(menu_, "Mute",
                               reinterpret_cast<void*>(intptr_t{1}), 0);
  miOverwater_ = XPLMAppendMenuItem(menu_, "Overwater flight",
                                    reinterpret_cast<void*>(intptr_t{2}), 0);
  miRescan_ = XPLMAppendMenuItem(menu_, "Rescan Airline Packs",
                                 reinterpret_cast<void*>(intptr_t{3}), 0);
  XPLMAppendMenuSeparator(menu_);
  miReenable_ = XPLMAppendMenuItem(menu_, "Re-enable after fault",
                                   reinterpret_cast<void*>(intptr_t{4}), 0);
  updateMenuChecks();
}

void App::destroyMenu() {
  if (!menu_) return;
  XPLMDestroyMenu(menu_);
  XPLMRemoveMenuItem(XPLMFindPluginsMenu(), menuItem_);
  menu_ = nullptr;
  menuItem_ = -1;
}

void App::updateMenuChecks() {
  if (!menu_) return;
  XPLMCheckMenuItem(menu_, miMute_,
                    muted_ ? xplm_Menu_Checked : xplm_Menu_Unchecked);
  XPLMCheckMenuItem(menu_, miOverwater_,
                    overwater_ ? xplm_Menu_Checked : xplm_Menu_Unchecked);
  XPLMEnableMenuItem(menu_, miReenable_, FaultGuard::get().safeMode() ? 1 : 0);
  XPLMEnableMenuItem(menu_, miMute_, active_ ? 1 : 0);
  XPLMEnableMenuItem(menu_, miOverwater_, active_ ? 1 : 0);
}

int App::commandHandler(XPLMCommandRef, XPLMCommandPhase phase, void* refcon) {
  if (phase != xplm_CommandBegin) return 1;
  guarded("command", [&] {
    App& a = App::get();
    auto fn = a.cmds_[reinterpret_cast<intptr_t>(refcon)].fn;
    (a.*fn)();
  });
  return 1;
}

void App::registerCommands(bool on) {
  if (on == commandsRegistered_) return;
  for (intptr_t k = 0; k < static_cast<intptr_t>(std::size(cmds_)); ++k) {
    if (!cmds_[k].ref) continue;
    if (on)
      XPLMRegisterCommandHandler(cmds_[k].ref, commandHandler, 1,
                                 reinterpret_cast<void*>(k));
    else
      XPLMUnregisterCommandHandler(cmds_[k].ref, commandHandler, 1,
                                   reinterpret_cast<void*>(k));
  }
  commandsRegistered_ = on;
}

void App::watchPurserCalls(bool on) {
  if (on == purserWatched_) return;
  std::string missing;
  for (intptr_t k = 0; k < static_cast<intptr_t>(std::size(purserCmds_)); ++k) {
    WatchedCmd& c = purserCmds_[k];
    if (on) {
      c.ref = XPLMFindCommand(c.name);
      if (c.ref)
        XPLMRegisterCommandHandler(c.ref, purserCallHandler, 1,
                                   reinterpret_cast<void*>(k));
      else
        missing += std::string(missing.empty() ? "" : ", ") + c.name;
    } else if (c.ref) {
      XPLMUnregisterCommandHandler(c.ref, purserCallHandler, 1,
                                   reinterpret_cast<void*>(k));
      c.ref = nullptr;
    }
  }
  if (on && !missing.empty())
    logInfo("purser call commands not on this aircraft: %s", missing.c_str());
  purserWatched_ = on;
}

int App::purserCallHandler(XPLMCommandRef, XPLMCommandPhase phase,
                           void* refcon) {
  // observe only always return 1 so toliss still gets the press
  if (phase != xplm_CommandBegin) return 1;
  guarded("purser call", [&] {
    App& a = App::get();
    const intptr_t k = reinterpret_cast<intptr_t>(refcon);
    if (k < 0 || k >= static_cast<intptr_t>(std::size(a.purserCmds_))) return;
    const WatchedCmd& c = a.purserCmds_[k];
    logInfo("cabin call button: %s", c.name);
    a.recorder_.event(a.sim_.t, std::string("call button ") + c.name);
    if (c.cabinCall && a.purser_ && a.active_ && !FaultGuard::get().safeMode())
      a.purser_->cabinCall();
  });
  return 1;
}

}  // namespace fxa
