// SPDX-License-Identifier: MIT
// HHKB Professional Hybrid main board (STM32L072RBT6).
#pragma once

#define BOARD_HHKB_HYBRID
#define BOARD_NAME "HHKB Professional Hybrid"

/* No external oscillators are used (HSI16 -> PLL). */
#define STM32_LSECLK 0U
#define STM32_LSEDRV (3U << 11U)
#define STM32_HSECLK 0U

#undef STM32L072xx
#define STM32L072xx

#if !defined(_FROM_ASM_)
#    ifdef __cplusplus
extern "C" {
#    endif
void boardInit(void);
/* Same EEPROM writes as the stock E0 handler, then reset into the PFU
   bootloader's HID update mode. Never returns. */
void hhkb_enter_update_mode(void);
#    ifdef __cplusplus
}
#    endif
#endif
