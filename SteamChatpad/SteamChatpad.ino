/*
  Steam Chatpad
  =============

  Turns an original Xbox 360 Chatpad into a Bluetooth LE HID keyboard using an
  Arduino Nano ESP32. The firmware handles Chatpad serial input, UK keyboard
  mapping, modifier layers, three remembered BLE hosts, an on-device settings
  menu, backlight/LED control, battery reporting, and low-power sleep.

  Target hardware
  ---------------
  - Arduino Nano ESP32 (ESP32-S3)
  - Original Xbox 360 Chatpad
  - Regulated 3.3 V supply shared by the Nano and Chatpad

  Required libraries
  ------------------
  - HijelHID / HijelHID_BLEKeyboard
  - NimBLE-Arduino

  Wiring
  ------
  Chatpad TX  -> Nano D9  (GPIO18, UART RX)
  Chatpad RX  -> Nano D8  (GPIO17, UART TX)
  Chatpad 3V3 -> regulated 3.3 V
  Chatpad GND -> common ground

  Isolated Green button
  ---------------------
  The Green modifier switch is electrically isolated from the Chatpad matrix:
  one switch contact goes to Nano D2 and the other to GND. D2 uses INPUT_PULLUP,
  so the button is HIGH when released and LOW when pressed. It also acts as the
  wake input while the ESP32 is sleeping.

  Optional supply monitor
  -----------------------
  3.3 V rail -> 47 kOhm -> A0 -> 47 kOhm -> GND
  Add 100 nF from A0 to GND. The ADC measures the regulated 3.3 V rail rather
  than the raw battery, so the voltage thresholds below should be calibrated for
  the regulator and battery arrangement used in the finished build.

  Notes
  -----
  - Chatpad UART: 19,200 baud, 8N1.
  - The key map targets an English (United Kingdom) host keyboard layout.
  - The Chatpad hardware limits one continuous backlight-on period to roughly
    six seconds. The firmware does not repeatedly retrigger it, avoiding a
    visible periodic blink.
  - Green + Orange hold shortcuts provide host selection, settings, and sleep.
*/

#include <Arduino.h>
#include <iterator>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <HijelHID_BLEKeyboard.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp32-hal-cpu.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
// Hardware pins, timeouts, BLE behaviour and voltage thresholds are kept here
// so the build can be adapted without touching the application logic.
namespace Config {

constexpr bool DEBUG_LOG = false;  // USB Serial packet/debug output.
constexpr bool ENABLE_SUPPLY_MONITOR = true;  // Compile ADC supply monitoring support.
constexpr uint8_t BATTERY_PLACEHOLDER_PERCENT = 100;

constexpr uint32_t CHATPAD_BAUD = 19200;  // Xbox 360 Chatpad UART speed.
constexpr uint8_t CHATPAD_RX_PIN = D9;  // Chatpad TX -> Nano RX.
constexpr uint8_t CHATPAD_TX_PIN = D8;  // Chatpad RX -> Nano TX.
constexpr uint8_t GREEN_BUTTON_PIN = D2;  // Isolated Green switch -> GND when pressed.

constexpr uint8_t SUPPLY_SENSE_PIN = A0;  // Midpoint of the optional voltage divider.
constexpr float SUPPLY_DIVIDER_RATIO = 2.0f;  // 47 kOhm / 47 kOhm divider.
constexpr float SUPPLY_CALIBRATION = 1.000f;  // Adjust against a multimeter if required.

constexpr uint32_t KEEPALIVE_MS = 1000;
constexpr uint32_t BACKLIGHT_HARDWARE_ON_MS = 6000;
constexpr uint32_t BATTERY_SAMPLE_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t BATTERY_REPORT_MS = 5UL * 60UL * 1000UL;

constexpr uint32_t SETTINGS_IDLE_MS = 30UL * 1000UL;
constexpr uint32_t NUMERIC_AUTO_ACCEPT_MS = 3000;
constexpr uint32_t ERASE_ALL_CONFIRM_MS = 3000;
constexpr uint16_t SETTINGS_LED_ON_MS = 100;
constexpr uint16_t SETTINGS_LED_GAP_MS = 100;
constexpr uint16_t SETTINGS_LED_SECTION_GAP_MS = 300;
constexpr uint16_t SETTINGS_LED_CYCLE_GAP_MS = 700;
constexpr uint16_t SETTINGS_CONFIRM_PROMPT_MS = 1000;
constexpr uint16_t SETTINGS_CONFIRM_FLASH_MS = 750;
constexpr uint16_t SETTINGS_CONFIRM_FLASH_GAP_MS = 150;

constexpr uint8_t BACKLIGHT_OVERRIDE_NONE = 0xFF;
constexpr uint16_t SLEEP_OVERRIDE_NONE = 0xFFFF;
constexpr uint8_t BACKLIGHT_OVERRIDE_MAX_SECONDS = 30;
constexpr uint16_t SLEEP_OVERRIDE_MAX_SECONDS = 120;

constexpr float LOW_VOLTAGE = 3.12f;
constexpr float LOW_RESET_VOLTAGE = 3.16f;
constexpr float CRITICAL_VOLTAGE = 3.07f;

constexpr float BATTERY_100_V = 3.290f;
constexpr float BATTERY_75_V  = 3.255f;
constexpr float BATTERY_50_V  = 3.220f;
constexpr float BATTERY_25_V  = 3.180f;
constexpr float BATTERY_10_V  = 3.120f;
constexpr float BATTERY_5_V   = 3.070f;

constexpr uint32_t LOW_CONFIRM_MS = 60000;
constexpr uint32_t CRITICAL_CONFIRM_MS = 5000;

constexpr uint32_t REPEAT_DELAY_MS = 500;
constexpr uint32_t REPEAT_RATE_MS = 65;

constexpr uint8_t CPU_MHZ = 80;  // Active CPU frequency.

constexpr uint8_t BLE_TX_POWER_LOW = 2;
constexpr uint8_t BLE_TX_POWER_MEDIUM = 5;
constexpr uint8_t BLE_TX_POWER_HIGH = 8;

constexpr uint32_t BLE_SLOW_ADVERTISING_AFTER_MS = 15000;
constexpr uint16_t BLE_ADV_FAST_MIN = 160;
constexpr uint16_t BLE_ADV_FAST_MAX = 240;
constexpr uint16_t BLE_ADV_SLOW_MIN = 1600;
constexpr uint16_t BLE_ADV_SLOW_MAX = 1920;
constexpr uint16_t HID_TAP_MS = 25;
constexpr uint16_t HID_GAP_MS = 20;
constexpr uint16_t PAIRING_LED_STEP_MS = 100;
constexpr uint16_t MODIFIER_TAP_MAX_MS = 250;
constexpr uint16_t MODIFIER_DOUBLE_CLICK_MS = 500;
constexpr uint16_t MODIFIER_LATCH_FLASH_MS = 100;

constexpr uint8_t HOST_SLOT_COUNT = 3;  // Remembered BLE hosts.

constexpr uint32_t SLOT_SELECT_HOLD_MS = 3000;  // Green+Orange: saved-host selection.
constexpr uint32_t SETTINGS_HOLD_MS = 5000;  // Green+Orange: settings menu.
constexpr uint32_t SLOT_PAIR_HOLD_MS = SETTINGS_HOLD_MS;
constexpr uint32_t SOFT_OFF_HOLD_MS = 7000;  // Green+Orange: manual sleep.

constexpr uint32_t SLOT_SELECTION_TIMEOUT_MS = 10000;

constexpr uint16_t SLOT_FLASH_ON_MS = 100;
constexpr uint16_t SLOT_FLASH_BETWEEN_MS = 100;
constexpr uint16_t SLOT_FLASH_REPEAT_GAP_MS = 500;

}

// -----------------------------------------------------------------------------
// Xbox 360 Chatpad protocol constants
// -----------------------------------------------------------------------------
namespace ChatpadProtocol {

constexpr uint8_t HEADER_0 = 0x87;
constexpr uint8_t HEADER_1 = 0x02;
constexpr uint8_t HEADER_2 = 0x8C;

constexpr uint8_t CMD_PEOPLE_LED_OFF = 0x03;
constexpr uint8_t CMD_BACKLIGHT_OFF = 0x04;
constexpr uint8_t CMD_PEOPLE_LED_ON = 0x0B;
constexpr uint8_t CMD_BACKLIGHT_ON = 0x0C;
constexpr uint8_t CMD_LED_STATE_BASE = 0x10;
constexpr uint8_t CMD_KEEPALIVE = 0x1B;
constexpr uint8_t CMD_INITIALISE = 0x1F;

constexpr uint8_t MOD_SHIFT = 0x01;
constexpr uint8_t MOD_GREEN = 0x02;
constexpr uint8_t MOD_ORANGE = 0x04;
constexpr uint8_t MOD_PEOPLE = 0x08;

}

HardwareSerial& chatpadSerial = Serial1;
HijelHID_BLEKeyboard keyboard("steam chatpad", "Steam Controller", 100);

// -----------------------------------------------------------------------------
// Keyboard mapping and application state
// -----------------------------------------------------------------------------
enum class Accent : uint8_t { None, Circumflex, Tilde, Diaeresis, Grave };
enum class ActionKind : uint8_t { Character, AltCode, Accent };
enum class BatteryState : uint8_t { Normal, Low, Critical };

enum class PowerMode : uint8_t { Normal = 1, PowerSaver = 2, SuperPowerSaver = 3 };
enum class BluetoothPowerMode : uint8_t { Low = 1, Medium = 2, High = 3 };
enum class ReconnectMode : uint8_t { Eco = 1, Balanced = 2, Fast = 3 };

enum class SettingsPage : uint8_t {
  Inactive,
  Main,
  Bluetooth,
  BluetoothPairSlot,
  BluetoothForget,
  BluetoothTxPower,
  BluetoothReconnect,
  Power,
  PowerModeSelect,
  PowerBacklightTime,
  PowerSleepTime,
  PowerResetOverrides,
  Battery,
  BatteryReporting,
  BatteryReportNow,
};

enum class SlotMode : uint8_t { Normal, SelectSaved, SelectPair, PairingNew };

struct BondSlot {
  bool valid = false;
  NimBLEAddress address;
};

struct BaseKey {
  uint8_t scan;
  uint8_t hid;
  char letter;
};

struct LayerAction {
  uint8_t scan;
  ActionKind kind;
  uint16_t value;
};

constexpr BaseKey BASE_KEYS[] = {
    {0x17, KEY_1, 0}, {0x16, KEY_2, 0}, {0x15, KEY_3, 0},
    {0x14, KEY_4, 0}, {0x13, KEY_5, 0}, {0x12, KEY_6, 0},
    {0x11, KEY_7, 0}, {0x67, KEY_8, 0}, {0x66, KEY_9, 0},
    {0x65, KEY_0, 0},

    {0x27, KEY_Q, 'q'}, {0x26, KEY_W, 'w'}, {0x25, KEY_E, 'e'},
    {0x24, KEY_R, 'r'}, {0x23, KEY_T, 't'}, {0x22, KEY_Y, 'y'},
    {0x21, KEY_U, 'u'}, {0x76, KEY_I, 'i'}, {0x75, KEY_O, 'o'},
    {0x64, KEY_P, 'p'},

    {0x37, KEY_A, 'a'}, {0x36, KEY_S, 's'}, {0x35, KEY_D, 'd'},
    {0x34, KEY_F, 'f'}, {0x33, KEY_G, 'g'}, {0x32, KEY_H, 'h'},
    {0x31, KEY_J, 'j'}, {0x77, KEY_K, 'k'}, {0x72, KEY_L, 'l'},
    {0x62, KEY_COMMA, 0},

    {0x46, KEY_Z, 'z'}, {0x45, KEY_X, 'x'}, {0x44, KEY_C, 'c'},
    {0x43, KEY_V, 'v'}, {0x42, KEY_B, 'b'}, {0x41, KEY_N, 'n'},
    {0x52, KEY_M, 'm'}, {0x53, KEY_DOT, 0},

    {0x63, KEY_RETURN, 0}, {0x54, KEY_SPACE, 0},
    {0x71, KEY_BACKSPACE, 0}, {0x55, KEY_LEFT, 0},
    {0x51, KEY_RIGHT, 0},
};

#define CHAR_ACTION(scanCode, character) \
  {scanCode, ActionKind::Character, static_cast<uint16_t>(character)}
#define ALT_ACTION(scanCode, altCode) \
  {scanCode, ActionKind::AltCode, altCode}
#define ACCENT_ACTION(scanCode, accentType) \
  {scanCode, ActionKind::Accent, static_cast<uint16_t>(Accent::accentType)}

constexpr LayerAction GREEN_LAYER[] = {
    CHAR_ACTION(0x27, '!'), CHAR_ACTION(0x26, '@'), ALT_ACTION(0x25, 128),
    CHAR_ACTION(0x24, '#'), CHAR_ACTION(0x23, '%'), ACCENT_ACTION(0x22, Circumflex),
    CHAR_ACTION(0x21, '&'), CHAR_ACTION(0x76, '*'), CHAR_ACTION(0x75, '('),
    CHAR_ACTION(0x64, ')'),

    ACCENT_ACTION(0x37, Tilde), ALT_ACTION(0x36, 154), CHAR_ACTION(0x35, '{'),
    CHAR_ACTION(0x34, '}'), ACCENT_ACTION(0x33, Diaeresis), CHAR_ACTION(0x32, '/'),
    CHAR_ACTION(0x31, '\''), CHAR_ACTION(0x77, '['), CHAR_ACTION(0x72, ']'),
    CHAR_ACTION(0x62, ':'),

    ACCENT_ACTION(0x46, Grave), ALT_ACTION(0x45, 171), ALT_ACTION(0x44, 187),
    CHAR_ACTION(0x43, '-'), CHAR_ACTION(0x42, '|'), CHAR_ACTION(0x41, '<'),
    CHAR_ACTION(0x52, '>'), CHAR_ACTION(0x53, '?'),
};

