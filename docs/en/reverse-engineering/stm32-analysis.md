# STM32-side reverse-engineering notes

> English translation of [../../reverse-engineering/stm32-analysis.md](../../reverse-engineering/stm32-analysis.md). If the two differ, the Japanese version is authoritative.

A record of the static analysis of the STM32 application part (`[4:4+0x10000]`, starting at flash `0x08010000`) of the HFB `HHKB800_FW_A048.hfb` (sha256=635b995e...) and
of the bootloader area of the full dump from real hardware, `hhkb_stock_128k.bin`. The degree of confidence is stated for each item.

- Created: 2026-09-12
- Revised: 2026-09-13 (corrected the MCU to STM32L072. Corrected or removed statements that assumed the F103. Added EEPROM/calibration)

## 1. MCU: STM32L072RBT6 [confirmed]

- Basis: ① chip marking `STM32L072RBT6` ② L0 GPIO (IOP bus) literals ③ L0 NVM keys ④ references to the EEPROM (`0x0808xxxx`)

| Literal | Meaning on the L0 | Old interpretation (assuming F1, wrong) |
|---|---|---|
| `0x50000400` / `0x50000800` / `0x50000C00` / `0x50001C00` | GPIOB / GPIOC / GPIOD / GPIOH | (treated as not found) |
| `0x40010800` | **TIM21** | GPIOA |
| `0x40011400` | **TIM22** | GPIOD |
| `0x40010000` | SYSCFG | AFIO |
| `0x40010400` | EXTI | — |
| `0x40007000` | PWR | PWR |
| `0x40012400` | ADC1 | ADC1 |
| `0x40013000` | SPI1 | SPI1 |
| `0x40000400` | TIM3 | TIM3 |
| `0x40021000` | RCC | RCC |
| `0x40022000` | FLASH (NVM) | — |
| `0x89ABCDEF` / `0x02030405` | EEPROM (PEKEYR) keys | — |

- The existing patches (assembled with `-mcpu=cortex-m0`) are correct as Cortex-M0+ code
- For the operating clock (battery 4 MHz / USB 16 MHz), the 1 ms tick of TIM21, the key scan period and the per-key procedure (handling of the OpAmp, C14 and the ADC), see [power-management.md](power-management.md) §7

## 2. SPI1 communication [high confidence: the bus. Contents: need measurement]

- Only SPI1 (`0x40013000`) is used. It is used for communication with the nRF (EYSHCNZWZ). **The STM32 is the slave** (confirmed 2026-09-13. Details in `bluetooth-protocol.md`)
- The register layout is the same on the L0 (SR=+0x08, DR=+0x0C, CR2 bit6 = RXNEIE)
- Transmit function:
  ```asm
  push {r4,r5,lr}; cpsid i
  loop: ldr r4, [SPI1+8]          ; SPI1->SR
        ands r4, #2; beq loop     ; wait for TXE (polling)
        ldrb r4, [src, r0]
        str r4, [SPI1+0xC]        ; SPI1->DR
  cpsie i; pop
  ```
  → Blocking, polled 8-bit transmission. No interrupts/DMA
- `SPI1->CR2 |= 0x40` in the initialization is **RXNEIE** (receive interrupt enable). The earlier statement "SSOE" was wrong
- Pins (confirmed): PA15=NSS, PB3=SCK, PB4=MISO, PB5=MOSI (AF0, MspInit). Request line PB6

### 2-1. nRF transmit path [high confidence]

