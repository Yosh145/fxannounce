#include "ui/widgets.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "ui/theme.h"

namespace fxa::ui {

namespace {
constexpr int kCardColors = 7;  // begincard pushes endcard pops
float cardPadY() { return ImGui::GetFontSize() * 0.8f; }
}  // namespace

void Chip(const char* text, ImU32 bg, ImU32 fg) {
  ImVec2 ts = ImGui::CalcTextSize(text);
  ImVec2 pad(ImGui::GetFontSize() * 0.6f, ImGui::GetFontSize() * 0.2f);
  ImVec2 a = ImGui::GetCursorScreenPos();
  ImVec2 b(a.x + ts.x + pad.x * 2, a.y + ts.y + pad.y * 2);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(a, b, bg, (b.y - a.y) * 0.5f);
  dl->AddText(ImVec2(a.x + pad.x, a.y + pad.y), fg, text);
  ImGui::Dummy(ImVec2(b.x - a.x, b.y - a.y));
}

bool ToggleButton(const char* label, bool on, ImU32 onColor, ImVec2 size) {
  if (on) {
    ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(onColor, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, withAlpha(onColor, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, withAlpha(onColor, 0.85f));
  }
  bool clicked = ImGui::Button(label, size);
  if (on) ImGui::PopStyleColor(3);
  return clicked;
}

static void kvRow(const char* label, const ImU32* color, const char* fmt,
                  va_list ap) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextColored(vec(pal().dim), "%s", label);
  ImGui::TableSetColumnIndex(1);
  char buf[512];
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  if (color) ImGui::PushStyleColor(ImGuiCol_Text, *color);
  ImGui::TextWrapped("%s", buf);
  if (color) ImGui::PopStyleColor();
}

void KV(const char* label, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  kvRow(label, nullptr, fmt, ap);
  va_end(ap);
}

void KVColored(const char* label, ImU32 color, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  kvRow(label, &color, fmt, ap);
  va_end(ap);
}

bool BeginKVTable(const char* id, float labelWidthEm) {
  if (!ImGui::BeginTable(
          id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    return false;
  ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed,
                          ImGui::GetFontSize() * labelWidthEm);
  ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
  return true;
}

void EndKVTable() { ImGui::EndTable(); }

void Heading(const char* text, const char* help, const char* note) {
  ImGui::Spacing();
  pushFont(fonts().bold);
  ImGui::TextColored(vec(pal().hdr), "%s", text);
  ImGui::PopFont();
  if (note) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", note);
  }
  if (help) HelpMarker(help);
  ImVec2 a = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddLine(
      a, ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y),
      ImGui::GetColorU32(ImGuiCol_Separator));
  ImGui::Dummy(ImVec2(0, 2));
}

void Tooltip(const char* text) {
  if (!text || !*text) return;
  if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return;
  const Palette& p = pal();
  ImGui::PushStyleColor(ImGuiCol_Text, p.text);
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, p.dim);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, withAlpha(p.panel, 0.98f));
  if (ImGui::BeginTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
  ImGui::PopStyleColor(3);
}

void HelpMarker(const char* text) {
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  Tooltip(text);
}

void Progress(float frac, ImU32 track, ImU32 fill, float width) {
  const Palette& p = pal();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float h = ImGui::GetFontSize() * 0.4f;
  const float w = width > 0 ? width : ImGui::GetContentRegionAvail().x;
  const float line = ImGui::GetTextLineHeight();
  ImVec2 a = ImGui::GetCursorScreenPos();
  a.y += (line - h) * 0.5f;
  dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), track ? track : p.track,
                    h * 0.5f);
  const float f = std::clamp(frac, 0.0f, 1.0f);
  if (f > 0.003f)
    dl->AddRectFilled(a, ImVec2(a.x + std::max(h, f * w), a.y + h),
                      fill ? fill : p.accent, h * 0.5f);
  ImGui::Dummy(ImVec2(w, line));
}

bool BeginCard(const char* id, float minHeight, ImU32 rule, float width) {
  const Palette& p = pal();
  const float em = ImGui::GetFontSize();
  if (minHeight > 0)
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, minHeight),
                                        ImVec2(FLT_MAX, FLT_MAX));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, p.cardBg);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(em * 1.0f, cardPadY()));
  const bool visible = ImGui::BeginChild(
      id, ImVec2(width > 0 ? width : -FLT_MIN, 0),
      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  // top rule inset by the rounding drawn outside the childs clip rect
  const ImVec2 pos = ImGui::GetWindowPos();
  const ImVec2 size = ImGui::GetWindowSize();
  const float r = ImGui::GetStyle().ChildRounding;
  ImGui::PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), false);
  ImGui::GetWindowDrawList()->AddRectFilled(
      ImVec2(pos.x + r, pos.y), ImVec2(pos.x + size.x - r, pos.y + 2.0f),
      rule ? rule : p.brand);
  ImGui::PopClipRect();

  ImGui::PushStyleColor(ImGuiCol_Text, p.cardText);
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, p.cardDim);
  ImGui::PushStyleColor(ImGuiCol_Button, p.cardButton);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.cardButtonHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 80));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, p.cardButton);
  ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(255, 255, 255, 40));
  return visible;
}

float EndCard() {
  const float natural =
      ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + cardPadY();
  ImGui::PopStyleColor(kCardColors);
  ImGui::EndChild();
  return natural;
}

