# Changelog

One `## x.y.z` section per version, newest first. The section is pasted into the GitHub release for tag `vx.y.z`,
and the release is refused if it's missing.

## 0.3.0
### A new window
- **Two themes:** *Dark* (night navy with airline blue and red) and *White* (white with airline red). Switch with
  the White / Dark slider in the titlebar.
- **New font:** B612, the font Airbus designed for its cockpit displays.
- **Status** is one page for the flight: a Cabin card (stage, what is playing, boarding progress), a Flight card
  (airline, SimBrief callsign, route with a progress bar, departure and boarding time), the recent and coming
  announcements, and the aircraft, livery and Airline Pack.
- **Timeline** puts what comes next up front, with a countdown or what it is waiting for, and a *Play now* button.
  The history shows Zulu times (hover for local time). Untick *Timeline* in the titlebar to hide the tab.
- **Packs** shows what is playing, with its progress, *Skip* and *Mute*, and the boarding music, above your packs.
  Each file says where it is heard (cabin PA, interphone or music).
- **Settings** is split into pages (Announcements, Sound, Boarding, Timing, ToLiss, Display, Advanced) and changes
  apply at once: no more Apply button. *This flight is overwater* is on the Announcements page.
- Announcements have plain names ("Boarding welcome"); hover to see the Fenix file name.
- **Log** is hidden unless you switch it on (Settings → Advanced → *Show log*). A line that repeats now takes one row
  with a count (x5) instead of filling the view. The live sim inputs, and the "what would play now, and why"
  explanation on the Packs tab, come with it.
- The window can be opened on any aircraft. On one FXAnnounce doesn't support, Status says so and names it, and
  Packs and Settings still work.
- The Inspector tab and its probe checklist are gone; they were for development.
- The text size setting (Settings → Display) is now kept after a restart.

### Departure
- The safety briefing now starts 20–40 s after the aircraft starts moving (pushback, or taxi without a push), not a
  minute after the doors are armed. It used to be half over before the pushback tug was even connected.
- After the briefing and the cabin check, the cabin reports "cabin secured" by interphone during the taxi. The call for
  the crew to take their seats for takeoff (and the cabin dim at night) now waits for you to line up: landing lights
  ON, strobe ON (not AUTO), or a cabin call button once you are taxiing. Without one of those before takeoff thrust,
  it is skipped. A cabin call before you taxi still makes the cabin check quick.
- If you line up early, the cabin hurries its check so the calls still come before takeoff.

### In flight and landing
- After takeoff, a cabin call button, or switching NO SMOKING off and on again within 10 s, tells the crew they may
  leave their seats: the after-takeoff announcement plays straight away.
- Before landing, the cabin reports "cabin secured" once its check is done and you are in the approach phase or below
  10,000 ft. The seats-for-landing call then comes when the gear goes down, or when you flick NO SMOKING, instead of
  at 10,000 ft.
- The after-landing announcement now comes when the spoilers are stowed and the landing lights are off, as intended.
  The spoilers were read wrongly, so it always waited for the 2-minute fallback.

### SimBrief
- After takeoff the Status tab follows your plan ("DLH432 EDDM-LFMN: en route to LFMN, 312 NM", "returning to
  EDDM", "arrived at LFMN") instead of saying the plan is ignored because the departure airport is far away.
- A plan for your next leg can be loaded in flight; it is used once you are at its departure airport.
- The log no longer fills up with "pack locked" lines (thousands per flight) after takeoff.

### After the flight
- New setting Settings → Announcements → **Turnaround**, off by default: the flight is complete after deboarding, and a new
  SimBrief plan from that airport (or a new situation or reload) starts the next one. Switch it on to board the next
  leg at the same gate automatically, as before.

### Boarding
- Boarding no longer ends when the ToLiss doors close by themselves. In AUTO mode ToLiss closes the front door about
  13 minutes after the aircraft loads, which used to trigger "boarding complete" far too early. Boarding now takes a
  realistic time for your passenger count, shown as a countdown in the Status tab. With a SimBrief plan it completes
  about 10 minutes before departure.
- Opening, closing or switching the doors to OPEN in the ISCS no longer changes the boarding. Switching on the
  beacon, pushing back, starting an engine or arming the slides still ends boarding straight away.
- Pressing a cabin call button (FWD, AFT…) during boarding completes it about a minute later.
- If your SimBrief departure time is too close (SimBrief sets the off-block time 30 minutes after you start the
  dispatch unless you change it), boarding is shortened and the Status tab tells you how to fix it.
- If the SimBrief boarding time is more than an hour away, FXAnnounce assumes X-Plane's clock isn't set to your
  flight's time. It boards when the doors open instead and says so in the Status tab. Setting the clock later is
  picked up.
- Without a SimBrief plan, boarding also starts when ToLiss closed the door before you powered up the aircraft.
- After a turnaround, the next boarding starts even if ToLiss closed the door again in the meantime.

### Sound
- The cabin's call to the cockpit now sounds like the A320 cockpit buzzer (a 3-second buzz) instead of a chime.
- Announcements behind the closed cockpit door are now quiet and muffled, as if heard through the door. They get
  clear when you open the door, walk into the cabin or pull the PA knob on the ACP. Settings → Sound has the level,
  the muffle cutoff and a switch to turn muffling off.
- The ACP volume knobs work: the PA knob sets the level of the announcements and the music in the cockpit, and the CAB
  knob sets the level of cabin calls.
- The ACP PA knob is now read from the right place on every ToLiss model. If you never changed it, your settings are
  updated automatically, along with the old, louder closed-door level.

### Fixes
- Loading at a gate with engines running no longer leaves the cabin stuck: boarding is skipped with a note in the
  Status tab, and shutting the engines down at the gate lets you board. The state is now taken 10 s after loading, so
  the aircraft settling in place isn't mistaken for taxiing.
- No more "FMGS not initialized" message after engine shutdown (ToLiss resets the flight phase then).
- Files with names from other announcement tools, such as `BoardingStarted.ogg` or `Turbulence.ogg`, aren't Fenix
  announcements and aren't played. The Packs tab now says so and names the Fenix file name to use instead
  (`BoardingWelcome`, `SeatbeltSignOn`); the log counts them for each pack.
- Log lines no longer show a stray space ("CrewTakeSeats (takeoff) playing").
- The SimBrief plan is no longer written to the log twice when a flight trace starts.

## 0.2.0
### Downloads
- First packaged release: one zip with the plugin for Linux, Windows and macOS (Intel and Apple Silicon), ready to
  copy into `Resources/plugins/`.

### Fixes
- The UI no longer calls into X-Plane while the plugin is unloading (0.1.1). Disabling or reloading FXAnnounce no
  longer risks a crash.
- Your old `boarding_on_call` setting is converted to the new **Boarding starts** setting automatically.

### Changes since 0.1
- ToLiss A320 and A321 support, with boarding timed from the SimBrief plan.
- The A330 and A340 load.
- The Inspector and probe checklist for checking ToLiss values on your own aircraft.
