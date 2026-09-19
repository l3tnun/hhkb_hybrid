// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Two indicator LEDs: LED0 = PA8, LED1 = PA9, active high.
   Stock lights only one at a time, LED1 taking priority; we do the same. */
void led_indicator_init(void);
void led_indicator_task(void);

/* What LED0 shows (LED1 is driven separately for low battery / power off). */
typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_ON,             /* steady: BT reconnecting / USB active window */
    LED_MODE_BLINK_PAIR,     /* 2.5 Hz: pairing mode (Fn+Q) */
    LED_MODE_BLINK_BOND,     /* 5 Hz: waiting for a new bond (Fn+Ctrl+n) */
    LED_MODE_COUNT,          /* blink N times (slot number), then off */
} led_mode_t;

void led_set_mode(led_mode_t mode);
/* Show a count of blinks once (short press / slot switch: slot number + 1). */
void led_show_count(uint8_t count);
/* True while a count or pairing/bond blink is playing (status updates must not override). */
bool led_is_transient(void);
/* Timed steady LED0 (USB active window etc.), 0 = clear. */
void led_on_for(uint16_t ms);

/* LED1 overrides LED0 while on. */
void led1_on_for(uint16_t ms);   /* power-off / sleep indication */
void led1_set(bool on);          /* low battery steady */

