# OSUpad Clone VIA (Arduino)

Firmware rilis untuk OSUpad STM32F103 clone. Jalur USB STM32duino/libmaple
dipakai karena telah terbukti stabil pada clone ini, sementara jalur USB
QMK/ChibiOS tidak kompatibel pada sebagian chip clone.

## Dependensi

Firmware ini memakai library [VIA-Arduino](https://github.com/juarendra/VIA-Arduino)
(versi rilis terbaru) untuk penanganan protocol VIA v13. Install library di
folder `libraries` Arduino IDE, atau build dengan `arduino-cli compile
--libraries <path-ke-VIA-Arduino>`. Lapisan board (USB libmaple/USBComposite,
keyboard/mouse/RGB, dan storage flash) tetap milik firmware ini.

## Kemampuan

- VIA protocol v13 via library VIA-Arduino (`via::Protocol`) dengan
  `via-definition.json` berformat VIA V3.
- Empat layer editable, 16 macro dinamis (512 byte), aksi layer MO, LM, LT, TT,
  TO, TG, DF, PDF, OSL, OSM.
- Delapan WS2812 pada PA5, keyboard, mouse delapan tombol dan scroll dua arah,
  consumer/media, system-control HID.
- Penyimpanan keymap, macro, RGB, dan default layer di dua halaman flash
  terakhir (`0x08007800`/`0x08007C00`). Settings lama (format OSVP) dimigrasi
  otomatis saat update firmware.

Sesudah mengubah pengaturan VIA, tunggu sekitar satu detik sebelum mencabut
USB. Firmware tidak mengubah BOOT strap, Option Bytes, read-out protection,
atau bootloader.

## Dokumen

- [Panduan pengguna](../../DOC/PANDUAN_OSUPAD.md): VIA, keymap, macro, RGB,
  dan recovery.
- [Flash ST-Link](FLASH_STLINK.md): prosedur program pada `0x08000000`.
- [Release QA](RELEASE_QA.md): batas memori dan checklist produksi.

Definisi belum masuk database VIA global (namun PID `0x1000` telah disesuaikan agar cocok dengan firmware QMK aslinya). Di VIA, aktifkan **Show Design Tab**,
muat `via-definition.json`, dan pastikan **Use V2 definitions (deprecated)**
tetap nonaktif.

Unicode, MIDI, audio, steno, tap dance, serta ekstensi QMK khusus keyboard tidak
termasuk dalam implementasi Arduino ini.
