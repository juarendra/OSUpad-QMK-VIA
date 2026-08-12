# OSUpad WS2812 Fixed-Cycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the eight WS2812 LEDs on PA5 display the saved VIA RGB state correctly and persistently.

**Architecture:** Keep the VIA protocol, HSV renderer, keymap, macro, and flash storage intact. Replace only the PA5 WS2812 transmitter with Cortex-M3 DWT cycle-counter timing at 72 MHz. No PCB rewire, no new runtime dependency, no timer, SPI, or DMA.

**Tech Stack:** Arduino (libmaple), STM32duino STM32F1 core 2022.9.26, Cortex-M3 DWT, VIA-Arduino

## Global Constraints

- Build FQBN must be `stm32duino:STM32F1:genericSTM32F103C6:upload_method=STLinkMethod,cpu_speed=speed_72mhz,opt=osstd`.
- Binary must stay at or below `30720` bytes so it never reaches the settings pages `0x08007800`-`0x08007FFF`.
- Do not mass-erase flash. Preserve settings pages so saved keymap/macro/RGB survive.
- Preserve VIA protocol handling, HSV renderer effect mapping, keymap, macro, and OsupadStorage exactly.
- Do not modify `via-definition.json` beyond the V3 keycode module fix already applied.
- Interrupts stay disabled for one complete LED frame during `ws2812_write_byte()`.

---

### Task 1: Record Baseline

**Files:**
- Read: `FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino`
- Read: `FIRMWARE/OSUpadCloneVIA/via-definition.json`
- Read: `build/OSUpadCloneVIA.ino.bin`

**Interfaces:**
- Consumes: current git `main`, modified `via-definition.json`, active binary
- Produces: immutable baseline evidence

- [ ] **Step 1: Record git state**

```bash
git -C "D:\Pribadi\OSUpad-QMK-VIA" rev-parse --abbrev-ref HEAD
git -C "D:\Pribadi\OSUpad-QMK-VIA" status --short
git -C "D:\Pribadi\OSUpad-QMK-VIA" log --oneline -5
```

Expected: branch `main`, only `FIRMWARE/OSUpadCloneVIA/via-definition.json` modified, recent commits recorded.

- [ ] **Step 2: Record binary and settings evidence**

```powershell
Get-FileHash "D:\Pribadi\OSUpad-QMK-VIA\build\OSUpadCloneVIA.ino.bin" -Algorithm SHA256
```

Record the settings page dump evidence already captured in `C:\Users\jrjua\AppData\Local\Temp\opencode\osupad-page-a.bin`.

### Task 2: Write Failing Check Before Driver Fix

**Files:**
- Modify: `.github/workflows/build-osupad-clone-via.yml`
- Modify: `FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino`

**Interfaces:**
- Consumes: current `ws2812_write_byte()` implementation
- Produces: automated proof that the old implementation has function-call timing drift

- [ ] **Step 1: Add compile-time clock guard**

In `OSUpadCloneVIA.ino`, above `ws2812_write_byte()`:

```cpp
static_assert(F_CPU == 72000000L, "WS2812 timing requires 72 MHz");
```

- [ ] **Step 2: Build once and verify guard passes on 72 MHz**

Run the build with the exact FQBN. Expected: PASS.

- [ ] **Step 3: Add disassembly check**

Append to `.github/workflows/build-osupad-clone-via.yml` after the size check a step that runs:

```bash
arm-none-eabi-objdump -d build/OSUpadCloneVIA.ino.elf | grep -E "bl +[0-9a-f]+ <ws2812_delay" && exit 1 || exit 0
```

Expected: FAIL on the current build because `ws2812_write_byte()` calls `ws2812_delay()` via `bl`.

### Task 3: Replace the PA5 Transmitter

**Files:**
- Modify: `FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino`

**Interfaces:**
- Consumes: nothing from prior tasks
- Produces: `ws2812_begin()` and a cycle-accurate `ws2812_write_byte()`; delete `ws2812_delay()`

- [ ] **Step 1: Remove old delay and writer**

Delete `ws2812_delay()` entirely. Replace `ws2812_write_byte()` body with a DWT cycle-deadline writer.

- [ ] **Step 2: Add DWT init**

```cpp
static void ws2812_begin() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
  GPIOA->CRL = (GPIOA->CRL & ~(0xFUL << 20)) | (0x2UL << 20); /* PA5 out 2MHz */
  GPIOA->BRR = (1UL << 5);
}
```

- [ ] **Step 3: Add cycle-deadline writer**

Timing at 72 MHz (cycles for WS2812B):

- Bit period: `90` cycles (`1.25 us`)
- T0H: `25` cycles (`0.347 us`)
- T1H: `50` cycles (`0.694 us`)

