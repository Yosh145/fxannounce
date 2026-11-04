// log tab settings advanced show log live inputs recent log repeats collapse w
// a count fxannounce.log keeps everything
#include <imgui.h>

#include <string>

#include "core/strings.h"
#include "ui/tabs.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fxa::ui {

void drawLog(UiModel& m, UiState& st) {
  const Palette& p = pal();
  const float em = ImGui::GetFontSize();
  if (ImGui::CollapsingHeader("Live inputs")) {
    if (m.liveInputs.empty()) {
      ImGui::TextDisabled(
          "No supported aircraft: nothing is read from the sim.");
    } else if (BeginKVTable("##inputs", 11.0f)) {
      for (const auto& [k, v] : m.liveInputs) KV(k.c_str(), "%s", v.c_str());
      EndKVTable();
    }
    ImGui::Spacing();
  }
  ImGui::SetNextItemWidth(em * 16.0f);
  ImGui::InputTextWithHint("##filter", "filter", st.logFilter,
                           sizeof(st.logFilter));
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%d entries", static_cast<int>(m.log.size()));
  HelpMarker(
      "The newest entries. A line that comes again moves to the bottom with a "
      "count (x5) instead of a new row. "
      "FXAnnounce.log in X-Plane's Output folder has every line. Settings > "
      "Advanced > Log detail sets how "
      "much is written.");
  ImGui::PushStyleColor(ImGuiCol_ChildBg, p.panel);
  if (ImGui::BeginChild("##logtext", ImVec2(0, 0),
                        ImGuiChildFlags_AlwaysUseWindowPadding)) {
    pushFont(fonts().mono);
    for (const LogLine& l : m.log) {
      if (st.logFilter[0] && !icontains(l.msg, st.logFilter)) continue;
      const ImU32 c = l.level == LogLevel::Error  ? p.bad
                      : l.level == LogLevel::Warn ? p.warn
                                                  : 0;
      ImGui::TextDisabled("%s", l.time.c_str());
      ImGui::SameLine();
      ImGui::TextColored(vec(c ? c : p.dim), "%-5s", logLevelName(l.level));
      ImGui::SameLine();
      if (c) ImGui::PushStyleColor(ImGuiCol_Text, c);
      if (l.count > 1)
        ImGui::TextWrapped("%s  (x%d)", l.msg.c_str(), l.count);
      else
        ImGui::TextWrapped("%s", l.msg.c_str());
      if (c) ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
      ImGui::SetScrollHereY(1.0f);
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
}

}  // namespace fxa::ui
