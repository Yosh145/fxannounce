// toliss xp datarefs into simstate read only toward toliss
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/config.h"
#include "core/sim_state.h"
#include "sim/dataref.h"

namespace fxa {

class TolissAdapter {
 public:
  TolissAdapter();
  TolissAdapter(const TolissAdapter&) = delete;  // refs_ points into this
  TolissAdapter& operator=(const TolissAdapter&) = delete;

  // activation and plane load logs whats missing
  void resolve();
  // dt added to the purser clock unless paused
  SimState read(const Settings& s, float dt);
  // toliss inputs rounded to 2 dp for the traces raw change log drifting stuff
  // cabin alt left out
  std::vector<std::pair<std::string, std::string>> rawSnapshot() const;

 private:
  std::vector<float> landingCandidates() const;
  void useOverrideRef(const std::string& name);

  double clock_ = 0;
  float icaoTimer_ = 0;
  std::string icao_;
  std::string overrideName_;

  // x-plane
  DataRef onGround_{"sim/flightmodel/failures/onground_any"};
  DataRef gs_{"sim/flightmodel/position/groundspeed"};
  DataRef ias_{"sim/flightmodel/position/indicated_airspeed"};
  DataRef agl_{"sim/flightmodel/position/y_agl"};
  DataRef msl_{"sim/flightmodel/position/elevation"};
  DataRef vs_{"sim/flightmodel/position/vh_ind_fpm"};
  DataRef lat_{"sim/flightmodel/position/latitude"};
  DataRef lon_{"sim/flightmodel/position/longitude"};
  DataRef beacon_{"sim/cockpit2/switches/beacon_on"};
  DataRef landing_{"sim/cockpit2/switches/landing_lights_on"};
  DataRef speedbrake_{"sim/cockpit2/controls/speedbrake_ratio"};
  DataRef engRunning_{"sim/flightmodel/engine/ENGN_running"};
  DataRef engCount_{"sim/aircraft/engine/acf_num_engines"};
  DataRef fuel_{"sim/flightmodel/weight/m_fuel_total"};
  DataRef localTime_{"sim/time/local_time_sec"};
  DataRef sun_{"sim/graphics/scenery/sun_pitch_degrees"};
  DataRef paused_{"sim/time/paused"};
  DataRef replay_{"sim/time/is_in_replay"};
  DataRef external_{"sim/graphics/view/view_is_external"};
  DataRef headZ_{"sim/graphics/view/pilots_head_z"};
  DataRef eyeZ_{"sim/aircraft/view/acf_peZ"};
  DataRef icao_ref_{"sim/aircraft/view/acf_ICAO"};

  // toliss
  DataRef apPhase_{"AirbusFBW/APPhase"};
  DataRef seatbelt_{"AirbusFBW/SeatBeltSignsOn"};
  DataRef noSmoking_{"AirbusFBW/NoSmokingSignsOn"};
  DataRef paxDoors_{"AirbusFBW/PaxDoorArray"};
  DataRef paxDoorModes_{"AirbusFBW/PaxDoorModeArray"};
  DataRef slides_{"AirbusFBW/SlideArmedArray"};
  DataRef noPax_{"AirbusFBW/NoPax"};
  DataRef masks_{"AirbusFBW/PaxOxyMask"};
  DataRef cabinAlt_{"AirbusFBW/CabinAlt"};
  DataRef acVolts_{"AirbusFBW/ACBusVoltages"};
  DataRef acpKnob_[3] = {DataRef("AirbusFBW/ACP1KnobPush"),
                         DataRef("AirbusFBW/ACP2KnobPush"),
                         DataRef("AirbusFBW/ACP3KnobPush")};
  DataRef acpVol_[3] = {DataRef("AirbusFBW/ACP1RotaryPositions"),
                        DataRef("AirbusFBW/ACP2RotaryPositions"),
                        DataRef("AirbusFBW/ACP3RotaryPositions")};
  // a319/a320/a321 knobs 0-270 deg from the cockpit obj rotarypositions
  // fallback a339
  DataRef acpPaKnob_[3] = {DataRef("ckpt/oh/pa/1/anim"),
                           DataRef("ckpt/oh/pa/2/anim"),
                           DataRef("ckpt/oh/pa/3/anim")};
  DataRef acpCabKnob_[3] = {DataRef("ckpt/oh/cab/1/anim"),
                            DataRef("ckpt/oh/cab/2/anim"),
                            DataRef("ckpt/oh/cab/3/anim")};
  DataRef doorAngle_{"AirbusFBW/CockpitDoorAngle"};
  DataRef ckptDoor_{"ckpt/door"};
  DataRef satcomInhibit_{"AirbusFBW/SatComObjInhibit"};
  DataRef flightNo_{"toliss_airbus/init/flight_no"};
  DataRef cruiseAlt_{"toliss_airbus/init/cruise_alt"};
  DataRef parkBrake_{"AirbusFBW/ParkBrake"};
  DataRef chocks_{"AirbusFBW/Chocks"};
  DataRef ohpLights_{"AirbusFBW/OHPLightSwitches"};  // strobe idx verified run2

  // cross checks names from the binaries x-airbus
  DataRef ecam_{"AirbusFBW/ECAMFlightPhase"};
  DataRef qpac_{"AirbusFBW/QPACFlightPhase"};
  DataRef throttle_{"AirbusFBW/throttle_input"};
  DataRef sdSpoilers_{"AirbusFBW/SDSpoilerArray"};
  DataRef gearLever_{"AirbusFBW/GearLever"};
  DataRef flapLever_{"AirbusFBW/FlapLeverRatio"};
  DataRef tolissVs_{"toliss_airbus/pfdoutputs/captain/vertical_speed"};
  DataRef cargoDoors_{"AirbusFBW/CargoDoorArray"};
  DataRef distToDest_{"AirbusFBW/DistToDest"};
  DataRef zulu_{"sim/time/zulu_time_sec"};
  // landing light candidates
  DataRef landingSwitch_{"sim/cockpit2/switches/landing_lights_switch"};
  DataRef leftLand_{"AirbusFBW/LeftLandLightExtended"};
  DataRef rightLand_{"AirbusFBW/RightLandLightExtended"};
  // sound mix
  DataRef volInternal_{"AirbusFBW/fmod/vol/Internal"};
  DataRef volCockpit_{"AirbusFBW/fmod/vol/cockpit"};  // trace only
  DataRef volSpkrs_{"AirbusFBW/fmod/vol/spkrs"};      // trace only
  DataRef doorBlend_{"AirbusFBW/fmod/vol/FwdCockpitDoorBlend"};
  DataRef volOverride_;

  std::vector<DataRef*> refs_;  // for resolve and rawsnapshot
};

// fmgs phase to phase still assumed in places
std::optional<Phase> tolissPhase(int raw);

}  // namespace fxa
