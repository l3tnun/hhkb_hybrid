// SPDX-License-Identifier: MIT
#include "quantum.h"
#include "hhkb_hybrid.h"
#include "usb_main.h"
#include "raw_hid.h"
#ifdef VIA_ENABLE
#    include "via.h"
#endif
#include "nrf_spi.h"
#include "led_indicator.h"
#include "power.h"

extern uint16_t actuation_point[MATRIX_COLS][MATRIX_ROWS];

/* Calibration written by the stock firmware (read only).
   Actuation point = u16[CAL_BASE + 2i] + u16[CAL_OFFSET + 2i], i = col * 8 + row
   (stock: loads CAL_BASE, sums the two tables). */
#define EE_CAL_BASE   0x08081100U
#define EE_CAL_OFFSET 0x08081200U
#define CAL_MIN       300
#define CAL_MAX       3500
/* Stock firmware's fixed actuation point */
#define CAL_DEFAULT 0x55F

#define ADC_WAIT_LIMIT 100000U

static void adc_init(void) {
    palSetLineMode(A1, PAL_MODE_INPUT_ANALOG);
    rccEnableADC1(true);

    if (ADC1->CR & ADC_CR_ADEN) {
        ADC1->CR |= ADC_CR_ADDIS;
    }
    for (uint32_t i = 0; i < ADC_WAIT_LIMIT && (ADC1->CR & ADC_CR_ADEN); i++) {
    }
    ADC1->CR &= ~ADC_CR_ADVREGEN;
    ADC1->CR |= ADC_CR_ADVREGEN;
    wait_ms(2);

    /* Same as stock: synchronous clock, PCLK / 1 */
    MODIFY_REG(ADC1->CFGR2, ADC_CFGR2_CKMODE, ADC_CFGR2_CKMODE_0 | ADC_CFGR2_CKMODE_1);

    ADC1->CR |= ADC_CR_ADCAL;
    for (uint32_t i = 0; i < ADC_WAIT_LIMIT && (ADC1->CR & ADC_CR_ADCAL); i++) {
    }

    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    for (uint32_t i = 0; i < ADC_WAIT_LIMIT && !(ADC1->ISR & ADC_ISR_ADRDY); i++) {
    }

    MODIFY_REG(ADC1->CFGR1, ADC_CFGR1_EXTEN, 0);
    ADC1->CFGR1 &= ~ADC_CFGR1_CONT;
    MODIFY_REG(ADC1->SMPR, ADC_SMPR_SMP, 0);
    ADC1->CHSELR = 1UL << 1;
}

static void load_calibration(void) {
    const uint16_t *base   = (const uint16_t *)EE_CAL_BASE;
    const uint16_t *offset = (const uint16_t *)EE_CAL_OFFSET;
    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            uint8_t  i = col * 8 + row;
            uint32_t v = (uint32_t)base[i] + offset[i];
            actuation_point[col][row] = (v >= CAL_MIN && v <= CAL_MAX) ? v : CAL_DEFAULT;
        }
    }
}

void keyboard_pre_init_kb(void) {
    /* Matrix pins (same main board as the HHKB Classic). Other pins, including
       the Bluetooth module lines, are left as the bootloader configured them. */
    gpio_set_pin_output_push_pull(B0);
    gpio_set_pin_output_push_pull(B1);
    gpio_set_pin_output_push_pull(B2);
    gpio_set_pin_output_push_pull(B10);
    gpio_set_pin_output_push_pull(B12);
    gpio_set_pin_output_push_pull(B13);
    gpio_set_pin_output_push_pull(B14);
    gpio_set_pin_output_push_pull(B15);
    gpio_set_pin_output_push_pull(C6);
    gpio_set_pin_output_push_pull(C7);
    gpio_set_pin_output_push_pull(C8);
    gpio_set_pin_output_push_pull(C14);
    palSetPort(GPIOB, 0x407);
    gpio_write_pin_low(C7);
    gpio_write_pin_low(C8);
    gpio_write_pin_high(C14);

    adc_init();
    load_calibration();

    keyboard_pre_init_user();
}

/* Bluetooth output: key reports go to the nRF instead of USB. Raw HID (E0 update
   mode entry, diagnostics) always stays on USB. */
extern host_driver_t chibios_driver;

static uint8_t bt_keyboard_leds(void) {
    return chibios_driver.keyboard_leds();
}
static void bt_send_keyboard(report_keyboard_t *report) {
    nrf_spi_send_keyboard(report);
}
static void bt_send_nkro(report_nkro_t *report) {}
static void bt_send_mouse(report_mouse_t *report) {}
/* Consumer usages in the stock nRF BLE report map (0x31864): reports 2..5, one bit each. */
static const uint16_t bt_consumer_usages[4][8] = {
    {0x06F, 0x070, 0x030, 0x0B7, 0x0E2, 0x0E9, 0x0EA, 0x0B8},
    {0x087, 0x18A, 0x192, 0x088, 0x196, 0x221, 0x223, 0x224},
    {0x225, 0x226, 0x227, 0x22A, 0x0CD, 0x0B5, 0x0B6, 0x153},
    {0x154, 0x155, 0x206, 0x0B2, 0x0B3, 0x0B4, 0x003, 0x152},
};

