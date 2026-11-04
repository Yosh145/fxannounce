# Virtual Purser specification

**Scope:** what FXAnnounce does, written before the code. Sections 1–4 restate the Fenix *Airline Packs* rules (from
the Fenix "Airline Packs (Cabin Announcements) Guide" and the "Virtual Purser and Airline Packs Developer Guide",
updated October 2026) and say how each one maps onto the ToLiss aircraft. Section 5 lists every place we deviate.

**Aircraft:** the ToLiss A319, A320 and A321 (CEO and NEO, including the A321 LR and XLR), the same family Fenix
simulates. The A330-900 and A340-600 stay dormant until a later phase.

**Precedence:** the Fenix guides win. Where this document and the guides disagree, the guides are right, unless the
difference is listed as a deviation (D-n) in § 5. Deviations need the owner's approval.

Constants marked *(tunable)* are defaults in `FXAnnounce.ini` and are editable in the Settings tab.

## 1. Airline Packs

### 1.1 Location and identity
- **Fenix:** a pack is a folder named after an airline ICAO code (`BAW`), placed in an `AirlinePacks` folder. The
  default pack `Default` is used when no airline pack matches.
- **FXAnnounce:** packs live in `X-Plane 12/Resources/plugins/AirlinePacks/` plus any extra roots set in the ini.
  - A top-level folder named like a pack id (`^[A-Za-z0-9]{2,4}$`, case-insensitive) or `Default` is a pack.
  - A top-level folder with any other name is a *wrapper* when it has pack-id-named subfolders: each of those is a
    pack (`DLH OLD PA SYSTEM/DLH` → `DLH`). Without such subfolders, a folder that contains audio is a pack under
    its own folder name (for example `UALES`). That kind of pack is reachable only through a livery mapping (§ 1.4).
  - When two packs share a name, the first path in sorted order wins and the others are logged.

### 1.2 Files
- **Fenix:** audio files must be `.ogg`. `BoardingMusic.ogg` sits in the pack root. Announcements may be in the root or
  in any subfolder. A file name is `Type[tag]…[tag]~Display Name.ogg`: tags follow the type and come before the
  tilde. The display name (after `~`) is used only in the FAP. Folders starting with `.` or `__` are only hidden in
  the FAP.
- **FXAnnounce:**
  - only `.ogg` is accepted; other files are skipped and listed as scan warnings;
  - every subfolder is scanned, including `.`/`__` ones;
  - `BoardingMusic` files count only in the pack root, as in Fenix; music elsewhere is listed as a scan warning;
  - the display name is used in the UI;
  - tags and types are case-insensitive;
  - a file whose type is not a known announcement type (§ 2) or legacy alias (§ 1.3) is listed as "custom" and
    never played (in Fenix it would be FAP-only). Its scan warning says it is not a Fenix announcement type and, for
    names other announcer tools use, which Fenix type to rename it to (`BoardingStarted` → `BoardingWelcome`,
    `Turbulence` → `SeatbeltSignOn`). The log line for each pack counts these files and names their types.

### 1.3 Legacy names
- **Fenix:** legacy announcement names still work, but the guide doesn't list them. Community packs mostly use them.
- **FXAnnounce** maps them as follows (D-11). The context is a hard filter: a takeoff-context file is never used for
  the landing call. A file with a matching context also scores +1 (§ 3.3).

  | Legacy file type | Announcement type | Context |
  |---|---|---|
  | `CrewSeatsTakeoff` / `CrewSeatsLanding` | `CrewTakeSeats` | takeoff / landing |
  | `CabinDimTakeoff` / `CabinDimLanding` | `CabinDim` | takeoff / landing |
  | `CallCabinSecureTakeoff` / `CallCabinSecureLanding` | `CabinSecured` | takeoff / landing |
  | `DescentSeatbelts`, `DescentSeatbelt` | `DescentStarted` | — |
  | `FastenSeatbelt` | `SeatbeltSignOn` | — |
  | `DisembarkStarted` | `DeboardingStarted` | — |

  Non-legacy names of these types (`CrewTakeSeats`, `CabinDim`, `CabinSecured`) carry no context and are eligible for
  both calls. Packs can narrow them with phase tags, as Fenix intends.

  These are all the legacy names known (checked in November 2026 against the Fenix guides and the legacy list of the
  community tool FCAC). Names such as `BoardingStarted` and `Turbulence` come from other announcer tools; they are
  neither Fenix types nor Fenix legacy names and stay custom (§ 1.2).

### 1.4 Which pack is active
- **Fenix:** the pack whose folder name equals the flight number's airline code, or the livery's `icao_airline`,
  is used. Otherwise `Default` is used. Sounds from different packs are never mixed.
