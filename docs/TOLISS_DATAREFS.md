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

**Runs so far:** Run0 (2026-10-31, ToLiss A320 NEO V1.3.3, Lufthansa livery, gate only; `runs/Run0/`).

**Scope:** phase 2 covers the A319/A320/A321. The A339 and A346 rows are kept for a later phase.

**How encodings get verified:** fly the probe session with the Inspector tab's Probe section open and the recorder
on (`[general] recorder = yes`). The probe checklist ticks items as it sees them, and "Write probe summary" puts the
findings into `FXAnnounce.log`. Then move each row below from *assumed* to *verified*, noting the observed values.
The trace also logs every change of the raw ToLiss values ("raw" event rows). Until a row is verified, the code
treats it defensively: a missing dataref disables only its feature, and indices and thresholds are settings.

| Input | Dataref | Assumed encoding | Status |
|---|---|---|---|
| FMGS phase | `AirbusFBW/APPhase` | int 0 PreFlight, 1 Takeoff, 2 Climb, 3 Cruise, 4 Descent, 5 Approach, 6 GA, 7 Done | 0 at the gate seen in Run0; values 1–7 still assumed |
| Seatbelt signs | `AirbusFBW/SeatBeltSignsOn` | nonzero = signs lit | Run0: 0 → 1 → 0 while at the gate (owner to confirm it matched the switch) |
| No-smoking signs | `AirbusFBW/NoSmokingSignsOn` | nonzero = lit (informational) | assumed |
| Passenger doors | `AirbusFBW/PaxDoorArray` | float per door, 0 closed … 1 open; 20 entries; index 0 = forward left (L1) | **verified** Run0 (A320neo): L1 1.00 open, ramps to 0.00 over ~4 s when ToLiss AUTO closes it |
| Door modes | `AirbusFBW/PaxDoorModeArray` | per door 0 closed / 1 auto / 2 open (informational) | assumed |
| Slides armed | `AirbusFBW/SlideArmedArray` | nonzero = armed | assumed |
| Passengers | `AirbusFBW/NoPax` | count | Run0: 121 (plausible; to compare with the ISCS) |
| Pax oxygen masks | `AirbusFBW/PaxOxyMask` | nonzero = deployed | assumed |
| Cabin altitude | `AirbusFBW/CabinAlt` | ft (informational) | assumed |
| AC power | `AirbusFBW/ACBusVoltages` | volts per AC bus; powered if any > 90 V | **verified** Run0: unpowered at load, powered when batteries and external power came on (battery-only not yet seen separately) |
| ACP knobs pulled | `AirbusFBW/ACP{1,2,3}KnobPush` | per channel; nonzero = reception on; PA index `acp_pa_index` (7), CAB index `acp_cab_index` (6) (settings) | Run0: index 6 toggled several times with volume 0.80; which knob the owner pulled is to be confirmed (0.1.1 traces log the raw arrays) |
| ACP volumes | `AirbusFBW/ACP{1,2,3}RotaryPositions` | per channel 0 … 1 | assumed |
| Cockpit door | `AirbusFBW/CockpitDoorAngle`, `ckpt/door` | degrees (0 … ~90) or ratio (0 … 1); normalized to 0 … 1 | assumed |
| SatCom antenna | `AirbusFBW/SatComObjInhibit` | 0 = antenna shown (fitted), 1 = hidden | assumed |
| Flight number | `toliss_airbus/init/flight_no` | byte string ("DLH400") | assumed |
| Cruise altitude | `toliss_airbus/init/cruise_alt` | ft | assumed |
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
| ECAM flight phase | `AirbusFBW/ECAMFlightPhase` | int (X-Airbus): 1 elec pwr, 2 1st engine started, 3 1st engine T.O. power, 4 80 kt, 5 lift-off, 6 1500 ft, 7 800 ft, 8 touchdown, 9 80 kt, 10 2nd engine shut down, back to 1 five minutes later | assumed |
| QPAC flight phase | `AirbusFBW/QPACFlightPhase` | int (X-Airbus): 1 elec pwr, 2 1st engine started, 3 taxi to runway, 4 1st engine T.O. power, 5 80 kt, 6 lift-off, 7 1500 ft, 8 flaps up, 9 2000 ft (approach), 10 800 ft, 11 touchdown, 12 80 kt, 13 taxi to gate, 14 2nd engine shut down. X-Airbus lists "4" twice (T.O. power and 80 kt); 5 for 80 kt is our reading | assumed |
| Thrust levers | `AirbusFBW/throttle_input` | float array, per lever (X-Airbus reads index 4): 1.0 TOGA, 0.88 FLX/MCT, 0.7 CL, 0 idle, negative = reverse. FXAnnounce takes the highest of entries 0…4 | assumed |
| Ground spoilers | `AirbusFBW/SDSpoilerArray` | per panel; X-Airbus: entries 0 and 1 at 1 = ground spoilers out. FXAnnounce: any entry above 0.05 = out | assumed |
| Speedbrake armed | `sim/cockpit2/controls/speedbrake_ratio` | −0.5 = armed (X-Airbus) | X-Plane standard |
| Gear lever | `AirbusFBW/GearLever` | 1 down, 0 up (X-Airbus; informational) | assumed |
| Flap lever | `AirbusFBW/FlapLeverRatio` | 0 … 1, × 4 = detent 0/1/2/3/FULL (X-Airbus; informational) | assumed |
| ToLiss VS | `toliss_airbus/pfdoutputs/captain/vertical_speed` | ft/min (X-Airbus; informational) | assumed |
| Cargo doors | `AirbusFBW/CargoDoorArray` | per door, 0 closed (X-Airbus; informational) | assumed |
| Distance to destination | `AirbusFBW/DistToDest` | NM (X-Airbus; informational) | assumed |
| Landing lights (candidates) | `sim/cockpit2/switches/landing_lights_on`, `sim/cockpit2/switches/landing_lights_switch`, `AirbusFBW/LeftLandLightExtended`, `AirbusFBW/RightLandLightExtended` | the probe step "Toggle the landing lights" picks the one that follows the switches (`[toliss] landing_light_source`, default the first) | assumed |
| ToLiss sound mix | `AirbusFBW/fmod/vol/Internal`, `cockpit`, `spkrs` | 0 … 1 (names from the binaries) | assumed |
| ToLiss volume override | unknown | the ISCS "Ovrd XP Int/Ext Vol" switch; the probe step finds the dataref (`[toliss] volume_override_ref`) | unknown |
| Cockpit door sound blend | `AirbusFBW/fmod/vol/FwdCockpitDoorBlend` | 0 closed … 1 open (assumed) | assumed |

