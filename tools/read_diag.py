#!/usr/bin/env python3
"""Read the reset diagnostics of a keyboard running this repository's QMK.

Sends "AA AA C0 <offset> 00" to the QMK Raw HID interface (the FLASH TARGET of
`flash.py --list`) and decodes the record kept in RAM across resets
(keyboards/hhkb_hybrid/boards/HHKB_HYBRID/hhkb_diag.h). Nothing is written to
the keyboard's flash or EEPROM. See docs/troubleshooting.md.
"""
import argparse
import os
import re
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from flash import QMK_REPORT_SIZE, hhkb_usb_devices, hidraw_devices, is_flash_target, uevent_is_qmk  # noqa: E402

MAGIC = 0x48444732
CMD = 0xC0
CHUNK = 24

STAGES = [
    "BOOT (before the first housekeeping run)",
    "SCAN (matrix scan)",
    "LOOP (QMK keyboard task, USB / BT reports)",
    "HK_NRF (housekeeping: nRF link)",
    "HK_BT (housekeeping: BT output / reconnect)",
    "HK_POWER (housekeeping: button, auto-sleep, clock switch)",
    "IDLE (battery: sleep until the next scan)",
    "CLOCK_DOWN (switching to the 4 MHz battery clock)",
    "CLOCK_UP (switching back to 32 MHz)",
    "OFF (power off: waiting for the nRF idle)",
    "STOP (power off: STOP loop)",
    "USB_FAILSAFE (watchdog feed stopped: USB not configured in time)",
    "BT_FLUSH (waiting for the nRF to read a report)",
]
UPDATE_REASONS = ["none", "E0 (flash.py)", "F0 (flash.py, revert to stock)", "QK_BOOT or bootmagic", "repeated watchdog resets / faults"]
FLAGS = [(0x01, "USB power"), (0x02, "4 MHz clock"), (0x04, "BT output"), (0x08, "USB configured"), (0x10, "nRF up"), (0x20, "BT link")]
CSR_FLAGS = [(31, "LPWRRST"), (30, "WWDGRST"), (29, "IWDGRST"), (28, "SFTRST"), (27, "PORRST"), (26, "PINRST"), (25, "OBLRST"), (24, "FWRST")]


def batt_text(value: int) -> str:
    if value == 0xFF:
        return "not read"
    if value & 0x04:
        return "0 % (PB9 low)"
    if value & 0x02:
        return "<= 5 % (PB8 low)"
    if value & 0x01:
        return "<= 15 % (PB7 low)"
    return "above 15 % (PB7/8/9 high)"


EXCEPTIONS = {2: "NMI", 3: "HardFault", 11: "SVCall", 14: "PendSV", 15: "SysTick"}

LAYOUT = "<IBBBBIIBBBBIIBBBBIIIBBBB"
FIELDS = [
    "magic", "streak", "wdt_resets", "faults", "update_reason", "boot_csr",
    "uptime_ms", "stage", "flags", "fault_pending", "boots", "spi_irqs",
    "inc_uptime_ms", "inc_stage", "inc_flags", "inc_kind", "inc_ipsr", "inc_spi_irqs", "inc_pc_psp", "inc_pc_msp",
    "create_csr", "por_kept", "batt", "inc_batt",
]


def find_target(device=None) -> str:
    devices = hhkb_usb_devices()
    if len(devices) != 1:
        raise SystemExit(f"expected exactly one HHKB connected, found {len(devices)}.")
    targets = [p for p, u in hidraw_devices() if uevent_is_qmk(u) and is_flash_target(p, u)]
    if len(targets) != 1:
        raise SystemExit("no QMK Raw HID interface found: the keyboard is not running this repository's QMK (flash.py --list).")
    # hidraw numbers change on every reconnect: never send to another interface.
    if device and os.path.realpath(device) != os.path.realpath(targets[0]):
        raise SystemExit(f"{device} is not the QMK Raw HID interface (it is {targets[0]} now; see flash.py --list).")
    return targets[0]


def read_chunk(fd: int, offset: int) -> bytes:
    os.write(fd, b"\x00" + bytes([0xAA, 0xAA, CMD, offset, 0x00]).ljust(QMK_REPORT_SIZE, b"\x00"))
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        try:
            data = os.read(fd, QMK_REPORT_SIZE + 1)
        except BlockingIOError:
            time.sleep(0.01)
            continue
        if len(data) == QMK_REPORT_SIZE + 1 and data[0] == 0:
            data = data[1:]
        if data[:3] != bytes([0x55, 0x55, CMD]):
            continue  # not our answer
        if data[3] != 0x00:
            raise SystemExit("the firmware does not support the diagnostics read (built before it was added).")
        return data
    raise SystemExit("timed out waiting for the keyboard (close the VIA page and retry).")


