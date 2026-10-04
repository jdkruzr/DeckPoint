#include "KeyHelp.h"
#include "reader/ReaderCommands.h"

// DECKPOINT: hand-written key help per screen. Keys follow the keyboard
// bridge (BoardTDeckPro buttonMaskFor): Enter = Confirm, Esc / Backspace =
// Back, h = Left, l / Space = Right, k = Up, j = Down. Key names are the caps'
// printed names, so only the meanings go through tr().

namespace deckpoint {

namespace {

constexpr KeyHelpEntry HOME[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"h / l", StrId::STR_KH_HOME_HL},
    {"Enter", StrId::STR_KH_OPEN},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_RESUME},
    // Keyboard primer: Home's help doubles as the general introduction.
    {"mic key", StrId::STR_KH_PRIMER_MIC},
    {"Sym", StrId::STR_KH_PRIMER_SYM},
    {"Shift Sym Alt", StrId::STR_KH_PRIMER_MODS},
    {"?", StrId::STR_KH_PRIMER_HELP},
};

constexpr KeyHelpEntry LIBRARY[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"h / l", StrId::STR_KH_SWITCH_TAB},
    {"Enter", StrId::STR_KH_OPEN},
    {"hold Enter", StrId::STR_KH_LIBRARY_HOLD_ENTER},
    {"k on tabs", StrId::STR_KH_SEARCH},
    {"hold j / k", StrId::STR_KH_PAGE_JUMP},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_LIBRARY_BACK},
};

constexpr KeyHelpEntry FILE_BROWSER[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"hold j / k", StrId::STR_KH_PAGE_JUMP},
    {"Enter", StrId::STR_KH_OPEN},
    {"hold Enter", StrId::STR_KH_FILE_ACTIONS},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_UP_FOLDER},
    {"hold Esc", StrId::STR_KH_TOP_FOLDER},
};

constexpr KeyHelpEntry SETTINGS_SCREEN[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"h / l", StrId::STR_KH_SWITCH_TAB},
    {"Enter", StrId::STR_KH_CHANGE_SETTING},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_SETTINGS_BACK},
};

constexpr KeyHelpEntry TEXT_SETTINGS[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"h / l", StrId::STR_KH_SWITCH_TAB},
    {"Enter", StrId::STR_KH_CHANGE_SETTING},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_BACK},
};

// Raw-key reader (ReaderKeys): Sym gives the digits and ' ( ) / : ?
constexpr KeyHelpEntry READER[] = {
    {"j / l / Space / PgDn", StrId::STR_KH_NEXT_PAGE},
    {"k / h / b / PgUp", StrId::STR_KH_PREV_PAGE},
    {"1-999 + key", StrId::STR_KH_COUNT_PREFIX},
    {"gg / G", StrId::STR_KH_BOOK_START_END},
    {"]] [[ / ) (", StrId::STR_KH_NEXT_PREV_CHAPTER},
    {"N% / NG", StrId::STR_KH_GO_TO_PERCENT},
    {"t", StrId::STR_KH_CONTENTS},
    {"m a-z", StrId::STR_KH_SET_MARK},
    {"' a-z", StrId::STR_KH_JUMP_TO_MARK},
    {"''", StrId::STR_KH_JUMP_BACK},
    {"B", StrId::STR_KH_TOGGLE_BOOKMARK},
    {"Enter", StrId::STR_KH_READER_MENU},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_READER_ESC},
    {":", StrId::STR_KH_COMMAND_LINE},
    {"D", StrId::STR_KH_LOOKUP_TYPED},
    {"d", StrId::STR_KH_DICT_HINTS},
    {"d: a-z / Bksp / Esc", StrId::STR_KH_HINT_KEYS},
    {"v", StrId::STR_KH_HIGHLIGHT},
    {"/", StrId::STR_KH_SEARCH_BOOK},
    {"n / N", StrId::STR_KH_SEARCH_NEXT_PREV},
    {"/: Alt+k / Esc", StrId::STR_KH_SEARCH_KEYS},
};

constexpr KeyHelpEntry READER_MENU[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"hold j / k", StrId::STR_KH_PAGE_JUMP},
    {"Enter", StrId::STR_KH_CHOOSE},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_BACK_TO_BOOK},
};

constexpr KeyHelpEntry BOOKMARKS[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"hold j / k", StrId::STR_KH_PAGE_JUMP},
    {"Enter", StrId::STR_KH_GO_TO_BOOKMARK},
    {"hold Enter", StrId::STR_KH_BOOKMARK_ACTIONS},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_BACK_TO_BOOK},
};

