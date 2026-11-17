# Analysis of the nRF firmware image built into the HHKB Hybrid

> English translation of [../../reverse-engineering/nrf-image.md](../../reverse-engineering/nrf-image.md). If the two differ, the Japanese version is authoritative.

This document records the analysis of the Bluetooth (nRF) firmware image contained in the latter half of `HHKB800_FW_A048.hfb`, the stock firmware of the HHKB Professional HYBRID US layout.

## Conclusion

The Bluetooth part of the HHKB Hybrid has been confirmed to be the **Taiyo Yuden EYSHCNZWZ module (Nordic nRF52832, with built-in 512KB Flash / 64KB RAM)**.

- Main (USB/matrix): STM32L072RBT6 (Cortex-M0+, 64KiB application starting at 0x08010000 in HFB A048. Confirmed by the chip marking. The earlier statement "Cortex-M3 / F1 family" was wrong)
- Bluetooth: **nRF52832 (EYSHCNZWZ)**, SoftDevice-based, application base `0x00026000`
- Module certification: FCC ID `RYYEYSHCN`, Japanese construction design certification `001-A10745`, IC `4389B-EYSHCN` (from the Taiyo Yuden datasheet)

### Basis for confirming the part number

| Source | Contents |
|---|---|
| PCB photo (provided by the user) | Silver shielded module. `EYSHCN`-style marking on it (read by eye it also looked like `MDBT42Q`/`RAYTAC`, but it is consistent with the datasheet's marking specification, "the product name, lot number, Radio Act ID and company name are printed on the shield case") |
| FCC ID `RYYEYSHCN` | Grantee: TAIYO YUDEN, Bluetooth Smart / ANT Module, model EYSHCN, 2402-2480MHz |
| Taiyo Yuden datasheet `EYSHCNZWZ` (V1.4, 2019-06-06) | Chip: **Nordic nRF52832 (512kB Flash, 64kB RAM)**, Bluetooth 5.0 LE, 49-pin LGA, built-in 32MHz + 32.768kHz |
| Construction design certification `001-A10745` | EYSHCN: 001-A10745 (stated in the datasheet) |
| HFB binary analysis | Consistent with nRF52832 (SP at the end of 64KiB RAM = 0x20010000) + an S132-family SoftDevice |

### About the SoftDevice version

Item g of the general notes in the Taiyo Yuden datasheet says "a fixed SoftDevice (s132_nrf52_5.0.0_softdevice.hex) firmware is written to this product". On the other hand, the binary analysis of the HFB observes the application base `0x00026000` (= equivalent to S132 v6.x). This shows that **PFU updates the SoftDevice from v5.0.0 to the v6 family in a firmware update after shipment (as of A0.48)** (the latter section of the HFB also contains the SoftDevice itself, so the nRF side is also updated via the STM32).

The HFB bundles the images for the two chips in one file, and the PFU HID update (`E0/E1/E2/E3`) is structured so that both can be flashed (the nRF side is inferred to be flashed via the STM32).

## Section layout of the whole HFB

| HFB offset | Size | Contents | Load destination |
|---|---|---|---|
| `0x0000..0x0003` | 4 | File management information (CRC etc.) | - |
| `0x0004..0x10003` | 64KiB | STM32 application | STM32 flash `0x08010000` |
| `0x10004..0x4501f` | 217,116 B | nRF image (SD + application) | Starting at nRF flash `0x00001000` |
| `0x45020..0x4502f` | 16 | `AHUX01` + version footer | - |

In the nRF section, after the first 12 bytes of `0xff` + a 16-byte hash value, the data corresponding to nRF flash `0x00001000` begins.

```text
16-byte hash (a6d96838 b8bfe6cb 45970e39 57706d31)
```

This hash appears to be for verifying the section (equivalent to a CRC or CMAC; the calculation procedure is undetermined).

## Layout inside the nRF section

The offset inside the nRF section is `nrf_offset = hfb_offset - 0x10004`. The correspondence with flash addresses was confirmed to match the following.

```text
flash = 0x00026000 + (nrf_offset - 0x2501C)
```

| nrf offset | flash address | Contents |
|---|---|---|
| `0x1C` | `0x00001000` | SoftDevice vector table (SP=0x200012E0, Reset=0x000243D1) |
| `0x1C..0x23A40` | `0x00001000..0x00024A24` | SoftDevice S132 (many SVCs, peripheral register accesses) |
| `0x23A40..0x2501C` | `0x00024A24..0x00026000` | 0xff (unused/alignment) |
| `0x2501C` | `0x00026000` | **Application vector table** (SP=0x20010000, Reset=0x00026239) |
| `0x2501C..0x309A0` | `0x00026000..0x00031985` | Application code (about 45KiB) |
| `0x309A1..0x3501B` | `0x00031985..0x00036000` | 0xff (unused) |

## Vector tables

### SoftDevice side (flash 0x1000)

```text
SP   = 0x200012E0   (stack at SD startup)
Reset= 0x000243D1
NMI  = 0x00024343
SVC  = 0x00024429
Unused IRQ = 0x00002F19 (the body at 0x2F18 is an empty `bx lr` handler)
```

### Application side (flash 0x26000)

```text
SP   = 0x20010000   (end of the nRF52832's 64KiB RAM = consistent as the initial stack value)
Reset= 0x00026239
NMI  = 0x00026291, HardFault = 0x00026293, ...
IRQ0 = 0x0002859D   (POWER_CLOCK)
IRQ1 = 0x000262A7, ...
```

In the application vector table the exception handlers (0x262xx) and the IRQ handlers are separate; it is a standard SDK-generated application layout.

## Basis for inferring the SoftDevice and the part number

| Observation | Value | Meaning |
|---|---|---|
| Application base | `0x00026000` | SoftDevice size ≈ 152KB → matches the S132 v6.x family |
| Upper limit of RAM literals | `0x20010000` | RAM 64KiB = nRF52832 (it would be 0x20040000 for nRF52840) |
| SVC instructions | 834 places (769 of them with 0xFF) | Mainly SoftDevice API calls = SD-based implementation |
| Peripheral registers | `0x4000F500`(CLOCK), `0x40008000`, `0x40011000`, `0x4001E000` etc. | nRF52-family register map. No reference to `0x50000000`(CRYPTOCELL) → no evidence of nRF52840 |
| ASCII | ` HHKB-Hybrid`, ` PFU Limited`, ` nRF5x` | Device name / manufacturer name / SDK identifier |

Final confirmation of the exact SoftDevice version (S132 v6.0/v6.1) and of nRF52832/nRF52840 requires the checks in the next section.

## BLE profile information

A GATT initialization sequence was confirmed in the application code.

```text
Around 0x000243C2: adds the GAP Service (0x1800)
                   sets Device Name (0x2A00) / Appearance (0x2A01)
```

- BLE device name: `HHKB-Hybrid`
- Manufacturer string: `PFU Limited`
- A string originating from the SoftDevice's `nRF5x` (S132 family) SDK

## Remaining tasks

The part number has been confirmed as EYSHCNZWZ (nRF52832). What remains is:

1. **Reverse engineering of the STM32↔nRF communication protocol**: the application IRQ handlers (0x262A7 etc.) and `0x2BA11` etc. are likely to be the receive handling for STM32 communication. Cross-check with the USART/SPI driver on the STM32 firmware side.
2. **Confirming the SoftDevice version**: which revision of the S132 v6 family the SD in the HFB is. According to the datasheet, it is fixed at v5.0.0 at shipment.
3. **nRF-side firmware update flow**: working out how the STM32 writes to the nRF flash (SWD/SPI/UART) in the HID update of the HFB. The EYSHCNZWZ has SWDCLK/SWDIO pins, so rewriting via the pads on the module side is also possible.

## How to reproduce the analysis

`/tmp/HHKB800_FW_A048.hfb` was obtained with the following command.

```bash
curl -sL https://origin.pfultd.com/downloads/hhkb/HHKB800_FW_A048.hfb -o HHKB800_FW_A048.hfb
sha256sum HHKB800_FW_A048.hfb   # 635b995eb5a15aa50c2cedc60da7d8998fcca8285a09d41d984c07fffcaf9d43
```

```python
from pathlib import Path
import struct
data = Path('HHKB800_FW_A048.hfb').read_bytes()
app = data[4:4+0x10000]          # STM32
nrf = data[0x10004:0x10004+0x3501C]  # nRF (SD+app)
# vector tables
struct.unpack_from('<II', nrf, 0x1C)      # SD: SP/Reset
struct.unpack_from('<II', nrf, 0x2501C)   # App: SP/Reset
```
