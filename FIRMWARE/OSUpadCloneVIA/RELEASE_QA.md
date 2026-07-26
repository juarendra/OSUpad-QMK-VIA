# Release QA — OSUpad Clone VIA

## Automated checks completed

- Arduino build: `stm32duino:STM32F1:genericSTM32F103C6`, ST-Link method,
  72 MHz, size optimization.
- Latest local build size: 24,924 / 32,768 bytes flash; 4,616 / 10,240 bytes
  RAM. The GitHub Actions job rejects any binary above 30,720 bytes, preserving
  the final two 1 KiB settings pages.
- VIA definition: validated as `KeyboardDefinitionV3`; VID `0x7877`, PID
  `0x1004`, firmware version `1`, six matrix keys.
- USB topology: Windows enumerates keyboard interface `MI_00` and separate
  QMK Raw HID interface `MI_01` (vendor page `0xFF60`, usage `0x61`).
- Flash operation: ST-Link program and verify at `0x08000000`; option bytes,
  RDP, BOOT0/BOOT1, and bootloader are not modified. See `FLASH_STLINK.md`.

## Production acceptance checklist

Run this on each hardware revision before release:

1. Verify all six switches: `PB0`, `PA7`, `PA6`, `PB12`, `PB13`, `PB14`.
2. In VIA V3, sideload `via-definition.json`, remap a key, wait one second,
   power-cycle, and confirm the map remains.
3. Set Macro 0 to `macro-test`, assign `QK_MACRO_0` to a key, and confirm it
   types `macro-test`; power-cycle and repeat. Confirm the VIA macro panel
   reports 16 macros and 512 bytes of storage.
4. In the VIA macro editor, verify `{+KC_LCTL}{KC_C}{-KC_LCTL}`, `{KC_ENT}`
   and `{300}`. Confirm each action occurs in order and no modifier remains
   held afterward.
5. Assign and test `MO(1)`, `TG(1)`, `LT(1, KC_A)`, `LCTL_T(KC_ESC)`,
   `OSL(1)`, and `PDF(1)` through VIA. Verify transparent keys fall through
   to the lower layer and `PDF` survives a power cycle.
6. Assign volume up/down, play/pause, next/previous track, browser controls,
   and `KC_PWR`/`KC_SLEP`/`KC_WAKE`. Verify Windows reports consumer-control
   and system-control HID functions. Test power-related keycodes only on a
   non-critical machine because the operating system may sleep or power off.
7. Assign and hold cursor movement, vertical/horizontal wheel, Mouse Buttons
   1 through 8, and acceleration levels 0 through 2. Verify motion repeats at
   the selected speed and all button releases are clean.
8. Verify `RGB_TOG`, RGB hue/saturation/value/speed controls, plus QMK
   RGBLight effect IDs 1 through 42. Mode 0 must turn all LEDs off.
9. Change RGB brightness, effect, speed, and color; wait one second,
   power-cycle, and confirm the saved state.
10. Repeat the power-cycle test while editing a macro, then confirm that either
   the previous complete record or the new complete record loads (never a
   partially-corrupted map). The dual-page CRC record is designed for this.
11. Confirm USB reconnects after 100 unplug/replug cycles and keys do not stick
   when held during reconnect.
12. Run a 30-minute key-repeat and RGB-effect soak test; confirm no USB reset,
    missed key release, or unexpected RGB corruption.
13. Update the firmware using page erase (not Mass Erase), then power-cycle and
    confirm the saved keymap, macro, RGB state, and persistent default layer
    remain. Separately verify that a deliberate Mass Erase resets all of them.

## Scope notes

- Macros are VIA's NUL-separated QMK send-string buffer and are invoked by
  QMK keycodes `QK_MACRO_0` through `QK_MACRO_15` (`0x7700`–`0x770F`). The
  firmware implements QMK's text, tap, down, up, and delay bytecode, including
  keyboard, common consumer/media, and basic mouse usages. Unicode, MIDI,
  audio, steno, and user-defined QMK keycodes are intentionally unsupported.
- The reserved settings pages assume the C6-compatible 32 KiB application
  layout. Keep release firmware below `0x08007800`; the current build leaves
  about 9 KiB of headroom before that boundary.
