// DECKPOINT: the reader's `:` command line -- the band drawn over the status
// bar, its key handling, and the command registry with its handlers. The
// editing state machine (CommandLine) and the parser / matcher (Commands) are
// pure and host-tested; this file is the reader glue.

#include "ReaderCommands.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "activities/reader/DictionaryDefinitionActivity.h"
#include "activities/reader/EpubReaderActivity.h"
#include "activities/reader/EpubReaderBookmarksActivity.h"
#include "activities/reader/ReaderUtils.h"
#include "deckpoint/KeyHelp.h"
#include "deckpoint/KeyHelpActivity.h"
#include "deckpoint/SleepRequest.h"
#include "deckpoint/sync/AnnotationSync.h"
#include "fontIds.h"
#include "util/Dictionary.h"

using deckpoint::CommandLine;
using deckpoint::CommandResult;
using deckpoint::NumberTarget;
using deckpoint::ParsedCommand;
using deckpoint::ParseKind;

namespace {

constexpr int BAND_PAD = 3;
constexpr int BAND_BORDER = 2;

// Same test as EpubReaderActivity.cpp: these panels re-render to restore the
// grayscale-AA planes, so a stored page snapshot would never be used.
bool xteinkClassPanel() { return gpio.isXteinkDevice() || BoardConfig::isX4Pro() || BoardConfig::isX4Classic(); }

void indexBuildYield(void*) { vTaskDelay(1); }

CommandResult message(char* out, const size_t outSize, const char* text) {
  snprintf(out, outSize, "%s", text);
  return CommandResult::Message;
}

}  // namespace

namespace deckpoint::reader {

struct ReaderCommandHandlers {
  static EpubReaderActivity* reader(void* ctx) { return static_cast<EpubReaderActivity*>(ctx); }

  static CommandResult toc(void* ctx, const char*, char*, size_t) {
    auto* r = reader(ctx);
    r->chapterSelectOrigin = r->capturePosition();
    r->openChapterSelect(true);
    return CommandResult::Left;
  }

  static CommandResult bookmarks(void* ctx, const char*, char* msg, const size_t msgSize) {
    auto* r = reader(ctx);
    if (r->cachedBookmarks.empty()) return message(msg, msgSize, tr(STR_NO_BOOKMARKS));
    r->openBookmarksList(true);
    return CommandResult::Left;
  }

  static CommandResult notes(void* ctx, const char*, char* msg, const size_t msgSize) {
    auto* r = reader(ctx);
    if (!r->annotationStore) return message(msg, msgSize, tr(STR_SEL_UNAVAILABLE));
    const auto& list = r->annotationStore->annotations();
    bool any = false;
    for (size_t i = 0; i < list.size() && !any; i++) any = !list[i].deleted && list[i].isHighlight();
    if (!any) return message(msg, msgSize, tr(STR_NOTES_EMPTY));
    if (!r->openNotesList(true)) return message(msg, msgSize, tr(STR_DICT_LOW_MEMORY));
    return CommandResult::Left;
  }

  static CommandResult exportNotes(void* ctx, const char*, char* msg, const size_t msgSize) {
    reader(ctx)->exportNotes(msg, msgSize);
    return CommandResult::Message;
  }

  static CommandResult bookmark(void* ctx, const char*, char*, size_t) {
    auto* r = reader(ctx);
    if (!r->section) return CommandResult::Restore;
    r->addBookmark();
    r->showBookmarkMessage = true;
    r->bookmarkMessageTime = millis();
    r->requestUpdate();
    return CommandResult::Redraw;
  }

  static CommandResult goTo(EpubReaderActivity* r, const NumberTarget target, char* msg, const size_t msgSize) {
    if (target.kind == ParseKind::Percent) {
      r->jumpBackPosition = r->capturePosition();
      r->hasJumpBack = true;
      r->jumpToPercent(target.value);
      return CommandResult::Redraw;
    }
    if (target.kind == ParseKind::Page) {
      r->goToChapterPage(target.value);
      return CommandResult::Redraw;
    }
    snprintf(msg, msgSize, "%s: goto 0-100 | p1-%u", tr(STR_CMD_USAGE), static_cast<unsigned>(MAX_PAGE));
    return CommandResult::Message;
  }

