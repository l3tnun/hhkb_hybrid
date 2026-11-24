# Usage (key operations, Bluetooth and power with the QMK firmware)

> English translation of [../usage.md](../usage.md). If the two differ, the Japanese version is authoritative.

This describes how to operate the keyboard after flashing the QMK from this repository. It reproduces the operation of the stock firmware as closely as possible. For what you can change with the VIA-enabled build, see [via.md](via.md).

## Key layout (default keymap)

- **Base layer**: the standard HHKB layout for the US layout. While you hold the `Fn` key at the bottom right, the Fn layer is active.
- **Fn layer**:

| Row | Key pressed together with Fn → input |
|---|---|
| Number row | `Esc`→Esc, `1`–`=`→F1–F12, `\`→Insert, `` ` ``→Delete |
| Tab row | `Tab`→Caps Lock, `I`→Print Screen, `O`→Scroll Lock, `P`→Pause, `[`→↑, `Backspace`→Backspace |
| Control row | `A`→Volume down, `S`→Volume up, `D`→Mute, `H`→Keypad `*`, `J`→Keypad `/`, `K`→Home, `L`→Page Up, `;`→←, `'`→→, `Enter`→Keypad Enter |
| Shift row | `N`→Keypad `+`, `M`→Keypad `-`, `,`→End, `.`→Page Down, `/`→↓ |

- The initial layout of the VIA-enabled build is the same (you can change it with VIA).
- Of the DIP switches on the back of the keyboard, **SW6 (disable auto-sleep) is the only one** QMK reads. SW1–SW5 (the stock key layout modes and so on) are ignored, and the layout is fixed to the table above.

## Output (USB / Bluetooth)

- **Automatic** (default): outputs to USB if the keyboard is connected to a host over USB, and to Bluetooth otherwise (on battery power, or connected to a charger only).
- When you select a Bluetooth slot with `Fn`+`Ctrl`+`1`–`4`, the keyboard outputs to Bluetooth even while connected over USB.
- When you switch slots, the keyboard sends the release of the keys that were held down to the host it is switching away from, and then changes the connection target ([reverse-engineering/bluetooth-protocol.md](reverse-engineering/bluetooth-protocol.md) §11).
- `Fn`+`Ctrl`+`0` returns to automatic (the blue LED lights for 1 second).
- When powered on (on battery power), the keyboard reconnects to the slot used last. For an unregistered slot it does not start pairing automatically (register it starting from `Fn`+`Q`).

## Bluetooth operations

| Operation | Behavior |
|---|---|
| `Fn`+`Ctrl`+`1`–`4` | Switch to registered slot 1–4 (the blue LED blinks as many times as the slot number) |
| `Fn`+`Q` | Enter pairing mode (the blue LED blinks about 2.5 times per second) |
| `Fn`+`Ctrl`+`1`–`4` in pairing mode | Register a new host in that slot (the blue LED blinks about 5 times per second. Select the HHKB on the host, and if asked for a passkey, type it on the keyboard) |
| `Fn`+`X` in pairing mode | Cancel pairing and reconnect to the previous slot |
| In pairing mode, `1`–`4` while holding `Fn`+`Ctrl`+`Delete` (the rightmost key of the Tab row, the key with the Backspace/Delete legends) | Delete the registration of that slot |

- Pairing mode is cancelled automatically after 60 seconds.
- **Giving up on reconnecting (same as the stock firmware)**: when the connection target is not found, or when the connection is lost, the keyboard lights the blue LED and tries to reconnect for about 60 seconds. If that fails, it puts the nRF (the Bluetooth module) on standby and turns the LED off.
  - On battery power and DIP SW6 OFF: power off (the same STOP as auto-sleep)
  - DIP SW6 ON, or on USB power: the keyboard does not power off and stays awake. Pressing a key, or a short press of the power button (0.1–2 seconds), resets the keyboard (the same as cycling the power) and reconnects to the slot used last. The key you pressed is not input (with an iPad, it reconnected in a few seconds)
  - For the 60 seconds right after the connection is lost, the Bluetooth module tries to reconnect by itself, as with the stock firmware (the keyboard does not specify the connection target again).
- **Automatic reset when the Bluetooth module does not respond (same as the stock firmware)**: while outputting to Bluetooth, if the Bluetooth module does not pick up a key input or a request to switch the connection target for 1 second or more, the keyboard resets itself automatically (the same as cycling the power; it reconnects to the slot used last in a few seconds). It does not reset while outputting to USB.
- Resetting on a key press or a short press of the power button after giving up on reconnecting is behavior specific to QMK. The stock firmware only specifies the connection target again, but after a long time (an example: Bluetooth on an iPad turned OFF for 11 minutes) that did not reconnect, while cycling the power did.
- Mouse keys, NKRO and the power/sleep system keys cannot be sent over Bluetooth. Media keys are limited to those the stock Bluetooth firmware supports.
- With the VIA-enabled build, you can assign these operations to any keys you like, and disable the stock-compatible combinations ([via.md](via.md)).
- Whether the pairing information remains after flashing firmware is not guaranteed (there are cases where input over Bluetooth worked after flashing without pairing again). If it does not connect, register again starting from `Fn`+`Q`.