- **FXAnnounce:** the first match in this list wins (D-9):
  1. **Sidecar** `<livery folder>/airlinepack.cfg`: `airline = BAW`. The Fenix key `icao_airline` is accepted as an
     alias.
  2. **Central mapping** `AirlinePacks/liveries.ini`: a section `[<glob on the livery folder name>]` with the same keys.
  3. **SimBrief OFP** (through the simbrief_hub plugin, § 4.2a), when it passes the OFP check: its callsign
     (`icao_airline` + `flight_number`, "DLH400"), read like a flight number below.
  4. **Flight number** from the ToLiss MCDU (`toliss_airbus/init/flight_no`):
     - a leading three-letter ICAO code (`DLH400`) that names an installed pack;
     - or a two-character IATA code (`LH400`) mapped to ICAO through `native/data/airlines.tsv`.
  5. **Livery folder name:**
     - a token equal to an installed pack name, ignoring ToLiss configuration codes in square brackets
       (`[CFM]`, `[IAEST]`, `[LEA]`…);
     - otherwise an airline name from `airlines.tsv` ("Lufthansa" → `DLH`). The registration in the name (for
       example `G-UZHH`) picks between airlines that share a brand (easyJet: `G-` → `EZY`, `OE-` → `EJU`,
       `HB-` → `EZS`).
  6. `Default`, then silence if no `Default` pack is installed.

  A resolved airline code that names no installed pack falls through to the next source. Resolution reruns when
  the livery, the OFP or the flight number changes, but a new result takes effect only before ArmDoors. After that the pack
  is locked until the turnaround (§ 4.4).
- **Missing sounds:** as in Fenix, if the active pack lacks a sound, that announcement is skipped. The plugin never
  falls back to `Default` for a single file.

## 2. Announcement types and default modes
Fenix types, and the mode each one is performed in unless a mode tag overrides it:

| Type | Default mode | FXAnnounce delivery |
|---|---|---|
| BoardingWelcome, BoardingComplete, ArmDoors, DisarmDoors, PreSafetyBriefing, CrewTakeSeats, CabinDim, SeatbeltSignOn, SeatbeltSignOff, DescentStarted, AfterLanding, DeboardingStarted, DeboardingComplete | Manual | Cabin PA |
| SafetyBriefing, AfterTakeoff, CabinService, Decompression | Prerecorded | Cabin PA |
| CabinSecured | Interphone | Cockpit interphone (§ 4.3) |
| BoardingMusic | (music) | Cabin music channel |

- Mode tags `[Manual]`, `[Prerecorded]` and `[Interphone]` override the default. Manual and Prerecorded are both
  delivered as cabin PA, because there is no FAP (D-3).
- Mode tags never filter and never add to a score.

## 3. Tags and selection

### 3.1 Categories
- **Aircraft type:** Fenix defines `A319`, `A320`, `A321`. FXAnnounce treats any ICAO type designator as an aircraft
  tag (`A21N`, `B738`, `B77W`…), so a pack built for several aircraft parses correctly (D-4). The current aircraft
  has a set of type tags that includes its family:

  | ToLiss model | Type tags | Most specific |
  |---|---|---|
  | A319 | A319 | A319 |
  | A320 CEO / NEO | A320 / A320, A20N | A320 / A20N |
  | A321 CEO / NEO (incl. LR, XLR) | A321 / A321, A21N | A321 / A21N |
  | A330-900 *(later phase)* | A330, A339 | A339 |
  | A340-600 *(later phase)* | A340, A346 | A346 |
- **Refueling:** `Refueling`, `Refuelling`. True while fuel is being loaded (§ 4.2).
- **Time of day** (local time at the aircraft): `Morning` 06–12, `Afternoon` 12–18, `Evening` 18–24, `Night` 00–06.
- **FMGS flight phase:** `PreFlight`, `Takeoff`, `Climb`, `Cruise`, `Descent`, `Approach`, `GA`, `Done`, taken from
  the ToLiss FMGS phase (§ 4.2).
- **Overwater:** `Overwater`. Fenix computes it from the route (50 NM from land). FXAnnounce takes it from the livery
  config (`overwater = yes`) or a per-flight toggle (Settings → Announcements, the plugin menu and a command; D-2).
- **Variant:** a non-negative integer (`1`, `2`…). A file without one is variant 0.
- **Custom:** any other tag. Each custom tag must be present in the aircraft's custom-tag set:
  - the livery config `custom_tags` (Fenix key `fnx_airline_pack_custom_tags` accepted);
  - plus `WiFi` and `USB` when the ToLiss SatCom antenna is fitted (D-5, list *tunable*).

### 3.2 Eligibility
- **Fenix:** tags of different categories must all be satisfied (AND). Several tags of the same category need only
  one to match (OR).