| Item | Address/value | Meaning |
|---|---|---|
| Transmit buffer | RAM `0x200016ba` (0x80B) | Zero-cleared by `0x8015a80` |
| Transmit flag / length | `0x200000ca` / `0x200000cb` | Transmission starts when flag=1 and the length is set |
| Transmit execution | `0x8011146`: `bl 0x8015a98` | r0=buf, r1=len. Being the slave, it places one byte at a time in DR in step with the nRF's clock |
| Frame format | `[0]=0x00, [1]=0xAA, [2]=cmd, [3..]=data` | |
| Keyboard report | Response type `0x01` (0x8010d02-0x8010d28) | Response to the nRF's cmd 02. `00 AA 01 mod k1..k6 00` (11 bytes) |
| Key release family | `cmd=0x02` (0x8010d4e) | Part of the report in `[3]` |
| Others | `0xE5`(0x8010cb0), `0x40`(0x8010dd6), `0x08`(0x8010dfe), `0xE2`/`0xE3` | Meaning undetermined |
| Command-family 4-byte frames | `[0]=0x00/0x04, [1]=0xAA, [2]=0x80/0x40/0x10/0x64, [3]=0x00` | 0x801045a, 0x80105f8, 0x8010516, 0x80104ae etc. Meaning undetermined |

The stock firmware shares the same HID report between USB and BT and forwards it to the nRF over SPI.
The L0 version implements it as an SPI1 slave (`keyboards/hhkb_hybrid/nrf_spi.c`).

The release-only command (`cmd=0x02`) that the stock firmware has is also the basis for the processing that sends a key release to the old host before a slot switch (for the known defect and the countermeasure see [bluetooth-protocol.md](bluetooth-protocol.md) §11).

## 3. HID report processing [high confidence]

- `..`: processing that adds a key to the HID report (modifier + 6KRO)
  - `0x8015b8e` = press path (press report hook of the JIS/US toggle patch)
  - `0x8015b04` = release path (release report hook)

## 4. EEPROM and calibration [confirmed]

| Processing | Location | Contents |
|---|---|---|
| Calibration load | | u16 ×128 from `0x08081100` to RAM `0x2000076A` |
| Table write | | `0x08081000` ← RAM `0x2000066A`, `0x08081100` ← RAM `0x2000076A`, `0x08081310` ← 0xAAAA |
| Press detection threshold | | When not pressed `thr = cal[i] + ofs[i]` (cal=RAM `0x2000076A`, ofs=RAM `0x2000086A`); while pressed `thr − 0x32`. When flag `0x20000071` is set, a fixed `0x55F` (while pressed `0x55F − 0xC8`) |
| Decision | | Pressed when `ADC value > thr` |
| ADC read | | Wait for ADSTP → ADSTART → wait for EOC (10ms timeout each) → DR |
| ADC configuration | | Synchronous clock PCLK/1 (CKMODE=11), 12bit |
| EEPROM write HAL | | 32-bit word write; unlocked with , locked with |

- Index `i = col * 8 + row`
- QMK implementation: key input has been verified on hardware with the Duncaen (Classic) method, which reads `ofs` from EEPROM `0x08081200`
- The old record "the calibration is in the bootloader area" was wrong (it is in the EEPROM)

### 4-1. Overall picture of the EEPROM write ranges [confirmed: code disassembly]

The result of enumerating the places where the stock application (A048) and the bootloader write to the EEPROM (2026-09-14).
The write functions are application `0x08012144` and bootloader `0x08002748` (both 32-bit word writes).

| Range | Contents | Written by |
|---|---|---|
| `0x08080000–0x080802FF` | Keymap table A (mode n=0..2, 0x80 ×2 from `0x08080000 + n*0x100`) | Application `0x0801B45C` / bootloader `0x08001FA8` (erase is `0x08001C78`) |
| `0x08080300` | Keymap magic (valid when `0xAAAA`) | Application `0x0801B63C` / bootloader |
| `0x08080310–0x0808034F` / `0x08080350` | Settings table 64B / magic | Application `0x0801B400` / bootloader `0x080021C0` |
| `0x08080360` / `0x08080370` | Boot flag / application CRC | E0 and E3 (see `hardware.md`) |
| `0x08080380–0x0808047F` | BT connection target records, 16 entries × 16B (all erased when full, `0x0801AD98`) | Application `0x0801B148` |
| `0x08080500–0x08080513` | Mode settings, 5 words | Application `0x0801B288` / bootloader `0x08002234` |
| `0x08080520` | Sleep time (minutes) | Application `0x0801B620` |
| `0x08080700–0x08080CFF` | Keymap table B (n=0..2, 0x80 ×4 from `0x08080700 + n*0x200`) | Application `0x0801B57C` only |
| `0x08081000–0x080812FF` | Per-key table / calibration / offset table (the first byte is used for the boot decision) | Application `0x0801B360` `0x0801B500` / bootloader `0x0800BDA8` `0x0800BE8C` |
| `0x08081310` / `0x08081320` | Table-valid magic | Both |
| `0x08081330` / `0x08081338` | Version | Bootloader `0x0800BF04` / application `0x0801B674` |
| `0x08081350` / `0x08081360` | Update request / settings | Both |
| `0x08081400–0x080816FF` | Second half of keymap table A (0x80 ×2 from `0x08081400 + n*0x100`) | Application `0x0801B45C` only |