  static CommandResult gotoCmd(void* ctx, const char* args, char* msg, const size_t msgSize) {
    return goTo(reader(ctx), parseNumberTarget(args), msg, msgSize);
  }

  static CommandResult sync(void* ctx, const char*, char* msg, const size_t msgSize) {
    if (!reader(ctx)->launchKOReaderSync()) return message(msg, msgSize, tr(STR_KOREADER_SETUP_HINT));
    return CommandResult::Left;
  }

  static CommandResult dict(void* ctx, const char* args, char* msg, const size_t msgSize) {
    if (SETTINGS.dictionaryName[0] == '\0') return message(msg, msgSize, tr(STR_DICT_NO_DICT_SET));
    if (args[0] == '\0') {
      snprintf(msg, msgSize, "%s: dict <word>", tr(STR_CMD_USAGE));
      return CommandResult::Message;
    }
    return reader(ctx)->lookUpWord(args, msg, msgSize, false);
  }

  static CommandResult night(void* ctx, const char*, char*, size_t) {
    auto* r = reader(ctx);
    SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
    SETTINGS.saveToFile();
    r->requestUpdate();
    return CommandResult::Redraw;
  }

  static CommandResult clean(void* ctx, const char*, char*, size_t) {
    auto* r = reader(ctx);
    {
      // Same deep clean as the serial CMD:CLEAN (white, full waveform), then
      // the page comes back through the reader's forced (HALF) refresh.
      RenderLock lock;
      r->settleOverlayRefresh();
      r->renderer.clearScreen();
      r->renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    }
    r->handleForcedRefresh();
    return CommandResult::Redraw;
  }

  static CommandResult font(void* ctx, const char* args, char* msg, const size_t msgSize) {
    auto* r = reader(ctx);
    static constexpr StrId BUILTIN_NAMES[] = {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS};
    static_assert(std::size(BUILTIN_NAMES) == CrossPointSettings::BUILTIN_FONT_COUNT, "built-in font names");
    const auto& families = sdFontSystem.registry().getFamilies();
    const int builtinCount = static_cast<int>(std::size(BUILTIN_NAMES));
    const auto nameAt = [&](const int i) -> const char* {
      return i < builtinCount ? I18N.get(BUILTIN_NAMES[i]) : families[i - builtinCount].name.c_str();
    };
    const int total = builtinCount + static_cast<int>(families.size());

    if (args[0] == '\0') {
      const char* current = SETTINGS.sdFontFamilyName[0] != '\0'
                                ? SETTINGS.sdFontFamilyName
                                : nameAt(SETTINGS.fontFamily % CrossPointSettings::BUILTIN_FONT_COUNT);
      snprintf(msg, msgSize, "%s: %s", tr(STR_FONT), current);
      return CommandResult::Message;
    }

    NamePicker picker(args);
    for (int i = 0; i < total; i++) picker.add(i, nameAt(i));
    if (picker.count() == 0) {
      snprintf(msg, msgSize, "%s: %s", tr(STR_CMD_NO_FONT), args);
      return CommandResult::Message;
    }
    if (picker.count() > 1) {
      // "Which one? Noto Sans, NotoSansCJK, ..." (cut off when it runs out of room)
      size_t len = static_cast<size_t>(snprintf(msg, msgSize, "%s ", tr(STR_CMD_WHICH)));
      bool first = true;
      for (int i = 0; i < total && len + 1 < msgSize; i++) {
        if (matchName(args, nameAt(i)) != picker.quality()) continue;
        const int n = snprintf(msg + len, msgSize - len, "%s%s", first ? "" : ", ", nameAt(i));
        if (n < 0) break;
        len = std::min(msgSize - 1, len + static_cast<size_t>(n));
        first = false;
      }
      return CommandResult::Message;
    }

    const int pick = picker.index();
    const bool sdPick = pick >= builtinCount;
    if (sdPick ? strcmp(SETTINGS.sdFontFamilyName, nameAt(pick)) == 0
               : (SETTINGS.sdFontFamilyName[0] == '\0' && SETTINGS.fontFamily == pick)) {
      return CommandResult::Restore;  // already the font in use
    }
    {
      // Same switch as TextSettingsActivity::applyFamily: the render task may be
      // walking the resident SD font that ensureLoaded() frees.
      RenderLock lock;
      if (sdPick) {
        strncpy(SETTINGS.sdFontFamilyName, nameAt(pick), sizeof(SETTINGS.sdFontFamilyName) - 1);
        SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      } else {
        SETTINGS.fontFamily = static_cast<uint8_t>(pick);
        SETTINGS.sdFontFamilyName[0] = '\0';
      }
      sdFontSystem.ensureLoaded(r->renderer);
    }
    if (sdPick && SETTINGS.sdFontFamilyName[0] == '\0') {
      LOG_ERR("CMD", "Font family failed to load: %s", nameAt(pick));
    }
    // Persist + re-paginate, as the Text panel does after its font picker.
    r->applyReaderTextSettings();
    r->requestUpdate();
    return CommandResult::Redraw;
  }

