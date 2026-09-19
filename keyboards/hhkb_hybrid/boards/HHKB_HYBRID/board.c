// SPDX-License-Identifier: MIT
// HHKB Professional Hybrid main board (STM32L072RBT6).
//
// The PFU bootloader (0x08000000) jumps to this application at 0x08010000
// only when EEPROM[0x08080360]==1, EEPROM[0x08081200]==1 and the CRC16 of
// the 64KiB application matches EEPROM[0x08080370] (see docs/hardware.md).
// It jumps with interrupts disabled and its own clock/NVIC state still set,
// so __early_init() undoes that before the normal ChibiOS clock setup.
#include "hal.h"

/* EEPROM cells written by the stock E0 handler. */
#define EE_BOOT_FLAG  0x08080360U /* 1: bootloader may jump to the app */
#define EE_APP_CRC    0x08080370U /* expected CRC16 of the app */
#define EE_UPDATE_REQ 0x08081350U /* set to 1 on E0 */

#define NVM_PEKEY1 0x89ABCDEFU
#define NVM_PEKEY2 0x02030405U

#define BUSY_WAIT_LIMIT 100000U

static void nvm_wait_ready(void) {
    for (uint32_t i = 0; i < BUSY_WAIT_LIMIT && (FLASH->SR & FLASH_SR_BSY); i++) {
    }
}

static void eeprom_program_word(uint32_t address, uint32_t value) {
    if (*(uint32_t *)address == value) {
        return;
    }
    nvm_wait_ready();
    FLASH->SR = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_OPTVERR | FLASH_SR_RDERR | FLASH_SR_NOTZEROERR | FLASH_SR_FWWERR;
    *(uint32_t *)address = value;
    nvm_wait_ready();
}

void hhkb_enter_update_mode(void) {
    __disable_irq();

    if (FLASH->PECR & FLASH_PECR_PELOCK) {
        FLASH->PEKEYR = NVM_PEKEY1;
        FLASH->PEKEYR = NVM_PEKEY2;
    }
    eeprom_program_word(EE_BOOT_FLAG, 0);
    eeprom_program_word(EE_APP_CRC, 0);
    eeprom_program_word(EE_UPDATE_REQ, 1);
    FLASH->PECR |= FLASH_PECR_PRGLOCK | FLASH_PECR_PELOCK;

    /* Clear reset flags so the next boot does not look like a watchdog reset. */
    RCC->CSR |= RCC_CSR_RMVF;
    NVIC_SystemReset();
}

/* IWDG: LSI (~37kHz) / 64, reload 0xFFF -> ~7s (5..11s over LSI tolerance). */
static void watchdog_start(void) {
    IWDG->KR  = 0xCCCC;
    IWDG->KR  = 0x5555;
    IWDG->PR  = 4;
    IWDG->RLR = 0xFFF;
    for (uint32_t i = 0; i < BUSY_WAIT_LIMIT && IWDG->SR; i++) {
    }
    IWDG->KR = 0xAAAA;
}

static void undo_bootloader_state(void) {
    /* The bootloader runs the HAL SysTick and jumps with interrupts masked, so a
       SysTick (or PendSV) can still be pending. ChibiOS uses TIM21 for its tick
       and has no SysTick handler: clear them before interrupts get enabled. */
    SysTick->CTRL = 0;
    SCB->ICSR     = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    NVIC->ICER[0] = 0xFFFFFFFFU;
    NVIC->ICPR[0] = 0xFFFFFFFFU;

    /* Back to MSI with the PLL stopped: stm32_clock_init() assumes reset state. */
    RCC->CR |= RCC_CR_MSION;
    for (uint32_t i = 0; i < BUSY_WAIT_LIMIT && !(RCC->CR & RCC_CR_MSIRDY); i++) {
    }
    RCC->CFGR &= ~RCC_CFGR_SW;
    for (uint32_t i = 0; i < BUSY_WAIT_LIMIT && (RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_MSI; i++) {
    }
    RCC->CR &= ~RCC_CR_PLLON;
    for (uint32_t i = 0; i < BUSY_WAIT_LIMIT && (RCC->CR & RCC_CR_PLLRDY); i++) {
    }

    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN | RCC_IOPENR_GPIODEN | RCC_IOPENR_GPIOHEN;
}

/* Runs before .data/.bss initialization: no globals here. */
void __early_init(void) {
    /* Fail-safe: the previous run hung (or faulted) and was reset by the
       watchdog -> hand the board back to the PFU bootloader update mode. */
    if (RCC->CSR & RCC_CSR_IWDGRSTF) {
        hhkb_enter_update_mode();
    }
    RCC->CSR |= RCC_CSR_RMVF;

    watchdog_start();
    undo_bootloader_state();

    /* GPIOs are left as configured by the bootloader; the keyboard code
       configures the pins it uses. */
    stm32_clock_init();
}

void boardInit(void) {}
