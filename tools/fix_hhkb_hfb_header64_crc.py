#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from hfb_crc import crc16_reflected_ccitt


def crc16_x25(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
            crc &= 0xFFFF
    return crc ^ 0xFFFF


def header64_word(hfb: bytes) -> int:
    payload64 = hfb[4 : 4 + 0x10000]
    if len(payload64) != 0x10000:
        raise ValueError("HFB is too small to contain a 64 KiB application prefix")
    return crc16_x25(payload64) ^ 0xFFFF


def fix_header64_and_hfb_crc(hfb: bytes) -> tuple[bytes, int, int]:
    out = bytearray(hfb)
    old_header = int.from_bytes(out[2:4], "little")
    new_header = header64_word(out)
    out[2:4] = new_header.to_bytes(2, "little")
    hfb_crc = crc16_reflected_ccitt(bytes(out[2:]))
    out[0:2] = hfb_crc.to_bytes(2, "little")
    return bytes(out), old_header, new_header


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Recompute HHKB HFB file[2:4] as inverted X25 CRC16 over the first 64 KiB payload."
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    fixed, old_header, new_header = fix_header64_and_hfb_crc(args.input.read_bytes())
    args.output.write_bytes(fixed)
    stored = int.from_bytes(fixed[:2], "little")
    computed = crc16_reflected_ccitt(fixed[2:])
    print(
        f"{args.output}: header64 0x{old_header:04x}->0x{new_header:04x} "
        f"hfb_crc=0x{stored:04x} match={stored == computed}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
