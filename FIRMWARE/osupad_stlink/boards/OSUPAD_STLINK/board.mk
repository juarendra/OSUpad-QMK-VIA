# Board support for direct ST-Link programming. BOARD_PATH is the keyboard
# directory when a board profile is supplied by a keyboard.
BOARDSRC = $(BOARD_PATH)/boards/$(BOARD)/board.c
BOARDINC = $(BOARD_PATH)/boards/$(BOARD)

ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
