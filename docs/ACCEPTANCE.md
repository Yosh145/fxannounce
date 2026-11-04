# In-sim acceptance

The owner runs these checks in X-Plane. Agents never mark them passed. Record the date, the X-Plane and ToLiss
versions and the result of each row.

## Setup
1. Copy `build/FXAnnounce` to `X-Plane 12/Resources/plugins/`.
2. Put the packs into `X-Plane 12/Resources/plugins/AirlinePacks/` (folders named `DLH`, `EJU`, `UAL`, …; the
   `DLH OLD PA SYSTEM` wrapper can stay as it is).
3. Optional: `[general] recorder = yes` in `Output/preferences/FXAnnounce.ini`, or tick Settings → Advanced →
   "Record flight trace". The trace goes to `Output/FXAnnounce/`.
4. For the SimBrief checks: simbrief_hub installed with your Pilot ID, and an OFP generated for the flight.

## Checks
| # | Check | Expected |
|---|---|---|
| A-01 | Load the C172 | Plugins → FXAnnounce → Show/hide window opens the window (Mute and Overwater greyed out): Status shows the UNSUPPORTED AIRCRAFT card with the C172's name and the chip says UNSUPPORTED; Packs and Settings work. `Log.txt` says dormant; frame time unchanged with the window closed |
| A-02 | Load the ToLiss A321 with the BA livery | Chip ACTIVE; the Status Aircraft card shows the A321, the livery and the pack; `FXAnnounce.log` gives the reason for the pack |
| A-03 | Titlebar: the White/Dark switch, then the Timeline checkbox | The theme changes at once and is kept after an X-Plane restart; unticking Timeline hides its tab, ticking it brings it back |
| A-04 | At the gate with power, press FWD (or AFT/ALL) on the overhead CALLS panel | `FXAnnounce.log` shows "cabin call button"; boarding music plays; BoardingWelcome every 2–6 min |
| A-04b | Settings → Announcements → "Boarding starts" = Doors open, doors open | Boarding starts 15 s after a door opens |
| A-05 | Boarding time up (Status, Cabin card: the boarding bar's "complete in …") or beacon on | BoardingComplete, then ArmDoors on beacon/push/engine start |
| A-06 | Beacon on and doors armed, wait at the gate, then push back | Nothing while parked; PreSafetyBriefing 20–40 s after the push starts (at least 60 s after ArmDoors), then SafetyBriefing; music stopped |
| A-07 | After the briefing and the cabin check (taxiing) | The cockpit call buzzer; pulling CAB on the ACP plays CabinSecured. Stage "Cabin secured, waiting for line-up"; CrewTakeSeats not yet |
| A-08 | Takeoff thrust while a call plays | The call stops; nothing more until after takeoff |
| A-09 | Acceleration altitude | AfterTakeoff 20–60 s later; seatbelt sign off → SeatbeltSignOff |
| A-10 | Top of climb, descent | CabinService in cruise; DescentStarted after the descent starts |
| A-11 | Descent below 10,000 ft (or APPR phase), then gear down | The buzzer and CabinSecured (landing) once the cabin check is done; CrewTakeSeats (landing), and CabinDim at night, when the gear lever goes down |
| A-12 | Landing, spoilers stowed, landing lights off | AfterLanding as soon as the lights go off below 40 kt (not 2 min after touchdown) |
| A-13 | At the stand, beacon off | DisarmDoors, then music and DeboardingStarted when the door opens, DeboardingComplete later |
| A-14 | Pause the sim during an announcement | Audio stops; resumes from the same point on unpause |
| A-15 | Cockpit door open/closed, ACP PA knob in/out, camera in the cabin | PA level follows (never below the floor) |
| A-16 | Packs tab during an announcement | The Now playing card shows the announcement, the file and its progress; Skip and Mute work; with Settings → Advanced → Show log on, "What would play now, and why" explains the choice |
| A-17 | Skip (Packs), Play now (Timeline) and the commands | Skip stops the current call; Play now fires the next event |
| A-18 | Rescan with a pack added | The new pack appears; the active pack stays locked after ArmDoors |
| A-19 | Plugins → Admin → disable and re-enable FXAnnounce | No crash; state rebuilt |
| A-21 | Press a cabin call during the safety briefing, before taxiing | The briefing plays to the end; the buzzer and CabinSecured come about 15 s after it; CrewTakeSeats still waits for the line-up |
| A-20 | Frame time with the window closed vs plugin removed | ± 0.1 ms |
| A-22 | SimBrief OFP for this airport, loaded more than 40 min before its ETD | The Status Flight card shows the OFP, the ETD, the boarding time and "Boarding in …". Silent even with the AUTO door open; boarding starts at that time without a button |
| A-23 | Same, but load inside the lead time (e.g. 20 min before ETD) | Status says the boarding time has passed; boarding starts 15 s after a door opens |
| A-24 | OFP for another airport | The Flight card says "Not used here: origin … NM away"; doors rule; the pack doesn't come from the OFP |
| A-25 | Short flight without initializing the FMGS | Log says the phase is estimated; AfterTakeoff, CabinService (if the cruise lasts) and DescentStarted still play |
| A-26 | A321ceo full flight (any livery) | Same sequence as on the A320; the trace shows the A321 door indices and ACP indices |
| A-27 | ISCS "Ovrd XP Int/Ext Vol" on, then move the internal slider | Once `[toliss] volume_override_ref` is set (TOLISS_DATAREFS "Verifying an encoding by hand"), the PA level follows the ToLiss slider |
| A-28 | Load the A339 | No FXAnnounce menu; `Log.txt` says the model is not supported yet |
| A-29 | Settings → Advanced → Show log on, through a flight | The Log tab appears; a line that repeats takes one row at the bottom with `(xN)`; Live inputs shows the sim values; `FXAnnounce.log` still has every line |
| A-30 | Instant fuel load in the ISCS at the gate | No "refuelling detected" in the log; an IACP refuel is detected |
| A-31 | Boarding running, ISCS doors on AUTO: wait until ToLiss closes L1 on its own (~13 min after loading) | Boarding continues: music and BoardingWelcome keep going; BoardingComplete only when the Status countdown ends |
| A-32 | During boarding, set L1 from AUTO to OPEN in the ISCS, then close and reopen it | No change to the stage or the countdown |
| A-33 | After BoardingComplete, reopen L1, then beacon on | Stage stays "Boarding complete"; ArmDoors waits for the door to close (up to 120 s) |
| A-34 | During boarding press FWD on the CALLS panel | BoardingComplete about 60 s later |
| A-35 | SimBrief OFP with the OBT left at its default (ETD about 30 min after dispatch), load straight away | Status warns "Boarding shortened to … min"; BoardingComplete about 10 min before the ETD, never sooner than 5 min after boarding starts |
| A-36 | SimBrief OFP, X-Plane clock set 6 h away from the flight's time | Status: "SimBrief boarding time is … h away: is the X-Plane clock set to the flight's time?"; the doors rule starts boarding; the pack still follows the OFP. Setting the clock to the flight's time brings back the schedule |
| A-37 | Doors rule (no OFP), cold and dark: let ToLiss close L1 before switching on power | Boarding starts about 15 s after AC power comes on |
| A-38 | ACP: pull PA, turn its volume knob; pull CAB during a CabinSecured call, turn its knob | The PA level follows the PA knob; the interphone follows the CAB knob (never below 0.15). CAB alone doesn't bring in PA announcements |
| A-39 | Cockpit door closed, PA knob in, camera in the seat | Announcements are quiet and muffled; open the door or walk into the cabin and they become clear; Settings → Sound → Muffle through the door off brings back the old unfiltered sound |
| A-40 | Line up with the landing lights ON (not just extended), or the strobe ON (not AUTO) | CrewTakeSeats (takeoff), then CabinDim at night, right after the signal |
| A-41 | While taxiing (above 5 kt) press FWD on the CALLS panel, after CabinSecured | CrewTakeSeats (takeoff) straight away. Pressed before the taxi it only makes the cabin check quick |
| A-42 | Landing lights ON while the safety briefing still plays | The briefing plays to the end; the cabin check takes 15 s; CabinSecured, then CrewTakeSeats |
| A-43 | Take off with no line-up signal (lights and strobe off, no call) | No CrewTakeSeats; the Timeline shows it expired at takeoff thrust |
| A-44 | Climbing, before AfterTakeoff: press FWD, or switch NO SMOKING off and on again within 10 s | AfterTakeoff plays at once |
| A-45 | Descent: switch NO SMOKING off and on again after CabinSecured (landing), gear still up | CrewTakeSeats (landing) at once |
| A-46 | Cruise with a SimBrief OFP accepted at the gate | Status: "<callsign> <orig>-<dest>: en route to <dest>, … NM"; FXAnnounce.log has no "pack locked" lines after takeoff |
| A-47 | Turn back toward the departure airport for more than 10 NM | Status: "returning to <orig>, … NM" |
| A-48 | Land at the destination | Status: "arrived at <dest>" |
| A-49 | Settings → Announcements → Turnaround off (default): deboard completely | Stage "Flight complete"; nothing more plays with the doors open. Generate the next leg's OFP in SimBrief: a new flight starts and boards on its schedule |
| A-50 | Turnaround on: deboard completely | As before: stage "Turnaround", next boarding after the gap |
| A-51 | Load at a gate with X-Plane's "start with engines running" | Status note "Started with engines running: boarding skipped…"; a cabin call doesn't board. Shut both engines down (parking brake set): boarding works as from cold and dark |
| A-52 | A pack with `BoardingStarted.ogg` or `Turbulence.ogg` | Packs tab: "not a Fenix announcement type, not played (rename to BoardingWelcome / SeatbeltSignOn to use it)"; FXAnnounce.log: "pack …: n sounds, 2 not played (not Fenix types: …)" |
| A-53 | Engines shut down at the stand after landing | No "FMGS not initialized" line in FXAnnounce.log |
| A-54 | Settings → Sound: drag Cabin PA volume during an announcement, then restart X-Plane | The level follows while dragging; the value is kept after the restart (there is no Apply button) |
| A-55 | Settings → Reset to defaults | A confirmation first; then defaults everywhere, but the window position, theme and text size are kept |
| A-56 | Timeline during boarding and in flight | The Coming up card shows the next announcement with a countdown or what it waits for; Play now fires it. History shows Zulu times (local time in 24 h and 12 h on hover) and plain names (the Fenix name on hover) |
| A-57 | Settings → Announcements → This flight is overwater, with a pack that has an `[Overwater]` file | The overwater file becomes eligible; the menu item and the command show the same state |
| A-58 | Status at the gate with a SimBrief OFP, then narrow the window | Cabin and Flight cards side by side, stacked when narrow; the Flight card shows the airline name, callsign, route bar, ETD and boarding time; Recent and Coming up fill down to the Aircraft card (up to 8 rows) as the window grows; long text wraps inside the cards |
| A-59 | Both themes with the cockpit at night and in daylight | Text readable everywhere; the cards are black with a coloured top line; no clipped text at 520 px wide or at text size 0.6 and 2.0 |
