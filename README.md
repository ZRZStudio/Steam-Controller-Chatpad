# Steam Chatpad

Turn an original **Xbox 360 Chatpad** into a compact **Bluetooth LE keyboard** using an **Arduino Nano ESP32**.

This firmware was developed for a Steam Controller chatpad conversion, but the Chatpad-to-BLE portion can also be used in other projects that need a small physical keyboard.

## Features

- Original Xbox 360 Chatpad support over 19,200-baud UART
- Bluetooth LE HID keyboard output
- English (United Kingdom) keyboard mapping
- Green and Orange symbol layers
- Tap, hold, and double-tap modifier behaviour
- Three remembered Bluetooth host slots
- Chatpad-operated settings menu
- Adjustable BLE transmit power and reconnect behaviour
- Configurable power-saving profiles
- Low-power light sleep with dedicated button wake
- Keyboard backlight and status-LED control
- Optional regulated-supply monitoring and BLE battery reporting

## Hardware

- Arduino Nano ESP32 (ESP32-S3)
- Original Xbox 360 Chatpad
- Regulated 3.3 V supply
- Optional 47 kOhm + 47 kOhm voltage divider and 100 nF capacitor for supply monitoring

## Wiring

| Chatpad | Arduino Nano ESP32 |
|---|---|
| TX | D9 / GPIO18 (RX) |
| RX | D8 / GPIO17 (TX) |
| 3.3 V | Regulated 3.3 V |
| GND | GND |

The Green modifier switch is isolated from the Chatpad key matrix and connected between **D2 and GND**. D2 uses `INPUT_PULLUP` and also acts as the wake input during light sleep.

See [docs/WIRING.md](docs/WIRING.md) for the full wiring notes and optional voltage-monitor circuit.

## Software requirements

- Arduino IDE or another compatible Arduino build environment
- Arduino ESP32 board support with **Arduino Nano ESP32** selected
- **HijelHID** / `HijelHID_BLEKeyboard`
- **NimBLE-Arduino**

## Uploading

1. Install Arduino IDE and the Arduino ESP32 board package.
2. Install the required HijelHID and NimBLE-Arduino libraries.
3. Open `SteamChatpad/SteamChatpad.ino`.
4. Select **Arduino Nano ESP32** as the target board.
5. Compile and upload.
6. Connect the Chatpad only after verifying the required 3.3 V wiring.

## Basic controls

Shift, Green, and Orange can be tapped, held, or double-tapped. Green + Orange provides the main firmware shortcuts:

| Hold | Action |
|---:|---|
| 3 seconds | Select remembered Bluetooth host |
| 5 seconds | Open settings |
| 7 seconds | Enter low-power sleep |

See [docs/CONTROLS.md](docs/CONTROLS.md) for the complete settings and shortcut guide.

## Power behaviour

The firmware runs the ESP32-S3 at 80 MHz while active and uses light sleep after the configured inactivity timeout. During sleep, Chatpad keep-alives stop and its UART pins are placed in a high-impedance state. The isolated Green button on D2 is used to wake the system.

Three default power profiles are included:

| Profile | Backlight | Sleep |
|---|---:|---:|
| Super Power Saver | 3 s | 12 s |
| Power Saver | 6 s | 25 s |
| Normal | 10 s | 60 s |

## Backlight behaviour

The original Xbox 360 Chatpad hardware limits one continuous keyboard-backlight period to roughly six seconds. The firmware does not continuously retrigger the lamp because doing so produces a noticeable periodic blink.

## Battery reporting

Supply monitoring is optional. When enabled, A0 reads the regulated 3.3 V rail through a 47 kOhm / 47 kOhm divider. This is **not a direct battery-voltage measurement**, so the included thresholds are intended as starting points and should be calibrated for the hardware used.

Battery reporting can be disabled in the on-device settings menu, in which case the BLE HID battery value remains fixed at 100%.

## Project structure

```text
Steam-Controller-Chatpad/
├── SteamChatpad/
│   └── SteamChatpad.ino
├── docs/
│   ├── CONTROLS.md
│   └── WIRING.md
├── .github/
│   └── ISSUE_TEMPLATE/
├── CHANGELOG.md
├── CONTRIBUTING.md
├── LICENSE
└── README.md
```

## Releases

For normal use, download the latest GitHub release and open `SteamChatpad.ino` in Arduino IDE. Developers can clone the repository to follow changes or contribute improvements.

## Disclaimer

This is an unofficial community project and is not affiliated with or endorsed by Valve, Microsoft, Xbox, or Arduino.

Hardware modifications are performed at your own risk. Check voltage levels and wiring before applying power.

## Licence

Released under the [MIT License](LICENSE).