- **FXAnnounce:** follows that rule, and treats each custom tag as its own category, so every custom tag on a file
  must be present. Variant files are eligible only when their variant is the session variant (§ 3.4).

### 3.3 Choice
- **Fenix:** among eligible files the purser picks the one with the most matching tags.
- **FXAnnounce:**
  - score = the number of matching non-mode tags;
  - +1 when a legacy context matches;
  - +0.5 when the matching aircraft tag is the model's most specific one, so `[A21N]` beats `[A321]` on an A321neo
    (D-4).
  - The highest score wins. Ties are broken at random, avoiding the file played last for that type, which matters for
    the repeated BoardingWelcome.

### 3.4 Variants
- **Fenix:** once per sim session, a random variant V in 0…max (the largest variant in the active pack) is picked
  and used for all flights, unless another pack is activated. Files of variant V are used. When the pack has no
  files of variant V for an announcement, variant 0 files are used. Gaps in the numbering fall back to variant 0.
- **FXAnnounce:** the same. V is re-rolled whenever the active pack changes.

## 4. Behaviour

### 4.1 Events and triggers
**Fenix triggers**, with **ToLiss mappings**. "Phase" means the FMGS phase (§ 4.2).

Durations: a *cabin check* lasts (45 s + 0.35 s × pax) × type factor × (1 ± 20 % random) × check multiplier
*(tunable)* (D-12). Type factors: A319 0.9, A320 1.0, A321 1.25, A330 1.6, A340 1.8.

| Event | Fenix trigger | ToLiss trigger |
|---|---|---|
| BoardingMusic | Played during boarding | Loops from the start of boarding to BoardingComplete, and from DeboardingStarted to DeboardingComplete. Paused while an announcement plays, then resumed (D-8) |
| BoardingWelcome | Every 2–6 min during boarding | During boarding (D-1): the first 20–45 s after boarding starts, then every 2–6 min *(tunable)*. When boarding starts is set out in § 4.1a (D-18) |
| BoardingComplete | Boarding completed | When the modeled boarding time is up (§ 4.1b, D-1), or earlier on beacon on, pushback (groundspeed > 1 kt), an engine running or the slides armed by the user. A cabin call during boarding completes it within 60 s *(tunable)* (§ 4.3a). Door events never complete boarding: ToLiss's AUTO doors close on their own timer, and a door set to OPEN or reopened changes nothing |
| ArmDoors | Beacon on, pushback, or engine start | Same, once boarding is complete. Also when the user arms the slides. Waits up to 120 s *(tunable)* for open doors to close |
| PreSafetyBriefing | About 1 min after the doors are armed | 20–40 s *(tunable)* after the aircraft first moves once boarding is complete (the pushback, or the taxi when there is no push), and at least 60 s *(tunable)* after ArmDoors ends (D-22) |
| SafetyBriefing | Right after PreSafetyBriefing | Same |
| CabinSecured (takeoff) | Cabin secured for takeoff | When a cabin check that starts as the SafetyBriefing ends has finished, by interphone (§ 4.3), during the taxi (D-23) |
| CrewTakeSeats (takeoff) | After the cabin safety check before takeoff | After CabinSecured, on the first *line-up signal*: landing lights ON, strobe ON (not AUTO), or a purser call once the aircraft has taxied (§ 4.2, § 4.3a). A signal during the briefing or the cabin check is kept, and the check then ends within 15 s *(tunable)*. Lights already ON when the cabin is secured count. Without a signal before takeoff thrust it expires (D-23) |
| CabinDim (takeoff) | After crew take seats, during darkness | After CrewTakeSeats, when the sun is below 0° *(tunable)* |
| *Takeoff thrust* | — | The first of: phase becomes Takeoff while rolling (groundspeed > 5 kt); thrust levers at or above the FLX/MCT detent (0.85 *(tunable)*) while rolling with an engine running; the ECAM flight phase reaches 3 (T.O. power) or 4 (80 kt); engines running on ground above 40 kt. Stops any announcement that is playing (D-6) and drops pending pre-takeoff items. The log names the signal that fired |
| AfterTakeoff | Shortly after passing acceleration altitude | Phase becomes Climb, plus 20–60 s *(tunable)*. At once on the pilots' "crew may leave their seats" signal: a purser call, or a NO SMOKING flick (§ 4.2), while airborne (D-25) |
| SeatbeltSignOn | Fasten seatbelt signs turned on | Airborne off→on of the sign, steady for 5 s. Suppressed within 3 min of DescentStarted |
| SeatbeltSignOff | Fasten seatbelt signs turned off | Airborne on→off of the sign, steady for 5 s |
| CabinService | Cabin service started (around top of climb) | Phase becomes Cruise, plus 3–10 min *(tunable)*. Skipped if the phase is past Cruise by then, or after a decompression |
| DescentStarted | Descent started | Phase becomes Descent (or Approach without a Descent phase), plus 30–90 s *(tunable)* |
| CabinSecured (landing) | Cabin secured for landing | By interphone when (a) a cabin check that starts when DescentStarted ends has finished, and (b) the phase is Approach or the aircraft is below 10,000 ft AGL *(tunable)* (D-24) |
| CrewTakeSeats (landing) | After the cabin safety check before landing | After CabinSecured, on a NO SMOKING flick (§ 4.2) or the gear lever down, whichever comes first. A flick before the cabin check ends is kept, and the check then ends within 15 s *(tunable)* (D-24) |
| CabinDim (landing) | As for takeoff | After CrewTakeSeats (landing), when the sun is below 0° *(tunable)* |
| *Landing calls dropped* | — | Pending landing calls are dropped below 1,000 ft AGL *(tunable)* and on touchdown |
| *Go-around* | — | Phase becomes GA: pending landing items are dropped. The cabin stays secured; nothing repeats |
| AfterLanding | Spoilers stowed and landing lights off after landing | On ground after touchdown with speedbrakes stowed (and the ground spoilers retracted, when ToLiss reports them), landing lights off and groundspeed < 40 kt. Touchdown is the first of: on ground after the flight, or ECAM flight phase 8 (touchdown) or 9 (80 kt) below 50 ft AGL. Fallback: 120 s after touchdown with groundspeed < 30 kt *(tunable)* |
| DisarmDoors | Beacon or seatbelt signs off on stand | After landing: groundspeed < 1 kt, parking brake or chocks set, and beacon off or seatbelt sign off. Also when the user disarms the slides |
| DeboardingStarted | Deboarding started | After DisarmDoors, when the forward passenger door opens. Fallback: 90 s after DisarmDoors *(tunable)* |
| DeboardingComplete | Deboarding completed | pax × 1.2 s × type factor (minimum 60 s) after DeboardingStarted. Then the flight is complete, or the turnaround follows when it is switched on (§ 4.4) |
| Decompression | Passenger oxygen masks deployed above 10,000 ft | Same. Pre-empts everything: playback stops, the queue empties, and CabinService is cancelled for the flight |

