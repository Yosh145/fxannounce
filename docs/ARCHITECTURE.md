# Architecture

**Scope:** how FXAnnounce is built: modules, threads, data flow and the crash-safety rules. Behaviour is specified in
`VP_SPEC.md`. The verified ToLiss dataref encodings are in `TOLISS_DATAREFS.md`. The plugin activates on the ToLiss
A319, A320 and A321 only; everything else leaves it dormant.

## 1. Modules
| Folder | What | Depends on |
|---|---|---|
| `native/src/core/` | Packs (scan, parse, legacy aliases, tags), selection, pack resolution, the purser state machine, the queue and timeline, config, the SimBrief OFP check, the OFP tracker for the flight (`OfpTracker`: at the origin, en route, returning, arrived) and the boarding schedule (`simbrief`), the CSV recorder and trace reader, the UI log view (`log`: repeats collapse) | C++17 only |
| `native/src/sim/` | `DataRef` wrapper, ToLiss detection and adapter (builds `SimState`), the simbrief_hub reader (`simbrief_source`) | XPLM |
| `native/src/audio/` | Ogg Vorbis decoder, decode worker and cache, the closed-door low-pass filter (`muffle`), XPLM playback, hearing model, generated chime | libvorbis, XPLM |
| `native/src/ui/` | ImgWindow host, themes, widgets and one file per tab, `tab_*.cpp` (pure ImGui, except `window.cpp` and `fonts.cpp`; § 7) | Dear ImGui, ImgWindow |
| `native/src/plugin.cpp` | XPLM entry points, menu, commands, flight loop | everything |

Rule: `core/` never includes an XPLM or ImGui header, so all behaviour can be unit-tested without X-Plane. The tabs
in `ui/` never include XPLM, so the headless UI test can render them.

## 2. Data flow (main thread, one flight loop)
```
ToLiss datarefs ──► TolissAdapter ──► SimState ──► Purser (state machine) ──► Queue ──► Player ──► XPLMPlayPCMOnBus
                                          │            ▲                          ▲          ▲
                                          ▼            │ BoardingPlan             │          │ PCM
sbh/* datarefs ──► SimbriefSource ──► Ofp ──► checkOfp / boarding schedule        │   decode worker + cache
                                               │ OFP callsign                     │
                     PackLibrary ◄── scan     ▼                                   │
                     PackResolver ──► active pack ──► Selector ──► chosen file ───┘
```
- The flight loop runs at about 4 Hz for state and every frame while audio is playing, so volumes follow the
  hearing model smoothly.
- Cabin sounds (PA, music) play as two channels started in the same frame from the same position: the clear voice
  and its *twin*, the muffled copy. Pause, resume, skip and stop act on both; the hearing model gives each its own
  gain (VP_SPEC § 4.5).
- The `SimState` snapshot and the `BoardingPlan` are the only inputs to the purser. Recorded `SimState` traces
  replay through the same purser in the tests. The trace reader maps columns by the header names, so older traces
  stay readable when columns are added.
- **simbrief_hub** (optional, separate plugin) is a read-only peer: FXAnnounce reads its `sbh/*` datarefs, resolving
  them again every 10 s until they exist, and re-reads the OFP when `sbh/seqno` changes. The origin and destination
  airports are looked up with `XPLMFindNavAid` on the main thread, once per OFP. `OfpTracker` (core, pure) follows the
  OFP through the flight from the aircraft position, and the purser tells the app when the aircraft has taken off.

## 3. Threads
- **Main thread:** all XPLM calls, the purser, the queue, the UI.
- **Workers:**
  - a pack scan worker: filesystem only;
  - a decode worker: file read and Vorbis decode into a PCM buffer, plus its muffled (low-passed) copy for the
    closed-door voice (VP_SPEC § 4.5). Changing the cutoff empties the cache; voices that are playing keep their buffer.
  They hand results back through mutex-protected queues that the flight loop drains. They never call XPLM.
- On plugin disable, workers are cancelled and joined before anything they reference is destroyed.

## 4. Crash safety
1. Every function X-Plane calls into (`XPlugin*`, flight loops, menu and command handlers, audio completion
   callbacks, window build) runs inside `fxa::guard(name, fn)`.
   - `guard` catches `std::exception` and `...`, logs the fault, and counts it.
   - No exception ever crosses into X-Plane.
