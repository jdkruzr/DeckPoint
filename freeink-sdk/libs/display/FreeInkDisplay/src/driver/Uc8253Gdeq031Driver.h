#pragma once

// DECKPOINT: UC8253 panel driver — GoodDisplay GDEQ031T10 (3.1", 240x320 B/W),
// as fitted to the LilyGo T-Deck Pro.
//
// Distinct from the other UC8253 drivers:
//   * Runs the panel's OTP waveforms (no host LUT upload). Full and fast
//     refreshes are selected the way GxEPD2's GxEPD2_310_GDEQ031T10 does it:
//     cascade setting TSFIX + a forced temperature that picks a faster OTP
//     waveform (0x5A full ~1.0 s, 0x79 partial ~0.7 s).
//   * v1.0 boards have no RESET line, so the controller is (re)initialised with
//     the PANEL SETTING soft reset, never a hardware pulse. That also means deep
//     sleep (0x07) is only entered when a RESET pin exists — without one the
//     controller could never be woken again.
//   * The panel scans natively as 240x320 portrait; the facade's framebuffer
//     is landscape 320x240 and planes are transposed on write (as Murphy M3).
//
// Refresh model (single-buffer friendly): the controller's DTM1 plane holds the
// frame currently on glass. A fast refresh writes the new frame to DTM2 and
// diffs against DTM1; afterwards the new frame is copied into DTM1 so the next
// diff has the right baseline. A full refresh writes the new frame to both.
//
// Selection: linked only when FREEINK_DRIVER_UC8253_GDEQ031 (T-Deck Pro env).

#include "PanelDriver.h"

namespace freeink {

class Uc8253Gdeq031Driver : public PanelDriver {
 public:
  uint32_t spiHz() const override;
  BusyPolarity busyPolarity() const override { return BusyPolarity::ActiveLow; }  // UC8253 BUSY_N low = busy
  PanelGeometry geometry() const override;
  // MISO is shared with the SD card on this bus; claim it so whichever of the
  // two calls SPI.begin() first leaves the bus fully wired.
  int8_t spiMiso() const override;
  int8_t coCs() const override;

  void begin(EpdBus& bus) override;
  void deepSleep(EpdBus& bus) override;
  void display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;

 private:
  void softInit(EpdBus& bus);
  void writePlane(EpdBus& bus, uint8_t command, const uint8_t* fb);
  void fillPlane(EpdBus& bus, uint8_t command, uint8_t value);
  void setFullWindow(EpdBus& bus);
  void powerOn(EpdBus& bus);
  void powerOff(EpdBus& bus);

  bool _powerOn = false;
  bool _needsFullRefresh = true;  // first refresh after begin() must be full (unknown glass state)
};

PanelDriver& uc8253Gdeq031Driver();

}  // namespace freeink
