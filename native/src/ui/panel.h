// titlebar tabs pure imgui
#pragma once

#include "ui/model.h"

namespace fxa::ui {

// saved as ui tab 4 was the inspector
enum TabId {
  TAB_STATUS = 0,
  TAB_TIMELINE = 1,
  TAB_PACKS = 2,
  TAB_SETTINGS = 3,
  TAB_LOG = 5
};
constexpr int kTabIds[] = {TAB_STATUS, TAB_TIMELINE, TAB_PACKS, TAB_SETTINGS,
                           TAB_LOG};
const char* tabName(int tab);
// timeline and log can be turned off
bool tabVisible(int tab, const Settings* s);

void drawPanel(UiModel& m, UiState& s);

}  // namespace fxa::ui
