// Font loading into ImgWindow's shared atlas (kept apart from theme.cpp so the UI builds without ImgWindow).
#include "ui/theme.h"

#include "ImgWindow.h"

#include <memory>

#include "gf-roboto-bold.inc"
#include "gf-roboto-regular-400.inc"
#include "gf-robotomono-regular-400.inc"

namespace fxa::ui {

namespace {
Fonts gFonts;
} // namespace

Fonts& fonts() { return gFonts; }

void loadFonts()
{
    if (ImgWindow::sFontAtlas) return;
    ImgWindow::sFontAtlas = std::make_shared<ImgFontAtlas>();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    gFonts.regular = ImgWindow::sFontAtlas->AddFontFromMemoryCompressedTTF(
        gf_roboto_regular_400_compressed_data, gf_roboto_regular_400_compressed_size, 15.0f, &cfg);
    gFonts.bold = ImgWindow::sFontAtlas->AddFontFromMemoryCompressedTTF(
        gf_roboto_bold_compressed_data, gf_roboto_bold_compressed_size, 15.0f, &cfg);
    gFonts.mono = ImgWindow::sFontAtlas->AddFontFromMemoryCompressedTTF(
        gf_robotomono_regular_400_compressed_data, gf_robotomono_regular_400_compressed_size, 14.0f, &cfg);
}

} // namespace fxa::ui
