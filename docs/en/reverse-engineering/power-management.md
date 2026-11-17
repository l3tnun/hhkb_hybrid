# Analysis of the power management of the stock firmware (power button, sleep)

> English translation of [../../reverse-engineering/power-management.md](../../reverse-engineering/power-management.md). If the two differ, the Japanese version is authoritative.

- Created: 2026-09-13 (static analysis)
- Target: stock A0.48 STM32 application (`hhkb_stock_128k.bin` from 0x08010000). Static analysis only
- Intermediate artifacts: `build/b4/power/` (not tracked by Git)
- [C] = confirmed in code, [I] = inferred

## 1. Overall picture

- There is no latch/regulator control pin [I high]. "Power off" and "inactivity sleep" are the same state (state 0)
- In state 0 it puts the nRF into idle (`40` → `10 00` in reply to 03, waits up to 3 seconds for `83 01`), drives the matrix/analog outputs Low, and puts the MCU into **STOP** (PWR_CR LPSDSR=1, PDDS=0, SLEEPDEEP, WFI;) [C]
- The wake-up sources are **only the falling edges** of **EXTI5 (PC5 = power button)** and **EXTI13 (PC13 = USB power)** (HAL mode 0x10210000) [C]. Keys do not wake it. RTC/LPTIM/IWDG are not used [C]
- On USB power it does not enter STOP (it handles SPI in a busy loop), and it does not sleep on USB suspend either [C]

## 2. Power button (PC5)

| Situation | Operation | Behavior | Basis |
|---|---|---|---|
| Running | Short press (0.1–2 seconds) | In state 3 (connected), LED0 blinks as many times as the connected slot number [I]; in state 5, reconnect |〜 [C] |
| Running | 2-second long press | State 0 (off). The same on USB power | and others [C] |
| Off (STOP) | Pressed for 1 second or more and released in under 10 seconds | Wake-up: PC14 High, battery check, LED0 off, BT reconnect (state 4) or USB (0x3C) | [C] |
| Off | Under 1 second | Returns to STOP | [C] |
| Off | 10 seconds or more | Lights LED1 for 3 seconds and stays off | [C] |

Procedure for going off: LED0 off / LED1 on → nRF idle → (on battery) stop USB (USB_CNTR=0, release the D+ pull-up) → wait for `83 01` (3 seconds) → LED1 off → wait until the button is released → drive PB0–2, PB10–15, PC6, PC7, PC8, PC14 Low → STOP loop.

On wake-up: select the clock by PC13 (battery 4MHz / USB 16MHz + HSI48 and CRS. §7-1) → re-initialize the ADC, timers, SPI1, NVIC (SPI1, EXTI4_15) and USB (only on USB power).

## 3. Inactivity sleep

- Counter `0x2000009C` (seconds). It advances only in state 3/5, with no VBUS and with the equivalent of DIP SW6 (`0x20000086[5]` = PC4 Low) OFF. It is reset by a key press [C]
- Upper limit = `0x20000079` × 60 seconds → state 0. The setting is in **EEPROM `0x08080520`** (1–60 minutes; if 0 or over 60, 30 is stored) [C]
- Other timeouts: waiting for pairing 600 seconds, state 2 600 seconds, reconnection 60 seconds → state 0 (state 5 if VBUS/SW6) [C]
- DIP SW6 (PC4) ON: does not STOP, keeps scanning keys in state 5, and reconnects on a key [C]. SW6 = "wake on key" [I medium-high]
- QMK's power OFF also sends the idle request and waits up to 3 seconds for `83 01` (disconnection) and for the request to be read before entering STOP (previously it only waited 1.5 seconds after a single pulse, and sometimes the link was not disconnected). After that, it drives PD2 High and enters STOP with the nRF stopped (a countermeasure for the defect where the nRF keeps the disconnection pending. [bluetooth-protocol.md](bluetooth-protocol.md) §14).
- QMK implements the 60-second reconnection give-up and the equivalent of state 5 when SW6 is ON (nRF idle, LED off, retry on a key / power-button short press). A retry by a key or a power-button short press resets and reconnects (QMK-specific. [bluetooth-protocol.md](bluetooth-protocol.md) §13). It does not imitate the automatic `01 00` when all slots are unregistered. The specification is in [../usage.md](../usage.md)

## 4. Low battery level

