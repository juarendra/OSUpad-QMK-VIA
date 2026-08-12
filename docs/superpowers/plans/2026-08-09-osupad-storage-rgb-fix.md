# OSUpad Storage + RGB Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix two regressions on OSUpad Clone VIA port: keymap not surviving reboot,
and RGB stuck white / unchangeable from VIA.

**Architecture:** One-line call insertion in `setup()` to activate flash storage scanning
before protocol boot; one timer variable in `deviceIndication` callback to auto-clear the
flash-after-write indicator so RGB rendering is not permanently overridden.

**Tech Stack:** Arduino (libmaple), stm32duino STM32F1 core 2022.9.26, VIA-Arduino

## Global Constraints

- Base commit: `c6f06d6` (v1.1.2-clone-via).
- Modify only `FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino`; the adapter layer
  (`osupad_via_adapters.cpp`) is already correct.
- No changes to VIA-Arduino library.
- No changes to `via-definition.json`.
- Build FQBN: `stm32duino:STM32F1:genericSTM32F103C6:upload_method=STLinkMethod,cpu_speed=speed_72mhz,opt=osstd`
- Binary must stay below 30720 bytes.
- Post-fix, release as `v1.1.3-clone-via`.

---

### Task 1: Call `storage.begin()` before `protocol.begin()`

**Files:**
- Modify: `FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino:728`

**Interfaces:**
- Consumes: `OsupadStorage storage` (declared at line 711).
- Produces: `storage.begin()` scans flash pages A/B, sets `activeValid_` and
  `activeSlot_`, handles OSVP→VIAA migration if needed.
- `Protocol::begin()` at line 728 implicitly calls `load()` → `storage_->read()`
  which requires `activeValid_ == true` to succeed.

- [ ] **Step 1: Add the call**

Insert before `protocol.begin(millis());` (line 728):

```cpp
void setup() {
  pinMode(PA5, OUTPUT);
  digitalWrite(PA5, LOW);
  rgb_render();
  storage.begin();           // ← NEW: scan flash slots before protocol load
  protocol.begin(millis());
```

- [ ] **Step 2: Build**

```bash
arduino-cli compile --fqbn stm32duino:STM32F1:genericSTM32F103C6:upload_method=STLinkMethod,cpu_speed=speed_72mhz,opt=osstd \
  --libraries "$ARDUINO_LIBS" --libraries "$VIA_ARDUINO" --output-dir build \
  FIRMWARE/OSUpadCloneVIA
```

Expected: PASS, binary ≤ 30720 bytes.

- [ ] **Step 3: Commit**

```bash
git add FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino
git commit -m "fix(osupad): call storage.begin() before protocol begin

OsupadStorage::begin() scans flash pages A/B and sets activeValid_.
Without it, activeValid_ stays false, read() always fails, load()
always falls back to default keymap on every boot."
```

---

### Task 2: Auto-clear `device_indication` after flash

**Files:**
- Modify: `FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino:119, 697-699, 770-773`

**Interfaces:**
- Consumes: `deviceIndication(uint8_t)` callback (line 697), `rgb_render()` (line 168).
- Produces: when VIA sends `setDeviceIndication(1)`, LEDs flash white for 2 seconds
  then revert to normal RGB; a second call within the window resets the timer.

- [ ] **Step 1: Add `device_indication_until` to globals**

After line 119 (`static bool device_indication = false;`), add:

```cpp
static uint32_t device_indication_until = 0;
```

- [ ] **Step 2: Replace toggle with timed flag**

Replace lines 697-699:

```cpp
    void deviceIndication(uint8_t) override {
      device_indication = !device_indication;
      rgb_render();
    }
```

with:

```cpp
    void deviceIndication(uint8_t) override {
      device_indication = true;
      device_indication_until = millis() + 2000;
      rgb_render();
    }
```

- [ ] **Step 3: Expire the flag at the end of `loop()`**

At the bottom of `loop()` (after line 781, before closing `}`), add:

```cpp
    if (device_indication && (int32_t)(now - device_indication_until) >= 0) {
      device_indication = false;
      rgb_render();
    }
```

- [ ] **Step 4: Build and verify size**

Same compile command as Task 1. Expected: PASS, binary ≤ 30720 bytes.

- [ ] **Step 5: Commit**

```bash
git add FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino
git commit -m "fix(osupad): auto-clear device indication after 2 seconds

VIA sends setDeviceIndication(1) during discovery to flash the
keyboard. The old toggle callback could leave device_indication
stuck true, permanently overriding RGB output with white. Replace
the toggle with a 2-second timeout that auto-clears and re-renders
normal RGB."
```

---

### Task 3: Tag, Release, Flash

After both commits are verified and pushed:

- [ ] **Step 1: Tag**

```bash
git tag v1.1.3-clone-via
git push origin v1.1.3-clone-via
```

- [ ] **Step 2: Create GitHub Release**

```bash
gh release create v1.1.3-clone-via build/OSUpadCloneVIA.ino.bin \
  -R juarendra/OSUpad-QMK-VIA \
  -t "OSUpad Clone VIA v1.1.3" \
  -n "### Bug Fixes
- **Keymap/macro tidak hilang:** OsupadStorage::begin() sekarang dipanggil saat boot,
  mencegah fallback ke keymap default.
- **RGB bisa diubah:** device_indication sekarang auto-clear setelah 2 detik,
  tidak lagi memblokir kontrol warna/mode dari VIA.

Flash melalui ST-Link ke 0x08000000."
```

- [ ] **Step 3: Flash via ST-Link**

```bash
ST-LINK_CLI.exe -c SWD -P build/OSUpadCloneVIA.ino.bin 0x08000000 \
  -V after_programming -Rst -Run
```

- [ ] **Step 4: Acceptance test**

1. Load via-definition.json (atau gunakan cached definition dari PID 0x1000).
2. Di VIA, remap 2 key, set macro, ganti mode/warna RGB.
3. Tunggu 1 detik, cabut USB, colok lagi.
4. Keymap + macro + RGB harus tersimpan.
5. RGB harus responsif ke perubahan di tab Lighting.
