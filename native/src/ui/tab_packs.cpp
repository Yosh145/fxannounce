// packs now playing w skip/mute active pack every pack file explainer w show
// log
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "core/announcement.h"
#include "core/strings.h"
#include "ui/tabs.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fxa::ui {

namespace {

void nowPlayingCard(UiModel& m) {
  const Palette& p = pal();
  const float em = ImGui::GetFontSize();
  BeginCard("##now");
  CardTitle("NOW PLAYING");
  if (m.calling) {
    BigText("Cabin calling", 1.3f, p.cardWarn);
    ImGui::TextWrapped("Pull CAB on the ACP to answer (attempt %d).",
                       std::max(1, m.callAttempt));
  } else if (m.now.file.empty()) {
    BigText("Nothing playing", 1.3f, p.cardDim);
  } else {
    BigText(m.playing.empty() ? "Announcement" : m.playing.c_str(), 1.3f);
    Tooltip(m.playingFenix.c_str());
    pushFont(fonts().mono, 0.9f);
    ImGui::TextDisabled("%s%s", m.now.file.c_str(),
                        m.now.interphone ? "  (interphone)" : "");
    ImGui::PopFont();
    if (m.now.decoding) {
      ImGui::TextDisabled("Loading...");
    } else {
      char t[48];
      std::snprintf(t, sizeof(t), "%s / %s%s", mmssText(m.now.pos).c_str(),
                    mmssText(m.now.dur).c_str(),
                    m.now.paused ? " (paused)" : "");
      pushFont(fonts().mono);
      const float tw = ImGui::CalcTextSize(t).x;
      const float bar =
          std::max(em * 4.0f, ImGui::GetContentRegionAvail().x - tw - em);
      Progress(m.now.dur > 0 ? static_cast<float>(m.now.pos / m.now.dur) : 0.0f,
               p.cardButton, p.accent, bar);
      ImGui::SameLine(0, em);
      ImGui::TextUnformatted(t);
      ImGui::PopFont();
    }
  }
  ImGui::Spacing();
  ImGui::BeginDisabled(!m.active);
  ImGui::BeginDisabled(m.now.file.empty() && !m.calling);
  if (ImGui::Button("Skip", ImVec2(em * 6, 0)) && m.onSkip) m.onSkip();
  ImGui::EndDisabled();
  Tooltip("Stops this announcement; the sequence continues (fxannounce/skip).");
  ImGui::SameLine();
  if (ToggleButton(m.muted ? "Unmute" : "Mute", m.muted, p.cardWarn,
                   ImVec2(em * 6, 0)) &&
      m.onToggleMute)
    m.onToggleMute();
  Tooltip(
      "Silences every FXAnnounce sound; the sequence continues "
      "(fxannounce/mute_toggle).");
  ImGui::EndDisabled();
  ImGui::SameLine(0, em * 1.5f);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("Boarding music");
  ImGui::SameLine();
  ImGui::TextWrapped("%s", m.music.empty() ? "off" : m.music.c_str());
  EndCard();
}

void activePackLine(UiModel& m) {
  const float em = ImGui::GetFontSize();
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("Active pack");
  ImGui::SameLine();
  pushFont(fonts().bold);
  ImGui::TextUnformatted(m.pack.empty() ? "-" : m.pack.c_str());
  ImGui::PopFont();
  ImGui::SameLine(0, em * 1.5f);
  ImGui::TextDisabled("Custom tags");
  ImGui::SameLine();
  ImGui::TextUnformatted(m.customTags.empty() ? "none" : m.customTags.c_str());
  HelpMarker(
      "Custom tags switched on for this flight: from the livery's "
      "airlinepack.cfg and, when the ToLiss SatCom "
      "antenna is fitted, the SatCom tags (Settings > Announcements). Files "
      "with other custom tags are not played.");
  const float w = em * 6.5f;
  ImGui::SameLine();
  const float right =
      ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w;
  if (right > ImGui::GetCursorPosX())
    ImGui::SetCursorPosX(right);
  else
    ImGui::NewLine();
  ImGui::BeginDisabled(!m.onRescan || m.scanning);
  if (ImGui::Button(m.scanning ? "Scanning..." : "Rescan", ImVec2(w, 0)) &&
      m.onRescan)
    m.onRescan();
  ImGui::EndDisabled();
  Tooltip("Looks for new packs and files (fxannounce/rescan).");
}

std::string notPlayedWhy(const SoundFile& f) {
  if (const char* hint = fenixRenameHint(f.typeName))
    return "'" + f.typeName +
           "' is not a Fenix announcement type, so it is not played. Rename it "
           "to " +
           hint + " to use it.";
  return "Not a Fenix announcement type, so it is not played (custom sounds "
         "are for the Fenix cabin panel).";
}

void packTable(const Pack& pk, const UiState& st) {
  const Palette& p = pal();
  if (!ImGui::BeginTable("##files", 4,
                         ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_SizingStretchProp |
                             ImGuiTableFlags_BordersInnerV))
    return;
  ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 3.0f);
  ImGui::TableSetupColumn("Announcement", ImGuiTableColumnFlags_WidthStretch,
                          2.2f);
  ImGui::TableSetupColumn("Tags", ImGuiTableColumnFlags_WidthStretch, 1.6f);
  ImGui::TableSetupColumn("Heard on", ImGuiTableColumnFlags_WidthStretch, 1.1f);
  ImGui::TableHeadersRow();
  for (const SoundFile& f : pk.files) {
    if (st.packFilter[0] && !icontains(f.relPath, st.packFilter)) continue;
    ImGui::PushID(f.path.c_str());
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextWrapped("%s", f.relPath.c_str());
    ImGui::TableSetColumnIndex(1);
    if (f.type) {
      ImGui::TextWrapped("%s", annLabel(*f.type, f.context).c_str());
      Tooltip(annFenixLabel(*f.type, f.context).c_str());
    } else {
      ImGui::TextColored(vec(p.dim), "(not played)");
      HelpMarker(notPlayedWhy(f).c_str());
    }
    ImGui::TableSetColumnIndex(2);
    std::string tags;
    for (const std::string& t : f.rawTags) tags += "[" + t + "]";
    ImGui::TextWrapped("%s", tags.empty() ? "-" : tags.c_str());
    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted(f.type ? heardOn(*f.type, f.effectiveMode()) : "-");
    ImGui::PopID();
  }
  ImGui::EndTable();
}

