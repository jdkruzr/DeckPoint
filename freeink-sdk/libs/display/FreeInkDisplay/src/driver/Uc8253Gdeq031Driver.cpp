#include "Uc8253Gdeq031Driver.h"

#include <BoardConfig.h>
#include <Gdeq031Tuning.h>

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
constexpr uint8_t CMD_LUT_VCOM = 0x20;
constexpr uint8_t CMD_LUT_WW = 0x21;  // {NEW,OLD}=11: dark gray mask
constexpr uint8_t CMD_LUT_KW = 0x22;  // {NEW,OLD}=10: light gray mask
constexpr uint8_t CMD_LUT_WK = 0x23;  // {NEW,OLD}=01: unused
constexpr uint8_t CMD_LUT_KK = 0x24;  // {NEW,OLD}=00: untouched
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

// Grayscale pass: register LUTs (PSR REG=1, KW mode) with the stock CDI.
constexpr uint8_t PSR_BW_REG_LUT[] = {0x3F, 0x0D};
constexpr uint8_t CDI_GRAY = 0x97;  // DDX=01: {NEW,OLD} selects WW/KW/WK/KK
constexpr uint8_t LUT_LEN = 42;      // 6 groups x 7 bytes (KW mode)
constexpr uint8_t LUT_LEN_VCOM = 56;  // 8 groups x 7 bytes
// UC8253 level select: 00 0V/VCOM_DC, 01 VSH (toward black on this film),
// 10 VSL (toward white), 11 float/VDHR. Polarity matches the Murphy UC8253
// tables (black->white uses VSL). A full black->white DU drive is ~15 frames.
constexpr uint8_t LVL_0V = 0x00;
constexpr uint8_t LVL_VSL = 0x80;

// Calibrated on a v1.1 unit (see plan): with repeats the drive multiplies, so
// 8/4 frames punched holes in thin glyph strokes; 3/1 better; 4/0 (dark gray
// stays black, faint edges whitened) gave the cleanest text; 2/1 gives a clean
// solid mid-gray on images AND reads well as text — the shipping default.
uint8_t s_lightFrames = 2;
uint8_t s_darkFrames = 1;
uint8_t s_repeat = 1;

// One group: {group repeat, phase1 level|frames, phase2 (1 frame settle),
// phase3, phase4, state1 repeat, state2 repeat} — the Murphy DU group shape.
// The repeat knob is the GROUP repeat (0 = run once). State repeats stay at 1:
// the controller ends the whole LUT at the first state whose repeat count is 0
// (UC8253 datasheet, LUT termination rules), so 0 there means "no drive".
void writeGroup(uint8_t* lut, uint8_t level, uint8_t frames) {
  lut[0] = s_repeat;
  lut[1] = static_cast<uint8_t>(level | (frames & 0x3F));
  lut[2] = 0x01;
  lut[3] = 0x00;
  lut[4] = 0x00;
  lut[5] = 0x01;
  lut[6] = 0x01;
}
}  // namespace

void gdeq031SetGrayFrames(uint8_t lightFrames, uint8_t darkFrames) {
  s_lightFrames = lightFrames > 63 ? 63 : lightFrames;
  s_darkFrames = darkFrames > 63 ? 63 : darkFrames;
}

void gdeq031GetGrayFrames(uint8_t& lightFrames, uint8_t& darkFrames) {
  lightFrames = s_lightFrames;
  darkFrames = s_darkFrames;
}

void gdeq031SetGrayRepeat(uint8_t repeat) { s_repeat = repeat; }

uint8_t gdeq031GrayRepeat() { return s_repeat; }

uint32_t Uc8253Gdeq031Driver::spiHz() const {
  return BoardConfig::ACTIVE.displaySpiHz != 0 ? BoardConfig::ACTIVE.displaySpiHz : DEFAULT_SPI_HZ;
}

PanelGeometry Uc8253Gdeq031Driver::geometry() const { return {FB_W, FB_H, FB_WB, PLANE_BYTES}; }

int8_t Uc8253Gdeq031Driver::spiMiso() const { return BoardConfig::ACTIVE.sd.miso; }

