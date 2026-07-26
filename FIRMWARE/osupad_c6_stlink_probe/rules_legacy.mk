# QMK 0.16.9 does not read the processor/board metadata from keyboard.json.
# Keep this compatibility file separate from the modern QMK build settings.
MCU = STM32F103
BOARD = OSUPAD_C6_STLINK
PLATFORM = ChibiOS
PROTOCOL = ChibiOS
BOOTLOADER = stm32duino
MCU_LDSCRIPT = STM32F103x6_direct
EEPROM_DRIVER = transient
