# About the reverse-engineering notes

> English translation of [../../reverse-engineering/README.md](../../reverse-engineering/README.md). If the two differ, the Japanese version is authoritative.

This folder contains the reverse-engineering notes on the stock firmware and the PCB. They are kept as the basis for the port.

- These are not usage instructions. For the build and flashing procedures, see the docs starting from [../quickstart.md](../quickstart.md).
- The intermediate artifacts used for the analysis (dumps of the stock firmware, code disassembly output, working folders and so on; `hhkb_stock_128k.bin`, `build/...` etc. in the text) are not published, because they contain the stock firmware and binaries originating from PFU.
- Labels such as "B-4a" in the text indicate stages of the porting work and have nothing to do with the procedures.
- The confidence of each statement is indicated per document with [confirmed] / [inferred] and the like.

| File | Contents |
|---|---|
| [stm32-analysis.md](stm32-analysis.md) | Analysis of the STM32 (MCU) side. How the EEPROM is used, and reading and writing the stock keymap |
| [implementation.md](implementation.md) | The HFB format, the HID update protocol (E0–E3, and the QMK-specific F0), and USB errors in the update mode |
| [bluetooth-protocol.md](bluetooth-protocol.md) | SPI communication between the STM32 and the nRF52832 (Bluetooth) |
| [nrf-image.md](nrf-image.md) | Analysis of the nRF firmware contained in the HFB |
| [power-management.md](power-management.md) | Power button, sleep and battery; the operating clock and key scan on battery power and on USB power (the basis for QMK's power-saving behavior) |
| [led-indication.md](led-indication.md) | LED indication of the stock firmware |