### 4.1a When boarding starts
- **Fenix:** boarding is simulated by the aircraft; when it starts is not published.
- **FXAnnounce** (D-18, D-19), with the setting "Boarding starts" *(tunable)*:
  - **SimBrief schedule** (default): boarding starts at the *boarding time*, ETD minus a lead time, where the ETD is
    the OFP's `est_out` (§ 4.2a) and the clock is X-Plane's zulu time. The comparison is on the UTC time of day,
    wrapped to ± 12 h, because X-Plane has no year.
    - **Lead time** = type minimum + (type maximum − type minimum) × load, plus a random ± 1 min *(tunable)*, rolled
      once per flight. Load = passengers ÷ seats, clamped to 0…1. Passengers: ToLiss `NoPax`, else the OFP
      `pax_count`. Seats: the OFP `max_passengers`, else the type default.

      | Type | Lead time | Default seats |
      |---|---|---|
      | A319 | 30–34 min | 144 |
      | A320 | 30–37 min | 180 |
      | A321 | 34–40 min | 220 |
    - **Plan seen before the boarding time:** nothing plays until then, even if ToLiss's AUTO door is already open.
      At the boarding time, boarding starts if the aircraft is at the gate (on ground, engines off, not moving). The
      doors don't have to be open. Without AC power the audio holds (D-13).
    - **Plan first seen after the boarding time** (a late load or a late OFP), or **no valid plan** (no simbrief_hub,
      no OFP, or the OFP failed its check): the doors rule below.
    - **Clock check:** a boarding time more than 60 min *(tunable)* ahead when the plan is first evaluated means the
      X-Plane clock doesn't match the OFP (for example, the sim is set to another time of day). The doors rule
      applies, the OFP still selects the Airline Pack, and the Status tab says so. A jump of the X-Plane zulu clock
      by more than 5 min before boarding starts evaluates the plan again, so setting the clock later is picked up.
    - A new OFP before boarding starts re-times the boarding. After the turnaround (§ 4.4), the next leg is timed
      from the OFP the same way.
  - **Doors open:** boarding starts at the gate (on ground, engines off, not moving) once there has been AC power for
    15 s *(tunable)* while a passenger door is open, or has been open at this gate since the aircraft loaded (or since
    the turnaround). The second case covers ToLiss's AUTO door closing on its timer before the aircraft is powered.
  - **Cabin call only:** boarding starts only on a cabin call.
  - In every mode a cabin call button at the gate starts boarding at once (§ 4.3a).

