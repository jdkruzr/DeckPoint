#pragma once

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "deckpoint/KeyHelp.h"  // DECKPOINT
#include "util/ButtonNavigator.h"

class EpubReaderPercentSelectionActivity final : public Activity, private UiAppHost {
 public:
  // Slider-style percent selector for jumping within a book.
  explicit EpubReaderPercentSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              int initialPercent);

  const deckpoint::KeyHelp* keyHelp() const override { return &deckpoint::GO_TO_PERCENT_KEY_HELP; }  // DECKPOINT
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool wantsRawKeys() const override;
  void onKey(const freeink::KeyEvent& event) override;
  void render(RenderLock&&) override;

 private:
  // The UiAppHost app hosts the shared slider dialog popup (capsule slider,
  // [-] [+] [Confirm] buttons) drawn over the screen underneath.
  static void percentScreen(UiScreen& screen, void* user);
  static void onSliderEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onStepEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onOkEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildPercentScreen(UiScreen& screen);
  void cancel();
  void confirm();

  // Current percent value (0-100) shown on the slider.
  int percent = 0;

  ButtonNavigator buttonNavigator;

  // Swallow the swipe/tap fallout of a slider drag so its release can't trigger
  // the back gesture and cancel the dialog, or step the percent as a swipe.
  bool draggingSlider = false;
  uint8_t typedDigits = 0;  // DECKPOINT: digits typed so far (0 = not typing)

  // Change the current percent by a delta and wrap within bounds.
  void adjustPercent(int delta);
  // Absolute percent (clamped 0-100), from slider drag/tap positions.
  void setPercent(int value);
};
