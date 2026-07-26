# QMK USB investigation: STM32F103 clone

## Observations

- ST-Link detects device ID `0x414` and reports 256 KiB flash; the package
  marking is `STM32F103`, but the chip is treated as an F103-compatible clone.
- A direct-ST-Link STM32duino/libmaple USB HID build enumerates in Windows as
  a HID keyboard. This verifies the board's USB wiring, PA11/PA12 connection,
  USB clock source, and host cable.
- A direct-ST-Link QMK/ChibiOS build with only six ordinary HID keys fails as
  `Unknown USB Device (Device Descriptor Request Failed)`. It has no VIA,
  RGB, Raw HID, EEPROM, mouse keys, or NKRO enabled.

## Eliminated causes

The same minimal QMK build was tested with a 6 KiB main/interrupt stack
instead of QMK's standard 1 KiB exception stack. Its initial vector changed
from `0x20000400` to `0x20001800`; Windows produced the identical descriptor
request failure. Stack size is therefore not the root cause.

A clone-safe attempt that changed ChibiOS from bulk-clearing USB status flags
to libmaple-style individual clearing also produced the identical error.

QMK 0.16.9 was also built from its original source with a legacy-compatible
configuration and flashed at `0x08000000`. ST-Link verified the 16,504-byte
image, but Windows still reported `USB\\VID_0000&PID_0002` / *Device Descriptor
Request Failed*. This rules out a USB regression limited to newer QMK releases:
the old and current ChibiOS USBv1 paths both fail on this chip.

## Conclusion

The fault is below QMK's keyboard/VIA configuration layer: it is a
compatibility issue between this clone's USB implementation and the ChibiOS
STM32F1 USBv1 driver or its low-level initialization sequence. Fixing QMK
requires a maintained patch/port of that USB driver, then re-testing basic HID
before enabling VIA. The likely reference implementation is STM32duino's
working libmaple USB stack; porting it requires replacing or adapting QMK's
endpoint-0, PMA, interrupt, and reset handling, not merely changing a compile
option.

The working STM32duino HID firmware is retained as the safe recovery path. No
option bytes, boot straps, or read-out protection were modified by this
investigation.
