# HHKB Professional HYBRID — QMK Firmware

> English translation of [README.md](README.md). If the two differ, the Japanese version is authoritative.

This project **replaces** the stock firmware of the HHKB Professional HYBRID (US layout, `PD-KB800` series, USB `04fe:0021`) **with QMK**. It runs QMK by rewriting, over HID, only the same range that a stock update rewrites (the 64 KiB STM32 application area), and it is built on the premise that you can go back to the stock firmware **without disassembly**.  
The goal is that anyone can reproduce the identical firmware from this one repository (QMK itself is pinned to a specific commit).  

> ⚠️ This entire project was created using generative AI. As of 2026/09/19, `Claude Opus 5` and `DeepSeek V4.1 Flash` are used.  

> ⚠️ **Unofficial, at your own risk — you must not contact the manufacturer (PFU/HHKB).**  
> This is a modification that is not official PFU/HHKB. **From the moment you flash** the firmware in this repository (either `default` or `via`), **the manufacturer's warranty no longer applies**.  
> **The same is true even if you restore the stock firmware without disassembly.**  
> **Whatever state the keyboard is in, do not go to the manufacturer for warranty service, repair requests, inquiries or the like. Use this entirely at your own risk.**

## Introduction

**If this is your first time, start with [docs/en/quickstart.md](docs/en/quickstart.md).** It lays out the commands and the expected output in this order: setup → build → convert → check the state → flash → verify.

