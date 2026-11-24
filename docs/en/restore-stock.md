# Restoring the stock firmware (no disassembly)

> English translation of [../restore-stock.md](../restore-stock.md). If the two differ, the Japanese version is authoritative.

You can go back from QMK to the stock firmware **without disassembly**. This procedure is verified on hardware.

## Preparation

- The stock HFB `local/HHKB800_FW_A048.hfb` (see [setup.md](setup.md) for obtaining and verifying it)
- Connect only the one keyboard you are going to flash over USB, and close the VIA page

## How it works, and cautions (if you were using the VIA-enabled build)

The VIA-enabled build (the `via` keymap) stores its keymap and settings in **the same EEPROM region as keymap table B of the stock firmware** (`0x08080700–0x08080CFF`). When the keyboard is returned to stock, it behaves as follows.

1. At startup, the bootloader first checks the valid mark of the stock keymap (`0x08080300`). If it is invalid, the bootloader rebuilds only keymap table A with the default values and sets the mark back to valid (it does not touch table B).
2. Next, the stock firmware sees the mark as valid and loads table A and **table B**.

If VIA data remains in table B, the stock firmware uses it as a keymap, so **many keys cannot be typed or produce characters different from their legends**. For this reason, when going back from the VIA-enabled build, table B must be returned to the stock state (zeros) before flashing. Procedures A and B below both do this automatically.

**Avoid entering the update mode with QMK's `QK_BOOT` key (the Reset / Boot key in VIA) to go back to stock.** That path is also used for updating QMK, so it does not erase table B.

The `default` keymap (without VIA support) does not use table B, so there is no problem whichever procedure you use to go back.

## Procedure A: flash with flash.py (recommended)

Do this while QMK is running on the keyboard.

```sh
python3 tools/flash.py --list                                             # check the FLASH TARGET (input1, 06 60 ff)
python3 tools/flash.py --device /dev/hidrawN local/HHKB800_FW_A048.hfb
```

- `flash.py` determines that the HFB is stock (does not contain QMK) and sends `AA AA F0` to QMK. The output shows `stock HFB on a QMK keyboard: clearing the stock keymap window (F0)`. The VIA-enabled build of QMK erases table B and then enters the update mode.
- With the default build (without VIA support) of QMK, `F0 not supported (non-VIA QMK build, window unused); using E0` is shown next. The default build does not use table B, so this is normal.
- With an old VIA-enabled build that does not have this command, `flash.py` aborts without flashing. First flash the latest VIA-enabled build, then run it again.
- It takes about 3 minutes. For what to do if it stops midway, see [troubleshooting.md](troubleshooting.md).

## Procedure B: enter the update mode with bootmagic, then flash

Use this when, for example, QMK is running but key input or Raw HID cannot be used.

### 1. Enter the update mode

1. If the keyboard is on battery power and turned on, turn it OFF with a 2-second long press of the power button (if USB is plugged in while the power is on, the keyboard does not restart, so bootmagic does not act).
2. With the USB cable unplugged, **press and hold Esc** at the top left of the keyboard.
3. Connect the USB cable while holding Esc, wait a few seconds, then release it.
4. If `python3 tools/flash.py --list` shows `product='HHKB-Hybrid' bcdDevice=0001` and keys do nothing, the keyboard is in the update mode.

bootmagic resets the QMK settings. With the VIA-enabled build it also returns table B to zeros.

### 2. Flash the stock HFB

The keyboard is in the update mode, so flash with `--resume`.

```sh
python3 tools/flash.py --list                                                   # check the FLASH TARGET (input2, 06 00 ff)
python3 tools/flash.py --resume --device /dev/hidrawN local/HHKB800_FW_A048.hfb
```

If USB recognition of the update mode fails and `FLASH TARGET` is not shown (`error -71` in the kernel log), unplug and replug USB without pressing Esc; the keyboard is recognized again, still in the update mode.

## Verification

A few seconds after flashing, the keyboard restarts and is recognized as the stock firmware. A single disconnect and reconnect may occur right after startup.

- `python3 tools/flash.py --list` shows `product='HHKB-Hybrid' bcdDevice=0001` (this is the same as the update mode, so judge by the key input in the next item)
- Keys type what their legends say, and the Fn layout is the stock one
  - **If the blue LED stays lit and keys cannot be typed:** the stock firmware keeps trying to reconnect over Bluetooth and its input is not going to USB (because the hosts registered for Bluetooth under QMK are carried over to the stock firmware). Switching the output to USB with **Fn + Ctrl + 0** makes input work.
- With USB unplugged, Bluetooth connection and input on battery work

## If key input is already broken

If you went back to stock from the VIA-enabled build without erasing table B and key input is broken on stock, you can fix it in the following order (flashing goes over HID, so the work can be done even though key input is broken).

1. While still on the stock firmware, flash the latest VIA-enabled build of QMK (the `FLASH TARGET` in `--list` is the stock firmware's `input2`. Do not add `--resume`).
2. Flash the stock HFB with Procedure A (table B is erased).
3. Check input on stock.

## Additional notes

- The ON/OFF setting of the JIS/US correction is stored in the least significant bit of the stock sleep-time setting byte. This bit remains after going back to stock, so if the correction was ON, the stock auto-sleep time may be off by 1 minute (e.g. 30 minutes → 31 minutes). Setting the sleep time again with the stock configuration tool resolves this ([jis-us-toggle.md](jis-us-toggle.md)).
- DFU is the last resort for when the keyboard can no longer be restored over HID (it requires shorting R6/R85 on the board, i.e. physical disassembly). Before that, try the checks in [troubleshooting.md](troubleshooting.md).
