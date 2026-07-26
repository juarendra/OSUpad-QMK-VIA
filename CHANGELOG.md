# Changelog

## v1.0.0-clone-via — 2026-07-26

Rilis produk pertama untuk OSUpad STM32F103 clone.

- Firmware Arduino STM32duino/libmaple yang stabil pada USB clone.
- VIA V3 melalui Raw HID, 4 layer dan 16 macro QMK/VIA persisten.
- Keycode keyboard, media/consumer, mouse (termasuk tombol 4–8 dan scroll
  horizontal), serta system control.
- RGBLight 8 LED dengan mode QMK dan konfigurasi persisten.
- Penyimpanan redundant ber-CRC dan dokumentasi recovery/flash ST-Link.
- Build GitHub Actions, validasi kontrak firmware/JSON, serta QA perangkat
  fisik telah lulus.

## Catatan kompatibilitas

Firmware ini ditujukan untuk STM32F103 clone yang tidak kompatibel dengan
jalur USB QMK/ChibiOS. Jangan gunakan binary QMK `osupad_via.bin` pada target
tersebut.
