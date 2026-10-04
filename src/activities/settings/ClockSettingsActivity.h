#pragma once
#include "activities/UiListActivity.h"

// Clock configuration under System settings: timezone, 12/24-hour format,
// home-header display, and manual NTP sync. On boards without an RTC only the
// timezone, DST and sync rows are listed (the system clock still keeps time).
class ClockSettingsActivity final : public UiListActivity {
 public:
  explicit ClockSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int ITEM_COUNT = 5;

  void onEnter() override;

 private:
  int listCount() const override { return rowCount_; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Row values are flash/translation strings assigned directly into rowItems_;
  // the one formatted value (the sync row's live time) lives here so its
  // pointer stays valid across the frame. Fits "HH:MM PM" + NUL.
  char syncTime_[9] = {0};
  // Visible rows; each row's actionValue is its MenuItem.
  freeink::ui::ListItem rowItems_[ITEM_COUNT]{};
  int rowCount_ = 0;
};
