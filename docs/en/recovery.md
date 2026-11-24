# HHKB Professional HYBRID DFU Recovery Procedure

> English translation of [../recovery.md](../recovery.md). If the two differ, the Japanese version is authoritative.

This document summarises the procedure by which an HHKB Professional HYBRID US layout unit, which could no longer be recognized over normal USB HID after a modified firmware was flashed, was recovered to the equivalent of stock A048 using STM32 DFU.

## Before using DFU (be sure to check)

DFU **requires disassembling the keyboard** in order to short R6/R85 on the board, and carries risk. In the following cases, DFU is not needed.

- `04fe:0021` appears in `lsusb -d 04fe:` → the keyboard can be flashed again over HID. Check the state with `python3 tools/flash.py --list`; if it is an `HHKB-Hybrid` on which keys do nothing, it is in the update mode, so flash with `--resume` ([device-state.md](device-state.md), [troubleshooting.md](troubleshooting.md)).
- USB recognition is unstable (`error -71` in the kernel log) → this is a known phenomenon that usually recovers by unplugging and replugging the cable or connecting directly to a different port.

Consider DFU only when `04fe:0021` does not appear even after trying everything in "Before considering DFU (disassembly)" in [troubleshooting.md](troubleshooting.md).

- This repository does not provide the disassembly procedure, or photos or diagrams showing the location of R6/R85.
- The `dfu-util` commands pass files in the current directory (such as `HHKB800_FW_A048_app_08010000_64k.bin`) to Docker. Run them in the directory where these files are placed (e.g. `local/`). Run the `tools/flash.py` commands in the root of the repository.

> **Important:** If you write the stock application back with DFU after flashing this repository's QMK over HID, the bootloader's boot check (CRC) is still that of QMK, so **the stock application does not start and the keyboard is recognized in the PFU update mode (`04fe:0021`, keys do nothing).** This is not a failure. In that case, after "Steps after writing" below, flash the stock HFB over HID with `--resume` ("Relationship with the bootloader's boot check" at the end).

## Prerequisites

- Target: HHKB Professional HYBRID, US layout
- Stock firmware file: `HHKB800_FW_A048.hfb`
- DFU device with which the recovery succeeded:

```text
0483:df11 STMicroelectronics STM Device in DFU Mode
```

- DFU alt setting used:

```text
alt=0, name="@Internal Flash  /0x08000000/1536*128g"
```

In the examples below, `dfu-util` is run inside Docker (so that no package has to be added to the host; if the host has `dfu-util`, you may use it as is). `--privileged` is added in order to pass the USB device to the container.

## Important notes

Do not write `HHKB800_FW_A048.hfb` as it is with DFU.

On this unit, offset `4` of the HFB file corresponded to `0x08010000` in DFU. Also, the DFU listing appears to show the equivalent of 192KiB, but the device went down when an actual read or write at `0x08020000` or beyond was attempted.

Therefore, what is written back in the recovery is limited to the following.

```text
Source:      65536 bytes from offset 4 of HHKB800_FW_A048.hfb
Destination: 0x08010000
Range:       0x08010000..0x0801ffff
```

Regions not to touch:

```text
0x08000000..0x0800ffff  bootloader region
Option Bytes
DATA Memory
```

## Recovery file

Already generated file:

```text
HHKB800_FW_A048_app_08010000_64k.bin
size=65536
sha256=f9fab50e4194e88211e3431c888e377e69f77b41383f2f72952e1ad6ab04b1f7
```

Original stock HFB:

```text
HHKB800_FW_A048.hfb
size=282672
sha256=635b995eb5a15aa50c2cedc60da7d8998fcca8285a09d41d984c07fffcaf9d43
```

To regenerate the 64KiB recovery file:

```bash
python3 - <<'PY'
from pathlib import Path
import hashlib

hfb = Path("HHKB800_FW_A048.hfb").read_bytes()
app = hfb[4:4 + 0x10000]
Path("HHKB800_FW_A048_app_08010000_64k.bin").write_bytes(app)
print(len(app), hashlib.sha256(app).hexdigest())
PY
```

Expected value:

```text
65536 f9fab50e4194e88211e3431c888e377e69f77b41383f2f72952e1ad6ab04b1f7
```

