#include "ui/panel.h"

#include "core/strings.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace fxa::ui {

namespace {

enum TabId { TAB_STATUS, TAB_TIMELINE, TAB_PACKS, TAB_SETTINGS, TAB_INSPECTOR, TAB_LOG };
const char* kTabNames[kTabCount] = { "Status", "Timeline", "Packs", "Settings", "Inspector", "Log" };

void drawHeader(UiModel& m)
{
    const Palette& p = pal();
    if (fonts().bold) ImGui::PushFont(fonts().bold, 0.0f);
    ImGui::TextColored(vec(p.hdr), "FXAnnounce");
    if (fonts().bold) ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m.version.c_str());
    ImGui::SameLine();
    if (m.safeMode)
        Chip("SAFE MODE", withAlpha(p.bad, 0.85f), IM_COL32_WHITE);
    else if (!m.active)
        Chip("NO TOLISS AIRCRAFT", p.chipBg, p.dim);
    else if (m.muted)
        Chip("MUTED", withAlpha(p.warn, 0.75f), IM_COL32_WHITE);
    else
        Chip("ACTIVE", withAlpha(p.ok, 0.70f), IM_COL32_WHITE);

    if (m.safeMode) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
        ImGui::TextWrapped("FXAnnounce stopped itself after repeated faults. The simulator is not affected. Last fault: %s",
                           m.lastFault.c_str());
        ImGui::PopStyleColor();
        if (m.onReenable && ImGui::Button("Re-enable")) m.onReenable();
    }
}

void drawStatus(UiModel& m)
{
    const Palette& p = pal();
    if (m.active) {
        Heading("Cabin");
        if (BeginKVTable("##cabin")) {
            KV("Stage", "%s", m.stage.empty() ? "-" : m.stage.c_str());
            if (m.calling) KVColored("Interphone", p.warn, "%s", "CABIN CALLING: pull CAB on the ACP");
            KV("Now playing", "%s", m.nowPlaying.empty() ? "-" : m.nowPlaying.c_str());
            EndKVTable();
        }
        if (!m.nowPlaying.empty() && !m.calling) Progress(m.progress);
        if (BeginKVTable("##cabin2")) {
            KV("Next", "%s", m.next.empty() ? "-" : m.next.c_str());
            KV("Boarding music", "%s", m.music.empty() ? "-" : m.music.c_str());
            EndKVTable();
        }
    }
    Heading("Aircraft & pack");
    if (BeginKVTable("##acpack")) {
        KV("Aircraft", "%s", m.aircraft.empty() ? "-" : m.aircraft.c_str());
        KV("Type tags", "%s", m.typeTags.empty() ? "-" : m.typeTags.c_str());
        KV("Livery", "%s", m.livery.empty() ? "-" : m.livery.c_str());
        KV("Airline pack", "%s", m.pack.empty() ? "-" : m.pack.c_str());
        KV("Why", "%s", m.packReason.empty() ? "-" : m.packReason.c_str());
        if (!m.simbrief.empty())
            KVColored("SimBrief", m.simbriefTone == 0 ? p.ok : (m.simbriefTone == 1 ? p.warn : p.dim), "%s", m.simbrief.c_str());
        KV("Custom tags", "%s", m.customTags.empty() ? "-" : m.customTags.c_str());
        KVColored("Overwater", m.overwater ? p.accent : p.dim, "%s", m.overwater ? "yes" : "no");
        EndKVTable();
    }
    if (!m.status.empty()) {
        Heading("Inputs", "What the purser sees now. Raw dataref values are on the Inspector tab.");
        if (BeginKVTable("##status")) {
            for (const auto& [k, v] : m.status) KV(k.c_str(), "%s", v.c_str());
            EndKVTable();
        }
    }
    ImGui::Spacing();
    const float em = ImGui::GetFontSize();
    ImGui::BeginDisabled(!m.active);
    if (ImGui::Button("Skip", ImVec2(7 * em, 0)) && m.onSkip) m.onSkip();
    ImGui::SetItemTooltip("Stop the current announcement; the sequence continues.");
    ImGui::SameLine();
    if (ImGui::Button("Advance", ImVec2(7 * em, 0)) && m.onAdvance) m.onAdvance();
    ImGui::SetItemTooltip("Fire the next pending event now.");
    ImGui::SameLine();
    if (ToggleButton(m.muted ? "Unmute" : "Mute", m.muted, p.warn, ImVec2(7 * em, 0)) && m.onToggleMute) m.onToggleMute();
    ImGui::SameLine();
    if (ToggleButton("Overwater", m.overwater, p.accent, ImVec2(7 * em, 0)) && m.onToggleOverwater) m.onToggleOverwater();
    ImGui::SetItemTooltip("This flight crosses open water: allows [Overwater] announcements.");
    ImGui::EndDisabled();
}

