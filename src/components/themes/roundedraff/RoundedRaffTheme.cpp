#include "RoundedRaffTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "deckpoint/KeyLegend.h"  // DECKPOINT
#include "fontIds.h"

namespace {
#if defined(DECKPOINT_COMPACT_UI) && DECKPOINT_COMPACT_UI
// DECKPOINT: compact pills and card (15px bold labels on ~25px rows).
constexpr int kCoverRadius = 8;
constexpr int kInteractiveInsetX = 12;
constexpr int kMenuRowPaddingY = 4;
#else
constexpr int kCoverRadius = 18;
constexpr int kInteractiveInsetX = 20;
constexpr int kMenuRowPaddingY = 10;
#endif
constexpr int kMenuRadius = 30;
constexpr int kBottomRadius = 15;
constexpr int kRowRadius = 20;
constexpr int kTitleFontId = UI_12_FONT_ID;  // Requested main title size: 12px
constexpr int kGuideFontId = SMALL_FONT_ID;  // Closest available to requested 6px

void drawScrollBar(const GfxRenderer& renderer, Rect rect, int itemCount, int pageStartIndex, int pageItems) {
  if (itemCount <= 0 || pageItems <= 0 || itemCount <= pageItems) {
    return;
  }

  const int barW = RoundedRaffMetrics::values.scrollBarWidth;
  const int barX = rect.x + rect.width - RoundedRaffMetrics::values.scrollBarRightOffset - barW;
  const int barY = rect.y;
  const int barH = rect.height;

  const int thumbH = std::max(10, (barH * pageItems) / itemCount);
  const int maxStart = std::max(1, itemCount - pageItems);
  const int maxTravel = std::max(1, barH - thumbH);
  const int clampedStart = std::clamp(pageStartIndex, 0, maxStart);
  const int thumbY = barY + (clampedStart * maxTravel) / maxStart;

  renderer.fillRect(barX, thumbY, barW, thumbH);
}

#if defined(DECKPOINT_COMPACT_UI) && DECKPOINT_COMPACT_UI
// DECKPOINT: compact home card. The stock layout centers a 300px cover in a
// dithered tile and puts the title in the header band; at 240x320 that
// truncated the title and pushed the menu off-screen. Here a white rounded
// card holds the cover at its left with the title and author beside it
// (Continue Reading stays the first menu pill, so the card has no selection).
void drawCompactHomeCard(GfxRenderer& renderer, const Rect rect, const std::vector<RecentBook>& recentBooks,
                         bool& coverRendered, bool& coverBufferStored, const std::function<bool()>& storeCoverBuffer) {
  constexpr int inset = 4;
  constexpr int textGap = 8;
  constexpr int cardRadius = 12;
  static int cardCoverWidth = 0;
  const int cardX = rect.x + RoundedRaffMetrics::values.contentSidePadding;
  const int cardW = rect.width - 2 * RoundedRaffMetrics::values.contentSidePadding;
  const int cardY = rect.y;
  const int cardH = rect.height;

  if (recentBooks.empty()) {
    renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 1, cardRadius, true);
    renderer.drawCenteredText(kTitleFontId, cardY + (cardH - renderer.getLineHeight(kTitleFontId)) / 2,
                              tr(STR_NO_OPEN_BOOK), true, EpdFontFamily::BOLD);
    return;
  }

  const RecentBook& book = recentBooks[0];
  const int coverH = std::min(RoundedRaffMetrics::values.homeCoverHeight, cardH - 2 * inset);
  if (cardCoverWidth == 0) cardCoverWidth = coverH * 3 / 5;
  const int coverX = cardX + inset;
  const int coverY = cardY + (cardH - coverH) / 2;