constexpr KeyHelpEntry WORD_SELECT[] = {
    {"h / l", StrId::STR_KH_PREV_NEXT_WORD},
    {"k / j", StrId::STR_KH_PREV_NEXT_LINE},
    {"Enter", StrId::STR_KH_LOOK_UP},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_BACK_TO_BOOK},
};

constexpr KeyHelpEntry DEFINITION[] = {
    {"Space / j / l", StrId::STR_KH_NEXT_PAGE},
    {"k / h", StrId::STR_KH_PREV_PAGE},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_BACK},
};

constexpr KeyHelpEntry KEYBOARD_ENTRY[] = {
    {"Enter", StrId::STR_KH_DONE},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_CANCEL},
    {"Backspace", StrId::STR_KH_DELETE_BACK},
    {"Shift+Backspace", StrId::STR_KH_DELETE_FORWARD},
    {"Alt+Backspace", StrId::STR_KH_CLEAR_ALL},
    {"Alt+H / Alt+L", StrId::STR_KH_MOVE_CURSOR},
    {"Alt+P", StrId::STR_KH_SHOW_PASSWORD},
};

constexpr KeyHelpEntry IMAGE_VIEWER[] = {
    {"Space / j / l", StrId::STR_KH_NEXT_IMAGE},
    {"k / h", StrId::STR_KH_PREV_IMAGE},
    {"Enter", StrId::STR_KH_SET_SLEEP_COVER},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_BACK_TO_FILES},
};

constexpr KeyHelpEntry GO_TO_PERCENT[] = {
    {"0 - 9", StrId::STR_KH_TYPE_PERCENT}, {"h / l", StrId::STR_KH_STEP_SMALL},
    {"j / k", StrId::STR_KH_STEP_LARGE},   {"Backspace", StrId::STR_KH_DELETE_DIGIT},
    {"Enter", StrId::STR_KH_GO},           {DECKPOINT_KEY_MIC, StrId::STR_KH_CANCEL},
};

// `v` / long-press highlight selection (EpubReaderSelection.cpp), all stages.
constexpr KeyHelpEntry SELECTION[] = {
    {"a-z", StrId::STR_KH_SEL_PICK},
    {"Enter", StrId::STR_KH_SEL_ONE_WORD},
    {"Enter", StrId::STR_KH_SEL_SAVE},
    {"n", StrId::STR_KH_SEL_NOTE},
    {"Backspace", StrId::STR_KH_SEL_UNDO},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_SEL_CANCEL},
    {"a-z", StrId::STR_KH_SEL_ON_HIGHLIGHT},
    {"j / k, Enter", StrId::STR_KH_SEL_MENU_MOVE},
    {"d / v / n / x", StrId::STR_KH_SEL_MENU_KEYS},
};

// The reader's note sheet (EpubReaderNote.cpp). A bare '?' types, so help is Alt+v.
constexpr KeyHelpEntry NOTE_EDITOR[] = {
    {"Enter", StrId::STR_KH_NOTE_SAVE},
    {"Shift+Enter", StrId::STR_KH_NOTE_NEWLINE},
    {DECKPOINT_KEY_MIC, StrId::STR_KH_NOTE_CANCEL},
    {"Backspace", StrId::STR_KH_DELETE_BACK},
    {"Shift+Backspace", StrId::STR_KH_DELETE_FORWARD},
    {"Alt+Backspace", StrId::STR_KH_CLEAR_ALL},
    {"Alt+h / Alt+l", StrId::STR_KH_MOVE_CURSOR},
    {"Alt+k / Alt+j", StrId::STR_KH_NOTE_UP_DOWN},
};

constexpr TouchHelpEntry TOUCH_NOTE[] = {
    {StrId::STR_KH_T_TAP_NOTE_BUTTONS, StrId::STR_KH_T_NOTE_BUTTONS_WHAT, TouchWhen::Always},
    {StrId::STR_KH_T_TAP_NOTE_TEXT, StrId::STR_KH_T_NOTE_CURSOR, TouchWhen::Always},
};

constexpr TouchHelpEntry TOUCH_SELECT[] = {
    {StrId::STR_KH_T_TAP_WORD, StrId::STR_KH_T_SET_END, TouchWhen::Always},
    {StrId::STR_KH_T_TAP_BAR, StrId::STR_KH_T_BAR_WHAT, TouchWhen::Always},
    {StrId::STR_KH_T_TAP_ITEM, StrId::STR_KH_T_SELECT, TouchWhen::Always},
    {StrId::STR_KH_T_TAP_OUTSIDE, StrId::STR_KH_SEL_CANCEL, TouchWhen::Always},
};

