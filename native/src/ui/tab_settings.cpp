// settings sidebar pages instant apply checks/combos on click sliders/text on
// release edited live so you hear volume while dragging
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "core/announcement.h"
#include "core/strings.h"
#include "ui/tabs.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fxa::ui {

namespace {

// shown as pct
bool isLevel(const char* key) {
  for (const char* k :
       {"master_volume", "pa_volume", "music_volume", "interphone_volume",
        "chime_volume", "exterior_gain", "floor", "door_leak"})
    if (std::strcmp(key, k) == 0) return true;
  return false;
}

void labelCell(const char* label, const char* help) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  ImGui::TextWrapped("%s", label);
  if (help && *help) HelpMarker(help);
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
}

// true apply and save now
bool settingRow(const SettingDef& d, Settings& s) {
  bool commit = false;
  ImGui::PushID(d.key);
  labelCell(d.label, d.help);
  switch (d.type) {
    case SettingType::Bool:
      commit = ImGui::Checkbox("##v", &(s.*d.b));
      break;
    case SettingType::Int:
      if (!d.choiceLabels.empty()) {
        const int n = static_cast<int>(d.choiceLabels.size());
        const int v = std::clamp(s.*d.i, 0, n - 1);
        if (ImGui::BeginCombo("##v", d.choiceLabels[static_cast<size_t>(v)])) {
          for (int k = 0; k < n; ++k)
            if (ImGui::Selectable(d.choiceLabels[static_cast<size_t>(k)],
                                  k == v)) {
              s.*d.i = k;
              commit = true;
            }
          ImGui::EndCombo();
        }
      } else {
        ImGui::SliderInt("##v", &(s.*d.i), static_cast<int>(d.min),
                         static_cast<int>(d.max));
        commit = ImGui::IsItemDeactivatedAfterEdit();
      }
      break;
    case SettingType::Float:
      if (isLevel(d.key)) {
        float pct = s.*d.f * 100.0f;
        if (ImGui::SliderFloat("##v", &pct, d.min * 100.0f, d.max * 100.0f,
                               "%.0f%%"))
          s.*d.f = pct / 100.0f;
      } else {
        ImGui::SliderFloat("##v", &(s.*d.f), d.min, d.max, "%.2f");
      }
      commit = ImGui::IsItemDeactivatedAfterEdit();
      break;
    case SettingType::String: {
      char buf[512];
      std::snprintf(buf, sizeof(buf), "%s", (s.*d.s).c_str());
      if (ImGui::InputText("##v", buf, sizeof(buf))) s.*d.s = buf;
      commit = ImGui::IsItemDeactivatedAfterEdit();
      break;
    }
  }
  ImGui::PopID();
  return commit;
}

bool beginRows(const char* id) {
  if (!ImGui::BeginTable(
          id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    return false;
  ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 1.0f);
  ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 1.1f);
  return true;
}

void thisFlight(UiModel& m) {
  Heading("This flight");
  if (!beginRows("##flight")) return;
  labelCell("This flight is overwater",
            "Allows [Overwater] announcements on this flight. Each new flight "
            "starts from the livery's airlinepack.cfg, "
            "or from 'Overwater by default' above. Also in the plugin menu and "
            "fxannounce/overwater_toggle.");
  bool on = m.overwater;
  ImGui::BeginDisabled(!m.active || !m.onToggleOverwater);
  if (ImGui::Checkbox("##overwater", &on) && m.onToggleOverwater)
    m.onToggleOverwater();
  ImGui::EndDisabled();
  ImGui::EndTable();
}

