#!/usr/bin/env python3
import argparse
from pathlib import Path


def crc16_reflected_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
            crc &= 0xFFFF
    return crc


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify or rewrite HHKB .hfb CRC.")
    parser.add_argument("hfb", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    parser.add_argument("--fix", action="store_true", help="write recomputed CRC to output")
    args = parser.parse_args()

    data = bytearray(args.hfb.read_bytes())
    if len(data) < 18:
        raise SystemExit("file is too small to be an HHKB .hfb")

    stored = int.from_bytes(data[:2], "little")
    computed = crc16_reflected_ccitt(data[2:])
    print(f"stored=0x{stored:04x}")
    print(f"computed=0x{computed:04x}")
    print(f"match={stored == computed}")

    tail = bytes(data[-16:])
    print(f"type={tail[:6].decode('ascii', errors='replace')}")
    print(f"version_bytes={tail[6:10].hex()}")

    if args.fix:
        if args.output is None:
            raise SystemExit("--fix requires --output")
        data[:2] = computed.to_bytes(2, "little")
        args.output.write_bytes(data)
        print(f"wrote={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