  static CommandResult home(void* ctx, const char*, char*, size_t) {
    reader(ctx)->onGoHome();
    return CommandResult::Left;
  }

  static CommandResult sleep(void*, const char*, char*, size_t) {
    requestSleep();
    return CommandResult::Left;
  }

  // `:search text` / `:s text`: the line turns into the `/` prompt and searches
  // (no text: the last search again).
  static CommandResult search(void* ctx, const char* args, char* msg, const size_t msgSize) {
    auto* r = reader(ctx);
    r->setCommandLineMode(true);
    r->cmdLine.setText(args);
    r->cmdLine.remember();
    {
      RenderLock lock;
      r->cmdLineShown = true;
    }
    if (!r->startSearch(args, true, true, msg, msgSize)) return CommandResult::Message;
    return CommandResult::Kept;
  }

  static CommandResult help(void* ctx, const char*, char*, size_t) {
    auto* r = reader(ctx);
    openKeyHelp(r->renderer, r->mappedInput, r->name.c_str(), r->keyHelp());
    return CommandResult::Left;
  }
};

namespace {
using H = ReaderCommandHandlers;
}  // namespace

constexpr CommandSpec READER_COMMANDS[] = {
    {"toc", "t", nullptr, StrId::STR_KH_CONTENTS, &H::toc},
    {"bookmarks", "bm", nullptr, StrId::STR_CMD_HELP_BOOKMARKS, &H::bookmarks},
    {"bookmark", "mark", nullptr, StrId::STR_KH_TOGGLE_BOOKMARK, &H::bookmark},
    {"notes", "n", nullptr, StrId::STR_CMD_HELP_NOTES, &H::notes},
    {"export", nullptr, nullptr, StrId::STR_CMD_HELP_EXPORT, &H::exportNotes},
    {"goto", "g", "N | pN", StrId::STR_CMD_HELP_GOTO, &H::gotoCmd},
    {"sync", nullptr, nullptr, StrId::STR_CMD_HELP_SYNC, &H::sync},
    {"dict", nullptr, "word", StrId::STR_CMD_HELP_DICT, &H::dict},
    {"search", "s", "text", StrId::STR_KH_SEARCH_BOOK, &H::search},
    {"night", nullptr, nullptr, StrId::STR_CMD_HELP_NIGHT, &H::night},
    {"clean", nullptr, nullptr, StrId::STR_CMD_HELP_CLEAN, &H::clean},
    {"font", nullptr, "name", StrId::STR_CMD_HELP_FONT, &H::font},
    {"home", nullptr, nullptr, StrId::STR_KH_GO_HOME, &H::home},
    {"sleep", nullptr, nullptr, StrId::STR_CMD_HELP_SLEEP, &H::sleep},
    {"help", nullptr, nullptr, StrId::STR_KH_THIS_HELP, &H::help},
};
constexpr size_t READER_COMMAND_COUNT = std::size(READER_COMMANDS);

namespace {

// Rows after the commands: the shortcuts and editing keys of the line itself.
// Only keys on the T-Deck Pro keymap (Tab / arrows exist on BLE keyboards only).
struct FixedRow {
  const char* keys;
  StrId what;
};
constexpr FixedRow LINE_ROWS[] = {
    {":N", StrId::STR_KH_CMD_NUMBER},           {":pN  :#N", StrId::STR_KH_CMD_PAGE},
    {"Alt+Space", StrId::STR_KH_CMD_COMPLETE},  {"Alt+k", StrId::STR_KH_CMD_RECALL},
    {"Alt+Backspace", StrId::STR_KH_CLEAR_ALL}, {DECKPOINT_KEY_MIC, StrId::STR_KH_CMD_CANCEL},
};

StrId commandHelpRow(const uint8_t index, char* keys, const size_t keysSize) {
  if (index < READER_COMMAND_COUNT) {
    const CommandSpec& c = READER_COMMANDS[index];
    snprintf(keys, keysSize, ":%s%s%s%s%s", c.name, c.alias ? " :" : "", c.alias ? c.alias : "", c.argHint ? " " : "",
             c.argHint ? c.argHint : "");
    if (c.help == StrId::STR_CMD_HELP_SYNC && annotationsync::ready()) return StrId::STR_CMD_HELP_SYNC_HL;
    return c.help;
  }
  const size_t fixed = index - READER_COMMAND_COUNT;
  if (fixed < std::size(LINE_ROWS)) {
    snprintf(keys, keysSize, "%s", LINE_ROWS[fixed].keys);
    return LINE_ROWS[fixed].what;
  }
  keys[0] = '\0';
  return StrId::STR_KH_COMMAND_LINE;
}

}  // namespace

constexpr KeyHelpExtra READER_COMMAND_HELP = {
    StrId::STR_KH_SECTION_COMMANDS, static_cast<uint8_t>(READER_COMMAND_COUNT + std::size(LINE_ROWS)), &commandHelpRow};

}  // namespace deckpoint::reader

