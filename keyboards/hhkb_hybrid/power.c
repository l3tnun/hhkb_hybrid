// SPDX-License-Identifier: MIT
// Power management for the HHKB Hybrid (STM32L072), battery operation only.
//
// Follows the stock firmware (docs/reverse-engineering/power-management.md):
//   - Power button PC5 (low = pressed): a >= 2 s hold turns the board off.
//   - Inactivity auto-sleep after a timeout, enabled when DIP SW6 is OFF
//     (PC4 high). SW6 ON (PC4 low) disables auto-sleep.
//   - Off/sleep is STM32 STOP mode. The matrix is muxed to one ADC pin with no
//     EXTI, so keys cannot wake STOP (this matches the stock firmware): wake is
//     PC5 (button) or PC13 (USB plugged) only. LPTIM1 (~2 s) wakes periodically
//     only to refresh the IWDG so it never expires in STOP.
//   - Waking that should power on (button 1..10 s, or USB) does NVIC_SystemReset;
//     __early_init then boots cleanly.
//
// USB power (PC13 low) disables the power button and auto-sleep entirely.
#include "quantum.h"
#include <hal.h>
#include "power.h"
#include "nrf_spi.h"
#include "led_indicator.h"
#ifdef HHKB_LOW_POWER
#    include "usb_main.h"
#endif

#define PWR_BUTTON_PIN C5  /* low = pressed */
#define USB_VBUS_PIN   C13 /* low = USB power present */
#define DIP_SW6_PIN    C4  /* low = SW6 on: disable auto-sleep */

#define BUTTON_OFF_HOLD_MS 2000
#define BUTTON_SHORT_MIN_MS 100 /* stock short press: 0.1..2 s */
#define BUTTON_ON_MIN_MS   1000
#define BUTTON_ON_MAX_MS   10000
#define OFF_ORANGE_MS      1500 /* keep the orange 'off' indication visible */

/* LSI ~37 kHz, LPTIM1 prescaler /128 -> ~289 Hz. */
#define LSI_HZ         37000u
#define LPTIM_HZ       (LSI_HZ / 128u)
#define LPTIM_WAKE_ARR ((LPTIM_HZ * 2u) - 1u) /* ~2 s */
#define EXTI_LINE_LPTIM1 29

/* Last BT slot, kept across a software reset so a power-on reconnects the host
   that was in use (not the lowest-numbered bond the nRF would pick). Uses the
   reserved top-of-RAM words that survive a software reset. */
#define SLOT_ADDR        ((volatile uint32_t *)0x20004FF8u)
#define SLOT_MAGIC       0x5100u

/* JIS-on-US mode flag is persisted in the stock's sleep-timeout EEPROM byte,
   exactly like the stock JIS/US toggle: the low bit of the minutes value
   (1..60) carries the flag. The stock startup check keeps values in 1..60, so
   an odd value (e.g. 31) survives a full power loss and a revert to stock. This
   is genuine non-volatile STM32L0 data EEPROM (byte write, auto-erase). */
#define SLEEP_EE_ADDR    0x08080520u  /* sleep timeout in minutes, 1..60 */
#define NVM_PEKEY1       0x89ABCDEFu
#define NVM_PEKEY2       0x02030405u

#ifdef HHKB_LOW_POWER
/* Battery low-power run (docs/reverse-engineering/power-management.md §7).
   The stock firmware runs at 4 MHz on battery (HSI16 / 4 into the PLL) and at
   16 MHz only on USB power. ChibiOS is built for the 32 MHz PLL clock (USB needs
   it), so on battery we switch at run time to HSI16 / 4 = 4 MHz with the PLL,
   MSI and HSI48 off, and the USB peripheral stopped; on USB power we switch back.
   Both timers whose prescaler ChibiOS computes from the build-time clock are
   corrected at each switch: TIM21 (system tick) and TIM3 (wait_us). */