## Power and auto-sleep (on battery power)

| Operation / state | Behavior |
|---|---|
| Long press the power button for 2 seconds | Power off (the orange LED lights and then goes out). Bluetooth is disconnected before stopping (as with the stock firmware, it waits up to 3 seconds for the disconnection) |
| Short press of the power button (0.1–2 seconds, on battery power) | If connected, the blue LED blinks as many times as the slot number. If the keyboard has given up on reconnecting Bluetooth, it retries the reconnection |
| While off, press the power button for 1–10 seconds and release it | The keyboard starts |
| Connect USB while off | The keyboard starts |
| No operation for a while | Auto-sleep (30 minutes by default. In the VIA-enabled build it can be changed with VIA). Wake with the power button or by connecting USB |
| DIP SW6 ON | No auto-sleep. Even if the Bluetooth reconnection fails, the keyboard does not power off, and it reconnects on a key press or a short press of the power button |
| On USB power | The power button and auto-sleep are disabled |

- Pressing a key does not wake the keyboard from sleep (same as the stock firmware).
- The battery level is reported to the Bluetooth host in the same 4 steps as the stock firmware (100/15/5/0 %). Check it in the battery indicator of the OS or similar. It is not shown on the keyboard's LEDs.

### Low-power operation on battery power

On battery power (no USB power), the keyboard works as follows to make the batteries last longer. On USB power it works as before.

- It runs the MCU at 4 MHz (32 MHz on USB power) and stops the USB circuitry. The stock firmware also runs at 4 MHz on battery power.
- Keys are scanned once every 5 ms, and the MCU rests in between (the stock firmware scans about every 11 ms).
- Between scans, the key reading circuit (OpAmp and ADC) is stopped.
- For about 1 second after startup (power on) and right after USB is unplugged or plugged in (about 50 ms), it keeps running at 32 MHz without switching. When USB is connected while on battery power, it returns to 32 MHz and restarts USB.

If this behavior causes problems (missed or wrong key input, Bluetooth disconnecting and so on), flashing firmware built with `-e HHKB_LOW_POWER=no` returns the keyboard to the previous behavior ([build.md](build.md#build-options)).

**How to compare battery life** (a method that needs no ammeter): record the date and time you put in new batteries, and compare the number of days until the level shown on the Bluetooth host (the battery indicator of the OS or similar) drops from 100 % to 15 %, or until you can no longer type. When comparing, keep the battery brand, the DIP switches, the number of connected devices and the hours of use per day the same. If you have the same record for the stock firmware, you can compare against it.

## LED

| Indication | Meaning |
|---|---|
| Blue stays lit | Reconnecting over Bluetooth, or switching the connection target (goes out after about 60 seconds. After a failure, see "Giving up on reconnecting" above.) |
| Blue blinks (about 2.5 times per second) | Pairing mode |
| Blue blinks (about 5 times per second) | Waiting for registration (stops once registered and connected) |
| Blue blinks as many times as the slot number | The slot was switched, or a short press of the power button showed the connected slot |
| Blue lights for 1 second | Output was returned to automatic (USB first) |
| Orange | Power-off operation in progress |

A detailed analysis of the LEDs of the stock firmware is in [reverse-engineering/led-indication.md](reverse-engineering/led-indication.md).

## Using the keyboard with an OS set to the JIS layout

There is a correction feature that lets you input symbols as shown by the US legends while the OS layout setting stays JIS. Switch it ON/OFF with `Ctrl`+`Alt`+`Shift`+`J`. For details, see [jis-us-toggle.md](jis-us-toggle.md).

## Entering the mode for flashing (update mode)

- **bootmagic**: with the power off (or with the batteries removed), connect USB while holding `Esc`. Keep holding it for a few seconds and then release it. The keyboard resets the QMK settings and then enters the update mode, and keys do nothing. If `python3 tools/flash.py --list` shows `HHKB-Hybrid`, it is in the update mode.
  - If you plug in USB while the keyboard is powered on and on battery power, it does not restart, so bootmagic does not work. First long press the power button for 2 seconds to turn it off.
- Normal flashing does not need bootmagic. `tools/flash.py` switches to the update mode automatically.
