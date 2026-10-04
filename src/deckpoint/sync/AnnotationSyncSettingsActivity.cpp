#include "AnnotationSyncSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>
#include <WiFi.h>

#include <memory>

#include "AnnotationSyncStore.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "WebDavClient.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
enum Row { ROW_ENABLED, ROW_URL, ROW_USER, ROW_PASSWORD, ROW_FOLDER, ROW_TEST };

constexpr StrId ROW_LABELS[AnnotationSyncSettingsActivity::MENU_ITEMS] = {StrId::STR_ANNOTATION_SYNC_ENABLED,
                                                                          StrId::STR_WEBDAV_URL,
                                                                          StrId::STR_USERNAME,
                                                                          StrId::STR_APP_PASSWORD,
                                                                          StrId::STR_SYNC_FOLDER,
                                                                          StrId::STR_TEST_CONNECTION};

void save() {
  if (!ANNOTATION_SYNC_STORE.saveToFile()) LOG_ERR("ASYNC", "Failed to save annotation sync settings");
}
}  // namespace

namespace annotationsync {
const char* errorMessage(const dav::Error error) {
  switch (error) {
    case dav::Error::None:
      return tr(STR_CONNECTION_OK);
    case dav::Error::Auth:
      return tr(STR_DAV_ERR_AUTH);
    case dav::Error::NotFound:
      return tr(STR_DAV_ERR_NOT_FOUND);
    case dav::Error::Network:
      return tr(STR_DAV_ERR_NETWORK);
    case dav::Error::Tls:
      return tr(STR_DAV_ERR_TLS);
    case dav::Error::Server:
      return tr(STR_DAV_ERR_SERVER);
    case dav::Error::NoMemory:
      return tr(STR_DAV_ERR_NO_MEMORY);
    case dav::Error::BadUrl:
      return tr(STR_DAV_ERR_BAD_URL);
    case dav::Error::SdIo:
      return tr(STR_DAV_ERR_SD);
    case dav::Error::TooLarge:
      return tr(STR_DAV_ERR_TOO_LARGE);
    default:
      return tr(STR_DAV_ERR_OTHER);
  }
}
}  // namespace annotationsync

AnnotationSyncSettingsActivity::AnnotationSyncSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("AnnotationSyncSettings", renderer, mappedInput) {
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems[i].label = I18N.get(ROW_LABELS[i]);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
}

const char* AnnotationSyncSettingsActivity::headerTitle() const { return tr(STR_ANNOTATION_SYNC); }

void AnnotationSyncSettingsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  auto& store = ANNOTATION_SYNC_STORE;
  // One keyboard entry per text row; the setter runs only on Enter.
  const auto editText = [this](const char* title, const std::string& initial, const size_t maxLen, const InputType type,
                               void (*apply)(const std::string&)) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, title, initial, maxLen, type),
                           [apply](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             apply(std::get<KeyboardResult>(result.data).text);
                             save();
                           });
  };

  switch (index) {
    case ROW_ENABLED:
      store.setEnabled(!store.isEnabled());
      save();
      requestUpdate();
      break;
    case ROW_URL:
      // Prefill the scheme: '/' and ':' sit on the Sym layer.
      editText(tr(STR_WEBDAV_URL), store.getServerUrl().empty() ? "https://" : store.getServerUrl(),
               AnnotationSyncStore::MAX_URL, InputType::Url, [](const std::string& v) {
                 ANNOTATION_SYNC_STORE.setServerUrl((v == "https://" || v == "http://") ? "" : v);
               });
      break;
    case ROW_USER:
      editText(tr(STR_USERNAME), store.getUsername(), AnnotationSyncStore::MAX_FIELD, InputType::Text,
               [](const std::string& v) { ANNOTATION_SYNC_STORE.setUsername(v); });
      break;
    case ROW_PASSWORD:
      editText(tr(STR_APP_PASSWORD), store.getPassword(), AnnotationSyncStore::MAX_FIELD, InputType::Password,
               [](const std::string& v) { ANNOTATION_SYNC_STORE.setPassword(v); });
      break;
    case ROW_FOLDER:
      editText(tr(STR_SYNC_FOLDER), store.getFolder().empty() ? "/" : store.getFolder(), AnnotationSyncStore::MAX_FIELD,
               InputType::Text, [](const std::string& v) { ANNOTATION_SYNC_STORE.setFolder(v == "/" ? "" : v); });
      break;
    case ROW_TEST:
      if (!store.isConfigured()) return;
      if (auto activity = makeUniqueNoThrow<AnnotationSyncTestActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), [](const ActivityResult&) {});
      } else {
        LOG_ERR("ASYNC", "OOM: AnnotationSyncTestActivity");
      }
      break;
    default:
      break;
  }
}

void AnnotationSyncSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const auto& store = ANNOTATION_SYNC_STORE;
  const auto orNotSet = [](std::string& out, const std::string& v) { out = v.empty() ? tr(STR_NOT_SET) : v; };
  rowValues[ROW_ENABLED].clear();
  orNotSet(rowValues[ROW_URL], store.getServerUrl());
  orNotSet(rowValues[ROW_USER], store.getUsername());
  rowValues[ROW_PASSWORD] = store.getPassword().empty() ? tr(STR_NOT_SET) : "******";
  rowValues[ROW_FOLDER] = store.getFolder().empty() ? tr(STR_SYNC_FOLDER_HINT) : store.getFolder();
  rowValues[ROW_TEST] = store.isConfigured() ? "" : std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
  for (int i = 0; i < MENU_ITEMS; i++) rowItems[i].value = rowValues[i].empty() ? nullptr : rowValues[i].c_str();
  GUI.setCheckboxRow(rowItems[ROW_ENABLED], store.isEnabled());

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(MENU_ITEMS);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

// --- Test connection ---------------------------------------------------------

void AnnotationSyncTestActivity::onEnter() {
  Activity::onEnter();
  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void AnnotationSyncTestActivity::onWifiSelectionComplete(const bool success) {
  {
    RenderLock lock(*this);
    state = success ? CHECKING : FAILED;
    message = success ? tr(STR_CHECKING_FOLDER) : tr(STR_WIFI_CONN_FAILED);
  }
  requestUpdate();
  if (success) runCheck();
}

void AnnotationSyncTestActivity::runCheck() {
  const auto& store = ANNOTATION_SYNC_STORE;
  // Heap, not stack: the client embeds the TLS transport and a 1 KB upload
  // chunk. Freed at scope exit, before the result screen renders.
  auto client = makeUniqueNoThrow<WebDavClient>();
  dav::Error error = dav::Error::NoMemory;
  bool isFolder = false;
  if (client) {
    client->setCredentials(store.getUsername(), store.getPassword());
    dav::Props props;
    const WebDavClient::Result r = client->propfind(store.folderUrl(), props);
    if (r.date[0]) trustedtime::applyHttpDate(r.date);
    LOG_INF("ASYNC", "Test connection: status %d, date \"%s\", etag %s", r.status, r.date, props.etag);
    error = r.error;
    isFolder = props.isCollection;
  } else {
    LOG_ERR("ASYNC", "OOM: WebDavClient");
  }
  // A file at the folder path answers 207 too; it is not a usable folder.
  if (error == dav::Error::None && !isFolder) error = dav::Error::NotFound;
  {
    RenderLock lock(*this);
    state = error == dav::Error::None ? SUCCESS : FAILED;
    message = error == dav::Error::None ? tr(STR_FOLDER_FOUND) : annotationsync::errorMessage(error);
  }
  requestUpdate();
}

void AnnotationSyncTestActivity::onExit() {
  Activity::onExit();
  // Same as the KOReader auth screen: drop Wi-Fi and reboot back here on a
  // clean heap (a TLS session leaves the heap fragmented on PSRAM-less boards).
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestartToSettings();
  }
}

void AnnotationSyncTestActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TEST_CONNECTION));

  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;
  if (state == SUCCESS || state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, state == SUCCESS ? tr(STR_CONNECTION_OK) : tr(STR_CONNECTION_FAILED),
                              true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + height + 10, message);
  } else if (state == CHECKING) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, message);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void AnnotationSyncTestActivity::loop() {
  if (state != SUCCESS && state != FAILED) return;
  int x = 0;
  int y = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}