#    if STM32_ST_USE_TIMER != 21
#        error "power.c retimes TIM21 as the ChibiOS system tick"
#    endif
#    define LP_HCLK_HZ    4000000u  /* HSI16 / 4 (RCC_CR HSIDIVEN) */
#    define MID_HCLK_HZ   16000000u /* HSI16, only while the PLL locks */
#    define VBUS_STABLE_MS 50       /* PC13 must be stable this long to switch */
/* No switch during start-up: QMK starts USB after keyboard_pre_init, and USB
   must never be started without the PLL. */
#    define BOOT_SETTLE_MS 1000

static bool     lp_clock;         /* running at LP_HCLK_HZ, USB stopped */
static bool     lp_vbus_last;
static uint16_t lp_vbus_since;
static uint16_t lp_scan_start;

/* Timers clocked from PCLK (PPRE1 = PPRE2 = 1, so TIMCLK = HCLK). Keep the
   TIM21 count (it is the system time in tickless mode): UG reloads the
   prescaler at once and clears the count, which is then written back. */
static void retime_timers(uint32_t hclk) {
    uint16_t cnt = (uint16_t)TIM21->CNT;
    TIM21->PSC   = (uint16_t)(hclk / CH_CFG_ST_FREQUENCY - 1u);
    TIM21->EGR   = TIM_EGR_UG;
    TIM21->CNT   = cnt;
    TIM21->SR    = ~TIM_SR_UIF; /* UG sets UIF; the update interrupt is unused */
    GPTD3.clock  = hclk;         /* wait_us: gptStart() derives PSC from it */
}

static void clock_down(void) {
    /* USB cannot run without the PLL: detach and power the transceiver down
       (the stock firmware does not start USB on battery). */
    if (USB_DRIVER.state != USB_STOP) {
        usbDisconnectBus(&USB_DRIVER);
        usbStop(&USB_DRIVER);
    }
    /* Make sure TIM3 is started, so that retime_timers() sets GPTD3.clock for
       good. Not wait_us(1): that programs ARR = 0 and the timer never updates. */
    wait_us(2);

    chSysLock();
    /* PLL -> HSI16 (16 MHz, still 1 wait state), then PLL off, then /4. The
       divider also feeds the PLL, so it may only be set with the PLL unused. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {
    }
    RCC->CR &= ~RCC_CR_PLLON;
    RCC->CR |= RCC_CR_HSIDIVEN;
    while (!(RCC->CR & RCC_CR_HSIDIVF)) {
    }
    /* 4 MHz in range 1: zero wait states, no prefetch / pre-read. */
    FLASH->ACR &= ~(FLASH_ACR_LATENCY | FLASH_ACR_PRFTEN | FLASH_ACR_PRE_READ);
    retime_timers(LP_HCLK_HZ);
    chSysUnlock();

    /* Oscillators nothing uses on battery: MSI (ChibiOS keeps it as a PLL
       fallback), HSI48 and its reference buffer (left on by the bootloader
       when it saw USB power), CRS. LSI stays on for the IWDG and LPTIM1. */
    RCC->CR &= ~RCC_CR_MSION;
    RCC->CRRCR &= ~RCC_CRRCR_HSI48ON;
    SYSCFG->CFGR3 &= ~(SYSCFG_CFGR3_ENREF_HSI48 | SYSCFG_CFGR3_ENBUF_VREFINT_ADC | SYSCFG_CFGR3_ENBUF_SENSOR_ADC);
    RCC->APB1ENR &= ~RCC_APB1ENR_CRSEN;
    lp_clock = true;
}

static void clock_up(void) {
    /* 32 MHz needs one wait state: set it before raising the clock. */
    FLASH->ACR |= FLASH_ACR_LATENCY | FLASH_ACR_PRFTEN | FLASH_ACR_PRE_READ;
    while (!(FLASH->ACR & FLASH_ACR_LATENCY)) {
    }
    chSysLock();
    RCC->CR &= ~RCC_CR_HSIDIVEN;
    while (RCC->CR & RCC_CR_HSIDIVF) {
    }
    retime_timers(MID_HCLK_HZ);
    chSysUnlock();

    /* The PLL settings written by stm32_clock_init() are still in RCC_CFGR.
       Wait for the lock with interrupts on (the nRF SPI link keeps running). */
    RCC->CR |= RCC_CR_MSION | RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {
    }

    chSysLock();
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
    retime_timers(STM32_HCLK);
    chSysUnlock();
    lp_clock = false;

    restart_usb_driver(&USB_DRIVER);
}

