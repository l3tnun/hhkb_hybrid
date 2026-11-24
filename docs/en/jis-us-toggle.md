# Typing as shown by the US legends in a JIS environment (JIS/US toggle)

> English translation of [../jis-us-toggle.md](../jis-us-toggle.md). If the two differ, the Japanese version is authoritative.

A correction feature that lets you input symbols **as shown by the US legends** of the HHKB while the layout on the OS side stays set to **JIS (Japanese)**.

## Operation

- **Ctrl + Alt + Shift + J** switches it ON / OFF (it toggles on each press).
- The **`JIS_TOG` key** also switches it. In the VIA-enabled build (`via` keymap) you can assign it to any key you like from the Custom tab of VIA ([via.md](via.md)).
- In the VIA-enabled build, the following settings are available in the HHKB menu of VIA.
  - JIS/US correction ON/OFF. A state switched with a key operation is reflected in the display when you reload the VIA page.
  - Enabling/disabling the switching by Ctrl+Alt+Shift+J (enabled by default). When disabled, Ctrl+Alt+Shift+J is sent as normal key input, and the correction is switched only with `JIS_TOG` and the menu.
- There is no mode indicator LED. Judge by the input result:
  - Correction OFF (normal US): `Shift + 2` → `"`
  - Correction ON (US legends in a JIS environment): `Shift + 2` → `@`
- The switched state is kept (see "Persistence" below).
- With any keymap the conversion is done on the keyboard side (`process_record_kb`), so it works even if you change the keymap.
- **Keycodes with modifiers** such as `S(KC_2)` (VIA's `@` and so on) are also converted. The Shift contained in the keycode is treated the same as a Shift being held down, and Ctrl / Alt / GUI are sent together with the converted key.
- **VIA macros are not converted** (because macro strings are sent directly without going through key processing). To use a macro containing symbols in a JIS environment, specify the keys that produce the intended characters on the JIS layout.

## Build option (enable/disable)

This feature can be enabled or disabled at build time (enabled by default). `HHKB_JIS_US_TOGGLE` in `rules.mk` (default `yes`) adds `-DHHKB_JIS_US_TOGGLE`, and when disabled, the code for the conversion, the toggle and the persistence is left out of the compilation entirely.

```sh
./tools/build.sh                                  # default: JIS/US toggle enabled
./tools/build.sh default -e HHKB_JIS_US_TOGGLE=no  # disabled (same for the via keymap: ./tools/build.sh via -e HHKB_JIS_US_TOGGLE=no)
```

In a build with the feature disabled, `Ctrl+Alt+Shift+J` is treated as a normal `J`, and no symbol conversion is done. The `JIS_TOG` key becomes a key that does nothing, and the JIS/US correction items are shown in the HHKB menu of VIA but cannot be operated.

## How it works

When a US-layout keymap is used with a JIS host, some symbol keys produce different characters. When the correction is ON, each affected key is replaced with "the key + Shift state that produces the character of the US legend on a JIS host" and sent.

Conversion table (US legend → key to send / Shift):

| US legend (pressed) | Sent | Shift |
|---|---|---|
| `Shift+2` → `@` | `[` | No |
| `Shift+6` → `^` | `=` | No |
| `Shift+7` → `&` | `6` | Yes |
| `Shift+8` → `*` | `'` | Yes |
| `Shift+9` → `(` | `8` | Yes |
| `Shift+0` → `)` | `9` | Yes |
| `Shift+-` → `_` | `INT1`(ろ) | Yes |
| `Shift+=` → `+` | `;` | Yes |
| `Shift+;` → `:` | `'` | No |
| `Shift+'` → `"` | `2` | Yes |
| `Shift+[` → `{` | `]` | Yes |
| `Shift+]` → `}` | `\` | Yes |
| `Shift+\` → `\|` | `INT3`(¥) | Yes |
| `` Shift+` `` → `~` | `=` | Yes |
| `=` | `-` | Yes |
| `[` | `]` | No |
| `]` | `\` | No |
| `\` | `INT3`(¥) | No |
| `'` | `7` | Yes |
| `` ` `` | `[` | Yes |

(This mapping is identical to the conversion table of the stock JIS/US toggle patch, whose operation has been verified.)

Not supported: rollover in which several of the converted symbol keys are held down at the same time (single-key input, use together with Shift, and the common input pattern of moving to another key while holding Shift are supported).

## Persistence

The toggle flag is saved in the **built-in data EEPROM of the STM32L0** (the same method as the stock JIS/US toggle).

- It is stored in the **least significant bit of the stock sleep timeout setting byte `0x08080520` (1–60 minutes)**.
  - Correction ON = odd (e.g. 31), OFF = even (e.g. 30).
  - The stock startup check treats 1–60 as valid values, so an odd value is also kept without being reset.
- **Fully non-volatile**: it is kept even if you remove the batteries, and even if you return to the stock firmware.
- The bootloader's boot check flag (`0x08080360` / CRC `0x08080370`) is never touched. Because the write is byte-wise, adjacent bytes are not changed either.

Additional notes:

- The sleep byte becomes odd, but **QMK does not use this byte for the auto-sleep time** (the default build uses the compile-time constant `HHKB_AUTOSLEEP_TIMEOUT_MS`, and the VIA-enabled build uses the setting in the HHKB menu of VIA), so this value does not affect the behavior of QMK.
- While QMK is running, **the toggle of this feature is the only thing that rewrites this byte** (the official Keymap Tool does not work on QMK and is not supported). If you return to the stock firmware and set the sleep time again with the official tool, the least significant bit may change, but that happens while the stock firmware is running, and after flashing QMK afterwards you can set it again with the toggle of this feature.
- This byte does not change when the settings are reset by "設定の初期化" (Reset settings) in the VIA-enabled build or by bootmagic (the correction ON/OFF is kept).

Implementation: `power_save_jis_mode` / `power_load_jis_mode` (`power.c`). The EEPROM write unlocks with `FLASH->PEKEYR` and writes a byte (the same procedure as the update mode handling in `board.c`).

## Where it is implemented

- Conversion and toggle: `keyboards/hhkb_hybrid/hhkb_hybrid.c` (`jis_translate` / `jis_send` / `jis_toggle` / `process_record_kb`)
- `JIS_TOG` keycode: `keyboards/hhkb_hybrid/hhkb_hybrid.h` (`enum hhkb_keycodes`)
- HHKB menu of VIA (correction ON/OFF, enabling/disabling Ctrl+Alt+Shift+J): `hhkb_hybrid.c` (`via_custom_value_command_kb`); the definition JSON comes from `tools/gen_via_json.py`
- Persistence: `keyboards/hhkb_hybrid/power.c` (`SLEEP_EE_ADDR`, EEPROM write). The enabled/disabled state of Ctrl+Alt+Shift+J is in the EEPROM area of the VIA-enabled build ([via.md](via.md))