int8_t Uc8253Gdeq031Driver::coCs() const { return BoardConfig::ACTIVE.sd.cs; }

void Uc8253Gdeq031Driver::softInit(EpdBus& bus) {
  bus.cmdData(CMD_PANEL_SETTING, PSR_SOFT_RESET, sizeof(PSR_SOFT_RESET));
  delay(1);
  // Writes sent while the controller is still resetting are dropped; a lost PSR leaves the
  // default scan direction and the next frame lands rotated 180 degrees.
  uint8_t waitedMs = 0;
  while (bus.isBusy() && waitedMs < 50) {
    delay(1);
    waitedMs++;
  }
  if (waitedMs > 0 && Serial) Serial.printf("[GDEQ] soft reset busy for %u ms\n", waitedMs);
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

void Uc8253Gdeq031Driver::copyGrayscaleLsb(EpdBus& bus, const uint8_t* lsb) {
  if (!lsb) {
    _grayLsbValid = false;
    return;
  }
  writePlane(bus, CMD_DTM1, lsb);  // LSB mask -> OLD
  _grayLsbValid = true;
}

void Uc8253Gdeq031Driver::copyGrayscaleMsb(EpdBus& bus, const uint8_t* msb) {
  if (!msb || !_grayLsbValid) return;
  writePlane(bus, CMD_DTM2, msb);  // MSB mask -> NEW
}

void Uc8253Gdeq031Driver::loadGrayLuts(EpdBus& bus) {
  const uint8_t settle = s_lightFrames > s_darkFrames ? s_lightFrames : s_darkFrames;
  uint8_t vcom[LUT_LEN_VCOM] = {};
  uint8_t idle[LUT_LEN] = {};
  uint8_t dark[LUT_LEN] = {};
  uint8_t light[LUT_LEN] = {};
  writeGroup(vcom, LVL_0V, settle);
  writeGroup(idle, LVL_0V, settle);
  writeGroup(dark, LVL_VSL, s_darkFrames);
  writeGroup(light, LVL_VSL, s_lightFrames);
  bus.cmdData(CMD_LUT_VCOM, vcom, sizeof(vcom));
  bus.cmdData(CMD_LUT_WW, dark, sizeof(dark));
  bus.cmdData(CMD_LUT_KW, light, sizeof(light));
  bus.cmdData(CMD_LUT_WK, idle, sizeof(idle));
  bus.cmdData(CMD_LUT_KK, idle, sizeof(idle));
}

void Uc8253Gdeq031Driver::displayGray(EpdBus& bus, const uint8_t* fb, const bool turnOff, const unsigned char* lut,
                                      const bool factoryMode) {
  (void)fb;
  (void)lut;
  (void)factoryMode;
  if (!_grayLsbValid) {
    if (turnOff) powerOff(bus);
    return;
  }
  // Same RAM, register LUTs: no soft reset here (it would not clear RAM, but
  // the panel setting below fully replaces the OTP selection anyway).
  bus.cmdData(CMD_PANEL_SETTING, PSR_BW_REG_LUT, sizeof(PSR_BW_REG_LUT));
  bus.cmd(CMD_VCOM_DATA_INTERVAL);
  bus.data(CDI_GRAY);
  loadGrayLuts(bus);
  powerOn(bus);
  bus.cmd(CMD_PARTIAL_IN);
  setFullWindow(bus);
  bus.cmd(CMD_DISPLAY_REFRESH);
  bus.waitBusy(" GDEQ_GRAY");
  bus.cmd(CMD_PARTIAL_OUT);
  _grayLsbValid = false;
  if (turnOff) powerOff(bus);
}

void Uc8253Gdeq031Driver::cleanupGrayscaleBuffers(EpdBus& bus, const uint8_t* bw) {
  _grayLsbValid = false;
  // Back to the OTP waveforms, and DTM1 = the B/W frame for the next fast diff.
  // Gray pixels read as "black" there; a later page that whitens them runs the
  // normal black->white transition, and the periodic full refresh scrubs any
  // residue.
  softInit(bus);
  if (bw) writePlane(bus, CMD_DTM1, bw);
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
