# Build procedure

> English translation of [../build.md](../build.md). If the two differ, the Japanese version is authoritative.

Complete the preparation in [setup.md](setup.md) beforehand (git, Docker that runs without `sudo`, fetching the `qmk_firmware` submodule). You do not need to install an ARM toolchain on the host. The build is done entirely inside Docker.

## Building

**Run it at the root of the repository.**

```sh
./tools/build.sh                                   # keymap = default (no VIA support)
./tools/build.sh via                               # keymap = via (VIA support. docs/via.md)
./tools/build.sh via -e HHKB_JIS_US_TOGGLE=no      # the second and later arguments are passed to qmk compile as is
```

When it finishes, it displays the following.

```
Built: <repository>/qmk_firmware/.build/hhkb_hybrid_via.bin
<sha256>  <repository>/qmk_firmware/.build/hhkb_hybrid_via.bin

Next: convert to an HFB and flash (see docs/quickstart.md):
  python3 tools/qmk_to_hfb.py ".../hhkb_hybrid_via.bin" local/HHKB800_FW_A048.hfb local/hhkb_hybrid_via.hfb
  python3 tools/flash.py --list
  python3 tools/flash.py --device /dev/hidrawN local/hhkb_hybrid_via.hfb
```

### Build options

| Option | Details |
|---|---|
| `-e HHKB_JIS_US_TOGGLE=no` | Remove the JIS/US correction feature from the build ([jis-us-toggle.md](jis-us-toggle.md)) |
| `-e HHKB_LOW_POWER=no` | Remove the low-power operation on battery power (4 MHz operation, thinned-out scanning, stopping USB and the ADC. [usage.md](usage.md#low-power-operation-on-battery-power)) from the build. This is for comparing against operation without the power-saving operation when a problem appears with the power-saving operation |
| `-e HHKB_DIAG_TEST=yes` | For testing the fail-safe. Ctrl+Alt+Shift+F11 stops the main loop (watchdog reset), and Ctrl+Alt+Shift+F12 triggers a HardFault ([troubleshooting.md](troubleshooting.md#finding-out-why-the-keyboard-stopped-responding-diagnostic-record)). Do not add it to firmware for everyday use |

## Artifacts

- The artifact is `qmk_firmware/.build/hhkb_hybrid_<keymap>.bin`.
- **`build.sh` deletes `qmk_firmware/.build` every time it runs.** If you build `default` and `via` one after the other, the one built first is deleted. Do the conversion to an HFB on the keymap you built immediately before.
- The output file name is the same even when you add build options.
- QMK also places a copy of the same `.bin` directly under `qmk_firmware/`, but do not use it (`build.sh` deletes old copies when it runs).
- If it exceeds 64 KiB it does not fit in the application area, so `qmk_to_hfb.py` aborts the conversion.

## Converting to an HFB

```sh
python3 tools/qmk_to_hfb.py qmk_firmware/.build/hhkb_hybrid_via.bin local/HHKB800_FW_A048.hfb local/hhkb_hybrid_via.hfb
```

- Using the stock HFB (`local/HHKB800_FW_A048.hfb`) as the template, it replaces only the STM32 application area and recalculates the CRCs.
- If the template's sha256 does not match the official A0.48, it aborts (this can be overridden with `--allow-unknown-template`, but do not use it).
- Meaning of the output:

| Output | Meaning |
|---|---|
| `template: HHKB800_FW_A048.hfb (A0.48) (sha256 ok)` | The template is the official stock HFB |
| `qmk size: N bytes ... sha256=...` | The size and sha256 of the source `.bin` (can be used to check reproducibility) |
| `header64 0x11a3->0x....` | The CRC of the application area (the value used for the bootloader's boot check). Stock is `0x11a3`; the right-hand side is the value for the firmware being flashed |
| `stored_crc=0x.... match=True` | The HFB's file CRC was set correctly (a self-check right after conversion) |

## What `build.sh` does

1. If `qmk_firmware` itself is missing, show the command to fetch it and exit
2. If the required QMK submodules (`lib/chibios`, `lib/chibios-contrib`, `lib/lufa`, `lib/printf`) are missing, fetch them
3. Copy this repository's `keyboards/hhkb_hybrid/` to `qmk_firmware/keyboards/` (this repository is the source of truth)
4. Delete `qmk_firmware/.build` and the old `.bin` / `.hex` directly under `qmk_firmware/`
5. Run `qmk compile -kb hhkb_hybrid -km <keymap> [options]` in the official QMK image pinned by digest (`ghcr.io/qmk/qmk_cli@sha256:b7d7fa8f...`, QMK CLI 1.2.0 / arm-none-eabi-gcc 15.2.0)
6. Display the path and sha256 of the artifact, and the next steps

- `qmk_firmware/keyboards/hhkb_hybrid/` is a working copy that is overwritten on every build. Make your edits in `keyboards/hhkb_hybrid/` at the top of the repository (the copy does not show up in git status because of a setting in `.gitmodules`).
- If you run `qmk compile` directly in Docker, run `build.sh` beforehand so that the latest definition is copied. Otherwise the build uses the old definition.

## Reproducibility

- `qmk_firmware` is pinned to a specific commit (tag `0.34.4`) as a submodule. The lower-level submodules are also pinned by the `qmk_firmware` commit.
- The Docker image used for the build is also pinned by digest.
- Therefore, the same commit should yield the same `.bin`. **To check, compare the sha256 of the `.bin` built in a different environment** (displayed by `build.sh` and `qmk_to_hfb.py`)**.** You can also compare using the CRC of the application area (the right-hand value of `header64`).
- `match=True` from `qmk_to_hfb.py` is a self-check right after conversion, so it does not confirm reproducibility.
- The sha256 values when built from the contents of this repository are as follows (if you change `keyboards/hhkb_hybrid/`, update this table too).

  | Build | sha256 of the `.bin` |
  |---|---|
  | `./tools/build.sh via` | `f015c9e78eda16041052c3ddb8ecaf7de0ae2b692fac7f43287f7aa9c8f2da9a` |
  | `./tools/build.sh default` | `341910d153ca813a6a14e2d7112be81ca9044afc310422970146e4ddec41cd72` |
  | `./tools/build.sh via -e HHKB_LOW_POWER=no` | `f7a57da03b193e741c3f1097bc4fb0cf536c04a303d83cb436f3a4357cfd3569` |
  | `./tools/build.sh default -e HHKB_LOW_POWER=no` | `36604adc34516a942a44e2b17e12e4af6c622da873313d8e3c68fd2b20ba1aac` |

## Checking EEPROM references

You can check that the built firmware does not reference anything other than the permitted EEPROM addresses (the bootloader's boot check, calibration, the VIA storage area, etc.). Run it whenever you make a change that involves the EEPROM.

```sh
python3 tools/check_eeprom_refs.py qmk_firmware/.build/hhkb_hybrid_via.bin
```

If `result: OK` is displayed at the end, there is no problem.

## Generating the VIA definition JSON

`via/hhkb_hybrid.json` is generated from `keyboards/hhkb_hybrid/keyboard.json`. If you change the layout, the custom keycodes or the HHKB menu, regenerate it and commit it.

```sh
python3 tools/gen_via_json.py
```

## About QMK Userspace

This repository has the QMK Userspace structure (`qmk.json` + `keyboards/`). The pinned QMK `0.34.4` does not directly resolve keyboard definitions located in an external Userspace, so `build.sh` copies the keyboard definition into QMK itself before building (the official QMK Userspace GitHub Action copies it in the same way).

## When updating QMK itself or the build image

This changes the baseline for reproducibility, so do it only for a deliberate update.

```sh
cd qmk_firmware
git fetch origin
git checkout <new commit/tag>
cd ..
git add qmk_firmware
git commit -m "Bump qmk_firmware to <version>"
```

- To update the build image, rewrite the digest of `QMK_IMAGE` in `tools/build.sh`.
- After updating, always rebuild, and check `check_eeprom_refs.py` and the behavior on real hardware.
- If updating QMK itself changes the EEPROM layout, such as the size of eeconfig, you need to raise `HHKB_VIA_LAYOUT_VERSION` in the VIA version (the maintenance notes in [via.md](via.md)).
