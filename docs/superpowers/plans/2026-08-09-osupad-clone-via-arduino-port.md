# OSUpadCloneVIA → VIA-Arduino Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ganti penanganan packet VIA v13 di firmware `OSUpadCloneVIA` dengan `via::Protocol` milik VIA-Arduino, sambil mempertahankan lapisan board (USB libmaple/USBComposite, engine keyboard/mouse/consumer/system, RGB WS2812, dual-page flash storage + migrasi settings lama).

**Architecture:** Firmware memakai `via::Protocol` sebagai sumber kebenaran keymap/macro/RGB/default_layer. Lapisan board disambung lewat tiga adapter baru: `OsupadTransport` (`via::Transport`), `OsupadCustomValue` (`via::CustomValue`, RGB + default_layer, 6 byte), dan `OsupadStorage` (`via::Storage`, dua slot flash 1 KiB di `0x08007800`/`0x08007C00`, migrasi 1x dari format OSVP lama). Semua adapter ditulis portable (tanpa Arduino.h) supaya bisa di-host-test; impl flash libmaple dipisah.

**Tech Stack:** Arduino/STM32F1 libmaple core (`stm32duino:STM32F1@2022.9.26`), USBComposite 1.0.8, VIA-Arduino (juarendra/VIA-Arduino), g++ host test, GitHub Actions.

## Global Constraints

- Core: `stm32duino:STM32F1@2022.9.26`, board `genericSTM32F103C6`, `upload_method=STLinkMethod,cpu_speed=speed_72mhz,opt=osstd`.
- Binary harus ≤ 30720 byte; dua halaman 1 KiB terakhir (`0x08007800`, `0x08007C00`) untuk settings, tidak boleh terisi kode.
- `via-definition.json` TIDAK berubah: VID `0x7877`, PID `0x1004`, firmware version `1`, matrix 2x3, 4 layer, 16 macro, 512 byte.
- CRC32 = reflected `0xEDB88320`, init `0xFFFFFFFF`, final XOR `~`; `osupadCrc32("123456789") == 0xCBF43926`.
- VIA-Arduino dipakai sebagai dependency (di-checkout di CI, `--libraries`), tidak di-vendor.
- Lokal Windows tidak punya `g++`/`arduino-cli`; CI adalah compiler otoritatif. Setiap tugas harus push + CI hijau.
- VIA-Arduino `stm32f1::FlashStorage` TIDAK dipakai (slot `0x0801F000` salah untuk board ini, dan `write()`-nya butuh `erase()` manual).
- Parity engine dijaga: mouse 8 tombol + scroll 2 arah, consumer, system, RGB effect 1..42, macro bytecode QMK, tap-hold (MT/LT/TT), oneshot (OSL/OSM), layer MO/TO/TG/DF/PDF/LM.

---

## File Structure

- Modify: `FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino` — hapus `handle_via`/settings_* lama, wire Protocol, keymap flat.
- Modify: `FIRMWARE/OSUpadCloneVIA/via_raw_hid.h` — tambah `OsupadTransport : via::Transport`.
- Create: `FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.h` — constants, `OsupadRgbState`, `osupadCrc32`, `osupadConvertRecord`, `OsupadCustomValue`, `OsupadStorage`.
- Create: `FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.cpp` — implementasi pure (tanpa Arduino.h).
- Create: `FIRMWARE/OSUpadCloneVIA/stm32_flash_memory.h` — `Stm32FlashMemory : via::FlashMemory` (libmaple `FLASH_*`), header-only.
- Create: `tests/port/crc_test.cpp`, `tests/port/migration_test.cpp`, `tests/port/custom_value_test.cpp`, `tests/port/storage_test.cpp`.
- Modify: `.github/workflows/build-osupad-clone-via.yml` — checkout VIA-Arduino, tambah job `port-tests`.
- Modify: `tools/verify_osupad_clone_via.py` — token sesuai struktur baru.
- Modify: `FIRMWARE/OSUpadCloneVIA/README.md`, `RELEASE_QA.md`, `FLASH_STLINK.md`.

### Constants (dipakai lintas tugas)

```cpp
// osupad_via_adapters.h
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "VIA_Protocol.h"
#include "VIA_STM32F1_Flash.h"

constexpr uint32_t kStoragePageA = 0x08007800UL;
constexpr uint32_t kStoragePageB = 0x08007C00UL;
constexpr uint32_t kStoragePageBytes = 1024;

// Protocol record layout (StateHeader 12 byte + payload)
constexpr size_t kStateHeaderSize = 12;
constexpr size_t kKeymapBytes = 4 * 6 * sizeof(uint16_t);      // 48
constexpr size_t kMacroBytes = 512;
constexpr size_t kLayoutOptionsBytes = sizeof(uint32_t);       // 4
constexpr size_t kCustomBytes = 6;                              // rgb5 + default_layer1
constexpr size_t kPayloadBytes = kKeymapBytes + kMacroBytes + kLayoutOptionsBytes + kCustomBytes;  // 570
constexpr size_t kRecordSize = kStateHeaderSize + kPayloadBytes; // 582
constexpr size_t kLayoutOptionsOffset = kStateHeaderSize + kKeymapBytes + kMacroBytes;  // 572
constexpr uint32_t kViaaMagic = 0x56494141UL;   // VIA-Arduino StateHeader magic (kStateMagic)
constexpr uint16_t kViaaVersion = 2;
constexpr uint32_t kOsvpMagic = 0x4F535650UL;   // "OSVP"
constexpr size_t kOsvpHeaderSize = 16;
constexpr size_t kOsvpPayloadV2 = 566;
constexpr size_t kOsvpPayloadLegacy = 245;
constexpr size_t kOsvpLegacyMacroBytes = 192;

struct OsupadRgbState {
  uint8_t brightness, effect, speed, hue, saturation;
};

uint32_t osupadCrc32(const uint8_t* data, size_t len);
bool osupadConvertRecord(const uint8_t* oldRecord, size_t oldLen,
                         uint8_t* outRecord, size_t outCap, size_t* outLen);

class OsupadCustomValue : public via::CustomValue {
 public:
  OsupadCustomValue(OsupadRgbState& rgb, uint8_t& defaultLayer,
                    void (*onApply)() = nullptr);
  bool set(uint8_t packet[via::kPacketSize]) override;
  bool get(uint8_t packet[via::kPacketSize]) override;
  size_t stateSize() const override { return kCustomBytes; }
  bool saveState(uint8_t* output, size_t length) const override;
  bool loadState(const uint8_t* input, size_t length) override;
  bool validateState(const uint8_t* input, size_t length) const override;

 private:
  void apply();
  OsupadRgbState& rgb_;
  uint8_t& defaultLayer_;
  void (*onApply_)();
};

class OsupadStorage : public via::Storage {
 public:
  OsupadStorage(via::FlashMemory& flash, uint8_t* recordBuffer, size_t recordBytes);
  bool begin();
  size_t capacity() const override { return kRecordSize; }
  bool read(size_t offset, uint8_t* output, size_t length) override;
  bool write(size_t offset, const uint8_t* input, size_t length) override;
  bool commit() override;
  bool erase() override;

 private:
  bool validAt(uint32_t addr) const;
  uint32_t osvpSequenceAt(uint32_t addr) const;
  bool readRecord(uint32_t addr, uint8_t* out) const;
  bool programRecord(uint32_t addr, const uint8_t* record);
  via::FlashMemory& flash_;
  uint8_t* buffer_;
  size_t bufferBytes_;
  uint32_t activeSlot_;
  uint32_t provisionalSlot_;
  bool activeValid_;
  bool provisionalReady_;
};
```

