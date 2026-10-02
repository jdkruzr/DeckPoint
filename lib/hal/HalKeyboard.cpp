#include <BoardConfig.h>
#include <HalKeyboard.h>

#if FREEINK_DEVICE_TDECKPRO
#include <BoardTDeckPro.h>
#endif

HalKeyboard halKeyboard;

#if FREEINK_DEVICE_TDECKPRO

bool HalKeyboard::present() const { return BoardTDeckPro::keyboardPresent(); }
bool HalKeyboard::pop(freeink::KeyEvent& out) { return BoardTDeckPro::popKey(out); }
void HalKeyboard::flush() { BoardTDeckPro::flushKeys(); }
void HalKeyboard::setRawMode(const bool raw) { BoardTDeckPro::setRawKeyMode(raw); }
bool HalKeyboard::rawMode() const { return BoardTDeckPro::rawKeyMode(); }

#else

bool HalKeyboard::present() const { return false; }
bool HalKeyboard::pop(freeink::KeyEvent&) { return false; }
void HalKeyboard::flush() {}
void HalKeyboard::setRawMode(bool) {}
bool HalKeyboard::rawMode() const { return false; }

#endif