constexpr LayerAction ORANGE_LAYER[] = {
    ALT_ACTION(0x27, 161), ALT_ACTION(0x26, 229), ALT_ACTION(0x25, 233),
    CHAR_ACTION(0x24, '$'), ALT_ACTION(0x23, 254), ALT_ACTION(0x22, 253),
    ALT_ACTION(0x21, 250), ALT_ACTION(0x76, 237), ALT_ACTION(0x75, 243),
    CHAR_ACTION(0x64, '='),

    ALT_ACTION(0x37, 225), ALT_ACTION(0x36, 223), ALT_ACTION(0x35, 240),
    ALT_ACTION(0x34, 163), ALT_ACTION(0x33, 165), CHAR_ACTION(0x32, '\\'),
    CHAR_ACTION(0x31, '"'), ALT_ACTION(0x77, 169), ALT_ACTION(0x72, 248),
    CHAR_ACTION(0x62, ';'),

    ALT_ACTION(0x46, 230), ALT_ACTION(0x45, 156), ALT_ACTION(0x44, 231),
    CHAR_ACTION(0x43, '_'), CHAR_ACTION(0x42, '+'), ALT_ACTION(0x41, 241),
    ALT_ACTION(0x52, 181), ALT_ACTION(0x53, 191),
};

#undef CHAR_ACTION
#undef ALT_ACTION
#undef ACCENT_ACTION

// -----------------------------------------------------------------------------
// Runtime state
// -----------------------------------------------------------------------------
uint8_t packet[8] = {};
uint8_t packetLength = 0;
uint8_t modifiers = 0;
uint8_t previousModifiers = 0;
uint8_t key1 = 0;
uint8_t key2 = 0;
uint8_t previousKey1 = 0;
uint8_t previousKey2 = 0;
uint8_t chatpadReportedModifiers = 0;
bool greenButtonDown = false;
uint8_t repeatScan = 0;
uint8_t lastLedState = 0xFF;

Accent pendingAccent = Accent::None;
BatteryState batteryState = BatteryState::Normal;
float latestSupplyVoltage = Config::BATTERY_100_V;
uint32_t lastBatteryReportAt = 0;

uint8_t latchedModifiers = 0;
uint8_t oneShotModifiers = 0;
uint8_t suppressedModifiers = 0;
uint8_t modifierUsedWhileHeld = 0;
uint8_t modifierIgnoreRelease = 0;
uint32_t modifierPressedAt[3] = {0, 0, 0};
uint32_t modifierLastTapAt[3] = {0, 0, 0};

bool backlightOn = true;
bool peopleLedOn = false;
bool bleWasPaired = false;
bool capsChordHeld = false;
bool slotChordHeld = false;
bool pairingShowGreen = true;
bool slowAdvertisingActive = false;
uint32_t bleDisconnectedAt = 0;

Preferences slotPreferences;

BondSlot hostSlots[Config::HOST_SLOT_COUNT];

uint8_t activeSlot = 0;
uint8_t pendingPairSlot = 0;

SlotMode slotMode = SlotMode::Normal;

PowerMode powerMode = PowerMode::PowerSaver;
BluetoothPowerMode bluetoothPowerMode = BluetoothPowerMode::Low;
ReconnectMode reconnectMode = ReconnectMode::Balanced;
uint8_t backlightOverrideSeconds = Config::BACKLIGHT_OVERRIDE_NONE;
uint16_t sleepOverrideSeconds = Config::SLEEP_OVERRIDE_NONE;
bool batteryReportingEnabled = false;

SettingsPage settingsPage = SettingsPage::Inactive;
uint8_t settingsPendingSelection = 0;
uint16_t settingsNumericValue = 0;
uint8_t settingsNumericDigits = 0;
bool settingsEraseAllCompleted = false;
uint32_t settingsLastInputAt = 0;
uint32_t settingsPageEnteredAt = 0;
uint32_t settingsNumericLastDigitAt = 0;
uint32_t settingsGreenPressedAt = 0;
uint32_t settingsConfirmationPromptUntil = 0;

// Shut down BLE and Chatpad activity, configure wake sources, then sleep.
void enterLowPowerSleep();
void publishBatteryLevel();
void sampleAndPublishBatteryNow();
void restartAdvertisingAtPowerRate(bool slow);

uint32_t lastActivityAt = 0;
uint32_t lastKeepaliveAt = 0;
uint32_t lastBacklightOnCommandAt = 0;
uint32_t lastBatterySampleAt = 0;
uint32_t repeatAt = 0;
uint32_t lowVoltageSince = 0;
uint32_t criticalVoltageSince = 0;
uint32_t pairingLedAt = 0;
uint32_t slotChordSince = 0;
uint32_t slotModeEnteredAt = 0;

// -----------------------------------------------------------------------------
// Chatpad command and indicator helpers
// -----------------------------------------------------------------------------
void debugPacket(const uint8_t* data) {
  if (!Config::DEBUG_LOG) return;

  Serial.print("Chatpad:");
  for (uint8_t i = 0; i < 8; ++i) Serial.printf(" %02X", data[i]);
  Serial.println();
}

void buildChatpadCommand(uint8_t command, uint8_t* message) {
  message[0] = ChatpadProtocol::HEADER_0;
  message[1] = ChatpadProtocol::HEADER_1;
  message[2] = ChatpadProtocol::HEADER_2;
  message[3] = command;
  message[4] = 0;

  uint8_t sum = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    sum = static_cast<uint8_t>(sum + message[i]);
  }

  message[4] = static_cast<uint8_t>(0U - sum);
}

void sendChatpadCommand(uint8_t command) {
  uint8_t message[5];
  buildChatpadCommand(command, message);

  chatpadSerial.write(message, sizeof(message));
  chatpadSerial.flush();
}

// Change the Chatpad backlight only when its logical state changes.
void setBacklight(bool enabled) {
  if (enabled == backlightOn) return;

  sendChatpadCommand(enabled ? ChatpadProtocol::CMD_BACKLIGHT_ON
                             : ChatpadProtocol::CMD_BACKLIGHT_OFF);

  backlightOn = enabled;
  if (enabled) lastBacklightOnCommandAt = millis();
}

// Mirror the Chatpad controller's own backlight timeout without sending another
// ON command. Re-triggering the lamp periodically causes a visible blink.
void followBacklightHardwareTimeout(uint32_t now) {
  if (backlightOn &&
      now - lastBacklightOnCommandAt >= Config::BACKLIGHT_HARDWARE_ON_MS) {
    backlightOn = false;
  }
}

void setPeopleLed(bool enabled) {
  if (enabled == peopleLedOn) return;
  sendChatpadCommand(enabled ? ChatpadProtocol::CMD_PEOPLE_LED_ON
                             : ChatpadProtocol::CMD_PEOPLE_LED_OFF);
  peopleLedOn = enabled;
}

bool settingsActive() {
  return settingsPage != SettingsPage::Inactive;
}

uint8_t profileBacklightSeconds(PowerMode mode) {
  switch (mode) {
    case PowerMode::Normal: return 10;
    case PowerMode::PowerSaver: return 6;
    case PowerMode::SuperPowerSaver: return 3;
  }
  return 6;
}

uint16_t profileSleepSeconds(PowerMode mode) {
  switch (mode) {
    case PowerMode::Normal: return 60;
    case PowerMode::PowerSaver: return 25;
    case PowerMode::SuperPowerSaver: return 12;
  }
  return 25;
}

uint32_t effectiveBacklightIdleMs() {
  const uint8_t seconds =
      backlightOverrideSeconds == Config::BACKLIGHT_OVERRIDE_NONE
          ? profileBacklightSeconds(powerMode)
          : backlightOverrideSeconds;
  return static_cast<uint32_t>(seconds) * 1000UL;
}

uint32_t effectiveSleepIdleMs() {
  const uint16_t seconds =
      sleepOverrideSeconds == Config::SLEEP_OVERRIDE_NONE
          ? profileSleepSeconds(powerMode)
          : sleepOverrideSeconds;
  return static_cast<uint32_t>(seconds) * 1000UL;
}

uint8_t configuredBluetoothTxPowerLevel() {
  switch (bluetoothPowerMode) {
    case BluetoothPowerMode::Low: return Config::BLE_TX_POWER_LOW;
    case BluetoothPowerMode::Medium: return Config::BLE_TX_POWER_MEDIUM;
    case BluetoothPowerMode::High: return Config::BLE_TX_POWER_HIGH;
  }
  return Config::BLE_TX_POWER_LOW;
}

void flashColourLed(uint8_t ledBit, uint8_t flashes) {
  for (uint8_t pulse = 0; pulse < flashes; ++pulse) {
    sendChatpadCommand(ChatpadProtocol::CMD_LED_STATE_BASE | ledBit);
    delay(Config::SETTINGS_LED_ON_MS);
    sendChatpadCommand(ChatpadProtocol::CMD_LED_STATE_BASE);
    if (pulse + 1 < flashes) delay(Config::SETTINGS_LED_GAP_MS);
  }
  delay(Config::SETTINGS_LED_SECTION_GAP_MS);
  lastLedState = 0xFF;
}

void flashSettingsConfirmationLed(uint8_t ledBit) {
  sendChatpadCommand(ChatpadProtocol::CMD_LED_STATE_BASE | ledBit);
  delay(Config::SETTINGS_CONFIRM_FLASH_MS);
  sendChatpadCommand(ChatpadProtocol::CMD_LED_STATE_BASE);
  delay(Config::SETTINGS_CONFIRM_FLASH_GAP_MS);
  lastLedState = 0xFF;
}

void markActivity() {
  const uint32_t now = millis();
  lastActivityAt = now;
  followBacklightHardwareTimeout(now);

  if (settingsActive()) {
    setBacklight(true);
    return;
  }

  if (effectiveBacklightIdleMs() == 0) setBacklight(false);
  else setBacklight(true);
}

bool slotFlashState(uint8_t slot, uint32_t elapsedMs) {
  if (slot < 1 || slot > Config::HOST_SLOT_COUNT) return false;

  const uint32_t pulseSectionMs =
      static_cast<uint32_t>(slot - 1) *
          (Config::SLOT_FLASH_ON_MS + Config::SLOT_FLASH_BETWEEN_MS) +
      Config::SLOT_FLASH_ON_MS;

  const uint32_t cycleMs =
      pulseSectionMs + Config::SLOT_FLASH_REPEAT_GAP_MS;

  uint32_t positionMs = elapsedMs % cycleMs;

  for (uint8_t pulse = 0; pulse < slot; ++pulse) {
    const uint32_t pulseStartMs =
        static_cast<uint32_t>(pulse) *
        (Config::SLOT_FLASH_ON_MS + Config::SLOT_FLASH_BETWEEN_MS);

    if (positionMs >= pulseStartMs &&
        positionMs < pulseStartMs + Config::SLOT_FLASH_ON_MS) {
      return true;
    }
  }

  return false;
}

void confirmSlotWithPeopleLed(uint8_t slot) {
  if (slot < 1 || slot > Config::HOST_SLOT_COUNT) return;

  for (uint8_t pulse = 0; pulse < slot; ++pulse) {
    setPeopleLed(true);
    delay(Config::SLOT_FLASH_ON_MS);

    setPeopleLed(false);

    if (pulse + 1 < slot) {
      delay(Config::SLOT_FLASH_BETWEEN_MS);
    }
  }

  delay(Config::SLOT_FLASH_REPEAT_GAP_MS);
}

// -----------------------------------------------------------------------------
// Modifier handling
// -----------------------------------------------------------------------------
int8_t modifierIndex(uint8_t bit) {
  if (bit == ChatpadProtocol::MOD_SHIFT) return 0;
  if (bit == ChatpadProtocol::MOD_GREEN) return 1;
  if (bit == ChatpadProtocol::MOD_ORANGE) return 2;
  return -1;
}

uint8_t effectiveModifiers() {
  return static_cast<uint8_t>(
      (modifiers & ~suppressedModifiers) |
      oneShotModifiers |
      latchedModifiers);
}

void refreshModifierIndicators() {
  lastLedState = 0xFF;
}

void setLatchedModifier(uint8_t bit, bool enabled) {
  if (enabled) {

    oneShotModifiers &= ~bit;

    if (bit == ChatpadProtocol::MOD_GREEN) {
      latchedModifiers &= ~ChatpadProtocol::MOD_ORANGE;
    } else if (bit == ChatpadProtocol::MOD_ORANGE) {
      latchedModifiers &= ~ChatpadProtocol::MOD_GREEN;
    }

    latchedModifiers |= bit;

    if (bit == ChatpadProtocol::MOD_SHIFT && keyboard.isPaired()) {
      keyboard.press(KEY_LSHIFT);
    }
  } else {
    latchedModifiers &= ~bit;

    if (bit == ChatpadProtocol::MOD_SHIFT && keyboard.isPaired()) {
      keyboard.release(KEY_LSHIFT);
    }
  }

  refreshModifierIndicators();
}

void armOneShotModifier(uint8_t bit) {

  if (bit == ChatpadProtocol::MOD_GREEN) {
    oneShotModifiers &= ~ChatpadProtocol::MOD_ORANGE;
  } else if (bit == ChatpadProtocol::MOD_ORANGE) {
    oneShotModifiers &= ~ChatpadProtocol::MOD_GREEN;
  }

  oneShotModifiers |= bit;
  refreshModifierIndicators();
}

void consumeOneShotModifiers() {
  const uint8_t consumed = oneShotModifiers;
  if (consumed == 0) return;

  oneShotModifiers = 0;

  const uint8_t bits[3] = {
      ChatpadProtocol::MOD_SHIFT,
      ChatpadProtocol::MOD_GREEN,
      ChatpadProtocol::MOD_ORANGE,
  };

  for (uint8_t bit : bits) {
    if ((consumed & bit) == 0) continue;
    const int8_t index = modifierIndex(bit);
    if (index >= 0) modifierLastTapAt[index] = 0;
  }

  refreshModifierIndicators();
}

void markPhysicalModifiersUsedForKey() {
  const uint8_t latchableMask =
      ChatpadProtocol::MOD_SHIFT |
      ChatpadProtocol::MOD_GREEN |
      ChatpadProtocol::MOD_ORANGE;

  modifierUsedWhileHeld |=
      static_cast<uint8_t>(modifiers & latchableMask & ~suppressedModifiers);
}

void clearLatchedModifiers() {
  if ((latchedModifiers & ChatpadProtocol::MOD_SHIFT) != 0 &&
      keyboard.isPaired()) {
    keyboard.release(KEY_LSHIFT);
  }

  latchedModifiers = 0;
  oneShotModifiers = 0;
  suppressedModifiers = 0;
  modifierUsedWhileHeld = 0;
  modifierIgnoreRelease = 0;

  for (uint8_t i = 0; i < 3; ++i) {
    modifierPressedAt[i] = 0;
    modifierLastTapAt[i] = 0;
  }

  refreshModifierIndicators();
}

const char* modifierDebugName(uint8_t bit);

