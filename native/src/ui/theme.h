// dark navy delta blue/red white emirates red themes b612 fonts black cards
#pragma once

#include <imgui.h>

namespace fxa::ui {

struct Fonts {
  ImFont* regular = nullptr;
  ImFont* bold = nullptr;
  ImFont* mono = nullptr;
};
Fonts& fonts();
// adds the 3 b612 fonts to any atlas plugin headless test
void addFonts(ImFontAtlas* atlas);
void loadFonts();  // plugin enable only imgwindow shared atlas

enum class Theme { Dark = 0, White = 1 };

struct Palette {
  // page
  ImU32 page, panel, field, fieldHover, border;
  ImU32 text, dim, hdr;
  ImU32 chrome;  // selected sidebar item pressed button
  ImU32 accent;  // checks grabs progress
  ImU32 brand;   // card rule tab overline
  ImU32 tab, tabHover, tabSelected;
  ImU32 ok, warn, bad;  // ecam green ok amber caution red error
  ImU32 track, chipBg;
  // inside cards black in both themes
  ImU32 cardBg, cardText, cardDim, cardOk, cardWarn, cardBad, cardButton,
      cardButtonHover;
  bool dark;
};
const Palette& pal();
void setTheme(Theme t, bool colorBlind);

// per imgui context cheap
void applyStyle(float fontScale);

inline ImVec4 vec(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
ImU32 withAlpha(ImU32 c, float a);

// scale relative to body text f can be null for size only
void pushFont(ImFont* f, float scale = 1.0f);

}  // namespace fxa::ui