- The HID update (E0–E3) writes only the boot flag, the CRC and the update request. It does not touch the keymap areas.
- Anything other than the above (`0x08080480–0x080804FF`, `0x08080524–0x080806FF`, `0x08080D00–0x08080FFF`, `0x08081370–0x080813FF`, `0x08081700–0x080817FF`) is not found as a constant reference.

### 4-2. Loading the keymap and writing back the defaults [confirmed: code disassembly / the call timing is inferred]

- `0x0801C744`: tests `0x08080300 == 0xAAAA` with `0x0801B120`.
  - Valid: reads table A (`0x0801B050`) and table B (`0x0801B0CC`) for n=0..2.
  - Invalid: loads the default keymap into RAM (`0x0801D1EC`) and calls `0x0801B848`.
- `0x0801B848`: `0x08080300 ← 0xFFFF` → writes table A and table B → updates `0x08080500` → `0x08080300 ← 0xAAAA`.
- `0x0801C744` is called from three places, `0x0801190C` / `0x0801A9D6` / `0x0801C0FA` (inferred to be at boot and at mode decision. Not verified on hardware).
- In the EEPROM dump of 2026-09-13 (DFU alt=2), `0x08080300 = 0xAAAA`, and `0x08080700–0x08080CFF` and `0x08081400–0x080816FF` are all zero.
- However, the **bootloader** checks the magic before the stock application does (`0x0800C498` → `0x080047A0`). If it is invalid, it loads the keymap into RAM with the defaults, rewrites **only table A and `0x08080500`** with `0x08001FA8`, and sets the magic back to `0xAAAA`. The bootloader does not reference table B. Therefore, merely invalidating the magic leaves the contents of table B to be read as they are by the stock application [confirmed: code disassembly + real hardware].
- QMK's VIA support uses the table B area (`0x08080700–0x08080CFF`) as the EEPROM for VIA. Because of the above, table B must be reset to zero (the stock state at the time of the dump) before returning to stock. It was verified on hardware that key input breaks on the stock firmware if you return with VIA data left in table B ([../via.md](../via.md), [../restore-stock.md](../restore-stock.md)).

## 5. Classic vs Hybrid comparison [high confidence]

The application areas of the HHKB Professional Classic stock firmware (`HHKB410_FW_A429.hfb`) and the Hybrid stock firmware were compared.

| Observation | Hybrid | Classic | Meaning |
|---|---|---|---|
| Column multiplexer constants `0x70000000` / `0x60001000` / `0x00007000` (GPIOB BSRR values) | ×16 / ×16 / ×16 | ×16 / ×16 / ×16 | The column selection circuit is identical |
| `0x40010800` (TIM21) | ×5 | ×4 | Both are L0 |
| `0x40011400` (TIM22) | ×1 | ×1 | Identical |

- User observation (PCB photos): the only difference between the Classic and the Hybrid is the BT module
- Key input has been verified on hardware with Duncaen's matrix implementation (rows B0/B1/B2/B10, columns B12-B14, C6/B15 select, C7 OpAmp, C8 discharge, ADC ch1)

