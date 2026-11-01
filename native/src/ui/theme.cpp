// Colours and spacing follow OpenTurb Pro's theme (openturbpro/native/src/ui/theme.cpp) so the two plugins look
// alike side by side.
#include "ui/theme.h"

namespace fxa::ui {

namespace {

constexpr ImU32 rgb(int r, int g, int b) { return IM_COL32(r, g, b, 255); }

const Palette kDefault = {
    rgb(226, 232, 240), rgb(122, 129, 142), rgb(150, 170, 200), rgb(90, 170, 255),
    rgb(74, 222, 128), rgb(251, 146, 60), rgb(248, 113, 113), rgb(44, 49, 60), rgb(40, 46, 58),
};

// Colour-blind safe (Okabe-Ito sky blue, bluish green, orange, vermillion).
const Palette kColorBlind = {
    rgb(226, 232, 240), rgb(128, 134, 146), rgb(150, 170, 200), rgb(86, 180, 233),
    rgb(0, 158, 115), rgb(230, 159, 0), rgb(213, 94, 0), rgb(44, 49, 60), rgb(40, 46, 58),
};

const Palette* gPal = &kDefault;

} // namespace

const Palette& pal() { return *gPal; }
void setColorBlind(bool on) { gPal = on ? &kColorBlind : &kDefault; }

ImU32 withAlpha(ImU32 c, float a)
{
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w = a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

void applyStyle(float fontScale)
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.FontScaleMain = fontScale;
    s.WindowPadding = ImVec2(12, 10);
    s.FramePadding = ImVec2(8, 4);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding = ImVec2(6, 3);
    s.FrameRounding = 4;
    s.GrabRounding = 4;
    s.TabRounding = 4;
    s.ChildRounding = 4;
    s.PopupRounding = 4;
    s.ScrollbarRounding = 6;
    s.WindowBorderSize = 0;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;

    const Palette& p = pal();
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = vec(p.text);
    c[ImGuiCol_TextDisabled]         = vec(p.dim);
    c[ImGuiCol_WindowBg]             = ImVec4(0.086f, 0.098f, 0.122f, 1.0f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.110f, 0.122f, 0.149f, 1.0f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.110f, 0.122f, 0.149f, 0.98f);
    c[ImGuiCol_Border]               = ImVec4(0.20f, 0.22f, 0.27f, 1.0f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.16f, 0.18f, 0.22f, 1.0f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.20f, 0.23f, 0.29f, 1.0f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.24f, 0.28f, 0.36f, 1.0f);
    c[ImGuiCol_Button]               = ImVec4(0.18f, 0.21f, 0.27f, 1.0f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.24f, 0.29f, 0.38f, 1.0f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.19f, 0.38f, 0.65f, 1.0f);
    c[ImGuiCol_Header]               = ImVec4(0.16f, 0.18f, 0.23f, 1.0f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.21f, 0.25f, 0.32f, 1.0f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.24f, 0.29f, 0.38f, 1.0f);
    c[ImGuiCol_Tab]                  = ImVec4(0.13f, 0.15f, 0.19f, 1.0f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.24f, 0.33f, 0.50f, 1.0f);
    c[ImGuiCol_TabSelected]          = ImVec4(0.19f, 0.38f, 0.65f, 1.0f);
    c[ImGuiCol_TabSelectedOverline]  = vec(p.accent);
    c[ImGuiCol_SliderGrab]           = vec(p.accent);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.55f, 0.75f, 1.0f, 1.0f);
    c[ImGuiCol_CheckMark]            = vec(p.accent);
    c[ImGuiCol_Separator]            = ImVec4(0.22f, 0.25f, 0.31f, 1.0f);
    c[ImGuiCol_TableHeaderBg]        = ImVec4(0.13f, 0.15f, 0.19f, 1.0f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.20f, 0.22f, 0.27f, 1.0f);
    c[ImGuiCol_PlotHistogram]        = vec(p.accent);
}

} // namespace fxa::ui
