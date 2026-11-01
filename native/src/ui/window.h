// FxWindow: the FXAnnounce panel in an XPLM window (ImgWindow: Panel Graphics on X-Plane 12.4.4+ with OpenGL
// fallback; pop-out and VR handled by the base class).
#pragma once

#include "ui/model.h"

#include <functional>

namespace fxa::ui {

// The plugin provides the model each frame (nullptr: plugin not running).
void setModelProvider(std::function<UiModel*()> provider);

void showWindow(bool show);
void toggleWindow();
bool windowVisible();
void destroyWindow();   // stores geometry into the settings first
// Releases ImgWindow's shared font atlas and pending textures while XPLM is still valid (XPluginDisable), so nothing
// calls into X-Plane from static destructors when the plugin library is unloaded (ARCHITECTURE.md § 4).
void shutdownUi();

} // namespace fxa::ui