---

### Task 1: CI port-tests + VIA-Arduino dependency + adapter skeleton + crc

**Files:**
- Create: `FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.h`
- Create: `FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.cpp`
- Create: `tests/port/crc_test.cpp`
- Modify: `.github/workflows/build-osupad-clone-via.yml`

**Interfaces:**
- Produces: `osupadCrc32(const uint8_t*, size_t) -> uint32_t`; constants `kViaaMagic`, `kRecordSize`, dst; file `osupad_via_adapters.h/.cpp` yang host-testable.

- [ ] **Step 1: Create `osupad_via_adapters.h`** dengan isi persis blok `### Constants` di atas.

- [ ] **Step 2: Create `osupad_via_adapters.cpp`** (hanya crc dulu):

```cpp
#include "osupad_via_adapters.h"

uint32_t osupadCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1U)));
  }
  return ~crc;
}
```

- [ ] **Step 3: Create `tests/port/crc_test.cpp`**:

```cpp
#include <assert.h>
#include <stdint.h>
#include "osupad_via_adapters.h"

int main() {
  static_assert(via::kPacketSize == 32, "VIA packet size");
  static_assert(kRecordSize == 582, "record size");
  static_assert(kCustomBytes == 6, "custom bytes");
  assert(osupadCrc32((const uint8_t*)"123456789", 9) == 0xCBF43926UL);
  assert(osupadCrc32((const uint8_t*)"VIA", 3) != 0);
  return 0;
}
```

- [ ] **Step 4: Update `.github/workflows/build-osupad-clone-via.yml`** — tambah checkout VIA-Arduino ke job `build` dan tambah job `port-tests`:

```yaml
  port-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Check out VIA-Arduino
        uses: actions/checkout@v4
        with:
          repository: juarendra/VIA-Arduino
          path: via-arduino
      - name: Build and run port tests
        shell: bash
        run: |
          set -euo pipefail
          for t in tests/port/*_test.cpp; do
            g++ -std=c++11 -Wall -Wextra -Werror -I via-arduino/src -I FIRMWARE/OSUpadCloneVIA \
              "$t" FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.cpp -o port_test
            ./port_test
          done
```

Pada job `build`, tambah step checkout VIA-Arduino dan flag `--libraries`:

```yaml
      - name: Check out VIA-Arduino
        uses: actions/checkout@v4
        with:
          repository: juarendra/VIA-Arduino
          path: via-arduino
```

Ubah perintah compile menjadi (tambah `--libraries "$GITHUB_WORKSPACE/via-arduino"`):

```bash
          arduino-cli compile \
            --fqbn stm32duino:STM32F1:genericSTM32F103C6:upload_method=STLinkMethod,cpu_speed=speed_72mhz,opt=osstd \
            --libraries "$RUNNER_TEMP/arduino-libraries" \
            --libraries "$GITHUB_WORKSPACE/via-arduino" \
            --output-dir build \
            FIRMWARE/OSUpadCloneVIA
```

- [ ] **Step 5: Commit + push + cek CI**

```bash
git add FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.h \
        FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.cpp \
        tests/port/crc_test.cpp .github/workflows/build-osupad-clone-via.yml
git commit -m "ci(osupad): add port tests and VIA-Arduino dependency"
git push
```

Lalu: `gh run watch $(gh run list --limit 1 --json databaseId --jq '.[0].databaseId') --exit-status`
Expected: job `port-tests` PASS, job `build` PASS (sketch lama masih kompil).

---

### Task 2: Migrasi record OSVP → VIAA (pure)

**Files:**
- Modify: `FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.h` (deklarasi `osupadConvertRecord` sudah ada)
- Modify: `FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.cpp`
- Create: `tests/port/migration_test.cpp`

**Interfaces:**
- Consumes: `osupadCrc32`, konstanta dari Task 1.
- Produces: `osupadConvertRecord(const uint8_t*, size_t, uint8_t*, size_t, size_t*) -> bool`.

- [ ] **Step 1: Tulis failing test `tests/port/migration_test.cpp`**:

