// SPDX-License-Identifier: MIT
// Indicator LEDs for the HHKB Hybrid (LED0 = PA8, LED1 = PA9, active high).
// Patterns follow the stock firmware (docs/reverse-engineering/led-indication.md). LED1
// takes priority over LED0 at the pin, exactly like the stock code.
#include "quantum.h"
#include "led_indicator.h"

#define LED0_PIN A8
#define LED1_PIN A9

#define BLINK_PAIR_MS 200 /* 2.5 Hz, 50% */
#define BLINK_BOND_MS 100 /* 5 Hz, 50% */
#define COUNT_ON_MS   200
#define COUNT_OFF_MS  800

static led_mode_t mode;
static uint8_t    count_remaining;
static bool       count_phase_on;
static uint16_t   phase_timer;
static bool       blink_on;

static uint16_t led0_timer_ms; /* led_on_for window */
static uint16_t led0_timer_start;

static uint16_t led1_timer_ms;
static uint16_t led1_timer_start;
static bool     led1_steady;

static void drive(bool led0, bool led1) {
    /* LED1 wins the pin when both would be on, matching the stock behaviour. */
    gpio_write_pin(LED1_PIN, led1);
    gpio_write_pin(LED0_PIN, led0 && !led1);
}

void led_indicator_init(void) {
    gpio_write_pin_low(LED0_PIN);
    gpio_write_pin_low(LED1_PIN);
    gpio_set_pin_output_push_pull(LED0_PIN);
    gpio_set_pin_output_push_pull(LED1_PIN);
}

void led_set_mode(led_mode_t m) {
    if (m == mode) {
        return;
    }
    mode        = m;
    phase_timer = timer_read();
    blink_on    = true;
}

void led_show_count(uint8_t count) {
    mode            = LED_MODE_COUNT;
    count_remaining = count;
    count_phase_on  = true;
    phase_timer     = timer_read();
}

void led_on_for(uint16_t ms) {
    led0_timer_ms    = ms;
    led0_timer_start = timer_read();
}

void led1_on_for(uint16_t ms) {
    led1_timer_ms    = ms;
    led1_timer_start = timer_read();
}

void led1_set(bool on) {
    led1_steady = on;
}

bool led_is_transient(void) {
    return mode == LED_MODE_BLINK_PAIR || mode == LED_MODE_BLINK_BOND ||
           (mode == LED_MODE_COUNT && count_remaining > 0);
}

static bool led0_wanted(void) {
    if (led0_timer_ms && timer_elapsed(led0_timer_start) < led0_timer_ms) {
        return true;
    }
    switch (mode) {
        case LED_MODE_ON:
            return true;
        case LED_MODE_BLINK_PAIR:
        case LED_MODE_BLINK_BOND:
            return blink_on;
        case LED_MODE_COUNT:
            return count_remaining > 0 && count_phase_on;
        default:
            return false;
    }
}

void led_indicator_task(void) {
    if (mode == LED_MODE_BLINK_PAIR || mode == LED_MODE_BLINK_BOND) {
        uint16_t period = (mode == LED_MODE_BLINK_PAIR) ? BLINK_PAIR_MS : BLINK_BOND_MS;
        if (timer_elapsed(phase_timer) >= period) {
            phase_timer = timer_read();
            blink_on    = !blink_on;
        }
    } else if (mode == LED_MODE_COUNT && count_remaining > 0) {
        uint16_t period = count_phase_on ? COUNT_ON_MS : COUNT_OFF_MS;
        if (timer_elapsed(phase_timer) >= period) {
            phase_timer = timer_read();
            if (count_phase_on) {
                count_phase_on = false;
            } else {
                count_phase_on = true;
                if (--count_remaining == 0) {
                    mode = LED_MODE_OFF;
                }
            }
        }
    }

    bool led1 = led1_steady || (led1_timer_ms && timer_elapsed(led1_timer_start) < led1_timer_ms);
    drive(led0_wanted(), led1);
}
