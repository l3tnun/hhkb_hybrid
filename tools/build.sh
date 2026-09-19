#!/usr/bin/env bash
# Build the hhkb_hybrid QMK firmware reproducibly.
#
# Uses the pinned qmk_firmware submodule (so the exact same core is used by
# everyone) and the official QMK Docker image. The keyboard definition in this
# repo's keyboards/ is the source of truth; it is staged into the pinned core
# for the build (QMK 0.34.4 resolves keyboards only under qmk_firmware/keyboards).
#
# Usage: tools/build.sh [keymap] [qmk compile options...]
#   tools/build.sh                                  # keymap default
#   tools/build.sh via                              # VIA keymap
#   tools/build.sh via -e HHKB_JIS_US_TOGGLE=no     # extra options go to qmk compile
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QMK_FW="$ROOT/qmk_firmware"
KB="hhkb_hybrid"
KM="${1:-default}"
[ $# -gt 0 ] && shift
EXTRA=("$@")

# QMK build image, pinned by digest so that everyone builds with the same
# toolchain (QMK CLI 1.2.0, arm-none-eabi-gcc 15.2.0).
QMK_IMAGE="ghcr.io/qmk/qmk_cli@sha256:b7d7fa8fb4432b569931de5ad59098cb788f440ed61a62c5126746b71aee0f4a"

if [ ! -e "$QMK_FW/Makefile" ]; then
    echo "qmk_firmware submodule missing. Run:" >&2
    echo "  git submodule update --init qmk_firmware" >&2
    exit 1
fi

# Ensure the submodules the build needs are present (pinned by qmk_firmware).
need_submodules=(lib/chibios lib/chibios-contrib lib/lufa lib/printf)
missing=()
for m in "${need_submodules[@]}"; do
    [ -e "$QMK_FW/$m/.git" ] || [ -n "$(ls -A "$QMK_FW/$m" 2>/dev/null)" ] || missing+=("$m")
done
if [ ${#missing[@]} -gt 0 ]; then
    echo "Initialising QMK submodules: ${missing[*]}"
    git -C "$QMK_FW" submodule update --init "${missing[@]}"
fi

# Stage this repo's keyboard definition into the pinned core (source of truth is here).
rm -rf "$QMK_FW/keyboards/$KB"
cp -r "$ROOT/keyboards/$KB" "$QMK_FW/keyboards/"

# Fresh output: .build is removed on every run, and QMK also leaves a copy of
# each firmware in the qmk_firmware/ root (stale copies are removed too).
rm -rf "$QMK_FW/.build"
rm -f "$QMK_FW/${KB}_"*.bin "$QMK_FW/${KB}_"*.hex
docker run --rm -u "$(id -u):$(id -g)" \
    -v "$QMK_FW":/qmk_firmware \
    -e SKIP_GIT=1 -e SKIP_VERSION=1 \
    -w /qmk_firmware \
    "$QMK_IMAGE" \
    qmk compile -kb "$KB" -km "$KM" ${EXTRA[@]+"${EXTRA[@]}"}

BIN="$QMK_FW/.build/${KB}_${KM}.bin"
echo
echo "Built: $BIN"
sha256sum "$BIN"
echo
echo "Next: convert to an HFB and flash (see docs/quickstart.md):"
echo "  python3 tools/qmk_to_hfb.py \"$BIN\" local/HHKB800_FW_A048.hfb local/${KB}_${KM}.hfb"
echo "  python3 tools/flash.py --list"
echo "  python3 tools/flash.py --device /dev/hidrawN local/${KB}_${KM}.hfb"
