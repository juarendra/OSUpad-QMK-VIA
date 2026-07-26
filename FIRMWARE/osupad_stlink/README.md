# OSUpad ST-Link firmware

This is the OSUpad QMK firmware variant for direct SWD programming with an
ST-Link. Its QMK metadata links the application to flash address `0x08000000`.
It uses QMK's `custom` bootloader mode, so no USB bootloader is included or
entered by the firmware.

## Build

Copy this `osupad_stlink` directory to `qmk_firmware/keyboards/`, then build:

```sh
qmk compile -kb osupad_stlink -km via
```

Do not use `FIRMWARE/osupad_via.bin` from the repository for this target: that
binary is linked for the STM32duino bootloader and starts at `0x08002000`.

## Flash with STM32 ST-LINK Utility

1. Connect SWDIO, SWCLK, GND, and 3.3 V from the ST-Link.
2. Open the binary built for `osupad_stlink`.
3. Program it at address `0x08000000`.
4. Reset the MCU.

There is intentionally no USB bootloader in this variant. Every future update
must be flashed through ST-Link (or the STM32F103 ROM UART bootloader).

## Clone diagnostic

The workflow also produces `osupad-stlink-clone-diagnostic`, a conservative
build with LTO and NKRO disabled. It is the first USB compatibility test for
F103-compatible clones; it is flashed at the same `0x08000000` address.

## Main-stack clone diagnostic

The `osupad-stlink-clone-msp-diagnostic` artifact also disables LTO and NKRO,
then starts QMK with the Cortex-M main stack (MSP) rather than the usual
process stack (PSP). It is a diagnostic for a chip that enters a HardFault
straight after reset. Flash it through ST-Link at `0x08000000`; it does not
change the bootloader or option-byte settings.
