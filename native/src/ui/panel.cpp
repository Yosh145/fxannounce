#include "ui/panel.h"

#include <imgui.h>

#include <algorithm>

#include "ui/tabs.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fxa::ui {

namespace {

// tinted pill readable in both themes
void stateChip(const char* text, ImU32 color) {
  Chip(text, withAlpha(color, 0.18f), color);
}

void drawHeader(UiModel& m) {
  const Palette& p = pal();
  const ImGuiStyle& style = ImGui::GetStyle();
  pushFont(fonts().bold, 1.15f);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("FXAnnounce");
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s", m.version.c_str());
  ImGui::SameLine();
  const float chipY = ImGui::GetCursorPosY();
  ImGui::SetCursorPosY(
      chipY + (ImGui::GetFrameHeight() - ImGui::GetFontSize() * 1.4f) * 0.5f);
  if (m.safeMode)
    stateChip("SAFE MODE", p.bad);
  else if (!m.active)
    stateChip("UNSUPPORTED", p.dim);
  else if (m.muted)
    stateChip("MUTED", p.warn);
  else
    stateChip("ACTIVE", p.ok);
  if (m.muted && !m.safeMode)
    Tooltip(
        "Announcements are muted. Unmute on the Packs tab, from the menu or "
        "with fxannounce/mute_toggle.");

  // timeline checkbox white/dark switch wraps under when narrow
  if (m.settings) {
    Settings& s = *m.settings;
    const float em = ImGui::GetFontSize();
    const float sp = style.ItemSpacing.x;
    const float tlW = ImGui::CalcTextSize("Timeline").x + sp +
                      ImGui::CalcTextSize("(?)").x + sp +
                      ImGui::GetFrameHeight();
    const float swW = ImGui::CalcTextSize("White").x + sp + em * 2.1f + sp +
                      ImGui::CalcTextSize("Dark").x;
    const float total = tlW + em * 1.5f + swW;
    ImGui::SameLine();
    const float right =
        ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetContentRegionAvail().x < total) ImGui::NewLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - total));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Timeline");
    HelpMarker(
        "Shows the Timeline tab: what comes next and what has played on this "
        "flight.");
    ImGui::SameLine();
    if (ImGui::Checkbox("##timeline", &s.showTimeline)) settingsChanged(m);
    ImGui::SameLine(0, em * 1.5f);
    bool dark = s.theme == static_cast<int>(Theme::Dark);
    if (PillSwitch("theme", "White", "Dark", &dark)) {
      s.theme = static_cast<int>(dark ? Theme::Dark : Theme::White);
      settingsChanged(m);
    }
  }

  if (m.safeMode) {
    ImGui::Spacing();
    BeginCard("##safe", 0, p.bad);
    ImGui::PushStyleColor(ImGuiCol_Text, p.cardBad);
    ImGui::TextWrapped(
        "FXAnnounce stopped itself after repeated faults. The simulator is not "
        "affected.");
    ImGui::PopStyleColor();
    ImGui::TextDisabled("Last fault: %s", m.lastFault.c_str());
    if (m.onReenable && ImGui::Button("Re-enable")) m.onReenable();
    EndCard();
  }
}

}  // namespace

const char* tabName(int tab) {
  switch (tab) {
    case TAB_STATUS:
      return "Status";
    case TAB_TIMELINE:
      return "Timeline";
    case TAB_PACKS:
      return "Packs";
    case TAB_SETTINGS:
      return "Settings";
    case TAB_LOG:
      return "Log";
    default:
      return "?";
  }
}

bool tabVisible(int tab, const Settings* s) {
  switch (tab) {
    case TAB_STATUS:
    case TAB_PACKS:
    case TAB_SETTINGS:
      return true;
    case TAB_TIMELINE:
      return !s || s->showTimeline;
    case TAB_LOG:
      return s && s->showLog;
    default:
      return false;
  }
}

void drawPanel(UiModel& m, UiState& s) {
  drawHeader(m);
  ImGui::Spacing();
  if (!tabVisible(s.tab, m.settings)) s.tab = TAB_STATUS;
  if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyScroll |
                                       ImGuiTabBarFlags_DrawSelectedOverline)) {
    const int saved = s.tab;
    for (int t : kTabIds) {
      if (!tabVisible(t, m.settings)) continue;
      const ImGuiTabItemFlags fl =
          (!s.tabRestored && t == saved) ? ImGuiTabItemFlags_SetSelected : 0;
      if (!ImGui::BeginTabItem(tabName(t), nullptr, fl)) continue;
      s.tab = t;
      ImGui::Spacing();
      if (ImGui::BeginChild("##body", ImVec2(0, 0), ImGuiChildFlags_None)) {
        switch (t) {
          case TAB_STATUS:
            drawStatus(m, s);
            break;
          case TAB_TIMELINE:
            drawTimeline(m, s);
            break;
          case TAB_PACKS:
            drawPacks(m, s);
            break;
          case TAB_SETTINGS:
            drawSettings(m, s);
            break;
          case TAB_LOG:
            drawLog(m, s);
            break;
          default:
            break;
        }
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    s.tabRestored = true;
    ImGui::EndTabBar();
  }
}

}  // namespace fxa::ui
