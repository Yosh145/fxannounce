// one per tab_ cpp
#pragma once

#include "ui/model.h"

namespace fxa::ui {

void drawStatus(UiModel& m, UiState& st);
void drawTimeline(UiModel& m, UiState& st);
void drawPacks(UiModel& m, UiState& st);
void drawSettings(UiModel& m, UiState& st);
void drawLog(UiModel& m, UiState& st);

// apply save
inline void settingsChanged(UiModel& m) {
  if (m.onSettingsChanged) m.onSettingsChanged();
}

}  // namespace fxa::ui