static void bt_send_extra(report_extra_t *report) {
    if (report->report_id != REPORT_ID_CONSUMER) {
        return; /* system control (power/sleep) has no BLE report */
    }
    uint8_t bits[4] = {0};
    for (uint8_t r = 0; r < 4 && report->usage; r++) {
        for (uint8_t b = 0; b < 8; b++) {
            if (bt_consumer_usages[r][b] == report->usage) {
                bits[r] |= 1 << b;
            }
        }
    }
    nrf_spi_send_consumer(bits);
}

static host_driver_t bt_driver = {
    .keyboard_leds = bt_keyboard_leds,
    .send_keyboard = bt_send_keyboard,
    .send_nkro     = bt_send_nkro,
    .send_mouse    = bt_send_mouse,
    .send_extra    = bt_send_extra,
};

/* Same value as tmk_core/protocol/chibios/chibios.c (not exported by a header). */
#ifndef USB_GETSTATUS_REMOTE_WAKEUP_ENABLED
#    define USB_GETSTATUS_REMOTE_WAKEUP_ENABLED (2U)
#endif

static bool output_bt = false;
#ifdef HHKB_JIS_US_TOGGLE
static bool jis_mode   = false; /* JIS-on-US correction; see process_record_kb */
#endif
static bool prefer_bt = false; /* Fn+Ctrl+1 sets it, Fn+Ctrl+0 clears it (automatic) */
static bool bt_idle_requested = false; /* BT told to disconnect while on USB */

/* Wait until the nRF has read the queued report and put it on the air. Only the
   latest report is kept, so a report queued after this overwrites it. */
static void bt_flush_report(void) {
    for (uint16_t i = 0; i < 40 && !nrf_spi_report_idle(); i++) {
        wait_ms(1); /* nRF poll clears the pending report from its SPI IRQ */
    }
    wait_ms(2); /* let the nRF put the report on the air before it changes */
}

/* Push an all-keys-released report to the BT host we are about to leave and let
   the nRF transmit it, so a modifier held for a stock combo (e.g. the Ctrl of
   Fn+Ctrl+1..4) is not left pressed on the old host. Must run before a select /
   pair / idle / delete request: answer_poll() gives a pending control priority,
   so the release would otherwise be sent after the link changed. */
static void bt_release_before_control(void) {
    if (!output_bt) {
        return;
    }
    clear_keyboard();
    bt_flush_report();
}

static void set_output_bt(bool bt) {
    if (bt == output_bt) {
        return;
    }
    clear_keyboard(); /* release everything on the path being left */
    if (output_bt) {
        bt_flush_report(); /* leaving BT: let the release reach the old host */
    }
    output_bt = bt;
    host_set_driver(bt ? &bt_driver : &chibios_driver);
}

/* Bluetooth host slots (stock key handling, flows in docs/reverse-engineering/bluetooth-protocol.md):
   Fn+Ctrl+1..4 switch to a bonded host; Fn+Q enters pairing mode (Bluetooth goes idle),
   then Fn+Ctrl+1..4 pairs a new host on that slot, replacing its bond; Fn+X or a timeout
   cancels and reconnects the last slot. */
#define PAIRING_TIMEOUT_MS 60000
#define BT_RECONNECT_TIMEOUT_MS 60000 /* stock state 4: 60 s then OFF or state 5 */

static bool     pairing_mode = false;
static bool     bonding_wait = false; /* Fn+Ctrl+n after Fn+Q: open advertising, pairing_mode already false */
static uint32_t pairing_since;
static uint8_t  last_slot = 0;

/* BT link vs the stock STM32 states 3/4/5 (docs/reverse-engineering/power-management.md).
   No auto-pair on empty slots (stock sends 01 00; we do not). */
typedef enum {
    BT_PHASE_NONE = 0, /* USB output, or nRF not yet used */
    BT_PHASE_TRY,      /* reconnecting / slot select in flight: LED on, 60 s */
    BT_PHASE_UP,       /* nRF reports connected (83 xx3) */
    BT_PHASE_GIVEUP,   /* stock state 5: nRF idle, LED off, key or short-press retries */
} bt_phase_t;

static bt_phase_t bt_phase            = BT_PHASE_NONE;
static uint32_t   bt_try_since        = 0;
static bool       bt_try_select_sent  = false;

static void bt_begin_try(void) {
    bt_phase           = BT_PHASE_TRY;
    bt_try_since       = timer_read32();
    bt_try_select_sent = false;
}

static void select_bt_slot(uint8_t slot) {
    last_slot = slot;
    power_save_last_slot(slot);
    prefer_bt = true;
    set_output_bt(true);
    if (nrf_spi_connected_slot() != slot) {
        bt_release_before_control();
        nrf_spi_select_slot(slot);
        bt_begin_try();
        bt_try_select_sent = true;
    } else {
        bt_phase = BT_PHASE_UP;
    }
    led_show_count(slot + 1); /* blink the slot number as switch feedback */
}

static void pairing_start(void) {
    pairing_mode  = true;
    bonding_wait  = false;
    pairing_since = timer_read32();
    prefer_bt     = true;
    set_output_bt(true); /* passkey digits typed on the keyboard go to the nRF */
    bt_release_before_control();
    nrf_spi_bt_idle();
    led_set_mode(LED_MODE_BLINK_PAIR);
}

