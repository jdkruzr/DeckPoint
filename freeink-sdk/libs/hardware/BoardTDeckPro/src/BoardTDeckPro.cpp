#include "BoardTDeckPro.h"

#include <BoardConfig.h>
#include <InputManager.h>
#include <SPI.h>
#include <Tca8418.h>
#include <Wire.h>

using freeink::KeyEvent;
using freeink::SpecialKey;
namespace KeyMod = freeink::KeyMod;

namespace BoardTDeckPro {
namespace {

// --- pins (LilyGo utilities.h, checked against the v1.0 schematic) -----------
constexpr int8_t PIN_I2C_SDA = 13;
constexpr int8_t PIN_I2C_SCL = 14;
constexpr int8_t PIN_KB_INT = 15;
constexpr int8_t PIN_KB_LED = 42;
constexpr int8_t PIN_LORA_CS = 3;
constexpr int8_t PIN_LORA_EN = 46;    // load switch for LORA_VDD (whole radio supply)
constexpr int8_t PIN_LORA_BUSY = 6;
constexpr int8_t PIN_SPI_SCK = 36;
constexpr int8_t PIN_SPI_MISO = 47;
constexpr int8_t PIN_SPI_MOSI = 33;
constexpr int8_t PIN_GPS_EN = 39;
constexpr int8_t PIN_MODEM_EN = 41;
constexpr int8_t PIN_MOTOR = 2;          // v1.0 vibration motor / v1.1 DRV2605 enable
constexpr int8_t PIN_GYRO_1V8_EN = 38;   // v1.0 only (v1.1 reuses 38 as touch RST)
constexpr int8_t PIN_EPD_RST_V11 = 16;

constexpr uint8_t ADDR_TCA8418 = 0x34;
constexpr uint8_t ADDR_DRV2605 = 0x5A;  // v1.1 only
constexpr uint32_t I2C_HZ = 400000;

constexpr uint8_t KB_ROWS = 4;
constexpr uint8_t KB_COLS = 10;

Revision s_revision = Revision::Unknown;
bool s_loraAsleep = false;
freeink::Tca8418 s_kb;
bool s_backlight = false;

// --- keymap --------------------------------------------------------------------
// Indexed by TCA8418 code - 1 (= row*10 + hwcol). Hardware columns run
// right-to-left relative to the printed layout, so row 0 reads p..q. Physical
// layout, left to right:
//   q w e r t y u i o p
//   a s d f g h j k l ⌫
//   Alt z x c v b n m $ ⏎
//   ⇧ 🎤 Space Sym ⇧
// The Sym column is what is printed on the keycaps.
enum class KeyRole : uint8_t { Char, Backspace, Enter, Space, Escape, Shift, Sym, Alt };
struct KeyDef {
  KeyRole role;
  char base, shifted, sym;
};
constexpr KeyDef C(char b, char s, char y) { return {KeyRole::Char, b, s, y}; }
constexpr KeyDef R(KeyRole r, char y = 0) { return {r, 0, 0, y}; }

constexpr KeyDef KEYMAP[35] = {
    // row 0 (codes 1..10): p o i u y t r e w q
    C('p', 'P', '@'), C('o', 'O', '+'), C('i', 'I', '-'), C('u', 'U', '_'), C('y', 'Y', ')'),
    C('t', 'T', '('), C('r', 'R', '3'), C('e', 'E', '2'), C('w', 'W', '1'), C('q', 'Q', '#'),
    // row 1 (11..20): ⌫ l k j h g f d s a
    R(KeyRole::Backspace), C('l', 'L', '"'), C('k', 'K', '\''), C('j', 'J', ';'), C('h', 'H', ':'),
    C('g', 'G', '/'), C('f', 'F', '6'), C('d', 'D', '5'), C('s', 'S', '4'), C('a', 'A', '*'),
    // row 2 (21..30): ⏎ $ m n b v c x z Alt
    R(KeyRole::Enter), C('$', '$', '$'), C('m', 'M', '.'), C('n', 'N', ','), C('b', 'B', '!'),
    C('v', 'V', '?'), C('c', 'C', '9'), C('x', 'X', '8'), C('z', 'Z', '7'), R(KeyRole::Alt),
    // row 3 (31..35): R⇧ Sym Space 🎤 L⇧ — the mic key is Esc (Sym: '0')
    R(KeyRole::Shift), R(KeyRole::Sym), R(KeyRole::Space), R(KeyRole::Escape, '0'), R(KeyRole::Shift),
};

const KeyDef* lookup(uint8_t code) {
  if (code == 0 || code > sizeof(KEYMAP) / sizeof(KEYMAP[0])) return nullptr;
  return &KEYMAP[code - 1];
}

// --- modifiers -------------------------------------------------------------------
// Each modifier works two ways: chorded (held while another key is pressed) or
// sticky (tapped alone: one-shot for the next key; tapped twice: locked; tapped
// once more: off).
struct Modifier {
  uint8_t heldCount = 0;  // two Shift keys can be down at once
  bool usedWhileHeld = false;
  bool latched = false;
  bool locked = false;

