#pragma once

// DECKPOINT: the open book's highlights and notes (`:notes`, reader menu
// "Notes"), in book order under chapter headings. Each row is the highlighted
// text (two lines, ellipsized) with the note's first line under it.
//
// Keys: j / k move (hold: page), Enter goes to the highlight, `e` goes there
// and opens the note sheet, `x` twice deletes (tombstone, no undo), mic back.
// Touch: tap goes to it, long-press (or hold Enter) opens Go to / Edit note /
// Delete. Finishes with an AnnotationResult the reader turns into the jump.
//
// The store is the reader's (it stays on the stack under this screen); row
// labels point straight into the annotations' string blocks, so nothing is
// copied per row.

#include <Epub.h>

#include <memory>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "deckpoint/KeyHelp.h"
#include "deckpoint/annotations/AnnotationStore.h"

class NotesListActivity final : public UiListActivity {
 public:
  NotesListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Epub> epub,
                    deckpoint::annotations::AnnotationStore& store);

  const deckpoint::KeyHelp* keyHelp() const override { return &deckpoint::NOTES_KEY_HELP; }
  void onEnter() override;
  void render(RenderLock&&) override;
  void onUnmappedKey(const freeink::KeyEvent& event) override;

 private:
  int listCount() const override { return static_cast<int>(order.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  const char* headerTitle() const override;
  const char* legendExtra() const override;
  void drawFooter() override;

  // Rebuilds order / rows / headings from the store. Caller holds the RenderLock
  // once the screen is up (row labels alias the annotations' string blocks).
  void rebuildRows();
  bool editable() const;
  void finishWith(int row, bool editNote);
  void showActions();
  void confirmDelete();
  void deleteRow(int row);

  std::shared_ptr<Epub> epub;
  deckpoint::annotations::AnnotationStore& store;
  std::vector<uint16_t> order;              // list indices in book order
  std::vector<freeink::ui::ListItem> rows;  // one per order entry
  std::vector<std::string> headings;        // TOC titles for entries without a chapter
  std::vector<std::string> noteFirstLines;  // subtitle copies for multi-line notes
  int deleteArmed = -1;                     // row the first `x` was pressed on
  bool confirmingDelete = false;
  OptionPopup popup;
};
