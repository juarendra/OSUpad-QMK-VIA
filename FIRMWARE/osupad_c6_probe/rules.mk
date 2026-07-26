# The normal STM32duino target selects the 128 KiB/20 KiB F103xB linker.
# This board is deliberately constrained to the proven C6 memory layout.
OPT_DEFS += -DBOOTLOADER_STM32DUINO
MCU_LDSCRIPT = STM32F103x6_stm32duino
BOOTLOADER_TYPE = stm32duino
DFU_ARGS = -d 1EAF:0003 -a 2 -R
DFU_SUFFIX_ARGS = -v 1EAF -p 0003
EEPROM_DRIVER = transient
