# Changelog

## v1.1.4-clone-via — 2026-08-12

- **Fix (RGB):** Perbaikan format penerimaan Custom Value VIA v3 (`0x80`–`0x83`). Semua opsi di tab Lighting VIA (mode, warna, brightness, speed, off) sekarang berfungsi normal pada VIA v3 web.
- **Fix (RGB):** Penyesuaian timing bit-bang WS2812 di 72MHz (`delay(14)/delay(11)` untuk bit 1, dan `delay(6)/delay(16)` untuk bit 0). Mencegah masalah LED selalu menyala putih terang.
- **Fix (RGB):** Callback `deviceIndication` sekarang mematuhi *value* (0=off).
- **Fix (Storage):** Menambahkan `storage.begin()` pada saat boot agar keymap, macro, dan RGB state berhasil dimuat dan tidak lagi kembali ke *default* saat USB dicabut-colok.
- **Fix (Storage):** Mencegah penghapusan storage tidak sengaja di *High-Density clone chips* (flash 256KB, page erase 2KB).
- **Update:** Merubah default warna LED saat boot menjadi merah terang.

## v1.1.0-clone-via — 2026-08-11

- **Feat:** Porting protokol VIA v13 untuk sepenuhnya menggunakan library `VIA-Arduino`.
- **Feat:** Mengubah USB PID dari `0x1004` ke `0x1000` (sama dengan QMK OSUpad). Hal ini memungkinkan auto-detect di VIA tanpa perlu mengunggah ulang JSON jika sebelumnya cache definisi OSUpad sudah ada di browser.
- **Feat:** Implementasi Dual-page Flash Storage yang tahan dari kegagalan daya (*power-loss recovery*) dengan migrasi otomatis dari format lama (OSVP).
- **Docs:** Penambahan Buku Panduan dalam format DOCX dan PDF.

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