## 6. Bootloader [confirmed]

For the boot decision, E0/E3 and the state at the jump, see `../hardware.md`.

- Reset → (initialization) → → (boot decision)
- The bootloader uses the HAL SysTick (`SysTick_Handler` =), and also uses EXTI (there is a reference to `0x40010400`)
- No access to RCC_CSR (`RCC+0x50`) is found by static scanning (it does not clear the reset cause flags)
- There is no backup of the stock application in the bootloader area (match rate 5–8% = chance level)
- However, the bootloader has **its own keyboard function** [confirmed: code disassembly + real hardware]. It contains the column selection constants of the matrix (`0x70000000` ×32 / `0x60001000` ×30. The same counts as the stock application), initialization of ADC1, SPI1 (nRF) and TIM, and loading of the calibration (`0x08081100` / `0x08081200`).
  - Boot decision (`0x0800d998`): it jumps to the application if all of `0x08080360 == 1`, `0x08081200 == 1` and a CRC match hold. If any of them does not hold, it proceeds to `0x0800b160`.
  - `0x0800b160`: after loading the calibration and initializing SPI1, the ADC and so on, it enters the main loop `0x0800c468`.
  - `0x0800c5c6`: if `0x08081350 == 1` (update request), it sets **`0x08081350←0`** with `0x0800bd58(0)` and enters state 8 (update mode). The other states (0–5, 0x3c, 0x3d) are normal keyboard operation.
  - Therefore the update request is one-shot, and if the MCU is reset after the update mode has been entered, it runs on the built-in keyboard function with the boot flag still invalid (real hardware: power-cycling brought key input and Bluetooth back, with the US layout and shown as `HHKB-Hybrid`).
  - There are only three places where the boot flags and related values in the EEPROM are written: the E0 handling (`0x0800b536`: `0x08080360←0`, `0x08080370←0`, `0x08081350←1`), E3 completion (`0x08000552` / `0x08001740`: the CRC and `0x08080360←1`), and the transition to the update mode (`0x0800c5d0`: `0x08081350←0`).
  - The HID command dispatch (`0x0800b27c`) handles 0–7, `0x86`, `0x87`, `0xD0`, `0xE0`–`0xE7`, `0xF3` and `0xF4` (the stock application also handles, around `0x0801a958`, 0–9, `0x86`, `0x87`, `0xAA`, `0xD0`, `0xE0`–`0xE7`). E1 (`0x0800b572`) does not appear to check the state (whether flashing can be started from E1 while running on the built-in keyboard is unverified).
  - Starting the IWDG and its keys (`0xCCCC` / `0x5555`) are not found. The bootloader does not handle the IWDG. The CRC16 is table-driven (`0x0800b24c`, table `0x0800f280`) and finishes in a short time even for 64 KiB.
  - The initial SP is `0x20002030`. The bootloader does not use RAM above that.

## 7. Verification of DFU recovery on hardware

- Flashed the 64KiB stock application part to `0x08010000` via DFU (`0483:df11`) and checked it with a full dump
  - The bootloader area is unchanged before and after flashing (`d5722341...`)
  - The application area matches the application part of the official HFB (`f9fab50e...`)
- Upload of `0x08020000` and beyond gives `LIBUSB_ERROR_IO`
- Note: raw flashing via DFU does not update the expected CRC in the EEPROM (`0x08080370`). It boots only when that matches the CRC16 of the application written back (see `../recovery.md`)

## 8. Reproducing the analysis

```python
from pathlib import Path
data = Path('HHKB800_FW_A048.hfb').read_bytes()
app = data[4:4 + 0x10000]           # STM32 @ 0x08010000
boot = Path('hhkb_stock_128k.bin').read_bytes()[:0x10000]  # bootloader @ 0x08000000
```

Code disassembly: capstone (`CS_MODE_THUMB | CS_MODE_MCLASS`). PC-relative literals were resolved to identify the peripheral register and EEPROM references.
