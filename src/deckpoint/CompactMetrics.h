#pragma once

// DECKPOINT: compact theme metrics for small panels (T-Deck Pro, 3.1" 240x320).
//
// CrossPoint's themes are tuned for ~480x800 panels at ~220 PPI. On a 129 PPI
// 240x320 panel the same pixel sizes come out ~1.7x larger physically and only
// a third of the rows fit. Themes read their constexpr metric tables directly
// (e.g. LyraMetrics::values.menuRowHeight) in ~70 places, so rather than chase
// every read, each theme wraps its table in DECKPOINT_THEME_METRICS(...) and a
// compact build (-DDECKPOINT_COMPACT_UI) rewrites the table at compile time.
//
// Included by BaseTheme.h after ThemeMetrics is defined; do not include
// directly before that struct exists.

#include <algorithm>

namespace deckpoint {

// Pixel metrics for a ~240px-wide portrait UI with 10pt UI fonts (24px line).
constexpr ThemeMetrics compactMetrics(ThemeMetrics m) {
  m.batteryWidth = 12;
  m.batteryHeight = 8;
  m.topPadding = 3;
  m.batteryBarHeight = 14;
  m.headerHeight = 34;
  m.verticalSpacing = 6;

  m.previewPadding = 6;
  m.contentSidePadding = 8;
  m.listRowHeight = 28;
  m.listWithSubtitleRowHeight = 44;
  m.listInset = 4;
  m.listSidePadding = 6;
  m.headerSidePadding = 8;
  m.headerUnderlineSize = std::min(m.headerUnderlineSize, 2);

  m.menuRowHeight = 30;
  m.menuSpacing = 2;
  m.tabSpacing = 4;
  m.tabBarHeight = 30;
  m.coverGridTabBarHeight = 36;

  m.homeTopPadding = 20;
  m.homeCoverHeight = 96;
  m.homeCoverTileHeight = 104;
  m.homeRecentBooksCount = std::min(m.homeRecentBooksCount, 1);
  m.homeMenuTopOffset = 4;

  m.buttonHintsHeight = 0;
  m.sideButtonHintsWidth = 0;

  m.progressBarHeight = 8;
  m.statusBarHorizontalMargin = 4;
  m.statusBarVerticalMargin = 8;
  m.keyboardKeyHeight = 28;

  m.popupMarginX = 8;
  m.popupMarginY = 6;
  m.optionPopupItemSpacing = 4;
  m.optionPopupInnerPadding = 8;
  m.optionPopupSelectionVPadding = 6;
  m.optionPopupDialogSideMargin = 6;
  return m;
}

}  // namespace deckpoint

#if defined(DECKPOINT_COMPACT_UI) && DECKPOINT_COMPACT_UI
#define DECKPOINT_THEME_METRICS(...) ::deckpoint::compactMetrics(__VA_ARGS__)
#else
#define DECKPOINT_THEME_METRICS(...) __VA_ARGS__
#endif