  bool active() const { return heldCount > 0 || latched || locked; }
  void press() {
    heldCount++;
    usedWhileHeld = false;
  }
  void release() {
    if (heldCount > 0) heldCount--;
    if (heldCount > 0 || usedWhileHeld) return;
    if (locked) {
      locked = false;
    } else if (latched) {
      latched = false;
      locked = true;
    } else {
      latched = true;
    }
  }
  void consume() {
    if (heldCount > 0) usedWhileHeld = true;
    latched = false;
  }
};
Modifier s_shift, s_sym, s_alt;

Modifier* modifierFor(KeyRole role) {
  switch (role) {
    case KeyRole::Shift:
      return &s_shift;
    case KeyRole::Sym:
      return &s_sym;
    case KeyRole::Alt:
      return &s_alt;
    default:
      return nullptr;
  }
}

// --- event queue -------------------------------------------------------------------
constexpr uint8_t QUEUE_LEN = 32;
KeyEvent s_queue[QUEUE_LEN];
uint8_t s_qHead = 0, s_qCount = 0;

void enqueue(const KeyEvent& e) {
  if (s_qCount == QUEUE_LEN) {  // drop oldest
    s_qHead = (s_qHead + 1) % QUEUE_LEN;
    s_qCount--;
  }
  s_queue[(s_qHead + s_qCount) % QUEUE_LEN] = e;
  s_qCount++;
}

// --- button bridge -------------------------------------------------------------------
// Keys held down that map to buttons, and taps still being reported. A tap is
// kept asserted until the hook has shown it for MIN_VISIBLE_MS of polling:
// InputManager only accepts a state that is stable past its debounce window,
// so a press and release landing between two polls would otherwise vanish.
constexpr unsigned long MIN_VISIBLE_MS = 30;
bool s_raw = false;
uint8_t s_heldButtons = 0;
uint8_t s_pulseButtons = 0;
unsigned long s_pulseFirstSeen[8] = {};
uint8_t s_buttonForCode[36] = {};  // code -> (1 << BTN_*) or 0, captured at press time

uint8_t buttonMaskFor(const KeyDef& def) {
  // Only plain (unmodified) keys navigate; modified keys are text/commands.
  if (s_shift.active() || s_sym.active() || s_alt.active()) return 0;
  switch (def.role) {
    case KeyRole::Enter:
      return 1U << InputManager::BTN_CONFIRM;
    case KeyRole::Backspace:
    case KeyRole::Escape:
      return 1U << InputManager::BTN_BACK;
    case KeyRole::Space:
      return 1U << InputManager::BTN_RIGHT;
    case KeyRole::Char:
      switch (def.base) {
        case 'h':
          return 1U << InputManager::BTN_LEFT;
        case 'j':
          return 1U << InputManager::BTN_DOWN;
        case 'k':
          return 1U << InputManager::BTN_UP;
        case 'l':
          return 1U << InputManager::BTN_RIGHT;
        default:
          return 0;
      }
    default:
      return 0;
  }
}

unsigned long s_lastKeyMs = 0;

void handlePress(uint8_t code) {
  const KeyDef* def = lookup(code);
  if (def == nullptr) return;
  s_lastKeyMs = millis();

  if (Modifier* m = modifierFor(def->role)) {
    m->press();
    return;
  }

  // Bridge first: it reads the modifier state before this key consumes it.
  if (!s_raw) {
    const uint8_t mask = buttonMaskFor(*def);
    s_buttonForCode[code] = mask;
    s_heldButtons |= mask;
    s_pulseButtons |= mask;
  }

  KeyEvent e;
  e.keycode = code;
  e.mods = static_cast<uint8_t>((s_shift.active() ? KeyMod::Shift : 0) | (s_alt.active() ? KeyMod::Alt : 0));
  const bool sym = s_sym.active();
  switch (def->role) {
    case KeyRole::Char:
      e.ch = sym ? def->sym : (s_shift.active() ? def->shifted : def->base);
      break;
    case KeyRole::Space:
      e.ch = ' ';
      break;
    case KeyRole::Enter:
      e.special = SpecialKey::Enter;
      break;
    case KeyRole::Backspace:
      e.special = s_shift.active() ? SpecialKey::Delete : SpecialKey::Backspace;
      break;
    case KeyRole::Escape:
      if (sym && def->sym) {
        e.ch = def->sym;
      } else {
        e.special = SpecialKey::Escape;
      }
      break;
    default:
      break;
  }
  // Shift on a symbol is meaningless; don't report it as a modifier.
  if (sym) e.mods &= static_cast<uint8_t>(~KeyMod::Shift);
  enqueue(e);
#ifdef DECKPOINT_KEY_DEBUG
  if (Serial) {
    Serial.printf("[%lu] [KEY] code=%u ch=%c special=%u mods=0x%02x bridge=0x%02x\n", millis(), code,
                  e.ch ? e.ch : '-', static_cast<unsigned>(e.special), e.mods, s_raw ? 0 : s_buttonForCode[code]);
  }
#endif

  // Shift lock behaves as caps lock: it stays on; one-shots are spent.
  s_shift.consume();
  s_sym.consume();
  s_alt.consume();
}

void handleRelease(uint8_t code) {
  const KeyDef* def = lookup(code);
  if (def == nullptr) return;
  if (Modifier* m = modifierFor(def->role)) {
    m->release();
    return;
  }
  if (code < sizeof(s_buttonForCode)) {
    s_heldButtons &= static_cast<uint8_t>(~s_buttonForCode[code]);
    s_buttonForCode[code] = 0;
  }
}

void serviceKeyboard() {
  if (!s_kb.present()) return;
  // KEY_INT is active-low while the FIFO holds events; skip the bus otherwise.
  if (digitalRead(PIN_KB_INT) == HIGH) return;
  freeink::Tca8418::RawEvent ev;
  for (uint8_t i = 0; i < 16 && s_kb.readEvent(ev); i++) {
    if (ev.pressed) {
      handlePress(ev.code);
    } else {
      handleRelease(ev.code);
    }
  }
  s_kb.clearInterrupts();
}

uint8_t buttonHook() {
  serviceKeyboard();
  const unsigned long now = millis();
  for (uint8_t b = 0; b < 8; b++) {
    const uint8_t bit = static_cast<uint8_t>(1U << b);
    if (!(s_pulseButtons & bit)) continue;
    if (s_pulseFirstSeen[b] == 0) {
      s_pulseFirstSeen[b] = now ? now : 1;
    } else if (now - s_pulseFirstSeen[b] >= MIN_VISIBLE_MS) {
      s_pulseButtons &= static_cast<uint8_t>(~bit);
      s_pulseFirstSeen[b] = 0;
    }
  }
  return s_raw ? 0 : static_cast<uint8_t>(s_heldButtons | s_pulseButtons);
}

void outputLow(int8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void outputHigh(int8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
}

// Put the SX1262 into cold-start sleep (~160 nA). The radio stays powered:
// LORA_EN switches its whole supply, and an unpowered radio would be
// back-powered through the shared SPI lines. A falling edge on its NSS wakes
// it, so its CS must stay parked high afterwards (it is never touched again).
bool sleepLoRa() {
  const unsigned long start = millis();
  pinMode(PIN_LORA_BUSY, INPUT);
  while (digitalRead(PIN_LORA_BUSY) == HIGH) {  // POR + calibration after power-up
    if (millis() - start > 100) return false;
    delay(1);
  }
  // Same pins the SD card and panel use later; a repeat SPI.begin() keeps them.
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_LORA_CS, LOW);
  SPI.transfer(0x84);  // SetSleep
  SPI.transfer(0x00);  // cold start, RTC timeout disabled
  digitalWrite(PIN_LORA_CS, HIGH);
  SPI.endTransaction();
  delay(1);
  return true;
}

bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

}  // namespace

void begin() {
  // Shared SPI bus: deselect everyone before the first transaction. The SX1262
  // stays powered (an unpowered radio would load the shared lines) but is put
  // to sleep until DeckPoint has a use for it.
  outputHigh(PIN_LORA_EN);
  outputHigh(PIN_LORA_CS);
  outputHigh(BoardConfig::ACTIVE.sd.cs);
  outputHigh(BoardConfig::ACTIVE.display.cs);
  delay(5);  // LORA_VDD ramp
  s_loraAsleep = sleepLoRa();

  // Rails we don't use yet: GPS, LTE modem, motor, keyboard backlight.
  outputLow(PIN_GPS_EN);
  outputLow(PIN_MODEM_EN);
  outputLow(PIN_MOTOR);
  outputLow(PIN_KB_LED);
  s_backlight = false;

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_HZ);

