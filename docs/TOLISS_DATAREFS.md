# ToLiss datarefs used by FXAnnounce

**Scope:** every sim input the purser reads, the encoding the code assumes, and whether that encoding has been
confirmed in the sim.

**How names were found:** from the ToLiss plugin binaries (A320 V1p3, A321 V1p8 (systems "V1.9.1"), A339 V1p2;
the A320 and A321 binaries contain every `AirbusFBW/` and `toliss_airbus/` name below), from the ToLiss Simulation
Manual, and from X-Plane's DataRefs.txt.

**X-Airbus** (FrankLFRS, v1.15, a FlyWithLua library for ToLiss) is a source of names and value meanings since
phase 2, with the owner's OK. Only facts are taken from it (which dataref, what a value means); no code is copied,
and the script itself is not in the repository (it has no license). Rows from it say "X-Airbus" and stay *assumed*
until a probe flight confirms them.

**Runs so far:** Run0 (2026-10-31, ToLiss A320 NEO V1.3.3, Lufthansa livery, gate only; `runs/Run0/`). Run1
(2026-11-01, FXAnnounce 0.2.0, ToLiss A320 NEO V1.3.3, EDDM, SimBrief DLH432, boarding only; `runs/Run1/`). Run2
(2026-11-02, FXAnnounce 0.2.1, ToLiss A320 NEO V1.3.3, full flight EDDM-LFMN, SimBrief DLH432; `runs/Run2/`; times
below are trace seconds).

**Cockpit objects:** the manipulators in `cockpit_INN.obj` (A320 V1p3, A321 V1p8) and `A330-900_cockpit_INN.obj` (A339
V1p2) name the dataref, the index and a tooltip for every ACP knob. Rows marked "cockpit object" come from them.

**Scope:** phase 2 covers the A319/A320/A321. The A339 and A346 rows are kept for a later phase.

**How encodings get verified:** fly with the recorder on (Settings → Advanced → Record flight trace). The trace logs
every change of the raw ToLiss values ("raw" event rows), and the Log tab's *Live inputs* table shows what the purser
reads. For anything the trace doesn't cover, an agent writes step-by-step instructions (below) and the owner runs them
with DataRefTool. Then move each row below from *assumed* to *verified*, noting the observed values. Until a row is
verified, the code treats it defensively: a missing dataref disables only its feature, and indices and thresholds are
settings. (0.2.x had an in-plugin probe checklist on an Inspector tab; 0.3.0 removed it.)