using deckpoint::reader::READER_COMMAND_COUNT;
using deckpoint::reader::READER_COMMANDS;
using deckpoint::reader::ReaderCommandHandlers;

// ---------------------------------------------------------------------------
// Command line band
// ---------------------------------------------------------------------------

void EpubReaderActivity::setCommandLineMode(const bool searchMode) {
  if (searchMode == cmdSearchMode) return;
  char other[CommandLine::MAX_LEN + 1];
  snprintf(other, sizeof(other), "%s", cmdOtherHistory);
  snprintf(cmdOtherHistory, sizeof(cmdOtherHistory), "%s", cmdLine.lastCommand());
  cmdLine.setHistory(other);
  cmdSearchMode = searchMode;
}

void EpubReaderActivity::openCommandLine(const char* prefill, const bool pageDirty, const bool searchMode,
                                         const char* message) {
  readerKeys.reset();
  pendingManualTurn = 0;
  keyPopupShown = false;
  setCommandLineMode(searchMode);
  cmdLine.open(prefill);
  snprintf(cmdMessage, sizeof(cmdMessage), "%s", message ? message : "");
  cmdMessageTime = 0;
  if (pageDirty || !section) {
    // A popup is still painted on the page (or there is no page yet): let the
    // render task draw a clean page; commandLineAfterRender() adds the band.
    {
      RenderLock lock;
      cmdLineShown = true;
    }
    requestUpdate();
    return;
  }
  RenderLock lock;
  settleOverlayRefresh();
  cmdLineShown = true;
  // One framebuffer copy (9.6 KB on the T-Deck Pro), held only while the line
  // is open, so closing it is a FAST refresh instead of a page re-render.
  if (!xteinkClassPanel() && renderer.hasFrameBuffer()) cmdPageStored = renderer.storeBwBuffer();
  if (!renderer.hasFrameBuffer()) return;
  drawCommandLine();
  pushOverlayRefresh();
}

