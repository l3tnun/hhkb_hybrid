#!/usr/bin/env python3
import argparse
import glob
import json
import os
import struct
import time
from pathlib import Path

from hfb_crc import crc16_reflected_ccitt


VID = 0x04FE
# HHKB Professional HYBRID (English layout). Stock firmware, the PFU update mode
# and this repository's QMK all use 04fe:0021; no other model is supported.
PIDS = {0x0021}
REPORT_SIZE = 64
CHUNK_SIZE = 57
UPDATE_MODE_PID = 0x0021
# keyboards/hhkb_hybrid (QMK) answers E0 on its Raw HID interface, whose
# reports are fixed at 32 bytes by QMK (RAW_EPSIZE). It uses the stock PID, so
# it is told apart by its product name ("HHKB Hybrid QMK").
QMK_NAME_MARK = "QMK"
QMK_REPORT_SIZE = 32

# First bytes of the HID report descriptor of the interface to flash through.
STOCK_VENDOR_PREFIX = bytes([0x06, 0x00, 0xFF])  # usage page 0xFF00: stock FW / update mode (input2)
QMK_RAW_PREFIX = bytes([0x06, 0x60, 0xFF])  # usage page 0xFF60: QMK Raw HID (input1)


def hex_bytes(data: bytes) -> str:
    return " ".join(f"{byte:02x}" for byte in data)


def hidraw_devices():
    for path in glob.glob("/dev/hidraw*"):
        name = os.path.basename(path)
        sys_base = f"/sys/class/hidraw/{name}/device"
        uevent_path = os.path.join(sys_base, "uevent")
        try:
            uevent = Path(uevent_path).read_text()
        except OSError:
            continue
        if f"HID_ID=0003:{VID:08X}:" not in uevent:
            continue
        for pid in PIDS:
            if f":{pid:08X}" in uevent:
                yield path, uevent


def device_uevent(path: str) -> str:
    try:
        return Path(f"/sys/class/hidraw/{os.path.basename(path)}/device/uevent").read_text()
    except OSError:
        return ""


def uevent_pid(uevent: str):
    for line in uevent.splitlines():
        if line.startswith("HID_ID="):
            return int(line.rsplit(":", 1)[1], 16)
    return None


def uevent_phys(uevent: str) -> str:
    for line in uevent.splitlines():
        if line.startswith("HID_PHYS="):
            return line.split("=", 1)[1]
    return ""


def uevent_name(uevent: str) -> str:
    for line in uevent.splitlines():
        if line.startswith("HID_NAME="):
            return line.split("=", 1)[1]
    return ""


def uevent_is_qmk(uevent: str) -> bool:
    return QMK_NAME_MARK in uevent_name(uevent)


def descriptor_prefix(path: str) -> bytes:
    try:
        return Path(f"/sys/class/hidraw/{os.path.basename(path)}/device/report_descriptor").read_bytes()[:3]
    except OSError:
        return b""


def usb_device_dir(path: str) -> str:
    """sysfs directory of the USB device owning a hidraw node (hidraw -> HID -> interface -> device)."""
    hid = os.path.realpath(f"/sys/class/hidraw/{os.path.basename(path)}/device")
    return os.path.dirname(os.path.dirname(hid))


def sysfs_attr(directory: str, name: str) -> str:
    try:
        return Path(directory, name).read_text().strip()
    except OSError:
        return ""


def is_flash_target(path: str, uevent: str) -> bool:
    prefix = descriptor_prefix(path)
    return prefix == (QMK_RAW_PREFIX if uevent_is_qmk(uevent) else STOCK_VENDOR_PREFIX)


def hhkb_usb_devices() -> list[str]:
    """USB devices (not interfaces) with a supported VID/PID."""
    found = []
    for directory in sorted(glob.glob("/sys/bus/usb/devices/*")):
        if ":" in os.path.basename(directory):
            continue
        try:
            vid = int(sysfs_attr(directory, "idVendor"), 16)
            pid = int(sysfs_attr(directory, "idProduct"), 16)
        except ValueError:
            continue
        if vid == VID and pid in PIDS:
            found.append(os.path.realpath(directory))
    return found


def describe_state(device_dir: str) -> str:
    if QMK_NAME_MARK in sysfs_attr(device_dir, "product"):
        return "QMK (this repository)"
    return "stock firmware or PFU update mode (keys work = stock, keys do nothing = update mode)"


