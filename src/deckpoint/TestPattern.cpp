#include "TestPattern.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <cstdio>

#include "fontIds.h"

namespace deckpoint {

// A full-screen calibration image in the logical Portrait frame:
//  * three nested frames at insets 0/3/6 px — count the visible ones per edge
//    to measure how far the bezel overlaps the glass;
//  * rulers along the top and left edges, ticks every 10 px, labels every 50;
//  * corner labels and an "UP" arrow to catch rotation/mirroring;
//  * 1/2/4 px checkerboards to check pixel pitch and refresh fidelity.
// Lay a real ruler alongside to get the physical PPI.
void drawTestPattern(GfxRenderer& renderer) {
  const auto savedOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  renderer.clearScreen();

  for (int inset = 0; inset <= 6; inset += 3) {
    renderer.drawRect(inset, inset, w - 2 * inset, h - 2 * inset);
  }

  char label[8];
  for (int x = 10; x < w; x += 10) {
    const bool major = x % 50 == 0;
    renderer.drawLine(x, 7, x, 7 + (major ? 12 : 5));
    if (major && x + 14 < w) {
      snprintf(label, sizeof(label), "%d", x);
      renderer.drawText(SMALL_FONT_ID, x + 2, 18, label);
    }
  }
  for (int y = 10; y < h; y += 10) {
    const bool major = y % 50 == 0;
    renderer.drawLine(7, y, 7 + (major ? 12 : 5), y);
    if (major && y + 12 < h) {
      snprintf(label, sizeof(label), "%d", y);
      renderer.drawText(SMALL_FONT_ID, 22, y - 6, label);
    }
  }

  renderer.drawText(UI_10_FONT_ID, 44, 34, "TL");
  renderer.drawText(UI_10_FONT_ID, w - 40, 34, "TR");
  renderer.drawText(UI_10_FONT_ID, 44, h - 40, "BL");
  renderer.drawText(UI_10_FONT_ID, w - 40, h - 40, "BR");

  // Asymmetric arrow: a flag on the right side of the shaft shows mirroring.
  const int cx = w / 2;
  renderer.drawLine(cx, 60, cx, 130, 3, true);
  renderer.drawLine(cx, 60, cx - 14, 78, 3, true);
  renderer.drawLine(cx, 60, cx + 14, 78, 3, true);
  renderer.fillRect(cx + 2, 100, 18, 10);
  renderer.drawText(UI_10_FONT_ID, cx - 12, 134, "UP");

  snprintf(label, sizeof(label), "%dx%d", w, h);
  renderer.drawText(UI_10_FONT_ID, cx - 34, 160, label);

  const int boxY = 200, box = 40;
  const int pitches[] = {1, 2, 4};
  for (int i = 0; i < 3; i++) {
    const int bx = 40 + i * 60;
    const int p = pitches[i];
    for (int yy = 0; yy < box; yy++) {
      for (int xx = 0; xx < box; xx++) {
        if (((xx / p) + (yy / p)) % 2 == 0) renderer.drawPixel(bx + xx, boxY + yy);
      }
    }
    snprintf(label, sizeof(label), "%dpx", p);
    renderer.drawText(SMALL_FONT_ID, bx + 6, boxY + box + 4, label);
  }

  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  renderer.setOrientation(savedOrientation);
}

}  // namespace deckpoint
