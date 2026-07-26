# Firmware OSUpad

## Firmware rilis: OSUpadCloneVIA

[`OSUpadCloneVIA`](OSUpadCloneVIA) adalah satu-satunya firmware yang didukung
untuk OSUpad dengan STM32F103 clone. Unduh binary dan JSON VIA dari
[Releases](https://github.com/juarendra/OSUpad-QMK-VIA/releases/latest), lalu
flash menggunakan ST-Link pada alamat `0x08000000`.

| Berkas | Fungsi |
| --- | --- |
| `OSUpadCloneVIA.ino.bin` | Firmware rilis untuk clone; tersedia sebagai asset GitHub Release. |
| `via-definition.json` | Definisi VIA V3; muat dari tab Design. |
| `FLASH_STLINK.md` | Flash, update, dan recovery tanpa mengubah BOOT/RDP. |
| `RELEASE_QA.md` | Validasi teknis dan checklist produksi. |

Lihat [panduan pengguna](../DOC/PANDUAN_OSUPAD.md) untuk setup VIA, macro, RGB,
dan contoh keycode.

## Referensi QMK STM32 asli

Folder `osupad` dan binary `osupad_via.bin` dipertahankan semata-mata sebagai
referensi QMK untuk MCU STM32 asli. Jangan gunakan binary itu pada clone yang
bermasalah dengan USB QMK/ChibiOS; gunakan `OSUpadCloneVIA`.

Folder probe USB, eksperimen ST-Link QMK, dan firmware HID awal sengaja tidak
ada di branch rilis agar pengguna tidak salah memilih firmware.
