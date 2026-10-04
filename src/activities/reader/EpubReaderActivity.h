#pragma once

#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/PageLink.h>
#include <Epub/Section.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "BookmarkEntry.h"
#include "ChapterPosition.h"
#include "EpubReaderMenuActivity.h"
#include "ProgressMapper.h"
#include "ReaderActivity.h"
#include "ReaderToolbarUi.h"
#include "components/OptionPopup.h"
#include "deckpoint/CommandLine.h"                  // DECKPOINT
#include "deckpoint/KeyHelp.h"                      // DECKPOINT
#include "deckpoint/annotations/AnnotationStore.h"  // DECKPOINT
#include "deckpoint/reader/BookSearch.h"            // DECKPOINT
#include "deckpoint/reader/HintSession.h"           // DECKPOINT
#include "deckpoint/reader/Marks.h"                 // DECKPOINT
#include "deckpoint/reader/ReaderCommands.h"        // DECKPOINT
#include "deckpoint/reader/ReaderKeys.h"            // DECKPOINT

class EpubReaderActivity final : public ReaderActivity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  std::string pendingAnchor;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  std::optional<uint32_t> cachedVisibleTextOffset;
  std::optional<uint32_t> currentPageVisibleOffset;
  std::optional<uint32_t> pendingOffsetJump;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  int8_t pendingManualTurn = 0;
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0.0f;
  bool pendingScreenshot = false;
  bool pendingSyncSaveError = false;
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;
  bool skipNextButtonCheck = false;
  bool automaticPageTurnActive = false;
  bool showBookmarkMessage = false;
  bool showDictionaryMessage = false;
  unsigned long dictionaryMessageTime = 0UL;
  bool currentPageBookmarked = false;
  int idlePrewarmSpine = -1;
  int idlePrewarmPage = -1;
  unsigned long lastRenderCompleteMs = 0;
  bool bookmarkRemoved = false;
  std::vector<BookmarkEntry> cachedBookmarks;
  bool recentsEntryRemoved = false;
  unsigned long bookmarkMessageTime = 0UL;
  bool pendingReadFolderMove = false;

  // Toolbar reader menu (SETTINGS.readerMenuStyle == READER_MENU_TOOLBAR): drawn
  // over the page instead of pushing the full-screen list menu. Select opens the
  // Toolbar; its tools open the Contents/Text/More bottom-sheet panels.
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Overlay overlay = Overlay::None;
  int focusedTool = 0;  // toolbar tool focus: 0=Contents, 1=Text, 2=More
  int panelIndex = 0;   // selected row within the active panel
  // Panel list navigation: a tap steps one row, a hold jumps PANEL_HOLD_STEP rows in one go
  // (a contents list runs to hundreds of chapters). One jump per hold, not a repeat -- every
  // step repaints the panel, so repeating is bounded by the e-ink refresh anyway and reads as
  // sluggish. True once a hold has jumped, so the release that ends it is swallowed.
  static constexpr unsigned long PANEL_HOLD_MS = 1500;
  static constexpr int PANEL_HOLD_STEP = 10;
  bool panelHoldJumped = false;
  // Whether the panel draws its cursor row. Button boards always do; touch
  // boards only once a button has moved it, so a tapped row is not left inverted.
  bool panelCursorShown = false;
  // FreeInkUI chrome + tap targets for the overlay; created when it opens,
  // released when it closes.
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  // Modal option picker over the panel (same component the Settings screens
  // use), for enum rows: font size / line spacing / alignment / orientation /
  // auto page turn. Toggle rows stay one-tap toggles, as in Settings.
  OptionPopup overlayPopup;
  // True while a clean-page snapshot (renderer.storeBwBuffer) backs the open
  // overlay, letting panel->toolbar steps restore the page without a full
  // re-render. Discarded on close / whenever the page under the overlay changes.
  bool overlayPageStored = false;
  // True while a deferred overlay chrome refresh (pushOverlayRefresh) may still
  // be running on the panel. settleOverlayRefresh() must run before the
  // framebuffer is touched or another differential refresh is pushed.
  bool overlayRefreshPending = false;
  void pushOverlayRefresh();
  void settleOverlayRefresh();
  int autoTurnOption = 0;  // current auto page-turn rate index (More panel)
  std::vector<EpubReaderMenuActivity::MenuItem> moreItems;

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  std::vector<PageLink> currentPageLinks;
  int currentPageLinkMarginLeft = 0;
  int currentPageLinkMarginTop = 0;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;
  // The back-stack outlives the reader (sleep, home) in links.bin so Back
  // still returns to where a followed link was tapped.
  void saveLinkStack() const;
  void loadLinkStack();

  uint16_t buildViewportWidth = 0;
  uint16_t buildViewportHeight = 0;
  bool partialRebuildStartFailed = false;

  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;

  static constexpr int BUILD_PAGES_PER_CHUNK = 8;
  static constexpr int BACKGROUND_BUILD_PAGES_PER_TICK = 2;
  static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP = 32 * 1024;
  static constexpr size_t BACKGROUND_BUILD_MIN_MAX_ALLOC = 16 * 1024;
  // Requires the render lock; heap admission is checked separately by the build tick.
  bool backgroundBuildWanted() const;
  bool buildTickHeapGate();
  bool buildHeapPaused = false;
  static constexpr size_t RENDER_MIN_FREE_HEAP = 24 * 1024;
  static constexpr int BUILD_WINDOW_AHEAD = 5;
  static constexpr int PARTIAL_REBUILD_START_MARGIN = 15;
  static constexpr int BUILD_POPUP_PAGE_THRESHOLD = 20;
  static constexpr size_t BUILD_POPUP_BYTE_THRESHOLD = 96 * 1024;
  static constexpr unsigned long BUILD_POPUP_DEADLINE_MS = 1000;
  bool buildPopupPending = false;
  void showBuildPopup(GfxRenderer& renderer, int& pagesUntilFullRefresh);
  bool applyDeferredReposition();
  void clearDeferredReposition();
  void rememberCurrentContentOffset();
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  // Live section position, or the values cached before a child screen
  // released the section.
  ChapterPosition chapterPosition() const;
  int bookPercentFor(const ChapterPosition& position) const;
  void openReaderMenu();
  // Toolbar reader menu (see Overlay above).
  bool usesToolbarMenu() const;
  void openOverlay(Overlay target);
  void closeOverlayToPage();
  void discardOverlayPage();
  void handleOverlayInput();
  void renderOverlay();
  std::string currentChapterTitle() const;
  // Text panel rows (font, size, line spacing, alignment, focus reading).
  std::string textRowName(int row) const;
  std::string textRowValue(int row) const;
  void showTextRowPopup(int row);
  // Persist + re-paginate + re-render under the open panel (live preview).
  void applyTextSettingLive();
  void paintOverlayPopup();
  // Persist the reader text settings, (re)load the selected SD font, and
  // re-paginate the current chapter so changes apply without re-opening the book.
  void applyReaderTextSettings();
  // More panel rows.
  void buildMoreActions();
  std::string moreRowName(int row) const;
  std::string moreRowValue(int row) const;
  void activateMoreRow(int row);
  void openFootnoteSelect(bool reopenMenuOnCancel);
  void openDictionaryWordSelect();
  bool launchKOReaderSync();
  unsigned long confirmLongPressThreshold() const;
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  void loadCachedBookmarks();
  void addBookmark();
  void updateBookmarkFlag();

  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

  // DECKPOINT: keyboard reader keys (deckpoint/reader/EpubReaderKeys.cpp).
  // Marks are allocated on first m / ' use; the jump-back slot ('') is
  // in-memory only.
  deckpoint::reader::ReaderKeys readerKeys;
  std::unique_ptr<deckpoint::reader::MarkTable> marks;
  deckpoint::reader::MarkPosition jumpBackPosition;
  bool hasJumpBack = false;
  deckpoint::reader::MarkPosition chapterSelectOrigin;  // jump-back candidate while `t` is open
  // A popup (pending keys or a toast) is painted over the page; it is erased by
  // the next page render. keyPopupTime == 0 means untimed (pending keys).
  bool keyPopupShown = false;
  unsigned long keyPopupTime = 0;
  unsigned long keyPopupRenderStamp = 0;
  // Set once a key leaves the reader (menu, contents, help, home) so the rest
  // of the same key batch is dropped; cleared by the next loop().
  bool keysSuspended = false;
  void runReaderCommand(const deckpoint::reader::ReaderCommand& cmd);
  void keyPageTurns(bool forward, int count);
  void keyChapterJump(int delta);
  void keyJumpToSpine(int spineIndex, bool lastPage);
  deckpoint::reader::MarkPosition capturePosition();
  bool jumpToPosition(const deckpoint::reader::MarkPosition& position);
  bool ensureMarksLoaded();
  void showKeyPopup(const char* text, bool timed);
  void showKeyPopupLocked(const char* text, bool timed);  // caller holds the RenderLock
  void keyPopupTick();
  // Contents list; fromKeys: Esc returns to the page (not the reader menu) and
  // a pick records the jump-back position.
  void openChapterSelect(bool fromKeys);
  // Bookmarks list; fromKeys: cancel returns to the page, a pick records the jump-back.
  void openBookmarksList(bool fromKeys);
  void applyProgressChangeResult(const ActivityResult& result, bool reopenMenuOnCancel);

  // DECKPOINT: `:` command line (deckpoint/reader/ReaderCommands.cpp), drawn as
  // a band over the status bar. While it is open it owns every key.
  friend struct deckpoint::reader::ReaderCommandHandlers;
  deckpoint::CommandLine cmdLine;
  // The render task repaints the band over any page render while this is set.
  bool cmdLineShown = false;
  // The renderer holds the clean page from under the band (storeBwBuffer), so
  // closing restores it with one FAST refresh instead of a re-render.
  bool cmdPageStored = false;
  // DECKPOINT: the page on glass went through a gray pass; a B/W snapshot restore would lose it.
  bool pageHasGray = false;
  // Non-empty: the band shows this (error / candidates) instead of the prompt
  // until the next key or the toast timeout.
  char cmdMessage[96] = {};
  unsigned long cmdMessageTime = 0;
  // `/` prompt instead of `:` (deckpoint/reader/EpubReaderSearch.cpp runs it).
  bool cmdSearchMode = false;
  // The other prompt's one-entry history, swapped in when the mode changes.
  char cmdOtherHistory[deckpoint::CommandLine::MAX_LEN + 1] = {};
  // message: shown in the band right away instead of the prompt (untimed).
  void openCommandLine(const char* prefill, bool pageDirty, bool searchMode = false, const char* message = nullptr);
  void setCommandLineMode(bool searchMode);
  void commandLineKey(const freeink::KeyEvent& event);
  void submitCommandLine();
  void completeCommandLine();
  void closeCommandLine(deckpoint::CommandResult how);
  void showCommandMessage(const char* text);
  // Caller must not hold the RenderLock.
  void paintCommandLine();
  // Draws the band into the framebuffer; caller holds the RenderLock.
  void drawCommandLine() const;
  // From loop(): expires messages; true while the line owns input.
  bool commandLineTick();
  // Render-task hooks: before a page render / after it is on the glass.
  void commandLineBeforeRender();
  void commandLineAfterRender();
  // 1-based page of the current chapter (jump-back recorded).
  void goToChapterPage(int page);

  // DECKPOINT: `d` hint-mode dictionary (deckpoint/reader/EpubReaderHints.cpp).
  // `hints` and hintOpenPending are shared with the render task (RenderLock);
  // hintsOpen is the main loop's own view: while set the hints own every key.
  // The same session carries the highlight selection (hintsSelect).
  std::unique_ptr<deckpoint::reader::HintSession> hints;
  bool hintOpenPending = false;  // labels go up after the clean page render
  bool hintsOpen = false;
  bool hintsSelect = false;  // main loop's view: the open session is a selection
  deckpoint::reader::HintPurpose hintPendingPurpose = deckpoint::reader::HintPurpose::Lookup;
  // Message shown over the next page render (a selection that ended with one).
  const char* pendingHintToast = nullptr;
  void openHints(bool pageDirty, deckpoint::reader::HintPurpose purpose = deckpoint::reader::HintPurpose::Lookup);
  // Caller holds the RenderLock for the *Locked / begin / draw / render hooks.
  void beginHints();
  // Current page + its word boxes in a fresh session (null: none / OOM).
  std::unique_ptr<deckpoint::reader::HintSession> loadHintSessionLocked();
  // Copies the clean page out of the framebuffer (false: re-render instead).
  bool storeHintPageLocked();
  void drawHintOverlay();
  void drawHintLabels() const;
  // Puts the overlay back over the stored page after a change; true when the
  // page must be re-rendered for it instead (caller then requestUpdate()s).
  bool repaintHintsLocked();
  // Returns true when the page must be re-rendered (no stored copy to restore).
  bool closeHintsLocked(bool restorePage);
  void closeHints(bool restorePage);
  void hintKey(const freeink::KeyEvent& event);
  // From loop(): true while the hints own input.
  bool hintsTick();
  void hintsBeforeRender();
  void hintsAfterRender();
  // Dictionary lookup shared by `:dict` and the hints (ReaderCommands.cpp):
  // opens the definition (Left) or writes why not into msg (Message).
  // Progress shows in the command band, or as a popup for the hints.
  deckpoint::CommandResult lookUpWord(const char* word, char* msg, size_t msgSize, bool fromHints);
  // Inverts a picked word on the page in the framebuffer (caller holds the
  // RenderLock); lookUpPickedWord() then pushes it with the lookup popup.
  void markPickedWordLocked(const deckpoint::reader::WordBox& box);
  void lookUpPickedWord(const char* word);

  // DECKPOINT: touch alongside the keyboard (deckpoint/reader/EpubReaderTouch.cpp).
  // From loop(): a touch while a keyboard mode is up (`:` / `/` line, running
  // search, hint labels, search mark) cancels that mode and is swallowed.
  bool keyModeTouchTick();
  // From loop(): a long-press claims its contact and opens the word popup
  // (Look up / Highlight / Note) over the word under it, or the highlight
  // popup over a highlighted word (none: ignored); true when it fired.
  bool touchWordMenuTick();
  // From loop(): a tap on a highlighted word opens its popup; true when it did.
  bool touchHighlightTapTick();

  // DECKPOINT: highlight selection (deckpoint/reader/EpubReaderSelection.cpp)
  // on the hint session: `v` labels pick start / end, long-press popups.
  // Same as wantsRawKeys() minus the keyboard: nothing else owns the page.
  bool readerOwnsPage() const;
  void selectionKey(const freeink::KeyEvent& event);
  // A touch while a selection is up (tap / long-press at x,y; swipe: x < 0).
  void selectionTouch(int x, int y);
  // Caller holds the RenderLock for these.
  void drawSelectionOverlay();
  // Inverts words [from, to] on the framebuffer, one block per line.
  void invertWordsLocked(int from, int to);
  void drawSelectionLegend() const;
  void drawSelectionMenu() const;
  deckpoint::reader::MenuLayout selectionMenuLayout() const;
  deckpoint::reader::BarLayout selectionBarLayout() const;
  // Opens the popup over word `word` (a highlight's when it sits in one).
  void openSelectionMenuLocked(int word);
  // Installs a touch-opened selection with the popup over `word` and puts it
  // on the glass. True when a page render must draw it (caller requestUpdate()s).
  bool openTouchSelectionLocked(std::unique_ptr<deckpoint::reader::HintSession> session, int word);
  // Runs the popup's chosen action; sets *rerender / *lookUp (word copied).
  void runSelectionActionLocked(deckpoint::reader::SelectionAction action, bool* rerender, char* lookUp,
                                size_t lookUpSize);
  // Saves the selected range; ends the session. True when the page must render.
  bool saveSelectionLocked();
  // Ends the session; `pageChanged` re-renders the page (else the clean page
  // is restored). `toast` (tr string) is shown afterwards. True: requestUpdate().
  bool endSelectionLocked(bool pageChanged, const char* toast);
  // Set while a page that shows highlights is on the glass (taps check it).
  bool pageHasHighlightMarks = false;

  // DECKPOINT: note sheet over the page (deckpoint/reader/EpubReaderNote.cpp),
  // SelectMode::Note of the selection session. Caller holds the RenderLock;
  // a true return means the page must render (caller requestUpdate()s).
  struct NoteSheetGeometry;
  NoteSheetGeometry noteSheetGeometry() const;
  // Opens the sheet on highlight `highlight` (list index), prefilled with its
  // note. `pageChanged`: the page re-renders first (a highlight just saved).
  bool openNoteEditorLocked(int highlight, bool pageChanged);
  // Re-wraps the note for the sheet and keeps the cursor line in view.
  void relayoutNoteLocked();
  void drawNoteSheet() const;
  // *help: Alt+v asked for the note help (caller opens it after the lock).
  bool noteKeyLocked(const freeink::KeyEvent& event, bool* help);
  bool noteTouchLocked(int x, int y);
  bool saveNoteLocked();
  bool cancelNoteLocked();
  // From hintsTick(): pushes typed changes once the panel is free (typing
  // during a refresh is coalesced into the next one).
  void noteSheetTick();

  // DECKPOINT: `/` search with n / N (deckpoint/reader/EpubReaderSearch.cpp).
  // The session runs in slices from loop() and owns input while it exists;
  // searchMark (shared with the render task, RenderLock) is the inverted hit.
  std::unique_ptr<deckpoint::reader::SearchSession> search;
  deckpoint::reader::SearchMark searchMark;
  char lastSearch[deckpoint::CommandLine::MAX_LEN + 1] = {};
  void openSearchPrompt(bool pageDirty);
  // Runs `query` (empty: the last one) from the current page; false (with msg
  // filled) when it cannot start.
  bool startSearch(const char* query, bool forward, bool fromPrompt, char* msg, size_t msgSize);
  void searchNextKey(bool forward);
  bool searchKey(const freeink::KeyEvent& event);
  // From loop(): true while a search runs.
  bool searchTick();
  enum class SearchStep : uint8_t { Continue, Yield, Hit, NotFound, Failed, Cancelled };
  SearchStep searchStep(char* msg, size_t msgSize);
  void finishSearch(SearchStep how, const char* msg);
  void paintSearchProgress();
  // Drops the inverted hit; restore = put the clean page back now (else the
  // caller is about to render a page anyway).
  void clearSearchMark(bool restore);
  // Caller holds the RenderLock for these.
  bool drawSearchMarkLocked();
  void searchBeforeRender();
  void searchAfterRender();

  // DECKPOINT: highlights / notes (deckpoint/reader/EpubReaderAnnotations.cpp).
  // Opened with the book (book id + its file, if any); placement is resolved
  // per chapter on first draw. Shared with the render task (RenderLock).
  std::unique_ptr<deckpoint::annotations::AnnotationStore> annotationStore;
  void openAnnotations();
  // Underline + note-marker rectangles for this page; empty (and no work
  // beyond one list scan) when the chapter has no highlights. Caller holds the
  // RenderLock; fonts must be ready (after the prewarm scan).
  void planAnnotationMarks(const Page& page, int fontId, int marginLeft, int marginTop,
                           std::vector<deckpoint::annotations::MarkRect>& out);
  void drawAnnotationMarks(const std::vector<deckpoint::annotations::MarkRect>& marks) const;
  // From loop(): runs a serial seeding request (CMD:ANNOTATE...), if any.
  void annotationsTick();

  // DECKPOINT: notes list + export (deckpoint/reader/EpubReaderNotes.cpp).
  // fromKeys: cancel returns to the page (not the reader menu). False when
  // the book has no annotation store (or OOM).
  bool openNotesList(bool fromKeys);
  // Goes to the page holding highlight `index`'s start (resolving its chapter
  // first if needed); editNote opens the note sheet there after the render.
  void jumpToAnnotation(int index, bool editNote, const deckpoint::reader::MarkPosition& origin);
  // Highlight whose note sheet the next hint session opens on (-1: none).
  // Shared with the render task (RenderLock).
  int pendingNoteHighlight = -1;
  // From beginHints(): installs `session` with the pending highlight's note
  // sheet. False (session dropped) when the highlight is not on the page.
  bool openPendingNoteLocked(std::unique_ptr<deckpoint::reader::HintSession> session);
  // `:export`: Markdown + My Clippings; msg gets the outcome.
  bool exportNotes(char* msg, size_t msgSize);

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  void applyOrientation(uint8_t orientation);
  void applyInitialOrientation() override;
  // The orientation the current layout was built for. The control center's
  // orientation tile can move SETTINGS.orientation while this reader sits on
  // the activity stack, and Pop restores it without onEnter(), so the drift has
  // to be noticed here rather than assumed away.
  uint8_t appliedOrientation = 0;

  // Modal shown when a protected book refuses to open (loan expired /
  // date unverified); OK exits, "Sync time" (when offered) verifies the
  // clock over Wi-Fi and reopens the book.
  OptionPopup loadFailurePopup;
  // Protection error captured by loadBook() for handleLoadFailure(); the
  // failed Epub itself does not outlive loadBook().
  std::string loadProtectionError;

  bool loadBook() override;
  bool handleLoadFailure() override;
  // Wi-Fi join + SNTP for a loan whose date could not be verified, then a
  // clean re-open of the book.
  void beginLoanTimeSync();
  std::string getBookTitle() const override { return epub ? epub->getTitle() : ""; }
  std::string getBookAuthor() const override { return epub ? epub->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return epub ? epub->getThumbBmpPath() : ""; }
  int getProgressBasisPoints() const override;
  void renderBook() override;
  void onEndOfBookRendered() override;

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                              bool allowFastInitialRefresh)
      : ReaderActivity("EpubReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~EpubReaderActivity() override;

  const deckpoint::KeyHelp* keyHelp() const override { return &deckpoint::READER_KEY_HELP; }  // DECKPOINT
  // DECKPOINT: raw keyboard keys while reading (see wantsRawKeys gating).
  bool wantsRawKeys() const override;
  void onKey(const freeink::KeyEvent& event) override;
  void loop() override;
  void render(RenderLock&& lock) override;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  bool skipLoopDelay() override;

  ScreenshotInfo getScreenshotInfo() const override;
  CrossPointPosition getCurrentPosition() const;
};
