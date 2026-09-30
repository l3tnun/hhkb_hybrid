# SPDX-License-Identifier: MIT

# STM32L072RBT6 (not in QMK's MCU list; same settings as Duncaen's HHKB Classic port)
MCU_FAMILY = STM32
MCU_SERIES = STM32L0xx
MCU_STARTUP = stm32l0xx
MCU = cortex-m0plus
ARMV = 6

# Application at 0x08010000 (64KiB window written by the stock update)
MCU_LDSCRIPT = STM32L072xB_hhkb

# The PFU bootloader does not set VTOR before jumping to the application
ADEFS += -DCRT0_VTOR_INIT=1

# Custom capacitive matrix (ADC based) - see matrix.c
CUSTOM_MATRIX = lite
SRC += matrix.c

# nRF52832 Bluetooth module link (SPI1 slave)
SRC += nrf_spi.c
SRC += led_indicator.c
SRC += power.c

# Battery low-power run (4 MHz clock, scan every HHKB_LP_SCAN_INTERVAL_MS with
# WFI sleep in between, USB stopped, OpAmp and ADC off between scans). Enabled
# by default. Built with =no the firmware is the same as before this option.
# Disable with: ./tools/build.sh <keymap> -e HHKB_LOW_POWER=no
HHKB_LOW_POWER ?= yes
ifeq ($(strip $(HHKB_LOW_POWER)), yes)
    OPT_DEFS += -DHHKB_LOW_POWER
endif

# JIS-on-US correction (Ctrl+Alt+Shift+J). Enabled by default.
# Disable with: ./tools/build.sh <keymap> -e HHKB_JIS_US_TOGGLE=no
HHKB_JIS_US_TOGGLE ?= yes
ifeq ($(strip $(HHKB_JIS_US_TOGGLE)), yes)
    OPT_DEFS += -DHHKB_JIS_US_TOGGLE
endif