void EpubReaderActivity::drawCommandLine() const {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  int marginTop, marginRight, marginBottom, marginLeft;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int bandH = lineH + 2 * BAND_PAD + BAND_BORDER;
  const int top = screenH - marginBottom - bandH;
  renderer.fillRect(0, top, screenW, screenH - top, false);
  renderer.fillRect(0, top, screenW, BAND_BORDER, true);

  const int x = marginLeft + BAND_PAD;
  const int y = top + BAND_BORDER + BAND_PAD;
  const int maxW = screenW - marginLeft - marginRight - 2 * BAND_PAD;

  if (cmdMessage[0] != '\0') {
    const std::string shown = renderer.truncatedText(UI_10_FONT_ID, cmdMessage, maxW, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x, y, shown.c_str(), true, EpdFontFamily::BOLD);
    return;
  }

  char line[CommandLine::MAX_LEN + 2];
  snprintf(line, sizeof(line), "%c%s", cmdSearchMode ? '/' : ':', cmdLine.text());
  const int cursorW = std::max(4, lineH / 3);
  // Long lines scroll left so the end (and the cursor) stays visible.
  const char* shown = line;
  int textW = renderer.getTextWidth(UI_10_FONT_ID, shown);
  while (textW + 1 + cursorW > maxW && shown[1] != '\0') {
    do {
      ++shown;
    } while ((static_cast<uint8_t>(*shown) & 0xC0) == 0x80);
    textW = renderer.getTextWidth(UI_10_FONT_ID, shown);
  }
  renderer.drawText(UI_10_FONT_ID, x, y, shown, true);
  renderer.fillRect(x + textW + 1, y, cursorW, lineH, true);
}

void EpubReaderActivity::paintCommandLine() {
  RenderLock lock;
  settleOverlayRefresh();
  if (!renderer.hasFrameBuffer()) return;
  drawCommandLine();
  pushOverlayRefresh();
}

void EpubReaderActivity::showCommandMessage(const char* text) {
  snprintf(cmdMessage, sizeof(cmdMessage), "%s", text ? text : "");
  cmdMessageTime = std::max(1UL, millis());
  paintCommandLine();
}

void EpubReaderActivity::closeCommandLine(const CommandResult how) {
  cmdLine.close();
  cmdMessage[0] = '\0';
  cmdMessageTime = 0;
  bool rerender = false;
  {
    RenderLock lock;
    cmdLineShown = false;
    if (how == CommandResult::Restore && cmdPageStored && !pageHasGray) {
      settleOverlayRefresh();
      // No baseline resync: the glass shows the band, and erasing it needs the
      // differential to keep diffing against the last pushed frame.
      renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
      cmdPageStored = false;
      pushOverlayRefresh();
    } else {
      if (cmdPageStored) renderer.discardStoredBwBuffer();
      cmdPageStored = false;
      rerender = how == CommandResult::Restore;
    }
  }
  if (rerender) requestUpdate();
}

void EpubReaderActivity::completeCommandLine() {
  char completed[CommandLine::MAX_LEN + 1];
  char candidates[80];
  const size_t matches = deckpoint::completeCommand(cmdLine.text(), READER_COMMANDS, READER_COMMAND_COUNT, completed,
                                                    sizeof(completed), candidates, sizeof(candidates));
  if (completed[0] != '\0') cmdLine.setText(completed);
  if (matches > 1) {
    showCommandMessage(candidates);
  } else if (completed[0] != '\0') {
    paintCommandLine();
  }
}

