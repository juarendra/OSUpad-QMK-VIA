# Release QA — OSUpad Clone VIA

## Automated checks completed

- Arduino build: `stm32duino:STM32F1:genericSTM32F103C6`, ST-Link method,
  72 MHz, size optimization.
- Build size: 21,396 / 32,768 bytes flash; 3,968 / 10,240 bytes RAM.
- VIA definition: validated as `KeyboardDefinitionV3`; VID `0x7877`, PID
  `0x1004`, firmware version `1`, six matrix keys.
- USB topology: Windows enumerates keyboard interface `MI_00` and separate
  QMK Raw HID interface `MI_01` (vendor page `0xFF60`, usage `0x61`).
- Flash operation: ST-Link program and verify at `0x08000000`; option bytes,
  RDP, BOOT0/BOOT1, and bootloader are not modified.

## Production acceptance checklist

Run this on each hardware revision before release:

1. Verify all six switches: `PB0`, `PA7`, `PA6`, `PB12`, `PB13`, `PB14`.
2. In VIA V3, sideload `via-definition.json`, remap a key, wait one second,
   power-cycle, and confirm the map remains.
3. Set Macro 0 to `macro-test`, assign `QK_MACRO_0` to a key, and confirm it
   types `macro-test`; power-cycle and repeat.
4. Change RGB brightness, effect, speed, and color; wait one second,
   power-cycle, and confirm the saved state.
5. Repeat the power-cycle test while editing a macro, then confirm that either
   the previous complete record or the new complete record loads (never a
   partially-corrupted map). The dual-page CRC record is designed for this.
6. Confirm USB reconnects after 100 unplug/replug cycles and keys do not stick
   when held during reconnect.
7. Run a 30-minute key-repeat and RGB-effect soak test; confirm no USB reset,
   missed key release, or unexpected RGB corruption.

## Scope notes

- Macros are VIA's NUL-separated text macro buffer and are invoked by QMK
  keycodes `QK_MACRO_0` through `QK_MACRO_15` (`0x7700`–`0x770F`).
- The reserved settings pages assume the C6-compatible 32 KiB application
  layout. Keep release firmware below `0x08007800`; the current build leaves
  about 9 KiB of headroom before that boundary.
