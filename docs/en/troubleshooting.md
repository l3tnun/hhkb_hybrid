# Troubleshooting

> English translation of [../troubleshooting.md](../troubleshooting.md). If the two differ, the Japanese version is authoritative.

## Check these first

- **Do not leave the keyboard you are flashing paired over Bluetooth with the PC used for flashing.** It can cause the transfer to stop midway ("The transfer stops at the same position every time" below).

- **As long as the keyboard shows up on USB, disassembly (DFU) is not needed.** If `04fe:0021` appears in `lsusb -d 04fe:`, the keyboard can be flashed again over HID.
- Checking the state: `python3 tools/flash.py --list` ([device-state.md](device-state.md))
- USB errors can be checked in the kernel log: `journalctl -k -f` (or `sudo dmesg -w`)

## What not to do

- Flashing with `--allow-crc-mismatch` (the HFB may be corrupted; redo it from the conversion)
- Trying to flash to hidraw nodes other than the `FLASH TARGET` one after another
- Unplugging the USB cable or removing the batteries during flashing (except when `flash.py` asks you to unplug and replug)
- Flashing with several HHKBs connected at the same time
- Proceeding to DFU (disassembly) although `04fe:0021` is visible on USB

## What to do for each flash.py message

| Message | Meaning | Keyboard state | What to do |
|---|---|---|---|
| `no read/write permission for /dev/hidrawN; set up the udev rule (docs/setup.md)` / `PermissionError` | No access permission for the hidraw | Unchanged | Set up the udev rule in [setup.md](setup.md), then unplug and replug the cable |
| `expected exactly one HHKB connected, found N.` | There are 0 HHKBs, or 2 or more | Unchanged | Connect only the one keyboard you are going to flash |
| `specify --device; use --list to find the FLASH TARGET` | The flash target cannot be determined automatically | Unchanged | Pass the `FLASH TARGET` from `--list` to `--device` |
| `/dev/hidrawN is not the flash target interface (descriptor ...)` | An interface such as the keyboard input one was specified | Unchanged | Specify the `FLASH TARGET` from `--list` |
| `--resume is only for a keyboard already in the PFU update mode; ...` | `--resume` was given for QMK | Unchanged | Run again without `--resume` |
| `CRC mismatch: stored=0x.... computed=0x....` | The HFB file is corrupted | Unchanged | Convert it again with `qmk_to_hfb.py`. For the stock HFB, obtain it again and check the sha256 |
| `this QMK VIA build cannot clear the stock keymap window (no F0); ...` | The running VIA-enabled build is old and cannot prepare for going back to stock | Unchanged | First flash the latest VIA-enabled build, then flash the stock HFB again |
| `mode change failed: ... Nothing was written. ...` | Sending or receiving the command that switches to the update mode failed | Normally unchanged (nothing flashed). However, the command may have arrived and the keyboard may have entered the update mode | If `--list` shows `HHKB-Hybrid` and keys do nothing, it is in the update mode, so flash with `--resume`. Otherwise, run again |
| `update-mode HID interface not found (USB enumeration may have failed).` `unplug and replug the USB cable; waiting up to 180s ...` | USB recognition failed after the keyboard entered the update mode ("USB error (-71)" below) | Update mode | **Unplug and replug the USB cable**. Once it is found, flashing continues automatically |
| `update-mode interface did not appear; replug the keyboard and rerun with --resume ...` | The time to wait for the unplug and replug (180 seconds by default) has passed | Update mode | Unplug and replug, check the `FLASH TARGET` with `--list`, and run again with `--resume --device /dev/hidrawN <HFB>` |
| `transfer failed: ...` `The keyboard stays in the PFU update mode ...` | An error during the transfer (disconnection, timeout, response mismatch and so on) | Update mode | Unplug and replug if necessary, check with `--list`, and run again with `--resume --device /dev/hidrawN <HFB>` |
| `transfer failed: ...` `If keys still work, the keyboard is running the stock firmware ...` (when `--resume` was given) | E1 or a later step failed with `--resume` | Update mode, or stock firmware (`--resume` was added by mistake) | If keys work, it is the stock firmware, so run again without `--resume`. If they do nothing, same as the row above |
| `/dev/hidrawN is not an HHKB hidraw node; use --list` | A hidraw that is not an HHKB, or that does not exist, was specified | Unchanged | Specify the `FLASH TARGET` from `--list` |
| `the HFB file is required (except with --list)` | No HFB file was specified | Unchanged | Specify the path of the HFB |
| `F0 not supported (non-VIA QMK build, window unused); using E0` | **Normal output** when going back to stock from the default build (without VIA support) of QMK | — | Nothing to do (flashing simply continues) |
| `WARNING: no FLASH TARGET` in `--list` | USB recognition partly failed and the flash target interface was not created | Depends on the state | Unplug and replug the USB cable and run `--list` again |
| `transfer failed: unexpected E1/E2/E3 response: ...` | The keyboard's response was not what was expected (for E3, the firmware may not have been accepted at the end of the transfer) | Most likely the update mode | Check the state with `--list`, and if it is in the update mode, flash again with `--resume` |

