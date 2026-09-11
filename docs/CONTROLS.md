# Controls and settings

## Modifiers

Shift, Green, and Orange support three interaction styles:

- **Tap:** applies to the next key only.
- **Hold:** remains active while physically held.
- **Double tap:** latches until cancelled or toggled off.

Pressing a different modifier cancels another one-shot or latched modifier.

## Green / Orange shortcuts

Some common combinations are:

| Combination | Action |
|---|---|
| Green or Orange + Left Arrow | Up Arrow |
| Green or Orange + Right Arrow | Down Arrow |
| Green + Backspace | Delete |
| Orange + Backspace | Escape |
| Green + Space | Tab |
| Orange + Space | Shift + Tab |
| Green + Enter | Home |
| Orange + Enter | End |

## Green + Orange hold actions

| Hold time | Action |
|---|---|
| 3 seconds | Select a remembered Bluetooth host |
| 5 seconds | Open the Chatpad settings menu |
| 7 seconds | Enter low-power sleep |

Release the buttons after the desired hold duration to trigger the action.

## Settings menu

While the settings menu is open, Chatpad input is consumed by the firmware and is not sent to the connected host.

### Main menu

1. Bluetooth
2. Power
3. Battery

### Bluetooth

- Pair or replace host slot 1, 2, or 3.
- Forget an individual host slot.
- Erase all stored host slots.
- Select Low, Medium, or High BLE transmit power.
- Select Eco, Balanced, or Fast reconnect behaviour.

### Power

Profiles:

| Profile | Backlight timeout | Sleep timeout |
|---|---:|---:|
| Super Power Saver | 3 s | 12 s |
| Power Saver | 6 s | 25 s |
| Normal | 10 s | 60 s |

Backlight and sleep times can also be overridden from the menu.

### Battery

Battery reporting can be left at a fixed 100% value or switched to the experimental measured mode when the optional A0 voltage-divider circuit is fitted.

## Backlight note

The original Chatpad controller limits a continuous keyboard-backlight period to roughly six seconds. The firmware deliberately avoids repeatedly retriggering it because doing so creates a visible periodic blink.
