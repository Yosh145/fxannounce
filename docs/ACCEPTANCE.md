# In-sim acceptance

The owner runs these checks in X-Plane. Agents never mark them passed. Record the date, the X-Plane and ToLiss
versions and the result of each row.

## Setup
1. Copy `build/FXAnnounce` to `X-Plane 12/Resources/plugins/`.
2. Put the packs into `X-Plane 12/Resources/plugins/AirlinePacks/` (folders named `DLH`, `EJU`, `UAL`, …; the
   `DLH OLD PA SYSTEM` wrapper can stay as it is).
3. Optional: `[general] recorder = yes` in `Output/preferences/FXAnnounce.ini`, or tick "Record flight trace" in the
   Settings tab. The trace goes to `Output/FXAnnounce/`.
4. For the SimBrief checks: simbrief_hub installed with your Pilot ID, and an OFP generated for the flight.

## Checks
| # | Check | Expected |
|---|---|---|
| A-01 | Load the C172 | No FXAnnounce menu; `Log.txt` says dormant; frame time unchanged |
| A-02 | Load the ToLiss A321 with the BA livery | Menu appears; Status: type tags `A321` (or `A321, A21N` with NEO engines), pack resolved with a reason |
| A-03 | Inspector tab during the probe session | `docs/TOLISS_DATAREFS.md` rows can be filled in (ACP indices, door values, `APPhase` values) |
| A-04 | At the gate with power, press FWD (or AFT/ALL) on the overhead CALLS panel | `FXAnnounce.log` shows "cabin call button"; boarding music plays; BoardingWelcome every 2–6 min |
| A-04b | Settings → Cabin → "Boarding starts" = Doors open, doors open | Boarding starts 15 s after a door opens |
| A-05 | Doors close (ISCS AUTO) or beacon on | BoardingComplete, then ArmDoors on beacon/push/engine start |
| A-06 | About 1 min later | PreSafetyBriefing, then SafetyBriefing; music stopped |
| A-07 | After the briefing and the cabin check | CrewTakeSeats, (CabinDim at night), cabin chime; pulling CAB on the ACP plays CabinSecured |
| A-08 | Takeoff thrust while a call plays | The call stops; nothing more until after takeoff |
| A-09 | Acceleration altitude | AfterTakeoff 20–60 s later; seatbelt sign off → SeatbeltSignOff |
| A-10 | Top of climb, descent | CabinService in cruise; DescentStarted after the descent starts |
| A-11 | Approach | CrewTakeSeats (landing), chime |
| A-12 | Landing, spoilers stowed, landing lights off | AfterLanding |
| A-13 | At the stand, beacon off | DisarmDoors, then music and DeboardingStarted when the door opens, DeboardingComplete later |
| A-14 | Pause the sim during an announcement | Audio stops; resumes from the same point on unpause |
| A-15 | Cockpit door open/closed, ACP PA knob in/out, camera in the cabin | PA level follows (never below the floor) |
| A-16 | Packs tab | Play buttons preview files; "What would play now" explains the choice |
| A-17 | Skip / Advance buttons and commands | Skip stops the current call; Advance fires the next event |
| A-18 | Rescan with a pack added | The new pack appears; the active pack stays locked after ArmDoors |
| A-19 | Plugins → Admin → disable and re-enable FXAnnounce | No crash; state rebuilt |
| A-21 | Press a cabin call during the safety briefing | The briefing plays to the end; CrewTakeSeats comes about 15 s after it |
| A-20 | Frame time with the window closed vs plugin removed | ± 0.1 ms |
| A-22 | SimBrief OFP for this airport, loaded more than 40 min before its ETD | Status shows the OFP, the ETD and "boarding at …Z (in …)". Silent even with the AUTO door open; boarding starts at that time without a button |
| A-23 | Same, but load inside the lead time (e.g. 20 min before ETD) | Status says the boarding time has passed; boarding starts 15 s after a door opens |
| A-24 | OFP for another airport | Status shows "OFP ignored: origin … NM away"; doors rule; the pack doesn't come from the OFP |
| A-25 | Short flight without initializing the FMGS | Log says the phase is estimated; AfterTakeoff, CabinService (if the cruise lasts) and DescentStarted still play |
| A-26 | A321ceo full flight (any livery) | Same sequence as on the A320; the probe shows the A321 door indices and ACP indices |
| A-27 | ISCS "Ovrd XP Int/Ext Vol" on, then move the internal slider | After the probe step identified the override, the PA level follows the ToLiss slider |
| A-28 | Load the A339 | No FXAnnounce menu; `Log.txt` says the model is not supported yet |
| A-29 | Inspector → Probe through a full flight | Every *(auto)* item ticked; "Write probe summary" puts the findings in `FXAnnounce.log` |
| A-30 | Instant fuel load in the ISCS at the gate | No "refuelling detected" in the log; an IACP refuel is detected |
