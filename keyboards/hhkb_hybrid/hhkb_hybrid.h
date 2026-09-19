// SPDX-License-Identifier: MIT
#pragma once

#include "quantum.h"

// LAYOUT_60_hhkb is generated from keyboard.json

/* Keyboard keycodes, assignable anywhere with VIA (Custom tab). Their values are
   stored in the VIA keymap EEPROM: append only, never reorder. Keep
   tools/gen_via_json.py (CUSTOM_KEYCODES) in the same order. */
enum hhkb_keycodes {
    JIS_TOG = QK_KB_0, /* toggle JIS-on-US correction (like Ctrl+Alt+Shift+J) */
    BT_SLOT1,          /* Bluetooth slot 1..4 (Fn+Ctrl+1..4); in pairing mode, pair on it */
    BT_SLOT2,
    BT_SLOT3,
    BT_SLOT4,
    BT_PAIR,           /* enter pairing mode (Fn+Q) */
    BT_CANCEL,         /* cancel pairing mode (Fn+X) */
    OUT_AUTO,          /* automatic output: USB when a host is present (Fn+Ctrl+0) */
    BT_DEL1,           /* in pairing mode, delete the bond on slot 1..4 (Fn+Q, then Fn+Ctrl+Del+1..4) */
    BT_DEL2,
    BT_DEL3,
    BT_DEL4,
};