void EpubReaderActivity::submitCommandLine() {
  cmdLine.remember();
  if (cmdSearchMode) {
    char msg[96];
    msg[0] = '\0';
    if (!startSearch(cmdLine.text(), true, true, msg, sizeof(msg))) showCommandMessage(msg);
    return;
  }
  const ParsedCommand parsed = deckpoint::parseCommandLine(cmdLine.text(), READER_COMMANDS, READER_COMMAND_COUNT);
  char msg[96];
  msg[0] = '\0';
  {
    // Hidden while the command runs: a page render it triggers must not put the band back.
    RenderLock lock;
    cmdLineShown = false;
  }

  CommandResult result = CommandResult::Restore;
  switch (parsed.kind) {
    case ParseKind::Empty:
      break;
    case ParseKind::Percent:
    case ParseKind::Page:
    case ParseKind::BadNumber: {
      NumberTarget target;
      target.kind = parsed.kind;
      target.value = parsed.value;
      result = ReaderCommandHandlers::goTo(this, target, msg, sizeof(msg));
      break;
    }
    case ParseKind::Command:
      LOG_DBG("CMD", "Run :%s %s", READER_COMMANDS[parsed.index].name, parsed.args());
      result = READER_COMMANDS[parsed.index].handler(this, parsed.args(), msg, sizeof(msg));
      break;
    case ParseKind::Ambiguous: {
      char unused[CommandLine::MAX_LEN + 1];
      char candidates[64];
      deckpoint::completeCommand(parsed.name(), READER_COMMANDS, READER_COMMAND_COUNT, unused, sizeof(unused),
                                 candidates, sizeof(candidates));
      snprintf(msg, sizeof(msg), "%s %s", tr(STR_CMD_WHICH), candidates);
      result = CommandResult::Message;
      break;
    }
    case ParseKind::Unknown:
    default:
      snprintf(msg, sizeof(msg), "%s: %s", tr(STR_CMD_UNKNOWN), parsed.name());
      result = CommandResult::Message;
      break;
  }

  if (result == CommandResult::Kept) return;
  if (result == CommandResult::Message) {
    {
      RenderLock lock;
      cmdLineShown = true;
    }
    showCommandMessage(msg);
    return;
  }
  closeCommandLine(result);
  if (result == CommandResult::Left) keysSuspended = true;
}

void EpubReaderActivity::commandLineKey(const freeink::KeyEvent& event) {
  // A shown message goes away with the next key, which then edits as usual.
  const bool hadMessage = cmdMessage[0] != '\0';
  cmdMessage[0] = '\0';
  cmdMessageTime = 0;
  switch (cmdLine.feed(event)) {
    case CommandLine::Action::Edited:
      paintCommandLine();
      return;
    case CommandLine::Action::Submit:
      submitCommandLine();
      return;
    case CommandLine::Action::Cancel:
      closeCommandLine(CommandResult::Restore);
      return;
    case CommandLine::Action::Complete:
      if (cmdSearchMode) {
        if (hadMessage) paintCommandLine();
        return;
      }
      completeCommandLine();
      if (hadMessage && cmdMessage[0] == '\0') paintCommandLine();
      return;
    case CommandLine::Action::None:
    default:
      if (hadMessage) paintCommandLine();
      return;
  }
}

bool EpubReaderActivity::commandLineTick() {
  if (!cmdLine.isOpen()) return false;
  if (!wantsRawKeys()) {
    // Something else took over the screen (end of book, an overlay, the
    // keyboard went away): drop the line without painting over it.
    closeCommandLine(CommandResult::Left);
    return false;
  }
  if (cmdMessageTime != 0 && millis() - cmdMessageTime >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    cmdMessage[0] = '\0';
    cmdMessageTime = 0;
    paintCommandLine();
  }
  // Hold the auto-turn interval and swallow buttons / taps while typing.
  lastPageTurnTime = millis();
  return true;
}

void EpubReaderActivity::commandLineBeforeRender() {
  if (!cmdPageStored) return;
  renderer.discardStoredBwBuffer();
  cmdPageStored = false;
}

void EpubReaderActivity::commandLineAfterRender() {
  if (!cmdLineShown || !renderer.hasFrameBuffer()) return;
  if (!xteinkClassPanel()) cmdPageStored = renderer.storeBwBuffer();
  drawCommandLine();
  pushOverlayRefresh();
}

// ---------------------------------------------------------------------------
// Actions shared with the reader keys
// ---------------------------------------------------------------------------

