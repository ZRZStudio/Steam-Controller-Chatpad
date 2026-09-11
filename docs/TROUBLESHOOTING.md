# Troubleshooting

[← Project home](../README.md) · [Documentation](README.md) · [Getting started](GETTING_STARTED.md)

This firmware powers the chatpad conversion featured in the [full OG Steam Controller (2015) upgrade and modification video](https://www.youtube.com/watch?v=59sTsHHFOmE) on the **ZRZ YouTube channel**.

<p align="center">
  <a href="https://www.youtube.com/watch?v=59sTsHHFOmE">
    <img src="assets/video-thumbnail.png" alt="Watch the full OG Steam Controller (2015) upgrade and modification video on the ZRZ YouTube channel" width="480">
  </a>
</p>

Check one change at a time and keep a record of the working software versions.

## Compilation or upload fails

| Symptom | Check |
| :--- | :--- |
| `HijelHID_BLEKeyboard.h` not found | Install HijelHub's **HijelHID_BLEKeyboard** library. |
| `NimBLEDevice.h` not found | Install **NimBLE-Arduino**. |
| Missing BLE methods, types, or signatures | Compare installed library and board-core versions with the [setup guide](GETTING_STARTED.md#install-the-software). Save the first compiler error. |
| Board pin names or GPIO helpers missing | Confirm the target is **Arduino Nano ESP32** and the selected core provides that board. |
| Arduino asks to move the sketch | Keep `SteamChatpad.ino` in the `SteamChatpad` folder. |
| No upload port | Check the USB data cable, selected board, and port. |

The repository does not yet record a project-tested dependency matrix. Report the exact versions if compilation fails.

## Keyboard does not appear in Bluetooth settings

1. Press and release **Green** to wake it.
2. Look for **`steam chatpad`**.
3. If a host is already saved, [pair into a chosen slot](CONTROLS.md#pair-or-replace-a-host). Normal reconnection can be restricted to the active saved host.
4. If the host has a stale keyboard entry, remove it in that host's Bluetooth settings, then pair again.

The default inactivity timeout is 25 seconds, so wake the Chatpad again if a scan takes longer.

## Paired, but no keys arrive

- Open a plain text editor and wait for Bluetooth to reconnect.
- Exit settings with Orange until you return to normal typing; the menu consumes key input.
- Confirm the active host slot is the device you are using.
- Check crossed UART wiring: Chatpad TX → D9 and Chatpad RX → D8.
- Verify power and common ground.

If only Green-related functions fail, check the isolated switch on D2; this firmware does not use the matrix-reported Green bit.

## Wrong characters or missing symbols

Set the host layout to **English (United Kingdom)**. US layouts can change punctuation such as `@`, quotation marks, and `#`.

Some extended symbols and accents use Windows Alt+numpad sequences. These are not portable Unicode input and may vary by OS, code page, and application. Include the affected key, layer, and host in a report. See [symbol behaviour](CONTROLS.md#symbols-and-host-layout).

## Green does not wake the Chatpad

Check the [isolated Green-switch wiring](WIRING.md#isolated-green-switch). D2 should be HIGH when released and LOW when pressed. Release it after pressing: startup waits while it remains low.

Only this dedicated input is configured for wake. A short to ground can prevent both sleep entry and normal startup.

## Settings close or a timeout saves unexpectedly

The menu exits after **30 seconds** without input. Numeric timeout fields automatically save **three seconds after the last digit**. Use Backspace to correct a number while editing, or Orange to go back before it is accepted.

Release Green + Orange within the desired hold window. Holding beyond seven seconds requests sleep rather than opening settings.

## Backlight goes out after about six seconds

This is the Chatpad's continuous-backlight limit. Longer software timeouts do not override it. The firmware avoids repeated refresh pulses because those can produce periodic blinking. See [backlight behaviour](CONFIGURATION.md#backlight-limitation).

## Battery stays at 100% or seems inaccurate

Fixed **100%** is the default when measured reporting is disabled.

Measured mode requires the A0 divider and reads the regulated rail. Check the wiring and [calibrate the thresholds](CONFIGURATION.md#supply-monitoring) for the actual supply. Leave reporting disabled if the circuit is absent.

## Still stuck?

[Open a bug report](https://github.com/ZRZStudio/Steam-Controller-Chatpad/issues/new?template=bug_report.md) with the firmware commit, board-core and library versions, host OS/layout, exact reproduction steps, and first compiler error or a short relevant log. For electrical issues, include relevant wiring details or a clear photo.


