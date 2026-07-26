# OSUpad STM32F103C6 USB probe

This is a deliberately small QMK USB-HID build for a board selected in the
Arduino IDE as **Generic STM32F103C6/fake STM32F103C8**. It is a hardware
compatibility probe, not the final VIA firmware.

It reserves the first 8 KiB of flash for the existing STM32duino bootloader
and links the QMK application at `0x08002000`. Its linker limits are exactly
32 KiB flash and 10 KiB RAM, matching QMK's `STM32F103x6_stm32duino` profile.

The probe intentionally has no VIA, RGB, mouse keys, NKRO, or EEPROM feature.
If it enumerates as a USB keyboard, the clone and its USB path are compatible
with QMK; the next step is to measure which OSUpad features fit in the C6
memory budget.

Build with:

```sh
qmk compile -kb osupad_c6_probe -km default -e 'EXTRAFLAGS=-DCRT0_CONTROL_INIT=0'
```

Program `osupad_c6_probe_default.bin` with the existing STM32duino bootloader
(or program it at `0x08002000` through ST-Link). Do not program it at
`0x08000000`, because that would overwrite the bootloader.