  s_revision = probe(ADDR_DRV2605) ? Revision::V1_1 : Revision::V1_0;
  if (s_revision == Revision::V1_1) {
    BoardConfig::ACTIVE.display.rst = PIN_EPD_RST_V11;
  } else {
    outputLow(PIN_GYRO_1V8_EN);  // BHI260AP unused; keep its 1.8 V rail down
  }

  pinMode(PIN_KB_INT, INPUT_PULLUP);
  if (s_kb.begin(Wire, ADDR_TCA8418, KB_ROWS, KB_COLS)) {
    InputManager::setButtonHook(buttonHook);
  }

  logStatus();
}

bool loraAsleep() { return s_loraAsleep; }

void logStatus() {
  if (!Serial) return;
  Serial.printf("[%lu] [TDECK] revision %s, keyboard %s, LoRa %s, kb backlight %s\n", millis(), revisionName(),
                s_kb.present() ? "ok" : "MISSING", s_loraAsleep ? "asleep" : "BUSY stuck (left in standby)",
                s_backlight ? "on" : "off");
}

Revision revision() { return s_revision; }

const char* revisionName() {
  switch (s_revision) {
    case Revision::V1_0:
      return "v1.0";
    case Revision::V1_1:
      return "v1.1";
    default:
      return "unknown";
  }
}

