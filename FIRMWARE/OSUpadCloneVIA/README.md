# OSUpad Clone VIA (Arduino)

This firmware keeps the proven STM32duino/libmaple USB path and exposes two
USB HID interfaces: a keyboard and a dedicated 32-byte vendor Raw HID
interface for VIA Web. The dedicated interface has QMK's expected usage page
`0xFF60`, usage `0x61`, and no report ID.

It provides four editable layers, 16 QMK/VIA dynamic macros (512 bytes total),
eight WS2812 LEDs on PA5, keyboard, eight-button mouse (including horizontal
scroll), consumer/media, and system-control HID reports.
Macros support ordinary ASCII text plus QMK send-string commands for tap,
key-down, key-up, modifiers, and delay. Standard QMK layer actions supported
by the four-layer hardware are `MO`, `LM`, `LT`, `TT`, `TO`, `TG`, `DF`,
`PDF`, `OSL`, and `OSM`. QMK Mouse Keys cursor movement, all eight mouse
buttons, vertical/horizontal scrolling, and acceleration levels are supported.
Core QMK system controls (`KC_PWR`, `KC_SLEP`, `KC_WAKE`) and standard
consumer controls are supported. Unicode, MIDI, audio, steno, tap dance, and
keyboard-specific QMK extensions remain outside this Arduino implementation.
The firmware reports VIA protocol v13 and `via-definition.json` is a VIA V3
draft definition. Keep **Use V2 definitions (deprecated)** disabled in VIA's
**Design** tab. Automatic listing requires a separate upstream VIA registry
submission.

Keymaps, macros, RGB state, and persistent default layer are stored redundantly
in the final two 1 KiB pages of the 32 KiB C6 application range (`0x08007800`
and `0x08007C00`) with a versioned CRC32 record. Existing 192-byte macro
records migrate automatically to the 512-byte format. Previous clone RGB mode
numbers migrate automatically to QMK RGBLight-compatible mode IDs. Writes are
coalesced for 750 ms; wait one second after editing before unplugging. No boot
strap, option byte, read-out-protection, or bootloader area is modified.

See `FLASH_STLINK.md` for the direct-SWD flashing and recovery procedure, and
`RELEASE_QA.md` for the automated checks and production acceptance test list.