def load_symbols(map_path: Path):
    symbols = []
    for line in map_path.read_text(errors="replace").splitlines():
        m = re.match(r"^\s+0x(0*8[0-9a-f]{6})\s+([A-Za-z_][\w.]*)\s*$", line)
        if m:
            symbols.append((int(m.group(1), 16), m.group(2)))
    return sorted(set(symbols))


def symbolize(pc: int, symbols) -> str:
    if not pc:
        return "-"
    pc &= ~1
    best = None
    for addr, name in symbols:
        if addr <= pc:
            best = (addr, name)
        else:
            break
    text = f"0x{pc:08x}"
    if best and 0x08010000 <= pc < 0x08020000:
        text += f" ({best[1]}+0x{pc - best[0]:x})"
    return text


def flag_text(value: int, table) -> str:
    return ", ".join(name for bit, name in table if value & bit) or "-"


def main() -> int:
    parser = argparse.ArgumentParser(description="Read the reset diagnostics of this repository's QMK (read only).")
    parser.add_argument("--device", help="QMK Raw HID hidraw node (default: the FLASH TARGET of flash.py --list)")
    parser.add_argument("--map", type=Path, help="map file of the running build, to name the fault PC (qmk_firmware/.build/*.map)")
    args = parser.parse_args()

    path = find_target(args.device)
    fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)
    try:
        first = read_chunk(fd, 0)
        size = first[5]
        raw = bytearray(first[6 : 6 + min(CHUNK, size)])
        while len(raw) < size:
            chunk = read_chunk(fd, len(raw))
            raw += chunk[6 : 6 + min(CHUNK, size - len(raw))]
    finally:
        os.close(fd)

    expected = struct.calcsize(LAYOUT)
    if size < expected:
        raise SystemExit(f"unexpected record size {size} (expected {expected}): use the read_diag.py of the same version")
    d = dict(zip(FIELDS, struct.unpack_from(LAYOUT, raw)))
    print(f"device={path} size={size} raw={raw.hex()}")
    if d["magic"] != MAGIC:
        print("no record (cleared by a power-on reset)")
        return 0
    symbols = load_symbols(args.map) if args.map else []

    def stage(value):
        return STAGES[value] if value < len(STAGES) else f"unknown ({value})"

    csr = d["boot_csr"]
    def csr_text(value):
        return ", ".join(n for b, n in CSR_FLAGS if value >> b & 1) or "-"

    print(f"boots since the record was created: {d['boots']} (created at a start with {csr_text(d['create_csr'] << 24)})")
    print(f"power-on / brown-out resets that kept the record: {d['por_kept']}")
    print(f"last start: reset flags {csr_text(csr)} (RCC_CSR=0x{csr:08x})")
    print(f"watchdog resets: {d['wdt_resets']}  faults: {d['faults']}  current streak: {d['streak']}")
    reason = d["update_reason"]
    print(f"last update-mode request: {UPDATE_REASONS[reason] if reason < len(UPDATE_REASONS) else reason}")
    print(f"now: up {d['uptime_ms'] / 1000:.1f} s, {stage(d['stage'])}, {flag_text(d['flags'], FLAGS)}, SPI IRQs {d['spi_irqs']}, battery {batt_text(d['batt'])}")
    kind = d["inc_kind"]
    if kind == 0:
        print("last incident: none")
        return 0
    print(f"last incident: {'watchdog reset' if kind == 1 else 'fault' if kind == 2 else kind}")
    print(f"  after {d['inc_uptime_ms'] / 1000:.1f} s, in {stage(d['inc_stage'])}")
    print(f"  state: {flag_text(d['inc_flags'], FLAGS)}")
    print(f"  SPI IRQs since that start: {d['inc_spi_irqs']}")
    print(f"  battery: {batt_text(d['inc_batt'])}")
    if kind == 2:
        ipsr = d["inc_ipsr"]
        name = EXCEPTIONS.get(ipsr, f"IRQ {ipsr - 16}" if ipsr >= 16 else f"exception {ipsr}")
        print(f"  exception: {name}")
        print(f"  PC (thread stack): {symbolize(d['inc_pc_psp'], symbols)}")
        print(f"  PC (interrupt stack): {symbolize(d['inc_pc_msp'], symbols)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
