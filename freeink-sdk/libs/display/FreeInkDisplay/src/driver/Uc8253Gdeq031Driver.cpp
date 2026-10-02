#include "Uc8253Gdeq031Driver.h"

#include <BoardConfig.h>

namespace freeink {
namespace {
// UC8253 command set (see hardware/UC8253.pdf in the LilyGo T-Deck-Pro repo).
constexpr uint8_t CMD_PANEL_SETTING = 0x00;
constexpr uint8_t CMD_POWER_OFF = 0x02;
constexpr uint8_t CMD_POWER_ON = 0x04;
constexpr uint8_t CMD_DEEP_SLEEP = 0x07;
constexpr uint8_t CMD_DTM1 = 0x10;  // "old" plane
constexpr uint8_t CMD_DISPLAY_REFRESH = 0x12;
constexpr uint8_t CMD_DTM2 = 0x13;  // "new" plane
constexpr uint8_t CMD_VCOM_DATA_INTERVAL = 0x50;
constexpr uint8_t CMD_PARTIAL_WINDOW = 0x90;
constexpr uint8_t CMD_PARTIAL_IN = 0x91;
constexpr uint8_t CMD_PARTIAL_OUT = 0x92;
constexpr uint8_t CMD_CASCADE_SETTING = 0xE0;
constexpr uint8_t CMD_FORCE_TEMPERATURE = 0xE5;

// Controller-native (portrait) geometry. The facade's framebuffer is the
// transposed landscape frame (FB_W x FB_H); writePlane() rotates into RAM.
constexpr uint16_t PANEL_W = 240;
constexpr uint16_t PANEL_H = 320;
constexpr uint16_t PANEL_WB = PANEL_W / 8;  // 30
constexpr uint16_t FB_W = PANEL_H;          // 320
constexpr uint16_t FB_H = PANEL_W;          // 240
constexpr uint16_t FB_WB = FB_W / 8;        // 40
constexpr uint32_t PLANE_BYTES = static_cast<uint32_t>(PANEL_WB) * PANEL_H;
static_assert(static_cast<uint32_t>(FB_WB) * FB_H == PLANE_BYTES, "framebuffer and panel RAM must match in size");

// Values from GxEPD2_310_GDEQ031T10 (_InitDisplay / _Update_Full / _Update_Part).
constexpr uint8_t PSR_SOFT_RESET[] = {0x1E, 0x0D};
constexpr uint8_t PSR_BW_OTP[] = {0x1F, 0x0D};  // KW mode, B/W, OTP LUT
constexpr uint8_t TSFIX = 0x02;
constexpr uint8_t TEMP_FULL = 0x5A;     // fast full waveform (~1.0 s)
constexpr uint8_t TEMP_PARTIAL = 0x79;  // fast partial waveform (~0.7 s)
constexpr uint8_t CDI_FULL = 0x97;
constexpr uint8_t CDI_PARTIAL = 0xD7;   // border floating during partial updates

constexpr uint32_t DEFAULT_SPI_HZ = 4000000;  // what GxEPD2 and Meshtastic run this panel at
}  // namespace

uint32_t Uc8253Gdeq031Driver::spiHz() const {
  return BoardConfig::ACTIVE.displaySpiHz != 0 ? BoardConfig::ACTIVE.displaySpiHz : DEFAULT_SPI_HZ;
}

PanelGeometry Uc8253Gdeq031Driver::geometry() const { return {FB_W, FB_H, FB_WB, PLANE_BYTES}; }

int8_t Uc8253Gdeq031Driver::spiMiso() const { return BoardConfig::ACTIVE.sd.miso; }

int8_t Uc8253Gdeq031Driver::coCs() const { return BoardConfig::ACTIVE.sd.cs; }

void Uc8253Gdeq031Driver::softInit(EpdBus& bus) {
  bus.cmdData(CMD_PANEL_SETTING, PSR_SOFT_RESET, sizeof(PSR_SOFT_RESET));
  delay(1);
  bus.cmdData(CMD_PANEL_SETTING, PSR_BW_OTP, sizeof(PSR_BW_OTP));
  // Soft reset drops the analog domain; the next refresh must power on again.
  _powerOn = false;
}

void Uc8253Gdeq031Driver::setFullWindow(EpdBus& bus) {
  // Inclusive byte-aligned window: x 0..239, y 0..319, scan inside window only.
  const uint8_t win[] = {0x00, static_cast<uint8_t>((PANEL_W - 1) | 0x07), 0x00, 0x00,
                         static_cast<uint8_t>((PANEL_H - 1) >> 8), static_cast<uint8_t>((PANEL_H - 1) & 0xFF), 0x01};
  bus.cmdData(CMD_PARTIAL_WINDOW, win, sizeof(win));
}

void Uc8253Gdeq031Driver::writePlane(EpdBus& bus, uint8_t command, const uint8_t* fb) {
  bus.cmd(CMD_PARTIAL_IN);
  setFullWindow(bus);
  bus.cmd(command);
  bus.beginTxn();
  // Transpose the landscape framebuffer into portrait controller RAM, one
  // controller row at a time. Controller pixel (cx, cy) takes framebuffer
  // pixel (x = cy, y = FB_H - 1 - cx) — the same mapping as the Murphy M3.
  uint8_t row[PANEL_WB];
  for (uint16_t cy = 0; cy < PANEL_H; cy++) {
    const uint16_t srcX = cy;
    const uint8_t srcMask = static_cast<uint8_t>(0x80 >> (srcX & 7));
    const uint8_t* srcCol = fb + (srcX >> 3);
    for (uint16_t b = 0; b < PANEL_WB; b++) {
      uint8_t out = 0;
      for (uint8_t bit = 0; bit < 8; bit++) {
        const uint16_t cx = static_cast<uint16_t>(b * 8 + bit);
        const uint16_t srcY = static_cast<uint16_t>(FB_H - 1 - cx);
        if (srcCol[static_cast<uint32_t>(srcY) * FB_WB] & srcMask) out |= static_cast<uint8_t>(0x80 >> bit);
      }
      row[b] = out;
    }
    bus.rawWriteBytes(row, PANEL_WB);
  }
  bus.endTxn();
  bus.cmd(CMD_PARTIAL_OUT);
}

void Uc8253Gdeq031Driver::fillPlane(EpdBus& bus, uint8_t command, uint8_t value) {
  bus.fillPlane(command, value, PANEL_H, PANEL_WB);
}

void Uc8253Gdeq031Driver::powerOn(EpdBus& bus) {
  if (_powerOn) return;
  bus.cmd(CMD_POWER_ON);
  bus.waitBusy(" GDEQ_PON");
  _powerOn = true;
}

void Uc8253Gdeq031Driver::powerOff(EpdBus& bus) {
  if (!_powerOn) return;
  bus.cmd(CMD_POWER_OFF);
  bus.waitBusy(" GDEQ_POF");
  _powerOn = false;
}

void Uc8253Gdeq031Driver::begin(EpdBus& bus) {
  // No-op pulse on v1.0 (no RESET line); a real reset on v1.1 (GPIO16).
  bus.reset();
  softInit(bus);
  // Unknown glass and RAM state: start both planes white so the first (full)
  // refresh has a sane baseline, like GxEPD2's initial writeScreenBuffer().
  fillPlane(bus, CMD_DTM1, 0xFF);
  fillPlane(bus, CMD_DTM2, 0xFF);
  _needsFullRefresh = true;
}

void Uc8253Gdeq031Driver::display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode,
                                  bool turnOff) {
  // GxEPD2 re-runs the soft init before every RAM write after a refresh
  // ("needed, reason unknown"); the controller keeps its RAM across it.
  softInit(bus);

  const bool full = _needsFullRefresh || mode != RefreshMode::Fast;
  if (full) {
    writePlane(bus, CMD_DTM1, fb);
    writePlane(bus, CMD_DTM2, fb);
  } else {
    // DTM1 already holds the on-glass frame (written after the previous
    // refresh); a dual-buffer host can still hand us its own baseline.
    if (prev != nullptr) writePlane(bus, CMD_DTM1, prev);
    writePlane(bus, CMD_DTM2, fb);
  }

  bus.cmd(CMD_CASCADE_SETTING);
  bus.data(TSFIX);
  bus.cmd(CMD_FORCE_TEMPERATURE);
  bus.data(full ? TEMP_FULL : TEMP_PARTIAL);
  bus.cmd(CMD_VCOM_DATA_INTERVAL);
  bus.data(full ? CDI_FULL : CDI_PARTIAL);

  powerOn(bus);
  if (!full) {
    bus.cmd(CMD_PARTIAL_IN);
    setFullWindow(bus);
  }
  bus.cmd(CMD_DISPLAY_REFRESH);
  bus.waitBusy(full ? " GDEQ_FULL" : " GDEQ_FAST");
  if (!full) bus.cmd(CMD_PARTIAL_OUT);

  // Seed the "old" plane with what is now on glass for the next fast diff.
  softInit(bus);
  writePlane(bus, CMD_DTM1, fb);
  _needsFullRefresh = false;

  if (turnOff) powerOff(bus);
}

void Uc8253Gdeq031Driver::displayGray(EpdBus& bus, const uint8_t* fb, const bool turnOff, const unsigned char* lut,
                                      const bool factoryMode) {
  (void)fb;
  (void)lut;
  (void)factoryMode;
  if (turnOff) powerOff(bus);
}

void Uc8253Gdeq031Driver::deepSleep(EpdBus& bus) {
  powerOff(bus);
  // Without a RESET line the controller cannot leave deep sleep, so a v1.0
  // board only powers the analog domain down (the panel holds its image
  // regardless). v1.1 (RESET on GPIO16) can sleep properly.
  if (BoardConfig::ACTIVE.display.rst >= 0) {
    bus.cmd(CMD_DEEP_SLEEP);
    bus.data(0xA5);
    _needsFullRefresh = true;  // RAM is lost across deep sleep
  }
}

PanelDriver& uc8253Gdeq031Driver() {
  static Uc8253Gdeq031Driver instance;
  return instance;
}

}  // namespace freeink