def print_device_list() -> None:
    usb_devices = hhkb_usb_devices()
    if not usb_devices:
        print(f"no HHKB ({VID:04x}:0021) found. Check the cable and USB port.")
        return
    nodes = list(hidraw_devices())
    for directory in usb_devices:
        print(
            f"USB {os.path.basename(directory)}: product='{sysfs_attr(directory, 'product')}' "
            f"bcdDevice={sysfs_attr(directory, 'bcdDevice')} serial={sysfs_attr(directory, 'serial') or '-'}"
        )
        print(f"  state: {describe_state(directory)}")
        mine = [(p, u) for p, u in nodes if usb_device_dir(p) == directory]
        if not mine:
            print("  no hidraw nodes (USB enumeration may have failed: replug the cable)")
        for path, uevent in sorted(mine, key=lambda item: int(item[0].rsplit("hidraw", 1)[1])):
            iface = uevent_phys(uevent).rsplit("/", 1)[-1]
            prefix = descriptor_prefix(path).hex(" ") or "??"
            role = "FLASH TARGET" if is_flash_target(path, uevent) else "not a flash target"
            access = "" if os.access(path, os.R_OK | os.W_OK) else "  (no read/write permission: see docs/setup.md)"
            print(f"  {path}  {iface}  descriptor={prefix}  {role}{access}")
        if mine and not any(is_flash_target(p, u) for p, u in mine):
            print("  WARNING: no FLASH TARGET (USB enumeration may have partly failed: replug the cable)")
    if len(usb_devices) > 1:
        print(f"WARNING: {len(usb_devices)} HHKBs connected. Connect only the keyboard to flash.")


def find_update_mode_input2():
    candidates = [
        path
        for path, uevent in hidraw_devices()
        if uevent_pid(uevent) == UPDATE_MODE_PID
        and uevent_phys(uevent).endswith("input2")
        and not uevent_is_qmk(uevent)
        and descriptor_prefix(path) == STOCK_VENDOR_PREFIX
    ]
    return candidates[0] if len(candidates) == 1 else None


def hfb_is_qmk(hfb: bytes) -> bool:
    """True if the HFB's STM32 application is a QMK build (USB product string
    "HHKB Hybrid QMK", stored as UTF-16LE in the descriptor)."""
    app = hfb[4:4 + 0x10000]
    return "QMK".encode("utf-16-le") in app


def wait_for_update_mode_replug(timeout: float):
    """Wait for the update-mode interface after a failed re-enumeration.

    The PFU bootloader's enumeration sometimes fails on the host (kernel log:
    usbhid ... -71) and then no hidraw appears; a USB replug recovers it. The
    keyboard stays in update mode (E0 cleared the boot flag), so the transfer can
    continue from E1 once the interface shows up.
    """
    print("update-mode HID interface not found (USB enumeration may have failed).")
    print(f"unplug and replug the USB cable; waiting up to {int(timeout)}s ...")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        found = find_update_mode_input2()
        if found is not None:
            time.sleep(2.0)  # let udev apply the hidraw permissions
            print(f"update-mode interface found: {found}")
            return found
        time.sleep(0.5)
    return None