void announcementList(UiModel& m, Settings& s) {
  Heading("Announcements to play",
          "Untick one to never play it; the sequence "
          "still continues. Hover a name for its "
          "Fenix file name.");
  if (!ImGui::BeginTable("##events", 2, ImGuiTableFlags_SizingStretchSame))
    return;
  for (int k = 0; k < kAnnTypeCount; ++k) {
    ImGui::TableNextColumn();
    const AnnType t = static_cast<AnnType>(k);
    const std::string fenix = annTypeName(t);
    bool on = s.eventEnabled(fenix);
    if (ImGui::Checkbox(annTypeLabel(t), &on)) {
      for (auto it = s.disabledEvents.begin(); it != s.disabledEvents.end();)
        it = iequals(*it, fenix) ? s.disabledEvents.erase(it) : std::next(it);
      if (!on) s.disabledEvents.insert(fenix);
      settingsChanged(m);
    }
    Tooltip(fenix.c_str());
  }
  ImGui::EndTable();
}

void sidebar(UiState& st) {
  const Palette& p = pal();
  const float em = ImGui::GetFontSize();
  ImGui::PushStyleColor(ImGuiCol_Header, p.chrome);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, p.fieldHover);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, p.chrome);
  for (int k = 0; k < kSettingPageCount; ++k) {
    const bool sel = st.settingsPage == k;
    ImGui::PushID(k);
    if (ImGui::Selectable("##pg", sel, 0, ImVec2(0, em * 1.7f)))
      st.settingsPage = k;
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    if (sel)
      ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + 3.0f, b.y),
                                                p.brand);
    const char* name = settingPageName(static_cast<SettingPage>(k));
    const ImVec2 ts = ImGui::CalcTextSize(name);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(a.x + em * 0.7f, a.y + (b.y - a.y - ts.y) * 0.5f),
        sel && p.dark ? IM_COL32_WHITE : (sel ? p.text : p.hdr), name);
    ImGui::PopID();
  }
  ImGui::PopStyleColor(3);
}

void resetFooter(UiModel& m) {
  const float em = ImGui::GetFontSize();
  if (ImGui::Button("Reset to defaults", ImVec2(em * 10, 0)))
    ImGui::OpenPopup("Reset settings?");
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("Changes apply at once.");
  if (ImGui::BeginPopupModal("Reset settings?", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Reset every setting to its default?");
    ImGui::TextDisabled("The window position, theme and text size are kept.");
    ImGui::Spacing();
    if (ImGui::Button("Reset", ImVec2(em * 7, 0))) {
      Settings& s = *m.settings;
      const Settings keep = s;
      s = Settings{};
      s.windowVisible = keep.windowVisible;
      s.winLeft = keep.winLeft;
      s.winTop = keep.winTop;
      s.winRight = keep.winRight;
      s.winBottom = keep.winBottom;
      s.tab = keep.tab;
      s.theme = keep.theme;
      s.fontScale = keep.fontScale;
      settingsChanged(m);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(em * 7, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

}  // namespace

void drawSettings(UiModel& m, UiState& st) {
  if (!m.settings) {
    ImGui::TextDisabled("Settings unavailable.");
    return;
  }
  Settings& s = *m.settings;
  const float em = ImGui::GetFontSize();
  st.settingsPage = std::clamp(st.settingsPage, 0, kSettingPageCount - 1);
  const float footer =
      ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

  ImGui::PushStyleColor(ImGuiCol_ChildBg, pal().panel);
  ImGui::BeginChild("##side", ImVec2(em * 10.0f, -footer),
                    ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleColor();
  sidebar(st);
  ImGui::EndChild();
  ImGui::SameLine(0, em);

  ImGui::BeginChild("##page", ImVec2(0, -footer));
  const SettingPage page = static_cast<SettingPage>(st.settingsPage);
  Heading(settingPageName(page));
  if (beginRows("##rows")) {
    for (const SettingDef& d : settingDefs())
      if (!d.hidden && d.page == page && settingRow(d, s)) settingsChanged(m);
    ImGui::EndTable();
  }
  if (page == SettingPage::Announcements) {
    thisFlight(m);
    announcementList(m, s);
  }
  if (page == SettingPage::Advanced) {
    ImGui::Spacing();
    ImGui::TextDisabled("Flight trace: %s", m.recorderPath.empty()
                                                ? "not recording"
                                                : m.recorderPath.c_str());
  }
  ImGui::EndChild();
  resetFooter(m);
}

}  // namespace fxa::ui