- When PB9 is Low (0%), LED1 lights on battery. On a 2-second long press or after 180 seconds: nRF idle, outputs off → **EXTI5 is disabled** (the button does not wake it) → STOP → wakes only on USB insertion → reset [C]

## 5. Plugging and unplugging USB power

- USB insertion during STOP: wakes on PC13, state 0x3C (USB) [C]
- Removal while running: in state 3/5 (USB clock) a system reset, in 0x3D state 0. Removal during STOP does not wake it [C]

## 6. Points to note for QMK (inferred)

- **IWDG**: QMK starts the IWDG (about 7 seconds) immediately after boot, and the IWDG of the STM32L0 does not stop during STOP. A reset on expiry is recorded by the fail-safe in `__early_init`, and three in a row put the keyboard into the **update mode** ([../device-state.md](../device-state.md)) → during STOP too, it has to wake about every 2 seconds with the LSI-driven LPTIM1 (EXTI29) and refresh the IWDG
- `stm32_clock_init()` overwrites RCC APB1ENR / PWR_CR / CCIPR / SYSCFG_CFGR3 → rather than resuming after returning from STOP, **wake-up = software reset** is simpler (the same as the stock reset path). Cost: the bootloader drives PD2 High, so the nRF is reset, and it takes about 10 seconds to reconnect
- TIM21 (ChibiOS's time) stops during STOP. Pending EXTI (button bounce) is cleared before entering
- Leakage countermeasures: PC14/PC7/PC8 Low, PB6 High, stop the USB D+ pull-up and HSI48/VREFINT
- SPI cannot be answered during STOP → put the nRF into idle beforehand (because the nRF retries an unanswered query up to 600 times)
- The sleep state is not retained (power-cycling = normal boot), so removing and reinserting the batteries always recovers

## 7. Power while on battery power (connected) and QMK's low-power operation

- Static analysis: 2026-09-29. The target is the same stock A0.48 application as in §1. Addresses are values in flash
- QMK implementation: `keyboards/hhkb_hybrid/power.c` (`HHKB_LOW_POWER`), `matrix.c`. The user-facing description is in [../usage.md](../usage.md#low-power-operation-on-battery-power)

### 7-1. Stock clocks [C]

At boot (`0x0801C454`) and on wake-up from STOP (`0x08011AC6`) it reads PC13 and selects the clock.

| | Battery (PC13 High) `0x08015CF4` | USB power (PC13 Low) `0x08015C1C` |
|---|---|---|
| SYSCLK | HSI16 ÷4 (HSIDIV) → PLL ×4 ÷4 = **4 MHz** | **HSI16 direct = 16 MHz** |
| Voltage range (PWR_CR VOS) | Range 1 | Range 1 |
| HSI48 / CRS | Not touched | Uses HSI48 for USB and synchronizes CRS to USB SOF |
| TIM21 (1 ms tick) | PSC 99, ARR 40 (about 1.03 ms) | PSC 99, ARR 160 (about 1.01 ms) |
| USB initialization | Not done (the battery initialization from `0x0801C570` onwards has no USB) | Done (`0x08015360`) |

Flag `0x20000045` = 1 is the battery clock. The "battery 4MHz" of §2 is as in this table.

### 7-2. Stock main loop and key scan [C]

- **The only function that uses WFI is the one that enters STOP (`0x08012D84`)**. Even while connected on battery power (state 3), the CPU keeps running at 4 MHz (Sleep mode is not used).
- The TIM21 interrupt (`0x08013F7C`) increments a counter every 1 ms. `0x08014EA4`, called from the main loop, starts a full key scan each time it has counted **11 of these ticks on battery, 16 on USB** (about every 11 ms on battery).
- The full key scan goes one key at a time (`0x08014C94`, key number = column × 8 + row, invalid numbers are skipped using table `0x0801DB68`). Procedure for one key:
  1. `0x0801C5F4`: **drive C7 (OpAmp) High**, select U1/U2 with C6/B15, and select the column. At the start of a column it waits briefly (one empty-loop iteration on battery). Drive C8 (discharge) High and wait briefly.
  2. `0x0801C2AC`: 15 empty-loop iterations → C8 Low → rows (B0/B1/B2/B10/B11) High → target row Low → wait (3 empty-loop iterations on battery, 35 on USB) → one ADC conversion → rows back to High.
  3. If a press is detected, wait about 1 ms (250 empty-loop iterations × 3 on battery), read again and confirm.
  4. **Drive C7 (OpAmp) Low** (`0x08014E16`).
- C7 is switched ON/OFF for every key.
- **C14 (power of the switch board) is driven High by `0x08014EA4` every time and is always ON**.
- The ADC is enabled (ADEN) once at boot and kept in use as it is (`0x08019550`). The configuration is synchronous clock (PCLK ÷1), 12 bits, sampling 1.5 cycles (`0x08015058`). On battery it converts at 4 MHz.

### 7-3. SPI, PB6 and battery level notification

- The PB6 request pulse in the stock firmware is per key change ([bluetooth-protocol.md](bluetooth-protocol.md) §5). QMK sends one per key report, with retransmissions at 30 ms intervals up to 5 times. This was not changed this time.
- QMK sends the battery level only on connection and when it changes (when the same value is seen 3 times in a row). The stock transmission frequency is unverified.

### 7-4. QMK's low-power operation (`HHKB_LOW_POWER`, enabled by default)

Before the change, QMK ran at 32 MHz (PLL, Range 1) even on battery power and repeated the full key scan without inserting any waits. The OpAmp (C7) was turned OFF only at the end of a scan, the ADC was always enabled, and USB was left started.

| Item | Before the change | After the change (only on battery power) | Basis |
|---|---|---|---|
| Clock | PLL 32 MHz | HSI16 ÷4 = 4 MHz. PLL, MSI, HSI48 and CRS stopped. Flash wait states and prefetch disabled | 4 MHz, the same as stock (7-1) |
| Scan frequency | Continuous, with no wait | Every 5 ms (`HHKB_LP_SCAN_INTERVAL_MS`). The remaining time is spent in WFI (Sleep) in the ChibiOS idle thread | Stock is about every 11 ms (7-2) |
| OpAmp (C7) | OFF only at the end of a scan (scans are continuous) | ON only during a scan. Waits 30 µs after turning it ON | Stock switches it ON/OFF for every key (7-2) |
| ADC | Always enabled | ADEN before a scan, ADDIS after. The conversion settings (4 MHz, 1.5 cycles) are the same as stock | Stock keeps it always enabled (7-2). It is stopped because it is not used between scans |
| USB | Left started | Stopped (`usbDisconnectBus()` and `usbStop()`) | Stock does not initialize USB on battery (7-1) |
| C14 | Always ON | Not changed | Stock is also always ON |

- The switching is done by `power_task()`. It switches after PC13 has been stable for 50 ms, and does not switch during the first 1 second after boot (QMK starts USB after `keyboard_pre_init`, so lowering the clock right after boot would start USB without the PLL). When USB power is detected it returns to 32 MHz and restarts USB with `restart_usb_driver()`.
- ChibiOS calculates the dividers of TIM21 (system tick, 1 kHz) and TIM3 (`wait_us`, 1 MHz) from the build-time clock (32 MHz). On every switch, the PSC of TIM21 is rewritten (CNT is saved and restored) and `GPTD3.clock` of the GPT driver is rewritten.
- At 4 MHz `wait_us()` (which restarts TIM3 on every call) is heavy, so the short waits of the matrix were made empty loops of at least 3 cycles per iteration (they never become shorter than the requested time). On USB power `wait_us()` is used as before.
- `wait_us(1)` is not used. ChibiOS's `gptPolledDelay()` sets ARR = interval − 1 = 0, and the timer is stopped while ARR is 0, so it never returns (a prototype called this at boot and dropped into the update mode through the IWDG fail-safe).
- For SPI with the nRF at 4 MHz (the nRF is a 125 kHz master), it was verified on real hardware on battery power that Bluetooth input and slot switching work.
- Things that were not adopted:
  - Setting unused pins to analog input: because which pins are unused has not been checked.
  - Lowering the voltage range (Range 2/3): stock also stays at Range 1 (7-1).
  - Stopping Flash during Sleep (FLASH_ACR SLEEP_PD): because it has not been checked on real hardware.
  - ADC auto-off (CFGR1 AUTOFF): because the ADC startup time is added to every conversion, and the time from driving the row to the conversion would differ from stock.
- A build with `HHKB_LOW_POWER=no` is byte-for-byte identical to the firmware before the low-power operation was added.
