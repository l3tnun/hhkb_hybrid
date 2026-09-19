#!/usr/bin/env python3
"""Generate the VIA (v3) definition for the via keymap from keyboard.json.

The key layout (KLE rows) comes from keyboards/hhkb_hybrid/keyboard.json; the
custom keycodes and the "JIS" settings menu mirror keyboards/hhkb_hybrid/
hhkb_hybrid.h and hhkb_hybrid.c. Load the output in usevia.app (Settings ->
Show Design tab -> Design -> Load draft definition).
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEYBOARD_JSON = ROOT / "keyboards" / "hhkb_hybrid" / "keyboard.json"
DEFAULT_OUTPUT = ROOT / "via" / "hhkb_hybrid.json"

# Array position n is QK_KB_n: keep in sync with enum hhkb_keycodes (append only).
CUSTOM_KEYCODES = [
    {"name": "JIS_TOG", "title": "JIS/US 補正の ON/OFF", "shortName": "JIS"},
    {"name": "BT_SLOT1", "title": "Bluetooth スロット 1 に切替（ペアリング中は登録）", "shortName": "BT1"},
    {"name": "BT_SLOT2", "title": "Bluetooth スロット 2 に切替（ペアリング中は登録）", "shortName": "BT2"},
    {"name": "BT_SLOT3", "title": "Bluetooth スロット 3 に切替（ペアリング中は登録）", "shortName": "BT3"},
    {"name": "BT_SLOT4", "title": "Bluetooth スロット 4 に切替（ペアリング中は登録）", "shortName": "BT4"},
    {"name": "BT_PAIR", "title": "Bluetooth ペアリング開始", "shortName": "Pair"},
    {"name": "BT_CANCEL", "title": "Bluetooth ペアリング取消", "shortName": "PCan"},
    {"name": "OUT_AUTO", "title": "出力を自動（USB 優先）に戻す", "shortName": "USB"},
    {"name": "BT_DEL1", "title": "Bluetooth スロット 1 の登録削除（ペアリングモード中のみ）", "shortName": "BDel1"},
    {"name": "BT_DEL2", "title": "Bluetooth スロット 2 の登録削除（ペアリングモード中のみ）", "shortName": "BDel2"},
    {"name": "BT_DEL3", "title": "Bluetooth スロット 3 の登録削除（ペアリングモード中のみ）", "shortName": "BDel3"},
    {"name": "BT_DEL4", "title": "Bluetooth スロット 4 の登録削除（ペアリングモード中のみ）", "shortName": "BDel4"},
]

# Channel 0 value ids: keep in sync with enum hhkb_via_value_id (hhkb_hybrid.c).
# Auto-sleep values are minutes (the firmware maps them to its stored code).
# Never use 0 as a dropdown value: usevia.app turns an option value of 0 into
# the option's index ("value || idx"), so it would send a different value.
MENUS = [
    {
        "label": "HHKB",
        "content": [
            {
                "label": "JIS/US 補正",
                "content": [
                    {
                        "label": "JIS/US 補正（JIS 配列の OS で US 刻印どおりに入力）。キー操作で切り替えた状態は、ページを再読み込みすると表示に反映されます",
                        "type": "toggle",
                        "content": ["id_hhkb_jis_mode", 0, 1],
                    },
                    {
                        "label": "Ctrl+Alt+Shift+J で補正を切替",
                        "type": "toggle",
                        "content": ["id_hhkb_jis_combo", 0, 2],
                    },
                ],
            },
            {
                "label": "自動スリープ",
                "content": [
                    {
                        "label": "無操作で自動スリープするまでの時間（電池駆動時のみ。DIP SW6 が ON なら無効）",
                        "type": "dropdown",
                        "options": [["1 分", 1], ["5 分", 5], ["10 分", 10], ["30 分（既定）", 30], ["60 分", 60]],
                        "content": ["id_hhkb_autosleep", 0, 3],
                    },
                ],
            },
            {
                "label": "Bluetooth キー操作",
                "content": [
                    {
                        "label": "純正互換のキー操作（Fn+Ctrl+1〜4 / Fn+Ctrl+0 / Fn+Q / Fn+X / Fn+Q の後 Fn+Ctrl+Del+数字）。OFF にすると VIA で割り当てた BT キーだけで操作します",
                        "type": "toggle",
                        "content": ["id_hhkb_stock_combos", 0, 4],
                    },
                ],
            },
            {
                "label": "設定の初期化",
                "content": [
                    {
                        "label": "キーマップ・マクロ・この画面の設定を初期状態に戻す（JIS/US 補正の ON/OFF は保持）。キーボードが再起動するので、ページを再読み込みして再接続してください",
                        "type": "button",
                        "content": ["id_hhkb_reset", 0, 5],
                    },
                ],
            },
        ],
    }
]


def kle_rows(layout: list[dict]) -> list[list]:
    rows: dict[float, list[dict]] = {}
    for key in layout:
        rows.setdefault(key["y"], []).append(key)

    result = []
    for y in sorted(rows):
        row: list = []
        x = 0.0
        for key in sorted(rows[y], key=lambda k: k["x"]):
            props = {}
            if key["x"] != x:
                props["x"] = key["x"] - x
            width = key.get("w", 1)
            if width != 1:
                props["w"] = width
            if props:
                row.append(props)
            row.append(f"{key['matrix'][0]},{key['matrix'][1]}")
            x = key["x"] + width
        result.append(row)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate the VIA definition JSON.")
    parser.add_argument("-o", "--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    kb = json.loads(KEYBOARD_JSON.read_text())
    layout = kb["layouts"]["LAYOUT_60_hhkb"]["layout"]
    rows = max(k["matrix"][0] for k in layout) + 1
    cols = max(k["matrix"][1] for k in layout) + 1

    definition = {
        "name": kb["keyboard_name"],
        "vendorId": kb["usb"]["vid"],
        "productId": kb["usb"]["pid"],
        "matrix": {"rows": rows, "cols": cols},
        "layouts": {"keymap": kle_rows(layout)},
        "menus": MENUS,
        "customKeycodes": CUSTOM_KEYCODES,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    # ASCII only (non-ASCII as \uXXXX): usevia.app does not decode a loaded
    # definition as UTF-8, so raw Japanese labels showed up garbled.
    args.output.write_text(json.dumps(definition, ensure_ascii=True, indent=2) + "\n")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
