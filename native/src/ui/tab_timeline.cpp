// timeline next one up front w play now rest of the list then history
#include <imgui.h>

#include <string>

#include "ui/tabs.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fxa::ui {

namespace {

std::string when(const UiModel::Pending& p) {
  if (p.inSec > 0) return "in " + mmssText(p.inSec);
  if (p.inSec == 0) return "now";
  return p.condition;
}

void comingUp(UiModel& m) {
  const Palette& p = pal();
  const float em = ImGui::GetFontSize();
  BeginCard("##next");
  CardTitle("COMING UP");
  if (m.pending.empty()) {
    BigText("Nothing pending", 1.2f, p.cardDim);
    if (!m.active) ImGui::TextDisabled("Load a ToLiss A319, A320 or A321.");
    EndCard();
    return;
  }
  const UiModel::Pending& n = m.pending.front();
  const float buttonW = em * 6.5f;
  if (ImGui::BeginTable("##nx", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("btn", ImGuiTableColumnFlags_WidthFixed, buttonW);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    BigText(n.name.c_str());
    if (!n.fenix.empty()) Tooltip(n.fenix.c_str());
    if (n.inSec >= 0) {
      pushFont(fonts().mono, 1.2f);
      ImGui::TextColored(vec(p.cardOk), "%s", when(n).c_str());
      ImGui::PopFont();
      if (!n.note.empty()) ImGui::TextDisabled("%s", n.note.c_str());
    } else {
      ImGui::TextWrapped("%s", n.condition.c_str());
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::Dummy(ImVec2(0, em * 0.4f));
    ImGui::BeginDisabled(!m.active || !m.onAdvance);
    if (ImGui::Button("Play now", ImVec2(buttonW, 0)) && m.onAdvance)
      m.onAdvance();
    ImGui::EndDisabled();
    Tooltip(
        "Plays the next announcement now instead of waiting for it "
        "(fxannounce/advance).");
    ImGui::EndTable();
  }
  EndCard();
}

void laterList(UiModel& m) {
  if (m.pending.size() < 2) return;
  const float em = ImGui::GetFontSize();
  ImGui::Spacing();
  if (ImGui::BeginTable(
          "##later", 2,
          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("when", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    for (size_t k = 1; k < m.pending.size(); ++k) {
      const UiModel::Pending& n = m.pending[k];
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + em * 0.5f);
      ImGui::TextWrapped("%s", n.name.c_str());
      if (!n.fenix.empty()) Tooltip(n.fenix.c_str());
      ImGui::TableSetColumnIndex(1);
      if (n.inSec >= 0) {
        pushFont(fonts().mono);
        ImGui::TextDisabled("%s", when(n).c_str());
        ImGui::PopFont();
        if (!n.note.empty()) {
          ImGui::PushStyleColor(ImGuiCol_Text, pal().dim);
          ImGui::TextWrapped("%s", n.note.c_str());
          ImGui::PopStyleColor();
        }
      } else {
        ImGui::PushStyleColor(ImGuiCol_Text, pal().dim);
        ImGui::TextWrapped("%s", n.condition.c_str());
        ImGui::PopStyleColor();
      }
    }
    ImGui::EndTable();
  }
}

void history(UiModel& m) {
  const Palette& p = pal();
  const float em = ImGui::GetFontSize();
  Heading("History",
          "Newest first. Hover a time for the local time, a name "
          "for its Fenix file name, and an outcome for "
          "the file that played or the reason it didn't. 'no sound "
          "in pack' means the active pack has no file "
          "for that call (Fenix skips it).");
  if (m.events.empty()) {
    ImGui::TextDisabled("Nothing has played on this flight yet.");
    return;
  }
  if (ImGui::BeginTable(
          "##events", 3,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed,
                            5.0f * em);
    ImGui::TableSetupColumn("Announcement", ImGuiTableColumnFlags_WidthStretch,
                            2.0f);
    ImGui::TableSetupColumn("Outcome", ImGuiTableColumnFlags_WidthStretch,
                            1.4f);
    ImGui::TableHeadersRow();
    for (size_t k = 0; k < m.events.size(); ++k) {
      const UiModel::EventRow& e = m.events[k];
      ImGui::PushID(static_cast<int>(k));
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      TextZulu(e.zuluSec, e.localSec, p.dim);
      ImGui::TableSetColumnIndex(1);
      ImGui::TextWrapped("%s", e.name.c_str());
      Tooltip(e.fenix.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::TextColored(
          vec(e.tone == 0 ? p.ok : (e.tone == 1 ? p.warn : p.dim)), "%s",
          e.outcome.c_str());
      Tooltip(e.detail.c_str());
      if (e.tone != 0 && !e.detail.empty()) HelpMarker(e.detail.c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

}  // namespace

void drawTimeline(UiModel& m, UiState&) {
  comingUp(m);
  laterList(m);
  ImGui::Spacing();
  history(m);
}

}  // namespace fxa::ui
