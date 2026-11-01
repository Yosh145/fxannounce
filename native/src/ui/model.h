// What the panel shows and the actions it can trigger. Filled by the plugin each frame the window is drawn;
// pure data + std::function so the headless UI test can drive every tab without X-Plane.
#pragma once

#include "core/config.h"
#include "core/pack_library.h"

#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace fxa::ui {

struct UiModel {
    std::string version;
    bool active = false;            // a ToLiss aircraft is loaded and the purser runs
    bool safeMode = false;
    std::string lastFault;
    bool muted = false;

    // Aircraft and pack (filled as milestones land)
    std::string aircraft;           // "ToLiss A321neo"
    std::string typeTags;           // "A321, A21N"
    std::string livery;
    std::string pack;               // active pack name
    std::string packReason;         // why this pack
    std::string customTags;
    bool overwater = false;
    std::string simbrief;           // SimBrief OFP and boarding plan, one line ("" when dormant)
    int simbriefTone = 2;           // 0 in use, 1 ignored, 2 none

    // Live readouts (label, value), shown on the Status and Inspector tabs
    std::vector<std::pair<std::string, std::string>> status;
    std::vector<std::pair<std::string, std::string>> inspector;

    std::deque<std::string> log;

    // Purser
    std::string stage, next, nowPlaying, music;
    float progress = 0;
    bool calling = false;
    std::vector<std::string> pending;
    struct EventRow {
        double t = 0;
        std::string what, outcome, detail;
        int tone = 0;   // 0 played, 1 problem, 2 neutral
    };
    std::vector<EventRow> events;   // most recent first

    // Packs tab: preview a file, explain a selection
    std::string previewing;
    std::function<void(const std::string& path, const std::string& label)> onPreview;
    std::function<void()> onStopPreview;
    std::function<std::vector<std::string>(int annType, int context)> explain;

    // Inspector tab: the guided probe (docs/TOLISS_DATAREFS.md)
    struct ProbeRow {
        std::string id, title, instruction, finding, applyKey, applyValue;
        bool step = false, done = false;
    };
    std::vector<ProbeRow> probe;
    std::string probeStep;          // id of the running step, or ""
    float probeStepLeft = 0;        // s
    int censusSize = 0;             // datarefs in the census snapshot (0 = none)
    std::vector<std::string> censusChanged;
    std::function<void(const std::string& id)> onProbeStart;
    std::function<void(const std::string& key, const std::string& value)> onProbeApply;
    std::function<void()> onProbeSummary, onProbeReset, onCensusSnapshot, onCensusDiff;

    const PackLibrary* library = nullptr;   // installed packs (nullptr until the first scan finishes)
    bool scanning = false;
    std::string packsDir;

    Settings* settings = nullptr;   // edited in place by the Settings tab

    // Actions (any may be empty)
    std::function<void()> onSkip, onAdvance, onToggleMute, onToggleOverwater, onRescan, onReenable;
    std::function<void()> onSettingsChanged;   // after Apply in the Settings tab
};

// Panel-only state that survives between frames.
struct UiState {
    int tab = 0;
    bool tabRestored = false;
    Settings pending;               // Settings tab edits until Apply
    bool pendingValid = false;
    char logFilter[64] = {};
    char inspectFilter[64] = {};
    char packFilter[64] = {};
    int explainType = 1;
    int explainContext = 0;
    std::vector<std::string> explainLines;
};

} // namespace fxa::ui