## Commands observed (pass-through handlers, never run by FXAnnounce)
| Command | Meaning | Status |
|---|---|---|
| `AirbusFBW/purser/fwd`, `aft` (all models), `all`, `mid`, `exit` (A321, A339) | cabin call buttons on the overhead CALLS panel | assumed: whether a mouse click on the overhead button runs the command is to be confirmed (FXAnnounce logs each press) |
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
| `AirbusFBW/CheckCabin` | cabin check / cabin ready (to be verified) |
| `AirbusFBW/CabinLightLevel`, `cabinLightLevel_perc` | cabin lighting (IACP Cabin Lighting tab) |
| `AirbusFBW/fmod/env/cabinChime` | ToLiss's cabin chime |

ToLiss already does some cabin-crew work by itself: AUTO doors open L1 and the cargo doors at the gate and close after
a set time (sooner with the beacon on), the slides are disarmed after landing once the engines and beacon are off,
and the cabin lights go off for takeoff and landing.

## Probe session checklist
The Inspector tab's **Probe** section runs this list. Items marked *(auto)* tick themselves when the value is seen;
*(step)* items have a Start button: do the action within 20 s and the probe reports what moved, with an Apply button
where a setting depends on it. **Snapshot** and **What changed?** diff every `AirbusFBW/`, `toliss_airbus/`, `ckpt/`
and `sbh/` dataref, for anything the list doesn't cover. **Write probe summary** puts the findings in
`FXAnnounce.log`.

1. **At the gate, cold** *(auto)*: `PaxDoorArray` closed and open (which indices open on this model),
   `ACBusVoltages` without and with power.
2. **ACP** *(step)*: "Pull PA on ACP 1", "Pull CAB on ACP 1": which `KnobPush` index moved (Apply sets
   `acp_pa_index` / `acp_cab_index`). Turning them shows the `RotaryPositions` range.
3. **Signs** *(auto)*: `SeatBeltSignsOn`, `NoSmokingSignsOn` changes.
4. **Slides** *(auto)*: `SlideArmedArray` 0 → 1 and back (which indices).
5. **SatCom** *(auto)*: `SatComObjInhibit` values; **engine option**: `acf_ICAO`.
6. **MCDU INIT A** *(auto)*: `toliss_airbus/init/flight_no` seen non-empty.
7. **SimBrief** *(auto)*: an OFP from simbrief_hub, its ETD and the check result.
8. **Landing lights** *(step)*: "Toggle the landing lights": which candidate follows (Apply sets
   `landing_light_source`).
9. **Sound mix** *(step)*: "Toggle ISCS Ovrd XP Int/Ext Vol": the census diff names the dataref (Apply sets
   `volume_override_ref`). Then move the ISCS internal slider: `fmod/vol/Internal` should follow.
10. **Cockpit door** *(auto)*: which of `CockpitDoorAngle`, `ckpt/door`, `FwdCockpitDoorBlend` moves, and its range.
11. **Flight** *(auto)*: every `APPhase`, `ECAMFlightPhase` and `QPACFlightPhase` value with the groundspeed, height
    and vertical speed when it was first seen; the highest `throttle_input` during the takeoff roll;
    `SDSpoilerArray` at touchdown. A go-around if possible.
12. **Masks** *(auto)*: pax oxygen MASK MAN ON → `PaxOxyMask`.