static void pairing_choose_slot(uint8_t slot) {
    pairing_mode = false;
    bonding_wait = true;
    last_slot    = slot;
    bt_release_before_control();
    nrf_spi_pair_slot(slot);
    led_set_mode(LED_MODE_BLINK_BOND);
}

static void pairing_cancel(void) {
    pairing_mode = false;
    bonding_wait = false;
    bt_release_before_control();
    nrf_spi_select_slot(last_slot);
    bt_begin_try();
    bt_try_select_sent = true;
}

static void bt_on_user_wake(void) {
    if (pairing_mode || bonding_wait || !output_bt) {
        return;
    }
    if (bt_phase == BT_PHASE_GIVEUP && nrf_spi_connected_slot() < 0) {
        bt_begin_try();
    }
}

static void power_short_press(void) {
    if (pairing_mode || bonding_wait) {
        return;
    }
    if (bt_phase == BT_PHASE_GIVEUP && output_bt) {
        bt_begin_try();
        return;
    }
    int8_t slot = nrf_spi_connected_slot();
    if (slot >= 0) {
        led_show_count((uint8_t)slot + 1); /* stock: blink the connected slot */
        return;
    }
    if (bt_phase == BT_PHASE_TRY) {
        bt_try_select_sent = false; /* send select again */
        bt_try_since       = timer_read32();
    }
}

/* Stock Fn+Q, then Fn+Ctrl+Del+n: delete the bond on slot n. Afterwards
   reconnect the last slot unless it was the deleted one. */
static bool    reconnect_after_delete = false;
static uint8_t deleted_slot;

static void pairing_delete_slot(uint8_t slot) {
    pairing_mode           = false;
    deleted_slot           = slot;
    reconnect_after_delete = true;
    bt_release_before_control();
    nrf_spi_delete_slot(slot);
}

#ifdef VIA_ENABLE
static void apply_autosleep(void);
#endif

void keyboard_post_init_kb(void) {
    bt_driver.send_raw_hid = chibios_driver.send_raw_hid;
    led_indicator_init();
    power_init();
#ifdef VIA_ENABLE
    apply_autosleep(); /* VIA setting; runs after via_init() and eeconfig init */
#endif
    int8_t saved = power_load_last_slot();
    if (saved >= 0) {
        last_slot = (uint8_t)saved;
    }
#ifdef HHKB_JIS_US_TOGGLE
    int8_t jis = power_load_jis_mode();
    if (jis >= 0) {
        jis_mode = (bool)jis;
    }
#endif
    nrf_spi_init();
    power_set_short_press_cb(power_short_press);
    keyboard_post_init_user();
}

/* Stock key combinations (docs/reverse-engineering/bluetooth-protocol.md §6), matched by key position so
   the Fn layer keeps its keycodes. Matrix positions from keyboard.json. */
#define FN_LAYER 1
static const keypos_t slot_keys[4] = {{.row = 1, .col = 3}, {.row = 1, .col = 1}, {.row = 1, .col = 2}, {.row = 1, .col = 4}}; /* 1..4 */
#define KEY_0_ROW 1
#define KEY_0_COL 9
#define KEY_Q_ROW 2
#define KEY_Q_COL 3
#define KEY_X_ROW 0
#define KEY_X_COL 2
#define KEY_DEL_ROW 2
#define KEY_DEL_COL 13

/* JIS-on-US correction: when a US-layout keymap is used on a host set to the
   Japanese (JIS) layout, several symbol keys produce the wrong character. When
   this mode is on we translate the affected keys to the key (and shift state)
   that yields the US legend on a JIS host. Toggle with the JIS_TOG keycode or
   Ctrl+Alt+Shift+J (the combo can be disabled in the VIA "JIS" menu); the
   flag is persisted (mapping mirrors the stock JIS/US toggle, see
   docs/reverse-engineering). Multi-key rollover of translated symbols is out of scope.
   Compiled in only when HHKB_JIS_US_TOGGLE is defined (rules.mk, default yes). */
#ifdef HHKB_JIS_US_TOGGLE

/* Returns true and fills dst/dst_shift if `keycode` is remapped in JIS mode,
   given whether Shift is currently held. */
static bool jis_translate(uint16_t keycode, bool shift, uint16_t *dst, bool *dst_shift) {
    if (shift) {
        switch (keycode) {
            case KC_2:    *dst = KC_LBRC; *dst_shift = false; return true; /* @ */
            case KC_6:    *dst = KC_EQL;  *dst_shift = false; return true; /* ^ */
            case KC_7:    *dst = KC_6;    *dst_shift = true;  return true; /* & */
            case KC_8:    *dst = KC_QUOT; *dst_shift = true;  return true; /* * */
            case KC_9:    *dst = KC_8;    *dst_shift = true;  return true; /* ( */
            case KC_0:    *dst = KC_9;    *dst_shift = true;  return true; /* ) */
            case KC_MINS: *dst = KC_INT1; *dst_shift = true;  return true; /* _ */
            case KC_EQL:  *dst = KC_SCLN; *dst_shift = true;  return true; /* + */
            case KC_SCLN: *dst = KC_QUOT; *dst_shift = false; return true; /* : */
            case KC_QUOT: *dst = KC_2;    *dst_shift = true;  return true; /* " */
            case KC_LBRC: *dst = KC_RBRC; *dst_shift = true;  return true; /* { */
            case KC_RBRC: *dst = KC_BSLS; *dst_shift = true;  return true; /* } */
            case KC_BSLS: *dst = KC_INT3; *dst_shift = true;  return true; /* | */
            case KC_GRV:  *dst = KC_EQL;  *dst_shift = true;  return true; /* ~ */
        }
    } else {
        switch (keycode) {
            case KC_EQL:  *dst = KC_MINS; *dst_shift = true;  return true; /* = */
            case KC_LBRC: *dst = KC_RBRC; *dst_shift = false; return true; /* [ */
            case KC_RBRC: *dst = KC_BSLS; *dst_shift = false; return true; /* ] */
            case KC_BSLS: *dst = KC_INT3; *dst_shift = false; return true; /* \ */
            case KC_QUOT: *dst = KC_7;    *dst_shift = true;  return true; /* ' */
            case KC_GRV:  *dst = KC_LBRC; *dst_shift = true;  return true; /* ` */
        }
    }
    return false;
}

