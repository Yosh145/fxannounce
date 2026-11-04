// imgwindow atlas side separate so the ui builds without imgwindow
#include <memory>

#include "ImgWindow.h"
#include "ui/theme.h"

namespace fxa::ui {

void loadFonts() {
  if (ImgWindow::sFontAtlas) return;
  ImgWindow::sFontAtlas = std::make_shared<ImgFontAtlas>();
  addFonts(ImgWindow::sFontAtlas->getAtlas());
}

}  // namespace fxa::ui