  if (!coverRendered) {
    bool hasCover = false;
    if (!book.coverBmpPath.empty()) {
      const std::string coverBmpPath =
          UITheme::getCoverThumbPath(book.coverBmpPath, RoundedRaffMetrics::values.homeCoverHeight);
      HalFile file;
      if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          cardCoverWidth = std::min(bitmap.getWidth(), cardW / 2);
          hasCover = BaseTheme::drawCoverThumbFill(renderer, bitmap, Rect{coverX, coverY, cardCoverWidth, coverH});
        }
      }
    }
    if (!hasCover) BaseTheme::drawCoverPlaceholder(renderer, Rect{coverX, coverY, cardCoverWidth, coverH});
    renderer.maskRoundedRectOutsideCorners(coverX, coverY, cardCoverWidth, coverH, kCoverRadius, Color::White);
    renderer.drawRoundedRect(coverX, coverY, cardCoverWidth, coverH, 1, kCoverRadius, true);
    renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 1, cardRadius, true);
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  const int textX = coverX + cardCoverWidth + textGap;
  const int textW = cardX + cardW - inset - 2 - textX;
  if (textW <= 0) return;
  const int titleLineHeight = renderer.getLineHeight(kTitleFontId);
  const int authorLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int authorH = book.author.empty() ? 0 : authorLineHeight + 2;
  const int titleLines = std::clamp((cardH - 2 * inset - authorH) / std::max(1, titleLineHeight), 1, 4);
  const auto lines = renderer.wrappedText(kTitleFontId, book.title.c_str(), textW, titleLines, EpdFontFamily::BOLD);
  int y = cardY + (cardH - static_cast<int>(lines.size()) * titleLineHeight - authorH) / 2;
  for (const auto& line : lines) {
    renderer.drawText(kTitleFontId, textX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += titleLineHeight;
  }
  if (!book.author.empty()) {
    const auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textW);
    renderer.drawText(UI_10_FONT_ID, textX, y + 2, author.c_str(), true);
  }
}
#endif

}  // namespace
int coverWidth = 0;

void RoundedRaffTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                                  const bool backButton) const {
#if defined(DECKPOINT_COMPACT_UI) && DECKPOINT_COMPACT_UI
  // DECKPOINT: the home band (shorter than a full header) keeps only the
  // battery; the compact home card shows the book title in full instead.
  if (rect.height < UITheme::getInstance().getMetrics().headerHeight) {
    BaseTheme::drawHeader(renderer, rect, nullptr, nullptr, backButton);
    return;
  }
#endif
  // Home screen header is custom-rendered in drawRecentBookCover.
  if (title == nullptr) {
    return;
  }
  BaseTheme::drawHeader(renderer, rect, title, subtitle, backButton);
}

void RoundedRaffTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
#if defined(DECKPOINT_COMPACT_UI) && DECKPOINT_COMPACT_UI
  (void)selectorIndex;
  (void)bufferRestored;
  drawCompactHomeCard(renderer, rect, recentBooks, coverRendered, coverBufferStored, storeCoverBuffer);
  return;
#endif
  const int tileWidth = rect.width - 2 * RoundedRaffMetrics::values.contentSidePadding;
  const int tileHeight = rect.height;
  const int tileY = rect.y;
  const bool hasContinueReading = !recentBooks.empty();
  if (coverWidth == 0) {
    coverWidth = RoundedRaffMetrics::values.homeCoverHeight * 0.6;
  }
  const int imgY = tileY + (tileHeight - RoundedRaffMetrics::values.homeCoverHeight) / 2;
  const int tileX = RoundedRaffMetrics::values.contentSidePadding;

  // Draw book card regardless, fill with message based on `hasContinueReading`
  // Draw cover image as background if available (inside the box)
  // Only load from SD on first render, then use stored buffer
  if (hasContinueReading) {
    RecentBook book = recentBooks[0];
    if (!coverRendered) {
      std::string coverPath = book.coverBmpPath;
      bool hasCover = true;
      if (coverPath.empty()) {
        hasCover = false;
      } else {
        const std::string coverBmpPath =
            UITheme::getCoverThumbPath(coverPath, RoundedRaffMetrics::values.homeCoverHeight);

        // First time: load cover from SD and render
        HalFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            coverWidth = bitmap.getWidth();
            // Narrow covers come out taller than the slot; fill 1:1 and crop
            // vertically instead of rescaling the dither.
            drawCoverThumbFill(renderer, bitmap,
                               Rect{tileX + (tileWidth - coverWidth) / 2, imgY, coverWidth,
                                    RoundedRaffMetrics::values.homeCoverHeight});
            renderer.maskRoundedRectOutsideCorners(tileX + (tileWidth - coverWidth) / 2, imgY, coverWidth,
                                                   RoundedRaffMetrics::values.homeCoverHeight, kCoverRadius,
                                                   Color::LightGray);
          } else {
            hasCover = false;
          }
          file.close();
        }
      }

      // Draw either way
      renderer.drawRoundedRect(tileX + (tileWidth - coverWidth) / 2, imgY, coverWidth,
                               RoundedRaffMetrics::values.homeCoverHeight, 1, kCoverRadius, true);

      if (!hasCover) {
        // Render empty cover
        renderer.fillRect(tileX + (tileWidth - coverWidth) / 2, imgY + (RoundedRaffMetrics::values.homeCoverHeight / 3),
                          coverWidth, 2 * RoundedRaffMetrics::values.homeCoverHeight / 3, true);
        renderer.drawIcon(CoverIcon, tileX + (tileWidth - coverWidth) / 2 + 24, imgY + 24, 32);
        renderer.maskRoundedRectOutsideCorners(tileX + (tileWidth - coverWidth) / 2, imgY, coverWidth,
                                               RoundedRaffMetrics::values.homeCoverHeight, kCoverRadius,
                                               Color::LightGray);
      }

      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
    }

    renderer.fillRoundedRect(tileX, tileY, tileWidth, imgY - tileY, kRowRadius, true, true, false, false,
                             Color::LightGray);
    renderer.fillRectDither(tileX, imgY, (tileWidth - coverWidth) / 2, RoundedRaffMetrics::values.homeCoverHeight,
                            Color::LightGray);
    renderer.fillRectDither(tileX + (tileWidth + coverWidth) / 2, imgY, (tileWidth - coverWidth) / 2,
                            RoundedRaffMetrics::values.homeCoverHeight, Color::LightGray);
    renderer.fillRoundedRect(tileX, imgY + RoundedRaffMetrics::values.homeCoverHeight, tileWidth,
                             tileHeight - (imgY - tileY + RoundedRaffMetrics::values.homeCoverHeight), kRowRadius,
                             false, false, true, true, Color::LightGray);
  } else {
    renderer.fillRoundedRect(tileX, tileY, tileWidth, tileHeight, kRowRadius, Color::LightGray);
    renderer.drawCenteredText(kTitleFontId, rect.y + rect.height / 2 - renderer.getLineHeight(kTitleFontId) / 2,
                              tr(STR_NO_OPEN_BOOK));
  }
}

