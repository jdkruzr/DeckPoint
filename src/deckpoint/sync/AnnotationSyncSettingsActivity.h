#pragma once

#include <string>

#include "WebDavProtocol.h"
#include "activities/Activity.h"
#include "activities/UiListActivity.h"

// DECKPOINT: Settings > System > Annotation Sync. WebDAV location and
// credentials for AnnotationSync-compatible highlight sync (Nextcloud:
// https://<host>/remote.php/dav/files/<user>/ plus a folder such as /eBooks).
class AnnotationSyncSettingsActivity final : public UiListActivity {
 public:
  AnnotationSyncSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEMS = 6;

 private:
  int listCount() const override { return MENU_ITEMS; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Fixed-capacity rows: labels set once, values refreshed in place.
  std::string rowValues[MENU_ITEMS];
  freeink::ui::ListItem rowItems[MENU_ITEMS]{};
};

// Joins Wi-Fi if needed, PROPFINDs the configured folder (Depth: 0) and shows
// OK / wrong credentials / folder not found / network error.
class AnnotationSyncTestActivity final : public Activity {
 public:
  AnnotationSyncTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AnnotationSyncTest", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CHECKING; }

 private:
  enum State { WIFI_SELECTION, CHECKING, SUCCESS, FAILED };
  State state = WIFI_SELECTION;
  const char* message = "";

  void onWifiSelectionComplete(bool success);
  void runCheck();
};

namespace annotationsync {
// User-facing text for a WebDAV error (tr() strings).
const char* errorMessage(dav::Error error);
}  // namespace annotationsync
