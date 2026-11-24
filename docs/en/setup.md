# Setup

> English translation of [../setup.md](../setup.md). If the two differ, the Japanese version is authoritative.

This is the preparation you do once before building and flashing. When you are done, go on to [quickstart.md](quickstart.md).

## Requirements

| Item | Details |
|---|---|
| OS | **Flashing is Linux only** (it uses `/dev/hidraw*`). Building is thought to be possible in any environment where Docker runs, but only Linux has been verified |
| git | Used to fetch the submodule (QMK itself) |
| Docker | The build runs inside the official QMK image. **You must be able to `docker run` without `sudo`** (be in the `docker` group, or use rootless Docker) |
| Python | 3.9 or later. The scripts in `tools/` run with the standard library alone (no additional packages needed) |
| coreutils | `sha256sum` is used to check the stock HFB and the build results |
| Disk | `qmk_firmware/` (QMK itself and the required submodules) about 710 MB, the QMK build image about 3.7 GB |
| Network | First time only: GitHub (submodule), ghcr.io (build image), PFU's distribution server (stock HFB) |
| USB cable | One that connects the keyboard directly to the PC. Flashing has also succeeded through a USB hub, but a direct connection is recommended for isolating problems |

Example of an environment where this was verified to work: Ubuntu 26.04 LTS, Docker 29.7, git 2.53, Python 3.14.

## 1. Fetch the repository

```sh
git clone <URL of this repository>
cd <the cloned directory>
git submodule update --init qmk_firmware
```

- `qmk_firmware` (QMK itself) is a submodule pinned to a specific commit. If you fetch only QMK itself, `tools/build.sh` automatically fetches the lower-level submodules needed for the build (`lib/chibios` etc.).
- You can also fetch it with `git clone --recursive`, but that takes more time and space because it fetches all of QMK's submodules.
- If you run `tools/build.sh` without `qmk_firmware` itself present, it shows the command above and exits.

## 2. Check Docker

```sh
docker run --rm hello-world
```

If you get `permission denied`, follow Docker's official instructions to add your user to the `docker` group, then log in again. The build image (`ghcr.io/qmk/qmk_cli`, pinned by digest) is fetched automatically the first time `tools/build.sh` runs.

## 3. Access permission to the keyboard (udev rule)

`tools/flash.py` and VIA (the browser's WebHID) read and write the keyboard's `/dev/hidraw*`. By default on Linux only root can access it, so allow ordinary users with a udev rule.

### Create the rule

Create `/etc/udev/rules.d/70-hhkb-hybrid.rules` (administrator privileges are required).

```
# HHKB Professional HYBRID: stock firmware / PFU update mode / QMK are all 04fe:0021
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="04fe", ATTRS{idProduct}=="0021", TAG+="uaccess"
```

- `TAG+="uaccess"` gives access to the logged-in desktop user (typical desktop environments that use systemd-logind). Keep the number in the file name lower than `73` (because the rule needs to be read before `73-seat-late.rules`).
- In environments without a login session, such as over SSH, allow access by group instead.

  ```
  SUBSYSTEM=="hidraw", ATTRS{idVendor}=="04fe", ATTRS{idProduct}=="0021", GROUP="plugdev", MODE="0660"
  ```

  In this case, add your user to the `plugdev` group and log in again (the group name differs between distributions).

### Apply and check

```sh
sudo udevadm control --reload-rules
```

After applying, **unplug and replug the keyboard's USB cable**. Then check.

```sh
python3 tools/flash.py --list
```

If `(no read/write permission: see docs/setup.md)` does not appear on any line of the list, the setup is done.

### Notes

- **Temporary permission with `sudo chmod` cannot be used.** When the keyboard switches to the update mode during flashing, USB is re-enumerated and the hidraw number changes, so the new node does not get the permission.
- Flashing itself also works with `sudo python3 tools/flash.py ...`, but the trace logs and the like end up owned by root. Using the udev rule is recommended.
- VIA in Chrome / Edge can also connect with the same rule. Browsers installed with Flatpak or Snap may be unable to access hidraw because of sandbox restrictions (unverified).

## 4. Prepare the stock HFB

It is used as the template for the conversion and when going back to stock. It is a file officially distributed by PFU. **This repository does not distribute it** (it contains a PFU-made binary and cannot be redistributed).

```sh
mkdir -p local
curl -fL -o local/HHKB800_FW_A048.hfb https://origin.pfultd.com/downloads/hhkb/HHKB800_FW_A048.hfb
sha256sum local/HHKB800_FW_A048.hfb
python3 tools/hfb_crc.py local/HHKB800_FW_A048.hfb
```

Expected values:

```
635b995eb5a15aa50c2cedc60da7d8998fcca8285a09d41d984c07fffcaf9d43  local/HHKB800_FW_A048.hfb
```

```
stored=0x3b01
computed=0x3b01
match=True
type=AHUX01
version_bytes=0a000408
```

- The size is 282,672 bytes.
- Do not use a file whose sha256 differs. `tools/qmk_to_hfb.py` also aborts the conversion for any template other than the known A0.48.

## The `local/` folder

`local/` at the top of the repository is covered by `.gitignore` (the directory itself is kept by `.gitkeep` and can be used right after clone). Use it as the place for files that must not be committed, such as the following.

| File | Example |
|---|---|
| Stock HFB | `local/HHKB800_FW_A048.hfb` |
| Converted HFBs | `local/hhkb_hybrid_default.hfb`, `local/hhkb_hybrid_via.hfb` |
| Flashing traces | `local/flash_<date>.jsonl` |

An HFB contains the PFU-made Bluetooth (nRF) firmware, so **converted HFBs cannot be distributed either**.
