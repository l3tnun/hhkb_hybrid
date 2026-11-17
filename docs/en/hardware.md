# Hardware overview

> English translation of [../hardware.md](../hardware.md). If the two differ, the Japanese version is authoritative.

Hardware information based on analysis of real hardware of the HHKB Professional HYBRID (US layout, `PD-KB800` series). For the detailed grounds of the port, see [reverse-engineering/](reverse-engineering).

## MCU

- **STM32L072RBT6** (Cortex-M0+, ARMv6-M)
- Flash 128 KiB:
  - `0x08000000–0x0800FFFF` (64 KiB): PFU bootloader (with the PFU update function). **Do not change it**
  - `0x08010000–0x0801FFFF` (64 KiB): application area. QMK is flashed here
- EEPROM (data EEPROM `0x08080000–0x080817FF`, 6 KB): used by the stock firmware for the boot check, calibration, keymap and settings. QMK writes only to addresses that the stock firmware also writes
  - Moving to the update mode: `0x08080360` / `0x08080370` / `0x08081350`
  - Storing the JIS/US correction: `0x08080520` (the least significant bit of the stock sleep time setting byte)
  - Storing the settings of the VIA-enabled build (`via` keymap): `0x08080700–0x08080CFF` (the stock keymap table B) and the valid mark of the stock keymap, `0x08080300`. Return this area to zeros before restoring the stock firmware ([via.md](via.md), [restore-stock.md](restore-stock.md))
  - Calibration (`0x08081100` / `0x08081200`) is read only
  - Analysis of the ranges written by the stock firmware and the bootloader: [reverse-engineering/stm32-analysis.md](reverse-engineering/stm32-analysis.md)

### Rewrite endurance (values from the STM32L0 datasheet)

Values for the STM32L072RB**T6** (suffix 6 = the −40 to 85°C part). From the STM32L0x2/L0x3 datasheet (tables "Flash memory and data EEPROM characteristics" and "… endurance and retention").

| Item | Value |
|---|---|
| Data EEPROM rewrite cycles | Minimum 100,000 cycles (−40 to 105°C) |
| Program Flash (application area) rewrite cycles | Minimum 10,000 cycles (−40 to 105°C) |
| Data retention | 30 years at 85°C even after the above number of rewrites |
| Erase/write time for 1 word (4 B) | Typical 3.28 ms |

- This is not wear leveling but the guaranteed number of times **the same location can be rewritten** (characterization based on JEDEC JESD22-A117). Both this firmware and the stock firmware overwrite the same addresses.
- This firmware writes to the EEPROM only for: switching the JIS/US correction (the same 1 B at `0x08080520` every time), changing keys and settings in VIA (only the words that changed), resetting the settings and the operation of returning to the stock firmware (the whole VIA area), and moving to the update mode (a few locations of the boot check). **It does not write during normal typing, startup, sleep or Bluetooth connection.**
- As a guide: switching the JIS/US correction 100 times a day reaches the minimum guaranteed count in about 2.7 years, and 10 times a day in about 27 years. Flashing firmware is limited by the 10,000 cycles of the application area (about 27 years even when flashing every day).
- Erasing the whole VIA area (1,536 B = 384 words) takes about 1.3 seconds. This is short enough compared with the IWDG (about 5–11 seconds).

### The bootloader's boot check

The application starts only when all of the following hold:

- EEPROM `0x08080360` == 1
- EEPROM `0x08081200` == 1
- CRC16(app, 0x10000) == EEPROM `0x08080370` (this CRC matches `[2:4]` of the HFB, header64)

A raw write over DFU does not update the CRC, so the application does not start. The HID update path (E0/E1/E2/E3) updates the CRC as well, so it starts.

How the update mode is entered: rewrite the EEPROM (`0x08080360←0`, `0x08080370←0`, `0x08081350←1`) and then `NVIC_SystemReset`. Both QMK and the stock firmware use this method.

Behavior of the bootloader when the boot check fails:

