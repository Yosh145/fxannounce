// dark navy page 003268 blue 9b1631 red white white page d71a21 red cards black
// either way
#include "ui/theme.h"

namespace fxa::ui {

namespace {

constexpr ImU32 hex(unsigned rgb) {
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
}

// card colours same in both themes
constexpr ImU32 kCardOk = hex(0x4ADE80), kCardWarn = hex(0xF5A623),
                kCardBad = hex(0xFF4D5E);
constexpr ImU32 kCardOkCb = hex(0x2BC48F), kCardWarnCb = hex(0xE69F00),
                kCardBadCb = hex(0xFF7A3D);

Palette makeDark(bool cb) {
  Palette p{};
  p.page = hex(0x0A1322);
  p.panel = hex(0x0F1C33);
  p.field = hex(0x16274A);
  p.fieldHover = hex(0x1C3260);
  p.border = hex(0x1E3054);
  p.text = hex(0xE8EDF5);
  p.dim = hex(0x8C9AB3);
  p.hdr = hex(0xB9C8E2);
  p.chrome = hex(0x003268);
  p.accent = hex(0x5B93D6);
  p.brand = hex(0x9B1631);
  p.tab = hex(0x0F1C33);
  p.tabHover = hex(0x1C3260);
  p.tabSelected = hex(0x003268);
  p.ok = cb ? hex(0x2BC48F) : hex(0x4ADE80);
  p.warn = cb ? hex(0xE69F00) : hex(0xF5A623);
  p.bad = cb ? hex(0xFF7A3D) : hex(0xFF4D5E);
  p.track = hex(0x1E3054);
  p.chipBg = hex(0x16274A);
  p.dark = true;
  return p;
}

Palette makeWhite(bool cb) {
  Palette p{};
  p.page = hex(0xFFFFFF);
  p.panel = hex(0xF5F5F6);
  p.field = hex(0xECECEE);
  p.fieldHover = hex(0xE2E2E5);
  p.border = hex(0xD9D9DD);
  p.text = hex(0x1A1A1A);
  p.dim = hex(0x6B6B70);
  p.hdr = hex(0x2B2B2B);
  p.chrome = hex(0xFBE3E4);  // light red tint under dark text
  p.accent = hex(0xD71A21);
  p.brand = hex(0xD71A21);
  p.tab = hex(0xECECEE);
  p.tabHover = hex(0xF6D4D6);
  p.tabSelected = hex(0xFFFFFF);  // blends w the page red overline marks it
  p.ok = cb ? hex(0x007A5A) : hex(0x1E8E3E);
  p.warn = cb ? hex(0xB57A00) : hex(0xB26A00);
  p.bad = cb ? hex(0xC24E00) : hex(0xA0001C);
  p.track = hex(0xE2E2E5);
  p.chipBg = hex(0xECECEE);
  p.dark = false;
  return p;
}

Palette finish(Palette p, bool cb, bool white) {
  p.cardBg = white ? hex(0x141414) : hex(0x000000);
  p.cardText = hex(0xF2F4F8);
  p.cardDim = hex(0x8C96A8);
  p.cardOk = cb ? kCardOkCb : kCardOk;
  p.cardWarn = cb ? kCardWarnCb : kCardWarn;
  p.cardBad = cb ? kCardBadCb : kCardBad;
  p.cardButton = IM_COL32(255, 255, 255, 28);
  p.cardButtonHover = IM_COL32(255, 255, 255, 48);
  return p;
}

Palette gPal = finish(makeDark(false), false, false);

}  // namespace

const Palette& pal() { return gPal; }

void setTheme(Theme t, bool colorBlind) {
  const bool white = t == Theme::White;
  gPal = finish(white ? makeWhite(colorBlind) : makeDark(colorBlind),
                colorBlind, white);
}

ImU32 withAlpha(ImU32 c, float a) {
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  v.w = a;
  return ImGui::ColorConvertFloat4ToU32(v);
}

void pushFont(ImFont* f, float scale) {
  ImGui::PushFont(f, ImGui::GetStyle().FontSizeBase * scale);
}

void applyStyle(float fontScale) {
  ImGuiStyle& s = ImGui::GetStyle();
  s.FontScaleMain = fontScale;
  s.WindowPadding = ImVec2(14, 12);
  s.FramePadding = ImVec2(8, 4);
  s.ItemSpacing = ImVec2(8, 6);
  s.ItemInnerSpacing = ImVec2(6, 4);
  s.CellPadding = ImVec2(6, 3);
  s.FrameRounding = 4;
  s.GrabRounding = 4;
  s.TabRounding = 4;
  s.ChildRounding = 8;
  s.PopupRounding = 6;
  s.ScrollbarRounding = 6;
  s.WindowBorderSize = 0;
  s.ChildBorderSize = 0;
  s.FrameBorderSize = 0;
  s.PopupBorderSize = 1;
  s.TabBorderSize = 0;
  s.TabBarBorderSize = 2;
  s.TabBarOverlineSize = 2;
  s.SelectableTextAlign = ImVec2(0, 0.5f);

  const Palette& p = pal();
  ImVec4* c = s.Colors;
  c[ImGuiCol_Text] = vec(p.text);
  c[ImGuiCol_TextDisabled] = vec(p.dim);
  c[ImGuiCol_WindowBg] = vec(p.page);
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = vec(withAlpha(p.panel, 0.98f));
  c[ImGuiCol_Border] = vec(p.border);
  c[ImGuiCol_FrameBg] = vec(p.field);
  c[ImGuiCol_FrameBgHovered] = vec(p.fieldHover);
  c[ImGuiCol_FrameBgActive] = vec(p.fieldHover);
  c[ImGuiCol_Button] = vec(p.field);
  c[ImGuiCol_ButtonHovered] = vec(p.fieldHover);
  c[ImGuiCol_ButtonActive] = vec(p.dark ? p.chrome : p.tabHover);
  c[ImGuiCol_Header] = vec(p.field);
  c[ImGuiCol_HeaderHovered] = vec(p.fieldHover);
  c[ImGuiCol_HeaderActive] = vec(p.chrome);
  c[ImGuiCol_Tab] = vec(p.tab);
  c[ImGuiCol_TabHovered] = vec(p.tabHover);
  c[ImGuiCol_TabSelected] = vec(p.tabSelected);
  c[ImGuiCol_TabSelectedOverline] = vec(p.brand);
  c[ImGuiCol_TabDimmed] = vec(p.tab);
  c[ImGuiCol_TabDimmedSelected] = vec(p.tabSelected);
  c[ImGuiCol_TabDimmedSelectedOverline] = vec(p.brand);
  c[ImGuiCol_SliderGrab] = vec(p.accent);
  c[ImGuiCol_SliderGrabActive] = vec(p.accent);
  c[ImGuiCol_CheckMark] = vec(p.accent);
  c[ImGuiCol_CheckboxSelectedBg] = vec(p.dark ? p.fieldHover : p.chrome);
  c[ImGuiCol_TitleBg] = vec(p.panel);
  c[ImGuiCol_TitleBgActive] = vec(p.field);
  c[ImGuiCol_TitleBgCollapsed] = vec(p.panel);
  c[ImGuiCol_InputTextCursor] = vec(p.text);
  c[ImGuiCol_TextLink] = vec(p.accent);
  c[ImGuiCol_TreeLines] = vec(p.border);
  c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ResizeGripHovered] = vec(withAlpha(p.accent, 0.5f));
  c[ImGuiCol_ResizeGripActive] = vec(p.accent);
  c[ImGuiCol_PlotHistogramHovered] = vec(p.accent);
  c[ImGuiCol_Separator] = vec(p.border);
  c[ImGuiCol_SeparatorHovered] = vec(p.accent);
  c[ImGuiCol_SeparatorActive] = vec(p.accent);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = vec(p.border);
  c[ImGuiCol_ScrollbarGrabHovered] = vec(p.fieldHover);
  c[ImGuiCol_ScrollbarGrabActive] = vec(p.dim);
  c[ImGuiCol_TableHeaderBg] = vec(p.panel);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] =
      p.dark ? ImVec4(1, 1, 1, 0.025f) : ImVec4(0, 0, 0, 0.025f);
  c[ImGuiCol_TableBorderLight] = vec(p.border);
  c[ImGuiCol_TableBorderStrong] = vec(p.border);
  c[ImGuiCol_PlotHistogram] = vec(p.accent);
  c[ImGuiCol_TextSelectedBg] = vec(withAlpha(p.accent, 0.35f));
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);
  c[ImGuiCol_NavCursor] = vec(p.accent);
}

}  // namespace fxa::ui