bool power_low_clock(void) {
    return lp_clock;
}

/* Switch the clock once PC13 has been stable for VBUS_STABLE_MS (a glitch must
   not drop a working USB link), and not before BOOT_SETTLE_MS after start-up.
   The board starts at 32 MHz with USB running, as without HHKB_LOW_POWER. */
static void clock_task(bool vbus) {
    if (vbus != lp_vbus_last) {
        lp_vbus_last  = vbus;
        lp_vbus_since = timer_read();
        return;
    }
    if (timer_read32() < BOOT_SETTLE_MS || timer_elapsed(lp_vbus_since) < VBUS_STABLE_MS) {
        return;
    }
    if (vbus && lp_clock) {
        clock_up();
    } else if (!vbus && !lp_clock) {
        clock_down();
    }
}

/* Battery only: sleep for the rest of the scan period instead of scanning
   continuously (the ChibiOS idle thread runs WFI; QMK enables
   CORTEX_ENABLE_WFI_IDLE by default). The stock firmware scans every ~11 ms on
   battery. */
void power_idle(void) {
    if (!lp_clock) {
        return;
    }
    uint16_t spent = timer_elapsed(lp_scan_start);
    if (spent < HHKB_LP_SCAN_INTERVAL_MS) {
        chThdSleepMilliseconds(HHKB_LP_SCAN_INTERVAL_MS - spent);
    }
    lp_scan_start = timer_read();
}
#endif

static uint32_t inactivity_start;
static uint32_t autosleep_timeout_ms = HHKB_AUTOSLEEP_TIMEOUT_MS;
static uint16_t button_down_start;
static bool     button_was_down;
static void (*short_press_cb)(void);

/* Harmless handlers: with PRIMASK set during the STOP loop these never run,
   but if ever taken they just clear the source so they cannot spin. */
OSAL_IRQ_HANDLER(Vector5C) { /* EXTI4_15 */
    EXTI->PR = EXTI->PR;
}
OSAL_IRQ_HANDLER(Vector74) { /* LPTIM1 */
    LPTIM1->ICR = LPTIM_ICR_ARRMCF;
}

static uint16_t lptim_cnt(void) {
    uint16_t a, b;
    do {
        a = (uint16_t)LPTIM1->CNT;
        b = (uint16_t)LPTIM1->CNT;
    } while (a != b);
    return a;
}

static void iwdg_refresh(void) {
    IWDG->KR = 0xAAAA;
}

static void lptim_start(void) {
    RCC->CSR |= RCC_CSR_LSION;
    while (!(RCC->CSR & RCC_CSR_LSIRDY)) {
    }
    RCC->APB1ENR |= RCC_APB1ENR_LPTIM1EN;
    RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_LPTIM1SEL) | RCC_CCIPR_LPTIM1SEL_0; /* LSI */

    LPTIM1->CR   = 0;
    LPTIM1->CFGR = (0x7u << LPTIM_CFGR_PRESC_Pos); /* /128 */
    LPTIM1->IER  = LPTIM_IER_ARRMIE;
    LPTIM1->CR   = LPTIM_CR_ENABLE;
    LPTIM1->ICR  = LPTIM_ICR_ARROKCF;
    LPTIM1->ARR  = LPTIM_WAKE_ARR;
    while (!(LPTIM1->ISR & LPTIM_ISR_ARROK)) {
    }
    LPTIM1->CR |= LPTIM_CR_CNTSTRT;

    EXTI->IMR  |= (1u << EXTI_LINE_LPTIM1);
    EXTI->RTSR |= (1u << EXTI_LINE_LPTIM1);
}