void cancelOtherSoftwareModifiers(uint8_t keepBit) {
  const uint8_t modifierMask =
      ChatpadProtocol::MOD_SHIFT |
      ChatpadProtocol::MOD_GREEN |
      ChatpadProtocol::MOD_ORANGE;

  const uint8_t cancelMask =
      static_cast<uint8_t>(modifierMask & ~keepBit);

  if ((latchedModifiers & ChatpadProtocol::MOD_SHIFT) != 0 &&
      (cancelMask & ChatpadProtocol::MOD_SHIFT) != 0 &&
      keyboard.isPaired()) {
    keyboard.release(KEY_LSHIFT);
  }

  const uint8_t cancelledLatched =
      static_cast<uint8_t>(latchedModifiers & cancelMask);
  const uint8_t cancelledOneShot =
      static_cast<uint8_t>(oneShotModifiers & cancelMask);

  latchedModifiers &= static_cast<uint8_t>(~cancelMask);
  oneShotModifiers &= static_cast<uint8_t>(~cancelMask);

  const uint8_t bits[3] = {
      ChatpadProtocol::MOD_SHIFT,
      ChatpadProtocol::MOD_GREEN,
      ChatpadProtocol::MOD_ORANGE,
  };

  for (uint8_t bit : bits) {
    if ((cancelMask & bit) == 0) continue;
    const int8_t index = modifierIndex(bit);
    if (index >= 0) modifierLastTapAt[index] = 0;
  }

  if (Config::DEBUG_LOG &&
      (cancelledLatched != 0 || cancelledOneShot != 0)) {
    Serial.printf("Other modifier state cancelled by %s press\n",
                  modifierDebugName(keepBit));
  }

  refreshModifierIndicators();
}

const char* modifierDebugName(uint8_t bit) {
  if (bit == ChatpadProtocol::MOD_SHIFT) return "Shift";
  if (bit == ChatpadProtocol::MOD_GREEN) return "Green";
  if (bit == ChatpadProtocol::MOD_ORANGE) return "Orange";
  return "Unknown";
}

void handleModifierPress(uint8_t bit) {
  const int8_t index = modifierIndex(bit);
  if (index < 0) return;

  const uint32_t now = millis();
  modifierPressedAt[index] = now;
  modifierUsedWhileHeld &= ~bit;

  cancelOtherSoftwareModifiers(bit);

  if ((latchedModifiers & bit) != 0) {
    setLatchedModifier(bit, false);
    oneShotModifiers &= ~bit;
    suppressedModifiers |= bit;
    modifierIgnoreRelease |= bit;
    modifierLastTapAt[index] = 0;

    if (Config::DEBUG_LOG) {
      Serial.printf("%s modifier -> LATCH OFF\n", modifierDebugName(bit));
    }

    refreshModifierIndicators();
    return;
  }

  if (modifierLastTapAt[index] != 0 &&
      now - modifierLastTapAt[index] <= Config::MODIFIER_DOUBLE_CLICK_MS &&
      (oneShotModifiers & bit) != 0) {
    oneShotModifiers &= ~bit;
    setLatchedModifier(bit, true);

    modifierIgnoreRelease |= bit;
    modifierLastTapAt[index] = 0;

    if (Config::DEBUG_LOG) {
      Serial.printf("%s modifier -> LATCH ON\n", modifierDebugName(bit));
    }

    return;
  }

  if (Config::DEBUG_LOG) {
    Serial.printf("%s modifier press\n", modifierDebugName(bit));
  }
}

void handleModifierRelease(uint8_t bit) {
  const int8_t index = modifierIndex(bit);
  if (index < 0) return;

  const uint32_t now = millis();

  if ((modifierIgnoreRelease & bit) != 0) {
    modifierIgnoreRelease &= ~bit;
    modifierPressedAt[index] = 0;
    modifierUsedWhileHeld &= ~bit;

    if ((latchedModifiers & bit) == 0) {
      suppressedModifiers &= ~bit;
    }

    refreshModifierIndicators();
    return;
  }

  const uint32_t pressedAt = modifierPressedAt[index];
  const uint32_t heldMs = pressedAt == 0 ? 0 : now - pressedAt;
  modifierPressedAt[index] = 0;

  if ((modifierUsedWhileHeld & bit) != 0) {
    modifierUsedWhileHeld &= ~bit;
    oneShotModifiers &= ~bit;
    modifierLastTapAt[index] = 0;

    if (Config::DEBUG_LOG) {
      Serial.printf("%s modifier release %lu ms -> HELD/USED, OFF\n",
                    modifierDebugName(bit),
                    static_cast<unsigned long>(heldMs));
    }

    refreshModifierIndicators();
    return;
  }

  if (heldMs > Config::MODIFIER_TAP_MAX_MS) {
    oneShotModifiers &= ~bit;
    modifierLastTapAt[index] = 0;

    if (Config::DEBUG_LOG) {
      Serial.printf("%s modifier release %lu ms -> HOLD, OFF\n",
                    modifierDebugName(bit),
                    static_cast<unsigned long>(heldMs));
    }

    refreshModifierIndicators();
    return;
  }

  armOneShotModifier(bit);
  modifierLastTapAt[index] = now;

  if (Config::DEBUG_LOG) {
    Serial.printf("%s modifier release %lu ms -> ONE SHOT\n",
                  modifierDebugName(bit),
                  static_cast<unsigned long>(heldMs));
  }
}

// -----------------------------------------------------------------------------
// Base key lookup and HID output
// -----------------------------------------------------------------------------
const BaseKey* findBaseKey(uint8_t scan) {
  for (const BaseKey& entry : BASE_KEYS) {
    if (entry.scan == scan) return &entry;
  }
  return nullptr;
}

const LayerAction* findLayerAction(const LayerAction* table, size_t count,
                                   uint8_t scan) {
  for (size_t i = 0; i < count; ++i) {
    if (table[i].scan == scan) return &table[i];
  }
  return nullptr;
}

bool isHeld(uint8_t scan) {
  return scan != 0 && (scan == key1 || scan == key2);
}

bool wasHeld(uint8_t scan) {
  return scan != 0 && (scan == previousKey1 || scan == previousKey2);
}

void tapKey(uint8_t hid, uint8_t hidModifiers = 0) {
  if (hid != KEY_NONE && keyboard.isPaired()) keyboard.tap(hid, hidModifiers);
}

bool slotNumberValid(uint8_t slot) {
  return slot >= 1 && slot <= Config::HOST_SLOT_COUNT;
}

uint8_t slotIndex(uint8_t slot) {
  return static_cast<uint8_t>(slot - 1);
}

void slotPreferenceKey(char* output, size_t outputSize, uint8_t slot, char suffix) {
  snprintf(output, outputSize, "s%u%c", static_cast<unsigned>(slot), suffix);
}

void saveHostSlot(uint8_t slot) {
  if (!slotNumberValid(slot)) return;

  const BondSlot& entry = hostSlots[slotIndex(slot)];
  char key[8] = {};

  slotPreferenceKey(key, sizeof(key), slot, 'v');
  slotPreferences.putBool(key, entry.valid);

  slotPreferenceKey(key, sizeof(key), slot, 'a');
  slotPreferences.putString(
      key,
      entry.valid ? String(entry.address.toString().c_str()) : String(""));

  slotPreferenceKey(key, sizeof(key), slot, 't');
  slotPreferences.putUChar(key, entry.valid ? entry.address.getType() : 0);
}

void saveActiveSlot() {
  slotPreferences.putUChar("active", activeSlot);
}

void clearHostSlot(uint8_t slot) {
  if (!slotNumberValid(slot)) return;

  hostSlots[slotIndex(slot)].valid = false;
  hostSlots[slotIndex(slot)].address = NimBLEAddress();
  saveHostSlot(slot);

  if (activeSlot == slot) {
    activeSlot = 0;
    saveActiveSlot();
  }
}

// -----------------------------------------------------------------------------
// Persistent settings and remembered BLE hosts
// -----------------------------------------------------------------------------
void loadHostSlots() {
  slotPreferences.begin("steamchat", false);

  for (uint8_t slot = 1; slot <= Config::HOST_SLOT_COUNT; ++slot) {
    char key[8] = {};

    slotPreferenceKey(key, sizeof(key), slot, 'v');
    const bool valid = slotPreferences.getBool(key, false);

    slotPreferenceKey(key, sizeof(key), slot, 'a');
    const String addressText = slotPreferences.getString(key, "");

    slotPreferenceKey(key, sizeof(key), slot, 't');
    const uint8_t addressType = slotPreferences.getUChar(key, BLE_ADDR_PUBLIC);

    BondSlot& entry = hostSlots[slotIndex(slot)];
    entry.valid = valid && addressText.length() > 0;

    if (entry.valid) {
      entry.address = NimBLEAddress(std::string(addressText.c_str()), addressType);
    }
  }

  activeSlot = slotPreferences.getUChar("active", 0);
  if (!slotNumberValid(activeSlot)) activeSlot = 0;

  uint8_t storedPowerMode = slotPreferences.getUChar(
      "pwrmode", static_cast<uint8_t>(PowerMode::PowerSaver));
  if (storedPowerMode < static_cast<uint8_t>(PowerMode::Normal) ||
      storedPowerMode > static_cast<uint8_t>(PowerMode::SuperPowerSaver)) {
    storedPowerMode = static_cast<uint8_t>(PowerMode::PowerSaver);
  }
  powerMode = static_cast<PowerMode>(storedPowerMode);

  backlightOverrideSeconds = slotPreferences.getUChar(
      "blovr", Config::BACKLIGHT_OVERRIDE_NONE);
  if (backlightOverrideSeconds != Config::BACKLIGHT_OVERRIDE_NONE &&
      backlightOverrideSeconds > Config::BACKLIGHT_OVERRIDE_MAX_SECONDS) {
    backlightOverrideSeconds = Config::BACKLIGHT_OVERRIDE_NONE;
  }

  sleepOverrideSeconds = slotPreferences.getUShort(
      "slpovr", Config::SLEEP_OVERRIDE_NONE);
  if (sleepOverrideSeconds != Config::SLEEP_OVERRIDE_NONE &&
      (sleepOverrideSeconds == 0 ||
       sleepOverrideSeconds > Config::SLEEP_OVERRIDE_MAX_SECONDS)) {
    sleepOverrideSeconds = Config::SLEEP_OVERRIDE_NONE;
  }

  uint8_t storedBluetoothPower = slotPreferences.getUChar(
      "btx", static_cast<uint8_t>(BluetoothPowerMode::Low));
  if (storedBluetoothPower < static_cast<uint8_t>(BluetoothPowerMode::Low) ||
      storedBluetoothPower > static_cast<uint8_t>(BluetoothPowerMode::High)) {
    storedBluetoothPower = static_cast<uint8_t>(BluetoothPowerMode::Low);
  }
  bluetoothPowerMode = static_cast<BluetoothPowerMode>(storedBluetoothPower);

  uint8_t storedReconnectMode = slotPreferences.getUChar(
      "reconn", static_cast<uint8_t>(ReconnectMode::Balanced));
  if (storedReconnectMode < static_cast<uint8_t>(ReconnectMode::Eco) ||
      storedReconnectMode > static_cast<uint8_t>(ReconnectMode::Fast)) {
    storedReconnectMode = static_cast<uint8_t>(ReconnectMode::Balanced);
  }
  reconnectMode = static_cast<ReconnectMode>(storedReconnectMode);

  batteryReportingEnabled = slotPreferences.getBool("batreport", false);
}

void savePowerModeAndClearOverrides(PowerMode mode) {
  const uint8_t stored = static_cast<uint8_t>(mode);
  if (powerMode != mode) slotPreferences.putUChar("pwrmode", stored);
  powerMode = mode;

  if (backlightOverrideSeconds != Config::BACKLIGHT_OVERRIDE_NONE) {
    backlightOverrideSeconds = Config::BACKLIGHT_OVERRIDE_NONE;
    slotPreferences.putUChar("blovr", backlightOverrideSeconds);
  }
  if (sleepOverrideSeconds != Config::SLEEP_OVERRIDE_NONE) {
    sleepOverrideSeconds = Config::SLEEP_OVERRIDE_NONE;
    slotPreferences.putUShort("slpovr", sleepOverrideSeconds);
  }
}

void saveBacklightOverride(uint8_t seconds) {
  if (seconds > Config::BACKLIGHT_OVERRIDE_MAX_SECONDS) {
    seconds = Config::BACKLIGHT_OVERRIDE_MAX_SECONDS;
  }
  if (backlightOverrideSeconds == seconds) return;
  backlightOverrideSeconds = seconds;
  slotPreferences.putUChar("blovr", backlightOverrideSeconds);
}

void saveSleepOverride(uint16_t seconds) {

  if (seconds == 0 || seconds > Config::SLEEP_OVERRIDE_MAX_SECONDS) {
    seconds = Config::SLEEP_OVERRIDE_MAX_SECONDS;
  }
  if (sleepOverrideSeconds == seconds) return;
  sleepOverrideSeconds = seconds;
  slotPreferences.putUShort("slpovr", sleepOverrideSeconds);
}

void clearPowerOverrides() {
  if (backlightOverrideSeconds != Config::BACKLIGHT_OVERRIDE_NONE) {
    backlightOverrideSeconds = Config::BACKLIGHT_OVERRIDE_NONE;
    slotPreferences.putUChar("blovr", backlightOverrideSeconds);
  }
  if (sleepOverrideSeconds != Config::SLEEP_OVERRIDE_NONE) {
    sleepOverrideSeconds = Config::SLEEP_OVERRIDE_NONE;
    slotPreferences.putUShort("slpovr", sleepOverrideSeconds);
  }
}

void saveBluetoothPowerMode(BluetoothPowerMode mode) {
  if (bluetoothPowerMode == mode) return;
  bluetoothPowerMode = mode;
  slotPreferences.putUChar("btx", static_cast<uint8_t>(bluetoothPowerMode));
  keyboard.setTxPower(configuredBluetoothTxPowerLevel());
}

void saveReconnectMode(ReconnectMode mode) {
  if (reconnectMode == mode) return;
  reconnectMode = mode;
  slotPreferences.putUChar("reconn", static_cast<uint8_t>(reconnectMode));
  bleDisconnectedAt = millis();

  if (!keyboard.isPaired() && slotMode == SlotMode::Normal) {
    if (reconnectMode == ReconnectMode::Eco) {
      restartAdvertisingAtPowerRate(true);
    } else {

      restartAdvertisingAtPowerRate(false);
    }
  }
}

