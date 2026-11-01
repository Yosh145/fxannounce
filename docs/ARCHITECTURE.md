# Architecture

**Scope:** how FXAnnounce is built: modules, threads, data flow and the crash-safety rules. Behaviour is specified in
`VP_SPEC.md`. The verified ToLiss dataref encodings are in `TOLISS_DATAREFS.md`. The plugin activates on the ToLiss
A319, A320 and A321 only; everything else leaves it dormant.

## 1. Modules
| Folder | What | Depends on |
|---|---|---|
| `native/src/core/` | Packs (scan, parse, legacy aliases, tags), selection, pack resolution, the purser state machine, the queue and timeline, config, the SimBrief OFP check and boarding schedule (`simbrief`), the probe checklist (`probe`), the CSV recorder and trace reader | C++17 only |
| `native/src/sim/` | `DataRef` wrapper, ToLiss detection and adapter (builds `SimState`), the simbrief_hub reader (`simbrief_source`), the dataref census for the probe (`probe_census`) | XPLM |
| `native/src/audio/` | Ogg Vorbis decoder, decode worker and cache, XPLM playback, hearing model, generated chime | libvorbis, XPLM |
| `native/src/ui/` | ImgWindow host, theme, widgets and tabs (pure ImGui, except `window.cpp` and `fonts.cpp`) | Dear ImGui, ImgWindow |
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
                     ProbeChecklist ◄── SimState + raw ToLiss values (Inspector tab)
```
- The flight loop runs at about 4 Hz for state and every frame while audio is playing, so volumes follow the
  hearing model smoothly.
- The `SimState` snapshot and the `BoardingPlan` are the only inputs to the purser. Recorded `SimState` traces
  replay through the same purser in the tests. The trace reader maps columns by the header names, so older traces
  stay readable when columns are added.
- **simbrief_hub** (optional, separate plugin) is a read-only peer: FXAnnounce reads its `sbh/*` datarefs, resolving
  them again every 10 s until they exist, and re-reads the OFP when `sbh/seqno` changes. The origin airport is looked
  up with `XPLMFindNavAid` on the main thread.

## 3. Threads
- **Main thread:** all XPLM calls, the purser, the queue, the UI.
- **Workers:**
  - a pack scan worker: filesystem only;
  - a decode worker: file read and Vorbis decode into a PCM buffer.
- The probe census (enumerating datarefs with `XPLMCountDataRefs`/`XPLMGetDataRefsByIndex`) runs on the main thread,
  only when the user presses Snapshot or What changed?, never in the flight loop.

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
   interface. The probe's Apply buttons write FXAnnounce's own settings only.
5. **Untrusted inputs** (pack files):
   - the size, duration, sample rate and channel count are validated before decoding;
   - decode errors are logged once and the file is marked bad;
   - filesystem calls use the `std::error_code` overloads.
6. **Audio buffers** are owned by the player until X-Plane's completion callback for that channel arrives (it can
   arrive inside `XPLMStopAudio`). Buffers of stopped channels are parked until then. The callback looks its voice
   up by id; it never dereferences a stale pointer.
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
| `Resources/plugins/AirlinePacks/` | packs (user-installed), plus optional `liveries.ini` |
| `<livery>/airlinepack.cfg` | optional per-livery mapping |
| `Output/preferences/FXAnnounce.ini` | settings (written by the UI) |
| `Output/FXAnnounce.log` | the plugin's own log (rotated at 1 MB, one backup). `Log.txt` gets one-line summaries |
| `Output/FXAnnounce/*.fxtrace.csv` | recorder output (when enabled) |
| `Resources/plugins/simbrief_hub/` | optional companion plugin (not ours); FXAnnounce only reads its datarefs |
