#include "KeyHelpActivity.h"

#include <GfxRenderer.h>
#include <HalKeyboard.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "KeyLegend.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace deckpoint {

namespace {

struct ScreenTitle {
  const char* activityName;
  StrId title;
};

constexpr ScreenTitle SCREEN_TITLES[] = {
    {"Home", StrId::STR_KH_TITLE_HOME},
    {"Library", StrId::STR_LIBRARY},
    {"FileBrowser", StrId::STR_BROWSE_FILES},
    {"Settings", StrId::STR_SETTINGS_TITLE},
    {"TextSettings", StrId::STR_TEXT_SETTINGS},
    {"EpubReader", StrId::STR_KH_TITLE_READER},
    {"EpubReaderMenu", StrId::STR_READER_MENU},
    {"EpubReaderBookmarks", StrId::STR_BOOKMARKS},
    {"DictionaryWordSelect", StrId::STR_KH_TITLE_WORD_SELECT},
    {"DictionaryDefinition", StrId::STR_DICTIONARY},
    {"KeyboardEntry", StrId::STR_KH_TITLE_TEXT_ENTRY},
    {"BmpViewer", StrId::STR_KH_TITLE_IMAGE},
    {"EpubReaderPercentSelection", StrId::STR_GO_TO_PERCENT},
    {"NetworkModeSelection", StrId::STR_FILE_TRANSFER},
    {"CrossPointWebServer", StrId::STR_FILE_TRANSFER},
    {"WifiSelection", StrId::STR_WIFI_NETWORKS},
    {"EpubReaderChapterSelection", StrId::STR_SELECT_CHAPTER},
    {"XtcReaderChapterSelection", StrId::STR_SELECT_CHAPTER},
    {"EpubReaderFootnoteSelect", StrId::STR_FOOTNOTES},
    {"KOReaderSettings", StrId::STR_KOREADER_SYNC},
    {"KOReaderSync", StrId::STR_SYNC_PROGRESS},
    {"KOReaderAuth", StrId::STR_KOREADER_AUTH},
    {"OpdsBookBrowser", StrId::STR_OPDS_BROWSER},
    {"OpdsServerList", StrId::STR_OPDS_SERVERS},
    {"LanguageSelect", StrId::STR_LANGUAGE},
    {"TimezonePicker", StrId::STR_TIMEZONE},
    {"About", StrId::STR_ABOUT},
    {"UsbDrive", StrId::STR_USB_DRIVE},
    {"CalibreConnect", StrId::STR_CALIBRE_WIRELESS},
    {"FontDownload", StrId::STR_MANAGE_FONTS},
    {"ClockSettings", StrId::STR_CLOCK},
    {"KeyboardLayouts", StrId::STR_KEYBOARD_LAYOUTS},
    {"QrDisplay", StrId::STR_DISPLAY_QR},
    {"StatusBarSettings", StrId::STR_CUSTOMISE_STATUS_BAR},
    {"PluginCatalog", StrId::STR_PLUGINS},
    {"ButtonRemap", StrId::STR_REMAP_FRONT_BUTTONS},
    {"HomeButtonSettings", StrId::STR_HOME_BUTTON},
};

constexpr int ROW_GAP = 3;
constexpr int COLUMN_GAP = 8;
constexpr int KEY_MAX_LINES = 2;
constexpr int WHAT_MAX_LINES = 3;

}  // namespace

KeyHelpActivity::KeyHelpActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* owner,
                                 const KeyHelp* table)
    : Activity("KeyHelp", renderer, mappedInput), table(table) {
  snprintf(ownerName, sizeof(ownerName), "%s", owner ? owner : "");
  title[0] = '\0';
}

void KeyHelpActivity::onEnter() {
  Activity::onEnter();
  {
    // Legends are recorded on the render task.
    RenderLock lock;
    copyLastLegend(legend);
  }
  buildRows();
  requestUpdate();
}

void KeyHelpActivity::addRow(const char* keys, const char* what, const bool heading) {
  if (rowCount >= MAX_ROWS) return;
  rows[rowCount++] = Row{keys ? keys : "", what ? what : "", heading};
}

void KeyHelpActivity::buildRows() {
  const char* screen = nullptr;
  for (const auto& entry : SCREEN_TITLES) {
    if (strcmp(entry.activityName, ownerName) == 0) {
      screen = I18N.get(entry.title);
      break;
    }
  }
  char humanized[sizeof(ownerName) + 8];
  if (!screen) {
    humanizeActivityName(ownerName, humanized, sizeof(humanized));
    screen = humanized;
  }
  snprintf(title, sizeof(title), "%s \xE2\x80\x94 %s", tr(STR_KH_KEYS), screen);  // "Keys — <screen>"

  rowCount = 0;
  addRow(nullptr, tr(STR_KH_SECTION_SCREEN), true);
  const int screenStart = rowCount;
  if (table) {
    for (uint8_t i = 0; i < table->count; i++) addRow(table->entries[i].keys, I18N.get(table->entries[i].what));
  } else if (strcmp(legend.owner, ownerName) == 0) {
    HelpRow legendRows[6];
    const int n = legendHelpRows(legend, tr(STR_KH_MOVE), legendRows, 6);
    for (int i = 0; i < n; i++) addRow(legendRows[i].keys, legendRows[i].what);
  }
  if (rowCount == screenStart) addRow(nullptr, tr(STR_KH_NO_SCREEN_KEYS));

  addRow(nullptr, tr(STR_KH_SECTION_GLOBAL), true);
  // Text fields type a literal '?', so help there is Alt+?.
  addRow(strcmp(ownerName, "KeyboardEntry") == 0 ? "Alt+?" : "?", tr(STR_KH_THIS_HELP));
  addRow("Esc", tr(STR_KH_CLOSE_BACK));
  addRow(tr(STR_KH_KEY_POWER), tr(STR_KH_SLEEP_WAKE));
  firstHiddenRow = rowCount;
}