/* Over BT only the latest keyboard report is kept (nrf_spi), so let each report
   be read by the nRF before sending the next, or a too-fast tap loses the key. */
static void jis_settle(void) {
    if (!output_bt) {
        return;
    }
    for (uint16_t i = 0; i < 40 && !nrf_spi_report_idle(); i++) {
        wait_ms(1); /* nRF poll clears the pending report from its SPI IRQ */
    }
    wait_ms(2); /* let the nRF put the report on the air before it changes */
}

/* Emit the translated key with the required shift state in a single "down"
   report (modifier byte and key together, like the stock JIS/US patch), then
   release. Overriding shift with separate reports dropped keys over BT.
   `extra` adds the non-Shift modifiers of a modded keycode (e.g. Ctrl of C(S(KC_2))). */
static void jis_send(uint16_t dst, bool dst_shift, uint8_t extra) {
    uint8_t real = get_mods();
    uint8_t tmp  = (real & ~MOD_MASK_SHIFT) | extra;
    if (dst_shift) {
        tmp |= MOD_BIT(KC_LSFT);
    }
    set_mods(tmp);
    register_code(dst);   /* one report: corrected mods + dst */
    jis_settle();
    unregister_code(dst); /* release */
    jis_settle();
    set_mods(real);       /* restore the physical modifier state */
    send_keyboard_report();
}

static void jis_toggle(void) {
    jis_mode = !jis_mode;
    power_save_jis_mode(jis_mode);
}
#endif /* HHKB_JIS_US_TOGGLE */

#ifdef VIA_ENABLE
/* VIA custom config (VIA_EEPROM_CUSTOM_CONFIG_SIZE 2, keymaps/via/config.h):
     byte 0: high nibble = VIA EEPROM layout version,
             bit 0 = Ctrl+Alt+Shift+J disabled, bit 1 = stock BT key combos disabled
     byte 1: auto-sleep code (index into autosleep_minutes, 0 = 30 min)
   Every default is 0, so a formatted EEPROM reads as the defaults plus a
   version mismatch (VIA reset). */
#define HHKB_VIA_LAYOUT_VERSION 2 /* bump when the VIA EEPROM layout changes (layer count, config size, ...) */
#define VIA_CFG_FLAGS           0
#define VIA_CFG_AUTOSLEEP       1
#define VIA_CFG_COMBO_OFF       0x01
#define VIA_CFG_STOCK_KEYS_OFF  0x02

/* Auto-sleep code -> minutes. The EEPROM keeps the code (0 = 30 min, so an erased
   byte is the default); VIA gets and sets minutes, because its dropdown treats an
   option value of 0 as missing and sends the option index instead. */
static const uint8_t autosleep_minutes[] = {30, 1, 5, 10, 60};

static uint8_t via_cfg_read(uint8_t index) {
    uint8_t v = 0;
    via_read_custom_config(&v, index, 1);
    return v;
}

static void via_cfg_write(uint8_t index, uint8_t v) {
    via_update_custom_config(&v, index, 1);
}

static void via_cfg_set_flag(uint8_t flag, bool set) {
    uint8_t flags = via_cfg_read(VIA_CFG_FLAGS);
    via_cfg_write(VIA_CFG_FLAGS, set ? (flags | flag) : (flags & ~flag));
}

static void via_cfg_defaults(void) {
    via_cfg_write(VIA_CFG_FLAGS, HHKB_VIA_LAYOUT_VERSION << 4);
    via_cfg_write(VIA_CFG_AUTOSLEEP, 0);
}

static void apply_autosleep(void) {
    uint8_t code = via_cfg_read(VIA_CFG_AUTOSLEEP);
    if (code >= ARRAY_SIZE(autosleep_minutes)) {
        code = 0;
    }
    power_set_autosleep_timeout((uint32_t)autosleep_minutes[code] * 60000U);
}

/* QMK's VIA magic is derived only from the build date, which SKIP_VERSION fixes,
   so a layout change is detected with our own version instead. */
void via_init_kb(void) {
    if ((via_cfg_read(VIA_CFG_FLAGS) >> 4) != HHKB_VIA_LAYOUT_VERSION) {
        via_cfg_defaults();
        eeconfig_init_via(); /* keymaps and macros back to the firmware defaults */
    }
}

