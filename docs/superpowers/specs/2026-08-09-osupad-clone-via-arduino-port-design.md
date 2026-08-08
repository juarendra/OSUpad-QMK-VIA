# Design: Port OSUpadCloneVIA ke VIA-Arduino (Protocol-core swap)

## Objective

Pindahkan penanganan protocol VIA v13 pada firmware `OSUpadCloneVIA` ke
library `VIA-Arduino` (`via::Protocol`), tanpa mengganti lapisan board yang
sudah terbukti di clone STM32F103: USB libmaple/USBComposite, engine
keyboard/mouse/consumer/system, RGB WS2812, dan primitif flash.

Scope terpilih: **Protocol-core swap** (bukan full library adoption). Lapisan
board OSUPad dipertahankan 100%; hanya logika packet VIA, persistence record,
dan pengelolaan state keymap/macro/RGB yang diserahkan ke `via::Protocol`.

## Non-goals

- Tidak memindahkan ke official STM32duino core / Cube HAL.
- Tidak memperluas engine keyboard library (tap-hold, oneshot, macro eksekusi,
  mouse, consumer, system tetap di OSUPad).
- Tidak menambah encoder, bootloader jump, layout options.
- Tidak mengubah `via-definition.json` (tetap 2x3, 4 layer, 16 macro, 512B).

## Architecture

```
OSUpadCloneVIA.ino  (engine board dipertahankan)
  via_raw_hid.{h,cpp}  -> dibungkus OsupadTransport (via::Transport)
  engine keyboard/mouse/consumer/system + tap-hold + oneshot + run_macro
  RGB WS2812 rendering + rgb_render()
  keymap flat[24], macro_buffer[512], rgb, default_layer

VIA-Arduino
  via::Protocol            -> mengganti handle_via()/copy_keymap/via_set-get_rgblight
  OsupadCustomValue        -> via::CustomValue (RGB + default_layer, 6 byte)
  OsupadStorage            -> via::Storage (2 halaman flash, format VIAA)

Dihapus dari OSUPad: handle_via(), via_set/get_rgblight(),
copy_keymap_to_buffer()/copy_buffer_to_keymap(), settings_load()/settings_commit(),
PersistentImage/LegacyPersistentImage + validator v1/v2/legacy.
```

## Components

### 1. OsupadTransport (via::Transport)
- Membungkus `via_raw_hid_receive()` / `via_raw_hid_send()`.
- `sendComplete()` default true (USBComposite sinkron, tidak track async).
- Ditempatkan di `via_raw_hid.h/.cpp` atau file adaptor baru.

### 2. OsupadCustomValue (via::CustomValue)
- State 6 byte: `uint8_t rgb[5]` (brightness, effect, speed, hue, saturation) +
  `uint8_t default_layer`.
- `set()`/`get()` channel 2 (qmk_rgblight): sama dengan logika `RGBLight`.
  Set berhasil -> render RGB + tandai dirty engine.
- `saveState()/loadState()/validateState()` untuk 6 byte.
- `save()` (dipakai command 0x09): default `packet[1] == 0x02`.
- Boot normal: `loadState` menyalin state tersimpan.
- Factory reset: Protocol mengisi 0 -> RGB off + default_layer 0 (sesuai
  konvensi VIA/QMK factory reset).

### 3. OsupadStorage (via::Storage)
- Dua slot 1 KiB: `SETTINGS_PAGE_A = 0x08007800`, `SETTINGS_PAGE_B = 0x08007C00`
  (sama dengan sekarang). Pakai primitif yang sudah terbukti:
  `FLASH_Unlock/FLASH_ErasePage/FLASH_ProgramHalfWord` + verify baca.
- Menyimpan byte-stream Protocol: `StateHeader` (VIAA, 12 byte packed) +
  payload (keymap 48 + macros 512 + layoutOptions 4 + custom 6 = 570 byte;
  total 582 byte). Muat dalam satu halaman 1 KiB.
- Semantik `via::Storage`:
  - `begin()`: pilih slot aktif, jalankan migrasi 1x bila perlu.
  - `read()`: baca dari slot aktif.
  - `write()`: Protocol menulis berulang kali sebelum `commit()` (termasuk
    header dua kali: crc 0 lalu crc final). Adapter menyangga seluruh record
    di RAM (~582 B, termasuk final header) dan menulis flash hanya saat
    `commit()`.
  - `commit()`: erase page target, program buffer setengah-kata + verify,
    flip aktif.
  - `erase()`: siapkan slot provisional untuk factory reset.
- Guard interval retry commit (engine-side) supaya kegagalan berulang tidak
  meng-eraser flash terus-menerus.

