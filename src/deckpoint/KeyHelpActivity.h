#pragma once

// DECKPOINT: full-screen keyboard help opened by `?` on any screen (Alt+? in
// text fields). Shows the owning screen's hand-written table, or its last key
// legend in key terms, then the keys that work everywhere.

#include <atomic>

#include "KeyHelp.h"
#include "KeyHelpText.h"
#include "activities/Activity.h"

namespace deckpoint {

class KeyHelpActivity final : public Activity {
 public:
  // `ownerName` / `table` describe the screen the help is for (the one being covered).
  KeyHelpActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* ownerName,
                  const KeyHelp* table);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool wantsRawKeys() const override;
  void onKey(const freeink::KeyEvent& event) override;

 private:
  struct Row {
    const char* keys;
    const char* what;
    bool heading;
  };
  static constexpr int MAX_ROWS = 48;
  // Key text for a table's runtime-composed extra section (KeyHelpExtra).
  static constexpr int MAX_EXTRA_ROWS = 24;
  static constexpr int EXTRA_KEYS_LEN = 24;

  void buildRows();
  void addRow(const char* keys, const char* what, bool heading = false);
  // +1 pages down, -1 pages up; false when there was nowhere to go.
  bool scroll(int direction);

  char ownerName[32];
  char title[64];
  const KeyHelp* table;
  LegendSnapshot legend{};
  Row rows[MAX_ROWS]{};
  char extraKeys[MAX_EXTRA_ROWS][EXTRA_KEYS_LEN]{};
  int rowCount = 0;
  int topRow = 0;  // main task writes under RenderLock; render reads
  // Set by render: first row that did not fit (rowCount when all fit).
  std::atomic<int> firstHiddenRow{0};
};

// Push the key help screen over the current activity.
void openKeyHelp(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* ownerName,
                 const KeyHelp* table);

}  // namespace deckpoint
