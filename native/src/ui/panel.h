// The FXAnnounce panel: a header strip and the tabs. Pure ImGui (no XPLM, no ImgWindow).
#pragma once

#include "ui/model.h"

namespace fxa::ui {

constexpr int kTabCount = 6;
const char* tabName(int tab);

void drawPanel(UiModel& m, UiState& s);

} // namespace fxa::ui