2. **Circuit breaker:** after 3 faults the plugin enters *safe mode*. Audio stops, flight loops are unregistered,
   and the window shows a red banner with a Re-enable button. The plugin never takes the sim down with it.
3. **Datarefs** go through `DataRef<T>`:
   - resolved once and re-resolved on plane load;
   - type checked against `XPLMGetDataRefTypes`, array reads bounds checked, strings NUL-terminated.
   - A missing dataref turns off only the features that need it, with one log line.
4. **Read-only toward ToLiss in v1:** no dataref writes, and no ToLiss commands are run. Observing ToLiss commands
   is allowed with handlers registered before ToLiss's (`inBefore = 1`) that always return 1, so the press reaches
   ToLiss unchanged. They are registered only while active and removed on deactivation. That's how the purser call
   buttons are seen (VP_SPEC § 4.3a). This still holds in phase 2 (announcement automation). The Virtual Purser
   actions of a later stage (slides, door modes, cabin lights, CABIN READY) will go through a `CabinActions`
   interface. The Settings tab writes FXAnnounce's own settings only.
5. **Untrusted inputs** (pack files):
   - the size, duration, sample rate and channel count are validated before decoding;
   - decode errors are logged once and the file is marked bad;
   - filesystem calls use the `std::error_code` overloads.
6. **Audio buffers** are owned by the player until X-Plane's completion callback for that channel arrives (it can
   arrive inside `XPLMStopAudio`). Buffers of stopped channels are parked until then. The callback looks its voice
   up by id; it never dereferences a stale pointer. A twin channel plays from its parent's `PcmBuffer` (the muffled
   copy lives in the same buffer) and is parked under its own id, so either completion can arrive first. Only the
   parent's completion ends an announcement.
7. **Nothing calls into X-Plane from static destructors.** Everything that owns XPLM resources is released in
   XPluginDisable: windows, ImgWindow's shared font atlas and textures (`ui::shutdownUi`), audio channels, flight
   loops. `App::~App` only joins threads and deliberately leaks what would call XPLM.
8. `.xpl` linkage:
   - hidden visibility, only `XPlugin*` exported (`native/exports.map`);
   - static libstdc++ on Linux release builds;
   - all third-party code compiled into the plugin.

## 5. Files at runtime
| Path | What |
|---|---|
| `Resources/plugins/FXAnnounce/{lin,win,mac}_x64/FXAnnounce.xpl` | the plugin |
| `Resources/plugins/AirlinePacks/` | packs (user-installed), plus optional `liveries.ini` and `airlines.tsv`. The release zip ships it empty except for `PUT_YOUR_PACKS_HERE.txt`; only sub-folders are scanned, so loose files are ignored |
| `<livery>/airlinepack.cfg` | optional per-livery mapping |
| `Output/preferences/FXAnnounce.ini` | settings (written by the UI) |
| `Output/FXAnnounce.log` | the plugin's own log (rotated at 1 MB, one backup). `Log.txt` gets one-line summaries |
| `Output/FXAnnounce/*.fxtrace.csv` | recorder output (when enabled) |
| `Resources/plugins/simbrief_hub/` | optional companion plugin (not ours); FXAnnounce only reads its datarefs |

## 6. Build, CI and releases
`.github/workflows/build.yml` runs on every push, pull request and tag.

| Job | Runner | Does |
|---|---|---|
| `linux` | ubuntu-22.04 (oldest glibc we support) | Release build with static libstdc++, `fx_tests`, `fx_ui_tests` |
| `windows` | windows-2022 | MSVC Release build, `fx_tests`, `fx_ui_tests` |
| `macos` | macos-15 | universal (x86_64 + arm64) Release build, checked with `lipo`, then `fx_tests`, `fx_ui_tests` |
| `package` | ubuntu-22.04 | after all three: `tools/package.sh` assembles the install layout below. It is uploaded as the `FXAnnounce` artifact on every run, so testers can grab any build |
| `release` | ubuntu-22.04 | tags only: creates a **draft** GitHub release with the zip, its checksum and the notes. The owner publishes it |

- **Test gate:** a release needs every job green, and both test suites run on all three platforms. `fx_ui_tests`
  keeps assertions on in Release builds on every compiler (`-UNDEBUG`, also under MSVC). `test_ui.cpp` refuses to
  compile if `NDEBUG` is still defined, so the gate can't silently test nothing.
