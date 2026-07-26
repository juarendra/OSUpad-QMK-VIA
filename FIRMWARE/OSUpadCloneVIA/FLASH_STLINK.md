# Flash through ST-Link (direct application image)

This firmware is a direct-SWD image: program its `.bin` at `0x08000000`.
It is **not** an STM32duino bootloader image and does not require changing
BOOT0 or BOOT1. Keep BOOT0 tied low for normal startup.

## Before flashing

1. Disconnect the USB cable from the macropad. Power it only from the ST-Link
   while programming; avoid powering it from both supplies unless the board is
   designed for that.
2. Connect ST-Link `SWDIO`, `SWCLK`, and `GND`; connect 3.3 V only when your
   ST-Link is intended to power the target. `NRST` is optional for normal
   programming.
3. In STM32 ST-LINK Utility, confirm the target is detected by SWD. Do not
   alter option bytes, Read Out Protection, or BOOT jumpers for this procedure.

## Program and verify

Open the `osupad-clone-via-stlink` GitHub Actions artifact, then use
`OSUpadCloneVIA.ino.bin`.

- In ST-LINK Utility: **File → Open file**, select the binary, enter start
  address `0x08000000`, then choose **Target → Program & Verify**.
- Or with ST-LINK CLI:

  ```text
  ST-LINK_CLI.exe -c SWD -P OSUpadCloneVIA.ino.bin 0x08000000 -V after_programming -Rst -Run
  ```

The release build is constrained to the first 30 KiB. The firmware binary never
uses the last two 1 KiB pages (`0x08007800` and `0x08007C00`), which store VIA
settings. For an update where those settings matter, select an erase mode that
erases only the required pages; **Mass Erase** deliberately clears the entire
flash, including the saved keymap, macros, and RGB configuration.

## First USB and VIA test

1. Disconnect ST-Link, connect the macropad directly to a data-capable USB
   port, and wait for Windows to enumerate it as OSUpad Clone VIA.
2. In VIA Web, open **Design**, load `via-definition.json`, leave **Use V2
   definitions** disabled, then authorize the device.
3. Remap one key and edit a macro. Wait at least one second before removing
   USB power; reconnect and verify both changes remain.

## Recovery

If USB does not enumerate, reconnect ST-Link and repeat the same direct flash.
The recovery path does not depend on the USB bootloader. If ST-Link reports
read-out protection, stop there: clearing RDP mass-erases the MCU and should
only be done deliberately after making a backup of any recoverable firmware.
