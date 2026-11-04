#include "sim/toliss_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "audio/hearing.h"
#include "core/log.h"
#include "core/toliss_decode.h"

namespace fxa {

namespace {

constexpr float kMsToKt = 1.943844f;
constexpr float kMToFt = 3.280840f;
constexpr float kIcaoRefresh = 5.0f;  // toliss can change acf_icao after load

float clamp01(float v) {
  return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

bool truthy(const DataRef& r) {
  const auto v = r.getFloat();
  return v && std::fabs(*v) > 0.01f;
}

}  // namespace

std::optional<Phase> tolissPhase(int raw) {
  if (raw < 0 || raw > 7) return std::nullopt;
  return static_cast<Phase>(raw);
}

TolissAdapter::TolissAdapter() {
  refs_ = {&onGround_,   &gs_,          &ias_,           &agl_,
           &msl_,        &vs_,          &lat_,           &lon_,
           &beacon_,     &landing_,     &speedbrake_,    &engRunning_,
           &engCount_,   &fuel_,        &localTime_,     &sun_,
           &paused_,     &replay_,      &external_,      &headZ_,
           &eyeZ_,       &icao_ref_,    &apPhase_,       &seatbelt_,
           &noSmoking_,  &paxDoors_,    &paxDoorModes_,  &slides_,
           &noPax_,      &masks_,       &cabinAlt_,      &acVolts_,
           &doorAngle_,  &ckptDoor_,    &satcomInhibit_, &flightNo_,
           &cruiseAlt_,  &parkBrake_,   &chocks_,        &ohpLights_,
           &ecam_,       &qpac_,        &throttle_,      &sdSpoilers_,
           &gearLever_,  &flapLever_,   &tolissVs_,      &cargoDoors_,
           &distToDest_, &zulu_,        &landingSwitch_, &leftLand_,
           &rightLand_,  &volInternal_, &volCockpit_,    &volSpkrs_,
           &doorBlend_};
  for (auto& r : acpKnob_) refs_.push_back(&r);
  for (auto& r : acpVol_) refs_.push_back(&r);
  for (auto& r : acpPaKnob_) refs_.push_back(&r);
  for (auto& r : acpCabKnob_) refs_.push_back(&r);
}

void TolissAdapter::resolve() {
  std::string missing;
  for (DataRef* r : refs_)
    if (!r->resolve())
      missing += std::string(missing.empty() ? "" : ", ") + r->name();
  if (!missing.empty())
    logWarn("datarefs not available (features that need them are off): %s",
            missing.c_str());
  icaoTimer_ = kIcaoRefresh;  // first tick
  overrideName_.clear();
  volOverride_ = DataRef();
}

void TolissAdapter::useOverrideRef(const std::string& name) {
  // once per name again after resolve on plane load
  if (name == overrideName_) return;
  overrideName_ = name;
  volOverride_ = name.empty() ? DataRef() : DataRef(overrideName_.c_str());
  if (!name.empty() && !volOverride_.resolve())
    logWarn("volume override dataref %s not found", name.c_str());
}

std::vector<float> TolissAdapter::landingCandidates() const {
  auto anyOn = [](const std::vector<float>& v) {
    return std::any_of(v.begin(), v.end(), [](float x) { return x > 0.5f; });
  };
  std::vector<float> c(3, -1.0f);
  if (landing_.good()) c[0] = truthy(landing_) ? 1.0f : 0.0f;
  if (landingSwitch_.good())
    c[1] = anyOn(landingSwitch_.getArray(16)) ? 1.0f : 0.0f;
  if (leftLand_.good() || rightLand_.good())
    c[2] = (leftLand_.getFloat().value_or(0) > 0.5f ||
            rightLand_.getFloat().value_or(0) > 0.5f)
               ? 1.0f
               : 0.0f;
  return c;
}

SimState TolissAdapter::read(const Settings& set, float dt) {
  SimState s;
  s.paused = truthy(paused_);
  s.replay = truthy(replay_);
  if (!s.paused && std::isfinite(dt) && dt > 0 && dt < 10) clock_ += dt;
  s.t = clock_;

  s.onGround = onGround_.good() ? truthy(onGround_) : true;
  s.gsKt = gs_.getFloat().value_or(0) * kMsToKt;
  s.iasKt = ias_.getFloat().value_or(0);
  s.aglFt = agl_.getFloat().value_or(0) * kMToFt;
  s.mslFt = msl_.getFloat().value_or(0) * kMToFt;
  s.vsFpm = vs_.getFloat().value_or(0);
  s.lat = lat_.getDouble().value_or(0);
  s.lon = lon_.getDouble().value_or(0);

  if (auto p = apPhase_.getInt()) {
    s.fmgsPhaseRaw = *p;
    s.fmgsPhase = tolissPhase(*p);
  }
  s.cruiseAltFt = cruiseAlt_.getFloat().value_or(0);

  s.beacon = truthy(beacon_);
  s.seatbelt = truthy(seatbelt_);
  if (noSmoking_.good()) s.noSmoking = truthy(noSmoking_) ? 1 : 0;
  // strobe on 2 not auto 1 lineup signal 4.2
  if (ohpLights_.good())
    s.strobeOn = switchAt(ohpLights_.getArray(32), set.strobeSwitchIndex, 2);
  {
    const std::vector<float> c = landingCandidates();
    const size_t pick = static_cast<size_t>(
        std::clamp(set.landingLightSource, 0, static_cast<int>(c.size()) - 1));
    s.landingLights = c[pick] >= 0 ? c[pick] > 0.5f : c[0] > 0.5f;
  }
  s.speedbrake = speedbrake_.getFloat().value_or(0);
  s.parkBrake = truthy(parkBrake_);
  s.chocks = truthy(chocks_);

  s.engineCount = std::clamp(engCount_.getInt().value_or(2), 1, 8);
  const std::vector<float> eng = engRunning_.getArray(8);
  for (int k = 0;
       k < std::min<int>(s.engineCount, static_cast<int>(eng.size())); ++k)
    if (eng[static_cast<size_t>(k)] > 0.5f) ++s.enginesRunning;

  const std::vector<float> doors = paxDoors_.getArray(20);
  s.doorCount = static_cast<int>(doors.size());
  for (size_t k = 0; k < doors.size(); ++k) {
    const float d = clamp01(doors[k]);
    s.doorMax = std::max(s.doorMax, d);
    if (k == 0) s.doorFwd = d;
  }
  const std::vector<float> slides = slides_.getArray(20);
  s.slidesKnown = !slides.empty();
  s.slidesArmed = std::any_of(slides.begin(), slides.end(),
                              [](float v) { return std::fabs(v) > 0.01f; });

  s.pax = std::max(0, noPax_.getInt().value_or(0));
  s.fuelKg = fuel_.getFloat().value_or(0);
  if (acVolts_.good()) {
    const std::vector<float> v = acVolts_.getArray(8);
    s.acPower = v.empty() || std::any_of(v.begin(), v.end(),
                                         [](float x) { return x > 90.0f; });
  }
  s.paxMasks = truthy(masks_);
  s.cabinAltFt = cabinAlt_.getFloat().value_or(0);

  for (int a = 0; a < 3; ++a) {
    const std::vector<float> knobs = acpKnob_[a].getArray(16);
    const std::vector<float> vols = acpVol_[a].getArray(16);
    auto at = [](const std::vector<float>& v, int i) {
      return (i >= 0 && i < static_cast<int>(v.size()))
                 ? v[static_cast<size_t>(i)]
                 : 0.0f;
    };
    // knob rotation where theres a knob else rotarypositions
    auto volume = [&](const DataRef& knob, int index) {
      if (knob.good())
        if (auto v = knob.getFloat(); v && std::isfinite(*v))
          return acpKnobVolume(*v);
      return clamp01(at(vols, index));
    };
    if (std::fabs(at(knobs, set.acpPaIndex)) > 0.01f) {
      s.paOn = true;
      s.paVol = std::max(s.paVol, volume(acpPaKnob_[a], set.acpPaIndex));
    }
    if (std::fabs(at(knobs, set.acpCabIndex)) > 0.01f) {
      s.cabOn = true;
      s.cabVol = std::max(s.cabVol, volume(acpCabKnob_[a], set.acpCabIndex));
    }
  }
  // ckpt/door is 0-80 deg on the a320neo run1 cockpitdoorangle stays 0 there
  float door = 0;
  if (auto a = doorAngle_.getFloat())
    door = std::max(door, *a > 1.5f ? *a / 80.0f : *a);
  if (auto c = ckptDoor_.getFloat())
    door = std::max(door, *c > 1.5f ? *c / 80.0f : *c);
  s.cockpitDoor = clamp01(door);

  s.viewExternal = truthy(external_);
  if (!s.viewExternal && headZ_.good() && eyeZ_.good())
    s.cameraInCabin =
        headZ_.getFloat().value_or(0) - eyeZ_.getFloat().value_or(0) >
        set.cabinViewAftM;
  s.sunPitchDeg = sun_.getFloat().value_or(45);
  s.localTimeSec = localTime_.getFloat().value_or(43200);

  s.flightNumber = flightNo_.getString(16);
  if (auto inh = satcomInhibit_.getInt()) s.satcom = *inh == 0;

  // cross checks 4.2
  if (auto v = ecam_.getInt()) s.ecamPhase = *v;
  if (auto v = qpac_.getInt()) s.qpacPhase = *v;
  if (throttle_.good()) {
    // x-airbus reads entry 4 max of the first five covers both
    const std::vector<float> t = throttle_.getArray(5);
    for (float x : t)
      if (std::isfinite(x)) s.throttleMax = std::max(s.throttleMax, x);
  }
  // ecam status codes not deflections 0 in 1 out 2 no hydraulics run2
  if (sdSpoilers_.good())
    s.groundSpoilers = groundSpoilersFromSd(sdSpoilers_.getArray(16));
  if (auto v = gearLever_.getInt()) s.gearLeverDown = *v != 0 ? 1 : 0;
  if (auto v = flapLever_.getFloat()) s.flapLever = clamp01(*v);
  s.tolissVsFpm = tolissVs_.getFloat().value_or(0);
  for (float x : cargoDoors_.getArray(8))
    s.cargoDoorMax = std::max(s.cargoDoorMax, clamp01(x));
  if (auto v = distToDest_.getFloat()) s.distToDestNm = *v;
  if (auto z = zulu_.getFloat(); z && std::isfinite(*z)) s.zuluSec = *z;

  // 4.5
  if (auto v = volInternal_.getFloat(); v && std::isfinite(*v))
    s.tolissVolInternal = clamp01(*v);
  if (auto v = doorBlend_.getFloat(); v && std::isfinite(*v))
    s.doorBlend = clamp01(*v);
  useOverrideRef(set.volumeOverrideRef);
  if (volOverride_.good()) s.tolissVolOverride = truthy(volOverride_) ? 1 : 0;

  icaoTimer_ += std::max(0.0f, dt);
  if (icaoTimer_ >= kIcaoRefresh) {
    icaoTimer_ = 0;
    icao_ = icao_ref_.getString(40);
  }
  s.acfIcao = icao_;
  return s;
}

std::vector<std::pair<std::string, std::string>> TolissAdapter::rawSnapshot()
    const {
  std::vector<std::pair<std::string, std::string>> rows;
  for (const DataRef* r : refs_) {
    const std::string name = r->name();
    const bool toliss = name.rfind("AirbusFBW/", 0) == 0 ||
                        name.rfind("ckpt/", 0) == 0 ||
                        name.rfind("toliss_airbus/", 0) == 0;
    // drifting values would flood the log
    if (!toliss || name == "AirbusFBW/CabinAlt" ||
        name == "AirbusFBW/DistToDest" ||
        name == "toliss_airbus/pfdoutputs/captain/vertical_speed")
      continue;
    std::string v;
    char buf[32];
    if (!r->good())
      v = "(missing)";
    else if (r->types() & xplmType_Data)
      v = "\"" + r->getString(64) + "\"";
    else if (r->arraySize() > 0) {
      for (float x : r->getArray(24)) {
        std::snprintf(buf, sizeof(buf), "%s%.2f", v.empty() ? "" : " ",
                      static_cast<double>(x));
        v += buf;
      }
    } else if (auto f = r->getFloat()) {
      std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(*f));
      v = buf;
    }
    rows.emplace_back(name, v);
  }
  return rows;
}

}  // namespace fxa
