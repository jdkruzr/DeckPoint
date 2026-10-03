#include "TouchTestActivity.h"

#include <BoardConfig.h>
#include <FreeInkUICore.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TouchMount.h>

#include <cstdio>
#include <cstring>

#include "fontIds.h"

namespace deckpoint {

namespace {

constexpr int GRID_STEP = 40;
constexpr int TARGET_INSET = 20;  // tap targets sit this far in from each corner
constexpr int TARGET_ARM = 6;
constexpr int DOT_HALF = 2;
// Contact travel (logical px) that makes a lift a swipe rather than a tap.
constexpr int SWIPE_LOG_MIN_PX = 20;

const char* swipeName(const freeink::ui::SwipeDir dir) {
  switch (dir) {
    case freeink::ui::SwipeDir::Left:
      return "LEFT";
    case freeink::ui::SwipeDir::Right:
      return "RIGHT";
    case freeink::ui::SwipeDir::Up:
      return "UP";
    case freeink::ui::SwipeDir::Down:
      return "DOWN";
    default:
      return "NONE";
  }
}

// Mapped panel-native point -> logical Portrait, through the same normalize +
// scale steps as InputManager::normalizeTouchPoint and GfxRenderer::tapToLogical.
freeink::TouchXY toPortrait(const uint16_t x, const uint16_t y) {
  const auto& t = BoardConfig::ACTIVE.touch;
  const int panelW = BoardConfig::ACTIVE.displayWidth;
  const int panelH = BoardConfig::ACTIVE.displayHeight;
  const int spanX = t.rawMaxX > t.rawMinX ? t.rawMaxX - t.rawMinX : 1;
  const int spanY = t.rawMaxY > t.rawMinY ? t.rawMaxY - t.rawMinY : 1;
  int phyX = static_cast<int>(static_cast<float>(x) / spanX * panelW);
  int phyY = static_cast<int>(static_cast<float>(y) / spanY * panelH);
  phyX = phyX < 0 ? 0 : (phyX > panelW - 1 ? panelW - 1 : phyX);
  phyY = phyY < 0 ? 0 : (phyY > panelH - 1 ? panelH - 1 : phyY);
  return freeink::nativeToPortrait(static_cast<uint16_t>(phyX), static_cast<uint16_t>(phyY),
                                   static_cast<uint16_t>(panelH));
}

void drawTarget(const GfxRenderer& renderer, const int x, const int y) {
  renderer.drawLine(x - TARGET_ARM, y, x + TARGET_ARM, y);
  renderer.drawLine(x, y - TARGET_ARM, x, y + TARGET_ARM);
}

}  // namespace

TouchTestActivity::TouchTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("TouchTest", renderer, mappedInput) {}

void TouchTestActivity::onEnter() {
  Activity::onEnter();
  if (!gpio.hasTouchHardware()) {
    LOG_ERR("TOUCH", "no touch controller answered at boot");
    snprintf(status, sizeof(status), "no touch controller");
  }
  LOG_INF("TOUCH", "touch test open (logical Portrait %dx%d)", BoardConfig::ACTIVE.displayHeight,
          BoardConfig::ACTIVE.displayWidth);
  requestUpdate();
}

void TouchTestActivity::onKey(const freeink::KeyEvent& /*event*/) {
  LOG_INF("TOUCH", "touch test closed");
  finish();
}

void TouchTestActivity::addDot(const int16_t x, const int16_t y, const bool strokeStart) {
  taskENTER_CRITICAL(&dotsLock);
  const int slot = (dotHead + dotCount) % MAX_DOTS;
  dots[slot] = Dot{x, y, strokeStart};
  if (dotCount < MAX_DOTS) {
    dotCount++;
  } else {
    dotHead = (dotHead + 1) % MAX_DOTS;
  }
  taskEXIT_CRITICAL(&dotsLock);
  dirty = true;
}

void TouchTestActivity::loop() {
  // Boards without the keyboard (or with it in button mode) close on Back.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  const InputManager& in = gpio.rawInput();
  if (!in.hasTouch()) return;

  const unsigned long now = millis();
  const InputManager::TouchRawSample raw = in.getTouchRawSample();
  const InputManager::TouchPoint point = in.getTouchPoint();
  const bool pressedEdge = in.wasTouchPressed();
  const bool releasedEdge = in.wasTouchReleased();
  bool forceRender = false;

  const auto logEvent = [&](const char* ev, const freeink::TouchXY& p) {
    LOG_INF("TOUCH", "raw=(%u,%u) mapped=(%u,%u) n=%u ev=%s fb=(%u,%u)", raw.x, raw.y, p.x, p.y, raw.count, ev,
            point.x, point.y);
    char line[sizeof(status)];
    snprintf(line, sizeof(line), "%s raw %u,%u -> %u,%u", ev, raw.x, raw.y, p.x, p.y);
    taskENTER_CRITICAL(&dotsLock);
    memcpy(status, line, sizeof(status));
    taskEXIT_CRITICAL(&dotsLock);
  };

  if (pressedEdge && point.valid) {
    const freeink::TouchXY p = toPortrait(point.x, point.y);
    downX = static_cast<int16_t>(p.x);
    downY = static_cast<int16_t>(p.y);
    lastX = downX;
    lastY = downY;
    lastMoveLogMs = now;
    logEvent("down", p);
    addDot(downX, downY, true);
    wasDown = true;
  } else if (wasDown && in.isTouchPressed() && point.valid && now - lastMoveLogMs >= MOVE_LOG_INTERVAL_MS) {
    const freeink::TouchXY p = toPortrait(point.x, point.y);
    if (p.x != static_cast<uint16_t>(lastX) || p.y != static_cast<uint16_t>(lastY)) {
      lastX = static_cast<int16_t>(p.x);
      lastY = static_cast<int16_t>(p.y);
      lastMoveLogMs = now;
      logEvent("move", p);
      addDot(lastX, lastY, false);
    }
  }

  if (releasedEdge && wasDown) {
    wasDown = false;
    const freeink::TouchXY p = {static_cast<uint16_t>(lastX), static_cast<uint16_t>(lastY)};
    logEvent("up", p);
    const int dx = lastX - downX;
    const int dy = lastY - downY;
    const int adx = dx < 0 ? -dx : dx;
    const int ady = dy < 0 ? -dy : dy;
    if (adx >= SWIPE_LOG_MIN_PX || ady >= SWIPE_LOG_MIN_PX) {
      float sx = 0, sy = 0, ex = 0, ey = 0;
      const bool sdkSwipe = in.wasSwipe(sx, sy, ex, ey);
      LOG_INF("TOUCH", "swipe=%s dx=%d dy=%d sdk=%d held=%lums",
              swipeName(freeink::ui::swipeDirection(downX, downY, lastX, lastY)), dx, dy, sdkSwipe ? 1 : 0,
              in.lastTouchHeldMs());
    }
    forceRender = true;
  }

  if (dirty && (forceRender || now - lastRenderRequestMs >= RENDER_INTERVAL_MS)) {
    dirty = false;
    lastRenderRequestMs = now;
    requestUpdate();
  }
}

void TouchTestActivity::render(RenderLock&&) {
  int count = 0;
  char line[sizeof(status)];
  taskENTER_CRITICAL(&dotsLock);
  for (int i = 0; i < dotCount; i++) renderDots[i] = dots[(dotHead + i) % MAX_DOTS];
  count = dotCount;
  memcpy(line, status, sizeof(line));
  taskEXIT_CRITICAL(&dotsLock);
  line[sizeof(line) - 1] = '\0';

  const auto savedOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  renderer.clearScreen();

  // Grid with coordinates every other line, so a dot's position can be read off the glass.
  char label[8];
  for (int x = GRID_STEP; x < w; x += GRID_STEP) {
    for (int y = 0; y < h; y += 4) renderer.drawPixel(x, y);
    if (x % (2 * GRID_STEP) == 0) {
      snprintf(label, sizeof(label), "%d", x);
      renderer.drawText(SMALL_FONT_ID, x + 2, TARGET_INSET + 10, label);
    }
  }
  for (int y = GRID_STEP; y < h; y += GRID_STEP) {
    for (int x = 0; x < w; x += 4) renderer.drawPixel(x, y);
    if (y % (2 * GRID_STEP) == 0) {
      snprintf(label, sizeof(label), "%d", y);
      renderer.drawText(SMALL_FONT_ID, 2, y + 2, label);
    }
  }
  renderer.drawRect(0, 0, w, h);

  // Corner targets + labels, in the frame the user reads the screen in.
  const int lh = renderer.getLineHeight(UI_10_FONT_ID);
  const int right = w - 1 - TARGET_INSET;
  const int bottom = h - 1 - TARGET_INSET;
  drawTarget(renderer, TARGET_INSET, TARGET_INSET);
  drawTarget(renderer, right, TARGET_INSET);
  drawTarget(renderer, TARGET_INSET, bottom);
  drawTarget(renderer, right, bottom);
  drawTarget(renderer, w / 2, h / 2);
  renderer.drawText(UI_10_FONT_ID, TARGET_INSET + 10, TARGET_INSET - lh / 2, "TL", true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, right - 10 - renderer.getTextWidth(UI_10_FONT_ID, "TR", EpdFontFamily::BOLD),
                    TARGET_INSET - lh / 2, "TR", true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, TARGET_INSET + 10, bottom - lh / 2, "BL", true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, right - 10 - renderer.getTextWidth(UI_10_FONT_ID, "BR", EpdFontFamily::BOLD),
                    bottom - lh / 2, "BR", true, EpdFontFamily::BOLD);

  const int smallLh = renderer.getLineHeight(SMALL_FONT_ID);
  const auto hint = renderer.wrappedText(SMALL_FONT_ID, tr(STR_TOUCH_TEST_HINT), w - 2 * TARGET_INSET, 3);
  int y = h / 2 + TARGET_ARM + 6;
  for (const auto& l : hint) {
    renderer.drawCenteredText(SMALL_FONT_ID, y, l.c_str());
    y += smallLh;
  }
  if (line[0] != '\0') renderer.drawCenteredText(SMALL_FONT_ID, bottom - TARGET_ARM - smallLh - 4, line);

  // Trail: dots, joined within a stroke.
  for (int i = 0; i < count; i++) {
    const Dot& d = renderDots[i];
    renderer.fillRect(d.x - DOT_HALF, d.y - DOT_HALF, 2 * DOT_HALF + 1, 2 * DOT_HALF + 1);
    if (i > 0 && !d.strokeStart) renderer.drawLine(renderDots[i - 1].x, renderDots[i - 1].y, d.x, d.y);
  }

  renderer.displayBuffer(firstRender ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstRender = false;
  renderer.setOrientation(savedOrientation);
}

void openTouchTest(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (strcmp(activityManager.currentActivityName(), "TouchTest") == 0) return;
  auto test = makeUniqueNoThrow<TouchTestActivity>(renderer, mappedInput);
  if (!test) {
    LOG_ERR("TOUCH", "OOM: touch test activity");
    return;
  }
  activityManager.pushActivity(std::move(test));
}

}  // namespace deckpoint
