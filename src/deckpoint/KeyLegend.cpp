#include "KeyLegend.h"

#include <GfxRenderer.h>
#include <HalKeyboard.h>
#include <I18n.h>

#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "fontIds.h"
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

std::string cleanLabel(const char* label);

namespace {
const char* s_legendExtra = nullptr;
constexpr char MIC = '\x01';  // placeholder for the Esc key's microphone glyph
constexpr int BOTTOM_MARGIN = 3;

bool isDirection(const char* label) {
  if (!label || !*label) return false;
  static const StrId dirs[] = {StrId::STR_DIR_UP, StrId::STR_DIR_DOWN, StrId::STR_DIR_LEFT, StrId::STR_DIR_RIGHT};
  for (const StrId id : dirs) {
    if (strcmp(label, I18N.get(id)) == 0) return true;
  }
  return strcmp(label, "<") == 0 || strcmp(label, ">") == 0 || strcmp(label, "^") == 0 || strcmp(label, "v") == 0;
}

int glyphAwareWidth(const GfxRenderer& renderer, const int fontId, const std::string& text) {
  int w = 0;
  size_t start = 0;
  while (true) {
    const size_t at = text.find(MIC, start);
    w += renderer.getTextWidth(fontId, text.substr(start, at - start).c_str());
    if (at == std::string::npos) break;
    w += Mic12Icon.w + 2;
    start = at + 1;
  }
  return w;
}

void drawGlyphAware(const GfxRenderer& renderer, const int fontId, int x, const int y, const std::string& text) {
  size_t start = 0;
  while (true) {
    const size_t at = text.find(MIC, start);
    const std::string part = text.substr(start, at - start);
    renderer.drawText(fontId, x, y, part.c_str());
    x += renderer.getTextWidth(fontId, part.c_str());
    if (at == std::string::npos) break;
    x += 1;
    drawIconLogical(renderer, Mic12Icon, x, y + (renderer.getLineHeight(fontId) - static_cast<int>(Mic12Icon.h)) / 2);
    x += Mic12Icon.w + 1;
    start = at + 1;
  }
}
}  // namespace

// Button-hint labels carry glyph decorations ("« Back", "Next »") that read
// as noise next to a key name; drop them.
std::string cleanLabel(const char* label) {
  std::string s = label ? label : "";
  for (const char* deco : {"\xC2\xAB", "\xC2\xBB", "\xE2\x80\xB9", "\xE2\x80\xBA"}) {  // « » ‹ ›
    size_t at;
    while ((at = s.find(deco)) != std::string::npos) s.erase(at, strlen(deco));
  }
  const size_t b = s.find_first_not_of(' ');
  const size_t e = s.find_last_not_of(' ');
  return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

void setLegendExtra(const char* extra) { s_legendExtra = extra; }

bool keyLegendEnabled() { return halKeyboard.present() && SETTINGS.keyLegend != 0; }

int keyLegendBandHeight(const GfxRenderer& renderer) {
  return keyLegendEnabled() ? renderer.getLineHeight(SMALL_FONT_ID) + BOTTOM_MARGIN + 2 : 0;
}

void drawHintLegend(const GfxRenderer& renderer, const char* back, const char* confirm, const char* previous,
                    const char* next) {
  if (!keyLegendEnabled()) return;
  const auto nonEmpty = [](const char* s) { return s != nullptr && *s != '\0'; };
  std::string parts[5];
  int count = 0;
  const std::string backL = cleanLabel(back), confirmL = cleanLabel(confirm);
  const std::string prevL = cleanLabel(previous), nextL = cleanLabel(next);
  if (!backL.empty()) parts[count++] = std::string("Esc (") + MIC + "): " + backL;
  if (!confirmL.empty()) parts[count++] = std::string("Enter: ") + confirmL;
  if (s_legendExtra && *s_legendExtra) parts[count++] = s_legendExtra;
  const bool prevDir = isDirection(previous), nextDir = isDirection(next);
  if ((prevDir || !nonEmpty(previous)) && (nextDir || !nonEmpty(next)) && (prevDir || nextDir)) {
    parts[count++] = "j/k: move";
  } else {
    if (!prevL.empty()) parts[count++] = std::string("k: ") + prevL;
    if (!nextL.empty()) parts[count++] = std::string("j: ") + nextL;
  }
  if (count == 0) return;

  // Widest spacing that fits, then fewer parts if even single spaces overflow.
  const int maxW = renderer.getScreenWidth() - 8;
  std::string line;
  for (const char* gap : {"    ", "   ", "  "}) {
    line.clear();
    for (int i = 0; i < count; i++) line += (i ? gap : "") + parts[i];
    if (glyphAwareWidth(renderer, SMALL_FONT_ID, line) <= maxW) break;
  }
  while (count > 1 && glyphAwareWidth(renderer, SMALL_FONT_ID, line) > maxW) {
    count--;
    line.clear();
    for (int i = 0; i < count; i++) line += (i ? "  " : "") + parts[i];
  }
  const int y = renderer.getScreenHeight() - renderer.getLineHeight(SMALL_FONT_ID) - BOTTOM_MARGIN;
  const int x = (renderer.getScreenWidth() - glyphAwareWidth(renderer, SMALL_FONT_ID, line)) / 2;
  drawGlyphAware(renderer, SMALL_FONT_ID, x, y, line);
}

int drawBottomKeyLegend(const GfxRenderer& renderer, const int fontId, const char* text) {
  if (!keyLegendEnabled()) return renderer.getScreenHeight();
  const int y = renderer.getScreenHeight() - renderer.getLineHeight(fontId) - BOTTOM_MARGIN;
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
