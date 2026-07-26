# OSUpad clone USB HID firmware

This is a small native-USB keyboard firmware for the STM32F103 clone used in
the OSUpad. It intentionally uses the STM32duino/libmaple USB implementation:
the same USB family already proven to enumerate on this board as Arduino COM12.

It is not a QMK/VIA build. QMK's ChibiOS USB device path returned a Windows
device-descriptor error on this clone, while the STM32duino USB path works.

## Current key map

| Physical matrix position | Pin | Key |
| --- | --- | --- |
| row 0, column 0 | PB0 | A |
| row 0, column 1 | PA7 | B |
| row 0, column 2 | PA6 | C |
| row 1, column 0 | PB12 | E |
| row 1, column 1 | PB13 | F |
| row 1, column 2 | PB14 | G |

## Build and flash from Arduino IDE

1. Open `OSUpadCloneHID.ino`.
2. Select **Generic STM32F103C6/fake STM32F103C8**.
3. Select **Upload method: STLink** and **CPU speed: 72 MHz**.
4. Upload with the ST-Link connected by SWD. This direct-ST-Link build starts
   at `0x08000000`; do not use the `STM32duino bootloader` upload method for
   this binary.
5. Disconnect/reconnect the OSUpad USB cable. Windows should install it as a
   standard HID keyboard; no serial COM port is expected from this firmware.

The firmware does not change option bytes, boot straps, or read-out protection.
