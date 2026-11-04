// status cabin flight side by side stacked when narrow recent coming up stretch
// to fill aircraft at the bottom dormant card when unsupported
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "ui/tabs.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fxa::ui {

namespace {

const char* kPlay = "\xE2\x96\xB6";   // play glyph
const char* kArrow = "\xE2\x86\x92";  // arrow
const char* kDot = "\xC2\xB7";        // middle dot

std::string when(const UiModel::Pending& p) {
  if (p.inSec > 0) return "in " + mmssText(p.inSec);
  if (p.inSec == 0) return "now";
  return p.condition;
}

void wrappedColored(const char* text, ImU32 color) {
  if (color) ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextWrapped("%s", text);
  if (color) ImGui::PopStyleColor();
}

void drawDormant(UiModel& m) {
  const Palette& p = pal();
  const float em = ImGui::GetFontSize();
  ImGui::Dummy(ImVec2(0, em));
  const float avail = ImGui::GetContentRegionAvail().x;
  const float w = std::min(avail, em * 34.0f);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w) * 0.5f);
  BeginCard("##dormant", 0, p.cardWarn, w);
  ImGui::Spacing();
  pushFont(fonts().bold, 1.4f);
  TextCentered("UNSUPPORTED AIRCRAFT", p.cardWarn);
  ImGui::PopFont();
  ImGui::Spacing();
  pushFont(fonts().bold, 1.1f);
  TextCentered(m.loadedAircraft.empty() ? "No aircraft detected yet"
                                        : m.loadedAircraft.c_str());
  ImGui::PopFont();
  ImGui::Spacing();
  TextCentered("FXAnnounce works with the ToLiss A319, A320 and A321.",
               p.cardDim);
  TextCentered("Packs and Settings stay available.", p.cardDim);
  ImGui::Spacing();
  EndCard();
}

float cabinCard(UiModel& m, float minH) {
  const Palette& p = pal();
  BeginCard("##cabin", minH);
  CardTitle("CABIN");
  BigText(m.stage.empty() ? "-" : m.stage.c_str());
  if (!m.playing.empty()) {
    wrappedColored((std::string(kPlay) + " " + m.playing).c_str(), p.cardOk);
    Tooltip(m.playingFenix.c_str());
  } else {
    ImGui::TextDisabled("Nothing playing");
  }
  if (m.boardingFrac >= 0) {
    ImGui::Spacing();
    Progress(m.boardingFrac, p.cardButton, p.accent);
    char buf[96];
    std::snprintf(
        buf, sizeof(buf), "%s%d %%  %s  complete in %s",
        m.boardingPax > 0
            ? (std::to_string(m.boardingPax) + " pax  " + kDot + "  ").c_str()
            : "",
        static_cast<int>(m.boardingFrac * 100.0f + 0.5f), kDot,
        mmssText(m.boardingLeftSec).c_str());
    wrappedColored(buf, p.cardDim);
    Tooltip(
        "Boarding takes a modeled time for the passengers on board (with a "
        "SimBrief plan, it ends about 10 min "
        "before the ETD). Beacon, pushback, engine start or a cabin call "
        "end it sooner.");
  }
  if (!m.boardingNote.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, p.cardWarn);
    ImGui::TextWrapped("%s", m.boardingNote.c_str());
    ImGui::PopStyleColor();
  }
  return EndCard();
}

float flightCard(UiModel& m, float minH) {
  const Palette& p = pal();
  const UiModel::Flight& f = m.flight;
  BeginCard("##flight", minH);
  CardTitle("FLIGHT");
  if (f.state == 0) {
    BigText("No SimBrief plan", 1.2f, p.cardDim);
    if (!f.reason.empty()) ImGui::TextWrapped("%s", f.reason.c_str());
    return EndCard();
  }
  std::string head =
      f.callsign.empty() ? std::string("SimBrief plan") : f.callsign;
  if (!f.airline.empty()) head = f.airline + " " + kDot + " " + head;
  BigText(head.c_str());
  const char* from = f.origin.empty() ? "----" : f.origin.c_str();
  const char* to = f.destination.empty() ? "----" : f.destination.c_str();
  if (f.routeFrac >= 0) {
    RouteBar(from, to, f.routeFrac);
    if (f.toGoNm >= 0 && f.line.find(" NM") == std::string::npos) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.0f NM to go", f.toGoNm);
      wrappedColored(buf, p.cardDim);
      Tooltip("Great-circle distance to the destination airport.");
    }
  } else if (!f.origin.empty() || !f.destination.empty()) {
    pushFont(fonts().mono, 1.1f);
    ImGui::Text("%s %s %s", from, kArrow, to);
    ImGui::PopFont();
  }
  if (!f.etd.empty()) {
    ImGui::TextDisabled("ETD");
    ImGui::SameLine();
    ImGui::TextUnformatted(f.etd.c_str());
    if (!f.boarding.empty()) {
      const float need = ImGui::GetFontSize() +
                         ImGui::CalcTextSize("Boarding").x +
                         ImGui::GetStyle().ItemSpacing.x +
                         ImGui::CalcTextSize(f.boarding.c_str()).x;
      ImGui::SameLine(0, ImGui::GetFontSize());
      if (ImGui::GetContentRegionAvail().x < need)
        ImGui::NewLine();  // no room own line
      ImGui::TextDisabled("Boarding");
      ImGui::SameLine();
      ImGui::TextUnformatted(f.boarding.c_str());
    }
  }
  if (!f.line.empty()) ImGui::TextWrapped("%s", f.line.c_str());
  if (f.state == 2 && !f.reason.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, p.cardWarn);
    ImGui::TextWrapped("%s", f.reason.c_str());
    ImGui::PopStyleColor();
  }
  return EndCard();
}

