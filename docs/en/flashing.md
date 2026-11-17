# Flashing procedure (over HID)

> English translation of [../flashing.md](../flashing.md). If the two differ, the Japanese version is authoritative.

This flashes firmware to the STM32 application area over the same HID update path (E0/E1/E2/E3) as a stock update. **No disassembly is needed.** If this is your first time, follow the flow in [quickstart.md](quickstart.md).

## Prerequisites

- The preparation in [setup.md](setup.md) is done (Linux, udev rule, stock HFB placed in `local/`)
- Only the one keyboard you are flashing is connected by USB
- The VIA page (browser tab) is closed
- The keyboard is recognized on USB in one of these states: stock firmware, QMK, or update mode ([device-state.md](device-state.md))

## 1. Build and convert

```sh
./tools/build.sh via
python3 tools/qmk_to_hfb.py qmk_firmware/.build/hhkb_hybrid_via.bin local/HHKB800_FW_A048.hfb local/hhkb_hybrid_via.hfb
```

- For the default version, read `via` as `default`. Details are in [build.md](build.md).
- Check that `template: HHKB800_FW_A048.hfb (A0.48) (sha256 ok)` and `stored_crc=0x.... match=True` appear.
- `match=True` only shows that the file CRC of the converted file was set correctly (it does not confirm the reproducibility of the build. See "Reproducibility" in [build.md](build.md)).

## 2. Check the flash target

```sh
python3 tools/flash.py --list
```

Use the `/dev/hidrawN` on the `FLASH TARGET` line. How to read the output, and how to tell the stock firmware and the update mode apart, are in [device-state.md](device-state.md).

| State | Flash target | Start of the report descriptor |
|---|---|---|
| Stock firmware / update mode | `input2` (vendor-defined raw HID, 64-byte reports) | `06 00 ff` |
| QMK | `input1` (QMK's Raw HID, 32-byte reports) | `06 60 ff` |

## 3. Flash

```sh
# When the stock firmware or QMK is running
python3 tools/flash.py --device /dev/hidrawN local/hhkb_hybrid_via.hfb

# When it is already in the update mode (an HHKB-Hybrid whose keys do nothing)
python3 tools/flash.py --resume --device /dev/hidrawN local/hhkb_hybrid_via.hfb
```

What `flash.py` does:

1. It checks the HFB's file CRC (aborts on a mismatch).
2. It checks that exactly one HHKB is connected and that `--device` is the flash-target interface.
3. (Unless `--resume` is given) it sends the command to switch to the update mode.
   - To the stock firmware and to QMK it sends `AA AA E0`. The settings of the QMK VIA version remain.
   - **When flashing the stock HFB to a running QMK it sends `AA AA F0`.** The VIA version resets the stock keymap area, where it saves its settings, to zero before entering the update mode ([restore-stock.md](restore-stock.md)).
4. It waits 10 seconds and automatically re-selects the re-enumerated update-mode flash target (`input2`, `06 00 ff`). If it is not found, it does not use any other interface; it tells you to unplug and replug USB and waits.
5. It sends E1 (start) → E2 (4,960 packets of 57 bytes each) → E3 (end), and finishes with `done`.

See [quickstart.md](quickstart.md#output-in-the-normal-case-example-of-flashing-from-qmk-to-qmk) for an example of the output in the normal case and the time it takes (about 3 minutes).

## Options

| Option | Use |
|---|---|
| `--list` | Display the state and flash target of the connected HHKB (no HFB needs to be given. Nothing is sent to the keyboard) |
| `--device /dev/hidrawN` | The flash target. If omitted, it is chosen automatically only when there is exactly one flash target (it can be omitted, but checking with `--list` and giving it explicitly is recommended) |
| `--dry-run` | Display the HFB's CRC, kind (`hfb_kind=QMK` / `hfb_kind=stock (no QMK)`) and packet count, then exit (does not touch the keyboard) |
| `--resume` | Use when already in the update mode. Starts from E1 without sending the switch command (aborts if QMK is running) |
| `--trace-log <file>` | Append a record of what is sent and received as JSON Lines (e.g. `--trace-log local/flash_$(date +%Y%m%d_%H%M).jsonl`) |
| `--replug-timeout <seconds>` | How long to wait for USB to be unplugged and replugged when the update-mode flash target does not appear (default 180) |
| `--allow-crc-mismatch` | For diagnostics. **Do not use it for normal flashing** |

## 4. Verify after flashing

- `python3 tools/flash.py --list` displays `product='HHKB Hybrid QMK' bcdDevice=0020` for QMK, or `product='HHKB-Hybrid' bcdDevice=0001` for the stock firmware
- Keys work over USB, and you can type over Bluetooth with USB unplugged
- For the VIA version, the read-out in [via.md](via.md) displays `protocol 0x000d` (and you can connect to VIA)

## If it stops partway

- Even if it stops after entering the update mode, the keyboard stays waiting in the update mode. **You can redo it with `--resume` as many times as needed.**
- USB enumeration sometimes fails when reconnecting into the update mode (`error -71` in the kernel log). In this case `flash.py` displays `unplug and replug the USB cable` and waits, so **unplug and replug the USB cable**. Once it is recognized, flashing continues automatically.
- What to do for each error message is collected in [troubleshooting.md](troubleshooting.md).

## Notes

- Only the STM32 application area (from `0x08010000`, 64 KiB) and the EEPROM addresses that the stock update process also writes are written. The PFU bootloader is not modified.
- The HFB also contains the stock Bluetooth (nRF) firmware, which is sent along with it on every flash.
- How flashing works (the HFB format, E0–E3, F0) is in [reverse-engineering/implementation.md](reverse-engineering/implementation.md).
- If `04fe:0021` no longer appears on USB at all, the last resort is DFU (requires disassembly) ([recovery.md](recovery.md)). Before that, try the checks in [troubleshooting.md](troubleshooting.md).