void saveBatteryReportingEnabled(bool enabled) {
  if (batteryReportingEnabled != enabled) {
    batteryReportingEnabled = enabled;
    slotPreferences.putBool("batreport", batteryReportingEnabled);
  }
  if (!batteryReportingEnabled) {
    batteryState = BatteryState::Normal;
    lowVoltageSince = 0;
    criticalVoltageSince = 0;
    lastBatterySampleAt = 0;
    lastBatteryReportAt = 0;

    publishBatteryLevel();
  }
}

uint8_t findSlotForAddress(const NimBLEAddress& address) {
  for (uint8_t slot = 1; slot <= Config::HOST_SLOT_COUNT; ++slot) {
    const BondSlot& entry = hostSlots[slotIndex(slot)];
    if (entry.valid && entry.address == address) return slot;
  }
  return 0;
}

bool addressStillBonded(const NimBLEAddress& address) {
  return NimBLEDevice::isBonded(address);
}

// Keep stored host-slot metadata aligned with NimBLE bond records.
void reconcileSlotsWithNimbleBonds() {

  for (uint8_t slot = 1; slot <= Config::HOST_SLOT_COUNT; ++slot) {
    BondSlot& entry = hostSlots[slotIndex(slot)];
    if (entry.valid && !addressStillBonded(entry.address)) {
      clearHostSlot(slot);
    }
  }

  const int bondCount = NimBLEDevice::getNumBonds();

  for (int bond = 0; bond < bondCount; ++bond) {
    const NimBLEAddress address = NimBLEDevice::getBondedAddress(bond);

    if (findSlotForAddress(address) != 0) continue;

    for (uint8_t slot = 1; slot <= Config::HOST_SLOT_COUNT; ++slot) {
      BondSlot& entry = hostSlots[slotIndex(slot)];
      if (entry.valid) continue;

      entry.valid = true;
      entry.address = address;
      saveHostSlot(slot);
      break;
    }
  }

  if (activeSlot == 0 || !hostSlots[slotIndex(activeSlot)].valid) {
    activeSlot = 0;

    for (uint8_t slot = 1; slot <= Config::HOST_SLOT_COUNT; ++slot) {
      if (hostSlots[slotIndex(slot)].valid) {
        activeSlot = slot;
        break;
      }
    }

    saveActiveSlot();
  }
}

// -----------------------------------------------------------------------------
// BLE advertising, pairing and host selection
// -----------------------------------------------------------------------------
void clearBleWhitelist() {
  while (NimBLEDevice::getWhiteListCount() > 0) {
    NimBLEDevice::whiteListRemove(NimBLEDevice::getWhiteListAddress(0));
  }
}

void restartAdvertising(bool whitelistOnly) {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising == nullptr) return;

  advertising->stop();
  advertising->setScanFilter(false, whitelistOnly);

  advertising->setMinInterval(Config::BLE_ADV_FAST_MIN);
  advertising->setMaxInterval(Config::BLE_ADV_FAST_MAX);
  advertising->start();

  slowAdvertisingActive = false;
  bleDisconnectedAt = millis();
}

void restartAdvertisingAtPowerRate(bool slow) {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising == nullptr) return;

  advertising->stop();

  if (slow) {
    advertising->setMinInterval(Config::BLE_ADV_SLOW_MIN);
    advertising->setMaxInterval(Config::BLE_ADV_SLOW_MAX);
  } else {
    advertising->setMinInterval(Config::BLE_ADV_FAST_MIN);
    advertising->setMaxInterval(Config::BLE_ADV_FAST_MAX);
  }

  advertising->start();
  slowAdvertisingActive = slow;

  if (Config::DEBUG_LOG) {
    Serial.printf("BLE advertising -> %s rate\n", slow ? "SLOW" : "FAST");
  }
}

void restrictAdvertisingToSlot(uint8_t slot) {
  if (!slotNumberValid(slot)) return;
  if (!hostSlots[slotIndex(slot)].valid) return;

  clearBleWhitelist();
  NimBLEDevice::whiteListAdd(hostSlots[slotIndex(slot)].address);
  restartAdvertising(true);
}

void openAdvertisingForPairing() {
  clearBleWhitelist();
  restartAdvertising(false);
}

bool getConnectedPeer(NimBLEAddress& address, uint16_t& connectionHandle) {
  NimBLEServer* server = NimBLEDevice::getServer();
  if (server == nullptr || server->getConnectedCount() == 0) return false;

  NimBLEConnInfo info = server->getPeerInfo(static_cast<size_t>(0));
  address = info.getIdAddress();
  connectionHandle = info.getConnHandle();
  return true;
}

void disconnectCurrentPeer() {
  NimBLEServer* server = NimBLEDevice::getServer();
  if (server == nullptr || server->getConnectedCount() == 0) return;

  NimBLEConnInfo info = server->getPeerInfo(static_cast<size_t>(0));
  server->disconnect(info.getConnHandle());
}

uint8_t scanToSlotNumber(uint8_t scan) {
  if (scan == 0x17) return 1;
  if (scan == 0x16) return 2;
  if (scan == 0x15) return 3;
  return 0;
}

void cancelSlotCommand() {
  slotMode = SlotMode::Normal;
  pendingPairSlot = 0;
  slotModeEnteredAt = 0;
  lastLedState = 0xFF;
}

void selectRememberedSlot(uint8_t slot) {
  if (!slotNumberValid(slot)) return;

  confirmSlotWithPeopleLed(slot);

  if (!hostSlots[slotIndex(slot)].valid) {

    cancelSlotCommand();
    return;
  }

  clearLatchedModifiers();
  keyboard.releaseAll();

  activeSlot = slot;
  saveActiveSlot();

  keyboard.end();

  clearBleWhitelist();
  NimBLEDevice::whiteListAdd(hostSlots[slotIndex(slot)].address);

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising != nullptr) advertising->setScanFilter(false, true);

  keyboard.begin();
  restrictAdvertisingToSlot(slot);

  bleWasPaired = false;
  cancelSlotCommand();
}

void beginPairingIntoSlot(uint8_t slot) {
  if (!slotNumberValid(slot)) return;

  confirmSlotWithPeopleLed(slot);

  clearLatchedModifiers();
  keyboard.releaseAll();
  setPeopleLed(false);

  keyboard.end();

  BondSlot& entry = hostSlots[slotIndex(slot)];

  if (entry.valid) {
    NimBLEDevice::deleteBond(entry.address);
    clearHostSlot(slot);
  }

  pendingPairSlot = slot;
  activeSlot = 0;
  saveActiveSlot();

  clearBleWhitelist();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising != nullptr) advertising->setScanFilter(false, false);

  keyboard.begin();
  openAdvertisingForPairing();

  slotMode = SlotMode::PairingNew;
  slotModeEnteredAt = millis();
  pairingShowGreen = true;
  pairingLedAt = millis() + Config::PAIRING_LED_STEP_MS;
  bleWasPaired = false;
  lastLedState = 0xFF;
}

uint8_t firstOccupiedHostSlot() {
  for (uint8_t slot = 1; slot <= Config::HOST_SLOT_COUNT; ++slot) {
    if (hostSlots[slotIndex(slot)].valid) return slot;
  }
  return 0;
}

void startFreshPairingInSlotOne() {
  pendingPairSlot = 1;
  activeSlot = 0;
  saveActiveSlot();
  slotMode = SlotMode::PairingNew;
  slotModeEnteredAt = millis();
  pairingShowGreen = true;
  pairingLedAt = millis() + Config::PAIRING_LED_STEP_MS;
  openAdvertisingForPairing();
  bleWasPaired = false;
  lastLedState = 0xFF;
}

bool forgetRememberedHost(uint8_t slot) {
  if (!slotNumberValid(slot) || !hostSlots[slotIndex(slot)].valid) return false;

  const NimBLEAddress forgottenAddress = hostSlots[slotIndex(slot)].address;
  const bool forgettingActiveHost = activeSlot == slot;

  if (forgettingActiveHost) {
    clearLatchedModifiers();
    keyboard.releaseAll();
    keyboard.end();
  }

  NimBLEDevice::deleteBond(forgottenAddress);
  clearHostSlot(slot);

  if (!forgettingActiveHost) return false;

  activeSlot = firstOccupiedHostSlot();
  saveActiveSlot();
  pendingPairSlot = 0;
  slotMode = SlotMode::Normal;

  keyboard.begin();
  if (slotNumberValid(activeSlot)) {
    restrictAdvertisingToSlot(activeSlot);
    bleWasPaired = false;
    return false;
  }

  startFreshPairingInSlotOne();
  return true;
}

void forgetAllRememberedHosts() {
  clearLatchedModifiers();
  keyboard.releaseAll();
  keyboard.end();
  clearBleWhitelist();
  keyboard.clearBonds();

  for (uint8_t slot = 1; slot <= Config::HOST_SLOT_COUNT; ++slot) {
    clearHostSlot(slot);
  }

  activeSlot = 0;
  saveActiveSlot();
  keyboard.begin();
  startFreshPairingInSlotOne();
}

bool handleSlotSelectionKey(uint8_t scan) {
  const uint8_t slot = scanToSlotNumber(scan);
  if (slot == 0) return false;

  if (slotMode == SlotMode::SelectSaved) {
    selectRememberedSlot(slot);
    return true;
  }

  if (slotMode == SlotMode::SelectPair) {
    beginPairingIntoSlot(slot);
    return true;
  }

  return false;
}

void serviceHostSlots() {
  const uint32_t now = millis();

  if ((slotMode == SlotMode::SelectSaved || slotMode == SlotMode::SelectPair) &&
      now - slotModeEnteredAt >= Config::SLOT_SELECTION_TIMEOUT_MS) {
    cancelSlotCommand();
  }

  if (slotMode != SlotMode::PairingNew) return;

  NimBLEAddress peerAddress;
  uint16_t connectionHandle = 0;

  if (!getConnectedPeer(peerAddress, connectionHandle)) return;

  const uint8_t knownSlot = findSlotForAddress(peerAddress);

  if (knownSlot != 0 && knownSlot != pendingPairSlot) {
    NimBLEServer* server = NimBLEDevice::getServer();
    if (server != nullptr) server->disconnect(connectionHandle);
    return;
  }

  if (!keyboard.isPaired()) return;

  if (!slotNumberValid(pendingPairSlot)) {
    cancelSlotCommand();
    return;
  }

  BondSlot& entry = hostSlots[slotIndex(pendingPairSlot)];
  entry.valid = true;
  entry.address = peerAddress;
  saveHostSlot(pendingPairSlot);

  activeSlot = pendingPairSlot;
  saveActiveSlot();

  clearBleWhitelist();
  NimBLEDevice::whiteListAdd(entry.address);

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising != nullptr) advertising->setScanFilter(false, true);

  pendingPairSlot = 0;
  slotMode = SlotMode::Normal;
  slotModeEnteredAt = 0;
  lastLedState = 0xFF;
}

// -----------------------------------------------------------------------------
// Captive settings menu
// -----------------------------------------------------------------------------
// Settings are controlled entirely from the Chatpad. Menu input is consumed
// locally and is never forwarded to the connected host.
int8_t scanToDigit(uint8_t scan) {
  switch (scan) {
    case 0x65: return 0;
    case 0x17: return 1;
    case 0x16: return 2;
    case 0x15: return 3;
    case 0x14: return 4;
    case 0x13: return 5;
    case 0x12: return 6;
    case 0x11: return 7;
    case 0x67: return 8;
    case 0x66: return 9;
  }
  return -1;
}

void resetSettingsEntryState() {
  settingsPendingSelection = 0;
  settingsNumericValue = 0;
  settingsNumericDigits = 0;
  settingsNumericLastDigitAt = 0;
  settingsGreenPressedAt = 0;
  settingsConfirmationPromptUntil = 0;
  settingsEraseAllCompleted = false;
}

bool settingsChoiceReadyForConfirmation() {
  switch (settingsPage) {
    case SettingsPage::BluetoothPairSlot:
      return slotNumberValid(settingsPendingSelection);

    case SettingsPage::BluetoothForget:
      return settingsPendingSelection >= 1 && settingsPendingSelection <= 4;

    case SettingsPage::BluetoothTxPower:
    case SettingsPage::BluetoothReconnect:
    case SettingsPage::PowerModeSelect:
      return settingsPendingSelection >= 1 && settingsPendingSelection <= 3;

    case SettingsPage::PowerBacklightTime:
    case SettingsPage::PowerSleepTime:
      return settingsNumericDigits > 0;

    case SettingsPage::BatteryReporting:
      return settingsPendingSelection == 1 || settingsPendingSelection == 2;

    case SettingsPage::PowerResetOverrides:
    case SettingsPage::BatteryReportNow:
      return true;

    case SettingsPage::Inactive:
    case SettingsPage::Main:
    case SettingsPage::Bluetooth:
    case SettingsPage::Power:
    case SettingsPage::Battery:
      return false;
  }
  return false;
}

void armSettingsConfirmationPrompt() {
  settingsConfirmationPromptUntil =
      millis() + Config::SETTINGS_CONFIRM_PROMPT_MS;
  lastLedState = 0xFF;
}

void setSettingsPage(SettingsPage page) {
  settingsPage = page;
  resetSettingsEntryState();
  settingsPageEnteredAt = millis();
  settingsLastInputAt = settingsPageEnteredAt;
  setPeopleLed(false);
  lastLedState = 0xFF;

  if (page == SettingsPage::PowerResetOverrides ||
      page == SettingsPage::BatteryReportNow) {
    armSettingsConfirmationPrompt();
  }
}

void noteSettingsInput() {
  settingsLastInputAt = millis();
  markActivity();
}

void showSettingsEntrySummary() {
  setPeopleLed(false);
  sendChatpadCommand(ChatpadProtocol::CMD_LED_STATE_BASE);
  lastLedState = 0;

  if (slotNumberValid(activeSlot)) confirmSlotWithPeopleLed(activeSlot);

  flashColourLed(0x04, static_cast<uint8_t>(powerMode));
}

void enterSettingsMenu() {
  if (slotMode == SlotMode::SelectSaved || slotMode == SlotMode::SelectPair) {
    cancelSlotCommand();
  }
  clearLatchedModifiers();
  keyboard.releaseAll();
  repeatScan = 0;
  pendingAccent = Accent::None;
  capsChordHeld = false;

  setSettingsPage(SettingsPage::Main);
  setBacklight(true);
  showSettingsEntrySummary();
  noteSettingsInput();
}

