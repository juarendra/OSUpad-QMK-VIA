# Direct SWD target: no user bootloader is reserved at the start of flash.
MCU_LDSCRIPT = STM32F103x6_direct
EEPROM_DRIVER = transient

# This clone runs with CRT0_CONTROL_INIT=0, so normal code and USB interrupts
# share the Cortex-M main stack. QMK's default main stack is only 1 KiB,
# insufficient for a reliable USB control transfer on this target.
USE_EXCEPTIONS_STACKSIZE = 0x1800
USE_PROCESS_STACKSIZE = 0x100
