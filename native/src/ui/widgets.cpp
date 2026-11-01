#include "ui/widgets.h"

#include "ui/theme.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace fxa::ui {

void Chip(const char* text, ImU32 bg, ImU32 fg)
{
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 pad(ImGui::GetFontSize() * 0.55f, ImGui::GetFontSize() * 0.18f);
    ImVec2 a = ImGui::GetCursorScreenPos();
    ImVec2 b(a.x + ts.x + pad.x * 2, a.y + ts.y + pad.y * 2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, bg, (b.y - a.y) * 0.5f);
    dl->AddText(ImVec2(a.x + pad.x, a.y + pad.y), fg, text);
    ImGui::Dummy(ImVec2(b.x - a.x, b.y - a.y));
}

bool ToggleButton(const char* label, bool on, ImU32 onColor, ImVec2 size)
{
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(onColor, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, withAlpha(onColor, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, withAlpha(onColor, 0.85f));
    }
    bool clicked = ImGui::Button(label, size);
    if (on) ImGui::PopStyleColor(3);
    return clicked;
}

static void kvRow(const char* label, const ImU32* color, const char* fmt, va_list ap)
{
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

void KV(const char* label, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvRow(label, nullptr, fmt, ap);
    va_end(ap);
}

void KVColored(const char* label, ImU32 color, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvRow(label, &color, fmt, ap);
    va_end(ap);
}

bool BeginKVTable(const char* id, float labelWidthEm)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) return false;
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * labelWidthEm);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void EndKVTable() { ImGui::EndTable(); }

void Heading(const char* text, const char* help, const char* note)
{
    ImGui::Spacing();
    if (fonts().bold) ImGui::PushFont(fonts().bold, 0.0f);
    ImGui::TextColored(vec(pal().hdr), "%s", text);
    if (fonts().bold) ImGui::PopFont();
    if (note) { ImGui::SameLine(); ImGui::TextDisabled("%s", note); }
    if (help) HelpMarker(help);
    ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(a, ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y),
                                        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(0, 2));
}

void HelpMarker(const char* text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void Progress(float frac, const char* overlay)
{
    const Palette& p = pal();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = ImGui::GetFontSize() * 0.45f;
    const float w = ImGui::GetContentRegionAvail().x;
    const float line = ImGui::GetTextLineHeight();
    ImVec2 a = ImGui::GetCursorScreenPos();
    a.y += (line - h) * 0.5f;
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), p.track, h * 0.5f);
    const float f = std::clamp(frac, 0.0f, 1.0f);
    if (f > 0.003f) dl->AddRectFilled(a, ImVec2(a.x + f * w, a.y + h), p.accent, h * 0.5f);
    ImGui::Dummy(ImVec2(w, line));
    if (overlay && *overlay) ImGui::TextDisabled("%s", overlay);
}

} // namespace fxa::ui