- **Install layout** (`FXAnnounce-<version>.zip`). The user copies both folders into `X-Plane 12/Resources/plugins/`:
  ```
  FXAnnounce/{lin_x64,win_x64,mac_x64}/FXAnnounce.xpl
  FXAnnounce/README.md, FXAnnounce/LICENSE, FXAnnounce/licenses/B612-OFL.txt
  AirlinePacks/PUT_YOUR_PACKS_HERE.txt
  ```
  The plugin is self-contained: fonts and the airline table are compiled in, so nothing else ships except the
  font licence (B612, SIL OFL 1.1, which must travel with the font).
- **Cutting a release:** bump `project(FXAnnounce VERSION x.y.z)`, add a `## x.y.z` section to `CHANGELOG.md`,
  commit, then push the tag `vx.y.z`. `package` fails if the tag isn't `v` + the CMake version or if the
  CHANGELOG section is missing. The release notes are that section followed by install steps, the zip's SHA-256
  and a link to the tagged source (GPL-3.0). Re-running the workflow on the same tag replaces that tag's draft; a
  published release is never changed (the job fails instead, so bump the version).
- **macOS signing:** the Mac build is not notarized; only the linker's ad-hoc signature is present. The notes and the
  README give the `xattr` command that clears the download quarantine.

## 7. User interface
The window is one ImgWindow (`ui/window.cpp`) that draws `ui::drawPanel` from a `UiModel` the app fills each
frame. The tabs only read the model and call its `std::function` actions, so the headless UI test renders every one
of them without X-Plane.

- **Any aircraft:** the menu (Show/hide window, Rescan) and the window exist on every aircraft. When FXAnnounce is
  dormant, Status shows an UNSUPPORTED AIRCRAFT card with the loaded aircraft's name, Packs and Settings work, and no
  flight loop runs except detection. Leaving a ToLiss hides the window; it reopens on the next ToLiss if it was open.
- **Titlebar row:** name, version and a state chip (`ACTIVE`, `MUTED`, `SAFE MODE`, `UNSUPPORTED`). On the right:
  the `Timeline` checkbox (`[ui] show_timeline`) and the `White / Dark` switch (`[ui] theme`), saved at once.
- **Tabs and their ids** (`[ui] tab`, stable across versions): Status 0, Timeline 1 (only with `show_timeline`),
  Packs 2, Settings 3, Log 5 (only with `[ui] show_log`). Id 4 was the Inspector (removed in 0.3.0). A saved id
  that is hidden or unknown opens Status.
- **Themes:** `ui/theme.cpp` holds one palette per theme ("Dark": navy page, Delta blue `#003268` and red
  `#9B1631` chrome; "White": white page, Emirates red `#D71A21` chrome). Every ImGui colour comes from the palette.
  Status colours follow the ECAM convention (green normal, amber caution, red only for errors and safe mode);
  `[ui] color_blind` swaps them for Okabe-Ito colours. Important content sits in *cards*: black rounded panels with
  a coloured top rule, the same in both themes.
- **Fonts:** B612 and B612 Mono (Airbus/Intactile, SIL OFL 1.1), compiled in as compressed arrays
  (`native/fonts/*.inc`, made with Dear ImGui's `binary_to_compressed_c`). The licence ships in the zip.
- **Status** is one page in bands, all cards: Cabin (stage, what plays, boarding progress: modeled time, not a
  passenger flow) and Flight (airline name from the airline table, callsign, a route bar from the great-circle
  distance between the SimBrief airports and the aircraft, ETD and boarding time) side by side; then Recent and
  Coming up, which stretch down to the Aircraft card (livery, pack) at the bottom and show as many rows as fit (at
  least 1 and 2, at most 8). Text in the cards wraps; the rows are measured wrapped. Narrow windows stack the cards
  (and show 3 rows).
- **Names:** announcements are shown in plain English (`annTypeLabel`); the Fenix name is in the tooltip.
- **Settings tab:** a sidebar of pages (`SettingDef::page`: Announcements, Sound, Boarding, Timing, ToLiss, Display,
  Advanced); the ini sections don't depend on it. Changes apply at once: a checkbox or combo on click, a slider or
  text field when it is released (`onSettingsChanged`: apply, save the ini, rescan if the pack folders changed).
- **Log tab:** the last 400 entries of `Log::recent()`. An entry that repeats an earlier one (same level and text)
  replaces it at the bottom with a count, so a repeating line takes one row. `FXAnnounce.log` keeps every line.
