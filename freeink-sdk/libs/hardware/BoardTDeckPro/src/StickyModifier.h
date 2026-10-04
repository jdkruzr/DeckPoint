#pragma once

#include <cstdint>

namespace BoardTDeckPro {

// One keyboard modifier (Shift, Sym, Alt). It works chorded (held while
// another key is pressed) or sticky: tapped alone it latches for the next key
// (one-shot); tapped again it locks (canLock) or cancels the latch (!canLock);
// a tap while locked turns it off. Pure state machine, host-tested.
struct StickyModifier {
  bool canLock = true;
  uint8_t heldCount = 0;  // two Shift keys can be down at once
  bool usedWhileHeld = false;
  bool latched = false;
  bool locked = false;

  bool active() const { return heldCount > 0 || latched || locked; }
  // One-shot pending or locked: the state a status badge shows.
  bool sticky() const { return latched || locked; }
  void press() {
    heldCount++;
    usedWhileHeld = false;
  }
  void release() {
    if (heldCount > 0) heldCount--;
    if (heldCount > 0 || usedWhileHeld) return;
    if (locked) {
      locked = false;
    } else if (latched) {
      latched = false;
      locked = canLock;
    } else {
      latched = true;
    }
  }
  // A non-modifier key was pressed: chords are spent, one-shots cleared.
  void consume() {
    if (heldCount > 0) usedWhileHeld = true;
    latched = false;
  }
};

}  // namespace BoardTDeckPro