void exitSettingsMenu() {

  const uint8_t heldNavigationButtons =
      modifiers & (ChatpadProtocol::MOD_GREEN | ChatpadProtocol::MOD_ORANGE);

  clearLatchedModifiers();
  modifierIgnoreRelease |= heldNavigationButtons;
  suppressedModifiers |= heldNavigationButtons;
  keyboard.releaseAll();
  repeatScan = 0;
  pendingAccent = Accent::None;
  capsChordHeld = false;

  settingsPage = SettingsPage::Inactive;
  resetSettingsEntryState();
  setPeopleLed(false);
  lastLedState = 0xFF;
  lastActivityAt = millis();

  if (effectiveBacklightIdleMs() == 0) setBacklight(false);
}

void settingsGoBack() {
  switch (settingsPage) {
    case SettingsPage::Main:
      exitSettingsMenu();
      return;

    case SettingsPage::Bluetooth:
    case SettingsPage::Power:
    case SettingsPage::Battery:
      setSettingsPage(SettingsPage::Main);
      return;

    case SettingsPage::BluetoothPairSlot:
    case SettingsPage::BluetoothForget:
    case SettingsPage::BluetoothTxPower:
    case SettingsPage::BluetoothReconnect:
      setSettingsPage(SettingsPage::Bluetooth);
      return;

    case SettingsPage::PowerModeSelect:
    case SettingsPage::PowerBacklightTime:
    case SettingsPage::PowerSleepTime:
    case SettingsPage::PowerResetOverrides:
      setSettingsPage(SettingsPage::Power);
      return;

    case SettingsPage::BatteryReporting:
    case SettingsPage::BatteryReportNow:
      setSettingsPage(SettingsPage::Battery);
      return;

    case SettingsPage::Inactive:
      return;
  }
}

void confirmNumericSetting() {
  if (settingsNumericDigits == 0) return;

  if (settingsPage == SettingsPage::PowerBacklightTime) {
    uint16_t value = settingsNumericValue;
    if (value > Config::BACKLIGHT_OVERRIDE_MAX_SECONDS) {
      value = Config::BACKLIGHT_OVERRIDE_MAX_SECONDS;
    }
    saveBacklightOverride(static_cast<uint8_t>(value));

    setBacklight(true);
    flashSettingsConfirmationLed(0x02);
    setSettingsPage(SettingsPage::Power);
    return;
  }

  if (settingsPage == SettingsPage::PowerSleepTime) {
    uint16_t value = settingsNumericValue;
    if (value == 0 || value > Config::SLEEP_OVERRIDE_MAX_SECONDS) {
      value = Config::SLEEP_OVERRIDE_MAX_SECONDS;
    }
    saveSleepOverride(value);
    flashSettingsConfirmationLed(0x02);
    setSettingsPage(SettingsPage::Power);
  }
}

void handleSettingsDigit(uint8_t digit) {
  noteSettingsInput();

  switch (settingsPage) {
    case SettingsPage::Main:
      if (digit == 1) setSettingsPage(SettingsPage::Bluetooth);
      else if (digit == 2) setSettingsPage(SettingsPage::Power);
      else if (digit == 3) setSettingsPage(SettingsPage::Battery);
      return;

    case SettingsPage::Bluetooth:
      if (digit == 1) setSettingsPage(SettingsPage::BluetoothPairSlot);
      else if (digit == 2) setSettingsPage(SettingsPage::BluetoothForget);
      else if (digit == 3) setSettingsPage(SettingsPage::BluetoothTxPower);
      else if (digit == 4) setSettingsPage(SettingsPage::BluetoothReconnect);
      return;

    case SettingsPage::BluetoothPairSlot:
      if (digit >= 1 && digit <= 3) {
        settingsPendingSelection = digit;
        confirmSlotWithPeopleLed(digit);
        armSettingsConfirmationPrompt();
      }
      return;

    case SettingsPage::BluetoothForget:
      if (digit >= 1 && digit <= 4) {
        settingsPendingSelection = digit;
        if (digit <= 3) confirmSlotWithPeopleLed(digit);
        armSettingsConfirmationPrompt();
      }
      return;

    case SettingsPage::BluetoothTxPower:
    case SettingsPage::BluetoothReconnect:
    case SettingsPage::PowerModeSelect:
      if (digit >= 1 && digit <= 3) {
        settingsPendingSelection = digit;
        armSettingsConfirmationPrompt();
      }
      return;

    case SettingsPage::Power:
      if (digit == 1) setSettingsPage(SettingsPage::PowerModeSelect);
      else if (digit == 2) setSettingsPage(SettingsPage::PowerBacklightTime);
      else if (digit == 3) setSettingsPage(SettingsPage::PowerSleepTime);
      else if (digit == 4) setSettingsPage(SettingsPage::PowerResetOverrides);
      return;

    case SettingsPage::PowerBacklightTime:
    case SettingsPage::PowerSleepTime: {
      const uint8_t maxDigits =
          settingsPage == SettingsPage::PowerBacklightTime ? 2 : 3;
      if (settingsNumericDigits >= maxDigits) return;
      settingsNumericValue =
          static_cast<uint16_t>(settingsNumericValue * 10U + digit);
      ++settingsNumericDigits;
      settingsNumericLastDigitAt = millis();
      armSettingsConfirmationPrompt();
      return;
    }

    case SettingsPage::Battery:
      if (digit == 1) setSettingsPage(SettingsPage::BatteryReporting);
      else if (digit == 2) setSettingsPage(SettingsPage::BatteryReportNow);
      return;

    case SettingsPage::BatteryReporting:
      if (digit == 1 || digit == 2) {
        settingsPendingSelection = digit;
        armSettingsConfirmationPrompt();
      }
      return;

    case SettingsPage::PowerResetOverrides:
    case SettingsPage::BatteryReportNow:
    case SettingsPage::Inactive:
      return;
  }
}

void handleSettingsBackspace() {
  if (settingsPage != SettingsPage::PowerBacklightTime &&
      settingsPage != SettingsPage::PowerSleepTime) return;
  if (settingsNumericDigits == 0) return;

  noteSettingsInput();
  settingsNumericValue /= 10U;
  --settingsNumericDigits;
  settingsNumericLastDigitAt =
      settingsNumericDigits == 0 ? 0 : millis();
  if (settingsNumericDigits == 0) {
    settingsConfirmationPromptUntil = 0;
    lastLedState = 0xFF;
  } else {
    armSettingsConfirmationPrompt();
  }
}

void acceptSettingsSelection() {
  noteSettingsInput();

  switch (settingsPage) {
    case SettingsPage::BluetoothPairSlot:
      if (slotNumberValid(settingsPendingSelection)) {
        const uint8_t slot = settingsPendingSelection;
        flashSettingsConfirmationLed(0x02);
        exitSettingsMenu();
        beginPairingIntoSlot(slot);
        modifierIgnoreRelease |= ChatpadProtocol::MOD_GREEN;
        suppressedModifiers |= ChatpadProtocol::MOD_GREEN;
      }
      return;

    case SettingsPage::BluetoothForget:
      if (slotNumberValid(settingsPendingSelection)) {
        const uint8_t slot = settingsPendingSelection;
        if (!hostSlots[slotIndex(slot)].valid) {
          flashSettingsConfirmationLed(0x04);
          setSettingsPage(SettingsPage::Bluetooth);
          return;
        }
        const bool openedFreshPairing = forgetRememberedHost(slot);
        flashSettingsConfirmationLed(0x02);
        if (openedFreshPairing) exitSettingsMenu();
        else setSettingsPage(SettingsPage::Bluetooth);
      }

      return;

    case SettingsPage::BluetoothTxPower:
      if (settingsPendingSelection >= 1 && settingsPendingSelection <= 3) {
        saveBluetoothPowerMode(
            static_cast<BluetoothPowerMode>(settingsPendingSelection));
        flashSettingsConfirmationLed(0x02);
        setSettingsPage(SettingsPage::Bluetooth);
      }
      return;

    case SettingsPage::BluetoothReconnect:
      if (settingsPendingSelection >= 1 && settingsPendingSelection <= 3) {
        saveReconnectMode(static_cast<ReconnectMode>(settingsPendingSelection));
        flashSettingsConfirmationLed(0x02);
        setSettingsPage(SettingsPage::Bluetooth);
      }
      return;

    case SettingsPage::PowerModeSelect:
      if (settingsPendingSelection >= 1 && settingsPendingSelection <= 3) {

        const PowerMode selectedMode =
            settingsPendingSelection == 1
                ? PowerMode::SuperPowerSaver
                : (settingsPendingSelection == 2
                       ? PowerMode::PowerSaver
                       : PowerMode::Normal);
        savePowerModeAndClearOverrides(selectedMode);
        markActivity();
        flashSettingsConfirmationLed(0x02);
        setSettingsPage(SettingsPage::Power);
      }
      return;

    case SettingsPage::PowerBacklightTime:
    case SettingsPage::PowerSleepTime:
      confirmNumericSetting();
      return;

    case SettingsPage::PowerResetOverrides:
      clearPowerOverrides();
      markActivity();
      flashSettingsConfirmationLed(0x02);
      setSettingsPage(SettingsPage::Power);
      return;

    case SettingsPage::BatteryReporting:
      if (settingsPendingSelection == 1 || settingsPendingSelection == 2) {
        const bool enabled = settingsPendingSelection == 2;
        saveBatteryReportingEnabled(enabled);
        if (enabled) sampleAndPublishBatteryNow();
        flashSettingsConfirmationLed(0x02);
        setSettingsPage(SettingsPage::Battery);
      }
      return;

    case SettingsPage::BatteryReportNow:
      sampleAndPublishBatteryNow();
      flashSettingsConfirmationLed(0x02);
      setSettingsPage(SettingsPage::Battery);
      return;

    case SettingsPage::Inactive:
    case SettingsPage::Main:
    case SettingsPage::Bluetooth:
    case SettingsPage::Power:
    case SettingsPage::Battery:
      return;
  }
}

void handleSettingsModifierEdges() {
  const bool greenDown = (modifiers & ChatpadProtocol::MOD_GREEN) != 0;
  const bool greenWasDown =
      (previousModifiers & ChatpadProtocol::MOD_GREEN) != 0;
  const bool orangeDown = (modifiers & ChatpadProtocol::MOD_ORANGE) != 0;
  const bool orangeWasDown =
      (previousModifiers & ChatpadProtocol::MOD_ORANGE) != 0;

  if (orangeDown && !orangeWasDown) {
    modifierIgnoreRelease |= ChatpadProtocol::MOD_ORANGE;
    noteSettingsInput();
    if (settingsChoiceReadyForConfirmation()) {
      flashSettingsConfirmationLed(0x04);
    }
    settingsGoBack();
    return;
  }

  if (greenDown && !greenWasDown) {
    modifierIgnoreRelease |= ChatpadProtocol::MOD_GREEN;
    settingsGreenPressedAt = millis();
    settingsEraseAllCompleted = false;
    acceptSettingsSelection();
  }

  if (!greenDown && greenWasDown) {
    settingsGreenPressedAt = 0;
    settingsEraseAllCompleted = false;
  }
}

void handleSettingsKeyPress(uint8_t scan) {

  if (scan == 0x71) {
    handleSettingsBackspace();
    return;
  }

  const int8_t digit = scanToDigit(scan);
  if (digit >= 0) handleSettingsDigit(static_cast<uint8_t>(digit));
}

void serviceSettingsMenu() {
  if (!settingsActive()) return;

  const uint32_t now = millis();

  if (settingsPage == SettingsPage::BluetoothForget &&
      settingsPendingSelection == 4 &&
      greenButtonDown &&
      settingsGreenPressedAt != 0 &&
      !settingsEraseAllCompleted &&
      now - settingsGreenPressedAt >= Config::ERASE_ALL_CONFIRM_MS) {
    settingsEraseAllCompleted = true;
    modifierIgnoreRelease |= ChatpadProtocol::MOD_GREEN;
    flashSettingsConfirmationLed(0x02);
    exitSettingsMenu();
    forgetAllRememberedHosts();
    modifierIgnoreRelease |= ChatpadProtocol::MOD_GREEN;
    suppressedModifiers |= ChatpadProtocol::MOD_GREEN;
    return;
  }

  if ((settingsPage == SettingsPage::PowerBacklightTime ||
       settingsPage == SettingsPage::PowerSleepTime) &&
      settingsNumericDigits > 0 &&
      now - settingsNumericLastDigitAt >= Config::NUMERIC_AUTO_ACCEPT_MS) {
    confirmNumericSetting();
    return;
  }

  if (now - settingsLastInputAt >= Config::SETTINGS_IDLE_MS) {
    exitSettingsMenu();
  }
}

void settingsMenuCoordinates(uint8_t& mainMenu, uint8_t& subMenu) {
  mainMenu = 0;
  subMenu = 0;

  switch (settingsPage) {
    case SettingsPage::Bluetooth: mainMenu = 1; return;
    case SettingsPage::BluetoothPairSlot: mainMenu = 1; subMenu = 1; return;
    case SettingsPage::BluetoothForget: mainMenu = 1; subMenu = 2; return;
    case SettingsPage::BluetoothTxPower: mainMenu = 1; subMenu = 3; return;
    case SettingsPage::BluetoothReconnect: mainMenu = 1; subMenu = 4; return;

    case SettingsPage::Power: mainMenu = 2; return;
    case SettingsPage::PowerModeSelect: mainMenu = 2; subMenu = 1; return;
    case SettingsPage::PowerBacklightTime: mainMenu = 2; subMenu = 2; return;
    case SettingsPage::PowerSleepTime: mainMenu = 2; subMenu = 3; return;
    case SettingsPage::PowerResetOverrides: mainMenu = 2; subMenu = 4; return;

    case SettingsPage::Battery: mainMenu = 3; return;
    case SettingsPage::BatteryReporting: mainMenu = 3; subMenu = 1; return;
    case SettingsPage::BatteryReportNow: mainMenu = 3; subMenu = 2; return;

    case SettingsPage::Inactive:
    case SettingsPage::Main:
      return;
  }
}

uint32_t settingsFlashSectionDuration(uint8_t flashes) {
  if (flashes == 0) return 0;
  return static_cast<uint32_t>(flashes) *
             (Config::SETTINGS_LED_ON_MS + Config::SETTINGS_LED_GAP_MS) -
         Config::SETTINGS_LED_GAP_MS;
}

