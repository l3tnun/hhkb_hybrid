#!/usr/bin/env python3
"""Convert a QMK .bin (linked at 0x08010000) into an HHKB .hfb image.

The stock HFB layout is:

    [0:2]        CRC16-CCITT over HFB[2:]        (file checksum)
    [2:4]        inverted X25 CRC16 over payload (header64)
    [4:4+0x10000] STM32 application (load address 0x08010000)
    [0x10004:]   nRF (Bluetooth) image + footer  (kept from stock HFB)

This script replaces only the STM32 application area with the QMK binary
(which must be <= 64 KiB) and recomputes both CRCs. The template must be the
official HHKB800_FW_A048.hfb (checked by sha256).
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from fix_hhkb_hfb_header64_crc import fix_header64_and_hfb_crc
from hfb_crc import crc16_reflected_ccitt

# Official stock HFBs this conversion has been verified with.
KNOWN_STOCK_HFB = {
    "635b995eb5a15aa50c2cedc60da7d8998fcca8285a09d41d984c07fffcaf9d43": "HHKB800_FW_A048.hfb (A0.48)",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qmk_bin", type=Path, help="QMK .bin (Flash at 0x08010000)")
    parser.add_argument("stock_hfb", type=Path, help="stock HFB (source of nRF section)")
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--allow-unknown-template",
        action="store_true",
        help="accept a stock HFB whose sha256 is not the verified A048 file (not recommended)",
    )
    args = parser.parse_args()

    qmk = args.qmk_bin.read_bytes()
    if len(qmk) > 0x10000:
        raise SystemExit(f"QMK binary too large: {len(qmk)} > 64 KiB")
    stock = args.stock_hfb.read_bytes()
    if len(stock) < 0x10004:
        raise SystemExit("stock HFB too small")
    stock_sha = hashlib.sha256(stock).hexdigest()
    if stock_sha in KNOWN_STOCK_HFB:
        print(f"template: {KNOWN_STOCK_HFB[stock_sha]} (sha256 ok)")
    elif args.allow_unknown_template:
        print(f"WARNING: unknown template sha256 {stock_sha}; continuing (--allow-unknown-template)")
    else:
        raise SystemExit(
            f"unknown stock HFB (sha256 {stock_sha}). Use the official HHKB800_FW_A048.hfb "
            "(sha256 635b995eb5a15aa50c2cedc60da7d8998fcca8285a09d41d984c07fffcaf9d43)."
        )

    out = bytearray(stock)
    payload = qmk.ljust(0x10000, b"\xff")
    out[4 : 4 + 0x10000] = payload

    fixed, old_header, new_header = fix_header64_and_hfb_crc(bytes(out))
    args.output.write_bytes(fixed)
    stored = int.from_bytes(fixed[:2], "little")
    computed = crc16_reflected_ccitt(fixed[2:])
    print(f"wrote {args.output}")
    print(f"qmk size: {len(qmk)} bytes (0x{len(qmk):x}) sha256={hashlib.sha256(qmk).hexdigest()}")
    print(f"header64 0x{old_header:04x}->0x{new_header:04x}")
    print(f"stored_crc=0x{stored:04x} match={stored == computed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
