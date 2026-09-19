// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "report.h"

/* SPI1 slave link to the nRF52832 Bluetooth module (docs/reverse-engineering/bluetooth-protocol.md). */
void nrf_spi_init(void);
/* Call from the main loop: re-sends the request pulse when the nRF missed it. */
void nrf_spi_task(void);
/* Pulse the request line (PB6) low: the nRF answers by polling with command 0x02. */
void nrf_spi_request(void);
/* PD2 enables the nRF (low = running). Driven low at init; kept for diagnostics. */
void nrf_spi_set_pd2(bool high);

/* True once the nRF sent its hello (0x01). */
bool nrf_spi_ready(void);
/* Host slot (0..3) the nRF reports as connected (0x83 (slot<<4)|3), or -1. */
int8_t nrf_spi_connected_slot(void);
/* Queue a keyboard report for the nRF (sent on its next 0x02 poll). */
void nrf_spi_send_keyboard(const report_keyboard_t *report);
/* True when the last queued keyboard report has been read by the nRF (no report
   pending). Used to serialise the taps of the JIS/US translation over BT, where
   only the latest report is kept and a too-fast sequence would drop a keypress. */
bool nrf_spi_report_idle(void);
/* Host slot requests (docs/reverse-engineering/bluetooth-protocol.md, stock). Slots are 0..3 (keys 1..4).
   select: 0x02 -> 10, 0x06 -> slot. The nRF answers 61 00 (bonded, reconnects) or 61 01.
   idle:   0x02 -> 40, 0x03 -> 10 00. Stops advertising / disconnects (83 01).
   pair:   0x02 -> 40, 0x03 -> 01 slot. Open advertising on the slot; a new bond REPLACES
           the slot's existing one (83 02, then 83 (slot<<4)|3).
   delete: 0x02 -> 40, 0x03 -> 02 slot. Deletes the slot's bond (85 00 ok). Not bound to keys. */
void nrf_spi_select_slot(uint8_t slot);
void nrf_spi_bt_idle(void);
void nrf_spi_pair_slot(uint8_t slot);
void nrf_spi_delete_slot(uint8_t slot);
/* True while a host slot request is waiting for the nRF. */
bool nrf_spi_control_busy(void);
/* Queue consumer key state: 4 bitmask bytes = BLE consumer reports 2..5 of the stock
   nRF report map (0x02 -> 02 R2 R3 R4 R5). All zero releases. */
void nrf_spi_send_consumer(const uint8_t bits[4]);
/* Report the battery level 0..100 (0x02 -> 80 level, 0x05 -> level; stock). */
void nrf_spi_send_battery(uint8_t level);