#include "KeyLegend.h"

#include <GfxRenderer.h>

#include <string>

#include "icons/mic12.h"

namespace deckpoint {

// freeink::Icon bits are in the logical frame (not pre-rotated), unlike the
// legacy GfxRenderer::drawIcon format, so plot them through drawPixel and let
// the renderer apply the orientation like it does for text.
void drawIconLogical(const GfxRenderer& renderer, const freeink::Icon& icon, const int x, const int y) {
  const int rowBytes = (icon.w + 7) / 8;
  for (int r = 0; r < icon.h; r++) {
    for (int c = 0; c < icon.w; c++) {
      const bool transparent = icon.bits[r * rowBytes + c / 8] & (0x80 >> (c % 8));
      if (!transparent) renderer.drawPixel(x + c, y + r);
    }
  }
}

int drawBottomKeyLegend(const GfxRenderer& renderer, const int fontId, const char* text) {
  constexpr int bottomMargin = 4;
  const int y = renderer.getScreenHeight() - renderer.getLineHeight(fontId) - bottomMargin;
  renderer.drawCenteredText(fontId, y, text, true);
  return y;
}

void drawCenteredEscLegend(const GfxRenderer& renderer, const int fontId, const int y, const char* before,
                           const char* after) {
  constexpr int iconGap = 1;
  const std::string head = std::string(before) + "Esc (";
  const std::string tail = std::string(")") + after;
  const int headW = renderer.getTextWidth(fontId, head.c_str());
  const int afterW = renderer.getTextWidth(fontId, tail.c_str());
  const int iconW = Mic12Icon.w;
  const int total = headW + iconGap + iconW + iconGap + afterW;
  int x = (renderer.getScreenWidth() - total) / 2;
  renderer.drawText(fontId, x, y, head.c_str());
  x += headW + iconGap;
  // Center the glyph on the text line.
  const int iconY = y + (renderer.getLineHeight(fontId) - static_cast<int>(Mic12Icon.h)) / 2;
  drawIconLogical(renderer, Mic12Icon, x, iconY);
  x += iconW + iconGap;
  renderer.drawText(fontId, x, y, tail.c_str());
}

}  // namespace deckpoint