| Input | Dataref | Assumed encoding | Status |
|---|---|---|---|
| FMGS phase | `AirbusFBW/APPhase` | int 0 PreFlight, 1 Takeoff, 2 Climb, 3 Cruise, 4 Descent, 5 Approach, 6 GA, 7 Done | **verified** Run2 except 6: 0 at the gate, 1 at takeoff thrust (3320), 2 at 1,460 ft (3397), 3 at cruise (4524), 4 at descent (6516), 5 approach (7888), 7 after landing at 35 kt (8205), back to 0 at engine shutdown (8463) |
| Seatbelt signs | `AirbusFBW/SeatBeltSignsOn` | nonzero = signs lit | **verified** Run2: follows the switch (`OHPLightSwitches` 11): on at the gate, off in cruise, on in the descent, off at the stand |
| No-smoking signs | `AirbusFBW/NoSmokingSignsOn` | nonzero = lit; a lit → unlit → lit flick within 10 s is the pilots' signal (VP_SPEC § 4.2) | **verified** Run2: lit with the switch at ON or AUTO (`OHPLightSwitches` 12 = 2 or 1, also with the gear up), unlit at OFF; a flick at FL175 read 1 → 0 → 1 with 4.2 s unlit |
| Passenger doors | `AirbusFBW/PaxDoorArray` | float per door, 0 closed … 1 open; 20 entries; index 0 = forward left (L1) | **verified** Run0 (A320neo): L1 1.00 open, ramps to 0.00 over ~4 s when ToLiss AUTO closes it |
| Door modes | `AirbusFBW/PaxDoorModeArray` | per door 0 closed / 1 auto / 2 open (informational; the purser ignores door modes) | **verified** Run1: 1 for all four at load (ISCS AUTO), L1 1 → 2 when set to OPEN; 0 = closed still assumed |
| ToLiss AUTO door timer | `PaxDoorArray`, `CargoDoorArray` | in AUTO, L1 and the cargo doors open at the gate and close on their own after a set time (Simulation Manual § 3.6.4), sooner with the beacon on | Run1: open at load; cargo 2 closed at 12.5 min, L1 at 13.2 min, cargo 1 at 13.6 min, with no beacon. This is why door events no longer complete boarding (VP_SPEC D-1) |
| Slides armed | `AirbusFBW/SlideArmedArray` | nonzero = armed | **verified** Run2: entries 0–3 armed one after another over 2 s by ToLiss itself, 9 s after the doors closed (beacon still off); disarmed by ToLiss 5 s after the beacon went off at the stand |
| Passengers | `AirbusFBW/NoPax` | count | Run0: 121 (plausible; to compare with the ISCS). Run2: 144 for the whole flight, 130 after deboarding at the stand (a new load) |
| Pax oxygen masks | `AirbusFBW/PaxOxyMask` | nonzero = deployed | assumed |
| Cabin altitude | `AirbusFBW/CabinAlt` | ft (informational) | assumed |
| AC power | `AirbusFBW/ACBusVoltages` | volts per AC bus; powered if any > 90 V | **verified** Run0: unpowered at load, powered when batteries and external power came on (battery-only not yet seen separately) |
| ACP knobs pulled | `AirbusFBW/ACP{1,2,3}KnobPush` | 16 entries, nonzero = reception on: 0 VHF1, 1 VHF2, 2 VHF3, 3 HF1, 4 HF2, 5 INT, 6 CAB, 7 ILS (LS), 8 MKR, 9 VOR1, 10 VOR2, 11 ADF1, 12 ADF2, 13 MLS (A339: SAT1), 14 (A339: SAT2), 15 PA. Settings `acp_pa_index` (15), `acp_cab_index` (6) | **verified** (cockpit object, all three models) and Run1: the probe step saw ACP1 index 15 move for the PA knob; index 6 for CAB. Indices 5, 6 and 8 changed on all three ACPs at once in Run1, index 15 on ACP1 only |
| ACP volumes (A319/A320/A321) | `ckpt/oh/{pa,cab,int,vhf1,…}/{1,2,3}/anim` | knob rotation in degrees, 0 … 270 (the manipulator range); FXAnnounce reads `pa` and `cab` and divides by 270 | names and range from the cockpit object; values assumed until a knob turn is seen ("Verifying an encoding by hand") |
| ACP volumes (A339, fallback) | `AirbusFBW/ACP{1,2,3}RotaryPositions` | per channel 0 … 1, same index as `KnobPush` | A339: the cockpit object's volume manipulators write it. A320 Run1: stayed 0.80 for every entry the whole session, so the A320/A321 knobs don't drive it |
| Cockpit door | `AirbusFBW/CockpitDoorAngle`, `ckpt/door` | degrees; normalized by 80° (fully open) to 0 … 1 | **verified** Run1 (A320neo): `ckpt/door` 0 → 80.00 open, 60.07 on the way back, 0 closed; `CockpitDoorAngle` stayed 0 |
| SatCom antenna | `AirbusFBW/SatComObjInhibit` | 0 = antenna shown (fitted), 1 = hidden | Run1: 1 at load, toggled 0/1 with the ISCS setting (which value is "fitted" still to be confirmed against the exterior model) |
| Flight number | `toliss_airbus/init/flight_no` | byte string ("DLH400") | **verified** Run1: "" until INIT A, then "DLH432" |
| Cruise altitude | `toliss_airbus/init/cruise_alt` | ft | **verified** Run1: 0, then 34000 with INIT A |
| Parking brake | `AirbusFBW/ParkBrake` | nonzero = set | Run0: 1 at the gate |
| Chocks | `AirbusFBW/Chocks` | nonzero = in place | Run0: 1 at the gate |
| ToLiss marker | `AirbusFBW/APPhase` or `toliss_airbus/performance/V1` (X-Airbus uses the latter) | exists = ToLiss systems plugin loaded | both present in the A320/A321 binaries |
| Aircraft ICAO | `sim/aircraft/view/acf_ICAO` | may change at runtime with the ToLiss engine option (A321↔A21N, A320↔A20N): re-read every few seconds | Run0: `A20N` on the A320 NEO V1.3.3 |
| Beacon | `sim/cockpit2/switches/beacon_on` | 0/1 | **verified** Run0: follows the ToLiss beacon switch (ArmDoors fired on it) |
| Landing lights | `sim/cockpit2/switches/landing_lights_on` | 0/1 | assumed |
| Speedbrakes | `sim/cockpit2/controls/speedbrake_ratio` | ≤ 0.01 = stowed (negative = armed) | X-Plane standard |
| Engines | `sim/flightmodel/engine/ENGN_running`, `sim/aircraft/engine/acf_num_engines` | int per engine | X-Plane standard |
| Ground / speeds / heights | `sim/flightmodel/failures/onground_any`, `.../position/groundspeed` (m/s), `indicated_airspeed` (kt), `y_agl` (m), `elevation` (m), `vh_ind_fpm` | | X-Plane standard |
| Fuel | `sim/flightmodel/weight/m_fuel_total` | kg | X-Plane standard |
| Time and sun | `sim/time/local_time_sec`, `sim/graphics/scenery/sun_pitch_degrees` | s, deg | X-Plane standard |
| Pause / replay | `sim/time/paused`, `sim/time/is_in_replay` | 0/1 | X-Plane standard |
| Camera | `sim/graphics/view/view_is_external`, `sim/graphics/view/pilots_head_z`, `sim/aircraft/view/acf_peZ` | m; "in the cabin" = head more than `cabin_view_aft_m` aft of the default eye point | X-Plane standard |
| Position | `sim/flightmodel/position/latitude`, `longitude` | deg (teleport detection, OFP origin check) | X-Plane standard |
| Zulu time | `sim/time/zulu_time_sec` | s since 00:00 UTC (boarding schedule) | X-Plane standard |
| ECAM flight phase | `AirbusFBW/ECAMFlightPhase` | int (X-Airbus): 1 elec pwr, 2 1st engine started, 3 1st engine T.O. power, 4 80 kt, 5 lift-off, 6 1500 ft, 7 800 ft, 8 touchdown, 9 80 kt, 10 2nd engine shut down, back to 1 five minutes later | **verified** Run2 for 1–10: 2 as the first engine started (2527), 3 at takeoff thrust (3320), 4 at 77 kt, 5 at lift-off, 6 at 1,500 ft, 7 at 800 ft on the approach, 8 at touchdown, 9 below 80 kt, 10 at shutdown |
| QPAC flight phase | `AirbusFBW/QPACFlightPhase` | int (X-Airbus): 1 elec pwr, 2 1st engine started, 3 taxi to runway, 4 1st engine T.O. power, 5 80 kt, 6 lift-off, 7 1500 ft, 8 flaps up, 9 2000 ft (approach), 10 800 ft, 11 touchdown, 12 80 kt, 13 taxi to gate, 14 2nd engine shut down. X-Airbus lists "4" twice (T.O. power and 80 kt); 5 for 80 kt is our reading | **verified** Run2 for 1–14 (5 at 77 kt confirms our reading; 3 came with the second engine running, before taxi; 13 at 37 kt after landing) |
| Thrust levers | `AirbusFBW/throttle_input` | float array, per lever (X-Airbus reads index 4): 1.0 TOGA, 0.88 FLX/MCT, 0.7 CL, 0 idle, negative = reverse. FXAnnounce takes the highest of entries 0…4 | assumed |
| Ground spoilers | `AirbusFBW/SDSpoilerArray` | ECAM status per panel, entries 0–9 (spoilers 1–5 left/right interleaved): 0 retracted, 1 extended, 2 not available (no hydraulics); 10–15 always 2. Ground spoilers out = entries 0 and 1 both 1 (X-Airbus); 2 there = unknown | **verified** Run2: all 2 when unpowered; 0 with hydraulics; roll spoilers show 1 on entries 2–9 only (flight-control check, turns); all of 0–9 at 1 from touchdown (8175) to retraction (8203). 0.2.1 read "any entry > 0.05" and so always saw the spoilers out, which left AfterLanding to its fallback |
| Speedbrake armed | `sim/cockpit2/controls/speedbrake_ratio` | −0.5 = armed (X-Airbus) | X-Plane standard |
| Gear lever | `AirbusFBW/GearLever` | 1 down, 0 up (X-Airbus); down is a seats-for-landing signal (VP_SPEC § 4.1) | **verified** Run2: 1 at the gate, 0 four seconds after lift-off, 1 on the approach (7982, 3 min before touchdown) |
| Flap lever | `AirbusFBW/FlapLeverRatio` | 0 … 1, × 4 = detent 0/1/2/3/FULL (X-Airbus; informational) | **verified** Run2: 0.25 (1+F) for takeoff, 0.25 / 0.5 / 0.75 / 1.0 on the approach |
| ToLiss VS | `toliss_airbus/pfdoutputs/captain/vertical_speed` | ft/min (X-Airbus; informational) | assumed |
| Cargo doors | `AirbusFBW/CargoDoorArray` | per door, 0 closed … 1 open (X-Airbus; informational) | Run1: entries 0 and 1 open (1.00) at load, each ramped to 0 over ~15 s when ToLiss AUTO closed it |
| Distance to destination | `AirbusFBW/DistToDest` | NM (X-Airbus; informational) | assumed |
| Landing lights (candidates) | `sim/cockpit2/switches/landing_lights_on`, `sim/cockpit2/switches/landing_lights_switch`, `AirbusFBW/LeftLandLightExtended`, `AirbusFBW/RightLandLightExtended` | `[toliss] landing_light_source` picks the one that follows the switches (default the first) | **verified** Run2 for the default: `landing_lights_on` reads 1 only with a switch at ON (`OHPLightSwitches` 4/5 = 2); extended and OFF (1) reads 0. `*LandLightExtended` ramp 0 → 1 over 10 s as the lights extend |
| Overhead light switches | `AirbusFBW/OHPLightSwitches` | switch positions: 0 beacon (0/1), 1 wing, 2 nav & logo, 3 nose (0 off, 1 taxi, 2 T.O.), 4 / 5 landing left / right (0 retract, 1 off, 2 on), 6 runway turnoff, 7 strobe (0 off, 1 auto, 2 on), 8 dome (0 off, 1 dim, 2 bright), 11 seatbelt (0/1), 12 no smoking (0 off, 1 auto, 2 on). FXAnnounce reads the strobe (`[toliss] strobe_switch_index`, 7) | **verified** Run2 (A320neo) for these indices; 10 changed to 1 with the first overhead set-up (meaning unknown). A319/A321 assumed the same |
| ToLiss sound mix | `AirbusFBW/fmod/vol/Internal`, `cockpit`, `spkrs` | 0 … 1 (names from the binaries) | assumed |
| ToLiss volume override | unknown | the ISCS "Ovrd XP Int/Ext Vol" switch; set by hand in `[toliss] volume_override_ref` once found ("Verifying an encoding by hand") | unknown |
| Cockpit door sound blend | `AirbusFBW/fmod/vol/FwdCockpitDoorBlend` | 0 … 1: 1 when the cockpit door is open or the camera is aft of it | Run1: steps 0 ↔ 1 only; 1 with the door open (`ckpt/door` 80) and also with the door shut while the camera was in the cabin |
| Fuel loads | `sim/flightmodel/weight/m_fuel_total` | kg | Run1: two instant ISCS loads (12085 → 5171 kg, 5171 → 6350 kg) were not taken for refuelling, as intended |