### 4.1b How long boarding takes
- **Fenix:** boarding is a simulated passenger flow; BoardingComplete plays when it ends.
- **FXAnnounce** (D-1, D-12, D-19) models the length when boarding starts:
  - **Natural length** D = (300 s + 6 s × pax) × type factor × (1 ± 15 % random) *(base and per-pax tunable)*. Pax:
    ToLiss `NoPax`, else the OFP `pax_count`. About 17 min for 121 passengers on the A320.
  - **With a valid SimBrief plan** ("Boarding starts" = SimBrief schedule, and the clock check passed) boarding aims
    to complete 10 min *(tunable)* before the ETD: the length is the
    time to that target, but at most 1.5 × D and at least 5 min *(tunable)*. Boarding that starts on schedule follows
    the schedule, even when that is shorter than D.
    - When boarding starts more than a minute after the SimBrief boarding time (a late load, or an ETD that was
      already close, for example because the SimBrief OBT was left at its default of 30 min after dispatch) and the
      time to the target is shorter than D, boarding is shortened and the Status tab warns: "Boarding shortened to
      9 min: the SimBrief ETD is only 19 min away. Set the OBT later in SimBrief for a full boarding."
    - When the ETD has already passed, boarding takes the 5-min minimum, with the same kind of warning.
    - A new OFP during boarding re-times the end, never to less than 60 s from now.
  - **Without a plan** (or in the other "Boarding starts" modes): D, at least 5 min.
  - BoardingComplete fires when the time is up, whatever the doors are doing. Beacon, pushback, an engine running,
    the slides armed, a cabin call (§ 4.3a) or Advance end boarding sooner.
  - After BoardingComplete, a door that is opened again doesn't restart boarding. ArmDoors still waits for the doors
    to close (§ 4.1).

### 4.2 Sim inputs
- **FMGS phase:** `AirbusFBW/APPhase`, mapped to PreFlight…Done (encoding in `docs/TOLISS_DATAREFS.md`).
  - If the dataref is missing, or it still reads PreFlight 30 s *(tunable)* after lift-off and never read anything
    else during the flight (the FMGS was never initialized), an estimated phase is used instead (D-20), for the
    triggers and for the phase tags. The switch is logged once. ToLiss sets the phase back from Done to PreFlight
    when the engines are shut down after landing; that doesn't count.
  - The estimate uses ToLiss's QPAC flight phase when it is known: lift-off (6) is Takeoff; 1500 ft and flaps up
    (7–8) is Climb, refined to Cruise or Descent by altitude and vertical speed; the approach phases (9–10) are
    Approach; touchdown and after (11 and up) is Done. Without it, the estimate uses the ground state, altitude,
    vertical speed and the FMGS cruise altitude.
- **Cross-check signals** (ToLiss, names and encodings from the ToLiss binaries and X-Airbus): the ECAM flight phase
  (`AirbusFBW/ECAMFlightPhase`), the QPAC flight phase, the thrust lever positions (`AirbusFBW/throttle_input`) and
  the ground spoilers (`AirbusFBW/SDSpoilerArray`). They back up the ground events in § 4.1; APPhase stays the
  phase source when it works. Lift-off is the first of: 5 s above 50 ft AGL, or ECAM flight phase 5 or more after
  takeoff thrust.
- **Ground spoilers:** `AirbusFBW/SDSpoilerArray` holds the ECAM status of each spoiler panel: 0 retracted,
  1 extended, 2 not available (no hydraulic pressure). The ground spoilers are out when entries 0 and 1 (spoiler 1
  left and right, which only the ground spoiler function uses) both read 1. When they read 2 the state is unknown.
- **Line-up lights:** landing lights ON is `sim/cockpit2/switches/landing_lights_on` (1 only with the switch at ON;
  extended but OFF reads 0). Strobe ON is `AirbusFBW/OHPLightSwitches` entry 7 *(tunable)* at 2; 1 is AUTO.
- **NO SMOKING flick:** `AirbusFBW/NoSmokingSignsOn` goes lit → unlit → lit, unlit for at most 10 s *(tunable)*. The
  pilots use it to signal the cabin (D-25). Seatbelt sign changes are not flicks; they stay SeatbeltSignOn/Off.
- **Gear lever:** `AirbusFBW/GearLever`, 1 = down.
- **Taxied:** after ArmDoors, on the ground with an engine running above 5 kt.
- **Refueling:** on the ground, total fuel mass rising by more than 2 kg/s and less than 150 kg/s *(both tunable)*
  within the last 60 s. The upper bound ignores instant fuel loads from the ISCS.