bool KeyHelpActivity::scroll(const int direction) {
  const int hidden = firstHiddenRow.load();
  int next = topRow;
  if (direction > 0) {
    if (hidden >= rowCount) return false;
    next = hidden;
  } else {
    if (topRow == 0) return false;
    next = std::max(0, topRow - std::max(1, hidden - topRow));
  }
  {
    RenderLock lock;
    topRow = next;
  }
  requestUpdate();
  return true;
}

bool KeyHelpActivity::wantsRawKeys() const { return halKeyboard.present(); }

void KeyHelpActivity::onKey(const freeink::KeyEvent& event) {
  const bool scrollable = topRow > 0 || firstHiddenRow.load() < rowCount;
  const bool plain = event.special == freeink::SpecialKey::None && event.mods == 0;
  if (scrollable && plain && (event.ch == 'j' || event.ch == 'k')) {
    scroll(event.ch == 'j' ? 1 : -1);
    return;
  }
  finish();
}

// Button / touch boards: Back or Confirm close, Up / Down page.
void KeyHelpActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    scroll(1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    scroll(-1);
  }
}

void KeyHelpActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, title, nullptr, false);

  const int x = metrics.contentSidePadding;
  const int contentW = screenW - 2 * metrics.contentSidePadding;
  const int lh = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLh = renderer.getLineHeight(SMALL_FONT_ID);
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int footerY = screenH - smallLh - 3;
  const int bottom = footerY - ROW_GAP;

  int keyW = 0;
  for (int i = 0; i < rowCount; i++) {
    if (rows[i].heading) continue;
    keyW = std::max(keyW, renderer.getTextWidth(UI_10_FONT_ID, rows[i].keys, EpdFontFamily::BOLD));
  }
  keyW = std::min(keyW, contentW * 2 / 5);
  const int whatX = x + keyW + COLUMN_GAP;
  const int whatW = contentW - keyW - COLUMN_GAP;

  int y = top;
  int i = topRow;
  for (; i < rowCount; i++) {
    const Row& row = rows[i];
    if (row.heading) {
      // Keep a heading with at least one row under it.
      if (y + smallLh + 3 + lh > bottom) break;
      renderer.drawText(SMALL_FONT_ID, x, y, row.what, true, EpdFontFamily::BOLD);
      renderer.drawLine(x, y + smallLh + 1, x + contentW, y + smallLh + 1);
      y += smallLh + 3 + ROW_GAP;
      continue;
    }
    const auto keyLines = renderer.wrappedText(UI_10_FONT_ID, row.keys, keyW, KEY_MAX_LINES, EpdFontFamily::BOLD);
    const auto whatLines = renderer.wrappedText(UI_10_FONT_ID, row.what, whatW, WHAT_MAX_LINES);
    const int lines = static_cast<int>(std::max<size_t>(1, std::max(keyLines.size(), whatLines.size())));
    if (y + lines * lh > bottom && i > topRow) break;
    for (size_t l = 0; l < keyLines.size(); l++) {
      renderer.drawText(UI_10_FONT_ID, x, y + static_cast<int>(l) * lh, keyLines[l].c_str(), true,
                        EpdFontFamily::BOLD);
    }
    for (size_t l = 0; l < whatLines.size(); l++) {
      renderer.drawText(UI_10_FONT_ID, whatX, y + static_cast<int>(l) * lh, whatLines[l].c_str());
    }
    y += lines * lh + ROW_GAP;
  }
  firstHiddenRow = i;

  // Footer: scroll hints when the list overflows, then how to close.
  char footer[96];
  const bool more = topRow > 0 || i < rowCount;
  if (more) {
    snprintf(footer, sizeof(footer), "%s%s%s   %s", i < rowCount ? tr(STR_KH_MORE_BELOW) : "",
             (i < rowCount && topRow > 0) ? "   " : "", topRow > 0 ? tr(STR_KH_MORE_ABOVE) : "",
             tr(STR_KH_ANY_KEY_CLOSES));
    if (renderer.getTextWidth(SMALL_FONT_ID, footer) > contentW) {
      snprintf(footer, sizeof(footer), "%s%s%s", i < rowCount ? tr(STR_KH_MORE_BELOW) : "",
               (i < rowCount && topRow > 0) ? "   " : "", topRow > 0 ? tr(STR_KH_MORE_ABOVE) : "");
    }
  } else {
    snprintf(footer, sizeof(footer), "%s", tr(STR_KH_ANY_KEY_CLOSES));
  }
  renderer.drawCenteredText(SMALL_FONT_ID, footerY, footer);

  renderer.displayBuffer();
}

void openKeyHelp(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* ownerName,
                 const KeyHelp* table) {
  auto help = makeUniqueNoThrow<KeyHelpActivity>(renderer, mappedInput, ownerName, table);
  if (!help) {
    LOG_ERR("KEYHELP", "OOM: key help activity");
    return;
  }
  activityManager.pushActivity(std::move(help));
}

}  // namespace deckpoint