static void exti_wake_lines(void) {
    rccEnableAPB2(RCC_APB2ENR_SYSCFGEN, true);
    SYSCFG->EXTICR[1] = (SYSCFG->EXTICR[1] & ~SYSCFG_EXTICR2_EXTI5) | SYSCFG_EXTICR2_EXTI5_PC;
    SYSCFG->EXTICR[3] = (SYSCFG->EXTICR[3] & ~SYSCFG_EXTICR4_EXTI13) | SYSCFG_EXTICR4_EXTI13_PC;
    EXTI->FTSR |= (1u << 5) | (1u << 13); /* falling: button press, USB plug */
    EXTI->IMR  |= (1u << 5) | (1u << 13);
}

/* Enable ONLY the wake interrupts in the NVIC; disable and clear everything else.
   On Cortex-M0+ WFI wakes on an interrupt that is enabled in the NVIC even while
   PRIMASK masks it from being taken. This was missing before and STOP never woke. */
static void nvic_for_stop(void) {
    NVIC->ICER[0] = 0xFFFFFFFFu;
    NVIC->ICPR[0] = 0xFFFFFFFFu;
    nvicEnableVector(EXTI4_15_IRQn, 3);
    nvicEnableVector(LPTIM1_IRQn, 3);
}

static void outputs_off(void) {
    gpio_write_pin_low(B0);
    gpio_write_pin_low(B1);
    gpio_write_pin_low(B2);
    gpio_write_pin_low(B10);
    gpio_write_pin_low(B12);
    gpio_write_pin_low(B13);
    gpio_write_pin_low(B14);
    gpio_write_pin_low(B15);
    gpio_write_pin_low(C6);
    gpio_write_pin_low(C7);
    gpio_write_pin_low(C8);
    gpio_write_pin_low(C14);
    ADC1->CR &= ~ADC_CR_ADEN;
    SPI1->CR1 &= ~SPI_CR1_SPE;
}

static void enter_stop_once(void) {
    PWR->CR |= PWR_CR_CWUF;
    PWR->CR = (PWR->CR & ~PWR_CR_PDDS) | PWR_CR_LPSDSR;
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
}

/* Never returns: STOP until the board should power on, then software-reset. */
static void enter_off(void) {
    led_set_mode(LED_MODE_OFF);
    gpio_write_pin_high(A9); /* orange while shutting down (auto-sleep too) */

    nrf_spi_bt_idle();
    for (uint16_t i = 0; i < OFF_ORANGE_MS; i++) {
        wait_ms(1); /* keep orange on: wait for the link to idle, but at least OFF_ORANGE_MS */
        if ((i & 0x1FF) == 0) {
            iwdg_refresh();
        }
    }

    __disable_irq();
    nvic_for_stop();
    lptim_start();
    exti_wake_lines();
    outputs_off();
    gpio_write_pin_low(A8);
    gpio_write_pin_low(A9);

    for (;;) {
        iwdg_refresh();
        EXTI->PR      = (1u << 5) | (1u << 13) | (1u << EXTI_LINE_LPTIM1);
        LPTIM1->ICR   = LPTIM_ICR_ARRMCF;
        NVIC->ICPR[0] = 0xFFFFFFFFu;

        enter_stop_once();

        if (!gpio_read_pin(USB_VBUS_PIN)) {
            NVIC_SystemReset(); /* USB plugged: recover on USB */
        }
        if (!gpio_read_pin(PWR_BUTTON_PIN)) {
            /* Held: once it reaches 1 s, light LED0 (blue) and power on immediately,
               without waiting for release, so "blue on" is the go signal. */
            uint16_t t0  = lptim_cnt();
            uint16_t need = (uint16_t)((LPTIM_HZ * BUTTON_ON_MIN_MS) / 1000u);
            while (!gpio_read_pin(PWR_BUTTON_PIN)) {
                iwdg_refresh();
                if ((uint16_t)(lptim_cnt() - t0) >= need) {
                    gpio_write_pin_high(A8); /* blue: powering on */
                    NVIC_SystemReset();
                }
            }
        }
    }
}

void power_init(void) {
    gpio_set_pin_input(PWR_BUTTON_PIN);
    gpio_set_pin_input(DIP_SW6_PIN);
    inactivity_start = timer_read32();
}

