// SPDX-License-Identifier: MIT
#pragma once

/* key matrix (Topre capacitive, custom ADC scan in matrix.c) */
#define MATRIX_ROWS 4
#define MATRIX_COLS 15

/* Debouncing for the capacitive scan */
#define DEBOUNCE 5

/* wait_us() via GPT TIM3 (required by matrix scan) */
#define WAIT_US_TIMER GPTD3

/* Fail-safe: on USB power (PC13 low), stop feeding the watchdog if USB is not
   configured within this time after USB power appears, so the board falls back
   to the PFU update mode. On battery the watchdog is fed while the main loop runs. */
#define HHKB_USB_FAILSAFE_TIMEOUT_MS 20000

/* Inactivity auto-sleep on battery (power.c). Disabled by DIP SW6 (PC4 low).
   30 min. */
#define HHKB_AUTOSLEEP_TIMEOUT_MS 1800000

/* Battery low-power run (HHKB_LOW_POWER, power.c): one matrix scan per this
   period, sleeping in between (the stock firmware scans every ~11 ms on
   battery). */
#define HHKB_LP_SCAN_INTERVAL_MS 5

/* Keep the main loop running while USB is suspended (no host, charger only, or
   battery): key scanning, Bluetooth output and the watchdog feed must not stop.
   Remote wakeup is handled in process_record_kb instead. */
#define NO_USB_STARTUP_CHECK
