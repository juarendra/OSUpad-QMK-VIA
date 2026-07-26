# OSUpad STM32F103C6 ST-Link USB probe

This is the direct-SWD counterpart of `osupad_c6_probe`. Use it when the
currently installed program starts at `0x08000000`, so no STM32duino
bootloader occupies the first 8 KiB of flash.

It is linked for exactly 32 KiB flash and 10 KiB RAM and is deliberately
minimal: six ordinary keys, no VIA, RGB, mouse keys, NKRO, or EEPROM.

The diagnostic reserves a 6 KiB main/interrupt stack. It is deliberately
larger than QMK's normal 1 KiB exception stack because this clone runs QMK
with `CRT0_CONTROL_INIT=0` and shares that stack with USB control transfers.

Program the generated binary with ST-Link at address `0x08000000`. It replaces
the currently running Arduino sketch. The pre-flash image should be backed up
before programming.