```cpp
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "osupad_via_adapters.h"

// Old OSVP payload: keymap(48) + macros + rgb(5) + default_layer(1).
static void fillOsvp(uint8_t* rec, size_t payloadBytes, uint16_t version,
                     uint8_t effect, uint8_t defaultLayer, bool legacy) {
  uint32_t magic = kOsvpMagic;
  memcpy(rec, &magic, 4);
  rec[4] = version & 0xFF; rec[5] = version >> 8;
  rec[6] = payloadBytes & 0xFF; rec[7] = payloadBytes >> 8;
  rec[8] = 0x01; rec[9] = 0x00; rec[10] = 0x00; rec[11] = 0x00;  // sequence 1
  uint8_t* payload = rec + kOsvpHeaderSize;
  for (int i = 0; i < 48; ++i) payload[i] = (uint8_t)i;  // keymap
  if (legacy) {
    for (int i = 0; i < (int)kOsvpLegacyMacroBytes; ++i) payload[48 + i] = (uint8_t)(i + 1);
    payload[48 + kOsvpLegacyMacroBytes + 0] = 0x30;  // brightness
    payload[48 + kOsvpLegacyMacroBytes + 1] = effect;
    payload[48 + kOsvpLegacyMacroBytes + 2] = 0x40;  // speed
    payload[48 + kOsvpLegacyMacroBytes + 3] = 0x50;  // hue
    payload[48 + kOsvpLegacyMacroBytes + 4] = 0x60;  // saturation
  } else {
    for (int i = 0; i < 512; ++i) payload[48 + i] = (uint8_t)(i + 1);
    payload[48 + 512 + 0] = 0x30;
    payload[48 + 512 + 1] = effect;
    payload[48 + 512 + 2] = 0x40;
    payload[48 + 512 + 3] = 0x50;
    payload[48 + 512 + 4] = 0x60;
    payload[48 + 512 + 5] = defaultLayer;
  }
  uint32_t crc = osupadCrc32(payload, payloadBytes);
  memcpy(rec + 12, &crc, 4);
}

static void assertV1Effect(uint8_t oldEffect, uint8_t expectedNew) {
  uint8_t rec[582 + 16];
  uint8_t out[582];
  memset(rec, 0xFF, sizeof(rec));
  fillOsvp(rec, kOsvpPayloadV2, 1, oldEffect, 2, false);
  size_t outLen = 0;
  assert(osupadConvertRecord(rec, 16 + kOsvpPayloadV2, out, sizeof(out), &outLen));
  assert(outLen == kRecordSize);
  assert(out[0] == 0x41 && out[1] == 0x41 && out[2] == 0x49 && out[3] == 0x56);  // VIAA magic LE
  assert(out[4] == 2 && out[5] == 0);                                            // version 2
  assert(out[6] == (kPayloadBytes & 0xFF) && out[7] == (kPayloadBytes >> 8));
  assert(osupadCrc32(out + kStateHeaderSize, kPayloadBytes) ==
         ((uint32_t)out[8] | ((uint32_t)out[9] << 8) | ((uint32_t)out[10] << 16) | ((uint32_t)out[11] << 24)));
  assert(out[kStateHeaderSize + 48 + 512 + 1] == expectedNew);  // migrated rgb.effect
  assert(out[kStateHeaderSize + 48 + 512 + 5] == 2);            // default_layer kept
}

int main() {
  // OSVP v2 -> VIAA
  uint8_t rec[582 + 16];
  uint8_t out[582];
  memset(rec, 0xFF, sizeof(rec));
  fillOsvp(rec, kOsvpPayloadV2, 2, 21, 3, false);
  size_t outLen = 0;
  assert(osupadConvertRecord(rec, 16 + kOsvpPayloadV2, out, sizeof(out), &outLen));
  assert(outLen == kRecordSize);
  assert(out[0] == 0x41 && out[1] == 0x41 && out[2] == 0x49 && out[3] == 0x56);
  assert(out[4] == 2 && out[5] == 0);
  assert(memcmp(out + kStateHeaderSize, rec + kOsvpHeaderSize, 48) == 0);       // keymap
  assert(memcmp(out + kStateHeaderSize + 48, rec + kOsvpHeaderSize + 48, 512) == 0);  // macros
  assert(out[kStateHeaderSize + 48 + 512 + 0] == 0x30);  // brightness
  assert(out[kStateHeaderSize + 48 + 512 + 1] == 21);    // effect unchanged (v2)
  assert(out[kStateHeaderSize + 48 + 512 + 5] == 3);     // default_layer

  // Legacy OSVP (192-byte macros) -> VIAA, macro zero-padded, default_layer 0
  memset(rec, 0xFF, sizeof(rec));
  memset(out, 0, sizeof(out));
  fillOsvp(rec, kOsvpPayloadLegacy, 1, 9, 0, true);
  outLen = 0;
  assert(osupadConvertRecord(rec, 16 + kOsvpPayloadLegacy, out, sizeof(out), &outLen));
  assert(outLen == kRecordSize);
  assert(memcmp(out + kStateHeaderSize + 48, rec + kOsvpHeaderSize + 48, kOsvpLegacyMacroBytes) == 0);
  for (int i = 48 + (int)kOsvpLegacyMacroBytes; i < 48 + 512; ++i)
    assert(out[kStateHeaderSize + i] == 0);               // padded
  assert(out[kStateHeaderSize + 48 + 512 + 5] == 0);      // default_layer 0

  // v1 compact RGB effects 1..10 -> QMK IDs
  assertV1Effect(1, 1);   // static
  assertV1Effect(4, 9);   // rainbow swirl
  assertV1Effect(10, 37); // twinkle

  // Reject garbage
  uint8_t bad[16 + kOsvpPayloadV2];
  memset(bad, 0xEE, sizeof(bad));
  outLen = 0;
  assert(!osupadConvertRecord(bad, sizeof(bad), out, sizeof(out), &outLen));

  // Reject when output too small
  memset(rec, 0xFF, sizeof(rec));
  fillOsvp(rec, kOsvpPayloadV2, 2, 21, 0, false);
  outLen = 0;
  assert(!osupadConvertRecord(rec, 16 + kOsvpPayloadV2, out, 16, &outLen));

  return 0;
}
```

- [ ] **Step 2: Run test, pastikan GAGAL** — push, `gh run watch ...` pada job `port-tests`: FAIL karena `osupadConvertRecord` belum diimplementasi.

- [ ] **Step 3: Implement `osupadConvertRecord` + tabel migrasi effect di `osupad_via_adapters.cpp`**:

```cpp
namespace {

uint8_t migrateV1RgbEffect(uint8_t effect) {
  switch (effect) {
    case 0: return 0;
    case 1: return 1;   // static
    case 2: return 2;   // breathing
    case 3: return 6;   // rainbow mood
    case 4: return 9;   // rainbow swirl
    case 5: return 15;  // snake
    case 6: return 21;  // knight
    case 7: return 24;  // Christmas
    case 8: return 25;  // static gradient
    case 9: return 35;  // RGB test
    case 10: return 37; // twinkle
    default: return 1;
  }
}

}  // namespace

bool osupadConvertRecord(const uint8_t* oldRecord, size_t oldLen,
                         uint8_t* outRecord, size_t outCap, size_t* outLen) {
  if (!oldRecord || !outRecord || !outLen) return false;
  if (oldLen < kOsvpHeaderSize || outCap < kRecordSize) return false;
  uint32_t magic;
  memcpy(&magic, oldRecord, 4);
  if (magic != kOsvpMagic) return false;
  const uint16_t version = (uint16_t)(oldRecord[4] | (oldRecord[5] << 8));
  const uint16_t payloadSize = (uint16_t)(oldRecord[6] | (oldRecord[7] << 8));
  if (payloadSize != kOsvpPayloadV2 && payloadSize != kOsvpPayloadLegacy) return false;
  if (kOsvpHeaderSize + payloadSize != oldLen) return false;

  const uint8_t* payload = oldRecord + kOsvpHeaderSize;
  const bool legacy = (payloadSize == kOsvpPayloadLegacy);
  const size_t srcMacroBytes = legacy ? kOsvpLegacyMacroBytes : kMacroBytes;

  uint32_t storedCrc;
  memcpy(&storedCrc, oldRecord + 12, 4);
  if (osupadCrc32(payload, payloadSize) != storedCrc) return false;

  memset(outRecord, 0, kRecordSize);
  uint8_t* outPayload = outRecord + kStateHeaderSize;
  memcpy(outPayload, payload, 48);                                  // keymap
  memcpy(outPayload + 48, payload + 48, srcMacroBytes);             // macros
  uint8_t effect = payload[48 + srcMacroBytes + 1];
  if (version == 1) effect = migrateV1RgbEffect(effect);
  outPayload[48 + kMacroBytes + 1] = effect;                        // rgb.effect (migrated)
  outPayload[48 + kMacroBytes + 0] = payload[48 + srcMacroBytes + 0];  // brightness
  outPayload[48 + kMacroBytes + 2] = payload[48 + srcMacroBytes + 2];  // speed
  outPayload[48 + kMacroBytes + 3] = payload[48 + srcMacroBytes + 3];  // hue
  outPayload[48 + kMacroBytes + 4] = payload[48 + srcMacroBytes + 4];  // saturation
  outPayload[48 + kMacroBytes + 5] = legacy ? 0 : payload[48 + srcMacroBytes + 5];  // default_layer

  outRecord[0] = (uint8_t)(kViaaMagic & 0xFF);
  outRecord[1] = (uint8_t)((kViaaMagic >> 8) & 0xFF);
  outRecord[2] = (uint8_t)((kViaaMagic >> 16) & 0xFF);
  outRecord[3] = (uint8_t)((kViaaMagic >> 24) & 0xFF);
  outRecord[4] = (uint8_t)(kViaaVersion & 0xFF);
  outRecord[5] = (uint8_t)(kViaaVersion >> 8);
  outRecord[6] = (uint8_t)(kPayloadBytes & 0xFF);
  outRecord[7] = (uint8_t)(kPayloadBytes >> 8);
  uint32_t crc = osupadCrc32(outPayload, kPayloadBytes);
  outRecord[8] = (uint8_t)(crc & 0xFF);
  outRecord[9] = (uint8_t)((crc >> 8) & 0xFF);
  outRecord[10] = (uint8_t)((crc >> 16) & 0xFF);
  outRecord[11] = (uint8_t)((crc >> 24) & 0xFF);

  *outLen = kRecordSize;
  return true;
}
```

