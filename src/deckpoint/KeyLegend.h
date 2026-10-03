#pragma once

// DECKPOINT: on-screen key legends that name T-Deck Pro keys the way their
// caps do. The Esc key is the one printed with a microphone, so legends draw
// the glyph next to the word.

#include <Icon.h>

class GfxRenderer;

namespace deckpoint {

struct LegendSnapshot;

// Draw a freeink::Icon (logical-frame bits) at x,y through the renderer.
void drawIconLogical(const GfxRenderer& renderer, const freeink::Icon& icon, int x, int y);

// Extra screen-specific hint ("h/l: tab") folded into the next legend drawn
// by drawHintLegend(); callers set it around their footer and clear it after.
void setLegendExtra(const char* extra);

// True when legends should be drawn (keyboard present + Settings > Key Legend).
bool keyLegendEnabled();

// Height the bottom legend band occupies (0 when legends are off).
int keyLegendBandHeight(const GfxRenderer& renderer);

// Turn a screen's button-hint labels (what Back / Confirm / Previous / Next
// do there) into a key legend on the bottom band, e.g.
//   "Esc (mic): Back   Enter: Select   j/k: move".
// Empty labels are skipped. Used by every theme's drawButtonHints() on
// keyboard boards, so screens get legends without per-screen code.
void drawHintLegend(const GfxRenderer& renderer, const char* back, const char* confirm, const char* previous,
                    const char* next);

// Copy of the last legend drawHintLegend() saw (recorded even when legends
// are switched off), tagged with the activity on top when it was drawn. The
// key help screen falls back to it. Caller must hold a RenderLock.
void copyLastLegend(LegendSnapshot& out);

// Draw a one-line key legend in `fontId`, centered, its baseline row at the
// bottom of the screen (with a small margin). Returns the legend's top y.
int drawBottomKeyLegend(const GfxRenderer& renderer, int fontId, const char* text);

// Draw `before` + "Esc (" + [mic glyph] + ")" + `after`, centered on the screen at y.
void drawCenteredEscLegend(const GfxRenderer& renderer, int fontId, int y, const char* before, const char* after);

}  // namespace deckpoint
