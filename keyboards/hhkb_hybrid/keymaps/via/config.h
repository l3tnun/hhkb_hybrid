// SPDX-License-Identifier: MIT
#pragma once

/* EEPROM window of eeprom_hhkb.c: stock keymap table B, 1536 B. */
#define EEPROM_SIZE 1536

/* 0: Base, 1: Fn, 2..7: free. The Bluetooth key combos (Fn+Ctrl+1..4, Fn+Q, ...)
   check layer 1, so keep Fn there. Changing the count moves the EEPROM layout. */
#define DYNAMIC_KEYMAP_LAYER_COUNT 8

/* 2 bytes of keyboard settings for the VIA "HHKB" menu (layout in hhkb_hybrid.c,
   docs/via.md). Changing the size moves the EEPROM layout. */
#define VIA_EEPROM_CUSTOM_CONFIG_SIZE 2
