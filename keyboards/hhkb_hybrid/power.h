// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Power management: STOP mode with periodic LPTIM1 wake to feed the watchdog,
   power button (PC5) and inactivity auto-sleep. Battery only; ignored on USB
   power. Wake (power button 1..10 s, or USB plugged) does a software reset, so
   __early_init re-runs a full clean boot (docs/reverse-engineering/power-management.md). */
void power_init(void);

/* Battery low-power run (HHKB_LOW_POWER, docs/reverse-engineering/power-management.md §7).
   power_task() drops the clock to 4 MHz and stops USB on battery (after
   start-up, with USB power stably absent), and switches back when USB power
   comes. power_low_clock() is true while at 4 MHz.
   power_idle(): call at the end of each main-loop pass; at 4 MHz it sleeps for
   the rest of the HHKB_LP_SCAN_INTERVAL_MS scan period. Both are no-ops without
   HHKB_LOW_POWER. */
#ifdef HHKB_LOW_POWER
bool power_low_clock(void);
void power_idle(void);
#else
static inline bool power_low_clock(void) {
    return false;
}
static inline void power_idle(void) {}
#endif

/* Call every housekeeping cycle with the current USB-power state.
   Handles the power button and the inactivity timer; may not return (it enters
   STOP and later resets). */
void power_task(bool vbus);

/* True when DIP SW6 is ON (PC4 low): auto-sleep off, stay awake after a failed
   BT reconnect like the stock firmware's state 5. */
bool power_sw6_on(void);

/* Enter OFF / STOP (nRF idle, orange LED). Does not return. Battery path only. */
void power_enter_off(void);

/* Battery only. A 0.1..2 s press (not a 2 s power-off hold) calls this.
   NULL disables it. USB power ignores the button entirely. */
void power_set_short_press_cb(void (*cb)(void));

/* Reset the inactivity timer (call on key activity). */
void power_note_activity(void);

/* Inactivity auto-sleep timeout. Defaults to HHKB_AUTOSLEEP_TIMEOUT_MS; the VIA
   build sets it from its settings. Restarts the inactivity timer. */
void power_set_autosleep_timeout(uint32_t ms);


/* Last BT slot, kept across a software reset (-1 if none saved). */
void   power_save_last_slot(uint8_t slot);
int8_t power_load_last_slot(void);

/* JIS-on-US correction mode flag, persisted in the stock sleep-timeout EEPROM
   byte's low bit (like the stock JIS/US toggle). True non-volatile: survives a
   full power loss and a revert to stock. Load returns -1 if the byte looks
   uninitialised. While QMK runs, only this toggle writes the byte. */
void   power_save_jis_mode(bool on);
int8_t power_load_jis_mode(void);