- Supported model: the **US layout** HHKB Professional HYBRID (USB ID `04fe:0021`). This is the only model verified on hardware. The JIS layout, Classic, Studio and others are not supported ([docs/en/device-state.md](docs/en/device-state.md#checking-the-supported-model)).
- Flashing is **Linux only** (it uses `/dev/hidraw*`). The build uses Docker.
- You need to obtain the stock firmware HFB (officially distributed by PFU) yourself.

## Documentation

| Purpose | Document |
|---|---|
| The overall flow | [docs/en/quickstart.md](docs/en/quickstart.md) |
| Setup (Docker, udev rules, obtaining and verifying the stock HFB) | [docs/en/setup.md](docs/en/setup.md) |
| How to tell the keyboard's state, how to choose the flash target, supported models | [docs/en/device-state.md](docs/en/device-state.md) |
| Building | [docs/en/build.md](docs/en/build.md) |
| Flashing (`flash.py` options) | [docs/en/flashing.md](docs/en/flashing.md) |
| Key operations, Bluetooth, power, LEDs | [docs/en/usage.md](docs/en/usage.md) |
| VIA | [docs/en/via.md](docs/en/via.md) |
| Typing what the US legends say in a JIS environment | [docs/en/jis-us-toggle.md](docs/en/jis-us-toggle.md) |
| Restoring the stock firmware | [docs/en/restore-stock.md](docs/en/restore-stock.md) |
| When you are in trouble | [docs/en/troubleshooting.md](docs/en/troubleshooting.md) |
| Recovery via DFU (a last resort that requires disassembly) | [docs/en/recovery.md](docs/en/recovery.md) |
| Hardware and EEPROM | [docs/en/hardware.md](docs/en/hardware.md) |
| Reverse-engineering notes | [docs/en/reverse-engineering/](docs/en/reverse-engineering/README.md) |
| Operating rules for AI agents | [AGENTS.md](AGENTS.md) |

## Current status (verified working)

- ✅ Boots and types as a USB keyboard
- ✅ Typing, slot switching and pairing over Bluetooth (via the nRF52832)
- ✅ Media keys (volume etc., BLE consumer)
- ✅ Power button (long press turns it OFF) and auto-sleep on inactivity (STOP mode, disabled with DIP SW6)
- ⚠️ Low-power operation on battery power (4 MHz operation, thinned-out scanning and Sleep, stopping USB and the readout circuit. Enabled by default; `-e HHKB_LOW_POWER=no` returns to the previous behavior). On battery power, key input, Bluetooth connection and switching, power-button long-press OFF and power-on, and unplugging and replugging USB are verified on hardware. Auto-sleep, the power-button short press, the battery level display and VIA are unverified. How much battery life improves has not been measured — [docs/en/usage.md](docs/en/usage.md#low-power-operation-on-battery-power)
- ✅ Indicator LEDs (connection state, power). A low battery level is not shown on the LEDs; it is reported to the Bluetooth host (see "Battery level" below)
- ✅ Reconnects to the last used BT slot at power ON (gives up after about 60 seconds if it is not found. See [docs/en/usage.md](docs/en/usage.md) for details)
- ✅ bootmagic (connect while holding Esc → enters the stock update mode, from which the stock firmware can be restored). It is disabled on battery power (enabled only while on USB power), and both are verified on hardware
- ✅ Automatic recovery from hangs and faults, and a diagnostic record: on a watchdog reset or a CPU fault QMK starts again, and it enters the update mode only when this happens 3 times in a row. The circumstances can be read out with `tools/read_diag.py`. Verified on hardware on both USB power and battery power (test build `HHKB_DIAG_TEST=yes`) — [docs/en/troubleshooting.md](docs/en/troubleshooting.md#finding-out-why-the-keyboard-stopped-responding-diagnostic-record)
- ⚠️ Battery level: 4 steps like the stock firmware (100/15/5/0 %). See [docs/en/reverse-engineering/power-management.md](docs/en/reverse-engineering/power-management.md) for details and the verification status
- ✅ A toggle for typing what the US legends say in a JIS environment (`Ctrl+Alt+Shift+J`; in the VIA version it can also be assigned to any key). It can be enabled/disabled with a build option (enabled by default) — [docs/en/jis-us-toggle.md](docs/en/jis-us-toggle.md)
- ✅ VIA support (`via` keymap): change the keymap, macros, the auto-sleep time and so on in VIA, saved to the EEPROM — [docs/en/via.md](docs/en/via.md)

Not supported: Vial, and flashing through the official macOS/Windows Keymap Tool.

## How it works (the key safety points)

- Only the STM32 application area `0x08010000–0x0801FFFF` (64 KiB) and the EEPROM addresses that a stock update touches are written. The PFU bootloader (`0x08000000–0x0800FFFF`) is not modified.
- Flashing uses the same **HID update path (E0/E1/E2/E3)** as a stock update. The boot check, including the CRC, is also updated just as the stock update does, so the keyboard boots straight away after flashing.
- **Restoring the stock firmware requires no disassembly**: you can flash the stock HFB with `tools/flash.py`, or enter the bootloader's update mode with bootmagic (connect while holding Esc) and flash it back (verified on hardware). Because the VIA version saves its settings in the stock keymap area, either path erases that area before going back.
- DFU (shorting R6/R85 on the board) requires physical disassembly, so it is limited to isolating problems and to a last resort ([docs/en/recovery.md](docs/en/recovery.md)).

### Bluetooth reuses the stock nRF firmware

Only the STM32 application side is converted to QMK. **The Bluetooth side is not our own work; the PFU-made nRF52832 firmware is used as is.**

- The HFB is generated by "replacing only the STM32 application area of the stock HFB with QMK, and leaving the nRF (Bluetooth) firmware image in the second half and the container (header and footer) as they are in the stock file" (`tools/qmk_to_hfb.py`). QMK on the STM32 side merely talks to this stock nRF firmware over SPI (`keyboards/hhkb_hybrid/nrf_spi.c`).
- Therefore **the generated `.hfb` contains a PFU-made proprietary binary (the nRF firmware) and cannot be redistributed**. Each user needs to prepare the stock `HHKB800_FW_A048.hfb` themselves and use it as the template for the conversion (this is why this repository does not distribute HFBs).

The detailed evidence from the analysis is collected in [docs/en/reverse-engineering/](docs/en/reverse-engineering/).

## Directory layout

```
.
├── qmk.json                     QMK Userspace manifest (declares the build targets)
├── keyboards/hhkb_hybrid/       Keyboard definition (this repository is the source of truth)
│   └── keymaps/{default,via}/   default (no VIA support) / via (VIA support)
├── via/hhkb_hybrid.json         VIA definition (generated by tools/gen_via_json.py)
├── qmk_firmware/                QMK itself (git submodule, pinned commit)
├── local/                       Place for the stock HFB, converted HFBs, etc. (covered by .gitignore. Empty right after clone)
├── tools/                       Build, conversion and flashing tools
│   ├── build.sh                 Build (pinned submodule + Docker)
│   ├── qmk_to_hfb.py            Convert a QMK .bin → a .hfb for the stock update
│   ├── flash.py                 Flash the keyboard over HID
│   ├── fix_hhkb_hfb_header64_crc.py  Fix the HFB header/CRC (used by qmk_to_hfb)
│   ├── hfb_crc.py               Compute the HFB CRC
│   ├── gen_via_json.py          Generate the VIA definition JSON
│   └── check_eeprom_refs.py     Check the EEPROM addresses the firmware references
└── docs/                        Procedures and reverse-engineering notes
```

## Building

Prerequisites: `git`, and `docker` that runs without `sudo` ([docs/en/setup.md](docs/en/setup.md)).

```sh
git clone <URL of this repository>
cd <the cloned directory>
git submodule update --init qmk_firmware      # QMK itself (pinned commit)
./tools/build.sh                              # → qmk_firmware/.build/hhkb_hybrid_default.bin
./tools/build.sh via                          # VIA version → qmk_firmware/.build/hhkb_hybrid_via.bin
```

Fetching `qmk_firmware` itself is mandatory (if it has not been fetched, `build.sh` shows the command to fetch it and exits). Once it is there, `build.sh` automatically fetches the lower-level QMK submodules needed for the build (chibios / chibios-contrib / lufa / printf). `build.sh` deletes `.build` every time it runs, so do the conversion on the keymap you built immediately before. See [docs/en/build.md](docs/en/build.md) for details.

## Flashing

Beforehand, set up the udev rule, place the stock HFB (`https://origin.pfultd.com/downloads/hhkb/HHKB800_FW_A048.hfb`) at `local/HHKB800_FW_A048.hfb` and check its sha256 ([docs/en/setup.md](docs/en/setup.md)).

```sh
# 1. Convert the QMK .bin to an HFB for the stock update (the stock HFB is used as the template)
python3 tools/qmk_to_hfb.py qmk_firmware/.build/hhkb_hybrid_default.bin local/HHKB800_FW_A048.hfb local/hhkb_hybrid_default.hfb

# 2. Check the state and the flash target (FLASH TARGET) (connect only the one keyboard you are flashing)
python3 tools/flash.py --list

# 3. Flash over HID (switches to the update mode automatically. About 3 minutes)
python3 tools/flash.py --device /dev/hidrawN local/hhkb_hybrid_default.hfb
```

To flash the VIA version (`./tools/build.sh via`), read `default` in the file names as `via` (`local/hhkb_hybrid_via.hfb`. The procedure is in [docs/en/via.md](docs/en/via.md)).

If the keyboard is already in the update mode (it is shown as `HHKB-Hybrid` and keys do nothing), add `--resume`. Close the VIA page while flashing. How to tell the states apart is in [docs/en/device-state.md](docs/en/device-state.md); what to do when it stops partway is in [docs/en/troubleshooting.md](docs/en/troubleshooting.md).

## VIA

Flash the `via` keymap and load `via/hhkb_hybrid.json` into VIA, and you can change the keymap, macros and the keyboard-specific settings (JIS/US correction, auto-sleep time, stock-compatible Bluetooth key operations) in VIA. The procedure and the limitations are in [docs/en/via.md](docs/en/via.md).

## Restoring the stock firmware (no disassembly)

Either flash the stock HFB (`HHKB800_FW_A048.hfb`) with `tools/flash.py` to the running QMK, or connect USB while holding Esc (bootmagic) to enter the update mode and then flash it back. With the VIA version, **avoid entering the update mode with the `QK_BOOT` key and then going back to stock** (the settings area is left behind and the stock firmware's key input breaks). The procedure is in [docs/en/restore-stock.md](docs/en/restore-stock.md).

## When the keyboard stops responding or restarts (keep the diagnostic record)

QMK keeps things such as watchdog resets, CPU faults and the reason for booting in a diagnostic record in RAM. It does not write to flash or the EEPROM. The record survives resets, OFF/ON with the power button, the update mode and firmware flashing, but **it is lost when both the battery and USB are removed.** If a problem occurs while in use over Bluetooth (on battery power), investigate with the following steps.

1. **Do not remove the batteries.** OFF/ON with the power button is fine.
2. As it is, connect it by USB to a PC that has this repository's environment.
3. Check the state (`python3 tools/flash.py --list`).
   - If it is `HHKB Hybrid QMK`, read the record out as is.
   - If keys do nothing (update mode), or it works as a US layout but JIS/US correction and VIA are unavailable (the bootloader's built-in keyboard), flash QMK again first. Decide whether to use `--resume` according to [docs/en/device-state.md](docs/en/device-state.md). The record survives the flashing.
4. Read the record out (this only reads; it does not write to the keyboard).

   ```sh
   python3 tools/read_diag.py --map local/<the .map of the same build as the flashed HFB>
   ```

   To have an AI agent investigate, open a new session in this repository and ask it to "read the diagnostic record with `tools/read_diag.py` and investigate the cause" (`read_diag.py` sends a command to the keyboard's hidraw, so under the rules in [AGENTS.md](AGENTS.md) you will be asked for approval before it runs).

- **Details are kept only for the most recent event.** The counts accumulate, but things such as where it stopped are overwritten by the next anomaly, so read the record out as soon as you can after it happens.
- **Note down the circumstances when it happened.** The approximate time, what you were doing (typing or leaving it idle), how long the keys were stopped, and whether Bluetooth disconnected. These are clues for matching against the record.
- In the current version, a hang recovers in 7–11 seconds and a fault in 1–2 seconds, still in QMK, and if it is only a restart, Bluetooth just drops for a few seconds and reconnects. Even for something as minor as "it froze for a moment" or "Bluetooth dropped", the record is there.
- How to read the output is in [docs/en/troubleshooting.md](docs/en/troubleshooting.md#finding-out-why-the-keyboard-stopped-responding-diagnostic-record).

## Reproducibility

QMK itself is pinned to a commit as a submodule (tag `0.34.4`), and the Docker image used for the build is also pinned by digest. The same commit should yield the same binary. Compare using the sha256 of the `.bin` that `build.sh` prints ([docs/en/build.md](docs/en/build.md#reproducibility)).

## License

The license in this repository differs from file to file. The `SPDX-License-Identifier` at the top of each file is authoritative.

### Source code licenses (per file)

| Subject | License | Notes |
|---|---|---|
| `keyboards/hhkb_hybrid/matrix.c` | **GPL-2.0-or-later** | A derivative work based on [Duncaen's HHKB Classic port](https://github.com/Duncaen/qmk_firmware/tree/hhkb_classic) (Topre capacitive scanning). Our changes are bounded waits, release hysteresis, and integration with the stock calibration |
| `keyboards/hhkb_hybrid/mcuconf.h` | **Apache-2.0** | ChibiOS template (Copyright ChibiOS / Giovanni Di Sirio). The header is retained |
| `keyboards/hhkb_hybrid/eeprom_hhkb.c` | **GPL-2.0-or-later** | A derivative work based on the write procedure of QMK's STM32L0/L1 EEPROM driver (`eeprom_stm32_L0_L1.c`) (restricts the EEPROM area for VIA to the stock keymap table B) |
| Files we created other than the above (`hhkb_hybrid.c/.h`, `nrf_spi.c/.h`, `power.c/.h`, `led_indicator.c/.h`, `boards/HHKB_HYBRID/board.c/.h`, `halconf.h`, `chconf.h`, `keymaps/default/`, `keymaps/via/`, `config.h`, `post_rules.mk`, `keyboard.json`, `via/`, `tools/`, `docs/`) | **MIT** (`LICENSE`) | Original implementation from reverse engineering the stock firmware |
| `qmk_firmware/` (submodule) | GPL-2.0 and others | QMK itself, ChibiOS (Apache-2.0), LUFA, etc. Each follows its upstream license |

### Build artifacts (important)

The built `.hfb` **contains the PFU-made nRF firmware (a proprietary binary)**, so **the generated firmware (`.hfb`) cannot be distributed**. Prepare the stock `HHKB800_FW_A048.hfb` yourself, and convert and flash it in your own environment.

Note that the build artifacts, including the `.bin`, are also combined works linked with QMK itself (GPL-2.0) and `matrix.c` (GPL-2.0-or-later) (distribution of this repository's source follows the license of each file). However, as stated above, the generated firmware itself, which contains the PFU-derived binary, is not something to be distributed.

### Acknowledgements

The scanning implementation for the Topre capacitive matrix is based on [Duncaen's HHKB Classic port](https://github.com/Duncaen/qmk_firmware/tree/hhkb_classic) (same main board).

### About PFU/HHKB-derived material

None of the MIT / GPL / Apache licenses above grants any rights to the official PFU/HHKB firmware, the official tools, the extracted contents of the official tools, disassembly output of the official firmware, generated firmware, or any other binaries or derivatives originating from PFU/HHKB.

This repository does not distribute the official PFU/HHKB firmware, the official tools, or generated firmware. Users should obtain `HHKB800_FW_A048.hfb` from the official site themselves and use it in their own environment.

This project is not official PFU/HHKB and has not received approval, warranty or support from PFU. Check the PFU/HHKB software license agreement and the applicable laws, and handle it at your own responsibility.
