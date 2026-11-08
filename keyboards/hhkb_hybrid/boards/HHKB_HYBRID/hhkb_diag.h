// SPDX-License-Identifier: MIT
// Reset diagnostics kept in RAM across resets (docs/troubleshooting.md).
//
// The record sits above ram0 (linker script), so neither the C runtime nor
// ChibiOS touch it, and below 0x20004FF8 (last BT slot, power.c). The PFU
// bootloader's stack starts at 0x20002030 and it uses no RAM above that, so the
// record also survives a pass through the bootloader (software reset, watchdog
// reset, the update mode and a firmware update), as long as the board keeps
// power. When power is lost the RAM contents are gone and the record starts
// over; a power-on / brown-out reset that left the RAM intact is only counted
// (por_kept).
#pragma once

#include <stdint.h>

#define HHKB_DIAG_ADDR  0x20004FC0u
#define HHKB_DIAG_MAGIC 0x48444732u /* "HDG2": change it whenever the layout changes */

/* Where the main loop was last (diag->stage). Kept in the record at a reset. */
enum hhkb_diag_stage {
    HHKB_STAGE_BOOT = 0,      /* before the first housekeeping run */
    HHKB_STAGE_SCAN,          /* matrix_scan_custom */
    HHKB_STAGE_LOOP,          /* between housekeeping runs (QMK keyboard_task, USB, BT reports) */
    HHKB_STAGE_HK_NRF,        /* housekeeping: nRF link */
    HHKB_STAGE_HK_BT,         /* housekeeping: BT output / reconnect */
    HHKB_STAGE_HK_POWER,      /* housekeeping: power_task (button, auto-sleep, clock switch) */
    HHKB_STAGE_IDLE,          /* power_idle (battery: sleep until the next scan) */
    HHKB_STAGE_CLOCK_DOWN,    /* switching to the 4 MHz battery clock */
    HHKB_STAGE_CLOCK_UP,      /* switching back to 32 MHz (USB) */
    HHKB_STAGE_OFF,           /* power off: waiting for the nRF idle */
    HHKB_STAGE_STOP,          /* power off: STOP loop */
    HHKB_STAGE_USB_FAILSAFE,  /* watchdog no longer fed: USB not configured in time */
    HHKB_STAGE_BT_FLUSH,      /* waiting for the nRF to read a report */
};

/* Why the firmware last asked for the PFU update mode (diag->update_reason). */
enum hhkb_diag_update_reason {
    HHKB_UPDATE_NONE = 0,
    HHKB_UPDATE_E0,           /* AA AA E0 (tools/flash.py) */
    HHKB_UPDATE_F0,           /* AA AA F0 (tools/flash.py, revert to stock) */
    HHKB_UPDATE_BOOT_KEY,     /* QK_BOOT or bootmagic */
    HHKB_UPDATE_RESET_STREAK, /* repeated watchdog resets / faults */
};

/* flags / inc_flags */
#define HHKB_DIAG_F_VBUS     0x01u /* USB power present */
#define HHKB_DIAG_F_LP_CLOCK 0x02u /* running at the 4 MHz battery clock */
#define HHKB_DIAG_F_BT_OUT   0x04u /* output to Bluetooth */
#define HHKB_DIAG_F_USB_CFG  0x08u /* USB configured by a host since power appeared */
#define HHKB_DIAG_F_NRF_UP   0x10u /* nRF hello seen */
#define HHKB_DIAG_F_BT_LINK  0x20u /* BT link up */

typedef struct {
    uint32_t magic;
    uint8_t  streak;        /* resets in a row that did not reach a healthy run */
    uint8_t  wdt_resets;    /* watchdog resets since the record was created */
    uint8_t  faults;        /* CPU faults / unhandled interrupts since then */
    uint8_t  update_reason; /* enum hhkb_diag_update_reason */
    uint32_t boot_csr;      /* RCC->CSR seen at the last start (reset flags) */
    /* live: updated while running */
    uint32_t uptime_ms;
    uint8_t  stage;
    uint8_t  flags;
    uint8_t  fault_pending; /* set by the fault handler right before its reset */
    uint8_t  boots;         /* starts since the record was created */
    uint32_t spi_irqs;      /* nRF SPI interrupts since the start */
    /* incident: copy of the live fields at the last watchdog reset or fault */
    uint32_t inc_uptime_ms;
    uint8_t  inc_stage;
    uint8_t  inc_flags;
    uint8_t  inc_kind;      /* 1 = watchdog, 2 = fault */
    uint8_t  inc_ipsr;      /* exception number of the fault (0 for the watchdog) */
    uint32_t inc_spi_irqs;
    uint32_t inc_pc_psp;    /* fault: PC in the PSP frame (fault in a thread) */
    uint32_t inc_pc_msp;    /* fault: PC in the MSP frame (fault in an interrupt) */
    uint8_t  create_csr;    /* RCC->CSR >> 24 at the start that created the record (kept) */
    uint8_t  por_kept;      /* power-on / brown-out resets that left the record intact */
    uint8_t  batt;          /* live: battery level inputs, HHKB_DIAG_BATT_* (0xFF: not read yet) */
    uint8_t  inc_batt;      /* incident: batt at the last watchdog reset or fault */
} hhkb_diag_t;

/* batt / inc_batt: battery level inputs that read low (stock thresholds). */
#define HHKB_DIAG_BATT_15 0x01u /* PB7 low: <= 15 % */
#define HHKB_DIAG_BATT_5  0x02u /* PB8 low: <= 5 % */
#define HHKB_DIAG_BATT_0  0x04u /* PB9 low: 0 % */

_Static_assert(sizeof(hhkb_diag_t) == 48 && sizeof(hhkb_diag_t) % 4 == 0, "hhkb_diag_t must fit 0x20004FC0..0x20004FEF");

#define HHKB_DIAG ((volatile hhkb_diag_t *)HHKB_DIAG_ADDR)

static inline void hhkb_diag_stage(uint8_t stage) {
    HHKB_DIAG->stage = stage;
}