bool settingsFlashSectionOn(uint8_t flashes, uint32_t positionMs) {
  for (uint8_t pulse = 0; pulse < flashes; ++pulse) {
    const uint32_t pulseStart = static_cast<uint32_t>(pulse) *
        (Config::SETTINGS_LED_ON_MS + Config::SETTINGS_LED_GAP_MS);
    if (positionMs >= pulseStart &&
        positionMs < pulseStart + Config::SETTINGS_LED_ON_MS) return true;
  }
  return false;
}

uint8_t settingsIndicatorState(uint32_t now) {

  if (settingsChoiceReadyForConfirmation()) {
    return static_cast<int32_t>(settingsConfirmationPromptUntil - now) > 0
               ? 0x06
               : 0;
  }

  uint8_t mainMenu = 0;
  uint8_t subMenu = 0;
  settingsMenuCoordinates(mainMenu, subMenu);

  if (mainMenu == 0) {
    return ((now - settingsPageEnteredAt) % 1000UL) <
                   Config::SETTINGS_LED_ON_MS
               ? 0x06
               : 0;
  }

  const uint32_t greenDuration = settingsFlashSectionDuration(mainMenu);
  const uint32_t orangeDuration = settingsFlashSectionDuration(subMenu);
  const uint32_t orangeStart =
      greenDuration + Config::SETTINGS_LED_SECTION_GAP_MS;
  const uint32_t cycleDuration =
      orangeStart + orangeDuration + Config::SETTINGS_LED_CYCLE_GAP_MS;
  const uint32_t safeCycleDuration = cycleDuration == 0 ? 1 : cycleDuration;
  const uint32_t position =
      (now - settingsPageEnteredAt) % safeCycleDuration;

  if (position < greenDuration &&
      settingsFlashSectionOn(mainMenu, position)) return 0x02;

  if (subMenu != 0 && position >= orangeStart &&
      position < orangeStart + orangeDuration &&
      settingsFlashSectionOn(subMenu, position - orangeStart)) return 0x04;

  return 0;
}

// -----------------------------------------------------------------------------
// UK keyboard layout and symbol layers
// -----------------------------------------------------------------------------
bool sendUKCharacter(char character) {
  struct Stroke { char character; uint8_t key; uint8_t modifiers; };

  static constexpr Stroke STROKES[] = {
      {'!', KEY_1, KEY_MOD_LSHIFT}, {'"', KEY_2, KEY_MOD_LSHIFT},
      {'#', KEY_HASH_TILDE, 0}, {'$', KEY_4, KEY_MOD_LSHIFT},
      {'%', KEY_5, KEY_MOD_LSHIFT}, {'^', KEY_6, KEY_MOD_LSHIFT},
      {'&', KEY_7, KEY_MOD_LSHIFT}, {'*', KEY_8, KEY_MOD_LSHIFT},
      {'(', KEY_9, KEY_MOD_LSHIFT}, {')', KEY_0, KEY_MOD_LSHIFT},
      {'@', KEY_APOSTROPHE, KEY_MOD_LSHIFT},
      {'~', KEY_HASH_TILDE, KEY_MOD_LSHIFT},
      {'{', KEY_LEFTBRACE, KEY_MOD_LSHIFT}, {'}', KEY_RIGHTBRACE, KEY_MOD_LSHIFT},
      {'[', KEY_LEFTBRACE, 0}, {']', KEY_RIGHTBRACE, 0},
      {'/', KEY_SLASH, 0}, {'?', KEY_SLASH, KEY_MOD_LSHIFT},
      {'\'', KEY_APOSTROPHE, 0}, {':', KEY_SEMICOLON, KEY_MOD_LSHIFT},
      {';', KEY_SEMICOLON, 0}, {'-', KEY_MINUS, 0},
      {'_', KEY_MINUS, KEY_MOD_LSHIFT}, {'=', KEY_EQUAL, 0},
      {'+', KEY_EQUAL, KEY_MOD_LSHIFT}, {'\\', KEY_NONUS_BACKSLASH, 0},
      {'|', KEY_NONUS_BACKSLASH, KEY_MOD_LSHIFT},
      {'<', KEY_COMMA, KEY_MOD_LSHIFT}, {'>', KEY_DOT, KEY_MOD_LSHIFT},
      {'`', KEY_GRAVE, 0},
  };

  for (const Stroke& stroke : STROKES) {
    if (stroke.character == character) {
      tapKey(stroke.key, stroke.modifiers);
      return true;
    }
  }
  return false;
}

uint8_t keypadDigit(char digit) {
  static constexpr uint8_t KEYS[] = {
      KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4,
      KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9,
  };
  return (digit >= '0' && digit <= '9') ? KEYS[digit - '0'] : KEY_NONE;
}

// Send characters that are not convenient HID keys using Windows Alt+numpad.
void sendWindowsAltCode(uint16_t code) {
  if (!keyboard.isPaired()) return;

  const bool restoreNumLock = !keyboard.isNumLockOn();
  if (restoreNumLock) {
    keyboard.tap(KEY_NUM_LOCK, 0, 35, 35);
    delay(60);
  }

  char digits[6] = {};
  snprintf(digits, sizeof(digits), "%04u", static_cast<unsigned>(code));

  keyboard.press(KEY_LALT);
  delay(20);
  for (const char* digit = digits; *digit != '\0'; ++digit) {
    keyboard.tap(keypadDigit(*digit), 0, 25, 12);
  }
  keyboard.release(KEY_LALT);
  delay(30);

  if (restoreNumLock) keyboard.tap(KEY_NUM_LOCK, 0, 35, 35);
}

uint16_t composedAltCode(Accent accent, char letter, bool uppercase) {
  if (uppercase && letter >= 'a' && letter <= 'z') letter -= ('a' - 'A');

  switch (accent) {
    case Accent::Circumflex:
      switch (letter) {
        case 'a': return 226; case 'A': return 194;
        case 'e': return 234; case 'E': return 202;
        case 'i': return 238; case 'I': return 206;
        case 'o': return 244; case 'O': return 212;
        case 'u': return 251; case 'U': return 219;
        default: return 0;
      }

    case Accent::Tilde:
      switch (letter) {
        case 'a': return 227; case 'A': return 195;
        case 'n': return 241; case 'N': return 209;
        case 'o': return 245; case 'O': return 213;
        default: return 0;
      }

    case Accent::Diaeresis:
      switch (letter) {
        case 'a': return 228; case 'A': return 196;
        case 'e': return 235; case 'E': return 203;
        case 'i': return 239; case 'I': return 207;
        case 'o': return 246; case 'O': return 214;
        case 'u': return 252; case 'U': return 220;
        case 'y': return 255; case 'Y': return 159;
        default: return 0;
      }

    case Accent::Grave:
      switch (letter) {
        case 'a': return 224; case 'A': return 192;
        case 'e': return 232; case 'E': return 200;
        case 'i': return 236; case 'I': return 204;
        case 'o': return 242; case 'O': return 210;
        case 'u': return 249; case 'U': return 217;
        default: return 0;
      }

    case Accent::None:
    default:
      return 0;
  }
}

void sendAccentAlone(Accent accent) {
  if (accent == Accent::Circumflex) sendUKCharacter('^');
  if (accent == Accent::Tilde) sendUKCharacter('~');
  if (accent == Accent::Diaeresis) sendWindowsAltCode(168);
  if (accent == Accent::Grave) sendUKCharacter('`');
}

bool consumePendingAccent(uint8_t scan, uint8_t activeModifiers) {
  if (pendingAccent == Accent::None) return false;

  if (scan == 0x54) {
    sendAccentAlone(pendingAccent);
    pendingAccent = Accent::None;
    return true;
  }

  const BaseKey* base = findBaseKey(scan);
  if (base != nullptr && base->letter != 0) {
    const uint16_t code = composedAltCode(
        pendingAccent, base->letter,
        ((activeModifiers & ChatpadProtocol::MOD_SHIFT) != 0) !=
        keyboard.isCapsLockOn());

    if (code != 0) {
      sendWindowsAltCode(code);
      pendingAccent = Accent::None;
      return true;
    }
  }

  sendAccentAlone(pendingAccent);
  pendingAccent = Accent::None;
  return false;
}

void executeLayerAction(const LayerAction& action) {
  switch (action.kind) {
    case ActionKind::Character:
      sendUKCharacter(static_cast<char>(action.value));
      break;
    case ActionKind::AltCode:
      sendWindowsAltCode(action.value);
      break;
    case ActionKind::Accent:
      pendingAccent = static_cast<Accent>(action.value);
      break;
  }
}

void sendBaseKey(uint8_t scan, uint8_t activeModifiers) {
  const BaseKey* base = findBaseKey(scan);
  if (base == nullptr) return;

  uint8_t hidModifiers = 0;
  const bool shifted = (activeModifiers & ChatpadProtocol::MOD_SHIFT) != 0;

  if (shifted &&
      (base->letter != 0 ||
       base->hid == KEY_LEFT ||
       base->hid == KEY_RIGHT ||
       base->hid == KEY_RETURN)) {
    hidModifiers = KEY_MOD_LSHIFT;
  }

  tapKey(base->hid, hidModifiers);
}

bool isRepeatable(uint8_t scan, uint8_t activeModifiers) {
  if ((activeModifiers & (ChatpadProtocol::MOD_GREEN |
                          ChatpadProtocol::MOD_ORANGE)) != 0) return false;
  return findBaseKey(scan) != nullptr;
}

void handleScan(uint8_t scan, uint8_t activeModifiers, bool repeated = false) {
  if (scan == 0 || !keyboard.isPaired()) return;

  if (!repeated && consumePendingAccent(scan, activeModifiers)) {
    consumeOneShotModifiers();
    return;
  }

  const bool colourModifierActive =
      (activeModifiers &
       (ChatpadProtocol::MOD_GREEN | ChatpadProtocol::MOD_ORANGE)) != 0;

  if (colourModifierActive && (scan == 0x55 || scan == 0x51)) {
    tapKey(scan == 0x55 ? KEY_UP : KEY_DOWN);

    if (!repeated) consumeOneShotModifiers();
    return;
  }

  const bool greenActive =
      (activeModifiers & ChatpadProtocol::MOD_GREEN) != 0;
  const bool orangeActive =
      (activeModifiers & ChatpadProtocol::MOD_ORANGE) != 0;

  if (greenActive || orangeActive) {
    bool handledNavigationShortcut = true;

    if (scan == 0x71) {
      tapKey(greenActive ? KEY_DELETE : KEY_ESCAPE);
    } else if (scan == 0x54) {
      if (greenActive) {
        tapKey(KEY_TAB);
      } else {
        tapKey(KEY_TAB, KEY_MOD_LSHIFT);
      }
    } else if (scan == 0x63) {
      tapKey(greenActive ? KEY_HOME : KEY_END);
    } else {
      handledNavigationShortcut = false;
    }

    if (handledNavigationShortcut) {

      if (!repeated) consumeOneShotModifiers();
      return;
    }
  }

  const LayerAction* action = nullptr;
  if ((activeModifiers & ChatpadProtocol::MOD_GREEN) != 0) {
    action = findLayerAction(GREEN_LAYER, (sizeof(GREEN_LAYER) / sizeof(GREEN_LAYER[0])), scan);
  } else if ((activeModifiers & ChatpadProtocol::MOD_ORANGE) != 0) {
    action = findLayerAction(ORANGE_LAYER, (sizeof(ORANGE_LAYER) / sizeof(ORANGE_LAYER[0])), scan);
  }

  if (action != nullptr) executeLayerAction(*action);
  else sendBaseKey(scan, activeModifiers);

  if (!repeated && isRepeatable(scan, activeModifiers)) {
    repeatScan = scan;
    repeatAt = millis() + Config::REPEAT_DELAY_MS;
  }

  if (!repeated) consumeOneShotModifiers();
}

