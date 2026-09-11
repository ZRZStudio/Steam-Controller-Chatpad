# Changelog

This firmware powers the chatpad conversion featured in the [full OG Steam Controller (2015) upgrade and modification video](https://www.youtube.com/watch?v=59sTsHHFOmE) on the **ZRZ YouTube channel**.

<p align="center">
  <a href="https://www.youtube.com/watch?v=59sTsHHFOmE">
    <img src="docs/assets/video-thumbnail.png" alt="Watch the full OG Steam Controller (2015) upgrade and modification video on the ZRZ YouTube channel" width="480">
  </a>
</p>

All notable public changes to Steam Chatpad will be documented here.

## Unreleased

### Documentation

- Linked the full ZRZ OG Steam Controller (2015) build video throughout the Markdown documentation.
- Redesigned the README with a custom banner, project video link, and source downloads.
- Added setup, configuration, and troubleshooting guides.
- Expanded wiring notes, menu paths, modifier shortcuts, and LED feedback.
- Clarified UK layout requirements, host-dependent symbols, and supply-monitor limitations.
- Added contributor guidance, a pull request template, and detailed issue templates.
- Firmware behaviour is unchanged from v1.0.0.

## 1.0.0 - 2026-09-11

Initial public release.

### Features
- Xbox 360 Chatpad serial decoding at 19,200 baud.
- Bluetooth LE HID keyboard output through an Arduino Nano ESP32.
- UK keyboard layout mapping with Green and Orange symbol layers.
- Tap, hold, and double-tap modifier behaviour.
- Three remembered Bluetooth host slots.
- On-device settings menu using the Chatpad itself.
- Adjustable Bluetooth transmit power and reconnect behaviour.
- Adjustable power profiles and low-power light sleep.
- Chatpad keyboard-backlight and indicator-LED control.
- Optional regulated-supply monitoring and BLE battery reporting.
- Dedicated isolated Green-button wake input on D2.