// Rows whose gesture is switched off in Settings > Controls are left out.
constexpr TouchHelpEntry TOUCH_BOOK[] = {
    {StrId::STR_KH_T_TAP_LEFT, StrId::STR_KH_PREV_PAGE, TouchWhen::TapZones},
    {StrId::STR_KH_T_TAP_RIGHT, StrId::STR_KH_NEXT_PAGE, TouchWhen::TapZones},
    {StrId::STR_KH_T_TAP_CENTER, StrId::STR_KH_READER_MENU, TouchWhen::MenuTap},
    {StrId::STR_KH_T_SWIPE_SIDEWAYS, StrId::STR_KH_T_TURN_PAGE, TouchWhen::PageSwipe},
    {StrId::STR_KH_T_SWIPE_TOP, StrId::STR_KH_READER_MENU, TouchWhen::Always},
    {StrId::STR_KH_T_HOLD_WORD, StrId::STR_KH_T_WORD_MENU, TouchWhen::ReaderTouch},
    {StrId::STR_KH_T_TAP_HIGHLIGHT, StrId::STR_KH_T_HIGHLIGHT_MENU, TouchWhen::ReaderTouch},
    {StrId::STR_KH_T_TAP_IN_MODE, StrId::STR_KH_T_CANCEL_MODE, TouchWhen::Always},
};

constexpr TouchHelpEntry TOUCH_LISTS[] = {
    {StrId::STR_KH_T_TAP_ITEM, StrId::STR_KH_T_SELECT, TouchWhen::Always},
    {StrId::STR_KH_T_SWIPE_UP_DOWN, StrId::STR_KH_T_SCROLL, TouchWhen::Always},
};

}  // namespace

constexpr KeyHelp HOME_KEY_HELP = makeKeyHelp(HOME);
constexpr KeyHelp LIBRARY_KEY_HELP = makeKeyHelp(LIBRARY);
constexpr KeyHelp FILE_BROWSER_KEY_HELP = makeKeyHelp(FILE_BROWSER);
constexpr KeyHelp SETTINGS_KEY_HELP = makeKeyHelp(SETTINGS_SCREEN);
constexpr KeyHelp TEXT_SETTINGS_KEY_HELP = makeKeyHelp(TEXT_SETTINGS);
constexpr KeyHelp READER_KEY_HELP = makeKeyHelp(READER, &reader::READER_COMMAND_HELP);
constexpr KeyHelp READER_MENU_KEY_HELP = makeKeyHelp(READER_MENU);
constexpr KeyHelp BOOKMARKS_KEY_HELP = makeKeyHelp(BOOKMARKS);
constexpr KeyHelp WORD_SELECT_KEY_HELP = makeKeyHelp(WORD_SELECT);
constexpr KeyHelp DEFINITION_KEY_HELP = makeKeyHelp(DEFINITION);
constexpr KeyHelp KEYBOARD_ENTRY_KEY_HELP = makeKeyHelp(KEYBOARD_ENTRY);
constexpr KeyHelp IMAGE_VIEWER_KEY_HELP = makeKeyHelp(IMAGE_VIEWER);
constexpr KeyHelp GO_TO_PERCENT_KEY_HELP = makeKeyHelp(GO_TO_PERCENT);
constexpr KeyHelp SELECTION_KEY_HELP = makeKeyHelp(SELECTION);
constexpr KeyHelp NOTE_EDITOR_KEY_HELP = makeKeyHelp(NOTE_EDITOR);

constexpr TouchHelp TOUCH_BOOK_HELP = makeTouchHelp(StrId::STR_KH_SECTION_TOUCH_BOOK, TOUCH_BOOK);
constexpr TouchHelp TOUCH_LISTS_HELP = makeTouchHelp(StrId::STR_KH_SECTION_TOUCH_LISTS, TOUCH_LISTS);
constexpr TouchHelp TOUCH_SELECT_HELP = makeTouchHelp(StrId::STR_KH_SECTION_TOUCH_SELECT, TOUCH_SELECT);
constexpr TouchHelp TOUCH_NOTE_HELP = makeTouchHelp(StrId::STR_KH_SECTION_TOUCH_NOTE, TOUCH_NOTE);

}  // namespace deckpoint