```cpp
static void ws2812_write_byte(uint8_t value) {
  for (uint8_t bit = 0; bit < 8; ++bit) {
    const uint32_t start = DWT->CYCCNT;
    GPIOA->BSRR = (1UL << 5);
    const uint32_t highCycles = (value & 0x80) ? 50U : 25U;
    while ((DWT->CYCCNT - start) < highCycles) {}
    GPIOA->BRR = (1UL << 5);
    while ((DWT->CYCCNT - start) < 90U) {}
    value <<= 1;
  }
}
```

- [ ] **Step 4: Wire begin and renderer**

In `setup()`, replace `pinMode(PA5, OUTPUT); digitalWrite(PA5, LOW);` with:

```cpp
ws2812_begin();
```

Call `ws2812_begin()` before the first `rgb_render()`. Keep the GRB order in `rgb_render()`:

```cpp
ws2812_write_byte(g);
ws2812_write_byte(r);
ws2812_write_byte(b);
```

- [ ] **Step 5: Keep interrupts disabled for a frame**

In `rgb_render()`, keep the existing `noInterrupts()` around the LED loop and `interrupts()` after it, or move the `noInterrupts()`/`interrupts()` pair around the full 8-LED write.

### Task 4: Verify VIA Definition

**Files:**
- Modify: `FIRMWARE/OSUpadCloneVIA/via-definition.json`

**Interfaces:**
- Consumes: already-applied removal of V2-only `lighting`
- Produces: a valid VIA V3 `KeyboardDefinitionV3`

- [ ] **Step 1: Confirm final JSON**

```json
{
  "name": "OSUpad Clone VIA",
  "vendorId": "0x1209",
  "productId": "0x7050",
  "firmwareVersion": 1,
  "matrix": { "rows": 2, "cols": 3 },
  "keycodes": ["qmk_rgblight_keycodes"],
  "menus": ["qmk_rgblight"],
  "layouts": { "keymap": [["0,0","0,1","0,2"],["1,0","1,1","1,2"]] }
}
```

- [ ] **Step 2: Validate JSON parses**

```powershell
python -m json.tool FIRMWARE/OSUpadCloneVIA/via-definition.json
```

Expected: no error.

- [ ] **Step 3: Confirm no V2 `lighting` key**

Confirm `"lighting"` is absent.

### Task 5: Build and Run Automated Checks

**Files:**
- Run: `tools/verify_osupad_clone_via.py`
- Run: all `tests/port/*_test.cpp`
- Build: `build/OSUpadCloneVIA.ino.bin`

**Interfaces:**
- Consumes: modified firmware and definition
- Produces: verified binary below `30720` bytes with no `ws2812_delay`

- [ ] **Step 1: Run contract check**

```powershell
python tools/verify_osupad_clone_via.py
```

- [ ] **Step 2: Run port tests**

```powershell
g++ -std=c++11 -Wall -Wextra -Werror -I "D:\Pribadi\VIA-Arduino\src" -I FIRMWARE/OSUpadCloneVIA tests/port/custom_value_test.cpp FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.cpp -o port_test
.\port_test
```

Repeat for `crc_test.cpp`, `migration_test.cpp`, `storage_test.cpp`.

- [ ] **Step 3: Build with exact FQBN**

```bash
arduino-cli compile --fqbn stm32duino:STM32F1:genericSTM32F103C6:upload_method=STLinkMethod,cpu_speed=speed_72mhz,opt=osstd --libraries "$ARDUINO_LIBS" --libraries "D:\Pribadi\VIA-Arduino" --output-dir build FIRMWARE/OSUpadCloneVIA
```

- [ ] **Step 4: Verify binary size**

```powershell
$b = Get-Item "D:\Pribadi\OSUpad-QMK-VIA\build\OSUpadCloneVIA.ino.bin"; $b.Length -le 30720
```

- [ ] **Step 5: Verify no `ws2812_delay` symbol**

```powershell
arm-none-eabi-nm build/OSUpadCloneVIA.ino.elf | findstr ws2812_delay
```

Expected: no match.

- [ ] **Step 6: Verify no branch-with-link inside writer**

```powershell
arm-none-eabi-objdump -d build/OSUpadCloneVIA.ino.elf | Select-String "bl .*ws2812"
```

Expected: no match.

### Task 6: Flash via ST-Link

**Files:**
- Write: `build/OSUpadCloneVIA.ino.bin` to STM32 flash `0x08000000`

**Interfaces:**
- Consumes: verified binary from Task 5
- Produces: target running the fixed-cycle driver

- [ ] **Step 1: Program, verify, reset, run**

