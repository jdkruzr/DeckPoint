#include "NotesListActivity.h"

#include <GfxRenderer.h>
#include <HalKeyboard.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "deckpoint/KeyLegend.h"
#include "deckpoint/annotations/AnnotationExport.h"
#include "fontIds.h"

namespace fui = freeink::ui;
using deckpoint::annotations::Annotation;
using deckpoint::annotations::Field;

namespace {
constexpr int ENTER_ACTIONS_MODE_MS = 700;

enum class RowAction : uint8_t { GoTo, EditNote, Delete };
}  // namespace

NotesListActivity::NotesListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Epub> epub,
                                     deckpoint::annotations::AnnotationStore& store)
    : UiListActivity("NotesList", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      epub(std::move(epub)),
      store(store) {}

void NotesListActivity::onEnter() {
  rebuildRows();  // before the base requests the first render
  UiListActivity::onEnter();
}

bool NotesListActivity::editable() const { return !store.readOnly(); }

void NotesListActivity::rebuildRows() {
  const auto& list = store.annotations();
  deckpoint::annotations::browseOrder(list, order);
  rows.clear();
  rows.reserve(order.size());

  // A heading starts each run of highlights from the same chapter. Entries
  // without a chapter name get their TOC title, held in `headings`, which is
  // reserved up front so the row pointers into it stay valid.
  const auto startsGroup = [&](const size_t row) {
    if (row == 0) return true;
    const Annotation& a = list[order[row]];
    const Annotation& prev = list[order[row - 1]];
    return a.spineIndex != prev.spineIndex || a.get(Field::Chapter) != prev.get(Field::Chapter);
  };
  size_t fallbackCount = 0;
  for (size_t row = 0; row < order.size(); row++) {
    if (startsGroup(row) && !list[order[row]].has(Field::Chapter)) fallbackCount++;
  }
  headings.clear();
  headings.reserve(fallbackCount);
  // A row's subtitle is one line: multi-line notes show their first line only.
  size_t multiLineNotes = 0;
  for (const uint16_t i : order) {
    if (list[i].has(Field::Note) && list[i].get(Field::Note).find('\n') != std::string_view::npos) multiLineNotes++;
  }
  noteFirstLines.clear();
  noteFirstLines.reserve(multiLineNotes);

  for (size_t row = 0; row < order.size(); row++) {
    const Annotation& a = list[order[row]];
    fui::ListItem item;
    // get() views are NUL-terminated and live as long as the annotation's block.
    item.label = a.has(Field::Text) ? a.get(Field::Text).data() : tr(STR_UNNAMED);
    item.subtitle = nullptr;
    if (a.has(Field::Note)) {
      const std::string_view note = a.get(Field::Note);
      const size_t newline = note.find('\n');
      if (newline == std::string_view::npos) {
        item.subtitle = note.data();
      } else {
        noteFirstLines.emplace_back(note.substr(0, newline));
        item.subtitle = noteFirstLines.back().c_str();
      }
    }
    item.actionValue = static_cast<int16_t>(row);
    if (startsGroup(row)) {
      if (a.has(Field::Chapter)) {
        item.sectionHeading = a.get(Field::Chapter).data();
      } else {
        const int toc = epub && a.spineIndex >= 0 ? epub->getTocIndexForSpineIndex(a.spineIndex) : -1;
        headings.push_back(toc >= 0 ? epub->getTocItem(toc).title : std::string(tr(STR_UNNAMED)));
        item.sectionHeading = headings.back().c_str();
      }
    }
    rows.push_back(item);
  }
}

const char* NotesListActivity::headerTitle() const {
  return deleteArmed >= 0 ? tr(STR_NOTES_DELETE_AGAIN) : tr(STR_NOTES);
}

const char* NotesListActivity::legendExtra() const {
  if (deleteArmed >= 0) return tr(STR_NOTES_DELETE_AGAIN);
  if (order.empty() || !editable()) return nullptr;
  return halKeyboard.present() ? tr(STR_NOTES_LEGEND) : tr(STR_NOTES_LEGEND_VIEW);
}

void NotesListActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(deleteArmed >= 0 ? tr(STR_CANCEL) : tr(STR_BACK),
                                            order.empty() ? "" : tr(STR_NOTES_GO), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void NotesListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (rows.empty()) {
    screen.centeredText(tr(STR_NOTES_EMPTY), screen.theme().bodyText);
    return;
  }
  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;  // buttons stay in loop()
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;  // the passage, ellipsized
  syncListViewport(screen, props);
  screen.list(props);
}

void NotesListActivity::render(RenderLock&&) {
  // UiListActivity::render plus the actions / confirm popup.
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; activeNav().consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  if (popup.processRender(renderer, mappedInput)) return;
  deckpoint::setLegendExtra(legendExtra());
  drawFooter();
  deckpoint::setLegendExtra(nullptr);
  renderer.displayBuffer();
}

void NotesListActivity::finishWith(const int row, const bool editNote) {
  if (row < 0 || row >= listCount()) return;
  app.clearTapFlash();
  AnnotationResult result;
  result.index = order[static_cast<size_t>(row)];
  result.editNote = editNote;
  setResult(std::move(result));
  finish();
}

void NotesListActivity::activateIndex(const int index) {
  if (popup.isActive() || index < 0 || index >= listCount()) return;
  nav.selected = index;
  finishWith(index, false);
}

void NotesListActivity::onRowLongPress(const int index) {
  if (popup.isActive() || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  showActions();
}

bool NotesListActivity::handleCustomInput() {
  if (popup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete) {
    // Dismissed without a choice: no delete.
    confirmingDelete = false;
    requestUpdate();
    return true;
  }
  if (deleteArmed >= 0 && deleteArmed != nav.selected) {
    deleteArmed = -1;  // moved away: the next `x` asks again
    requestUpdate();
  }
  return false;
}

bool NotesListActivity::handleButtons() {
  if (!order.empty() && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, ENTER_ACTIONS_MODE_MS)) {
    showActions();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (deleteArmed >= 0) {
      deleteArmed = -1;
      requestUpdate();
      return true;
    }
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finishWith(nav.selected, false);
    return true;
  }
  return false;
}

void NotesListActivity::onUnmappedKey(const freeink::KeyEvent& event) {
  if (popup.isActive() || order.empty() || (event.mods & freeink::KeyMod::Alt) != 0) return;
  const int row = nav.selected;
  if (row < 0 || row >= listCount()) return;
  if (event.ch == 'x' && editable()) {
    if (deleteArmed == row) {
      deleteRow(row);
    } else {
      deleteArmed = row;
      requestUpdate();
    }
    return;
  }
  if (deleteArmed >= 0 && event.ch != 0) {
    deleteArmed = -1;  // any other key: no delete
    requestUpdate();
  }
  if (event.ch == 'e' && editable()) finishWith(row, true);
}

void NotesListActivity::showActions() {
  if (order.empty() || popup.isActive()) return;
  static constexpr size_t MAX_ACTIONS = 3;
  const char* labels[MAX_ACTIONS];
  RowAction actions[MAX_ACTIONS];
  int count = 0;
  labels[count] = tr(STR_NOTES_GO);
  actions[count++] = RowAction::GoTo;
  if (editable() && halKeyboard.present()) {
    labels[count] = tr(STR_SEL_EDIT_NOTE);
    actions[count++] = RowAction::EditNote;
  }
  if (editable()) {
    labels[count] = tr(STR_DELETE);
    actions[count++] = RowAction::Delete;
  }
  const RowAction goTo = actions[0];
  const RowAction second = count > 1 ? actions[1] : goTo;
  const RowAction third = count > 2 ? actions[2] : goTo;
  popup.show(tr(STR_NOTES), labels, count, 0, [this, goTo, second, third](const int idx) {
    const RowAction action = idx == 0 ? goTo : idx == 1 ? second : third;
    switch (action) {
      case RowAction::GoTo:
        finishWith(nav.selected, false);
        break;
      case RowAction::EditNote:
        finishWith(nav.selected, true);
        break;
      case RowAction::Delete:
        confirmDelete();
        break;
    }
  });
  requestUpdate();
}

void NotesListActivity::confirmDelete() {
  if (order.empty() || popup.isActive()) return;
  confirmingDelete = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  popup.show(tr(STR_NOTES_CONFIRM_DELETE), options, 2, 0, [this](const int idx) {
    confirmingDelete = false;
    if (idx == 1) deleteRow(nav.selected);
    requestUpdate();
  });
  requestUpdate();
}

void NotesListActivity::deleteRow(const int row) {
  deleteArmed = -1;
  if (row < 0 || row >= listCount()) return;
  const size_t index = order[static_cast<size_t>(row)];
  bool deleted;
  {
    // The tombstone rebuilds the entry's string block, which the rows point into.
    RenderLock lock;
    deleted = store.deleteAndSave(index);
    rebuildRows();
  }
  if (!deleted) LOG_ERR("NTL", "Delete failed for annotation %u", static_cast<unsigned>(index));
  if (nav.selected >= listCount() && nav.selected > 0) nav.selected--;
  nav.follow(listCount());
  requestUpdate();
}
