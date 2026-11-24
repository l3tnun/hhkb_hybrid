# Identifying the keyboard's state and choosing the flash target

> English translation of [../device-state.md](../device-state.md). If the two differ, the Japanese version is authoritative.

Before flashing, always check **which state the connected keyboard is in** and **which `/dev/hidraw*` to send to**. If you get these wrong, the flash fails or you flash a different unit.

## First, connect only one keyboard

- **Connect only the one keyboard you are going to flash over USB.** Disconnect any other HHKB from USB (Bluetooth connections do not matter).
- An HHKB running the stock firmware or in the update mode has no USB serial number, so when several are connected they cannot be told apart over USB.
- `tools/flash.py` does not start flashing unless there is exactly one USB device with `04fe:0021`.

## Check with `flash.py --list`

```sh
python3 tools/flash.py --list
```

Example output (when QMK is running; the numbers and the serial depend on the environment):

```
USB 3-1: product='HHKB Hybrid QMK' bcdDevice=0020 serial=XXXXXXXXXXXXXXXX
  state: QMK (this repository)
  /dev/hidraw3  input0  descriptor=05 01 09  not a flash target
  /dev/hidraw4  input1  descriptor=06 60 ff  FLASH TARGET
  /dev/hidraw5  input2  descriptor=05 01 09  not a flash target
```

Example output (stock firmware or update mode):

```
USB 3-1: product='HHKB-Hybrid' bcdDevice=0001 serial=-
  state: stock firmware or PFU update mode (keys work = stock, keys do nothing = update mode)
  /dev/hidraw3  input0  descriptor=05 01 09  not a flash target
  /dev/hidraw4  input1  descriptor=05 01 05  not a flash target
  /dev/hidraw5  input2  descriptor=06 00 ff  FLASH TARGET
```

- **Pass the `/dev/hidrawN` of the line marked `FLASH TARGET` to `--device`.** Sending to any other node (the keyboard input interface, for example) fails. `flash.py` also checks this before flashing and aborts if it does not match.
- If `no read/write permission` is shown, set up the udev rule ([setup.md](setup.md)).
- If the USB device is listed but there is not a single hidraw (`no hidraw nodes`), or there are hidraw nodes but no `FLASH TARGET` line (`WARNING: no FLASH TARGET`), USB recognition has failed partly or completely. **Do not use any of the other listed hidraw nodes as the flash target**; unplug and replug the cable, then run `--list` again ([troubleshooting.md](troubleshooting.md)).
- If `no HHKB (04fe:0021) found.` is shown, check the cable and the port. The keyboard may also not be a supported model ("Checking the supported model" below).

## List of states

| State | product | bcdDevice | serial | Key input | Flash target | How to use `flash.py` |
|---|---|---|---|---|---|---|
| **Stock firmware** | `HHKB-Hybrid` | `0001` | none | **keys work** | `input2` (`06 00 ff`) | without `--resume` |
| **The bootloader's built-in keyboard** (QMK has not started) | `HHKB-Hybrid` | `0001` | none | **keys work** (US layout; the JIS/US correction and VIA are not available) | `input2` (`06 00 ff`) | without `--resume` |
| **PFU update mode** | `HHKB-Hybrid` | `0001` | none | **keys do nothing** | `input2` (`06 00 ff`) | **with `--resume`** |
| **QMK (default / via)** | `HHKB Hybrid QMK` | `0020` | present | keys work | `input1` (`06 60 ff`) | without `--resume` |

- The stock firmware and the update mode look exactly the same over USB. Tell them apart by **whether pressing keys produces input** (check in a text editor or similar).
- The bootloader's built-in keyboard also looks the same as the stock firmware, both over USB and in key input, and cannot be distinguished from it. It is also handled the same way as the stock firmware (flash without `--resume`). If you believe you flashed QMK but `HHKB-Hybrid` is shown, and keys work but the JIS/US correction and VIA are not available, the keyboard is in this state ("When QMK no longer starts" below).
- However, even with the stock firmware, if **the blue LED stays lit**, the keyboard is reconnecting over Bluetooth and its input does not go to USB. Switch to USB with **Fn + Ctrl + 0** first, and then judge.
- Use `--resume` only when the keyboard "has already entered the update mode". `flash.py` aborts if `--resume` is given for QMK, but because the stock firmware and the update mode cannot be distinguished, this decision is made by the user.

### Whether QMK is the default build or the VIA-enabled build

`--list` cannot tell them apart. The VIA-enabled build answers VIA queries, so you can check with the read commands in "Checking the running firmware (no writes)" in [via.md](via.md) (if `protocol 0x000d` is shown, it is the VIA-enabled build).

## When the keyboard enters the update mode unintentionally