## Entering DFU mode

1. Unplug USB
2. Remove the batteries
3. Confirm that the power is off (do not press the power button)
4. Short R6/R85
5. Connect USB while keeping the short
6. Confirm DFU recognition with `lsusb`

Check command:

```bash
lsusb | grep -i -E '0483|dfu|stm'
```

Expected example:

```text
Bus 001 Device 039: ID 0483:df11 STMicroelectronics STM Device in DFU Mode
```

## Confirming recognition with dfu-util

Run `dfu-util -l` in Docker.

```bash
docker run --rm --privileged \
  -v "$PWD:/work" -w /work \
  debian:bookworm bash -lc '
    apt-get update >/dev/null &&
    apt-get install -y dfu-util >/dev/null &&
    dfu-util -l
  '
```

Expected example:

```text
Found DFU: [0483:df11] ver=2200, devnum=39, cfg=1, intf=0, path="1-2", alt=2, name="@DATA Memory /0x08080000/2*3Ke", serial="..."
Found DFU: [0483:df11] ver=2200, devnum=39, cfg=1, intf=0, path="1-2", alt=1, name="@Option Bytes  /0x1FF80000/01*032 e", serial="..."
Found DFU: [0483:df11] ver=2200, devnum=39, cfg=1, intf=0, path="1-2", alt=0, name="@Internal Flash  /0x08000000/1536*128g", serial="..."
```

## Writing back

With R6/R85 still shorted, run the following.

```bash
docker run --rm --privileged \
  -v "$PWD:/work" -w /work \
  debian:bookworm bash -lc '
    apt-get update >/dev/null &&
    apt-get install -y dfu-util >/dev/null &&
    dfu-util -a 0 -s 0x08010000:leave -D HHKB800_FW_A048_app_08010000_64k.bin
  '
```

Important output on success:

```text
Downloading element to address = 0x08010000, size = 65536
Erase    done.
Download done.
File downloaded successfully
Submitting leave request...
Transitioning to dfuMANIFEST state
```

## Steps after writing

1. Remove the R6/R85 short
2. Unplug USB
3. Remove the batteries as well
4. Wait 10 seconds
5. Connect USB (put the batteries back if needed)

After that, check the following.

- USB input works
- Bluetooth input works
- The LED behaves as usual
- The power button behaves as usual

In this recovery, all 4 items above were OK.

If `python3 tools/flash.py --list` shows `HHKB-Hybrid` and keys do nothing, the keyboard has started in the PFU update mode (see "Important" at the beginning). Flash the stock HFB over HID with the procedure at the end.

## Procedure that failed

The following 128KiB write failed.

```text
write 131072 bytes to 0x08010000
```

Output on failure:

```text
dfu-util: Error during special command "ERASE_PAGE" get_status
```

Also, the DFU device went down on a read from `0x08020000` as well.

For this reason, the DFU recovery of this unit adopts the procedure of writing back only 64KiB from `0x08010000`.

## Relationship with the bootloader's boot check (added 2026-09-13)

The PFU bootloader starts the application only when the CRC16 of the application region (64KiB from `0x08010000`) matches the expected value at EEPROM `0x08080370` (details in `hardware.md`).

- A raw DFU write does not update this expected value.
- The stock firmware started in the recovery above because the expected value in the EEPROM was still the CRC of stock A048.
- If the stock application is written back with DFU after a different firmware (QMK and so on) was flashed by HID update, the CRC does not match and the application does not start. In this case the keyboard enumerates in the PFU update mode (`04fe:0021`), so flash the stock HFB by HID update.

```bash
# Run in the root of the repository
python3 tools/flash.py --list                                                  # check the update mode FLASH TARGET (input2)
python3 tools/flash.py --resume --device /dev/hidrawN local/HHKB800_FW_A048.hfb
```

- From QMK, the PFU update mode can be entered without disassembly by bootmagic (connect while holding Esc). DFU is not needed ([restore-stock.md](restore-stock.md)).

## Notes

Even in a state where recovery over HID is not possible, if STM32 ROM DFU can be entered by shorting R6/R85, recovery may be possible with this procedure.

However, the memory layout shown by DFU and the range that can actually be accessed safely may not match, so do not access `0x08020000` or beyond.