/* EEPROM reset (VIA "reset settings" button, EE_CLR key): the keyboard settings
   go back to their defaults too. The JIS-on-US mode itself lives in the stock
   sleep byte and is kept. */
void eeconfig_init_kb(void) {
    via_cfg_defaults();
#    if (EECONFIG_KB_DATA_SIZE) == 0
    eeconfig_update_kb(0); /* as QMK's default eeconfig_init_kb() */
#    endif
    eeconfig_init_user();
}

/* VIA "HHKB" menu (tools/gen_via_json.py MENUS), channel 0. */
enum hhkb_via_value_id {
    id_hhkb_jis_mode     = 1,
    id_hhkb_jis_combo    = 2,
    id_hhkb_autosleep    = 3,
    id_hhkb_stock_combos = 4,
    id_hhkb_reset        = 5, /* button */
};

/* "Reset settings" button: reset the EEPROM like EE_CLR, but only after the VIA
   reply has gone out (housekeeping_task_kb), since the keyboard restarts. */
#define VIA_RESET_DELAY_MS 300
static bool     via_reset_pending = false;
static uint32_t via_reset_since;

/* Both return false for a value id this keyboard does not handle. */
static bool hhkb_via_get(uint8_t id, uint8_t *value) {
    switch (id) {
        case id_hhkb_reset:
            *value = 0;
            return true;
#    ifdef HHKB_JIS_US_TOGGLE
        case id_hhkb_jis_mode:
            *value = jis_mode;
            return true;
        case id_hhkb_jis_combo:
            *value = !(via_cfg_read(VIA_CFG_FLAGS) & VIA_CFG_COMBO_OFF);
            return true;
#    endif
        case id_hhkb_autosleep: {
            uint8_t code = via_cfg_read(VIA_CFG_AUTOSLEEP);
            *value       = autosleep_minutes[code < ARRAY_SIZE(autosleep_minutes) ? code : 0];
            return true;
        }
        case id_hhkb_stock_combos:
            *value = !(via_cfg_read(VIA_CFG_FLAGS) & VIA_CFG_STOCK_KEYS_OFF);
            return true;
    }
    return false;
}

static bool hhkb_via_set(uint8_t id, uint8_t value) {
    switch (id) {
#    ifdef HHKB_JIS_US_TOGGLE
        case id_hhkb_jis_mode:
            if ((bool)value != jis_mode) {
                jis_toggle();
            }
            return true;
        case id_hhkb_jis_combo:
            via_cfg_set_flag(VIA_CFG_COMBO_OFF, !value);
            return true;
#    endif
        case id_hhkb_autosleep:
            for (uint8_t code = 0; code < ARRAY_SIZE(autosleep_minutes); code++) {
                if (autosleep_minutes[code] == value) {
                    via_cfg_write(VIA_CFG_AUTOSLEEP, code);
                    apply_autosleep();
                    break;
                }
            }
            return true; /* minutes not in the list are ignored */
        case id_hhkb_stock_combos:
            via_cfg_set_flag(VIA_CFG_STOCK_KEYS_OFF, !value);
            return true;
        case id_hhkb_reset:
            via_reset_pending = true;
            via_reset_since   = timer_read32();
            return true;
    }
    return false;
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    /* data = [ command_id, channel_id, value_id, value_data ] */
    uint8_t *command_id = &data[0];
    (void)length;
    if (data[1] == id_custom_channel) {
        switch (*command_id) {
            case id_custom_set_value:
                if (hhkb_via_set(data[2], data[3])) {
                    return;
                }
                break;
            case id_custom_get_value:
                if (hhkb_via_get(data[2], &data[3])) {
                    return;
                }
                break;
            case id_custom_save:
                return; /* values are written when set */
        }
    }
    *command_id = id_unhandled;
}
#endif /* VIA_ENABLE */

#ifdef HHKB_JIS_US_TOGGLE
static bool jis_combo_enabled(void) {
#    ifdef VIA_ENABLE
    return !(via_cfg_read(VIA_CFG_FLAGS) & VIA_CFG_COMBO_OFF);
#    else
    return true;
#    endif
}
#endif

/* Stock Bluetooth key combos (Fn+Ctrl+1..4, Fn+Ctrl+0, Fn+Q, Fn+X), unless disabled in VIA. */
static bool stock_keys_enabled(void) {
#ifdef VIA_ENABLE
    return !(via_cfg_read(VIA_CFG_FLAGS) & VIA_CFG_STOCK_KEYS_OFF);
#else
    return true;
#endif
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        power_note_activity();
        bt_on_user_wake();
    }
    /* NO_USB_STARTUP_CHECK disables QMK's suspend loop, so wake a sleeping USB host here. */
    if (record->event.pressed && !output_bt && USB_DRIVER.state == USB_SUSPENDED && (USB_DRIVER.status & USB_GETSTATUS_REMOTE_WAKEUP_ENABLED)) {
        usbWakeupHost(&USB_DRIVER);
    }

