# Flash melalui ST-Link

Firmware ini adalah image SWD langsung: program file `.bin` pada alamat
`0x08000000`. Ini **bukan** image STM32duino bootloader; BOOT0 dan BOOT1 tidak
perlu diubah. Untuk pemakaian normal, BOOT0 tetap terhubung ke GND.

## Sebelum flash

1. Lepas kabel USB dari macropad. Hindari memberi daya dari USB dan ST-Link
   sekaligus kecuali rangkaian board memang mendukungnya.
2. Hubungkan ST-Link `SWDIO`, `SWCLK`, dan `GND`. Hubungkan `3.3V` hanya bila
   ST-Link memang dipakai untuk memberi daya ke target. `NRST` opsional.
3. Pastikan STM32 ST-LINK Utility mendeteksi target melalui SWD. Jangan ubah
   Option Bytes, Read Out Protection, ataupun jumper BOOT.

## Program dan verifikasi

Unduh `OSUpadCloneVIA.ino.bin` dari
[GitHub Releases](https://github.com/juarendra/OSUpad-QMK-VIA/releases/latest).

- Di ST-LINK Utility: **File → Open file**, pilih binary, isi alamat awal
  `0x08000000`, kemudian pilih **Target → Program & Verify**.
- Atau dengan ST-LINK CLI:

  ```text
  ST-LINK_CLI.exe -c SWD -P OSUpadCloneVIA.ino.bin 0x08000000 -V after_programming -Rst -Run
  ```

Build rilis hanya memakai 30 KiB pertama. Dua halaman 1 KiB terakhir
(`0x08007800` dan `0x08007C00`) menyimpan setting VIA dalam format VIA-Arduino
(`StateHeader` + payload). Saat update dari firmware rilis lama, settings
otomatis dimigrasi dari format OSVP. Saat update, gunakan erase halaman yang
diperlukan bila ingin mempertahankan setting. **Mass Erase** sengaja menghapus
seluruh flash, termasuk keymap, macro, dan RGB tersimpan.

## Uji USB dan VIA pertama

1. Lepas ST-Link dan hubungkan USB data langsung ke komputer.
2. Di VIA Web, buka **Design**, muat `via-definition.json`, pastikan **Use V2
   definitions** nonaktif, kemudian authorize perangkat.
3. Remap satu tombol dan buat macro. Tunggu sedikitnya satu detik, cabut USB,
   sambungkan lagi, dan pastikan keduanya tetap tersimpan.

## Recovery

Jika USB tidak terdeteksi, hubungkan kembali ST-Link dan flash ulang binary
yang sama. Recovery tidak memerlukan bootloader USB. Bila ST-Link melaporkan
Read Out Protection, berhenti terlebih dahulu: menonaktifkan RDP melakukan
mass erase dan hanya boleh dilakukan dengan sengaja.
