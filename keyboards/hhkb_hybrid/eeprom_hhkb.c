// SPDX-License-Identifier: GPL-2.0-or-later
/* EEPROM driver for the VIA keymap (EEPROM_DRIVER = custom).
 *
 * QMK's STM32L0 driver maps its EEPROM at 0x08080000, which overlaps the PFU
 * bootloader's boot flag (0x08080360) and app CRC (0x08080370): an eeconfig
 * reset would stop the app from booting. This driver instead uses a window the
 * stock firmware itself writes: its keymap table B, 0x08080700..0x08080CFF
 * (1536 B). See docs/via.md and docs/reverse-engineering/stm32-analysis.md
 * §4-1/§4-2.
 *
 * Formatting (eeconfig reset) and erasing also invalidate the stock keymap magic
 * (0x08080300 != 0xAAAA). That alone does not make a revert safe: the PFU
 * bootloader repairs the magic and table A only, so the window must also be
 * zeroed before going back to the stock firmware (hhkb_hybrid.c: F0 command,
 * bootmagic).
 *
 * Word write/erase sequence follows QMK's eeprom_stm32_L0_L1.c.
 */
#include <stdint.h>
#include <string.h>

#include <hal.h>
#include "eeprom_driver.h"

#define HHKB_EEPROM_BASE  0x08080700U /* stock keymap table B */
#define HHKB_EEPROM_LIMIT 1536U       /* ..0x08080CFF */

/* Stock keymap tables are used only while this reads 0xAAAA (u16). */
#define STOCK_KEYMAP_MAGIC_ADDR 0x08080300U

#define NVM_PEKEY1 0x89ABCDEFU
#define NVM_PEKEY2 0x02030405U

_Static_assert(EEPROM_SIZE <= HHKB_EEPROM_LIMIT, "EEPROM_SIZE exceeds the stock keymap table B window");
_Static_assert((EEPROM_SIZE % 4) == 0, "EEPROM_SIZE must be word aligned");

#define EE_BYTE(offset) (*(volatile uint8_t *)(HHKB_EEPROM_BASE + (offset)))
#define EE_WORD(offset) (*(volatile uint32_t *)(HHKB_EEPROM_BASE + (offset)))

static void ee_wait(void) {
    for (uint32_t i = 0; i < 100000U && (FLASH->SR & FLASH_SR_BSY); i++) {
    }
}

static void ee_unlock(void) {
    ee_wait();
    if (FLASH->PECR & FLASH_PECR_PELOCK) {
        FLASH->PEKEYR = NVM_PEKEY1;
        FLASH->PEKEYR = NVM_PEKEY2;
    }
}

static void ee_lock(void) {
    ee_wait();
    FLASH->PECR |= FLASH_PECR_PELOCK;
}

/* Make the stock firmware treat its keymap tables as invalid. Same value the
   stock firmware writes before rewriting them; upper word bytes are kept. */
static void invalidate_stock_keymap(void) {
    volatile uint32_t *magic = (volatile uint32_t *)STOCK_KEYMAP_MAGIC_ADDR;
    uint32_t           w     = *magic;
    if ((w & 0xFFFFU) != 0xFFFFU) {
        ee_wait();
        *magic = (w & 0xFFFF0000U) | 0xFFFFU;
        ee_wait();
    }
}

void eeprom_driver_init(void) {}

/* erase=false is the eeconfig reset, which QMK runs after via_init() at boot:
   erasing here would wipe the keymaps VIA has just written. Leftover bytes
   (e.g. stock keymap data after a revert) are harmless: eeconfig rewrites its
   fields, VIA resets on a magic mismatch and the VIA config byte carries its own
   layout version (hhkb_hybrid.c via_init_kb). */
void eeprom_driver_format(bool erase) {
    if (erase) {
        eeprom_driver_erase();
        return;
    }
    ee_unlock();
    invalidate_stock_keymap();
    ee_lock();
}

void eeprom_driver_erase(void) {
    ee_unlock();
    for (uint32_t offset = 0; offset < EEPROM_SIZE; offset += 4) {
        ee_wait();
        FLASH->PECR |= FLASH_PECR_ERASE | FLASH_PECR_DATA;
        EE_WORD(offset) = 0;
        ee_wait();
        FLASH->PECR &= ~(FLASH_PECR_ERASE | FLASH_PECR_DATA);
    }
    invalidate_stock_keymap();
    ee_lock();
}

void eeprom_read_block(void *buf, const void *addr, size_t len) {
    uint32_t start = (uint32_t)addr;
    for (size_t i = 0; i < len; i++) {
        if (start + i >= EEPROM_SIZE) {
            break;
        }
        ((uint8_t *)buf)[i] = EE_BYTE(start + i);
    }
}

void eeprom_write_block(const void *buf, void *addr, size_t len) {
    uint32_t start = (uint32_t)addr;
    if (start >= EEPROM_SIZE) {
        return;
    }
    if (len > EEPROM_SIZE - start) {
        len = EEPROM_SIZE - start;
    }
    uint32_t end = start + len;

    ee_unlock();
    for (uint32_t word = start & ~3U; word < ((end + 3U) & ~3U); word += 4) {
        uint32_t old_word = EE_WORD(word);
        uint32_t new_word = old_word;
        for (uint32_t i = 0; i < 4; i++) {
            uint32_t byte = word + i;
            if (byte >= start && byte < end) {
                uint8_t v = ((const uint8_t *)buf)[byte - start];
                new_word  = (new_word & ~(0xFFU << (i * 8))) | ((uint32_t)v << (i * 8));
            }
        }
        if (new_word != old_word) { /* only changed words are programmed */
            ee_wait();
            EE_WORD(word) = new_word;
        }
    }
    ee_lock();
}