bool beginRows(const char* id) {
  if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
    return false;
  ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch, 1.25f);
  ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch, 1.0f);
  return true;
}

float wrapHeight(const std::string& text, float width) {
  return ImGui::CalcTextSize(text.c_str(), nullptr, false,
                             std::max(1.0f, width))
      .y;
}

// rows that fit below the cursor in a card minh tall 0 no limit
int fitRows(const std::vector<float>& heights, float minH, int minRows,
            int maxRows) {
  const int n = std::min(maxRows, static_cast<int>(heights.size()));
  if (minH <= 0) return n;
  const float limit = minH - ImGui::GetFontSize() * 0.8f -
                      ImGui::GetCursorPosY();  // bottom padding
  float used = 0;
  int k = 0;
  for (; k < n; ++k) {
    if (k >= minRows && used + heights[static_cast<size_t>(k)] > limit) break;
    used += heights[static_cast<size_t>(k)];
  }
  return k;
}

std::string recentText(const UiModel::EventRow& e) {
  return e.tone == 0 ? e.name : e.name + "  " + kDot + "  " + e.outcome;
}

float recentCard(UiModel& m, int maxRows, float minH) {
  const Palette& p = pal();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float em = ImGui::GetFontSize();
  BeginCard("##recent", minH);
  CardTitle("RECENT");
  if (m.events.empty()) {
    wrappedColored("Nothing has played on this flight yet.", pal().cardDim);
    return EndCard();
  }
  // rows wrap show what fits
  const float timeW = em * 4.2f;
  const float textW =
      ImGui::GetContentRegionAvail().x - timeW - style.CellPadding.x * 4.0f;
  std::vector<float> heights;
  for (size_t k = 0; k < m.events.size() && k < 8; ++k)
    heights.push_back(std::max(ImGui::GetTextLineHeight(),
                               wrapHeight(recentText(m.events[k]), textW)) +
                      style.CellPadding.y * 2.0f);
  const int rows = fitRows(heights, minH, 1, maxRows);
  if (ImGui::BeginTable(
          "##rc", 2,
          ImGuiTableFlags_SizingStretchProp)) {  // padding comes from
                                                 // drawstatus
    ImGui::TableSetupColumn("t", ImGuiTableColumnFlags_WidthFixed, timeW);
    ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch);
    for (int k = 0; k < rows; ++k) {
      const UiModel::EventRow& e = m.events[static_cast<size_t>(k)];
      ImGui::PushID(k);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      TextZulu(e.zuluSec, e.localSec, p.cardDim);
      ImGui::TableSetColumnIndex(1);
      wrappedColored(recentText(e).c_str(),
                     e.tone == 0 ? 0 : (e.tone == 1 ? p.cardWarn : p.cardDim));
      Tooltip((e.fenix + (e.detail.empty() ? "" : ": " + e.detail)).c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  return EndCard();
}

float comingCard(UiModel& m, int maxRows, float minH) {
  const Palette& p = pal();
  const ImGuiStyle& style = ImGui::GetStyle();
  BeginCard("##coming", minH);
  CardTitle("COMING UP");
  if (m.pending.empty()) {
    wrappedColored("Nothing pending.", pal().cardDim);
    return EndCard();
  }
  // name/when split 1.25 1 like beginrows
  const float inner =
      ImGui::GetContentRegionAvail().x - style.CellPadding.x * 4.0f;
  const float nameW = inner * 1.25f / 2.25f, whenW = inner / 2.25f;
  std::vector<float> heights;
  for (size_t k = 0; k < m.pending.size() && k < 8; ++k) {
    const UiModel::Pending& n = m.pending[k];
    const float h =
        std::max({ImGui::GetTextLineHeight(), wrapHeight(n.name, nameW),
                  n.inSec >= 0 ? 0.0f : wrapHeight(n.condition, whenW)});
    heights.push_back(h + style.CellPadding.y * 2.0f);
  }
  const int rows = fitRows(heights, minH, 2, maxRows);
  if (beginRows("##cm")) {
    for (int k = 0; k < rows; ++k) {
      const UiModel::Pending& n = m.pending[static_cast<size_t>(k)];
      ImGui::PushID(k);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextWrapped("%s", n.name.c_str());
      if (!n.fenix.empty()) Tooltip(n.fenix.c_str());
      ImGui::TableSetColumnIndex(1);
      if (n.inSec >= 0) {
        pushFont(fonts().mono);
        wrappedColored(when(n).c_str(), p.cardOk);
        ImGui::PopFont();
        if (!n.note.empty()) Tooltip(n.note.c_str());
      } else {
        wrappedColored(n.condition.c_str(), p.cardDim);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  return EndCard();
}

float aircraftCard(UiModel& m) {
  BeginCard("##aircraft");
  if (ImGui::BeginTable("##ac", 3, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("p", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    const char* titles[] = {"AIRCRAFT", "LIVERY", "AIRLINE PACK"};
    const std::string* values[] = {&m.aircraft, &m.livery, &m.pack};
    ImGui::TableNextRow();
    for (int k = 0; k < 3; ++k) {
      ImGui::TableSetColumnIndex(k);
      CardTitle(titles[k]);
    }
    ImGui::TableNextRow();
    for (int k = 0; k < 3; ++k) {
      ImGui::TableSetColumnIndex(k);
      ImGui::TextWrapped("%s", values[k]->empty() ? "-" : values[k]->c_str());
      if (k == 2)
        Tooltip(
            "Why this pack was chosen is in FXAnnounce.log (and in the Log "
            "tab, Settings > Advanced > Show log).");
    }
    ImGui::EndTable();
  }
  return EndCard();
}

// 2 cards in a row equal height from last frame fillh stretch target
template <typename A, typename B>
void cardRow(bool wide, float& rowH, A left, B right, float fillH = 0) {
  if (wide) {
    const float em = ImGui::GetFontSize();
    const float h = std::max(rowH, fillH);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(em * 0.3f, 0));
    const bool open = ImGui::BeginTable(
        "##row", 2,
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX);
    ImGui::PopStyleVar();  // only for this table the cards tables use the
                           // callers
    if (open) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const float a = left(h);
      ImGui::TableSetColumnIndex(1);
      const float b = right(h);
      rowH = std::max(a, b);
      ImGui::EndTable();
    }
  } else {
    left(0.0f);
    ImGui::Spacing();
    right(0.0f);
    rowH = 0;
  }
}

}  // namespace

void drawStatus(UiModel& m, UiState& st) {
  if (!m.active) {
    drawDormant(m);
    return;
  }
  const Palette& p = pal();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float em = ImGui::GetFontSize();
  if (m.calling) {
    BeginCard("##calling", 0, p.cardWarn);
    BigText("CABIN CALLING", 1.2f, p.cardWarn);
    ImGui::TextWrapped("Pull CAB on the ACP to answer (attempt %d).",
                       std::max(1, m.callAttempt));
    EndCard();
    ImGui::Spacing();
  }
  const bool wide = ImGui::GetContentRegionAvail().x >= em * 40.0f;
  ImGui::PushID("top");
  cardRow(
      wide, st.cardRowH[0], [&](float h) { return cabinCard(m, h); },
      [&](float h) { return flightCard(m, h); });
  ImGui::PopID();
  ImGui::Spacing();

  // band fills down to the aircraft card 2-8 rows 2-3 stacked
  const float aircraftH = st.cardRowH[2] > 0 ? st.cardRowH[2] : em * 4.5f;
  const float cellY = em * 0.28f;
  const float rowH = ImGui::GetTextLineHeight() + cellY * 2.0f;
  const float overhead = em * 0.8f * 2.0f + em * 1.2f + style.ItemSpacing.y;
  const float space =
      ImGui::GetContentRegionAvail().y - aircraftH - style.ItemSpacing.y * 2.0f;
  const int rows =
      std::clamp(static_cast<int>((space - overhead) / rowH), 2, wide ? 8 : 3);
  // the row table adds a few px measured last frame
  const float fill = wide ? std::max(0.0f, space - st.cardRowH[3]) : 0.0f;
  const float given = std::max(st.cardRowH[1], fill);
  const float bandTop = ImGui::GetCursorPosY();
  ImGui::PushID("band");
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                      ImVec2(style.CellPadding.x, cellY));
  cardRow(
      wide, st.cardRowH[1], [&](float h) { return recentCard(m, rows, h); },
      [&](float h) { return comingCard(m, rows, h); }, fill);
  ImGui::PopStyleVar();
  ImGui::PopID();
  ImGui::Spacing();
  if (wide)
    st.cardRowH[3] = std::clamp(
        ImGui::GetCursorPosY() - bandTop - given - style.ItemSpacing.y * 2.0f,
        0.0f, em * 3.0f);
  st.cardRowH[2] = aircraftCard(m);
}

}  // namespace fxa::ui
