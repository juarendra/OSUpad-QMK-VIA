# OSUpad Clone VIA (Arduino)

This firmware keeps the proven STM32duino/libmaple USB path and exposes two
USB HID interfaces: a keyboard and a dedicated 32-byte vendor Raw HID
interface for VIA Web. The dedicated interface has QMK's expected usage page
`0xFF60`, usage `0x61`, and no report ID.

It provides four editable layers, 16 text macros, and eight WS2812 LEDs on
PA5. Use `via-definition.json` with VIA Web's **Design** tab to sideload the
definition; automatic listing requires a separate upstream VIA registry
submission.

The first build is intentionally RAM-backed. VIA changes work immediately but
are reset after power loss. Flash-backed storage is kept as a separate follow-
up because it must reserve two known-safe flash pages on clone hardware.
