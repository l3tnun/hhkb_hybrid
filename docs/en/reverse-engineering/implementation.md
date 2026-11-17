# HFB format and HID update protocol

> English translation of [../../reverse-engineering/implementation.md](../../reverse-engineering/implementation.md). If the two differ, the Japanese version is authoritative.

Analysis of the structure of the stock update file `.hfb` and of the HID update protocol (E0/E1/E2/E3) that flashes it. The implementation is in `tools/qmk_to_hfb.py` (conversion) and `tools/flash.py` (flashing). For the user-facing description of the flashing procedure see [../flashing.md](../flashing.md), and for the boot decision see [../hardware.md](../hardware.md).

## HFB container format

```
[0:2]           File CRC16-CCITT (reflected CRC over the whole of HFB[2:])
[2:4]           header64 = CRC of the application area (the value the bootloader uses for the boot decision)
[4 : 4+0x10000] STM32 application area, 64 KiB (load address 0x08010000)
[0x10004:]      nRF (Bluetooth) firmware image + footer
```

- `tools/qmk_to_hfb.py` loads the stock HFB as a template, replaces **only the STM32 application area** with the QMK binary (within 64 KiB, padded with `0xFF` at the end), and recalculates the two CRCs.
- `[0x10004:]` (nRF firmware + footer) and the container format are left as in the stock file. Only the STM32 application side is turned into QMK; the Bluetooth side uses the stock nRF firmware as it is (see "Bluetooth reuses the stock nRF firmware" in [../../../README.en.md](../../../README.en.md)).

### The two CRCs

- **File CRC** (`[0:2]`): reflected CRC16-CCITT over the whole of `HFB[2:]`. A file integrity check.
- **header64** (`[2:4]`): CRC of the application area. **The bootloader jumps to the application only when this value matches the CRC16 of the actual application** (details in [../hardware.md](../hardware.md)). Raw flashing via DFU does not update this value, so the application does not boot. The HID update (E0–E3) updates this value as well, so the application boots as it is after flashing.

## HID update protocol (E0/E1/E2/E3)

Each command is sent as `AA AA Ex …`, and the normal response comes back as `55 55 Ex …`. The report size is 64 bytes in the update mode (PFU bootloader), and 32 bytes when E0 is sent via QMK's Raw HID.

| Command | Sent | Response | Role |
|---|---|---|---|
| **E0** | `AA AA E0` | `55 55 E0 00 00 00` | Transition to the update mode (clear the EEPROM boot flag and restart) |
| **E1** | `AA AA E1 00 08` + size + CRC | `55 55 E1 00 00 00` | Start of transfer (announces the firmware size and the HFB CRC) |
| **E2** | `AA AA E2 00 <len+2>` + packet_no(LE16) + data | `55 55 E2 00 00 02` + packet_no | Data transfer. At most 57 bytes per packet |
| **E3** | `AA AA E3` | `55 55 E3 00 00 00` (+status) | End of transfer / commit |

- **The transferred body is `HFB[2:]`** (everything that remains after removing the 2 bytes of the file CRC). It is sent with E2, 57 bytes at a time.
- The E2 response returns the packet_no that was sent unchanged, so the match is checked before moving on to the next packet.
- **If the status of the E3 response is non-zero**, the firmware may not have been accepted even though the transfer ran to the end. For that reason the HFB CRC is verified immediately before flashing (`flash.py` stops on a mismatch; this can be explicitly overridden with `--allow-crc-mismatch`).

## QMK-specific command (F0)

A command specific to the QMK in this repository (the VIA-enabled build) that does not exist in the stock protocol. It is accepted over QMK's Raw HID (32 bytes).

| Command | Sent | Response | Role |
|---|---|---|---|
| **F0** | `AA AA F0 00 00` | `55 55 F0 00 00` | Resets the stock keymap table B (`0x08080700–0x08080CFF`, the VIA storage area) to zero, invalidates the valid mark of the stock keymap (`0x08080300`), and then transitions to the update mode by the same procedure as E0 |

- Used before returning to the stock firmware. If you return to stock with VIA data left in table B, the stock firmware loads it as a keymap and key input breaks ([stm32-analysis.md](stm32-analysis.md) §4-2, [../restore-stock.md](../restore-stock.md)).
- `tools/flash.py` sends F0 instead of E0 when the application area of the HFB to be flashed does not contain UTF-16 "QMK" (a stock HFB) and the running firmware is QMK.
- Firmware that does not have F0 (the non-VIA build and old VIA-enabled builds) returns `55 55 F0 01 00`, the same as for any other unknown command. In that case `flash.py` checks whether the firmware responds to the VIA protocol version query (`0x01`); if it is a VIA-enabled build it aborts (because table B cannot be erased), and if it is a non-VIA build it continues with E0 (because table B is not in use).

## Transition to the update mode and re-enumeration

- When E0 (or F0) is sent to a running QMK, QMK returns the response, then disconnects USB (`usbDisconnectBus()` → `usbStop()` → 500 ms wait), and `hhkb_enter_update_mode()` writes the EEPROM and restarts. The keyboard re-enumerates in the update mode of the PFU bootloader.
- The stock firmware can also be moved to the update mode with E0. The stock E0 handling (from `0x0801ABE2`) only writes the EEPROM, waits in an empty loop and then calls `NVIC_SystemReset`; it does not disconnect USB.
- After E0 the USB device re-enumerates and the `/dev/hidraw*` number changes. `flash.py` automatically reselects the relevant interface of the update mode (the equivalent of `input2`). If the process stopped because of the number change, resume by specifying the new device with `--resume` (skips E0 and continues from E1).
- The flash target is the vendor-defined raw HID interface of the update mode. See [../flashing.md](../flashing.md) for how to identify it.

### USB errors (-71) that occur on re-enumeration in the update mode

Observed facts (September 2026, Linux / xhci_hcd):

- When the keyboard enumerates in the update mode (bcdDevice 0.01), the kernel sometimes emits `can't set config #1, error -71`, `usbhid ... can't add hid device: -71`, `can't read configurations, error -71` and the like. This has not been observed on enumeration of QMK (bcdDevice 0.20).
- It occurs intermittently. It happens on any of the E0 / F0 / bootmagic paths, and the first enumeration can also fail when USB is replugged by hand while the keyboard stays in the update mode.
- When replugged by hand, the device often disconnects and reconnects immediately and recovers on the second enumeration. On the E0 and similar paths, it sometimes stays failed and stops without a hidraw being created.
- Because it still occurred after adding the USB disconnect before moving to the update mode on the QMK side, we believe the root cause lies in the combination of the bootloader-side enumeration and the host (not identified). No clear correlation has been confirmed with whether a USB hub is in between or whether the VIA page is open.
- The boot flag has already been cleared at the point the update mode is entered, so the keyboard stays in the update mode even after replugging. When no hidraw appears, `flash.py` prompts for unplugging and replugging USB and waits, and continues from E1 once it appears ([../flashing.md](../flashing.md)).

## Verification before flashing

`tools/qmk_to_hfb.py` shows at conversion time whether `stored_crc` and the recalculated CRC match (`match=True`). This is a self-check immediately after conversion and does not verify build reproducibility. Whether "the intended binary has been reproduced" is checked against the sha256 of the `.bin` (shown by `build.sh` and `qmk_to_hfb.py`) or the CRC of the application area (the value of `header64` after conversion) ([../build.md](../build.md)).