Catatan: guard `if (osupadCrc32(oldRecord + kOsvpHeaderSize, oldLen - kOsvpHeaderSize) == 0) return false;` hanya anti-0xFF awal; validasi utama di bawah.

- [ ] **Step 4: Run test, pastikan PASS** — commit + push, `gh run watch`, job `port-tests` PASS.

- [ ] **Step 5: Commit**

```bash
git add FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.cpp tests/port/migration_test.cpp
git commit -m "feat(osupad): migrate OSVP settings records to VIAA format"
```

---

### Task 3: OsupadCustomValue (RGB + default_layer)

**Files:**
- Modify: `FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.cpp`
- Create: `tests/port/custom_value_test.cpp`

**Interfaces:**
- Consumes: `via::CustomValue` (VIA_Protocol.h), `OsupadRgbState`.
- Produces: `OsupadCustomValue` (set/get/saveState/loadState/validateState).

- [ ] **Step 1: Tulis failing test `tests/port/custom_value_test.cpp`**:

```cpp
#include <assert.h>
#include <string.h>
#include "osupad_via_adapters.h"

static int applyCount = 0;
static void onApply() { applyCount++; }

int main() {
  OsupadRgbState rgb = {0, 0, 0, 0, 0};
  uint8_t defaultLayer = 0;
  OsupadCustomValue cv(rgb, defaultLayer, &onApply);

  // set channel 2 qmk_rgblight
  uint8_t p[via::kPacketSize];
  memset(p, 0, sizeof(p));
  p[0] = 0x07; p[1] = 0x02; p[2] = 0x01; p[3] = 42;  // brightness 42
  assert(cv.set(p));
  assert(rgb.brightness == 42);
  p[2] = 0x02; p[3] = 9;                              // effect 9
  assert(cv.set(p));
  assert(rgb.effect == 9);
  p[2] = 0x04; p[3] = 10; p[4] = 200;                 // hue/saturation
  assert(cv.set(p));
  assert(rgb.hue == 10 && rgb.saturation == 200);
  assert(applyCount == 3);
  p[2] = 0x99;                                        // unknown channel -> reject
  assert(!cv.set(p));

  // get
  memset(p, 0, sizeof(p));
  p[0] = 0x08; p[1] = 0x02; p[2] = 0x01;
  assert(cv.get(p));
  assert(p[3] == 42);

  // save/load round trip
  rgb = {1, 2, 3, 4, 5};
  defaultLayer = 3;
  uint8_t state[kCustomBytes];
  assert(cv.saveState(state, kCustomBytes));
  assert(!cv.saveState(state, kCustomBytes - 1));
  OsupadRgbState rgb2 = {0, 0, 0, 0, 0};
  uint8_t dl2 = 0;
  OsupadCustomValue cv2(rgb2, dl2, nullptr);
  assert(cv2.loadState(state, kCustomBytes));
  assert(rgb2.brightness == 1 && rgb2.effect == 2 && rgb2.speed == 3 &&
         rgb2.hue == 4 && rgb2.saturation == 5 && dl2 == 3);
  assert(!cv2.loadState(state, kCustomBytes - 1));
  assert(cv2.validateState(state, kCustomBytes));
  assert(!cv2.validateState(state, 5));

  return 0;
}
```

- [ ] **Step 2: Run, pastikan FAIL** (job `port-tests`; link error `OsupadCustomValue` tidak ada).

- [ ] **Step 3: Implement `OsupadCustomValue` di `osupad_via_adapters.cpp`**:

```cpp
OsupadCustomValue::OsupadCustomValue(OsupadRgbState& rgb, uint8_t& defaultLayer,
                                     void (*onApply)())
    : rgb_(rgb), defaultLayer_(defaultLayer), onApply_(onApply) {}

void OsupadCustomValue::apply() {
  if (onApply_) onApply_();
}

bool OsupadCustomValue::set(uint8_t packet[via::kPacketSize]) {
  if (packet[1] != 0x02) return false;
  switch (packet[2]) {
    case 0x01: rgb_.brightness = packet[3]; break;
    case 0x02: rgb_.effect = packet[3]; break;
    case 0x03: rgb_.speed = packet[3]; break;
    case 0x04: rgb_.hue = packet[3]; rgb_.saturation = packet[4]; break;
    default: return false;
  }
  apply();
  return true;
}

bool OsupadCustomValue::get(uint8_t packet[via::kPacketSize]) {
  if (packet[1] != 0x02) return false;
  switch (packet[2]) {
    case 0x01: packet[3] = rgb_.brightness; break;
    case 0x02: packet[3] = rgb_.effect; break;
    case 0x03: packet[3] = rgb_.speed; break;
    case 0x04: packet[3] = rgb_.hue; packet[4] = rgb_.saturation; break;
    default: return false;
  }
  return true;
}

bool OsupadCustomValue::saveState(uint8_t* output, size_t length) const {
  if (length != kCustomBytes) return false;
  output[0] = rgb_.brightness;
  output[1] = rgb_.effect;
  output[2] = rgb_.speed;
  output[3] = rgb_.hue;
  output[4] = rgb_.saturation;
  output[5] = defaultLayer_;
  return true;
}

bool OsupadCustomValue::loadState(const uint8_t* input, size_t length) {
  if (length != kCustomBytes) return false;
  rgb_.brightness = input[0];
  rgb_.effect = input[1];
  rgb_.speed = input[2];
  rgb_.hue = input[3];
  rgb_.saturation = input[4];
  defaultLayer_ = input[5];
  apply();
  return true;
}

bool OsupadCustomValue::validateState(const uint8_t*, size_t length) const {
  return length == kCustomBytes;
}
```

- [ ] **Step 4: Run, pastikan PASS; commit**

```bash
git add FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.cpp tests/port/custom_value_test.cpp
git commit -m "feat(osupad): add OsupadCustomValue for RGB and default layer"
```

---

### Task 4: OsupadStorage (dual-page, single-valid-slot)

**Files:**
- Modify: `FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.cpp`
- Create: `tests/port/storage_test.cpp`

**Interfaces:**
- Consumes: `osupadCrc32`, `osupadConvertRecord`, `via::FlashMemory`, konstanta.
- Produces: `OsupadStorage` (`begin/read/write/commit/erase`), `stm32_flash_memory.h` di Task 5.

- [ ] **Step 1: Tulis failing test `tests/port/storage_test.cpp`**:

```cpp
#include <assert.h>
#include <string.h>
#include "osupad_via_adapters.h"

class FakeFlash : public via::FlashMemory {
 public:
  FakeFlash() { memset(mem_, 0xFF, sizeof(mem_)); }
  bool read(uint32_t addr, void* out, uint16_t length) override {
    uint32_t off = addr - kStoragePageA;
    if (off + length > sizeof(mem_)) return false;
    memcpy(out, mem_ + off, length);
    return true;
  }
  bool write(uint32_t addr, const void* data, uint16_t length) override {
    uint32_t off = addr - kStoragePageA;
    if (off + length > sizeof(mem_)) return false;
    memcpy(mem_ + off, data, length);
    return true;
  }
  bool erasePage(uint32_t addr) override {
    uint32_t off = addr - kStoragePageA;
    if (off + kStoragePageBytes > sizeof(mem_)) return false;
    memset(mem_ + off, 0xFF, kStoragePageBytes);
    return true;
  }
  bool commit() override { return true; }
  uint8_t* mem() { return mem_; }
 private:
  uint8_t mem_[2 * kStoragePageBytes];
};

static void makeViaaRecord(uint8_t* rec, uint8_t mark) {
  memset(rec, 0, kRecordSize);
  rec[0] = (uint8_t)(kViaaMagic & 0xFF);
  rec[1] = (uint8_t)((kViaaMagic >> 8) & 0xFF);
  rec[2] = (uint8_t)((kViaaMagic >> 16) & 0xFF);
  rec[3] = (uint8_t)((kViaaMagic >> 24) & 0xFF);
  rec[4] = 2; rec[5] = 0;
  rec[6] = (uint8_t)(kPayloadBytes & 0xFF);
  rec[7] = (uint8_t)(kPayloadBytes >> 8);
  for (size_t i = kStateHeaderSize; i < kRecordSize; ++i) rec[i] = mark;
  uint32_t crc = osupadCrc32(rec + kStateHeaderSize, kPayloadBytes);
  rec[8] = (uint8_t)(crc & 0xFF);
  rec[9] = (uint8_t)((crc >> 8) & 0xFF);
  rec[10] = (uint8_t)((crc >> 16) & 0xFF);
  rec[11] = (uint8_t)((crc >> 24) & 0xFF);
}

int main() {
  static uint8_t buf[kRecordSize];
  uint8_t tmp[kRecordSize];
  uint8_t rec[kRecordSize];
  makeViaaRecord(rec, 0x5A);

  // Fresh flash (all 0xFF): begin ok, read fails -> Protocol.load would reset
  FakeFlash flash;
  OsupadStorage storage(flash, buf, sizeof(buf));
  assert(storage.begin());
  assert(!storage.read(0, tmp, sizeof(tmp)));

  // First save: no erase() needed, write lazily prepares provisional
  assert(storage.write(0, rec, kRecordSize));
  assert(storage.commit());
  memset(tmp, 0, sizeof(tmp));
  assert(storage.read(0, tmp, sizeof(tmp)));
  assert(memcmp(tmp, rec, kRecordSize) == 0);
  // exactly one slot is valid after commit (the other erased)
  int ffA = 0, ffB = 0;
  for (int i = 0; i < (int)kStoragePageBytes; ++i) {
    if (flash.mem()[i] == 0xFF) ffA++;
    if (flash.mem()[kStoragePageBytes + i] == 0xFF) ffB++;
  }
  assert(ffA == (int)kStoragePageBytes || ffB == (int)kStoragePageBytes);

  // Second save via factoryReset-style erase()
  uint8_t rec2[kRecordSize];
  makeViaaRecord(rec2, 0xA5);
  assert(storage.erase());
  assert(storage.write(0, rec2, kRecordSize));
  assert(storage.commit());
  memset(tmp, 0, sizeof(tmp));
  assert(storage.read(0, tmp, sizeof(tmp)));
  assert(memcmp(tmp, rec2, kRecordSize) == 0);

  // Interrupted commit: after commit2 exactly one slot is erased (the next
  // provisional). Corrupt it with garbage as if a crash wrote there mid-commit;
  // the intact rec2 must still load.
  uint8_t garbage[kStateHeaderSize];
  memset(garbage, 0x42, sizeof(garbage));
  bool aErased = true, bErased = true;
  for (int i = 0; i < (int)kStoragePageBytes; ++i) {
    if (flash.mem()[i] != 0xFF) aErased = false;
    if (flash.mem()[kStoragePageBytes + i] != 0xFF) bErased = false;
  }
  assert(aErased != bErased);  // exactly one erased slot after commit2
  assert(flash.write(aErased ? kStoragePageA : kStoragePageB, garbage, sizeof(garbage)));
  OsupadStorage storage2(flash, buf, sizeof(buf));
  assert(storage2.begin());
  memset(tmp, 0, sizeof(tmp));
  assert(storage2.read(0, tmp, sizeof(tmp)));
  assert(tmp[kStateHeaderSize] == 0xA5);  // intact rec2 still loads

  // Migration: plant OSVP v2 record in page A, page B erased
  FakeFlash flash3;
  uint8_t osvp[16 + kOsvpPayloadV2];
  memset(osvp, 0xFF, sizeof(osvp));
  uint32_t magic = kOsvpMagic;
  memcpy(osvp, &magic, 4);
  osvp[4] = 2; osvp[5] = 0;
  osvp[6] = (uint8_t)(kOsvpPayloadV2 & 0xFF); osvp[7] = (uint8_t)(kOsvpPayloadV2 >> 8);
  osvp[8] = 1; osvp[9] = 0; osvp[10] = 0; osvp[11] = 0;
  uint8_t* osvpPayload = osvp + kOsvpHeaderSize;
  for (int i = 0; i < (int)kOsvpPayloadV2; ++i) osvpPayload[i] = (uint8_t)(i & 0xFF);
  uint32_t crc2 = osupadCrc32(osvpPayload, kOsvpPayloadV2);
  memcpy(osvp + 12, &crc2, 4);
  assert(flash3.write(kStoragePageA, osvp, sizeof(osvp)));
  OsupadStorage storage3(flash3, buf, sizeof(buf));
  assert(storage3.begin());
  uint8_t migrated[kRecordSize];
  memset(migrated, 0, sizeof(migrated));
  assert(storage3.read(0, migrated, sizeof(migrated)));
  assert(migrated[0] == 0x41 && migrated[1] == 0x41 && migrated[2] == 0x49 && migrated[3] == 0x56);
  assert(memcmp(migrated + kStateHeaderSize, osvpPayload, 48) == 0);

  // write() rejects oversized
  assert(!storage3.write(0, rec, kRecordSize + 1));
  assert(!storage3.write(kRecordSize - 1, rec, 2));

  return 0;
}
```

- [ ] **Step 2: Run, pastikan FAIL**.

- [ ] **Step 3: Implement `OsupadStorage` di `osupad_via_adapters.cpp`**:

```cpp
namespace {
void writeU32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}
uint32_t readU32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
}  // namespace

OsupadStorage::OsupadStorage(via::FlashMemory& flash, uint8_t* recordBuffer, size_t recordBytes)
    : flash_(flash), buffer_(recordBuffer), bufferBytes_(recordBytes),
      activeSlot_(kStoragePageA), provisionalSlot_(kStoragePageB),
      activeValid_(false), provisionalReady_(false) {}

bool OsupadStorage::validAt(uint32_t addr) const {
  if (bufferBytes_ < kRecordSize) return false;
  uint8_t header[kStateHeaderSize];
  if (!flash_.read(addr, header, kStateHeaderSize)) return false;
  if (readU32(header) != kViaaMagic) return false;
  if (header[4] != (kViaaVersion & 0xFF) || header[5] != (uint8_t)(kViaaVersion >> 8)) return false;
  if (header[6] != (uint8_t)(kPayloadBytes & 0xFF) || header[7] != (uint8_t)(kPayloadBytes >> 8)) return false;
  if (!flash_.read(addr + kStateHeaderSize, buffer_, kPayloadBytes)) return false;
  return osupadCrc32(buffer_, kPayloadBytes) == readU32(header + 8);
}

uint32_t OsupadStorage::osvpSequenceAt(uint32_t addr) const {
  uint8_t header[kOsvpHeaderSize];
  if (!flash_.read(addr, header, kOsvpHeaderSize)) return 0;
  if (readU32(header) != kOsvpMagic) return 0;
  return readU32(header + 8);
}

bool OsupadStorage::readRecord(uint32_t addr, uint8_t* out) const {
  return flash_.read(addr, out, kRecordSize);
}

bool OsupadStorage::programRecord(uint32_t addr, const uint8_t* record) {
  if (!flash_.erasePage(addr)) return false;
  for (uint16_t off = 0; off < kRecordSize; off += 2) {
    const uint8_t lo = record[off];
    const uint8_t hi = off + 1 < kRecordSize ? record[off + 1] : 0xFF;
    const uint16_t word = (uint16_t)lo | ((uint16_t)hi << 8);
    if (!flash_.write(addr + off, &word, 2)) return false;
  }
  uint8_t verify[kRecordSize];
  if (!readRecord(addr, verify)) return false;
  if (memcmp(verify, record, kRecordSize) != 0) return false;
  return true;
}

bool OsupadStorage::begin() {
  const bool aValid = validAt(kStoragePageA);
  const bool bValid = validAt(kStoragePageB);
  if (aValid && bValid) {
    activeSlot_ = kStoragePageA;
    activeValid_ = true;
    provisionalSlot_ = kStoragePageB;
    return true;
  }
  if (aValid) { activeSlot_ = kStoragePageA; activeValid_ = true; provisionalSlot_ = kStoragePageB; return true; }
  if (bValid) { activeSlot_ = kStoragePageB; activeValid_ = true; provisionalSlot_ = kStoragePageA; return true; }

  // Migration: convert oldest-format record in either page to VIAA.
  uint8_t oldRecord[16 + kOsvpPayloadV2];
  const uint32_t seqA = osvpSequenceAt(kStoragePageA);
  const uint32_t seqB = osvpSequenceAt(kStoragePageB);
  if (seqA || seqB) {
    const uint32_t oldAddr = (seqA >= seqB) ? kStoragePageA : kStoragePageB;
    if (!readRecord(oldAddr, oldRecord)) return false;
    const uint16_t ps = (uint16_t)(oldRecord[6] | (oldRecord[7] << 8));
    size_t oldLen;
    if (ps == kOsvpPayloadV2) oldLen = kOsvpHeaderSize + kOsvpPayloadV2;
    else if (ps == kOsvpPayloadLegacy) oldLen = kOsvpHeaderSize + kOsvpPayloadLegacy;
    else return false;
    size_t outLen = 0;
    if (!osupadConvertRecord(oldRecord, oldLen, buffer_, bufferBytes_, &outLen)) return false;
    if (!programRecord(oldAddr, buffer_)) return false;
    activeSlot_ = oldAddr;
    provisionalSlot_ = (oldAddr == kStoragePageA) ? kStoragePageB : kStoragePageA;
    activeValid_ = true;
    provisionalReady_ = false;
    return true;
  }

  activeValid_ = false;
  return true;  // empty or corrupt flash: Protocol load() will fail -> resetBuffers()
}

size_t OsupadStorage::capacity() const { return kRecordSize; }

bool OsupadStorage::read(size_t offset, uint8_t* output, size_t length) {
  if (!activeValid_ || length == 0) return false;
  if (offset > kRecordSize || length > kRecordSize - offset) return false;
  return flash_.read(activeSlot_ + (uint32_t)offset, output, (uint16_t)length);
}

bool OsupadStorage::write(size_t offset, const uint8_t* input, size_t length) {
  if (offset > kRecordSize || length > kRecordSize - offset) return false;
  if (!provisionalReady_) {  // lazy provisional: Protocol.save() never calls erase()
    provisionalSlot_ = (activeSlot_ == kStoragePageA) ? kStoragePageB : kStoragePageA;
    memset(buffer_, 0, kRecordSize);
    provisionalReady_ = true;
  }
  memcpy(buffer_ + offset, input, length);
  return true;
}

bool OsupadStorage::erase() {
  provisionalSlot_ = (activeSlot_ == kStoragePageA) ? kStoragePageB : kStoragePageA;
  memset(buffer_, 0, kRecordSize);
  provisionalReady_ = true;
  return true;
}

bool OsupadStorage::commit() {
  if (!provisionalReady_) return false;
  if (!programRecord(provisionalSlot_, buffer_)) return false;
  const uint32_t oldSlot = activeSlot_;
  activeSlot_ = provisionalSlot_;
  provisionalSlot_ = oldSlot;
  // single valid slot: erase the previous active page so a boot can never
  // face two valid records with different content.
  if (!flash_.erasePage(oldSlot)) {
    activeValid_ = false;
    return false;
  }
  provisionalReady_ = false;
  activeValid_ = true;
  return true;
}
```

Catatan desain (single-valid-slot): commit menulis record penuh ke provisional lalu meng-eraser slot aktif lama, sehingga setiap saat hanya satu slot valid — tidak perlu sequence, dan crash di titik mana pun menyisakan ≥1 slot valid. Endurance ~10K save (2 erase/save), jauh di atas pemakaian macropad.

- [ ] **Step 4: Run, pastikan PASS; commit**

```bash
git add FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.cpp tests/port/storage_test.cpp
git commit -m "feat(osupad): add dual-page flash storage with OSVP migration"
```

---

### Task 5: Stm32FlashMemory + rewire .ino + update verify script

**Files:**
- Create: `FIRMWARE/OSUpadCloneVIA/stm32_flash_memory.h`
- Modify: `FIRMWARE/OSUpadCloneVIA/via_raw_hid.h`
- Modify: `FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino`
- Modify: `tools/verify_osupad_clone_via.py`

**Interfaces:**
- Consumes: `OsupadTransport` (new), `OsupadStorage`, `OsupadCustomValue`, `via::Protocol`, `via::FlashMemory`.
- Produces: sketch yang compile di CI (core libmaple + VIA-Arduino).

- [ ] **Step 1: Create `stm32_flash_memory.h`** (header-only, pakai libmaple `FLASH_*`):