- **Darkness:** sun elevation below 0° *(tunable)* (D-15).
- **Time of day:** X-Plane local time at the aircraft, `sim/time/local_time_sec` (D-16).
- **Power:** cabin PA and music need AC power. Without it, nothing plays and the queue holds (D-13).
- **ACP:** a reception channel is on when its `AirbusFBW/ACP{1,2,3}KnobPush` entry is nonzero (PA 15, CAB 6 on every
  ToLiss model, from the cockpit objects). Its volume is the knob's rotation: on the A319/A320/A321
  `ckpt/oh/pa/N/anim` and `ckpt/oh/cab/N/anim` (0–270°), elsewhere `AirbusFBW/ACPNRotaryPositions` at the same index
  (0–1). The highest volume of the ACPs that have the channel on is used.
- **SatCom antenna:** `AirbusFBW/SatComObjInhibit`, which reflects the ISCS "HAS SATCOM ANTENNA" setting
  (YES/AUTO/NO) after ToLiss resolves it.

### 4.2a SimBrief OFP
- **Fenix:** the active Airline Pack follows the flight number; Fenix doesn't read SimBrief for the cabin.
- **FXAnnounce** reads the OFP from the **simbrief_hub** plugin (`sbh/*` datarefs, read-only) when it is installed.
  FXAnnounce has no network code of its own. It reads the OFP again whenever `sbh/seqno` changes.
