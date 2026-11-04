// widgets everything sizes off the font and avail width
#pragma once

#include <imgui.h>

#include <string>

namespace fxa::ui {

void Chip(const char* text, ImU32 bg, ImU32 fg);
bool ToggleButton(const char* label, bool on, ImU32 onColor,
                  ImVec2 size = ImVec2(0, 0));
void KV(const char* label, const char* fmt, ...) IM_FMTARGS(2);
void KVColored(const char* label, ImU32 color, const char* fmt, ...)
    IM_FMTARGS(3);
bool BeginKVTable(const char* id, float labelWidthEm = 9.0f);
void EndKVTable();
// heading thin rule optional note and on the same line
void Heading(const char* text, const char* help = nullptr,
             const char* note = nullptr);
void HelpMarker(const char* text);
// page colours even inside a card
void Tooltip(const char* text);
// 0 for track/fill/width theme avail
void Progress(float frac, ImU32 track = 0, ImU32 fill = 0, float width = 0);

// black rounded card w a coloured top rule minheight lines up a row endcard
// returns the natural height always call endcard
bool BeginCard(const char* id, float minHeight = 0, ImU32 rule = 0,
               float width = 0);
float EndCard();
void CardTitle(const char* text);
void BigText(const char* text, float scale = 1.5f, ImU32 color = 0);
// origin flown part aircraft dot destination
void RouteBar(const char* from, const char* to, float frac);
void TextCentered(const char* text, ImU32 color = 0);

// right the state true when toggled
bool PillSwitch(const char* id, const char* left, const char* right,
                bool* rightOn);

// secs since midnight negative unknown
std::string zuluText(double sec);
std::string localText(double sec);  // local w 12h in parens
std::string mmssText(double sec);
// local time in the tooltip
void TextZulu(double zuluSec, double localSec, ImU32 color = 0);

}  // namespace fxa::ui
