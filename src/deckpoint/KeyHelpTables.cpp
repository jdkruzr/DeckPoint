#include "KeyHelp.h"

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
    {"Esc", StrId::STR_KH_RESUME},
};

constexpr KeyHelpEntry LIBRARY[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"h / l", StrId::STR_KH_SWITCH_TAB},
    {"Enter", StrId::STR_KH_OPEN},
    {"hold Enter", StrId::STR_KH_LIBRARY_HOLD_ENTER},
    {"k on tabs", StrId::STR_KH_SEARCH},
    {"hold j / k", StrId::STR_KH_PAGE_JUMP},
    {"Esc", StrId::STR_KH_LIBRARY_BACK},
};

constexpr KeyHelpEntry FILE_BROWSER[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"hold j / k", StrId::STR_KH_PAGE_JUMP},
    {"Enter", StrId::STR_KH_OPEN},
    {"hold Enter", StrId::STR_KH_FILE_ACTIONS},
    {"Esc", StrId::STR_KH_UP_FOLDER},
    {"hold Esc", StrId::STR_KH_TOP_FOLDER},
};

constexpr KeyHelpEntry SETTINGS_SCREEN[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"h / l", StrId::STR_KH_SWITCH_TAB},
    {"Enter", StrId::STR_KH_CHANGE_SETTING},
    {"Esc", StrId::STR_KH_SETTINGS_BACK},
};

constexpr KeyHelpEntry TEXT_SETTINGS[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"h / l", StrId::STR_KH_SWITCH_TAB},
    {"Enter", StrId::STR_KH_CHANGE_SETTING},
    {"Esc", StrId::STR_KH_BACK},
};

constexpr KeyHelpEntry READER[] = {
    {"Space / j / l", StrId::STR_KH_NEXT_PAGE},
    {"k / h", StrId::STR_KH_PREV_PAGE},
    {"Enter", StrId::STR_KH_READER_MENU},
    {"Esc", StrId::STR_KH_GO_HOME},
};

constexpr KeyHelpEntry READER_MENU[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"hold j / k", StrId::STR_KH_PAGE_JUMP},
    {"Enter", StrId::STR_KH_CHOOSE},
    {"Esc", StrId::STR_KH_BACK_TO_BOOK},
};

constexpr KeyHelpEntry BOOKMARKS[] = {
    {"j / k", StrId::STR_KH_MOVE},
    {"hold j / k", StrId::STR_KH_PAGE_JUMP},
    {"Enter", StrId::STR_KH_GO_TO_BOOKMARK},
    {"hold Enter", StrId::STR_KH_BOOKMARK_ACTIONS},
    {"Esc", StrId::STR_KH_BACK_TO_BOOK},
};

constexpr KeyHelpEntry WORD_SELECT[] = {
    {"h / l", StrId::STR_KH_PREV_NEXT_WORD},
    {"k / j", StrId::STR_KH_PREV_NEXT_LINE},
    {"Enter", StrId::STR_KH_LOOK_UP},
    {"Esc", StrId::STR_KH_BACK_TO_BOOK},
};

constexpr KeyHelpEntry DEFINITION[] = {
    {"Space / j / l", StrId::STR_KH_NEXT_PAGE},
    {"k / h", StrId::STR_KH_PREV_PAGE},
    {"Esc", StrId::STR_KH_BACK},
};

constexpr KeyHelpEntry KEYBOARD_ENTRY[] = {
    {"Enter", StrId::STR_KH_DONE},
    {"Esc", StrId::STR_KH_CANCEL},
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
    {"Esc", StrId::STR_KH_BACK_TO_FILES},
};

constexpr KeyHelpEntry GO_TO_PERCENT[] = {
    {"0 - 9", StrId::STR_KH_TYPE_PERCENT},
    {"h / l", StrId::STR_KH_STEP_SMALL},
    {"j / k", StrId::STR_KH_STEP_LARGE},
    {"Backspace", StrId::STR_KH_DELETE_DIGIT},
    {"Enter", StrId::STR_KH_GO},
    {"Esc", StrId::STR_KH_CANCEL},
};

}  // namespace

constexpr KeyHelp HOME_KEY_HELP = makeKeyHelp(HOME);
constexpr KeyHelp LIBRARY_KEY_HELP = makeKeyHelp(LIBRARY);
constexpr KeyHelp FILE_BROWSER_KEY_HELP = makeKeyHelp(FILE_BROWSER);
constexpr KeyHelp SETTINGS_KEY_HELP = makeKeyHelp(SETTINGS_SCREEN);
constexpr KeyHelp TEXT_SETTINGS_KEY_HELP = makeKeyHelp(TEXT_SETTINGS);
constexpr KeyHelp READER_KEY_HELP = makeKeyHelp(READER);
constexpr KeyHelp READER_MENU_KEY_HELP = makeKeyHelp(READER_MENU);
constexpr KeyHelp BOOKMARKS_KEY_HELP = makeKeyHelp(BOOKMARKS);
constexpr KeyHelp WORD_SELECT_KEY_HELP = makeKeyHelp(WORD_SELECT);
constexpr KeyHelp DEFINITION_KEY_HELP = makeKeyHelp(DEFINITION);
constexpr KeyHelp KEYBOARD_ENTRY_KEY_HELP = makeKeyHelp(KEYBOARD_ENTRY);
constexpr KeyHelp IMAGE_VIEWER_KEY_HELP = makeKeyHelp(IMAGE_VIEWER);
constexpr KeyHelp GO_TO_PERCENT_KEY_HELP = makeKeyHelp(GO_TO_PERCENT);

}  // namespace deckpoint
