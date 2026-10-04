

#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace Lyra3CoversMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeCoverTileHeight = 300;
  v.homeRecentBooksCount = 3;
  v = DECKPOINT_THEME_METRICS(v);
#if defined(DECKPOINT_COMPACT_UI) && DECKPOINT_COMPACT_UI
  // DECKPOINT: three ~66px columns. Cover slot sized to the 0.6-aspect thumb
  // a 66px slot asks for; the tile covers 4px pad + cover + 4px gap + two
  // 13px caption lines + 4px pad, so the menu starts below the captions.
  v.homeRecentBooksCount = 3;
  v.homeCoverHeight = 110;
  v.homeCoverTileHeight = 148;
#endif
  return v;
}();
}  // namespace Lyra3CoversMetrics

class Lyra3CoversTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  int homeCoverThumbHeight(const GfxRenderer& renderer) const override;
};
