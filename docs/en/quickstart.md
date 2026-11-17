# Quickstart (from build to flashing)

> English translation of [../quickstart.md](../quickstart.md). If the two differ, the Japanese version is authoritative.

This is the overall flow for people flashing for the first time. See the linked documents for the details of each step.

> ⚠️ This is an unofficial modification. **Connect only the one keyboard you are flashing by USB**, and proceed only after you understand the procedure. Disassembly (DFU) is not normally needed.

## 0. Preparation (first time only)

Follow [setup.md](setup.md) and complete the following.

- Fetch the repository and the `qmk_firmware` submodule
- Docker runs without `sudo`
- udev rule (access permission to the keyboard's hidraw)
- Place the stock HFB at `local/HHKB800_FW_A048.hfb` and check its sha256

## 1. Check that it is a supported model

Only the US layout HHKB Professional HYBRID (USB ID `04fe:0021`) is supported. How to check is in [device-state.md](device-state.md#checking-the-supported-model).

## 2. Build

Choose one or the other.

```sh
./tools/build.sh          # default: no VIA support (the key layout is fixed in the firmware)
./tools/build.sh via      # via: the version where the key layout and settings can be changed with VIA (docs/via.md)
```

- The artifact is `qmk_firmware/.build/hhkb_hybrid_<keymap>.bin` (`<keymap>` is `default` or `via`).
- **`build.sh` deletes `.build` every time it runs.** Do the following conversion on the keymap you built immediately before.
- Details (build options, reproducibility) are in [build.md](build.md).

## 3. Convert to an HFB

```sh
python3 tools/qmk_to_hfb.py qmk_firmware/.build/hhkb_hybrid_via.bin local/HHKB800_FW_A048.hfb local/hhkb_hybrid_via.hfb
```

(For default, read `via` in the file names as `default`.)

What to check in the output:

```
template: HHKB800_FW_A048.hfb (A0.48) (sha256 ok)
wrote local/hhkb_hybrid_via.hfb
qmk size: ... bytes (0x....) sha256=...
header64 0x11a3->0x....
stored_crc=0x.... match=True
```

If `template: ... (sha256 ok)` and `match=True` appear, the conversion succeeded.

### When flashing a prebuilt HFB

When you skip steps 2–3 and flash an HFB you already have, check the following.

- You know where it came from and which version it is. Do not use a file you cannot identify (for example, one whose version cannot be determined from the name alone); redo it from steps 2–3.
- With `python3 tools/flash.py --dry-run <HFB>`, `match=True`, and `hfb_kind=QMK` (QMK) or `hfb_kind=stock (no QMK)` (stock) is what you intend.
- For the stock HFB, `sha256sum` matches the value in [setup.md](setup.md#4-prepare-the-stock-hfb) (`header_word=0x11a3` in `--dry-run`).

## 4. Check the keyboard's state and the flash target

- If you have the VIA page (browser tab) open, close it.
- If the keyboard you are flashing is paired with this PC over Bluetooth, remove the pairing on the PC side beforehand (the transfer can stop partway. [troubleshooting.md](troubleshooting.md)).
- Connect only the one keyboard you are flashing by USB.

```sh
python3 tools/flash.py --list
```

Check the state on the `state:` line and the flash target `/dev/hidrawN` on the `FLASH TARGET` line. The stock firmware and the update mode are displayed the same, so tell them apart by whether keys work. How to read the output is in [device-state.md](device-state.md).

## 5. Flash according to the starting state

| Current state | What to flash | Command |
|---|---|---|
| Stock firmware (keys work) | QMK HFB | `python3 tools/flash.py --device /dev/hidrawN local/hhkb_hybrid_via.hfb` |
| QMK | QMK HFB (update) | Same as above. The VIA settings normally remain (they are reset if you flashed a version in which the EEPROM layout changes, or if you switched between the default version and the via version) |
| QMK | Stock HFB (go back to stock) | `python3 tools/flash.py --device /dev/hidrawN local/HHKB800_FW_A048.hfb` ([restore-stock.md](restore-stock.md)) |
| Update mode (keys do nothing) | Any HFB | `python3 tools/flash.py --resume --device /dev/hidrawN <HFB>` |

- `/dev/hidrawN` is the `FLASH TARGET` from step 4.
- If you only want to check beforehand, add `--dry-run` (it shows the HFB's CRC, kind (`hfb_kind`) and packet count and exits, without touching the keyboard).
- If you want to keep a record, add `--trace-log local/flash_<date>.jsonl`.

### Output in the normal case (example of flashing from QMK to QMK)

```
size=282672 crc=0x.... computed_crc=0x.... match=True
header_word=0x.... e2_body_size=282670 e2_packets=4960
hfb_kind=QMK
device=/dev/hidrawN
waiting for firmware-update mode... 10s
...
waiting for firmware-update mode... 1s
device changed after entering update mode: /dev/hidrawM
sent 0/4960
sent 128/4960
...
sent 4864/4960
done
```

- When flashing the stock HFB to QMK, `stock HFB on a QMK keyboard: clearing the stock keymap window (F0)` appears after `device=`.
- If the hidraw number does not change after entering the update mode, the `device changed ...` line does not appear.
- With `--resume`, the `waiting for firmware-update mode` lines do not appear and it goes straight to `sent`.
- **It takes about 3 minutes** (about 175 seconds including the transition to the update mode, about 160 seconds with `--resume`). The `sent` line advances about every 4 seconds.

### What you must not do while flashing

- Unplug the USB cable (except when `flash.py` asks you to unplug and replug)
- Remove the batteries, or long-press the power button
- Open the VIA page, or run `flash.py` from another terminal
- Interrupt with Ctrl+C

Even if it stops partway, the keyboard stays in the update mode, so you can redo it with `--resume`. No disassembly is needed. What to do is in [troubleshooting.md](troubleshooting.md).

## 6. Verify after flashing

The keyboard restarts a few seconds after `done`.

```sh
python3 tools/flash.py --list
```

| What you flashed | Expected display |
|---|---|
| QMK | `product='HHKB Hybrid QMK' bcdDevice=0020`, `state: QMK (this repository)` |
| Stock firmware | `product='HHKB-Hybrid' bcdDevice=0001` (and keys work) |

Then check actual key input (USB), and Bluetooth input on battery power with USB unplugged. For the VIA version, also check that you can connect to VIA with the procedure in [via.md](via.md).

## What to read next

- Key operations, Bluetooth, power: [usage.md](usage.md)
- VIA: [via.md](via.md)
- Going back to stock: [restore-stock.md](restore-stock.md)
- When you are in trouble: [troubleshooting.md](troubleshooting.md)
