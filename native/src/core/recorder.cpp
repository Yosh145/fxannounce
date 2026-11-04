#include "core/recorder.h"

#include <cstdlib>
#include <filesystem>
#include <map>

namespace fxa {

namespace {

// keep header and line in sync reader goes by names so new cols go before event
// 0.1.x ends at satcom event 0.2.1 at door_blend event
const char* kHeader =
    "kind,t,paused,replay,on_ground,gs_kt,ias_kt,agl_ft,msl_ft,vs_fpm,lat,lon,"
    "fmgs_phase_raw,cruise_alt_ft,beacon,"
    "seatbelt,landing_lights,speedbrake,park_brake,chocks,engines_running,"
    "engine_count,door_max,door_fwd,door_count,"
    "slides_armed,pax,fuel_kg,ac_power,pax_masks,cabin_alt_ft,pa_on,pa_vol,cab_"
    "on,cab_vol,cockpit_door,view_external,"
    "camera_in_cabin,sun_pitch,local_time,flight_number,acf_icao,satcom,"
    "ecam_phase,qpac_phase,throttle_max,ground_spoilers,zulu_sec,toliss_vol_"
    "internal,toliss_vol_override,door_blend,"
    "strobe,no_smoking,gear_down,event";

size_t columnCount() {
  size_t n = 1;
  for (const char* c = kHeader; *c; ++c)
    if (*c == ',') ++n;
  return n;
}

std::vector<std::string> splitCsv(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  bool quoted = false;
  for (size_t k = 0; k < line.size(); ++k) {
    const char c = line[k];
    if (quoted) {
      if (c == '"' && k + 1 < line.size() && line[k + 1] == '"') {
        cur += '"';
        ++k;
      } else if (c == '"') {
        quoted = false;
      } else {
        cur += c;
      }
    } else if (c == '"') {
      quoted = true;
    } else if (c == ',') {
      out.push_back(cur);
      cur.clear();
    } else if (c != '\r') {
      cur += c;
    }
  }
  out.push_back(cur);
  return out;
}

std::string quote(const std::string& s) {
  if (s.find_first_of(",\"\n") == std::string::npos) return s;
  std::string out = "\"";
  for (char c : s)
    out +=
        (c == '"') ? std::string("\"\"") : std::string(1, c == '\n' ? ' ' : c);
  return out + "\"";
}

}  // namespace

const char* traceHeader() { return kHeader; }

std::string traceLine(const SimState& s) {
  char buf[1024];
  std::snprintf(buf, sizeof(buf),
                "S,%.2f,%d,%d,%d,%.1f,%.1f,%.0f,%.0f,%.0f,%.6f,%.6f,%d,%.0f,%d,"
                "%d,%d,%.2f,%d,%d,%d,%d,%.2f,%.2f,%d,%d,%d,"
                "%.1f,%d,%d,%.0f,%d,%.2f,%d,%.2f,%.2f,%d,%d,%.1f,%.0f,",
                s.t, s.paused, s.replay, s.onGround, s.gsKt, s.iasKt, s.aglFt,
                s.mslFt, s.vsFpm, s.lat, s.lon, s.fmgsPhaseRaw, s.cruiseAltFt,
                s.beacon, s.seatbelt, s.landingLights, s.speedbrake,
                s.parkBrake, s.chocks, s.enginesRunning, s.engineCount,
                s.doorMax, s.doorFwd, s.doorCount, s.slidesArmed, s.pax,
                s.fuelKg, s.acPower, s.paxMasks, s.cabinAltFt, s.paOn, s.paVol,
                s.cabOn, s.cabVol, s.cockpitDoor, s.viewExternal,
                s.cameraInCabin, s.sunPitchDeg, s.localTimeSec);
  char tail[256];
  std::snprintf(tail, sizeof(tail), "%d,%d,%.2f,%d,%.0f,%.2f,%d,%.2f,%d,%d,%d,",
                s.ecamPhase, s.qpacPhase, static_cast<double>(s.throttleMax),
                s.groundSpoilers, s.zuluSec,
                static_cast<double>(s.tolissVolInternal), s.tolissVolOverride,
                static_cast<double>(s.doorBlend), s.strobeOn, s.noSmoking,
                s.gearLeverDown);
  return std::string(buf) + quote(s.flightNumber) + "," + quote(s.acfIcao) +
         "," + (s.satcom ? "1" : "0") + "," + tail;
}

bool Recorder::open(const std::string& path) {
  close();
  std::error_code ec;
  const auto p = std::filesystem::u8path(path);
  std::filesystem::create_directories(p.parent_path(), ec);
#if defined(_WIN32)
  file_ = _wfopen(p.wstring().c_str(), L"w");
#else
  file_ = std::fopen(p.string().c_str(), "w");
#endif
  if (!file_) return false;
  path_ = path;
  lastState_ = -1e9;
  std::fprintf(file_, "%s\n", kHeader);
  return true;
}

void Recorder::close() {
  if (file_) std::fclose(file_);
  file_ = nullptr;
}

void Recorder::state(const SimState& s, double interval) {
  if (!file_ || s.t - lastState_ < interval) return;
  lastState_ = s.t;
  std::fprintf(file_, "%s\n", traceLine(s).c_str());
  std::fflush(file_);
}

void Recorder::event(double t, const std::string& what) {
  if (!file_) return;
  // e row empty state cols text in event
  std::fprintf(file_, "E,%.2f%s%s\n", t,
               std::string(columnCount() - 2, ',').c_str(),
               quote(what).c_str());
  std::fflush(file_);
}

std::vector<TraceRow> readTrace(const std::string& text) {
  std::vector<TraceRow> rows;
  std::map<std::string, size_t> col;
  auto useHeader = [&](const std::string& line) {
    col.clear();
    const std::vector<std::string> names = splitCsv(line);
    for (size_t k = 0; k < names.size(); ++k) col[names[k]] = k;
  };
  useHeader(kHeader);  // no header current layout
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    if (line.empty() || line[0] == '#') continue;
    if (line.rfind("kind,", 0) == 0) {
      useHeader(line);
      continue;
    }
    const std::vector<std::string> c = splitCsv(line);
    if (c.size() < 3 || (c[0] != "S" && c[0] != "E")) continue;
    auto field = [&](const char* name) -> const std::string* {
      const auto it = col.find(name);
      return (it != col.end() && it->second < c.size()) ? &c[it->second]
                                                        : nullptr;
    };
    auto f = [&](const char* name, float def) {
      const std::string* v = field(name);
      return (v && !v->empty()) ? static_cast<float>(std::atof(v->c_str()))
                                : def;
    };
    auto d = [&](const char* name, double def) {
      const std::string* v = field(name);
      return (v && !v->empty()) ? std::atof(v->c_str()) : def;
    };
    auto i = [&](const char* name, int def) {
      const std::string* v = field(name);
      return (v && !v->empty()) ? std::atoi(v->c_str()) : def;
    };
    auto b = [&](const char* name, bool def) {
      return i(name, def ? 1 : 0) != 0;
    };
    auto str = [&](const char* name) {
      const std::string* v = field(name);
      return v ? *v : std::string();
    };
    TraceRow r;
    r.isEvent = c[0] == "E";
    SimState& s = r.state;
    s.t = std::atof(c[1].c_str());
    if (r.isEvent) {
      r.event = c.back();
      rows.push_back(std::move(r));
      continue;
    }
    if (c.size() < 44) continue;  // cut off row
    s.paused = b("paused", false);
    s.replay = b("replay", false);
    s.onGround = b("on_ground", true);
    s.gsKt = f("gs_kt", 0);
    s.iasKt = f("ias_kt", 0);
    s.aglFt = f("agl_ft", 0);
    s.mslFt = f("msl_ft", 0);
    s.vsFpm = f("vs_fpm", 0);
    s.lat = d("lat", 0);
    s.lon = d("lon", 0);
    s.fmgsPhaseRaw = i("fmgs_phase_raw", -1);
    if (s.fmgsPhaseRaw >= 0 && s.fmgsPhaseRaw <= 7)
      s.fmgsPhase = static_cast<Phase>(s.fmgsPhaseRaw);
    s.cruiseAltFt = f("cruise_alt_ft", 0);
    s.beacon = b("beacon", false);
    s.seatbelt = b("seatbelt", false);
    s.landingLights = b("landing_lights", false);
    s.speedbrake = f("speedbrake", 0);
    s.parkBrake = b("park_brake", false);
    s.chocks = b("chocks", false);
    s.enginesRunning = i("engines_running", 0);
    s.engineCount = i("engine_count", 2);
    s.doorMax = f("door_max", 0);
    s.doorFwd = f("door_fwd", 0);
    s.doorCount = i("door_count", 0);
    s.slidesArmed = b("slides_armed", false);
    s.slidesKnown = true;
    s.pax = i("pax", 0);
    s.fuelKg = f("fuel_kg", 0);
    s.acPower = b("ac_power", true);
    s.paxMasks = b("pax_masks", false);
    s.cabinAltFt = f("cabin_alt_ft", 0);
    s.paOn = b("pa_on", false);
    s.paVol = f("pa_vol", 0);
    s.cabOn = b("cab_on", false);
    s.cabVol = f("cab_vol", 0);
    s.cockpitDoor = f("cockpit_door", 0);
    s.viewExternal = b("view_external", false);
    s.cameraInCabin = b("camera_in_cabin", false);
    s.sunPitchDeg = f("sun_pitch", 45);
    s.localTimeSec = f("local_time", 43200);
    s.flightNumber = str("flight_number");
    s.acfIcao = str("acf_icao");
    s.satcom = b("satcom", false);
    s.ecamPhase = i("ecam_phase", -1);
    s.qpacPhase = i("qpac_phase", -1);
    s.throttleMax = f("throttle_max", SimState::kUnknownLever);
    s.groundSpoilers = i("ground_spoilers", -1);
    s.zuluSec = d("zulu_sec", -1);
    s.tolissVolInternal = f("toliss_vol_internal", -1);
    s.tolissVolOverride = i("toliss_vol_override", -1);
    s.doorBlend = f("door_blend", -1);
    s.strobeOn = i("strobe", -1);
    s.noSmoking = i("no_smoking", -1);
    s.gearLeverDown = i("gear_down", -1);
    rows.push_back(std::move(r));
  }
  return rows;
}

}  // namespace fxa