When "`HHKB-Hybrid` is shown and keys do nothing at all", it is most likely not a failure but that **the keyboard has entered the update mode**. Disassembly (DFU) is not needed; flashing firmware with `--resume` brings it back.

Main causes of entering the update mode:

| Cause | Description |
|---|---|
| bootmagic | USB was connected while Esc was held down |
| Interrupted flashing | Flashing stopped after the switch to the update mode |
| QMK fail-safe (watchdog / fault) | When QMK hangs and the watchdog resets it after about 5 to 11 seconds, or a CPU fault (HardFault and so on) occurs, QMK leaves a record and starts again. If this happens **3 times in a row** (each time before QMK has run normally for 60 seconds), the keyboard enters the update mode |
| QMK fail-safe (USB not configured) | On USB power, if the host started enumeration but configuration did not complete within 20 seconds, QMK also resets through the watchdog (to get out of a state where communication stopped midway). If this happens 3 times in a row, the keyboard enters the update mode |

- Earlier versions of QMK (before the diagnostic record was added) enter the update mode after a single watchdog reset.
- The update mode lasts only as long as the MCU is not reset (for example, when USB is unplugged and replugged while the keyboard keeps running on the battery).

## When QMK no longer starts (the bootloader's built-in keyboard)

When the keyboard enters the update mode, the boot flag (EEPROM `0x08080360`) becomes invalid. The PFU bootloader erases the update request (`0x08081350`) at the moment it enters the update mode. Therefore, if the MCU is reset while in the update mode (for example by turning the keyboard OFF/ON with the power button), it behaves as follows.

- The bootloader does not enter the update mode, and starts with **the keyboard function built into the bootloader**. Key input and Bluetooth work, but the layout stays US, and QMK's features (the JIS/US correction, VIA and so on) are not there.
- QMK is still in flash memory, but it does not start because the boot flag is invalid. This state continues until the firmware is flashed again.
- How to fix it: flash the QMK HFB without `--resume` (the bootloader responds to E0 and enters the update mode).

This behavior is based on the disassembly of the bootloader ([reverse-engineering/stm32-analysis.md](reverse-engineering/stm32-analysis.md) §6) and on a case on real hardware that went through the following course.

1. Keys stopped working while the keyboard was in use over Bluetooth
2. After turning it OFF/ON with the power button, input worked again over both Bluetooth and USB
3. However, the layout stayed US, and `--list` showed `HHKB-Hybrid`
4. Flashing QMK again without `--resume` recovered it, but the keymap that had been changed in VIA was back to its initial state

The VIA settings survive re-flashing a version with the same EEPROM layout, and they are not erased by the update mode entered from a hang or a fault (the fail-safe) either. Only bootmagic (and "reset settings" in the HHKB menu) erases them. Therefore, this case is thought to be one where the keyboard restarted for some reason while in use over Bluetooth (on battery power), and bootmagic at that startup judged Esc to be pressed and entered the update mode. The reason for the restart, and whether Esc was actually pressed or it was a false detection, are not known. The current version disables bootmagic on battery power.

To investigate the cause, flash QMK again and then read out the diagnostic record with "Finding out why the keyboard stopped responding" in [troubleshooting.md](troubleshooting.md).

## Checking the supported model

- The target is the **HHKB Professional HYBRID (US layout)**, the model whose USB ID is `04fe:0021`. The update file of the stock firmware is `HHKB800_FW_A048.hfb` (the `PD-KB800` series).
- The only thing verified on hardware is a US layout unit with USB ID `04fe:0021`.
- The JIS layout, the HHKB Classic, the HHKB Studio and other models are not supported (the USB ID and the update file may differ; unverified).
- How to check:
  - Check the model number (`PD-KB800` series) on the label on the bottom of the keyboard
  - Check that `04fe:0021` appears in the output of `lsusb -d 04fe:`
- `tools/flash.py` does not treat anything other than `04fe:0021` as a flash target.

## Checking by hand (for reference)

`flash.py --list` reads the following information from sysfs.

```sh
for h in /sys/class/hidraw/hidraw*; do
  grep -q 'HID_ID=0003:000004FE:00000021' "$h/device/uevent" 2>/dev/null || continue
  usb=$(dirname "$(dirname "$(readlink -f "$h/device")")")
  printf '%s  %s  %s  bcdDevice=%s  descriptor=%s\n' "/dev/${h##*/}" \
    "$(sed -n 's/^HID_NAME=//p' "$h/device/uevent")" \
    "$(sed -n 's/^HID_PHYS=.*\/\(input[0-9]*\)$/\1/p' "$h/device/uevent")" \
    "$(cat "$usb/bcdDevice")" \
    "$(od -An -tx1 -N3 "$h/device/report_descriptor" | tr -s ' ' | sed 's/^ //')"
done
```

It does not open the keyboard's device files, so it can be run even without access permission.