- Flashing after the keyboard has entered the update mode sends everything again from the beginning (E1) with `--resume`. Resuming from a case that stopped right after the switch to the update mode is verified on hardware. Resuming after a failure in the middle of the transfer, and the behavior after an abnormal E3 response, are unverified.
- `unknown stock HFB (sha256 ...)` from `tools/qmk_to_hfb.py` indicates that the template is not the official `HHKB800_FW_A048.hfb`. Obtain it again following the steps in [setup.md](setup.md).

## USB error (-71)

At the moment the keyboard reconnects to USB in the update mode, errors like the following may appear in the kernel log and the hidraw may not be created.

```
usb 3-1: can't set config #1, error -71
usbhid 3-1:1.2: can't add hid device: -71
usb 3-1: can't read configurations, error -71
```

- Sometimes not a single interface is created, and sometimes only some (such as the flash target `input2`) are not created. In either case `flash.py` does not use any interface other than the flash target, and waits for an unplug and replug.
- It occurs intermittently, and the root cause has not been identified (we think it is due to the combination of the PFU bootloader's USB enumeration and the host). No clear relationship with the presence of a USB hub or with whether the VIA page is open or closed has been confirmed either.
- **The keyboard stays in the update mode, so unplugging and replugging the cable recovers it.** `flash.py` prompts for the unplug and replug and waits, and once the keyboard is recognized it continues flashing automatically.
- If it happens repeatedly, try connecting directly to a different port on the PC itself, or changing the cable.
- Details of the observed facts are in [reverse-engineering/implementation.md](reverse-engineering/implementation.md).

## The transfer stops at the same position (after `sent 1024/4960`) every time

This is the case where the transfer stops with `transfer failed: timed out waiting for HHKB response` after `sent 1024/4960`, and stops at the same position (in a trace, right after packet 1149) even when retried with `--resume`.

- This position is the boundary where the STM32 application region ends and the region of the Bluetooth (nRF) firmware contained in the HFB begins.
- On real hardware, this way of stopping occurred twice in a row when **the keyboard being flashed was already paired with this PC and, at the same time as the update mode USB connection, also connected to this PC over Bluetooth**. After the pairing of that keyboard was removed from the PC, USB was unplugged and replugged, and `--resume` was used, flashing completed to the end. The cause is not established, but we think the handover from the bootloader to the nRF stops while a Bluetooth connection is active.
- How to check: see whether `BLUETOOTH HID ... [HHKB-Hybrid...]` (`0005:04FE:0021`) appears in the kernel log (`journalctl -k`) at the time of the unplug and replug.
- What to do:
  1. Remove the pairing of that keyboard in the PC's Bluetooth settings (or block it temporarily, e.g. `bluetoothctl block <address>`).
  2. Unplug and replug the USB cable (after it has stopped, the keyboard may not respond even to the first command until it is unplugged and replugged).
  3. Check the `FLASH TARGET` with `--list` and flash with `--resume`.
  4. After flashing, pair the keyboard over Bluetooth again if needed.

## Keys cannot be typed / input is wrong

| Symptom | Possible cause | What to do |
|---|---|---|
| `HHKB-Hybrid` is shown and keys do nothing while **the blue LED stays lit** (for example right after going back to the stock firmware) | The stock firmware is reconnecting to a Bluetooth host and its input is not going to USB | Switch to USB with **Fn + Ctrl + 0**. If keys still do nothing, see the next row |
| `HHKB-Hybrid` is shown and keys do nothing at all | Update mode (bootmagic, interrupted flashing, the QMK fail-safe) | Flash an HFB with `--resume` ([device-state.md](device-state.md)) |
| QMK has been flashed, but `HHKB-Hybrid` is shown and keys work only in the US layout (the JIS/US correction and VIA are not available). Just before that, keys stopped working and came back after the power was turned off and on | After the keyboard entered the update mode through bootmagic (in versions before this countermeasure, it also acted on a restart on battery power) or the QMK fail-safe, the MCU was reset, and the keyboard is running on the bootloader's built-in keyboard ([device-state.md](device-state.md#when-qmk-no-longer-starts-the-bootloaders-built-in-keyboard)) | Flash the QMK HFB without `--resume`. Then see "Finding out why the keyboard stopped responding" below |
| After going back to stock, many keys cannot be typed or produce characters different from their legends | The keyboard was returned to stock with the VIA-enabled build's settings still left in the stock keymap region | "If key input is already broken" in [restore-stock.md](restore-stock.md) |
| Only on battery power, dropped or wrong key input, Bluetooth disconnections, no response and the like occur (they do not occur on a USB connection) | The low-power operation on battery power (`HHKB_LOW_POWER`, [usage.md](usage.md#low-power-operation-on-battery-power)) may be involved | Connect to USB. If it is `HHKB Hybrid QMK`, flash firmware built with `-e HHKB_LOW_POWER=no` and see whether the same symptom appears. If it is `HHKB-Hybrid` and keys do nothing, it is in the update mode, so flash the same firmware with `--resume` |
| Symbols are off on an OS set to the JIS layout / symbols are off on an OS set to the US layout | The ON/OFF setting of the JIS/US correction does not match | Toggle it with Ctrl+Alt+Shift+J ([jis-us-toggle.md](jis-us-toggle.md)) |
| A key changed in VIA does not work | It was assigned on a different layer | Check the layer number in VIA |

## Finding out why the keyboard stopped responding (diagnostic record)

QMK leaves the circumstances of a watchdog reset or a CPU fault in a diagnostic record in RAM. This record survives a reset, the update mode and the bootloader.

```sh
python3 tools/read_diag.py --map local/<.map of the same build as the flashed HFB>
```

- It only sends a read command (`AA AA C0`) to QMK's Raw HID (the `FLASH TARGET` in `--list`), and does not write to the keyboard's flash memory or EEPROM. Close the VIA page before running it.
- `--map` can be omitted. When given, the place where the fault occurred is shown as a function name. `build.sh` deletes `.build` every time, so keep a copy in `local/` of the `.map` (`qmk_firmware/.build/hhkb_hybrid_<keymap>.map`) from when the HFB to be flashed was built.
- **Removing both the batteries and USB normally erases the record** (because the contents of RAM are lost). Do not remove the batteries until the investigation is finished. If power dropped only for an instant and the contents of RAM remained, the record is not erased and `power-on / brown-out resets that kept the record` increases by 1.
- With a firmware version that does not support the diagnostic record, `the firmware does not support the diagnostics read` is shown.

How to read the output:

| Item | Meaning |
|---|---|
| `boots since the record was created` | The number of boots since the record was created. In parentheses is the boot reason when the record was created (`PORRST` means power was applied). It is not overwritten by later boots, so if the record has been re-created without your knowing why, check the reason here |
| `power-on / brown-out resets that kept the record` | Among the resets caused by power-on or a voltage drop, the number of times the contents of RAM remained and the record was not erased. If it has increased while on battery power, the battery voltage may have dropped for an instant |
| `watchdog resets` / `faults` | The number of watchdog resets / the number of faults since the record was created |
| `current streak` | The number of consecutive resets that happened before 60 seconds of normal operation. When it reaches 3, the keyboard enters the update mode |
| `last update-mode request` | The reason the update mode was last entered (E0/F0 from `flash.py`, `QK_BOOT` / bootmagic, consecutive resets) |
| `last incident` | The circumstances of the last watchdog reset or fault: the time since boot, the place being processed at that moment, the USB, battery and Bluetooth state, the number of nRF SPI interrupts, and the state of the battery level pins (PB7/PB8/PB9) |
| `exception` / `PC` | The type of the fault and where it occurred. For a fault during an interrupt, `PC (interrupt stack)` is the correct value; otherwise `PC (thread stack)` is (the other one may be an unrelated value) |

- If the number of SPI interrupts is extremely large relative to the time since boot (as a guide, more than several thousand per second), the interrupts in the communication with the nRF may have got into a state where they never stop.
- When reporting, attach the whole output including the `raw=` line, and which build (keymap, options, sha256 of the HFB) was used.

## Cannot connect to VIA

- Without the udev rule, the keyboard does not appear in the "Authorize device" list ([setup.md](setup.md)).
- Only the VIA-enabled build (the `via` keymap) can connect to VIA. The default build and the stock firmware cannot.
- If the definition JSON has not been loaded, load it following the steps in [via.md](via.md).

## Before considering DFU (disassembly)

Consider the DFU in [recovery.md](recovery.md) only if nothing is shown by `lsusb -d 04fe:` even after trying all of the following. DFU requires disassembly in order to short R6/R85 on the board.

1. Change the cable, connect directly to a different port on the PC itself, remove the USB hub
2. Remove the batteries, also unplug the USB cable, wait 10 seconds, then connect with USB only
3. Connect USB while holding Esc (bootmagic; if QMK is able to start, it enters the update mode)
4. Check `lsusb` on a different PC
