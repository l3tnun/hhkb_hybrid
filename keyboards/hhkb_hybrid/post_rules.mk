# SPDX-License-Identifier: MIT

# EEPROM restricted to the stock keymap table B window (eeprom_hhkb.c), used by
# the via keymap. post_rules.mk runs after the keymap's rules.mk, so the final
# EEPROM_DRIVER is visible here.
ifeq ($(strip $(EEPROM_DRIVER)), custom)
    SRC += eeprom_hhkb.c
endif
