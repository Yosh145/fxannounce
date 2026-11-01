// Reusable widgets (after OpenTurb Pro's widgets). Everything sizes from the font and available width.
#pragma once

#include <imgui.h>

namespace fxa::ui {

// Pill-shaped status chip.
void Chip(const char* text, ImU32 bg, ImU32 fg);
// Coloured toggle button; returns true when clicked.
bool ToggleButton(const char* label, bool on, ImU32 onColor, ImVec2 size = ImVec2(0, 0));
// Dim label + value row inside a 2-column table.
void KV(const char* label, const char* fmt, ...) IM_FMTARGS(2);
void KVColored(const char* label, ImU32 color, const char* fmt, ...) IM_FMTARGS(3);
// Begin a two-column label/value table sized to the available width.
bool BeginKVTable(const char* id, float labelWidthEm = 9.0f);
void EndKVTable();
// Section heading with a thin rule; an optional dim note and "(?)" help sit on the same line.
void Heading(const char* text, const char* help = nullptr, const char* note = nullptr);
// "?" marker with a tooltip.
void HelpMarker(const char* text);
// Thin progress track (0..1) with an optional overlay label.
void Progress(float frac, const char* overlay = nullptr);

} // namespace fxa::ui