- **OFP check:** the OFP is used only if its origin airport (looked up in X-Plane's navigation data) is within 5 NM
  *(tunable)* of the aircraft. A stale OFP (`sbh/stale`) is used if it passes. A failed check is logged and shown in
  the Status tab, and the OFP is ignored for both boarding and the pack.
- **During the flight:** the check applies until takeoff.
  - An OFP accepted at takeoff stays this flight's OFP and is no longer checked against its origin. The Status tab
    reads "DLH432 EDDM-LFMN: en route to LFMN, 312 NM" (the destination is looked up like the origin).
  - **Returning:** when the distance to the origin has dropped 10 NM *(tunable)* below its largest value since
    takeoff and the distance to the destination has grown 10 NM above its smallest value since takeoff, the Status
    reads "returning to EDDM, 23 NM". Both conditions are needed, so a departure route that loops back over the
    airport doesn't trip it. On the ground within 5 NM of the origin, the OFP passes the origin check again.
  - On the ground within 5 NM *(tunable)* of the destination, the Status reads "arrived at LFMN".
  - An OFP with a new `sbh/seqno` that arrives meanwhile is the next leg's: the Status names it, and it is checked
    as above once the aircraft is on the ground at its origin.
- **Used for:** the boarding time (§ 4.1a), the Airline Pack (§ 1.4) and the next flight when the turnaround is off
  (§ 4.4). The CDM TOBT is not used.

### 4.3 Interphone (CabinSecured)
- **Fenix:** the purser calls the cockpit. To answer, pull the CAB reception knob and press ATT; volume follows the
  knob. Unanswered calls are retried up to 3 times, then the purser gives up.
- **FXAnnounce:** any announcement whose chosen file is in Interphone mode goes through the cabin call. That is
  CabinSecured by default, or any file tagged `[Interphone]`. The cockpit call buzzer, generated in code (D-7),
  sounds in the cockpit: a 2.8 s buzz at 300 Hz with odd harmonics and a 25 Hz flutter, modeled on a recording of the
  A320's. The call is answered when
  any ACP's CAB reception knob is pulled out. The message then plays on the cockpit headset bus at that knob's
  volume. Each unanswered try lasts 30 s *(tunable)*, then the buzzer sounds again, up to 3 retries, after which the
  call is dropped. Interphone items expire like any other: a pre-takeoff call expires at takeoff thrust, and a landing
  call below 1,000 ft AGL. The interphone volume follows the CAB knob's rotation (§ 4.2), never below 0.15.

### 4.3a Cockpit cabin call buttons
- **Fenix:** call buttons expedite some long-running purser actions: the safety briefing and the cabin safety
  checks before takeoff and landing. The purser completes the current action much faster than in real life, so
  CABIN READY comes sooner.
- **FXAnnounce:** the purser calls are the ToLiss commands `AirbusFBW/purser/fwd`, `aft`, `all`, `mid` and `exit`.
  `mech` calls the ground mechanic and is ignored. FXAnnounce observes them with pass-through command handlers that
  always let ToLiss handle the press. A call:
  1. **At the gate before boarding** (on ground, engines off, AC power): starts boarding (D-18).
  2. **During boarding:** boarding completes within 60 s *(tunable)* of the call (D-21).
  3. **After ArmDoors, before the aircraft has taxied (§ 4.2):** the cabin check before takeoff takes at most 15 s
     *(tunable)* from the call, or from the end of the safety briefing if the call came first. The briefing audio
     itself plays to the end.
  4. **Once taxied, before takeoff:** the line-up signal (§ 4.1, D-23): CrewTakeSeats and CabinDim follow
     CabinSecured. A cabin check still running ends within 15 s.
  5. **Airborne, before AfterTakeoff has played:** AfterTakeoff plays now (D-25).
  6. **After the descent starts and before the landing calls:** the same as 3 for the cabin check before landing.
     CabinSecured still waits for the approach phase or the ready height, and CrewTakeSeats for a NO SMOKING flick
     or the gear down.

  Otherwise a call does nothing; FXAnnounce logs every press.

### 4.4 Flight life cycle
- **Flight complete** (setting "Turnaround" off, the default, D-26): after DeboardingComplete the stage is "Flight
  complete" and nothing more plays. A new SimBrief OFP (a new `sbh/seqno`) that passes the origin check here starts
  the next flight: the purser resets, pack resolution is unlocked, and boarding follows the OFP (§ 4.1a). A situation
  load, a teleport or an aircraft reload also starts a new flight.
- **Turnaround** (setting on): after DeboardingComplete, the purser resets for the next leg. Pack resolution is
  unlocked again. With an OFP for the next leg (its origin is this airport and its boarding time is still ahead) the
  next boarding follows the schedule (§ 4.1a). Otherwise the doors rule applies with a gap: the next boarding starts
  300 s later *(tunable)* if a door has been open at this gate since the turnaround (ToLiss's AUTO door may have
  closed again by then), or as soon as a door closes and opens again. The variant keeps its value (§ 3.4).
- **Starting mid-flight:** the state is taken 10 s after the plugin activates (or after a situation load), because
  X-Plane may still be settling the aircraft (Run2 showed 0.7 kt on the first tick). Every event that would already
  have happened is then marked done, so nothing fires late:
  - airborne: everything up to the current phase;
  - on the ground and moving: boarding, the doors and the safety briefing;
  - on the ground, parked, with an engine running: boarding and the doors. The Status tab says "Started with engines
    running: boarding skipped. Shut the engines down at the gate to board." When every engine is off at the gate
    (stationary, parking brake or chocks) before the first takeoff, the purser starts over at the gate and boarding
    works as usual.
- **Situation load or teleport** (position jumps more than 1 NM in one tick): audio stops and the state is
  re-inferred as for a mid-flight start.
- **Pause:** audio pauses with the sim and resumes from the same point, and timers freeze. Replay mutes all audio.

### 4.5 Hearing the cabin in the cockpit
- **Fenix:** cabin audio is heard in the cockpit through the open cockpit door, via the ACP PA reception knob, or by
  walking into the cabin.
- **FXAnnounce** plays every cabin sound (PA and music) twice in step: a **clear** voice and a **muffled** voice,
  low-passed at 800 Hz *(tunable)* as if heard through the closed door (D-14). With f = the door open fraction:
  - clear = max(door leak × f, PA knob pulled ? PA knob volume : 0);
  - muffled = through-the-door level × (1 − f), 0.10 by default;
  - the camera aft of the cockpit door: clear 1, muffled 0;
  - an external view: clear = the exterior gain, muffled 0.

  Each voice is then scaled by master × PA (or music) volume × ToLiss volume. The muffled level makes sure
  announcements are never fully missed. With muffling off *(tunable)*, there is one voice at
  max(through-the-door level, clear). All factors are *tunable*. Both voices use the PA bus *(tunable)*.
  - **Door open fraction:** ToLiss's own cockpit-door sound blend (`AirbusFBW/fmod/vol/FwdCockpitDoorBlend`) when it
    is available, otherwise the door angle (`ckpt/door`, 0–80°).
  - **ToLiss volume:** when ToLiss's ISCS "Ovrd XP Int/Ext Vol" is on, ToLiss's internal volume slider
    (`AirbusFBW/fmod/vol/Internal`) scales the PA, music, call buzzer and interphone. Until the dataref that holds the
    override switch is identified (`[toliss] volume_override_ref`, TOLISS_DATAREFS.md), this factor is 1.

## 5. Deviations from Fenix
| Id | Deviation | Why |
|---|---|---|
| D-1 | Boarding lasts a modeled time (§ 4.1b), not a simulated passenger flow. Door events, including ToLiss's AUTO door closing on its timer, never complete it | ToLiss doesn't simulate boarding, and its AUTO doors close about 13 min after the aircraft loads whatever the boarding is doing (probe flight Run1) |
| D-2 | `[Overwater]` comes from the livery config or a manual per-flight toggle | No land/water data along the route in X-Plane or ToLiss |
| D-3 | No FAP: Manual and Prerecorded are both cabin PA. No CABIN READY indication, EFB wallpaper or FAP screensaver | ToLiss has no FAP |
| D-4 | Any ICAO type designator is an aircraft tag. Family tags are hierarchical and the most specific is preferred | Community packs tag A21N, A20N, B738… |
| D-5 | A fitted ToLiss SatCom antenna adds `WiFi` and `USB` custom tags | ToLiss models no cabin connectivity. The antenna is the nearest signal (owner's choice) |
| D-6 | Takeoff thrust stops the announcement that is playing | Owner's choice: sterile cabin at takeoff |
| D-7 | The cockpit call buzzer for interphone calls is generated in code, modeled on a recording of the A320's (§ 4.3) | Packs don't carry it. ToLiss's own sounds aren't triggered (v1 is read-only) |
| D-8 | Boarding music also plays during deboarding, and pauses rather than mixes under announcements | Owner's choice |
| D-9 | Pack sources: sidecar, central mapping, SimBrief OFP callsign, MCDU flight number, livery-name heuristics | ToLiss liveries have no `icao_airline` field |
| D-10 | Pack folders may have any name (mapping only) and may sit inside wrapper folders | Packs are often distributed that way |
| D-11 | The legacy-name table in § 1.3 is ours | Fenix doesn't publish one |
| D-12 | The boarding length, cabin-check, deboarding and delay timings are our own model (§ 4.1, § 4.1b) | Fenix doesn't publish its model |
| D-13 | PA and music need AC power | Real cabin systems; cold-and-dark at the gate stays silent |
| D-14 | Hearing model: a muffled (low-passed) voice through the closed cockpit door at a minimum level, crossfading to the clear voice with the door, the PA knob and the camera; it follows ToLiss's cockpit-door sound blend and internal volume, and the ACP knob volumes (§ 4.5) | Owner's choice; X-Plane has no cockpit-door sound path of its own |
| D-15 | Darkness is decided by sun elevation | Fenix says "hours of darkness" without a definition |
| D-16 | Time-of-day tags use X-Plane's local time | X-Plane provides it at the aircraft position |
| D-17 | Not in v1: purser actions (arming slides, cabin lights, lavatories, doors) and FAP pre-recorded playback | Announcements first. Slides and lights are planned through the `CabinActions` interface |
| D-18 | Boarding starts at the SimBrief boarding time (ETD − lead time); after a late load, without a valid OFP, or when the boarding time is more than 60 min ahead (clock mismatch), once a door is or has been open at the gate with AC power for 15 s; at once on a cabin call button. Settings: doors open only, or cabin call only (§ 4.1a) | Owner's choice: ToLiss has no boarding trigger, and real boarding starts 30–40 min before departure |
| D-19 | The boarding lead time model: a per-type range, positioned by the load factor (§ 4.1a). With a plan, boarding aims to complete 10 min before the ETD and is shortened, with a warning, when the ETD is too close (§ 4.1b) | Owner's choice; Fenix doesn't publish its model. SimBrief's default OBT (30 min after dispatch) often leaves too little time |
| D-20 | An estimated phase replaces APPhase when the FMGS was never initialized (§ 4.2) | Without it no flight-phase event would fire; Fenix assumes an initialized FMGS |
| D-21 | A cabin call during boarding completes it within 60 s (§ 4.3a) | Owner's choice; Fenix expedites only the safety briefing and the cabin checks |
| D-22 | The safety briefing starts 20–40 s after the aircraft first moves once boarding is complete, and at least 60 s after ArmDoors (§ 4.1) | Owner's choice (Run2): 1 min after ArmDoors, half the briefing was over before the pushback tug was connected |
| D-23 | Before takeoff, CabinSecured comes when the cabin check ends (during the taxi); CrewTakeSeats and CabinDim wait for a line-up signal (landing lights ON, strobe ON, or a purser call once taxied) and expire without one (§ 4.1, § 4.3a) | Owner's choice (Run2): all three played right after engine start, 11 min before takeoff. Crews are called to their seats at line-up |
| D-24 | Before landing, CabinSecured comes when the cabin check ends in the approach window; CrewTakeSeats and CabinDim wait for a NO SMOKING flick or the gear down (§ 4.1) | Owner's choice (Run2): the landing calls came at 10,000 ft, 9 min before touchdown |
| D-25 | Pilot signals: a purser call while airborne or a NO SMOKING flick plays AfterTakeoff at once ("crew may leave their seats") (§ 4.1, § 4.2) | Owner's choice: airlines signal the cabin with a chime or a sign flick |
| D-26 | The turnaround is a setting, off by default. Off, the flight ends after DeboardingComplete, and a new SimBrief OFP from this airport starts the next one (§ 4.4) | Owner's choice: not everyone flies legs back to back |