```cpp
#pragma once

#include <Arduino.h>
#include <libmaple/flash.h>
#include "osupad_via_adapters.h"

class Stm32FlashMemory : public via::FlashMemory {
 public:
  bool read(uint32_t addr, void* out, uint16_t length) override {
    memcpy(out, reinterpret_cast<const void*>(addr), length);
    return true;
  }
  bool write(uint32_t addr, const void* data, uint16_t length) override {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    noInterrupts();
    FLASH_Unlock();
    for (uint16_t off = 0; off < length; off += 2) {
      const uint16_t word = (uint16_t)bytes[off] | ((uint16_t)bytes[off + 1] << 8);
      if (FLASH_ProgramHalfWord(addr + off, word) != FLASH_COMPLETE) {
        FLASH_Lock();
        interrupts();
        return false;
      }
    }
    FLASH_Lock();
    interrupts();
    return true;
  }
  bool erasePage(uint32_t addr) override {
    noInterrupts();
    FLASH_Unlock();
    const bool ok = FLASH_ErasePage(addr) == FLASH_COMPLETE;
    FLASH_Lock();
    interrupts();
    return ok;
  }
  bool commit() override { return true; }
};
```

- [ ] **Step 2: Tambah `OsupadTransport` di `via_raw_hid.h`** (append setelah deklarasi fungsi yang ada):

```cpp
#include "VIA_Protocol.h"

class OsupadTransport : public via::Transport {
 public:
  bool receive(uint8_t packet[via::kPacketSize]) override { return via_raw_hid_receive(packet); }
  bool send(const uint8_t packet[via::kPacketSize]) override { return via_raw_hid_send(packet); }
};
```

Pastikan `via_raw_hid.h` tetap punya include guard `#pragma once` dan deklarasi `via_raw_hid_receive/send` mendahului class.

- [ ] **Step 3: Rewire `OSUpadCloneVIA.ino`**

Include baru (tambah di blok include):

```cpp
#include <VIA_Protocol.h>
#include "osupad_via_adapters.h"
#include "stm32_flash_memory.h"
```

**HAPUS** dari `.ino` (fungsi/struktur/konstanta berikut, yang sudah pindah ke adapters/Protocol):
- Konstanta `SETTINGS_PAGE_A`, `SETTINGS_PAGE_B`, `SETTINGS_PAGE_BYTES`, `SETTINGS_MAGIC`, `SETTINGS_VERSION`, `SETTINGS_V1_VERSION`.
- Struct `PersistentPayload`, `LegacyPersistentPayload`, `PersistentImage`, `LegacyPersistentImage` + `static_assert`-nya.
- Fungsi `crc32`, `settings_image_valid`, `settings_v1_image_valid`, `legacy_settings_image_valid`, `migrate_v1_rgb_effect`, `settings_load`, `settings_commit`, `settings_mark_dirty`.
- Variabel `settings_sequence`, `settings_active_page`, `settings_dirty`, `settings_save_at`.
- `keymap_reset`, `macro_reset`, `copy_keymap_to_buffer`, `copy_buffer_to_keymap`, `via_set_rgblight`, `via_get_rgblight`, `handle_via`.
- Konstanta `SETTINGS_SAVE_DELAY_MS` tetap (dipakai local-dirty), `SETTINGS_RETRY_DELAY_MS` tetap (cadence retry).

**UBAH** di `.ino`:
1. `keymap` dan `default_keymap` jadi flat:
```cpp
static uint16_t keymap[LAYER_COUNT * KEY_COUNT];
static const uint16_t default_keymap[LAYER_COUNT * KEY_COUNT] = {
    0x0004, 0x0005, 0x0006, 0x0008, 0x0009, 0x000A, // A B C E F G
    0x0014, 0x001A, 0x001B, 0x001D, 0x001C, 0x0018, // Q W X Z Y U
    0x7700, 0x7701, 0x7702, 0x7703, 0x7704, 0x7705, // QMK Macro 0..5
    0x004F, 0x0050, 0x0051, 0x0052, 0x002C, 0x0029, // arrows, space, esc
};
```
2. `rgb` bertipe `OsupadRgbState` (definisi dari adapters):
```cpp
static OsupadRgbState rgb = {48, 1, 80, 0, 255};
static const OsupadRgbState default_rgb = {48, 1, 80, 0, 255};
```
3. `resolved_keycode` — ganti indeks `keymap[layer][key]` → `keymap[layer * KEY_COUNT + key]` (dua tempat: loop layer aktif dan fallback default_layer). Guard `keycode != 0x0001` tetap (KC_TRNS).
4. Semua pemanggil `settings_mark_dirty()` (di `rgb_set_effect`, `process_rgb_keycode`, blok DF/PDF/PDF pada `send_keycode`) → ganti dengan `markLocalDirty()`.
5. `setup()` — ganti blok `keymap_reset(); macro_reset(); settings_load();` menjadi init Protocol:
```cpp
  pinMode(PA5, OUTPUT);
  digitalWrite(PA5, LOW);
  rgb_render();
  protocol.begin(millis());
```
(Urutan: pin PA5 sebelum `protocol.begin()` karena `loadState` memanggil `apply()` → `rgb_render()`.)
6. `loop()` — ganti baris
```cpp
  uint8_t report[RAW_REPORT_BYTES];
  if (via_raw_hid_receive(report)) handle_via(report);
```
menjadi
```cpp
  protocol.task(millis());
```
dan tambahkan save lokal setelah blok scan/tap-hold:
```cpp
  if (local_dirty && (int32_t)(millis() - local_save_at) >= 0) {
    if (protocol.save()) local_dirty = false;
    else local_save_at = millis() + SETTINGS_RETRY_DELAY_MS;
  }
```

**TAMBAH** di `.ino` (sebelum `setup()`, setelah `macro_buffer` dan engine rgb/keyboard didefinisikan):

```cpp
// --- VIA-Arduino protocol wiring ---
static bool local_dirty = false;
static uint32_t local_save_at = 0;
static void markLocalDirty() {
  local_dirty = true;
  local_save_at = millis() + SETTINGS_SAVE_DELAY_MS;
}

class OsupadCallbacks : public via::Callbacks {
 public:
  uint32_t matrixRow(uint8_t row) const override {
    if (row >= 2) return 0;
    return (stable_state[row * 3] ? 1 : 0) |
           (stable_state[row * 3 + 1] ? 2 : 0) |
           (stable_state[row * 3 + 2] ? 4 : 0);
  }
  void deviceIndication(uint8_t) override {
    device_indication = !device_indication;
    rgb_render();
  }
};

static void applyRgb(const OsupadRgbState&) { rgb_render(); }

OsupadTransport transport;
Stm32FlashMemory flashMemory;
static uint8_t storageBuffer[kRecordSize];
OsupadStorage storage(flashMemory, storageBuffer, sizeof(storageBuffer));
static uint8_t loadBuffer[kPayloadBytes];
OsupadCustomValue customValue(rgb, default_layer, &applyRgb);
OsupadCallbacks callbacks;

via::Config protocolConfig = via::Config(
    2, 3, LAYER_COUNT, keymap, default_keymap,
    macro_buffer, MACRO_BYTES, MACRO_COUNT, 1, SETTINGS_SAVE_DELAY_MS,
    0, 0, nullptr, nullptr,
    loadBuffer, sizeof(loadBuffer), true, true, false);

via::Protocol protocol(protocolConfig, transport, &storage, &customValue, &callbacks);
```

