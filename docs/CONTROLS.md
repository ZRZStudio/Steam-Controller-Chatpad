# Controls and settings

[← Project home](../README.md) · [Documentation](README.md) · [Configuration](CONFIGURATION.md)

The firmware uses an **English (United Kingdom)** host layout. Green and Orange select symbol layers and provide navigation shortcuts.

## Modifier keys

| Gesture | Behaviour |
| :--- | :--- |
| Tap Shift, Green, or Orange | Applies the modifier to the next key. |
| Hold a modifier while pressing keys | Keeps it active while held. |
| Double-tap a modifier | Latches it until toggled off or cancelled. |

Pressing another modifier cancels an existing one-shot or latched modifier. **Shift + Orange** toggles the host's Caps Lock.

## Navigation shortcuts

| Keys | Output |
| :--- | :--- |
| Green or Orange + Left Arrow | Up Arrow |
| Green or Orange + Right Arrow | Down Arrow |
| Green + Backspace | Delete |
| Orange + Backspace | Escape |
| Green + Space | Tab |
| Orange + Space | Shift + Tab |
| Green + Enter | Home |
| Orange + Enter | End |

## Hold shortcuts

Hold **Green + Orange together**, then **release both**. The duration selects the action:

| Hold duration | Action |
| :--- | :--- |
| Under 3 seconds | No hold-menu action. |
| 3 to under 5 seconds | Enter saved-host selection. |
| 5 to under 7 seconds | Open settings. |
| 7 seconds or longer | Request light sleep. |

The action is selected when the chord ends, not as each threshold passes. For manual sleep, release Green; the firmware will not enter sleep while D2 is held low.

**Wake:** press and release the isolated Green switch. The firmware restarts and reconnects to the active saved host.

## Switch between saved hosts

1. Hold Green + Orange for **3 to under 5 seconds**, then release.
2. Press **1**, **2**, or **3** within **10 seconds**.
3. Wait for the selected host to reconnect.

The People indicator flashes the slot number. Selecting an empty slot cancels selection without starting pairing. Selection closes after 10 seconds if no slot is chosen.

Slots remember hosts for switching; they do not provide simultaneous typing to three devices.

## Use the settings menu

Open settings with the **5 to under 7 second** hold shortcut.

| Input | Action |
| :--- | :--- |
| Number keys | Enter a menu or choose a value. |
| Green | Confirm the pending selection. |
| Orange | Cancel / go back one level; at the main menu, exit. |
| Backspace | Remove the last digit while editing a numeric timeout. |
| No input for 30 seconds | Exit settings. |

Menu input is consumed by the firmware and is not sent to the host. Numeric timeouts also **save automatically three seconds after the last digit**.

### Menu map

Paths are number keys pressed **in sequence after opening settings**. Finish with Green where indicated.

| Path | Setting | Choice / confirmation |
| :--- | :--- | :--- |
| **1 → 1** | Pair / replace host | Slot **1–3**, then Green. |
| **1 → 2** | Forget host | Slot **1–3**, then Green. |
| **1 → 2 → 4** | Forget all hosts | Hold Green for **3 seconds**. |
| **1 → 3** | Bluetooth transmit power | **1** Low · **2** Medium · **3** High, then Green. |
| **1 → 4** | Reconnect mode | **1** Eco · **2** Balanced · **3** Fast, then Green. |
| **2 → 1** | Power profile | **1** Super Power Saver · **2** Power Saver · **3** Normal, then Green. |
| **2 → 2** | Backlight timeout | Enter seconds (**0–30**), then Green or wait 3 seconds. |
| **2 → 3** | Sleep timeout | Enter seconds (**1–120**), then Green or wait 3 seconds. |
| **2 → 4** | Reset timeout overrides | Green restores the selected profile's timeouts. |
| **3 → 1** | Battery reporting | **1** Fixed 100% · **2** Measured, then Green. |
| **3 → 2** | Report battery now | Green. In fixed mode, reports 100%. |

Backlight **0** disables the idle backlight. Values above 30 are clamped to 30. A sleep value of **0**, or above 120, is stored as **120 seconds**; zero does not disable sleep. Selecting a power profile clears custom timeouts.

### Pair or replace a host

1. Open settings.
2. Press **1 → 1 → slot number → Green**.
3. On the desired host, pair with **`steam chatpad`**.

Choosing an occupied slot removes that slot's previous bond before pairing. Other slots are retained. If the host already lists an old pairing for this keyboard, remove that entry before pairing again.

### Forget hosts

For one host, use **1 → 2 → slot number → Green**. If no saved hosts remain, the firmware opens fresh pairing into slot 1.

To clear all three slots, use **1 → 2 → 4**, then hold **Green for three seconds**. This removes saved host bonds and starts fresh pairing into slot 1. Other settings are retained.

### LED feedback

| Pattern | Meaning |
| :--- | :--- |
| Green and Orange flash together about once per second | Main settings menu. |
| Green flash count, followed by Orange flash count | Main-menu number, then submenu number. |
| Green and Orange briefly light together after a choice | A selection is ready for confirmation. |
| Green confirmation flash | Setting accepted. |
| Orange confirmation flash | Cancellation or unavailable selection, depending on the action. |
| People indicator flashes 1, 2, or 3 times | Slot-number feedback. |

For example, **two Green flashes followed by three Orange flashes** identifies **Power → Sleep timeout**.

## Symbols and host layout

Common punctuation targets a UK keyboard layout. Some extended symbols and composed accents are emitted as **Windows Alt+numpad sequences**, rather than Unicode text.

Those keys can depend on Windows settings, application behaviour, and code-page handling. Equivalent extended-symbol output is not established for SteamOS, Linux, macOS, or mobile hosts. Start with ordinary letters and punctuation when checking a new host.

For a mismatch, see [troubleshooting](TROUBLESHOOTING.md#wrong-characters-or-missing-symbols).