```powershell
& "$env:LOCALAPPDATA\Arduino15\packages\stm32duino\tools\STM32Tools\2022.9.26\win\stlink\ST-LINK_CLI.exe" -c SWD -P "D:\Pribadi\OSUpad-QMK-VIA\build\OSUpadCloneVIA.ino.bin" 0x08000000 -V after_programming -Rst -Run
```

- [ ] **Step 2: Compare target flash to binary**

```powershell
& "$env:LOCALAPPDATA\Arduino15\packages\stm32duino\tools\STM32Tools\2022.9.26\win\stlink\ST-LINK_CLI.exe" -c SWD HOTPLUG -CmpFile "D:\Pribadi\OSUpad-QMK-VIA\build\OSUpadCloneVIA.ino.bin" 0x08000000
```

Expected: `No difference found`.

- [ ] **Step 3: Verify clock and settings preserved**

```powershell
& "...\ST-LINK_CLI.exe" -c SWD HOTPLUG -r32 0x40021000 8
```

Expected `RCC_CFGR` selects PLL/HSE `x9` = 72 MHz.

```powershell
& "...\ST-LINK_CLI.exe" -c SWD HOTPLUG -r32 0x08007800 4
```

Expected: `0x56494141` still present, settings preserved.

### Task 7: Hardware Color Test

**Files:**
- None

**Interfaces:**
- Consumes: flashed firmware and LED hardware
- Produces: visual confirmation per effect

- [ ] **Step 1: Power via USB after unplugging ST-Link**

Use USB data cable. Do not power target from both USB and ST-Link.

- [ ] **Step 2: Verify static colors**

Test in order:

```text
effect 1, hue 0,   sat 255 -> red
effect 1, hue 85,  sat 255 -> green
effect 1, hue 170, sat 255 -> blue
effect 0 -> all LEDs off
brightness 32 / 128 / 255 -> brightness changes
```

- [ ] **Step 3: Verify all eight LEDs**

Confirm all eight LEDs update together with no white-forced overlay and no flicker.

### Task 8: VIA End-to-End Test

**Files:**
- Use: `FIRMWARE/OSUpadCloneVIA/via-definition.json`

**Interfaces:**
- Consumes: flashed firmware and VIA Design
- Produces: RGB controls working live and persisted

- [ ] **Step 1: Open VIA Design**

Enable **Show Design Tab**, open **Design**, keep **Use V2 definitions** off, remove stale draft, load the V3 JSON.

- [ ] **Step 2: Change brightness, effect, speed, hue, saturation**

Confirm LEDs change immediately.

- [ ] **Step 3: Wait one second, unplug, replug**

Confirm saved RGB state is restored.

- [ ] **Step 4: Verify keymap and macro survive**

Remap a key and create a macro; unplug and replug; confirm both persist.

- [ ] **Step 5: Read settings page via ST-Link**

Reconnect ST-Link, dump `0x08007800` `0x400`; confirm the saved RGB bytes at offset `0x240` match the last VIA values.

### Task 9: Docs, Commit, and Release

**Files:**
- Modify: `FIRMWARE/OSUpadCloneVIA/RELEASE_QA.md`
- Modify: `CHANGELOG.md`

**Interfaces:**
- Consumes: all prior verified results
- Produces: documented, committed fix

- [ ] **Step 1: Update `RELEASE_QA.md`**

Add a line noting the WS2812 transmitter now uses DWT cycle timing at 72 MHz and that PA5 is not SPI/timer/DMA.

- [ ] **Step 2: Update `CHANGELOG.md`**

Add `v1.1.5-clone-via` entry describing the DWT fixed-cycle WS2812 transmitter fix.

- [ ] **Step 3: Commit**

```bash
git -C "D:\Pribadi\OSUpad-QMK-VIA" add FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino FIRMWARE/OSUpadCloneVIA/via-definition.json FIRMWARE/OSUpadCloneVIA/RELEASE_QA.md CHANGELOG.md .github/workflows/build-osupad-clone-via.yml docs/superpowers/plans/2026-08-12-osupad-ws2812-fixed-cycle.md
git -C "D:\Pribadi\OSUpad-QMK-VIA" commit -m "fix(osupad): replace PA5 WS2812 bit-bang with DWT cycle timing"
```

- [ ] **Step 4: Tag and release**

Tag `v1.1.5-clone-via`, create GitHub release, attach `build/OSUpadCloneVIA.ino.bin` and `via-definition.json`.

### Task 10: Rollback Path

- [ ] **Step 1: If LED regression**

Reflash the previous binary with ST-Link. Do not mass-erase.

- [ ] **Step 2: If settings lost**

Restore the baseline binary; settings pages were never erased during this plan.