void drawTimeline(UiModel& m)
{
    const Palette& p = pal();
    Heading("Coming up");
    if (m.pending.empty()) ImGui::TextDisabled("Nothing pending.");
    for (const std::string& line : m.pending) ImGui::BulletText("%s", line.c_str());
    Heading("History", "Newest first. 'no sound in pack' means the active pack has no file for that call (Fenix: skipped).");
    if (m.events.empty()) {
        ImGui::TextDisabled("No announcements yet.");
        return;
    }
    if (ImGui::BeginTable("##events", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        const float em = ImGui::GetFontSize();
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 4.5f * em);
        ImGui::TableSetupColumn("Announcement", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Outcome", ImGuiTableColumnFlags_WidthFixed, 8.5f * em);
        ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableHeadersRow();
        for (const auto& e : m.events) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d:%02d", static_cast<int>(e.t) / 60, static_cast<int>(e.t) % 60);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(e.what.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(vec(e.tone == 0 ? p.ok : (e.tone == 1 ? p.warn : p.dim)), "%s", e.outcome.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextWrapped("%s", e.detail.c_str());
        }
        ImGui::EndTable();
    }
}

void drawExplain(UiModel& m, UiState& st)
{
    if (!m.explain) return;
    if (!ImGui::CollapsingHeader("What would play now, and why")) return;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(14 * em);
    if (ImGui::BeginCombo("##etype", annTypeName(static_cast<AnnType>(st.explainType)))) {
        for (int k = 0; k < kAnnTypeCount; ++k)
            if (ImGui::Selectable(annTypeName(static_cast<AnnType>(k)), k == st.explainType)) st.explainType = k;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const char* ctxNames[] = { "any call", "takeoff call", "landing call" };
    ImGui::SetNextItemWidth(9 * em);
    ImGui::Combo("##ectx", &st.explainContext, ctxNames, 3);
    ImGui::SameLine();
    if (ImGui::Button("Explain")) st.explainLines = m.explain(st.explainType, st.explainContext);
    if (fonts().mono) ImGui::PushFont(fonts().mono, 0.0f);
    for (const std::string& line : st.explainLines) ImGui::TextWrapped("%s", line.c_str());
    if (fonts().mono) ImGui::PopFont();
}

void drawPacks(UiModel& m, UiState& st)
{
    const Palette& p = pal();
    const float em = ImGui::GetFontSize();
    ImGui::BeginDisabled(!m.onRescan || m.scanning);
    if (ImGui::Button(m.scanning ? "Scanning..." : "Rescan", ImVec2(7 * em, 0)) && m.onRescan) m.onRescan();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(14 * em);
    ImGui::InputTextWithHint("##pf", "filter files", st.packFilter, sizeof(st.packFilter));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m.packsDir.c_str());
    if (!m.library) {
        ImGui::TextDisabled(m.scanning ? "Scanning Airline Packs..." : "No scan yet (load a ToLiss aircraft).");
        return;
    }
    const PackLibrary& lib = *m.library;
    ImGui::Text("%d packs, %d sounds", static_cast<int>(lib.packs.size()), static_cast<int>(lib.soundCount()));
    if (!m.previewing.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop preview") && m.onStopPreview) m.onStopPreview();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m.previewing.c_str());
    }
    drawExplain(m, st);
    if (!lib.warnings.empty() && ImGui::TreeNode("##libw", "Scan warnings (%d)", static_cast<int>(lib.warnings.size()))) {
        for (const std::string& w : lib.warnings) ImGui::TextColored(vec(p.warn), "%s", w.c_str());
        ImGui::TreePop();
    }
    for (const Pack& pk : lib.packs) {
        const bool active = pk.name == m.pack;
        char label[256];
        std::snprintf(label, sizeof(label), "%s%s  (%d sounds%s%s%s)###pk_%s", pk.name.c_str(), active ? "  [active]" : "",
                      static_cast<int>(pk.files.size()), pk.maxVariant > 0 ? ", variants 0-" : "",
                      pk.maxVariant > 0 ? std::to_string(pk.maxVariant).c_str() : "", pk.mappingOnly ? ", mapping only" : "",
                      pk.root.c_str());
        if (active) ImGui::PushStyleColor(ImGuiCol_Text, p.accent);
        const bool open = ImGui::TreeNodeEx(label, active ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (active) ImGui::PopStyleColor();
        if (!open) continue;
        if (!pk.wrapper.empty()) ImGui::TextDisabled("inside wrapper folder '%s'", pk.wrapper.c_str());
        if (ImGui::BeginTable("##files", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("##play", ImGuiTableColumnFlags_WidthFixed, 2.2f * em);
            ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Announcement", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Tags", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 6 * em);
            ImGui::TableHeadersRow();
            for (const SoundFile& f : pk.files) {
                if (st.packFilter[0] && !icontains(f.relPath, st.packFilter)) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(f.path.c_str());
                if (ImGui::SmallButton("Play") && m.onPreview) m.onPreview(f.path, pk.name + "/" + f.relPath);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(f.relPath.c_str());
                ImGui::TableSetColumnIndex(2);
                if (f.type) {
                    ImGui::TextUnformatted(annTypeName(*f.type));
                    if (f.context != CallContext::Any) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%s)", contextName(f.context));
                    }
                } else {
                    ImGui::TextColored(vec(p.dim), "custom: not played");
                }
                ImGui::TableSetColumnIndex(3);
                std::string tags;
                for (const std::string& t : f.rawTags) tags += "[" + t + "]";
                ImGui::TextUnformatted(tags.empty() ? "-" : tags.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(f.type ? modeName(f.effectiveMode()) : "-");
            }
            ImGui::EndTable();
        }
        if (!pk.warnings.empty() && ImGui::TreeNode("##pkw", "Warnings (%d)", static_cast<int>(pk.warnings.size()))) {
            for (const std::string& w : pk.warnings) ImGui::TextColored(vec(p.warn), "%s", w.c_str());
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
}

void drawProbe(UiModel& m)
{
    const Palette& p = pal();
    const float em = ImGui::GetFontSize();
    int done = 0;
    for (const auto& r : m.probe) done += r.done ? 1 : 0;
    char title[96];
    std::snprintf(title, sizeof(title), "Probe checklist (%d of %d seen)###probe", done, static_cast<int>(m.probe.size()));
    if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextWrapped("Fly with this open: items tick themselves as FXAnnounce sees the values. Steps need you to do "
                       "something after Start. Apply changes FXAnnounce's own settings only, never ToLiss.");
    if (ImGui::Button("Write probe summary") && m.onProbeSummary) m.onProbeSummary();
    ImGui::SetItemTooltip("Writes the findings to FXAnnounce.log as a table for docs/TOLISS_DATAREFS.md.");
    ImGui::SameLine();
    if (ImGui::Button("Reset probe") && m.onProbeReset) m.onProbeReset();
    if (ImGui::BeginTable("##probe", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("##ok", ImGuiTableColumnFlags_WidthFixed, 1.5f * em);
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Seen", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, 5.5f * em);
        for (const auto& r : m.probe) {
            ImGui::PushID(r.id.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(vec(r.done ? p.ok : p.dim), "%s", r.done ? "ok" : "-");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", r.title.c_str());
            if (r.step && !r.instruction.empty()) HelpMarker(r.instruction.c_str());
            ImGui::TableSetColumnIndex(2);
            if (r.id == m.probeStep)
                ImGui::TextColored(vec(p.warn), "watching... %.0f s left", static_cast<double>(m.probeStepLeft));
            else
                ImGui::TextWrapped("%s", r.finding.empty() ? "-" : r.finding.c_str());
            ImGui::TableSetColumnIndex(3);
            if (r.step) {
                ImGui::BeginDisabled(!m.probeStep.empty());
                if (ImGui::SmallButton("Start") && m.onProbeStart) m.onProbeStart(r.id);
                ImGui::EndDisabled();
            }
            if (!r.applyKey.empty()) {
                if (r.step) ImGui::SameLine();
                if (ImGui::SmallButton("Apply") && m.onProbeApply) m.onProbeApply(r.applyKey, r.applyValue);
                ImGui::SetItemTooltip("Set %s = %s in FXAnnounce.ini", r.applyKey.c_str(), r.applyValue.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    Heading("Dataref census", "Snapshot every AirbusFBW/, toliss_airbus/, ckpt/ and sbh/ dataref, do something in the "
                              "cockpit, then press What changed? to see which ones moved.");
    if (ImGui::Button("Snapshot") && m.onCensusSnapshot) m.onCensusSnapshot();
    ImGui::SameLine();
    ImGui::BeginDisabled(m.censusSize == 0);
    if (ImGui::Button("What changed?") && m.onCensusDiff) m.onCensusDiff();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (m.censusSize > 0) ImGui::TextDisabled("%d datarefs in the snapshot", m.censusSize);
    else ImGui::TextDisabled("no snapshot");
    if (!m.censusChanged.empty()) {
        if (fonts().mono) ImGui::PushFont(fonts().mono, 0.0f);
        const size_t shown = std::min<size_t>(m.censusChanged.size(), 60);
        for (size_t k = 0; k < shown; ++k) ImGui::BulletText("%s", m.censusChanged[k].c_str());
        if (fonts().mono) ImGui::PopFont();
        if (m.censusChanged.size() > shown)
            ImGui::TextDisabled("... and %d more (all in FXAnnounce.log)", static_cast<int>(m.censusChanged.size() - shown));
    }
}

void drawInspector(UiModel& m, UiState& st)
{
    if (!m.probe.empty()) {
        drawProbe(m);
        ImGui::Spacing();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    ImGui::InputTextWithHint("##if", "filter datarefs", st.inspectFilter, sizeof(st.inspectFilter));
    HelpMarker("Raw values of every dataref FXAnnounce reads. Use this with docs/TOLISS_DATAREFS.md to verify what each "
               "value means (doors, ACP knobs, signs, FMGS phase).");
    if (m.inspector.empty()) {
        ImGui::TextDisabled("No ToLiss aircraft.");
        return;
    }
    if (BeginKVTable("##insp", 20.0f)) {
        if (fonts().mono) ImGui::PushFont(fonts().mono, 0.0f);
        for (const auto& [k, v] : m.inspector)
            if (!st.inspectFilter[0] || icontains(k, st.inspectFilter)) KV(k.c_str(), "%s", v.c_str());
        if (fonts().mono) ImGui::PopFont();
        EndKVTable();
    }
}

bool settingRow(const SettingDef& d, Settings& s)
{
    bool changed = false;
    ImGui::PushID(d.key);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(d.label);
    if (d.help && *d.help) HelpMarker(d.help);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    switch (d.type) {
    case SettingType::Bool: changed = ImGui::Checkbox("##v", &(s.*d.b)); break;
    case SettingType::Int:
        if (!d.choiceLabels.empty()) {
            const int n = static_cast<int>(d.choiceLabels.size());
            int v = std::clamp(s.*d.i, 0, n - 1);
            if (ImGui::BeginCombo("##v", d.choiceLabels[static_cast<size_t>(v)])) {
                for (int k = 0; k < n; ++k)
                    if (ImGui::Selectable(d.choiceLabels[static_cast<size_t>(k)], k == v)) {
                        s.*d.i = k;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
        } else {
            changed = ImGui::SliderInt("##v", &(s.*d.i), static_cast<int>(d.min), static_cast<int>(d.max));
        }
        break;
    case SettingType::Float: changed = ImGui::SliderFloat("##v", &(s.*d.f), d.min, d.max, "%.2f"); break;
    case SettingType::String: {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s", (s.*d.s).c_str());
        if (ImGui::InputText("##v", buf, sizeof(buf))) {
            s.*d.s = buf;
            changed = true;
        }
        break;
    }
    }
    ImGui::PopID();
    return changed;
}

void drawSettings(UiModel& m, UiState& st)
{
    if (!m.settings) {
        ImGui::TextDisabled("Settings unavailable.");
        return;
    }
    if (!st.pendingValid) {
        st.pending = *m.settings;
        st.pendingValid = true;
    }
    const char* sections[] = { "general", "cabin", "boarding", "hearing", "tags", "timing", "toliss", "ui" };
    const char* titles[] = { "General", "Cabin", "Boarding (SimBrief schedule)", "Hearing", "Tags", "Timing", "ToLiss inputs", "Display" };
    constexpr int kSections = 8;
    for (int k = 0; k < kSections; ++k) {
        const bool open = ImGui::CollapsingHeader(titles[k], k < 2 || k == 3 ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (!open) continue;
        if (ImGui::BeginTable(sections[k], 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 16.0f);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
            for (const SettingDef& d : settingDefs())
                if (!d.hidden && std::strcmp(d.section, sections[k]) == 0) settingRow(d, st.pending);
            if (k == kSections - 1) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Text size");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderFloat("##fontscale", &st.pending.fontScale, 0.6f, 2.0f, "%.2f");
            }
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader("Announcements")) {
        ImGui::TextDisabled("Untick an announcement to never play it (the sequence still continues).");
        if (ImGui::BeginTable("##events", 3, ImGuiTableFlags_SizingStretchSame)) {
            for (int k = 0; k < kAnnTypeCount; ++k) {
                ImGui::TableNextColumn();
                const std::string name = annTypeName(static_cast<AnnType>(k));
                bool on = st.pending.eventEnabled(name);
                if (ImGui::Checkbox(name.c_str(), &on)) {
                    for (auto it = st.pending.disabledEvents.begin(); it != st.pending.disabledEvents.end();)
                        it = iequals(*it, name) ? st.pending.disabledEvents.erase(it) : std::next(it);
                    if (!on) st.pending.disabledEvents.insert(name);
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::Spacing();
    const float em = ImGui::GetFontSize();
    if (ImGui::Button("Apply", ImVec2(7 * em, 0))) {
        st.pending.fontScale = std::clamp(st.pending.fontScale, 0.6f, 2.0f);
        // Window geometry and tab are owned by the window, not by this form.
        st.pending.windowVisible = m.settings->windowVisible;
        st.pending.winLeft = m.settings->winLeft;
        st.pending.winTop = m.settings->winTop;
        st.pending.winRight = m.settings->winRight;
        st.pending.winBottom = m.settings->winBottom;
        st.pending.tab = m.settings->tab;
        *m.settings = st.pending;
        if (m.onSettingsChanged) m.onSettingsChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert", ImVec2(7 * em, 0))) st.pending = *m.settings;
    ImGui::SameLine();
    if (ImGui::Button("Defaults", ImVec2(7 * em, 0))) {
        const Settings keep = st.pending;
        st.pending = Settings{};
        st.pending.disabledEvents.clear();
        st.pending.fontScale = keep.fontScale;
    }
}

void drawLog(UiModel& m, UiState& st)
{
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    ImGui::InputTextWithHint("##filter", "filter", st.logFilter, sizeof(st.logFilter));
    ImGui::SameLine();
    ImGui::TextDisabled("%d lines", static_cast<int>(m.log.size()));
    if (ImGui::BeginChild("##logtext", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        if (fonts().mono) ImGui::PushFont(fonts().mono, 0.0f);
        const Palette& p = pal();
        for (const std::string& line : m.log) {
            if (st.logFilter[0] && !icontains(line, st.logFilter)) continue;
            const bool err = line.find(" ERROR ") != std::string::npos;
            const bool warn = line.find(" WARN ") != std::string::npos;
            if (err || warn) ImGui::PushStyleColor(ImGuiCol_Text, err ? p.bad : p.warn);
            ImGui::TextUnformatted(line.c_str());
            if (err || warn) ImGui::PopStyleColor();
        }
        if (fonts().mono) ImGui::PopFont();
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

} // namespace

const char* tabName(int tab) { return (tab >= 0 && tab < kTabCount) ? kTabNames[tab] : "?"; }

void drawPanel(UiModel& m, UiState& s)
{
    drawHeader(m);
    ImGui::Spacing();
    s.tab = std::clamp(s.tab, 0, kTabCount - 1);
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        const int saved = s.tab;
        for (int t = 0; t < kTabCount; ++t) {
            const ImGuiTabItemFlags fl = (!s.tabRestored && t == saved) ? ImGuiTabItemFlags_SetSelected : 0;
            if (!ImGui::BeginTabItem(kTabNames[t], nullptr, fl)) continue;
            s.tab = t;
            if (ImGui::BeginChild("##body", ImVec2(0, 0), ImGuiChildFlags_None)) {
                switch (t) {
                case TAB_STATUS: drawStatus(m); break;
                case TAB_TIMELINE: drawTimeline(m); break;
                case TAB_PACKS: drawPacks(m, s); break;
                case TAB_SETTINGS: drawSettings(m, s); break;
                case TAB_INSPECTOR: drawInspector(m, s); break;
                case TAB_LOG: drawLog(m, s); break;
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        s.tabRestored = true;
        ImGui::EndTabBar();
    }
}

} // namespace fxa::ui