Catatan:
- `RAW_REPORT_BYTES` tidak lagi dipakai untuk `handle_via`; biarkan konstanta tetap ada (dipakai `via_raw_hid`).
- `default_layer` tetap variabel engine; `OsupadCustomValue` memegang referensi sehingga `protocol.save()/load()` ikut mempersist-nya.
- `SETTINGS_SAVE_DELAY_MS = 750` dan `SETTINGS_RETRY_DELAY_MS = 1000` tetap.

- [ ] **Step 4: Update `tools/verify_osupad_clone_via.py`**

Ganti list token `.ino` menjadi (hapus token handle_via/settings_* lama, jaga yang masih ada):

```python
    sketch = sketch_path.read_text(encoding="utf-8")
    for token in (
        "MACRO_BYTES = 512",
        "#include <VIA_Protocol.h>",
        "via::Protocol protocol(protocolConfig",
        "protocol.task(millis())",
        "OsupadStorage storage(",
        "OsupadCustomValue customValue(",
        "0x29, 0x08",  # Mouse report exposes all eight QMK mouse buttons.
        "0x0A, 0x38, 0x02",  # Consumer AC Pan for horizontal scroll.
        "HID_CONSUMER_REPORT_DESCRIPTOR()",
        "HID_KEYBOARD_REPORT_DESCRIPTOR()",
        "HIDReporter SystemControl",
        "MOUSEKEY_INTERVAL_MS = 20",
        "usage >= 0xD1 && usage <= 0xD8",
        "MOUSE_WHEEL_LEFT",
        "MOUSE_WHEEL_RIGHT",
        "usage == 0xA5) bit = 1",  # KC_PWR
        "case 0x7833: rgb.effect = 35",  # QMK RGB test mode
        "case 0x7834: rgb.effect = 37",  # QMK twinkle mode
        "macro_buffer[MACRO_BYTES - 1] != 0",  # interrupted-write guard
        "resolved_keycode(uint8_t key)",
    ):
        require(sketch, token, sketch_path)
```

Tambah blok baru setelah cek `raw` (file adapters):

```python
    adapters_path = FIRMWARE / "osupad_via_adapters.cpp"
    adapters = adapters_path.read_text(encoding="utf-8")
    for token in (
        "0x08007800UL",     # settings page A
        "0x08007C00UL",     # settings page B
        "kOsvpMagic",       # OSVP migration
        "osupadConvertRecord",
        "kViaaMagic",
        "single valid slot",
    ):
        require(adapters, token, adapters_path)

    storage_path = FIRMWARE / "osupad_via_adapters.h"
    storage_h = storage_path.read_text(encoding="utf-8")
    for token in ("kViaaMagic = 0x56494141UL", "kOsvpMagic = 0x4F535650UL",
                  "class OsupadStorage", "class OsupadCustomValue"):
        require(storage_h, token, storage_path)
```

Dan update pesan akhir + docstring bila menyebut detail lama (mis. "storage: application ends before 0x08007800" tetap valid).

- [ ] **Step 5: Compile + CI**

```bash
git add FIRMWARE/OSUpadCloneVIA/stm32_flash_memory.h \
        FIRMWARE/OSUpadCloneVIA/via_raw_hid.h \
        FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino \
        tools/verify_osupad_clone_via.py
git commit -m "feat(osupad): swap VIA packet handling to VIA-Arduino protocol core"
git push
```

`gh run watch ...` — Expected: job `port-tests` PASS, job `build` PASS (compile libmaple + `--libraries` VIA-Arduino + verify script). Jika binary > 30720, kurangi: cek `opt` tetap `osstd`, dan pastikan tidak ada duplikasi (mis. struct lama masih ada).

- [ ] **Step 6: Jika verify script gagal karena token yang salah asumsi**, sesuaikan token dengan isi file aktual (baca file, cari string nyata) lalu amend/commit tambahan.

---

### Task 6: Docs

**Files:**
- Modify: `FIRMWARE/OSUpadCloneVIA/README.md`
- Modify: `FIRMWARE/OSUpadCloneVIA/RELEASE_QA.md`
- Modify: `FIRMWARE/OSUpadCloneVIA/FLASH_STLINK.md`

**Interfaces:**
- Consumes: struktur baru (VIA-Arduino dependency, adapter, storage format VIAA).

- [ ] **Step 1: `README.md`** — tambah bagian "Dependensi" dan ubah deskripsi build:

```markdown
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
```

- [ ] **Step 2: `RELEASE_QA.md`** — update:
- Angka build size → biarkan komentar "diisi dari hasil CI terakhir" (tulis nilai aktual dari CI run Task 5 bila tersedia).
- Tambahkan baris: "VIA protocol di-handle `via::Protocol` dari VIA-Arduino; storage dual-page single-valid-slot dengan migrasi OSVP→VIAA."
- Checklist produksi tetap (perilaku tidak berubah); item 10 dan 13 tetap valid (crash-safe + page-erase update mempertahankan settings).

- [ ] **Step 3: `FLASH_STLINK.md`** — perbarui kalimat format settings:

```markdown
Dua halaman 1 KiB terakhir (`0x08007800` dan `0x08007C00`) menyimpan setting
VIA dalam format VIA-Arduino (`StateHeader` + payload). Saat update dari
firmware rilis lama, settings otomatis dimigrasi dari format OSVP. Saat update,
gunakan erase halaman yang diperlukan bila ingin mempertahankan setting.
```

- [ ] **Step 4: Commit + push + cek CI**

```bash
git add FIRMWARE/OSUpadCloneVIA/README.md \
        FIRMWARE/OSUpadCloneVIA/RELEASE_QA.md \
        FIRMWARE/OSUpadCloneVIA/FLASH_STLINK.md
git commit -m "docs(osupad): document VIA-Arduino dependency and storage migration"
git push
```

`gh run watch ...` PASS.

---

### Task 7: Final review

**Files:**
- (semua file Task 1-6)

- [ ] **Step 1: Self-review** — baca diff `git diff origin/main...HEAD`, pastikan:
- Tidak ada sisa `handle_via`, `settings_load`, `settings_commit`, `PersistentImage`, `copy_keymap_to_buffer` di `.ino`.
- `via::Config` positional args benar (urutan sesuai `VIA_Protocol.h`).
- Parity engine utuh (mouse/consumer/system/RGB/macro/tap-hold/oneshot).
- Verify script dan CI hijau di run terakhir.

- [ ] **Step 2: Catat evidence** — tulis ringkasan compile acceptance (binary size, RAM) ke `RELEASE_QA.md` bila angka CI Task 5 belum dicatat. Commit bila ada perubahan.

- [ ] **Step 3: Selesai** — laporkan ke user: CI hijau, artifact `osupad-clone-via-stlink` berisi bin baru; hardware test mengikuti checklist RELEASE_QA.
