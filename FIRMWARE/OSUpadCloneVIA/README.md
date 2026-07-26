# OSUpad Clone VIA (Arduino)

This firmware keeps the proven STM32duino/libmaple USB path and exposes two
USB HID interfaces: a keyboard and a dedicated 32-byte vendor Raw HID
interface for VIA Web. The dedicated interface has QMK's expected usage page
`0xFF60`, usage `0x61`, and no report ID.

It provides four editable layers, 16 text macros, and eight WS2812 LEDs on
PA5. The firmware reports VIA protocol v13 and `via-definition.json` is a VIA
V3 draft definition. Keep **Use V2 definitions (deprecated)** disabled in
VIA's **Design** tab. Automatic listing requires a separate upstream VIA
registry submission.

Keymaps, macros, and RGB state are persistent. They are stored redundantly in
the final two 1 KiB pages of the 32 KiB C6 application range (`0x08007800` and
`0x08007C00`) with a versioned CRC32 record. Writes are coalesced for 750 ms;
wait one second after editing before unplugging. No boot strap, option byte,
read-out-protection, or bootloader area is modified.

See `RELEASE_QA.md` for automated checks and the production acceptance test
list.