### 4. Migrasi 1x (OSVP -> VIAA)
- Pada `begin()`, bila slot aktif berformat lama (magic `OSVP`, version 1/2,
  atau legacy layout), impor: keymap, macro, rgb (pakai tabel
  `migrate_v1_rgb_effect`), `default_layer`, lalu tulis format VIAA dan commit.
- Logika konversi record diekstrak jadi fungsi murni (host-testable).

### 5. via::Protocol config
```
rows=2, cols=3, layers=4
keymap=flat[24], defaultKeymap=flat[24]
macros=macro_buffer, macroBytes=512, macroCount=16
firmwareVersion=1
autoSaveMs=750
matrixStateEnabled=true     (VIA switch-matrix read 0x02/0x03)
eepromResetEnabled=true     (factory reset 0x0A)
bootloaderEnabled=false     (0x0B tetap 0xFF, tanpa bootloader)
loadBuffer=static array, loadBufferBytes=requiredLoadBufferSize()
defaultLayoutOptions=0
encoderCount=0
```

### 6. Engine OSUPad
- `keymap[LAYER_COUNT][KEY_COUNT]` menjadi flat `keymap[LAYER_COUNT * KEY_COUNT]`;
  indeks `layer * KEY_COUNT + key`. Wire format tetap 2x3 (VIA sends row, col).
- `layer_state` dan `default_layer` tetap engine-side.
- `resolved_keycode()` membaca keymap flat.
- `run_macro()` membaca `macro_buffer` (pointer sama yang di-feed ke Protocol).
- Perubahan via keycode (RGB `0x782x`, `DF`/`PDF`) -> set `local_dirty`;
  di `loop()`, setelah 750ms, panggil `protocol.save()`.

### 7. Callbacks (via::Callbacks)
- `matrixRow(row)`: bitmask `stable_state` (bit0 = kolom 0).
- `deviceIndication(value)`: toggle `device_indication` + `rgb_render()`.
- `changed()`: nop. `layoutOptionsChanged()`: nop. `bootloaderJump()`: nop.

## Data flow

- `setup()`: inisialisasi HID, scan pin, `usb`/HID begin; `protocol.begin(millis())`.
- `loop()`: `protocol.task(millis())` menggantikan
  `if (via_raw_hid_receive(report)) handle_via(report);`
  (task = receive -> process -> send response -> autosave). Scanning engine,
  mouse_task, rgb, tap-hold, dan save lokal tetap berjalan seperti sekarang.
- Perubahan via VIA -> `markDirty` -> autosave Protocol -> `OsupadStorage.commit()`.
- Perubahan via keycode -> `local_dirty` -> `protocol.save()` setelah 750ms.
- Boot: `begin()` -> `load()` via adapter; gagal -> reset buffer default.

## Parity deltas (perubahan perilaku yang disengaja)

- Factory reset (0x0A): RGB menjadi off (state 0) + default_layer 0, bukan
  `default_rgb {48,1,80,0,255}`. Sesuai konvensi VIA/QMK.
- `default_layer` kini ikut dipersist via custom state (sebelumnya hanya
  keymap/macro/RGB; default_layer juga sudah dipersist di record lama OSVP,
  tetap dipertahankan melalui migrasi).
- Komando 0x02/0x03 untuk layout options kini diproses Protocol (tidak
  dipakai OSUPad; value 0).
- Komando 0x0B bootloader -> 0xFF (sama dengan sekarang).

## Error handling

- Commit flash gagal: `save()` false -> `dirty_` tetap -> retry. Adapter
  menyediakan guard interval agar tidak meng-eraser flash berulang.
- Migrasi gagal baca/validasi: fallback ke default (reset buffer), tidak
  menghapus record lama.

## Testing / Verification (CI authoritative)

- GitHub Actions baru di repo `OSUpad-QMK-VIA`:
  - install core `rogerclarkmelbourne/Arduino_STM32` (libmaple) via board
    manager URL.
  - checkout `VIA-Arduino` dan compile sketch dengan
    `arduino-cli compile --libraries <VIA-Arduino>`.
  - host test `g++`: fungsi migrasi OSVP->VIAA (dengan flash fake) dan
    encode/decode `OsupadCustomValue`.
- `arduino-lint` untuk sketch (jika berlaku).

## Docs updates

- `README.md`: dependensi VIA-Arduino + cara build.
- `RELEASE_QA.md`: batas memori + checklist produksi.
- `FLASH_STLINK.md`: format penyimpanan baru (jika menyebut detail).
- `via-definition.json`: tidak berubah.

## Out of scope

- Porting engine ke library (tap-hold/oneshot/mouse/consumer/system/macro).
- Bootloader jump, encoder, layout options.
- Official STM32duino core.