#ifdef HHKB_JIS_US_TOGGLE
    /* JIS_TOG, or Ctrl+Alt+Shift+J unless disabled in VIA, toggles JIS-on-US
       correction (persisted). */
    if (keycode == JIS_TOG) {
        if (record->event.pressed) {
            jis_toggle();
        }
        return false;
    }
    if (keycode == KC_J && jis_combo_enabled()) {
        uint8_t m = get_mods();
        if ((m & MOD_MASK_CTRL) && (m & MOD_MASK_ALT) && (m & MOD_MASK_SHIFT)) {
            if (record->event.pressed) {
                jis_toggle();
            }
            return false;
        }
    }

    /* JIS-on-US symbol translation. A modded keycode such as S(KC_2) counts its
       own Shift, and its other modifiers are sent with the translated key. */
    if (jis_mode) {
        uint16_t basic = keycode;
        uint8_t  extra = 0;
        bool     shift = get_mods() & MOD_MASK_SHIFT;
        if (IS_QK_MODS(keycode)) {
            uint8_t m5   = QK_MODS_GET_MODS(keycode);
            uint8_t mods = (m5 & 0x10) ? (uint8_t)((m5 & 0x0F) << 4) : (m5 & 0x0F); /* 5-bit -> 8-bit */
            basic        = QK_MODS_GET_BASIC_KEYCODE(keycode);
            shift        = shift || (mods & MOD_MASK_SHIFT);
            extra        = mods & ~MOD_MASK_SHIFT;
        }
        uint16_t dst;
        bool     dst_shift;
        if (jis_translate(basic, shift, &dst, &dst_shift)) {
            if (record->event.pressed) {
                jis_send(dst, dst_shift, extra);
            }
            return false; /* suppress the untranslated key (press and release) */
        }
    }
#else
    if (keycode == JIS_TOG) {
        return false; /* correction not built in */
    }
#endif /* HHKB_JIS_US_TOGGLE */

    /* Bluetooth/output keycodes: same actions as the stock key combos below. */
    switch (keycode) {
        case BT_SLOT1 ... BT_SLOT4:
            if (record->event.pressed) {
                uint8_t slot = keycode - BT_SLOT1;
                if (pairing_mode) {
                    pairing_choose_slot(slot);
                } else {
                    select_bt_slot(slot);
                }
            }
            return false;
        case BT_PAIR:
            if (record->event.pressed) {
                pairing_start();
            }
            return false;
        case BT_CANCEL:
            if (record->event.pressed && pairing_mode) {
                pairing_cancel();
            }
            return false;
        case OUT_AUTO:
            if (record->event.pressed) {
                prefer_bt = false; /* automatic: USB when a host is present */
                led_on_for(1000);  /* blue for 1 s: switched to USB */
            }
            return false;
        case BT_DEL1 ... BT_DEL4:
            if (record->event.pressed && pairing_mode) { /* like the stock flow: after Fn+Q / BT_PAIR */
                pairing_delete_slot(keycode - BT_DEL1);
            }
            return false;
    }

    if (record->event.pressed && IS_LAYER_ON(FN_LAYER) && stock_keys_enabled()) {
        keypos_t key  = record->event.key;
        bool     ctrl = get_mods() & MOD_MASK_CTRL;
        for (uint8_t slot = 0; ctrl && slot < 4; slot++) {
            if (key.row == slot_keys[slot].row && key.col == slot_keys[slot].col) {
                if (pairing_mode && matrix_is_on(KEY_DEL_ROW, KEY_DEL_COL)) {
                    pairing_delete_slot(slot);
                } else if (pairing_mode) {
                    pairing_choose_slot(slot);
                } else {
                    select_bt_slot(slot);
                }
                return false;
            }
        }
        if (ctrl && key.row == KEY_0_ROW && key.col == KEY_0_COL) {
            prefer_bt = false;         /* automatic: USB when a host is present */
            led_on_for(1000);          /* blue for 1 s: switched to USB */
            return false;
        }
        if (!ctrl && key.row == KEY_Q_ROW && key.col == KEY_Q_COL) {
            pairing_start();
            return false;
        }
        if (!ctrl && pairing_mode && key.row == KEY_X_ROW && key.col == KEY_X_COL) {
            pairing_cancel();
            return false;
        }
    }
    return process_record_user(keycode, record);
}

static void watchdog_feed(void) {
    IWDG->KR = 0xAAAA;
}

/* Battery level inputs (stock): PB9 low -> 0 %,
   PB8 low -> 5 %, PB7 low -> 15 %, all high -> 100 %. Reported as 100 % on USB power.
   Sent to the nRF when the link comes up and when it changes. */
#define BATTERY_SAMPLE_MS    1000
#define BATTERY_STABLE_COUNT 3

static uint8_t battery_level_now(bool vbus) {
    if (vbus) {
        return 100;
    }
    if (!gpio_read_pin(B9)) {
        return 0;
    }
    if (!gpio_read_pin(B8)) {
        return 5;
    }
    if (!gpio_read_pin(B7)) {
        return 15;
    }
    return 100;
}

static void battery_task(bool vbus) {
    static uint16_t last_sample;
    static uint8_t  candidate = 0xFF, stable_count, reported = 0xFF;
    static bool     was_connected;

    bool connected = nrf_spi_connected_slot() >= 0;
    if (!connected) {
        was_connected = false;
        return;
    }
    if (timer_elapsed(last_sample) >= BATTERY_SAMPLE_MS || !was_connected) {
        last_sample   = timer_read();
        uint8_t level = battery_level_now(vbus);
        stable_count  = (level == candidate) ? stable_count + 1 : 1;
        candidate     = level;
        bool stable   = stable_count >= BATTERY_STABLE_COUNT;
        if (!was_connected || (stable && level != reported)) {
            reported = level;
            nrf_spi_send_battery(level);
        }
    }
    was_connected = true;
}