## Commands observed (pass-through handlers, never run by FXAnnounce)
| Command | Meaning | Status |
|---|---|---|
| `AirbusFBW/purser/fwd`, `aft` (all models), `all`, `mid`, `exit` (A321, A339) | cabin call buttons on the overhead CALLS panel | **verified** Run2 (A320neo): every click on FWD ran `AirbusFBW/purser/fwd` (five presses logged). The A320neo has only `fwd` and `aft`. ToLiss's command `AirbusFBW/CheckCabin` is described in the binary as "Press any of the purser buttons": the call buttons are what set ToLiss's cabin-checked flag (the T.O. memo's CABIN READY) |
| `AirbusFBW/purser/mech` (all models) | mechanic call (ignored by the purser) | assumed |

## simbrief_hub (optional companion plugin, read-only)
simbrief_hub (hotbso, LGPL-2.1, v2.3.3 on this machine) downloads the SimBrief OFP and publishes it. FXAnnounce reads:

| Dataref | Type | Content |
|---|---|---|
| `sbh/seqno` | int | +1 after each successful download; 0 = no OFP yet |
| `sbh/stale` | int | 1 = the last download failed, the data may be old |
| `sbh/est_out` | bytes | OFP `times.est_out`: UTC epoch seconds as text (the ETD) |
| `sbh/icao_airline`, `sbh/flight_number` | bytes | `general.icao_airline` ("DLH") and `general.flight_number` ("400") |
| `sbh/origin`, `sbh/destination` | bytes | ICAO codes |
| `sbh/pax_count`, `sbh/max_passengers` | bytes | passengers planned, seats of the airframe (text numbers) |
| `sbh/aircraft_icao` | bytes | OFP aircraft type (logged when it doesn't match) |

The byte datarefs return no data until the first download. They are not NUL-terminated; `DataRef::getString` handles
that.

## Commands and datarefs noted for the Virtual Purser stage (not used yet)
| Name | Effect (per its name and the Simulation Manual) |
|---|---|
| `toliss_airbus/door_commands/arm_slides`, `disarm_slides` | arm or disarm the door slides |
| `toliss_airbus/door_commands/all_to_mode_auto`, `all_to_mode_close`, `all_to_mode_open` | door modes (ISCS AUTO/CLOSED/OPEN) |
| `AirbusFBW/CheckCabin` | command, "Press any of the purser buttons" (binary): sets ToLiss's cabin-checked flag. No dataref exposes the flag; the E/WD memo text is in `AirbusFBW/EWD{n}{a,b,g,r,w}Text` |
| `AirbusFBW/CabinLightLevel`, `cabinLightLevel_perc` | cabin lighting (IACP Cabin Lighting tab) |
| `AirbusFBW/fmod/env/cabinChime` | ToLiss's cabin chime |

ToLiss already does some cabin-crew work by itself: AUTO doors open L1 and the cargo doors at the gate and close after
a set time (sooner with the beacon on), the slides are disarmed after landing once the engines and beacon are off,
and the cabin lights go off for takeoff and landing.

## Verifying an encoding by hand
With [DataRefTool](https://github.com/leecbaker/datareftool) installed:

1. Load the ToLiss aircraft, open DataRefTool's search window, and filter on the prefix (`AirbusFBW/`,
   `toliss_airbus/`, `ckpt/`).
2. Tick *Show only changed* (or watch the highlighted rows), do the single cockpit action, and note every dataref that
   changed, with its value before and after.
3. Repeat the action once to make sure the same dataref follows it.
4. Put the finding in the table above. If a setting depends on it (`[toliss]` in `FXAnnounce.ini`, also on the
   Settings → ToLiss page), set it there.

Still open:
- **Sound mix:** toggle ISCS "Ovrd XP Int/Ext Vol". The dataref that changes is `[toliss] volume_override_ref`
  (Settings → ToLiss → Volume override dataref). Then move the ISCS internal slider: `AirbusFBW/fmod/vol/Internal`
  should follow.
- **Landing lights** on the A319/A321: switch the landing lights and check which candidate follows
  (`[toliss] landing_light_source`: 0 `landing_lights_on`, 1 `landing_lights_switch`, 2 ToLiss left/right extended).
- **ACP volumes:** turn the PA and CAB volume knobs: `ckpt/oh/pa/1/anim` and `ckpt/oh/cab/1/anim` should run 0–270.
- **Masks:** pax oxygen MASK MAN ON: `AirbusFBW/PaxOxyMask`.
- **Go-around:** the `APPhase` value during a go-around (assumed 6).
