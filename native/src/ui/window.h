// the panel in an xplm window via imgwindow panel graphics on 12.4.4 w opengl
// fallback popout/vr from the base class
#pragma once

#include <functional>

#include "ui/model.h"

namespace fxa::ui {

// nullptr plugin not running
void setModelProvider(std::function<UiModel*()> provider);

void showWindow(bool show);
void toggleWindow();
bool windowVisible();
void destroyWindow();  // saves geometry first
// drop imgwindows atlas textures while xplm is still valid xplugindisable so no
// static dtor calls into xp
void shutdownUi();

}  // namespace fxa::ui