int RoundedRaffTheme::getMenuRowHeight(const GfxRenderer& renderer) const {
  return renderer.getLineHeight(kTitleFontId) + 2 * kMenuRowPaddingY;
}

void RoundedRaffTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                      const std::function<std::string(int index)>& buttonLabel,
                                      const std::function<UIIcon(int index)>& rowIcon) const {
  (void)rowIcon;
  const int sidePadding = RoundedRaffMetrics::values.contentSidePadding;
  const int rowX = rect.x + sidePadding;
  const int rowHeight = getMenuRowHeight(renderer);           // shared with HomeActivity's touch grid
  const int rowGap = RoundedRaffMetrics::values.menuSpacing;  // HomeActivity's touch grid steps by the same gap
  const int rowStep = rowHeight + rowGap;
  const int pageItems = std::max(1, rect.height / rowStep);
  const int safeSelectedIndex = std::max(0, selectedIndex);
  const int pageStartIndex = (safeSelectedIndex / pageItems) * pageItems;
  const int menuTop = rect.y;
  const int textLineHeight = renderer.getLineHeight(kTitleFontId);
  const int menuMaxWidth = std::max(0, rect.width - sidePadding * 2);

  for (int i = pageStartIndex; i < buttonCount && i < pageStartIndex + pageItems; ++i) {
    const std::string label = buttonLabel(i);
    const int rowY = menuTop + (i - pageStartIndex) * rowStep;
    constexpr int kRowPaddingX = 2 * kInteractiveInsetX;
    const int maxLabelWidth = std::max(0, menuMaxWidth - kRowPaddingX);
    const std::string truncatedLabel =
        renderer.truncatedText(kTitleFontId, label.c_str(), maxLabelWidth, EpdFontFamily::BOLD);
    const int rowWidth = std::min(
        menuMaxWidth, renderer.getTextWidth(kTitleFontId, truncatedLabel.c_str(), EpdFontFamily::BOLD) + kRowPaddingX);
    const bool isSelected = selectedIndex == i;
    renderer.fillRoundedRect(rowX, rowY, rowWidth, rowHeight, kMenuRadius, isSelected ? Color::Black : Color::White);
    const int textY = rowY + (rowHeight - textLineHeight) / 2;
    const int textX = rowX + kInteractiveInsetX;
    if (selectedIndex == i) {
      renderer.drawText(kTitleFontId, textX, textY, truncatedLabel.c_str(), false, EpdFontFamily::BOLD);
    } else {
      renderer.drawText(kTitleFontId, textX, textY, truncatedLabel.c_str(), true, EpdFontFamily::BOLD);
    }
  }

  drawScrollBar(renderer, rect, buttonCount, pageStartIndex, pageItems);
}

