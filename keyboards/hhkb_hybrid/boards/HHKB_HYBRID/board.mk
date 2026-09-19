# List of all the board related files.
BOARDSRC = $(BOARD_PATH)/boards/HHKB_HYBRID/board.c

# Required include directories
BOARDINC = $(BOARD_PATH)/boards/HHKB_HYBRID

# Shared variables
ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
