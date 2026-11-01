// Headless UI test: renders every tab of the panel for many frames at narrow and wide sizes with ImGui's
// assertions on (-UNDEBUG), in every plugin state (dormant, active, muted, safe mode). It fails if a draw list
// reaches the 16-bit index range, since ImgWindow's OpenGL path ignores VtxOffset.
// Build + run: cmake --build build --target fx_ui_tests && ./build/fx_ui_tests
#include "core/pack_library.h"
#include "ui/panel.h"
#include "ui/theme.h"

#include <imgui.h>
#include <imgui_impl_null.h>

#include <cstdio>

namespace fxa::ui {
// fonts.cpp needs ImgWindow; the headless test draws with ImGui's default font.
Fonts& fonts()
{
    static Fonts f;
    return f;
}
} // namespace fxa::ui

static int gFail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++gFail; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {

void frame(fxa::ui::UiModel& m, fxa::ui::UiState& s, float w, float h)
{
    ImGui_ImplNullPlatform_NewFrame();
    ImGui_ImplNullRender_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(w, h);
    ImGui::NewFrame();
    fxa::ui::applyStyle(m.settings ? m.settings->fontScale : 1.0f);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("FXAnnounce", nullptr, ImGuiWindowFlags_NoDecoration);
    fxa::ui::drawPanel(m, s);
    ImGui::End();
    ImGui::Render();
    for (const ImDrawList* dl : ImGui::GetDrawData()->CmdLists)
        CHECK(dl->VtxBuffer.Size < 65536, "draw list '%s' has %d vertices", dl->_OwnerName, dl->VtxBuffer.Size);
}

} // namespace

