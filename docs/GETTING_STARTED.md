# Getting started

[← Project home](../README.md) · [Documentation](README.md) · [Wiring](WIRING.md) · [Controls](CONTROLS.md)

This firmware powers the chatpad conversion featured in the [full OG Steam Controller (2015) upgrade and modification video](https://www.youtube.com/watch?v=59sTsHHFOmE) on the **ZRZ YouTube channel**.

Prepare the software, upload the firmware, and pair your first host.

## Before you begin

You will need:

- **Arduino Nano ESP32** and a USB data cable.
- Original **Xbox 360 Chatpad**.
- Suitable **regulated 3.3 V supply** and common ground.
- **Isolated Green switch** connected to D2 and GND.
- Arduino IDE and a host with Bluetooth LE keyboard support.
- Optionally, two 47 kΩ resistors and a 100 nF capacitor for supply monitoring.

Read the [wiring guide](WIRING.md) first. This project requires soldering and modification of the Green-button circuit. For more details about the wider controller build, follow the video linked above.

## Download the source

1. [Download the current source ZIP](https://github.com/ZRZStudio/Steam-Controller-Chatpad/archive/refs/heads/main.zip).
2. Extract it to a normal folder.
3. Open `SteamChatpad/SteamChatpad.ino` in Arduino IDE.

Keep `SteamChatpad.ino` inside the `SteamChatpad` folder. Arduino expects the sketch and its containing folder to have matching names.

## Install the software

| Component | Installation / reference |
| :--- | :--- |
| Arduino IDE | Install from [Arduino's software page](https://www.arduino.cc/en/software/). |
| ESP32 board support | Install an ESP32 core that provides **Arduino Nano ESP32** and is compatible with the libraries below. See [Espressif's installation guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html). |
| `HijelHID_BLEKeyboard` | In Library Manager, search for **HijelHID**. Use [HijelHub's BLE keyboard library](https://github.com/HijelHub/HijelHID_BLEKeyboard). |
| `NimBLE-Arduino` | Search for **NimBLE-Arduino** in Library Manager. See the [upstream project](https://github.com/h2zero/NimBLE-Arduino). |

The HijelHID documentation consulted for this guide specifies **ESP32 Arduino core 3.x** and **NimBLE-Arduino 2.3.8 or later**. These are upstream requirements, not a verified build matrix for this sketch. Check the library's [current requirements](https://github.com/HijelHub/HijelHID_BLEKeyboard#requirements) before choosing versions.

> [!NOTE]
> A known-good combination of board-core and library versions has not yet been recorded in this repository. Keep the versions from a working installation. If a clean setup fails to compile, include the exact versions and first compiler error in a bug report.

## Select the board and upload

1. Select **Arduino Nano ESP32** as the board, rather than the classic Arduino Nano.
2. Select the USB port for your board.
3. Keep the named pins (`D2`, `D8`, `D9`, `A0`) in the sketch. See [pin numbering](WIRING.md#pin-numbering) if you adapt the hardware.
4. Run **Verify** to compile, then **Upload**.
5. Disconnect power before completing or changing the Chatpad wiring. Recheck the [connection table](WIRING.md#connection-table) before powering the assembled build.

The firmware sets its active CPU frequency to **80 MHz** itself.

## Pair your first host

With no valid active host saved, the firmware opens pairing into **slot 1**.

1. Power the assembled Chatpad. If it has slept, press and release **Green**.
2. Open Bluetooth settings on the host and add **`steam chatpad`**.
3. Complete the host's pairing flow.
4. Set that keyboard's input layout to **English (United Kingdom)**.
5. Open a text editor and try letters, numbers, and punctuation.

The default sleep timeout is **25 seconds**. If the device disappears while pairing, press and release Green and try again.

If a host is already saved, use the [pair / replace procedure](CONTROLS.md#pair-or-replace-a-host) to add a host to a chosen slot.

## Check the basic functions

| Check | Expected behaviour |
| :--- | :--- |
| Letters and numbers | Input reaches the text editor. |
| Green + Space | Sends Tab. |
| Orange + Backspace | Sends Escape. |
| Green + Orange for 5–under 7 seconds, then release | Opens settings; number keys are consumed by the menu. |
| Orange at the main settings menu | Returns to normal typing. |
| Wait for sleep, then press and release Green | Firmware restarts and reconnects to the saved host. |

Allow Bluetooth to reconnect after waking. Some extended symbols use Windows Alt+numpad input and may behave differently on other hosts.

## Next steps

- [Learn the shortcuts and pair more hosts](CONTROLS.md).
- [Choose a power profile](CONFIGURATION.md#power-profiles).
- [Troubleshoot a build](TROUBLESHOOTING.md).
