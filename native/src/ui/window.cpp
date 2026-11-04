#include "ui/window.h"

#include <XPLMDisplay.h>

#include "ImgWindow.h"
#include "core/guard.h"
#include "ui/panel.h"
#include "ui/theme.h"

namespace fxa::ui {

namespace {

constexpr int kMinW = 520, kMinH = 380;

std::function<UiModel*()> gProvider;
UiState gState;

class FxWindow : public ImgWindow {
 public:
  FxWindow(int l, int t, int r, int b)
      : ImgWindow(l, t, r, b, xplm_WindowDecorationRoundRectangle,
                  xplm_WindowLayerFloatingWindows) {
    SetWindowTitle("FXAnnounce");
    SetWindowResizingLimits(kMinW, kMinH, 3000, 2400);
    ImGui::GetIO().IniFilename = nullptr;  // imgwindow leaves its context
                                           // current no imgui.ini in xps folder
  }

  void storeGeometry(Settings& s) {
    if (!IsInsideSim()) return;  // popout/vr geometry isnt in global boxels
    GetWindowGeometry(s.winLeft, s.winTop, s.winRight, s.winBottom);
  }

 protected:
  void buildInterface() override {
    guarded("window.build", [&] {
      UiModel* m = gProvider ? gProvider() : nullptr;
      const Settings* s = m ? m->settings : nullptr;
      setTheme(s && s->theme == 1 ? Theme::White : Theme::Dark,
               s && s->colorBlind);
      applyStyle(s ? s->fontScale : 1.0f);
      if (!m) {
        ImGui::TextDisabled("FXAnnounce is not running.");
        return;
      }
      if (mFirstRender && s) gState.tab = s->tab;
      drawPanel(*m, gState);
      if (m->settings) m->settings->tab = gState.tab;
    });
  }
};

FxWindow* gWindow = nullptr;

Settings* currentSettings() {
  UiModel* m = gProvider ? gProvider() : nullptr;
  return m ? m->settings : nullptr;
}

}  // namespace

void setModelProvider(std::function<UiModel*()> provider) {
  gProvider = std::move(provider);
}

void showWindow(bool show) {
  Settings* s = currentSettings();
  if (!gWindow) {
    if (!show) return;
    int l = s ? s->winLeft : 0, t = s ? s->winTop : 0, r = s ? s->winRight : 0,
        b = s ? s->winBottom : 0;
    int sl, st, sr, sb;
    XPLMGetScreenBoundsGlobal(&sl, &st, &sr, &sb);
    const bool onScreen = r - l >= kMinW && t - b >= kMinH && l >= sl &&
                          r <= sr && t <= st && b >= sb;
    if (!onScreen) {  // first open or monitors changed center 760x640
      const int w = 760, h = 640;
      l = sl + ((sr - sl) - w) / 2;
      t = st - ((st - sb) - h) / 2;
      r = l + w;
      b = t - h;
    }
    loadFonts();
    gWindow = new FxWindow(l, t, r, b);
  }
  if (!show && s) gWindow->storeGeometry(*s);
  gWindow->SetVisible(show);
  if (s) s->windowVisible = show;
}

void toggleWindow() { showWindow(!windowVisible()); }

bool windowVisible() { return gWindow && gWindow->GetVisible(); }

void destroyWindow() {
  if (!gWindow) return;
  if (Settings* s = currentSettings()) {
    gWindow->storeGeometry(*s);
    s->windowVisible = gWindow->GetVisible();
  }
  delete gWindow;
  gWindow = nullptr;
  gState.tabRestored = false;
}

void shutdownUi() {
  destroyWindow();
  ImgWindow::sFontAtlas.reset();
  ImgWindow::Shutdown();
  fonts() = Fonts{};
}

}  // namespace fxa::ui
