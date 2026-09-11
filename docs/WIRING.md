# Wiring

## Required connections

| Xbox 360 Chatpad | Arduino Nano ESP32 | Purpose |
|---|---|---|
| TX | D9 / GPIO18 | Nano UART RX |
| RX | D8 / GPIO17 | Nano UART TX |
| 3.3 V | Regulated 3.3 V | Power |
| GND | GND | Common ground |

TX and RX are crossed because each device's transmitter connects to the other device's receiver.

> **Important:** The Chatpad and Nano ESP32 logic are 3.3 V. Verify the supply before connecting the hardware.

## Isolated Green button

For reliable wake-from-sleep behaviour, the Green modifier switch is isolated from the Chatpad key matrix and connected directly to the Nano ESP32.

| Green switch contact | Connection |
|---|---|
| Contact 1 | D2 |
| Contact 2 | GND |

D2 is configured with `INPUT_PULLUP`, so the switch is HIGH when released and LOW when pressed.

## Optional supply-voltage monitor

The firmware can monitor the regulated 3.3 V rail through A0.

```text
3.3 V ---- 47 kOhm ----+---- 47 kOhm ---- GND
                       |
                       +---- A0
                       |
                     100 nF
                       |
                      GND
```

This measures the regulated rail, not the raw battery cell. The thresholds in the firmware therefore need calibration for the regulator and battery arrangement used in a particular build.
