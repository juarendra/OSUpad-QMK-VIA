# OSUpad Clone VIA (Arduino)

Firmware rilis untuk OSUpad STM32F103 clone. Jalur USB STM32duino/libmaple
dipakai karena telah terbukti stabil pada clone ini, sementara jalur USB
QMK/ChibiOS tidak kompatibel pada sebagian chip clone.

## Kemampuan

- VIA protocol v13 dengan `via-definition.json` berformat VIA V3.
- Empat layer editable, 16 macro dinamis (512 byte), dan aksi layer `MO`,
  `LM`, `LT`, `TT`, `TO`, `TG`, `DF`, `PDF`, `OSL`, serta `OSM`.
- Delapan WS2812 pada PA5, keyboard, mouse delapan tombol dan scroll dua arah,
  consumer/media, serta system-control HID.
- Penyimpanan keymap, macro, RGB, dan default layer secara redundant dengan
  CRC di dua halaman flash terakhir (`0x08007800` dan `0x08007C00`).

Sesudah mengubah pengaturan VIA, tunggu sekitar satu detik sebelum mencabut
USB. Firmware tidak mengubah BOOT strap, Option Bytes, read-out protection,
atau bootloader.

## Dokumen

- [Panduan pengguna](../../DOC/PANDUAN_OSUPAD.md): VIA, keymap, macro, RGB,
  dan recovery.
- [Flash ST-Link](FLASH_STLINK.md): prosedur program pada `0x08000000`.
- [Release QA](RELEASE_QA.md): batas memori dan checklist produksi.

Definisi belum masuk database VIA global. Di VIA, aktifkan **Show Design Tab**,
muat `via-definition.json`, dan biarkan **Use V2 definitions (deprecated)**
tetap nonaktif.

Unicode, MIDI, audio, steno, tap dance, serta ekstensi QMK khusus keyboard tidak
termasuk dalam implementasi Arduino ini.