class HHKBUpdater:
    def __init__(self, path: str, trace=None):
        self.path = path
        self.trace = trace
        self.report_size = QMK_REPORT_SIZE if uevent_is_qmk(device_uevent(path)) else REPORT_SIZE
        self.fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)

    def close(self):
        os.close(self.fd)

    def _read(self, timeout=60.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data = os.read(self.fd, self.report_size + 1)
                if data:
                    if len(data) == self.report_size + 1 and data[0] == 0:
                        return data[1:]
                    return data[: self.report_size]
            except BlockingIOError:
                pass
            time.sleep(0.01)
        raise TimeoutError("timed out waiting for HHKB response")

    def send(self, payload: bytes, expect_prefix: bytes, timeout=60.0, label="cmd", packet_no=None):
        if len(payload) > self.report_size:
            raise ValueError("payload too large")
        report = b"\x00" + payload.ljust(self.report_size, b"\x00")
        os.write(self.fd, report)
        response = self._read(timeout)
        if self.trace is not None:
            self.trace.write(
                json.dumps(
                    {
                        "time": time.time(),
                        "device": self.path,
                        "label": label,
                        "packet_no": packet_no,
                        "tx": payload.hex(),
                        "rx": response.hex(),
                        "rx_prefix": response[:16].hex(),
                        "rx_status": response[3] if len(response) > 3 else None,
                    },
                    sort_keys=True,
                )
                + "\n"
            )
            self.trace.flush()
        if not response.startswith(expect_prefix):
            status = response[3] if len(response) > 3 else None
            suffix = "" if status is None else f" status=0x{status:02x}"
            if label == "E3" and len(response) >= 19 and response[:3] == b"\x55\x55\xe3":
                suffix += f" e3_detail={hex_bytes(response[3:19])}"
            raise RuntimeError(f"unexpected {label} response: {response[:16].hex()}{suffix}")
        return response

    def firmup_mode_change(self):
        self.send(bytes([0xAA, 0xAA, 0xE0]), bytes([0x55, 0x55, 0xE0, 0, 0, 0]), label="E0")

    def stock_revert_mode_change(self) -> bool:
        """QMK (VIA build) only: clear the stock keymap window, then enter the update mode.

        Returns False when the running firmware does not know the command
        (answer 55 55 F0 01); the caller then falls back to E0.
        """
        response = self.send(bytes([0xAA, 0xAA, 0xF0, 0, 0]), bytes([0x55, 0x55, 0xF0]), label="F0")
        return response[3] == 0

    def is_via_build(self, timeout=1.0) -> bool:
        """True if the running QMK answers the VIA protocol-version query (0x01)."""
        os.write(self.fd, b"\x00" + bytes([0x01]).ljust(self.report_size, b"\x00"))
        try:
            response = self._read(timeout)
        except TimeoutError:
            return False
        return response[:1] == b"\x01"

    def firmup_start(self, firm_size: int, crc: bytes):
        payload = bytearray([0xAA, 0xAA, 0xE1, 0, 8])
        payload.extend(struct.pack("<I", firm_size))
        payload.extend(crc)
        self.send(payload, bytes([0x55, 0x55, 0xE1, 0, 0, 0]), label="E1")

    def firmup_send(self, hfb: bytes):
        body = hfb[2:]
        packets = (len(body) + CHUNK_SIZE - 1) // CHUNK_SIZE
        for packet_no in range(packets):
            chunk = body[packet_no * CHUNK_SIZE:(packet_no + 1) * CHUNK_SIZE]
            payload = bytearray([0xAA, 0xAA, 0xE2, 0, len(chunk) + 2])
            payload.extend(struct.pack("<H", packet_no))
            payload.extend(chunk)
            response = self.send(
                payload,
                bytes([0x55, 0x55, 0xE2, 0, 0, 2]),
                label="E2",
                packet_no=packet_no,
            )
            got = response[6] | (response[7] << 8)
            if got != packet_no:
                raise RuntimeError(f"packet ack mismatch: sent={packet_no} got={got}")
            if packet_no % 128 == 0:
                print(f"sent {packet_no}/{packets}")

    def firmup_end(self):
        self.send(bytes([0xAA, 0xAA, 0xE3]), bytes([0x55, 0x55, 0xE3, 0, 0, 0]), label="E3")


def main() -> int:
    parser = argparse.ArgumentParser(description="Experimental Linux HHKB .hfb updater over hidraw.")
    parser.add_argument("hfb", type=Path, nargs="?", help="HFB to write (not needed with --list)")
    parser.add_argument(
        "--device",
        help="hidraw path of the flash target, for example /dev/hidraw3 (see --list); "
        "if omitted, the only FLASH TARGET is used",
    )
    parser.add_argument("--list", action="store_true", help="show connected HHKBs, their state and the flash target")
    parser.add_argument("--dry-run", action="store_true", help="check the HFB CRC and packet count, then exit")
    parser.add_argument(
        "--resume",
        action="store_true",
        help="keyboard is already in the PFU update mode: skip the mode change and start from E1",
    )
    parser.add_argument("--trace-log", type=Path, help="write JSONL HID command/response trace")
    parser.add_argument(
        "--replug-timeout",
        type=float,
        default=180.0,
        help="seconds to wait for a USB replug when the update-mode interface does not appear after E0",
    )
    parser.add_argument(
        "--allow-crc-mismatch",
        action="store_true",
        help="allow stored/computed HFB CRC mismatch for explicit diagnostic probes",
    )
    args = parser.parse_args()

    devices = list(hidraw_devices())
    if args.list:
        print_device_list()
        return 0
    if args.hfb is None:
        parser.error("the HFB file is required (except with --list)")

    data = args.hfb.read_bytes()
    stored = int.from_bytes(data[:2], "little")
    computed = crc16_reflected_ccitt(data[2:])
    if stored != computed and not args.allow_crc_mismatch:
        raise SystemExit(f"CRC mismatch: stored=0x{stored:04x} computed=0x{computed:04x}")

    body_size = len(data) - 2
    packets = (body_size + CHUNK_SIZE - 1) // CHUNK_SIZE
    header_word = int.from_bytes(data[2:4], "little") if len(data) >= 4 else 0
    print(f"size={len(data)} crc=0x{stored:04x} computed_crc=0x{computed:04x} match={stored == computed}")
    print(f"header_word=0x{header_word:04x} e2_body_size={body_size} e2_packets={packets}")
    print(f"hfb_kind={'QMK' if hfb_is_qmk(data) else 'stock (no QMK)'}")
    if args.dry_run:
        return 0

    usb_devices = hhkb_usb_devices()
    if len(usb_devices) != 1:
        raise SystemExit(
            f"expected exactly one HHKB connected, found {len(usb_devices)}. "
            "Disconnect the other keyboards (or connect the one to flash) and run --list."
        )

    device = args.device
    if device is None:
        targets = [path for path, uevent in devices if is_flash_target(path, uevent)]
        if len(targets) != 1:
            raise SystemExit("specify --device; use --list to find the FLASH TARGET")
        device = targets[0]

    uevent = device_uevent(device)
    if not any(path == device for path, _ in devices):
        raise SystemExit(f"{device} is not an HHKB hidraw node; use --list")
    if not is_flash_target(device, uevent):
        raise SystemExit(
            f"{device} is not the flash target interface (descriptor {descriptor_prefix(device).hex(' ')}); "
            "use the FLASH TARGET shown by --list"
        )
    if args.resume and uevent_is_qmk(uevent):
        raise SystemExit(
            "--resume is only for a keyboard already in the PFU update mode; "
            "this one is running QMK, so run without --resume"
        )
    if not os.access(device, os.R_OK | os.W_OK):
        raise SystemExit(f"no read/write permission for {device}; set up the udev rule (docs/setup.md)")

    print(f"device={device}")

    trace = args.trace_log.open("a") if args.trace_log is not None else None
    updater = None
    in_update_mode = args.resume
    try:
        try:
            if not args.resume:
                updater = HHKBUpdater(device, trace=trace)
                # Going from QMK back to the stock firmware: the VIA build keeps its
                # settings in the stock keymap table B, which the stock firmware
                # would read as its keymap. Ask QMK to clear it first (F0).
                if uevent_is_qmk(device_uevent(device)) and not hfb_is_qmk(data):
                    print("stock HFB on a QMK keyboard: clearing the stock keymap window (F0)")
                    if not updater.stock_revert_mode_change():
                        if updater.is_via_build():
                            raise SystemExit(
                                "this QMK VIA build cannot clear the stock keymap window (no F0); "
                                "the stock firmware would read its VIA data as the keymap. "
                                "Flash a newer QMK build first, or enter the update mode with "
                                "bootmagic on a build that clears it."
                            )
                        print("F0 not supported (non-VIA QMK build, window unused); using E0")
                        updater.firmup_mode_change()
                else:
                    updater.firmup_mode_change()
                in_update_mode = True
                updater.close()
                updater = None
                for count in range(10, 0, -1):
                    print(f"waiting for firmware-update mode... {count}s")
                    time.sleep(1.0)
                # Only the update-mode vendor interface (input2, 06 00 ff) is used. A
                # partly failed enumeration (-71) can leave other interfaces, possibly
                # under the old hidraw number: never fall back to them.
                update_input2 = find_update_mode_input2()
                if update_input2 is None:
                    update_input2 = wait_for_update_mode_replug(args.replug_timeout)
                    if update_input2 is None:
                        raise SystemExit(
                            "update-mode interface did not appear; replug the keyboard, find the FLASH TARGET "
                            f"with --list, then rerun: python3 tools/flash.py --resume --device /dev/hidrawN {args.hfb}"
                        )
                if update_input2 != device:
                    print(f"device changed after entering update mode: {update_input2}")
                device = update_input2
            updater = HHKBUpdater(device, trace=trace)
            updater.firmup_start(len(data), data[:2])
            updater.firmup_send(data)
            updater.firmup_end()
        except (OSError, TimeoutError, RuntimeError) as exc:
            if not in_update_mode:
                raise SystemExit(
                    f"mode change failed: {exc}. Nothing was written. Check --list: if it shows "
                    "'HHKB-Hybrid' and keys do nothing, the keyboard is already in the update mode, so "
                    "rerun with --resume; otherwise retry."
                )
            if args.resume:
                raise SystemExit(
                    f"transfer failed: {exc}\n"
                    "If keys still work, the keyboard is running the stock firmware, not the update mode: "
                    "rerun without --resume. If keys do nothing, replug the USB cable if needed, find the "
                    "FLASH TARGET with --list, then rerun:\n"
                    f"  python3 tools/flash.py --resume --device /dev/hidrawN {args.hfb}"
                )
            raise SystemExit(
                f"transfer failed: {exc}\n"
                "The keyboard stays in the PFU update mode (keys do nothing). Replug the USB cable if needed, "
                "find the FLASH TARGET with --list, then rerun:\n"
                f"  python3 tools/flash.py --resume --device /dev/hidrawN {args.hfb}"
            )
        finally:
            if updater is not None:
                updater.close()
    finally:
        if trace is not None:
            trace.close()
    print("done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