void CardTitle(const char* text) {
  pushFont(fonts().mono, 0.8f);
  ImGui::TextDisabled("%s", text);
  ImGui::PopFont();
}

void BigText(const char* text, float scale, ImU32 color) {
  pushFont(fonts().bold, scale);
  if (color) ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextWrapped("%s", text);
  if (color) ImGui::PopStyleColor();
  ImGui::PopFont();
}

void RouteBar(const char* from, const char* to, float frac) {
  const Palette& p = pal();
  pushFont(fonts().mono, 1.1f);
  ImGui::TextUnformatted(from);
  ImGui::SameLine();
  const float toW = ImGui::CalcTextSize(to).x;
  const float w = std::max(
      ImGui::GetFontSize() * 3.0f,
      ImGui::GetContentRegionAvail().x - toW - ImGui::GetStyle().ItemSpacing.x);
  const float line = ImGui::GetTextLineHeight();
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float y = a.y + line * 0.5f;
  const float r = line * 0.22f;
  const float x0 = a.x + r, x1 = a.x + w - r;
  const float x = x0 + std::clamp(frac, 0.0f, 1.0f) * (x1 - x0);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(ImVec2(x0, y - 1.0f), ImVec2(x1, y + 1.0f),
                    p.cardButtonHover);  // ahead
  dl->AddRectFilled(ImVec2(x0, y - 1.5f), ImVec2(x, y + 1.5f),
                    p.accent);  // flown
  dl->AddCircleFilled(ImVec2(x0, y), r, p.accent);
  dl->AddCircle(ImVec2(x1, y), r, p.cardDim, 0, 1.5f);
  dl->AddCircleFilled(ImVec2(x, y), r * 1.6f, p.cardText);  // the plane
  dl->AddCircleFilled(ImVec2(x, y), r * 0.9f, p.accent);
  ImGui::Dummy(ImVec2(w, line));
  ImGui::SameLine();
  ImGui::TextUnformatted(to);
  ImGui::PopFont();
}

void TextCentered(const char* text, ImU32 color) {
  const float avail = ImGui::GetContentRegionAvail().x;
  const float w = ImGui::CalcTextSize(text).x;
  if (w < avail)
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w) * 0.5f);
  if (color) ImGui::PushStyleColor(ImGuiCol_Text, color);
  if (w < avail)
    ImGui::TextUnformatted(text);
  else
    ImGui::TextWrapped("%s", text);
  if (color) ImGui::PopStyleColor();
}

bool PillSwitch(const char* id, const char* left, const char* right,
                bool* rightOn) {
  const Palette& p = pal();
  const float em = ImGui::GetFontSize();
  bool toggled = false;
  ImGui::PushID(id);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(vec(*rightOn ? p.dim : p.text), "%s", left);
  if (ImGui::IsItemClicked() && *rightOn) toggled = true;
  ImGui::SameLine();
  const ImVec2 size(em * 2.1f, em * 1.05f);
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float y = a.y + (ImGui::GetFrameHeight() - size.y) * 0.5f;
  if (ImGui::InvisibleButton("##sw", ImVec2(size.x, ImGui::GetFrameHeight())))
    toggled = true;
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float r = size.y * 0.5f;
  dl->AddRectFilled(ImVec2(a.x, y), ImVec2(a.x + size.x, y + size.y),
                    hovered ? p.fieldHover : p.field, r);
  dl->AddRect(ImVec2(a.x, y), ImVec2(a.x + size.x, y + size.y), p.border, r);
  const float cx = *rightOn ? a.x + size.x - r : a.x + r;
  dl->AddCircleFilled(ImVec2(cx, y + r), r - 3.0f, p.accent);
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(vec(*rightOn ? p.text : p.dim), "%s", right);
  if (ImGui::IsItemClicked() && !*rightOn) toggled = true;
  ImGui::PopID();
  if (toggled) *rightOn = !*rightOn;
  return toggled;
}

std::string zuluText(double sec) {
  if (sec < 0) return "--:--Z";
  const int m = static_cast<int>(std::fmod(sec, 86400.0)) / 60;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02dZ", m / 60, m % 60);
  return buf;
}

std::string localText(double sec) {
  if (sec < 0) return "unknown";
  const int m = static_cast<int>(std::fmod(sec, 86400.0)) / 60;
  const int h = m / 60, mi = m % 60;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02d:%02d (%d:%02d %s)", h, mi,
                h % 12 == 0 ? 12 : h % 12, mi, h < 12 ? "AM" : "PM");
  return buf;
}

std::string mmssText(double sec) {
  if (sec < 0) sec = 0;
  const int s = static_cast<int>(sec);
  char buf[16];
  if (s >= 3600)
    std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", s / 3600, (s / 60) % 60,
                  s % 60);
  else
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
  return buf;
}

void TextZulu(double zuluSec, double localSec, ImU32 color) {
  const std::string z = zuluText(zuluSec);
  pushFont(fonts().mono);
  if (color)
    ImGui::TextColored(vec(color), "%s", z.c_str());
  else
    ImGui::TextUnformatted(z.c_str());
  ImGui::PopFont();
  if (localSec >= 0) Tooltip(("Local " + localText(localSec)).c_str());
}

}  // namespace fxa::ui