// -----------------------------------------------------------------------------
// Chatpad input processing
// -----------------------------------------------------------------------------
void updateKeyState(uint8_t newModifiers, uint8_t newKey1, uint8_t newKey2) {

  previousModifiers = modifiers;
  previousKey1 = key1;
  previousKey2 = key2;

  chatpadReportedModifiers =
      newModifiers &
      (ChatpadProtocol::MOD_SHIFT |
       ChatpadProtocol::MOD_ORANGE |
       ChatpadProtocol::MOD_PEOPLE);

  modifiers = chatpadReportedModifiers;
  if (greenButtonDown) modifiers |= ChatpadProtocol::MOD_GREEN;

  key1 = newKey1;
  key2 = newKey2;

  suppressedModifiers &= modifiers;

  const bool stateChanged =
      modifiers != previousModifiers || key1 != previousKey1 || key2 != previousKey2;

  if (stateChanged &&
      (modifiers != 0 || key1 != 0 || key2 != 0 ||
       previousModifiers != 0 || previousKey1 != 0 || previousKey2 != 0)) {
    markActivity();
  }

  const bool slotChord =
      (modifiers & (ChatpadProtocol::MOD_GREEN | ChatpadProtocol::MOD_ORANGE)) ==
      (ChatpadProtocol::MOD_GREEN | ChatpadProtocol::MOD_ORANGE);

  const bool slotChordWasDown =
      (previousModifiers & (ChatpadProtocol::MOD_GREEN | ChatpadProtocol::MOD_ORANGE)) ==
      (ChatpadProtocol::MOD_GREEN | ChatpadProtocol::MOD_ORANGE);

  if (!settingsActive() && slotChord && !slotChordWasDown) {

    slotChordHeld = true;
    slotChordSince = millis();

    oneShotModifiers &=
        static_cast<uint8_t>(~(ChatpadProtocol::MOD_GREEN | ChatpadProtocol::MOD_ORANGE));
    modifierLastTapAt[1] = 0;
    modifierLastTapAt[2] = 0;
    modifierPressedAt[1] = 0;
    modifierPressedAt[2] = 0;
    modifierUsedWhileHeld &=
        static_cast<uint8_t>(~(ChatpadProtocol::MOD_GREEN | ChatpadProtocol::MOD_ORANGE));
    modifierIgnoreRelease |=
        ChatpadProtocol::MOD_GREEN | ChatpadProtocol::MOD_ORANGE;
    refreshModifierIndicators();

    suppressedModifiers |=
        ChatpadProtocol::MOD_GREEN | ChatpadProtocol::MOD_ORANGE;
  }

  if (!settingsActive() && !slotChord && slotChordWasDown) {

    const uint32_t heldMs =
        slotChordSince == 0 ? 0 : millis() - slotChordSince;

    slotChordHeld = false;
    slotChordSince = 0;

    if (heldMs >= Config::SOFT_OFF_HOLD_MS) {

      slotMode = SlotMode::Normal;
      slotModeEnteredAt = 0;
      setPeopleLed(false);

      if (Config::DEBUG_LOG) {
        Serial.printf("Green+Orange held %lu ms -> manual soft off\n",
                      static_cast<unsigned long>(heldMs));
      }

      enterLowPowerSleep();
      return;
    } else if (heldMs >= Config::SETTINGS_HOLD_MS) {

      enterSettingsMenu();
    } else if (heldMs >= Config::SLOT_SELECT_HOLD_MS) {

      slotMode = SlotMode::SelectSaved;
      slotModeEnteredAt = millis();
    }
  }

  if (settingsActive()) {
    handleSettingsModifierEdges();

    if (key1 != 0 && !wasHeld(key1)) handleSettingsKeyPress(key1);
    if (key2 != 0 && !wasHeld(key2)) handleSettingsKeyPress(key2);

    repeatScan = 0;
    return;
  }

  if (!slotChord) {
    const uint8_t latchable[] = {
        ChatpadProtocol::MOD_SHIFT,
        ChatpadProtocol::MOD_GREEN,
        ChatpadProtocol::MOD_ORANGE,
    };

    for (uint8_t bit : latchable) {
      const bool down = (modifiers & bit) != 0;
      const bool wasDown = (previousModifiers & bit) != 0;

      if (down && !wasDown) handleModifierPress(bit);
      if (!down && wasDown) handleModifierRelease(bit);
    }
  }

  const bool capsChord =
      slotMode == SlotMode::Normal &&
      (modifiers & (ChatpadProtocol::MOD_ORANGE | ChatpadProtocol::MOD_SHIFT)) ==
      (ChatpadProtocol::MOD_ORANGE | ChatpadProtocol::MOD_SHIFT);

  if (capsChord && !capsChordHeld) {
    tapKey(KEY_CAPS_LOCK);
    modifierUsedWhileHeld |=
        ChatpadProtocol::MOD_ORANGE | ChatpadProtocol::MOD_SHIFT;
  }
  capsChordHeld = capsChord;

  const bool peopleDown = (modifiers & ChatpadProtocol::MOD_PEOPLE) != 0;
  const bool peopleWasDown = (previousModifiers & ChatpadProtocol::MOD_PEOPLE) != 0;

  if (keyboard.isPaired() && slotMode == SlotMode::Normal) {
    if (peopleDown && !peopleWasDown) keyboard.press(KEY_LGUI);
    if (!peopleDown && peopleWasDown) keyboard.release(KEY_LGUI);
  }

  const uint8_t activeModifiers = effectiveModifiers();

  if (key1 != 0 && !wasHeld(key1)) {
    markPhysicalModifiersUsedForKey();
    if (!handleSlotSelectionKey(key1)) handleScan(key1, activeModifiers);
  }

  if (key2 != 0 && !wasHeld(key2)) {
    markPhysicalModifiersUsedForKey();
    if (!handleSlotSelectionKey(key2)) handleScan(key2, activeModifiers);
  }

  if (repeatScan != 0 && !isHeld(repeatScan)) repeatScan = 0;
}

// The isolated D2 switch is the authoritative Green modifier input and wake key.
void serviceGreenButton() {
  const bool down = digitalRead(Config::GREEN_BUTTON_PIN) == LOW;
  if (down == greenButtonDown) return;

  greenButtonDown = down;
  updateKeyState(chatpadReportedModifiers, key1, key2);

  if (Config::DEBUG_LOG) {
    Serial.printf("Green D2 -> %s\n", down ? "PRESSED" : "RELEASED");
  }
}

void serviceKeyRepeat() {
  if (settingsActive()) return;
  if (repeatScan == 0 || !isHeld(repeatScan)) return;
  if (static_cast<int32_t>(millis() - repeatAt) < 0) return;

  handleScan(repeatScan, effectiveModifiers(), true);
  repeatAt = millis() + Config::REPEAT_RATE_MS;
}

// -----------------------------------------------------------------------------
// UART packet parsing and Chatpad service tasks
// -----------------------------------------------------------------------------
bool packetChecksumValid(const uint8_t* data) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < 8; ++i) sum = static_cast<uint8_t>(sum + data[i]);
  return sum == 0;
}

void processPacket(const uint8_t* data) {
  if (data[0] != 0xB4 || data[1] != 0xC5 || !packetChecksumValid(data)) return;
  debugPacket(data);
  updateKeyState(data[3], data[4], data[5]);
}

void readChatpad() {
  while (chatpadSerial.available() > 0) {
    const uint8_t value = static_cast<uint8_t>(chatpadSerial.read());

    if (packetLength == 0) {
      if (value == 0xB4) packet[packetLength++] = value;
      continue;
    }

    if (packetLength == 1) {
      if (value == 0xC5) packet[packetLength++] = value;
      else packetLength = (value == 0xB4) ? 1 : 0;
      continue;
    }

    packet[packetLength++] = value;
    if (packetLength == sizeof(packet)) {
      processPacket(packet);
      packetLength = 0;
    }
  }
}

void serviceChatpadIndicators() {
  const uint32_t now = millis();
  uint8_t state = 0;

  if (settingsActive()) {
    state = settingsIndicatorState(now);
    if (state != lastLedState) {
      sendChatpadCommand(ChatpadProtocol::CMD_LED_STATE_BASE | state);
      lastLedState = state;
    }
    return;
  }

  const bool pairingIndicator =
      slotMode == SlotMode::PairingNew ||
      slotMode == SlotMode::SelectPair ||
      (slotChordHeld && slotChordSince != 0 &&
       now - slotChordSince >= Config::SLOT_PAIR_HOLD_MS &&
       now - slotChordSince < Config::SOFT_OFF_HOLD_MS);

  if (pairingIndicator) {
    if (static_cast<int32_t>(now - pairingLedAt) >= 0) {
      pairingShowGreen = !pairingShowGreen;
      pairingLedAt = now + Config::PAIRING_LED_STEP_MS;
    }

    state = pairingShowGreen ? 0x02 : 0x04;
  } else {
    const uint8_t active = effectiveModifiers();

    const bool latchFlashOn =
        ((now / Config::MODIFIER_LATCH_FLASH_MS) & 1U) == 0;

    if ((latchedModifiers & ChatpadProtocol::MOD_SHIFT) != 0) {
      if (latchFlashOn) state |= 0x01;
    } else if (keyboard.isCapsLockOn() ||
               (active & ChatpadProtocol::MOD_SHIFT) != 0) {
      state |= 0x01;
    }

    if ((latchedModifiers & ChatpadProtocol::MOD_GREEN) != 0) {
      if (latchFlashOn) state |= 0x02;
    } else if ((active & ChatpadProtocol::MOD_GREEN) != 0) {
      state |= 0x02;
    }

    if ((latchedModifiers & ChatpadProtocol::MOD_ORANGE) != 0) {
      if (latchFlashOn) state |= 0x04;
    } else if ((active & ChatpadProtocol::MOD_ORANGE) != 0) {
      state |= 0x04;
    }
  }

  if (state != lastLedState) {
    sendChatpadCommand(ChatpadProtocol::CMD_LED_STATE_BASE | state);
    lastLedState = state;
  }
}

void servicePeopleSlotIndicator() {
  const uint32_t now = millis();
  const bool paired = keyboard.isPaired();

  if (paired && !bleWasPaired) {
    publishBatteryLevel();
    lastBatteryReportAt = millis();
    bleDisconnectedAt = 0;
    slowAdvertisingActive = false;

    if (Config::DEBUG_LOG) Serial.println("BLE host connected");

    if ((latchedModifiers & ChatpadProtocol::MOD_SHIFT) != 0) {
      keyboard.press(KEY_LSHIFT);
    }
  }

  if (!paired && bleWasPaired) {
    setPeopleLed(false);
    bleDisconnectedAt = now;
    slowAdvertisingActive = false;

    if (Config::DEBUG_LOG) Serial.println("BLE host disconnected");
    restartAdvertisingAtPowerRate(false);
  }

  bleWasPaired = paired;

  if (settingsActive()) {
    setPeopleLed(false);
    return;
  }

  const bool pairingThresholdHeld =
      slotChordHeld &&
      slotChordSince != 0 &&
      now - slotChordSince >= Config::SLOT_PAIR_HOLD_MS;

  if (slotMode == SlotMode::PairingNew ||
      slotMode == SlotMode::SelectPair ||
      pairingThresholdHeld) {
    setPeopleLed(false);
    return;
  }

  const bool showingCurrentSlotDuringHold =
      slotChordHeld &&
      slotChordSince != 0 &&
      now - slotChordSince >= Config::SLOT_SELECT_HOLD_MS &&
      now - slotChordSince < Config::SLOT_PAIR_HOLD_MS;

  if (showingCurrentSlotDuringHold || slotMode == SlotMode::SelectSaved) {

    const uint32_t patternStartedAt =
        showingCurrentSlotDuringHold
            ? slotChordSince + Config::SLOT_SELECT_HOLD_MS
            : slotModeEnteredAt;

    const bool ledShouldBeOn =
        slotFlashState(activeSlot, now - patternStartedAt);

    setPeopleLed(ledShouldBeOn);
    return;
  }

  setPeopleLed(false);
}

// -----------------------------------------------------------------------------
// BLE reconnect and power management
// -----------------------------------------------------------------------------
void serviceBleAdvertisingPower() {
  if (keyboard.isPaired()) return;

  if (slotMode != SlotMode::Normal) return;

  if (reconnectMode == ReconnectMode::Fast) return;

  const uint32_t now = millis();

  if (bleDisconnectedAt == 0) {
    bleDisconnectedAt = now;
    return;
  }

  const uint32_t slowAfterMs =
      reconnectMode == ReconnectMode::Eco
          ? 0
          : Config::BLE_SLOW_ADVERTISING_AFTER_MS;

  if (!slowAdvertisingActive && now - bleDisconnectedAt >= slowAfterMs) {
    restartAdvertisingAtPowerRate(true);
  }
}

void serviceChatpadPower() {
  const uint32_t now = millis();

  followBacklightHardwareTimeout(now);

  if (now - lastKeepaliveAt >= Config::KEEPALIVE_MS) {
    sendChatpadCommand(ChatpadProtocol::CMD_KEEPALIVE);
    lastKeepaliveAt = now;
  }

  if (settingsActive()) {
    return;
  }

  const uint32_t backlightIdleMs = effectiveBacklightIdleMs();
  if (backlightOn &&
      (backlightIdleMs == 0 || now - lastActivityAt >= backlightIdleMs)) {
    setBacklight(false);
  }

}

void serviceSleepTimer() {

  if (settingsActive()) return;

  if (millis() - lastActivityAt >= effectiveSleepIdleMs()) {
    enterLowPowerSleep();
  }
}

// -----------------------------------------------------------------------------
// Supply monitoring and BLE battery reporting
// -----------------------------------------------------------------------------
float readSupplyVoltage() {
  constexpr uint8_t SAMPLE_COUNT = 16;
  uint32_t totalMillivolts = 0;

  for (uint8_t sample = 0; sample < SAMPLE_COUNT; ++sample) {
    totalMillivolts += analogReadMilliVolts(Config::SUPPLY_SENSE_PIN);
    delayMicroseconds(250);
  }

  const float pinVolts =
      (totalMillivolts / static_cast<float>(SAMPLE_COUNT)) / 1000.0f;
  return pinVolts * Config::SUPPLY_DIVIDER_RATIO * Config::SUPPLY_CALIBRATION;
}

void flashBatteryWarning(uint8_t flashes) {

  if (settingsActive()) {
    return;
  }

  const bool restoreBacklight = backlightOn;

  for (uint8_t i = 0; i < flashes; ++i) {
    sendChatpadCommand(ChatpadProtocol::CMD_BACKLIGHT_OFF);
    delay(110);
    sendChatpadCommand(ChatpadProtocol::CMD_BACKLIGHT_ON);
    delay(110);
  }

  sendChatpadCommand(restoreBacklight ? ChatpadProtocol::CMD_BACKLIGHT_ON
                                      : ChatpadProtocol::CMD_BACKLIGHT_OFF);
  if (restoreBacklight) lastBacklightOnCommandAt = millis();
}

uint8_t batteryPercentFromVoltage(float voltage) {
  if (voltage >= Config::BATTERY_100_V) return 100;
  if (voltage >= Config::BATTERY_75_V)  return 75;
  if (voltage >= Config::BATTERY_50_V)  return 50;
  if (voltage >= Config::BATTERY_25_V)  return 25;
  if (voltage >= Config::BATTERY_10_V)  return 10;
  return 5;
}

void publishBatteryLevel() {
  const uint8_t level =
      batteryReportingEnabled
          ? batteryPercentFromVoltage(latestSupplyVoltage)
          : Config::BATTERY_PLACEHOLDER_PERCENT;
  keyboard.setBatteryLevel(level);

  if (Config::DEBUG_LOG) {
    if (batteryReportingEnabled) {
      Serial.printf("BLE battery level -> %u%% (%.3f V)\n",
                    level, latestSupplyVoltage);
    } else {
      Serial.printf("BLE battery level -> %u%% placeholder (ADC off)\n",
                    level);
    }
  }
}

void sampleAndPublishBatteryNow() {
  if (!batteryReportingEnabled || !Config::ENABLE_SUPPLY_MONITOR) {
    publishBatteryLevel();
    return;
  }

  latestSupplyVoltage = readSupplyVoltage();
  const uint8_t level = batteryPercentFromVoltage(latestSupplyVoltage);
  keyboard.setBatteryLevel(level);
  lastBatterySampleAt = millis();
  lastBatteryReportAt = lastBatterySampleAt;

  if (Config::DEBUG_LOG) {
    Serial.printf("Manual BLE battery level -> %u%% (%.3f V)\n",
                  level, latestSupplyVoltage);
  }
}

void setBatteryState(BatteryState nextState) {
  const bool stateChanged = nextState != batteryState;
  batteryState = nextState;

  if (!stateChanged) return;

  if (batteryState == BatteryState::Low) {
    flashBatteryWarning(2);
  }

  if (batteryState == BatteryState::Critical) {
    flashBatteryWarning(4);
  }
}

