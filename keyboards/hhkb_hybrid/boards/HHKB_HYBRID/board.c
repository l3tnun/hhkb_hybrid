// SPDX-License-Identifier: MIT
// HHKB Professional Hybrid main board (STM32L072RBT6).
//
// The PFU bootloader (0x08000000) jumps to this application at 0x08010000
// only when EEPROM[0x08080360]==1, EEPROM[0x08081200]==1 and the CRC16 of
// the 64KiB application matches EEPROM[0x08080370] (see docs/hardware.md).
// It jumps with interrupts disabled and its own clock/NVIC state still set,
// so __early_init() undoes that before the normal ChibiOS clock setup.
#include "hal.h"
#include "hhkb_diag.h"

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

/* Fail-safe: a watchdog reset or a CPU fault restarts QMK. Only when this many
   happen in a row, each before the firmware ran healthy for a while
   (hhkb_hybrid.c, HHKB_HEALTHY_RUN_MS), the board goes to the PFU update mode.
   Entering the update mode clears the boot flag, and the PFU bootloader then
   clears the update request on its first start, so after any later reset it
   runs its own built-in keyboard (US layout, Bluetooth, no QMK) until a
   firmware is written again: one transient hang must not do that. */
#define HHKB_RESET_STREAK_LIMIT 3

static void diag_copy_live(volatile hhkb_diag_t *d) {
    d->inc_uptime_ms = d->uptime_ms;
    d->inc_stage     = d->stage;
    d->inc_flags     = d->flags;
    d->inc_spi_irqs  = d->spi_irqs;
    d->inc_batt      = d->batt;
}

/* Returns true when the board should go to the update mode. */
static bool diag_start(uint32_t csr) {
    volatile hhkb_diag_t *d = HHKB_DIAG;
    /* A real power loss leaves the RAM random, so the magic no longer matches.
       A power-on / brown-out reset with the magic intact kept the RAM: keep the
       record (it may explain the reset) and count it. */
    if (d->magic != HHKB_DIAG_MAGIC) {
        for (uint32_t i = 0; i < sizeof(hhkb_diag_t) / 4; i++) {
            ((volatile uint32_t *)d)[i] = 0;
        }
        d->magic      = HHKB_DIAG_MAGIC;
        d->create_csr = (uint8_t)(csr >> 24);
    } else if ((csr & RCC_CSR_PORRSTF) && d->por_kept < 0xFF) {
        d->por_kept++;
    }
    bool wdt   = (csr & RCC_CSR_IWDGRSTF) != 0;
    bool fault = d->fault_pending != 0; /* the fault handler filled the incident */
    if (wdt && !fault) {
        diag_copy_live(d);
        d->inc_kind   = 1;
        d->inc_ipsr   = 0;
        d->inc_pc_psp = 0;
        d->inc_pc_msp = 0;
        if (d->wdt_resets < 0xFF) {
            d->wdt_resets++;
        }
    }
    if ((wdt || fault) && d->streak < 0xFF) {
        d->streak++;
    }
    d->fault_pending = 0;
    d->boot_csr      = csr;
    d->uptime_ms     = 0;
    d->stage         = HHKB_STAGE_BOOT;
    d->flags         = 0;
    d->spi_irqs      = 0;
    d->batt          = 0xFF;
    if (d->boots < 0xFF) {
        d->boots++;
    }
    if ((wdt || fault) && d->streak >= HHKB_RESET_STREAK_LIMIT) {
        d->streak        = 0;
        d->update_reason = HHKB_UPDATE_RESET_STREAK;
        return true;
    }
    return false;
}

static bool diag_frame_ok(const uint32_t *sp) {
    uint32_t a = (uint32_t)sp;
    return (a & 3u) == 0 && a >= 0x20000000u && a + 32u <= HHKB_DIAG_ADDR;
}

/* CPU fault or an interrupt without a handler (ChibiOS sends all of them to
   _unhandled_exception, which spins until the watchdog fires). Record where it
   happened and restart at once. The vector code reaches us with BL, so the
   EXC_RETURN is lost: both stacks' frames are recorded. */
void __attribute__((used, noreturn)) hhkb_diag_fault(const uint32_t *msp, const uint32_t *psp, uint32_t ipsr) {
    volatile hhkb_diag_t *d = HHKB_DIAG;
    if (d->magic == HHKB_DIAG_MAGIC) {
        diag_copy_live(d);
        d->inc_kind   = 2;
        d->inc_ipsr   = (uint8_t)ipsr;
        d->inc_pc_msp = diag_frame_ok(msp) ? msp[6] : 0;
        d->inc_pc_psp = diag_frame_ok(psp) ? psp[6] : 0;
        if (d->faults < 0xFF) {
            d->faults++;
        }
        d->fault_pending = 1;
    }
    NVIC_SystemReset();
    for (;;) {
    }
}

void __attribute__((naked, noreturn)) _unhandled_exception(void) {
    __asm__ volatile(
        "mrs r0, msp\n"
        "mrs r1, psp\n"
        "mrs r2, ipsr\n"
        "ldr r3, =hhkb_diag_fault\n"
        "bx  r3\n"
        ".ltorg\n");
}

/* Runs before .data/.bss initialization: no globals here. */
void __early_init(void) {
    uint32_t csr = RCC->CSR;
    if (diag_start(csr)) {
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
