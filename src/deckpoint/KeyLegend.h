#pragma once

// DECKPOINT: on-screen key legends that name T-Deck Pro keys the way their
// caps do. The Esc key is the one printed with a microphone, so legends draw
// the glyph next to the word.

#include <Icon.h>

class GfxRenderer;

namespace deckpoint {

// Draw a freeink::Icon (logical-frame bits) at x,y through the renderer.
void drawIconLogical(const GfxRenderer& renderer, const freeink::Icon& icon, int x, int y);

// Draw a one-line key legend in `fontId`, centered, its baseline row at the
// bottom of the screen (with a small margin). Returns the legend's top y.
int drawBottomKeyLegend(const GfxRenderer& renderer, int fontId, const char* text);

// Draw `before` + "Esc (" + [mic glyph] + ")" + `after`, centered on the screen at y.
void drawCenteredEscLegend(const GfxRenderer& renderer, int fontId, int y, const char* before, const char* after);

}  // namespace deckpoint