/* PC13 is low while USB power (VBUS) is present (measured: 0 on USB power).
   The PFU bootloader enables HSI48 for USB when PC13 is low, and
   the stock firmware sets its power state from it.
   Left as configured by the bootloader. */
#define USB_VBUS_PIN C13

/* LED0: steady only while actively reconnecting (stock state 4), not after giving up. */
static void led_indicator_update(void) {
    if (pairing_mode || bonding_wait) {
        return; /* keep the pairing / new-bond blink */
    }
    if (led_is_transient() && nrf_spi_connected_slot() < 0) {
        return; /* keep the slot-count blink until it finishes or we connect */
    }
    if (bt_phase == BT_PHASE_TRY) {
        led_set_mode(LED_MODE_ON);
    } else if (nrf_spi_control_busy() && bt_phase != BT_PHASE_GIVEUP && bt_phase != BT_PHASE_NONE) {
        led_set_mode(LED_MODE_ON); /* pairing / slot request still in flight */
    } else {
        led_set_mode(LED_MODE_OFF);
    }
}

void housekeeping_task_kb(void) {
#ifdef VIA_ENABLE
    if (via_reset_pending && timer_elapsed32(via_reset_since) >= VIA_RESET_DELAY_MS) {
        eeconfig_disable();    /* same as EE_CLR: full eeconfig/VIA init on the next boot */
        soft_reset_keyboard(); /* does not return */
    }
#endif
    static bool     vbus_present   = false;
    static bool     host_seen      = false; /* host started enumeration since USB power appeared */
    static bool     usb_configured = false; /* host configured us since USB power appeared */
    static uint32_t host_seen_since = 0;

    bool vbus = !gpio_read_pin(USB_VBUS_PIN);
    if (vbus && !vbus_present) {
        host_seen      = false;
        usb_configured = false;
    }
    vbus_present = vbus;

    usbstate_t usb_state = USB_DRIVER.state;
    if (usb_state == USB_SELECTED || usb_state == USB_ACTIVE) {
        if (!host_seen) {
            host_seen_since = timer_read32();
        }
        host_seen = true;
    }
    if (usb_state == USB_ACTIVE) {
        usb_configured = true;
    }
    /* A host is present when USB is powered and configured (a suspended host counts). */
    bool host_present = vbus && usb_configured && (usb_state == USB_ACTIVE || usb_state == USB_SUSPENDED);

    nrf_spi_task();
    battery_task(vbus);
    if (pairing_mode && timer_elapsed32(pairing_since) > PAIRING_TIMEOUT_MS) {
        pairing_cancel();
    }
    if (reconnect_after_delete && !nrf_spi_control_busy()) {
        reconnect_after_delete = false;
        if (last_slot != deleted_slot) {
            bt_release_before_control();
            nrf_spi_select_slot(last_slot);
            bt_begin_try();
            bt_try_select_sent = true;
        }
    }
    if (nrf_spi_connected_slot() >= 0 && last_slot != nrf_spi_connected_slot()) {
        last_slot = nrf_spi_connected_slot();
        power_save_last_slot(last_slot);
    }
    /* Output: Bluetooth when chosen with Fn+Ctrl+1, or when there is no USB host
       (battery, charger only) once the nRF is up; USB otherwise. */
    if (prefer_bt || (!host_present && nrf_spi_ready())) {
        bool was_usb = !output_bt;
        set_output_bt(true);
        bt_idle_requested = false;
        /* Going to BT automatically (battery): reconnect the last-used slot, not
           the nRF's default lowest bond. */
        if (was_usb && !prefer_bt && nrf_spi_connected_slot() != last_slot && !nrf_spi_control_busy()) {
            bt_release_before_control();
            nrf_spi_select_slot(last_slot);
            bt_begin_try();
            bt_try_select_sent = true;
        }
    } else if (host_present) {
        set_output_bt(false);
        /* One target at a time: drop any BT link while output goes to USB. */
        if (!bt_idle_requested && nrf_spi_connected_slot() >= 0 && !nrf_spi_control_busy()) {
            nrf_spi_bt_idle();
            bt_idle_requested = true;
        }
    }

    /* Reconnect window (stock state 4, 60 s). Do not auto-pair empty slots. */
    if (pairing_mode || bonding_wait) {
        /* pairing / open advertising own the link */
    } else if (!output_bt) {
        bt_phase           = BT_PHASE_NONE;
        bt_try_select_sent = false;
    } else if (nrf_spi_connected_slot() >= 0) {
        bt_phase           = BT_PHASE_UP;
        bt_try_select_sent = false;
        bonding_wait       = false;
    } else if (bt_phase == BT_PHASE_UP || bt_phase == BT_PHASE_NONE) {
        bt_begin_try();
    }
    if (!pairing_mode && !bonding_wait && output_bt && bt_phase == BT_PHASE_TRY) {
        if (!bt_try_select_sent && !nrf_spi_control_busy()) {
            bt_release_before_control();
            nrf_spi_select_slot(last_slot);
            bt_try_select_sent = true;
        }
        if (timer_elapsed32(bt_try_since) > BT_RECONNECT_TIMEOUT_MS) {
            bt_phase = BT_PHASE_GIVEUP; /* before idle: 83 01 must not restart TRY */
            bt_release_before_control(); /* BT output is still on here */
            nrf_spi_bt_idle();          /* overwrites a stuck select if any */
            if (!vbus && !power_sw6_on()) {
                power_enter_off(); /* does not return */
            }
        }
    }

    led_indicator_update();
    led_indicator_task();
    power_task(vbus);

    /* Fail-safe: on USB power, stop feeding the watchdog only if a host started
       enumeration but did not configure us in time (a charger never enumerates). */
    if (!vbus || usb_configured || !host_seen || timer_elapsed32(host_seen_since) < HHKB_USB_FAILSAFE_TIMEOUT_MS) {
        watchdog_feed();
    }
    housekeeping_task_user();
    power_idle(); /* battery: sleep until the next scan */
}