- If `0x08081350 == 1`, it sets `0x08081350←0` and then enters the update mode (keys do nothing).
- Otherwise, it runs with the keyboard function built into the bootloader (key scan, USB HID, communication with the nRF. US layout). It also responds to E0.
- Therefore, if the MCU is reset while in the update mode, it starts with the built-in keyboard, and QMK does not start until you flash firmware again ([device-state.md](device-state.md#when-qmk-no-longer-starts-the-bootloaders-built-in-keyboard)).

## RAM

- 20 KiB (`0x20000000–0x20004FFF`). The initial stack of the PFU bootloader is `0x20002030`, and the RAM above it is preserved even across a restart that goes through the bootloader.
- QMK uses `0x20000000–0x20004FBF`. The top 64 bytes are used for information kept across resets.
  - `0x20004FC0–0x20004FEF`: diagnostic record (`boards/HHKB_HYBRID/hhkb_diag.h`)
  - `0x20004FF8`: the Bluetooth slot used last (`power.c`)
- It is lost when the batteries and USB are removed (power-on reset).

## Key matrix

- Topre capacitive sensing. Read multiplexed onto a single ADC pin (PA1 / ADC ch1)
- Outputs driving the rows and columns: PB0/1/2/10/12/13/14/15, PC6/7/8/14
- **Keys cannot wake the keyboard from the bootloader's STOP** (the matrix is multiplexed onto a single ADC and there is no EXTI). This is the same for the stock firmware

## Bluetooth (nRF52832)

- STM32 = **SPI1 slave** (PA15=NSS, PB3=SCK, PB4=MISO, PB5=MOSI, mode 3, 8bit). The nRF is the master (125 kHz)
- **PB6** = request pulse (Low). **PD2** Low = nRF enabled
- Frame: the STM32 responds with `00 AA <cmd> <data>`. Polling by the nRF: `0x02` (keyboard/consumer), `0x03` (control), `0x05` (battery), `0x06` (slot selection)
- Media keys are sent as a bitmask, not as usage codes
- Details: [reverse-engineering/bluetooth-protocol.md](reverse-engineering/bluetooth-protocol.md)

## Power

- **PC5** (Low=pressed): power button. Long press for 2 seconds to turn off
- **PC13** (Low=on USB power): VBUS detection
- **PC4** (Low=DIP SW6 ON): disable auto-sleep
- Off/sleep is the **STOP mode** of the STM32. Wake-up is only on the falling edge of PC5 (button) / PC13 (USB). LPTIM1 (LSI, about 2 seconds) wakes it periodically to refresh the IWDG
- On wake-up it performs `NVIC_SystemReset` (software reset). This is not an IWDG reset, so it does not fall into the update mode
- Clock while running: 32 MHz on USB power (PLL. Required for USB). 4 MHz on battery power (HSI16 ÷4), with Sleep between key scans (the stock firmware also runs at 4 MHz on battery power. Build option `HHKB_LOW_POWER`, [usage.md](usage.md#low-power-operation-on-battery-power))
- Details: [reverse-engineering/power-management.md](reverse-engineering/power-management.md)

## Battery level

- Not an analog fuel gauge but 4 steps from 3 digital inputs:
  - PB9 Low → 0 %, PB8 Low → 5 %, PB7 Low → 15 %, all High → 100 %
  - Fixed at 100 % on USB power
- The mapping between pins and steps (PB7=15 %, PB8=5 %) is an inference from static analysis (only the 0 % path has been confirmed in the code). On real hardware only 100 % has been observed. For details and the verification status, see [reverse-engineering/power-management.md](reverse-engineering/power-management.md)

## Indicator LEDs

- **PA8** = blue (status), **PA9** = orange (warning). Both active high
- Only one of them is lit at a time, and orange (LED1) has priority. Same as the stock firmware
- Details: [reverse-engineering/led-indication.md](reverse-engineering/led-indication.md)

## Startup notes (how QMK handles them)

- The bootloader jumps with SysTick left pending and IRQs left masked, so `__early_init` clears PENDST/PENDSV and the NVIC
- Vector table relocation: `CRT0_VTOR_INIT=1`
- The IWDG does not stop during STOP, so during STOP the firmware wakes periodically with LPTIM1 and refreshes it