bool keyboardPresent() { return s_kb.present(); }

void injectKey(uint8_t code, bool pressed) {
  if (pressed) {
    handlePress(code);
  } else {
    handleRelease(code);
  }
}

unsigned long lastKeyActivityMs() { return s_lastKeyMs; }

bool popKey(KeyEvent& out) {
  serviceKeyboard();
  if (s_qCount == 0) return false;
  out = s_queue[s_qHead];
  s_qHead = (s_qHead + 1) % QUEUE_LEN;
  s_qCount--;
  return true;
}

void flushKeys() {
  serviceKeyboard();
  s_qHead = 0;
  s_qCount = 0;
}

void setRawKeyMode(bool raw) {
  if (raw == s_raw) return;
  s_raw = raw;
  // Drop any bridge state so a key held across the switch can't stick.
  s_heldButtons = 0;
  s_pulseButtons = 0;
  for (auto& t : s_pulseFirstSeen) t = 0;
  for (auto& m : s_buttonForCode) m = 0;
}

bool rawKeyMode() { return s_raw; }

ModifierState modifiers() {
  return {s_shift.active(), s_shift.locked, s_sym.active(), s_sym.locked, s_alt.active(), s_alt.locked};
}

void setKeyboardBacklight(bool on) {
  s_backlight = on;
  digitalWrite(PIN_KB_LED, on ? HIGH : LOW);
}

bool keyboardBacklight() { return s_backlight; }

void toggleKeyboardBacklight() { setKeyboardBacklight(!s_backlight); }

uint64_t keyboardWakeMask() { return s_kb.present() ? (1ULL << PIN_KB_INT) : 0; }

void prepareForSleep() {
  setKeyboardBacklight(false);
  if (s_kb.present()) s_kb.flush();
}

}  // namespace BoardTDeckPro
