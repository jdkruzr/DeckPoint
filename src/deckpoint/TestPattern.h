#pragma once

// DECKPOINT: on-glass calibration pattern (serial: CMD:TESTPATTERN).

class GfxRenderer;

namespace deckpoint {
void drawTestPattern(GfxRenderer& renderer);
}  // namespace deckpoint