void EpubReaderActivity::goToChapterPage(const int page) {
  if (!epub || page < 1) return;
  jumpBackPosition = capturePosition();
  hasJumpBack = true;
  const int index = page - 1;
  {
    RenderLock lock;
    clearDeferredReposition();
    pendingManualTurn = 0;
    pendingAnchor.clear();
    pendingOffsetJump.reset();
    pendingPercentJump = false;
    if (section && index < static_cast<int>(section->pageCount)) {
      section->currentPage = index;
    } else {
      // Not laid out that far yet: rebuild the chapter up to the page (the
      // render clamps to the chapter's last page).
      nextPageNumber = 0;
      pendingPageJump = static_cast<uint16_t>(index);
      section.reset();
    }
  }
  requestUpdate();
}

void EpubReaderActivity::openBookmarksList(const bool fromKeys) {
  const auto origin = capturePosition();
  auto list = makeUniqueNoThrow<EpubReaderBookmarksActivity>(renderer, mappedInput, epub, epub->getPath());
  if (!list) {
    LOG_ERR("CMD", "OOM: EpubReaderBookmarksActivity");
    return;
  }
  startActivityForResult(std::move(list), [this, fromKeys, origin](const ActivityResult& result) {
    if (fromKeys && !result.isCancelled) {
      jumpBackPosition = origin;
      hasJumpBack = true;
    }
    applyProgressChangeResult(result, !fromKeys);
  });
}

CommandResult EpubReaderActivity::lookUpWord(const char* word, char* msg, const size_t msgSize, const bool fromHints) {
  const auto progress = [this, fromHints](const char* text) {
    if (fromHints) {
      showKeyPopup(text, false);
    } else {
      showCommandMessage(text);
    }
  };
  if (SETTINGS.dictionaryName[0] == '\0') return message(msg, msgSize, tr(STR_DICT_NO_DICT_SET));
  progress(tr(STR_DICT_LOOKING_UP));
  // ~0.4 KB of lookup state, only for the lookup; freed before the definition screen.
  auto dictionary = makeUniqueNoThrow<Dictionary>();
  if (!dictionary) {
    LOG_ERR("CMD", "OOM: Dictionary");
    return message(msg, msgSize, tr(STR_DICT_LOW_MEMORY));
  }
  if (!dictionary->open(SETTINGS.dictionaryName)) return message(msg, msgSize, tr(STR_DICT_ERROR));
  if (dictionary->needsIndex()) {
    progress(tr(STR_DICT_INDEXING));
    Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
    if (!dictionary->buildIndex(&indexBuildYield, nullptr, &indexResult)) {
      return message(
          msg, msgSize,
          indexResult == Dictionary::IndexResult::LowMemory ? tr(STR_DICT_LOW_MEMORY) : tr(STR_DICT_READ_FAILED));
    }
  }
  std::string definition;
  std::string headword;
  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
  if (!dictionary->lookup(word, definition, headword, &result)) {
    switch (result) {
      case Dictionary::LookupResult::Decompress:
        return message(msg, msgSize, tr(STR_DICT_DECOMPRESS_ERROR));
      case Dictionary::LookupResult::LowMemory:
        return message(msg, msgSize, tr(STR_DICT_LOW_MEMORY));
      case Dictionary::LookupResult::ReadError:
        return message(msg, msgSize, tr(STR_DICT_READ_FAILED));
      default:
        snprintf(msg, msgSize, "%s: %s", tr(STR_DICT_NOT_FOUND), word);
        return CommandResult::Message;
    }
  }
  const bool html = dictionary->definitionsAreHtml();
  dictionary.reset();
  auto screen = makeUniqueNoThrow<DictionaryDefinitionActivity>(renderer, mappedInput, std::move(headword),
                                                                std::move(definition), html);
  if (!screen) {
    LOG_ERR("CMD", "OOM: DictionaryDefinitionActivity");
    return message(msg, msgSize, tr(STR_DICT_LOW_MEMORY));
  }
  startActivityForResult(std::move(screen), [this](const ActivityResult&) { requestUpdate(); });
  return CommandResult::Left;
}