void RoundedRaffTheme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode,
                                     int contentStartX, int contentWidth) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineY = rect.y + rect.height + lineHeight + metrics.verticalSpacing;
  const int thickness = cursorMode ? 3 : 2;

  if (contentWidth > 0) {
    renderer.drawLine(rect.x + contentStartX, lineY, rect.x + contentStartX + contentWidth - 1, lineY, thickness, true);
    return;
  }

  constexpr int hPadding = 8;
  const int lineW = textWidth + hPadding * 2;
  const int lineStart = rect.x + (rect.width - lineW) / 2;
  renderer.drawLine(lineStart, lineY, lineStart + lineW - 1, lineY, thickness, true);
}

void RoundedRaffTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                       const char* btn4) const {
  if (gpio.hidesButtonHints()) {  // DECKPOINT: was hasTouch()
    // Keyboard boards: the same labels become a key legend.
    if (!gpio.hasTouch()) deckpoint::drawHintLegend(renderer, btn1, btn2, btn3, btn4);
    return;
  }

  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = 20;
  const int groupGap = 10;
  const int bottomMargin = 10;
  const int hintHeight = RoundedRaffMetrics::values.buttonHintsHeight - 10;  // 30px total guide height
  const int groupWidth = (pageWidth - sidePadding * 2 - groupGap) / 2;
  const int hintY = pageHeight - hintHeight - bottomMargin;
  const int textY = hintY + (hintHeight - renderer.getLineHeight(kGuideFontId)) / 2;

  if (renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute()) {
    renderer.fillRect(sidePadding, hintY, groupWidth, hintHeight, true);
    renderer.fillRect(sidePadding + groupWidth + groupGap, hintY, groupWidth, hintHeight, true);
    renderer.setOrientation(origOrientation);
    return;
  }

  const bool backDisabled = (btn1 == nullptr || btn1[0] == '\0');
  const int leftGroupX = sidePadding;
  const int rightGroupX = leftGroupX + groupWidth + groupGap;
  const std::string backLabel = backDisabled ? "" : std::string(btn1);
  // Callers should provide the button labels. If a label is not specified, it should render empty.
  const std::string selectText = (btn2 && btn2[0] != '\0') ? std::string(btn2) : "";
  const std::string upText = (btn3 && btn3[0] != '\0') ? std::string(btn3) : "";
  const std::string downText = (btn4 && btn4[0] != '\0') ? std::string(btn4) : "";

  // Ensure button hints always "win" visually even if other elements accidentally render into this area.
  renderer.fillRect(leftGroupX, hintY, groupWidth, hintHeight, false);
  renderer.fillRect(rightGroupX, hintY, groupWidth, hintHeight, false);

  renderer.drawRoundedRect(leftGroupX, hintY, groupWidth, hintHeight, 2, kBottomRadius, true);
  const int selectWidth = renderer.getTextWidth(kGuideFontId, selectText.c_str(), EpdFontFamily::REGULAR);
  const int downWidth = renderer.getTextWidth(kGuideFontId, downText.c_str(), EpdFontFamily::REGULAR);
  constexpr int innerEdgePadding = 16;

  const int backX = leftGroupX + innerEdgePadding;
  const int selectX = leftGroupX + groupWidth - innerEdgePadding - selectWidth;
  const int upX = rightGroupX + innerEdgePadding;
  const int downX = rightGroupX + groupWidth - innerEdgePadding - downWidth;

  if (!backDisabled) {
    renderer.drawText(kGuideFontId, backX, textY, backLabel.c_str(), true, EpdFontFamily::REGULAR);
  }
  renderer.drawText(kGuideFontId, selectX, textY, selectText.c_str(), true, EpdFontFamily::REGULAR);

  renderer.drawRoundedRect(rightGroupX, hintY, groupWidth, hintHeight, 2, kBottomRadius, true);

  renderer.drawText(kGuideFontId, upX, textY, upText.c_str(), true, EpdFontFamily::REGULAR);
  renderer.drawText(kGuideFontId, downX, textY, downText.c_str(), true, EpdFontFamily::REGULAR);

  renderer.setOrientation(origOrientation);
}