bool power_sw6_on(void) {
    return !gpio_read_pin(DIP_SW6_PIN); /* low = SW6 on */
}

void power_enter_off(void) {
    enter_off();
}

void power_set_short_press_cb(void (*cb)(void)) {
    short_press_cb = cb;
}

void power_save_last_slot(uint8_t slot) {
    *SLOT_ADDR = SLOT_MAGIC | (slot & 0x0Fu);
}

int8_t power_load_last_slot(void) {
    uint32_t v = *SLOT_ADDR;
    return ((v & 0xFFF0u) == SLOT_MAGIC) ? (int8_t)(v & 0x0Fu) : -1;
}

static void ee_nvm_wait(void) {
    for (uint32_t i = 0; i < 100000u && (FLASH->SR & FLASH_SR_BSY); i++) {
    }
}

/* Write one byte to STM32L0 data EEPROM (auto-erase; neighbours untouched). */
static void sleep_ee_write(uint8_t v) {
    if (*(volatile uint8_t *)SLEEP_EE_ADDR == v) {
        return;
    }
    __disable_irq();
    if (FLASH->PECR & FLASH_PECR_PELOCK) {
        FLASH->PEKEYR = NVM_PEKEY1;
        FLASH->PEKEYR = NVM_PEKEY2;
    }
    ee_nvm_wait();
    FLASH->SR = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_OPTVERR | FLASH_SR_RDERR | FLASH_SR_NOTZEROERR | FLASH_SR_FWWERR;
    *(volatile uint8_t *)SLEEP_EE_ADDR = v;
    ee_nvm_wait();
    FLASH->PECR |= FLASH_PECR_PELOCK;
    __enable_irq();
}

void power_save_jis_mode(bool on) {
    uint8_t v = *(volatile uint8_t *)SLEEP_EE_ADDR;
    if (v == 0 || v > 60) {
        v = 30; /* stock default if the byte is out of the valid 1..60 range */
    }
    if (on) {
        v = (v == 60) ? 59u : (uint8_t)(v | 1u);   /* keep it valid, set flag */
    } else {
        v = (v == 59) ? 60u : (uint8_t)(v & ~1u);  /* keep it valid, clear flag */
    }
    sleep_ee_write(v);
}

int8_t power_load_jis_mode(void) {
    uint8_t v = *(volatile uint8_t *)SLEEP_EE_ADDR;
    if (v == 0 || v == 0xFFu || v > 60) {
        return -1; /* uninitialised / invalid: default off */
    }
    return (int8_t)(v & 1u);
}

void power_note_activity(void) {
    inactivity_start = timer_read32();
}

void power_set_autosleep_timeout(uint32_t ms) {
    autosleep_timeout_ms = ms;
    inactivity_start     = timer_read32(); /* count the new timeout from now */
}

void power_task(bool vbus) {
#ifdef HHKB_LOW_POWER
    clock_task(vbus);
#endif
    if (vbus) {
        button_was_down  = false;
        inactivity_start = timer_read32();
        return;
    }

    bool down = !gpio_read_pin(PWR_BUTTON_PIN);
    if (down && !button_was_down) {
        button_down_start = timer_read();
    }
    if (down && timer_elapsed(button_down_start) >= BUTTON_OFF_HOLD_MS) {
        gpio_write_pin_high(A9); /* orange: powering off (visible until STOP) */
        enter_off();
    }
    if (!down && button_was_down) {
        uint16_t held = timer_elapsed(button_down_start);
        if (held >= BUTTON_SHORT_MIN_MS && held < BUTTON_OFF_HOLD_MS && short_press_cb) {
            short_press_cb();
        }
    }
    button_was_down = down;

    /* SW6 OFF (PC4 high) enables auto-sleep; SW6 ON (PC4 low) disables it. */
    bool sw6_on = !gpio_read_pin(DIP_SW6_PIN);
    if (!sw6_on && timer_elapsed32(inactivity_start) >= autosleep_timeout_ms) {
        enter_off();
    }
}