void suspend_power_down_kb(void) {
    watchdog_feed();
    suspend_power_down_user();
}

/* Stock firmware update protocol, E0 only (stock handler):
   host sends "AA AA E0 00 00", the keyboard answers "55 55 E0 00 00" and enters
   the PFU bootloader update mode; E1..E3 are then handled by the bootloader.
   Any other command gets the stock error answer "55 55 <cmd> 01 00".
   QMK's Raw HID reports are 32 bytes (the stock interface uses 64). */
#define FWUP_REQ_MAGIC  0xAA
#define FWUP_RESP_MAGIC 0x55
#define FWUP_MODE_CHANGE 0xE0

/* Before resetting into the PFU update mode, drop the D+ pull-up and wait so
   the host sees a real disconnect. Without it, the bootloader's enumeration
   right after the reset sometimes failed on the host (usbhid -71) and stayed
   stuck, whereas a physical replug recovers by itself (docs/flashing.md). */
#define UPDATE_MODE_DETACH_MS 500

static void enter_update_mode_detached(void) {
    if (USB_DRIVER.state != USB_STOP) {
        usbDisconnectBus(&USB_DRIVER);
        usbStop(&USB_DRIVER);
        wait_ms(UPDATE_MODE_DETACH_MS);
    }
    hhkb_enter_update_mode();
}

#ifdef VIA_ENABLE
/* The VIA EEPROM window is the stock keymap table B (eeprom_hhkb.c). On a revert
   the PFU bootloader repairs only the stock keymap magic and table A, so the
   stock firmware would read the leftover VIA data from table B as its keymap.
   Zero the window (stock state) and invalidate the magic before reverting. */
#    include "eeprom_driver.h"

static void clear_stock_keymap_window(void) {
    eeprom_driver_erase();
}

/* Bootmagic (Esc held while connecting): reset the settings and also clear the
   window, since this path may be used to go back to the stock firmware. */
void bootmagic_reset_eeprom(void) {
    eeconfig_disable();
    clear_stock_keymap_window();
}
#endif

/* Returns true if `data` was a stock update command (answered here).
   AA AA E0 00 00: enter the update mode (settings kept, for QMK updates).
   AA AA F0 00 00: prepare a revert to the stock firmware (VIA build: clear the
                   stock keymap window first), then enter the update mode. */
#define FWUP_STOCK_REVERT 0xF0

static bool fwup_command(uint8_t *data, uint8_t length) {
    if (length < 5 || data[0] != FWUP_REQ_MAGIC || data[1] != FWUP_REQ_MAGIC) {
        return false;
    }
    uint8_t response[RAW_EPSIZE] = {FWUP_RESP_MAGIC, FWUP_RESP_MAGIC, data[2], 0x01, 0x00};
    bool    enter      = data[2] == FWUP_MODE_CHANGE && data[3] == 0 && data[4] == 0;
    bool    revert     = false;
#ifdef VIA_ENABLE
    revert = data[2] == FWUP_STOCK_REVERT && data[3] == 0 && data[4] == 0;
#endif
    if (enter || revert) {
        response[3] = 0x00;
        raw_hid_send(response, sizeof(response));
        wait_ms(200); /* let the answer reach the host before detaching */
#ifdef VIA_ENABLE
        if (revert) {
            clear_stock_keymap_window();
        }
#endif
        enter_update_mode_detached();
    }
    raw_hid_send(response, sizeof(response));
    return true;
}

#ifdef VIA_ENABLE
/* VIA owns raw_hid_receive(). 0xAA is not a VIA command id, so the stock update
   commands are taken here before VIA's dispatcher. */
bool via_command_kb(uint8_t *data, uint8_t length) {
    return fwup_command(data, length);
}
#else
void raw_hid_receive(uint8_t *data, uint8_t length) {
    fwup_command(data, length);
}
#endif

/* QK_BOOT / bootmagic: enter the PFU bootloader HID update mode, like the stock
   E0 command; then write an HFB with tools/flash.py --resume. QK_BOOT keeps the
   stock keymap window (VIA settings), so do not use it to go back to the stock
   firmware: use tools/flash.py or bootmagic instead (docs/restore-stock.md). */
void bootloader_jump(void) {
    enter_update_mode_detached(); /* QK_BOOT and bootmagic (USB is already started by then) */
}

void mcu_reset(void) {
    NVIC_SystemReset();
}
