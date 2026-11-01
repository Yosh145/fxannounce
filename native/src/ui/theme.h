// Theme, palette and fonts for the FXAnnounce window (the OpenTurb Pro look: dark navy, blue accent, Roboto).
#pragma once

#include <imgui.h>

namespace fxa::ui {

struct Fonts {
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;
    ImFont* mono = nullptr;
};
Fonts& fonts();
void loadFonts();   // once, at plugin enable (ImgWindow's shared atlas); fonts.cpp, not in the headless test

struct Palette {
    ImU32 text, dim, hdr, accent, ok, warn, bad, track, chipBg;
};
const Palette& pal();
void setColorBlind(bool on);

// Style for one ImGui context (call inside buildInterface; cheap).
void applyStyle(float fontScale);

inline ImVec4 vec(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
ImU32 withAlpha(ImU32 c, float a);

} // namespace fxa::ui
