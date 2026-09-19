#!/usr/bin/env python3
"""Audit the STM32L0 data-EEPROM addresses a QMK .bin refers to.

Scans the image's word-aligned little-endian values (literal pools) for
addresses in the data EEPROM (0x08080000..0x080817FF) and fails if any is
outside the allowlist below (docs/via.md). This audits constants, not every
run-time address: a computed address is covered by the entry of its base
(e.g. the VIA window base 0x08080700).
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

EEPROM_START = 0x08080000
EEPROM_END = 0x08081800

# eeprom_hhkb.c: stock keymap table B, base and end bound
VIA_WINDOW = (0x08080700, 0x08080D00)

ALLOWED = {
    0x08080300: "stock keymap magic (invalidated on EEPROM format)",
    0x08080360: "boot flag (update-mode entry)",
    0x08080370: "app CRC (update-mode entry)",
    0x08080520: "stock sleep byte / JIS-on-US flag",
    0x08081100: "calibration base (read)",
    0x08081200: "calibration offset (read)",
    0x08081350: "update request (update-mode entry)",
}


def main() -> int:
    parser = argparse.ArgumentParser(description="List EEPROM addresses referenced by a QMK .bin.")
    parser.add_argument("bin", type=Path)
    parser.add_argument("--load-address", type=lambda s: int(s, 0), default=0x08010000)
    args = parser.parse_args()

    data = args.bin.read_bytes()
    refs: dict[int, list[int]] = {}
    for off in range(0, len(data) - 3, 4):
        value = struct.unpack_from("<I", data, off)[0]
        if EEPROM_START <= value < EEPROM_END:
            refs.setdefault(value, []).append(args.load_address + off)

    bad = 0
    for value in sorted(refs):
        if value in ALLOWED:
            note = ALLOWED[value]
        elif VIA_WINDOW[0] <= value <= VIA_WINDOW[1]:
            note = "VIA EEPROM window"
        else:
            note = "NOT ALLOWED"
            bad += 1
        where = ", ".join(f"0x{a:08x}" for a in refs[value][:4])
        print(f"0x{value:08x}  x{len(refs[value])}  {note}  (at {where})")

    print("result:", "NG" if bad else "OK")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