void serviceSupplyMonitor() {
  if (!Config::ENABLE_SUPPLY_MONITOR || !batteryReportingEnabled) return;

  const uint32_t now = millis();
  if (now - lastBatterySampleAt < Config::BATTERY_SAMPLE_MS) return;
  lastBatterySampleAt = now;

  const float voltage = readSupplyVoltage();
  latestSupplyVoltage = voltage;

  if (lastBatteryReportAt == 0 ||
      now - lastBatteryReportAt >= Config::BATTERY_REPORT_MS) {
    publishBatteryLevel();
    lastBatteryReportAt = now;
  }

  if (voltage < Config::LOW_VOLTAGE) {
    if (lowVoltageSince == 0) lowVoltageSince = now;
  } else {
    lowVoltageSince = 0;
  }

  if (voltage < Config::CRITICAL_VOLTAGE) {
    if (criticalVoltageSince == 0) criticalVoltageSince = now;
  } else {
    criticalVoltageSince = 0;
  }

  if (voltage >= Config::LOW_RESET_VOLTAGE) {
    setBatteryState(BatteryState::Normal);
  } else if (criticalVoltageSince != 0 &&
             now - criticalVoltageSince >= Config::CRITICAL_CONFIRM_MS) {
    setBatteryState(BatteryState::Critical);
  } else if (lowVoltageSince != 0 &&
             now - lowVoltageSince >= Config::LOW_CONFIRM_MS &&
             batteryState == BatteryState::Normal) {
    setBatteryState(BatteryState::Low);
  }

  if (Config::DEBUG_LOG) {
    Serial.printf("Supply %.3f V | state %u\n", voltage,
                  static_cast<unsigned>(batteryState));
  }
}

// -----------------------------------------------------------------------------
// Chatpad startup and sleep preparation
// -----------------------------------------------------------------------------
void initialiseChatpad() {

  delay(500);
  sendChatpadCommand(ChatpadProtocol::CMD_INITIALISE);
  delay(500);
  sendChatpadCommand(ChatpadProtocol::CMD_KEEPALIVE);
  const bool startWithBacklight = effectiveBacklightIdleMs() != 0;
  sendChatpadCommand(startWithBacklight
                         ? ChatpadProtocol::CMD_BACKLIGHT_ON
                         : ChatpadProtocol::CMD_BACKLIGHT_OFF);
  sendChatpadCommand(ChatpadProtocol::CMD_PEOPLE_LED_OFF);

  backlightOn = startWithBacklight;
  lastBacklightOnCommandAt = startWithBacklight ? millis() : 0;
  peopleLedOn = false;
  lastLedState = 0xFF;
  lastKeepaliveAt = millis();
  lastActivityAt = millis();
}

// Configure D2 as the wake source before entering low-power sleep.
bool prepareGreenLightSleepWakeInput() {
  pinMode(Config::GREEN_BUTTON_PIN, INPUT_PULLUP);

  if (digitalRead(Config::GREEN_BUTTON_PIN) == LOW) {
    if (Config::DEBUG_LOG) Serial.println("Sleep cancelled: Green/D2 is still held LOW");
    return false;
  }

  const gpio_num_t wakeGpio =
      static_cast<gpio_num_t>(digitalPinToGPIONumber(Config::GREEN_BUTTON_PIN));

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_disable(wakeGpio);

  if (gpio_wakeup_enable(wakeGpio, GPIO_INTR_LOW_LEVEL) != ESP_OK) {
    if (Config::DEBUG_LOG) Serial.println("Failed to enable D2 LOW-level light-sleep wake");
    return false;
  }

  if (esp_sleep_enable_gpio_wakeup() != ESP_OK) {
    if (Config::DEBUG_LOG) Serial.println("Failed to enable GPIO light-sleep wake");
    return false;
  }

  gpio_sleep_sel_dis(wakeGpio);
  return true;
}

bool prepareGreenDeepSleepWakeInput() {
  const gpio_num_t wakeGpio =
      static_cast<gpio_num_t>(digitalPinToGPIONumber(Config::GREEN_BUTTON_PIN));

  pinMode(Config::GREEN_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(Config::GREEN_BUTTON_PIN) == LOW) {
    if (Config::DEBUG_LOG) Serial.println("Deep sleep cancelled: Green/D2 is still held LOW");
    return false;
  }

  if (!rtc_gpio_is_valid_gpio(wakeGpio)) {
    if (Config::DEBUG_LOG) Serial.println("D2 is not RTC-capable; cannot use it for EXT0 deep-sleep wake");
    return false;
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_disable(wakeGpio);

  if (rtc_gpio_init(wakeGpio) != ESP_OK ||
      rtc_gpio_set_direction(wakeGpio, RTC_GPIO_MODE_INPUT_ONLY) != ESP_OK) {
    if (Config::DEBUG_LOG) Serial.println("Failed to configure D2 as RTC input");
    return false;
  }

  rtc_gpio_pulldown_dis(wakeGpio);
  rtc_gpio_pullup_en(wakeGpio);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  if (esp_sleep_enable_ext0_wakeup(wakeGpio, 0) != ESP_OK) {
    if (Config::DEBUG_LOG) Serial.println("Failed to enable Green/D2 EXT0 deep-sleep wake");
    rtc_gpio_deinit(wakeGpio);
    return false;
  }

  return true;
}

// Put the Chatpad UART pins into defined low-power states before deep sleep.
void prepareChatpadPinsForDeepSleep() {
  const gpio_num_t txGpio =
      static_cast<gpio_num_t>(digitalPinToGPIONumber(Config::CHATPAD_TX_PIN));
  const gpio_num_t rxGpio =
      static_cast<gpio_num_t>(digitalPinToGPIONumber(Config::CHATPAD_RX_PIN));

  if (rtc_gpio_is_valid_gpio(txGpio)) {
    rtc_gpio_init(txGpio);
    rtc_gpio_set_direction(txGpio, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_pullup_dis(txGpio);
    rtc_gpio_pulldown_dis(txGpio);
    rtc_gpio_set_level(txGpio, 1);
    rtc_gpio_hold_en(txGpio);
  } else {

    pinMode(Config::CHATPAD_TX_PIN, OUTPUT);
    digitalWrite(Config::CHATPAD_TX_PIN, HIGH);
    gpio_pullup_dis(txGpio);
    gpio_pulldown_dis(txGpio);
    gpio_hold_en(txGpio);
    gpio_deep_sleep_hold_en();
  }

  if (rtc_gpio_is_valid_gpio(rxGpio)) {
    rtc_gpio_init(rxGpio);
    rtc_gpio_set_direction(rxGpio, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(rxGpio);
    rtc_gpio_pullup_en(rxGpio);
    rtc_gpio_hold_en(rxGpio);
  } else {
    pinMode(Config::CHATPAD_RX_PIN, INPUT_PULLUP);
  }
}

void enterDeepSleepFromLightStage() {
  if (!prepareGreenDeepSleepWakeInput()) {

    setCpuFrequencyMhz(80);
    delay(20);
    esp_restart();
  }

  prepareChatpadPinsForDeepSleep();

  if (Config::DEBUG_LOG) {
    Serial.println("Entering DEEP SLEEP: Green/D2 wake; D8 RTC-HIGH; D9 RTC input pull-up");
    Serial.flush();
  }

  esp_deep_sleep_start();
}

void prepareUnusedPinsForMaximumLightSleep() {
  const int pins[] = {
      Config::CHATPAD_TX_PIN,
      Config::CHATPAD_RX_PIN,
      Config::SUPPLY_SENSE_PIN,
      LEDR,
      LEDG,
      LEDB,
  };

  for (const int arduinoPin : pins) {
    const gpio_num_t gpio =
        static_cast<gpio_num_t>(digitalPinToGPIONumber(arduinoPin));

    if (gpio < GPIO_NUM_0 || gpio >= GPIO_NUM_MAX) continue;

    gpio_set_direction(gpio, GPIO_MODE_DISABLE);
    gpio_set_pull_mode(gpio, GPIO_FLOATING);

    gpio_sleep_sel_en(gpio);
    gpio_sleep_set_direction(gpio, GPIO_MODE_DISABLE);
    gpio_sleep_set_pull_mode(gpio, GPIO_FLOATING);
  }
}

void enterLowPowerSleep() {
  if (digitalRead(Config::GREEN_BUTTON_PIN) == LOW) return;

  setCpuFrequencyMhz(80);

  clearLatchedModifiers();
  keyboard.releaseAll();

  setPeopleLed(false);
  sendChatpadCommand(ChatpadProtocol::CMD_BACKLIGHT_OFF);
  sendChatpadCommand(ChatpadProtocol::CMD_LED_STATE_BASE);

  keyboard.beforeSleep();
  delay(250);
  keyboard.end();
  delay(500);
  bleWasPaired = false;

  if (!prepareGreenLightSleepWakeInput()) {
    keyboard.begin();

    if (slotNumberValid(activeSlot) && hostSlots[slotIndex(activeSlot)].valid) {
      restrictAdvertisingToSlot(activeSlot);
    } else if (slotMode == SlotMode::PairingNew) {
      openAdvertisingForPairing();
    }

    initialiseChatpad();
    return;
  }

  if (Config::DEBUG_LOG) {
    Serial.printf("Entering MAX LIGHT SLEEP at %lu ms: Chatpad UART/A0/RGB high-Z; Green/D2 only wake\n",
                  static_cast<unsigned long>(effectiveSleepIdleMs()));
    Serial.flush();
  }

  chatpadSerial.end();
  prepareUnusedPinsForMaximumLightSleep();

  esp_sleep_pd_config(ESP_PD_DOMAIN_CPU, ESP_PD_OPTION_OFF);

  Serial.end();
  esp_light_sleep_start();

  setCpuFrequencyMhz(80);
  delay(20);
  esp_restart();
}

// -----------------------------------------------------------------------------
// Arduino entry points
// -----------------------------------------------------------------------------
void setup() {
  const gpio_num_t greenGpio =
      static_cast<gpio_num_t>(digitalPinToGPIONumber(Config::GREEN_BUTTON_PIN));
  const gpio_num_t chatpadTxGpio =
      static_cast<gpio_num_t>(digitalPinToGPIONumber(Config::CHATPAD_TX_PIN));
  const gpio_num_t chatpadRxGpio =
      static_cast<gpio_num_t>(digitalPinToGPIONumber(Config::CHATPAD_RX_PIN));
  const esp_sleep_wakeup_cause_t bootWakeCause = esp_sleep_get_wakeup_cause();
  const bool wokeFromDeepGreen = bootWakeCause == ESP_SLEEP_WAKEUP_EXT0;

  if (wokeFromDeepGreen) {
    gpio_deep_sleep_hold_dis();

    if (rtc_gpio_is_valid_gpio(chatpadTxGpio)) {
      rtc_gpio_hold_dis(chatpadTxGpio);
      rtc_gpio_deinit(chatpadTxGpio);
    } else {
      gpio_hold_dis(chatpadTxGpio);
    }

    if (rtc_gpio_is_valid_gpio(chatpadRxGpio)) {
      rtc_gpio_hold_dis(chatpadRxGpio);
      rtc_gpio_deinit(chatpadRxGpio);
    }
  }

  if (wokeFromDeepGreen && rtc_gpio_is_valid_gpio(greenGpio)) {
    rtc_gpio_deinit(greenGpio);
  }

  pinMode(Config::GREEN_BUTTON_PIN, INPUT_PULLUP);

  if (digitalRead(Config::GREEN_BUTTON_PIN) == LOW) {
    while (digitalRead(Config::GREEN_BUTTON_PIN) == LOW) {
      delay(5);
    }
    delay(20);
  }

  greenButtonDown = false;
  chatpadReportedModifiers = 0;

  setCpuFrequencyMhz(Config::CPU_MHZ);

  pinMode(LEDR, OUTPUT);
  pinMode(LEDG, OUTPUT);
  pinMode(LEDB, OUTPUT);
  digitalWrite(LEDR, HIGH);
  digitalWrite(LEDG, HIGH);
  digitalWrite(LEDB, HIGH);

  if (Config::DEBUG_LOG) {
    Serial.begin(115200);
    delay(100);
    Serial.println("Steam Chatpad starting");
    Serial.println("Green button source: dedicated D2 active-LOW input");
    Serial.println("Wake source: Green/D2 only");
    if (wokeFromDeepGreen) {
      Serial.println("Wake confirmed: Green/D2 from DEEP SLEEP -> fresh boot");
    }
  }

  loadHostSlots();

  if (Config::DEBUG_LOG) {
    Serial.printf("Power mode %u: backlight %lu ms; sleep %lu ms; battery reporting %s\n",
                  static_cast<unsigned>(powerMode),
                  static_cast<unsigned long>(effectiveBacklightIdleMs()),
                  static_cast<unsigned long>(effectiveSleepIdleMs()),
                  batteryReportingEnabled ? "ON" : "OFF");
  }

  analogReadResolution(12);
  analogSetPinAttenuation(Config::SUPPLY_SENSE_PIN, ADC_11db);
  if (batteryReportingEnabled) {
    latestSupplyVoltage = readSupplyVoltage();
  }

  chatpadSerial.begin(Config::CHATPAD_BAUD, SERIAL_8N1,
                      Config::CHATPAD_RX_PIN, Config::CHATPAD_TX_PIN);

  keyboard.setLogLevel(HIDLogLevel::Off);
  keyboard.setTxPower(configuredBluetoothTxPowerLevel());
  keyboard.setTapDelay(Config::HID_TAP_MS);
  keyboard.setKeyGap(Config::HID_GAP_MS);
  keyboard.begin();
  publishBatteryLevel();
  lastBatteryReportAt = batteryReportingEnabled ? millis() : 0;

  reconcileSlotsWithNimbleBonds();

  if (activeSlot != 0 && hostSlots[slotIndex(activeSlot)].valid) {

    restrictAdvertisingToSlot(activeSlot);
    slotMode = SlotMode::Normal;
  } else {

    pendingPairSlot = 1;
    slotMode = SlotMode::PairingNew;
    slotModeEnteredAt = millis();
    openAdvertisingForPairing();
  }

  initialiseChatpad();

  pairingShowGreen = true;
  pairingLedAt = millis() + Config::PAIRING_LED_STEP_MS;
  lastBatterySampleAt = batteryReportingEnabled ? millis() : 0;
}

void loop() {

  serviceGreenButton();
  readChatpad();
  serviceSettingsMenu();
  serviceKeyRepeat();
  serviceHostSlots();
  serviceChatpadIndicators();
  servicePeopleSlotIndicator();
  serviceBleAdvertisingPower();
  serviceChatpadPower();
  serviceSupplyMonitor();
  serviceSleepTimer();

  delay(5);
}