void library(UiModel& m, UiState& st) {
  const Palette& p = pal();
  const float em = ImGui::GetFontSize();
  ImGui::SetNextItemWidth(em * 14);
  ImGui::InputTextWithHint("##pf", "filter files", st.packFilter,
                           sizeof(st.packFilter));
  if (!m.library) {
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled(
        "%s", m.scanning ? "Looking for Airline Packs..." : "Not scanned yet.");
    return;
  }
  const PackLibrary& lib = *m.library;
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%d packs, %d sounds", static_cast<int>(lib.packs.size()),
                      static_cast<int>(lib.soundCount()));
  if (!lib.warnings.empty() &&
      ImGui::TreeNode("##libw", "Scan warnings (%d)",
                      static_cast<int>(lib.warnings.size()))) {
    for (const std::string& w : lib.warnings)
      ImGui::TextColored(vec(p.warn), "%s", w.c_str());
    ImGui::TreePop();
  }
  if (lib.packs.empty())
    ImGui::TextDisabled(
        "No Airline Packs found. Put each pack in its own folder in the packs "
        "folder below.");
  for (const Pack& pk : lib.packs) {
    const bool active = pk.name == m.pack;
    char label[256];
    std::snprintf(label, sizeof(label), "%s%s   %d sounds###pk_%s",
                  pk.name.c_str(), active ? "   (active)" : "",
                  static_cast<int>(pk.files.size()), pk.root.c_str());
    if (active) ImGui::PushStyleColor(ImGuiCol_Text, p.accent);
    const bool open =
        ImGui::TreeNodeEx(label, active ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    if (active) ImGui::PopStyleColor();
    Tooltip(
        (pk.root +
         (pk.wrapper.empty() ? "" : "\ninside the folder '" + pk.wrapper + "'"))
            .c_str());
    if (!open) continue;
    packTable(pk, st);
    if (!pk.warnings.empty() &&
        ImGui::TreeNode("##pkw", "Warnings (%d)",
                        static_cast<int>(pk.warnings.size()))) {
      for (const std::string& w : pk.warnings)
        ImGui::TextColored(vec(p.warn), "%s", w.c_str());
      ImGui::TreePop();
    }
    ImGui::TreePop();
  }
}

void explainer(UiModel& m, UiState& st) {
  if (!m.explain) return;
  if (!ImGui::CollapsingHeader("What would play now, and why")) return;
  const float em = ImGui::GetFontSize();
  ImGui::SetNextItemWidth(14 * em);
  if (ImGui::BeginCombo("##etype",
                        annTypeLabel(static_cast<AnnType>(st.explainType)))) {
    for (int k = 0; k < kAnnTypeCount; ++k)
      if (ImGui::Selectable(annTypeLabel(static_cast<AnnType>(k)),
                            k == st.explainType))
        st.explainType = k;
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  const char* ctxNames[] = {"any call", "takeoff call", "landing call"};
  ImGui::SetNextItemWidth(9 * em);
  ImGui::Combo("##ectx", &st.explainContext, ctxNames, 3);
  ImGui::SameLine();
  if (ImGui::Button("Explain"))
    st.explainLines = m.explain(st.explainType, st.explainContext);
  pushFont(fonts().mono);
  for (const std::string& line : st.explainLines)
    ImGui::TextWrapped("%s", line.c_str());
  ImGui::PopFont();
}

}  // namespace

void drawPacks(UiModel& m, UiState& st) {
  nowPlayingCard(m);
  ImGui::Spacing();
  activePackLine(m);
  ImGui::Spacing();
  library(m, st);
  ImGui::Spacing();
  ImGui::TextDisabled("Packs folder: %s", m.packsDir.c_str());
  HelpMarker(
      "Each Airline Pack is a folder here, named with the airline's ICAO code "
      "(DLH, BAW...). FXAnnounce also "
      "scans the extra folders in Settings > Advanced. Added packs while "
      "X-Plane runs? Press Rescan.");
  if (m.settings && m.settings->showLog) {
    ImGui::Spacing();
    explainer(m, st);
  }
}

}  // namespace fxa::ui