int main()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui_ImplNullPlatform_Init();
    ImGui_ImplNullRender_Init();
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;   // as ImgWindow's OpenGL path

    fxa::Settings settings;
    fxa::ui::UiModel m;
    m.version = "test";
    m.settings = &settings;
    int applied = 0;
    m.onSettingsChanged = [&] { ++applied; };
    for (int k = 0; k < 500; ++k) m.log.push_back(k % 7 ? "12:00:00 INFO  line" : "12:00:00 WARN  something");
    fxa::PackLibrary lib;
    lib.warnings = { "pack folder not found: /x" };
    for (const char* name : { "DLH", "Default", "UALES" }) {
        fxa::Pack p;
        p.name = name;
        p.root = std::string("/packs/") + name;
        p.mappingOnly = std::string(name) == "UALES";
        p.wrapper = std::string(name) == "DLH" ? "DLH OLD PA SYSTEM" : "";
        for (const char* f : { "BoardingWelcome[Refueling][Evening].ogg", "CrewSeatsLanding[1].ogg", "MyCustom~Duty free.ogg",
                               "SafetyBriefing[A21N][WiFi].ogg" }) {
            auto sf = fxa::parseSoundFileName(f);
            sf->relPath = f;
            sf->path = p.root + "/" + f;
            p.files.push_back(*sf);
        }
        p.maxVariant = 1;
        p.warnings = { "x.mp4: not an .ogg file, skipped" };
        lib.packs.push_back(p);
    }
    m.library = &lib;
    m.pack = "DLH";
    m.packsDir = "/packs/";
    m.onRescan = [] {};
    for (int k = 0; k < 60; ++k) m.inspector.push_back({ "AirbusFBW/Ref" + std::to_string(k), "0 1 0.5 1" });
    m.stage = "Safety briefing";
    m.next = "CrewTakeSeats (takeoff) in 1:20";
    m.nowPlaying = "DLH/SafetyBriefing.ogg  1:02 / 4:48";
    m.progress = 0.2f;
    m.music = "DLH/BoardingMusic.ogg (paused)";
    m.pending = { "CrewTakeSeats (takeoff) in 1:20", "AfterTakeoff in 9:00" };
    for (int k = 0; k < 250; ++k)
        m.events.push_back({ k * 10.0, "BoardingWelcome", k % 3 ? "played" : "no sound in pack", "DLH/BoardingWelcome[Evening].ogg", k % 3 });
    m.onPreview = [](const std::string&, const std::string&) {};
    m.onStopPreview = [] {};
    m.previewing = "DLH/ArmDoors.ogg";
    m.explain = [](int, int) { return std::vector<std::string>{ "Pack DLH", "-> plays x.ogg", "  ok   x.ogg: no tags" }; };
    // Probe section (Inspector): auto items, steps with and without an Apply, one step running.
    m.probe = {
        { "apphase", "FMGS phase values (APPhase 0-7)", "", "0 (ground, 0 kt, 0 ft AGL, +0 fpm); 1 (ground, 32 kt...)", "", "", false, false },
        { "acp_cab", "ACP CAB reception knob index", "Press Start, then pull CAB.", "ACP1 KnobPush[6] 0 -> 1", "acp_cab_index", "6", true, true },
        { "landing", "Landing light source", "Press Start, then switch the landing lights.", "", "", "", true, false },
        { "volume", "ToLiss volume override switch", "Press Start, then toggle ISCS.", "", "", "", true, false },
    };
    int probeCalls = 0;
    m.onProbeStart = [&](const std::string&) { ++probeCalls; };
    m.onProbeApply = [&](const std::string&, const std::string&) { ++probeCalls; };
    m.onProbeSummary = [&] { ++probeCalls; };
    m.onProbeReset = [&] { ++probeCalls; };
    m.onCensusSnapshot = [&] { ++probeCalls; };
    m.onCensusDiff = [&] { ++probeCalls; };
    for (int k = 0; k < 80; ++k) m.censusChanged.push_back("AirbusFBW/Changed" + std::to_string(k));
    const char* simbriefLines[] = { "DLH400 EDDM-EGLL, ETD 13:15Z, boarding 12:39Z (boarding in 12:30)",
                                    "OFP ignored: origin EDDF is 160 NM away; doors rule",
                                    "simbrief_hub not installed: boarding starts when a door has been open at the gate" };

    struct State { bool active, muted, safe; };
    const State states[] = { { false, false, false }, { true, false, false }, { true, true, false }, { true, false, true } };
    const float widths[] = { 520.0f, 760.0f, 2000.0f };
    int frames = 0;
    for (const State& st : states) {
        m.active = st.active;
        m.muted = st.muted;
        m.safeMode = st.safe;
        m.lastFault = st.safe ? "tick: test fault" : "";
        m.aircraft = st.active ? "ToLiss A321 (a321.acf)" : "";
        m.status = { { "Phase", "Cruise" }, { "Next", "DescentStarted in 12:00" } };
        m.calling = st.muted;   // exercise the calling banner in one state
        const int si = static_cast<int>(&st - states);
        m.simbrief = st.active ? simbriefLines[si % 3] : "";
        m.simbriefTone = si % 3;
        m.probeStep = si == 2 ? "landing" : "";
        m.probeStepLeft = 12;
        m.censusSize = si == 0 ? 0 : 2400;
        for (float w : widths)
            for (int tab = 0; tab < fxa::ui::kTabCount; ++tab) {
                fxa::ui::UiState s;
                s.tab = tab;
                s.explainLines = { "Pack DLH", "-> plays x.ogg" };
                for (int f = 0; f < 10; ++f, ++frames) frame(m, s, w, w * 0.8f);
                CHECK(s.tab == tab, "tab %d restored as %d", tab, s.tab);
            }
    }
    for (float scale : { 0.6f, 2.0f }) {
        settings.fontScale = scale;
        fxa::ui::UiState s;
        for (int f = 0; f < 5; ++f, ++frames) frame(m, s, 520, 380);
    }

    ImGui_ImplNullRender_Shutdown();
    ImGui_ImplNullPlatform_Shutdown();
    ImGui::DestroyContext();
    std::printf("fx_ui_tests: %d frames, %d failures\n", frames, gFail);
    return gFail == 0 ? 0 : 1;
}
