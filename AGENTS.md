# Repository Guidelines

## Project Overview

OSUpad-QMK-VIA contains firmware and release support for a 2x3, six-key OSUpad macropad with RGB lighting. The primary supported target is the OSUpad STM32F103 clone, using an Arduino/STM32duino/libmaple firmware with VIA v3 support because QMK/ChibiOS USB is unreliable on some clone chips. The repository also preserves a QMK firmware definition for the original STM32-based Positron OSUpad as a reference/buildable variant.

## Architecture & Data Flow

Two firmware paths coexist:

- **Clone release path:** `FIRMWARE/OSUpadCloneVIA/` is the supported production firmware. `OSUpadCloneVIA.ino` samples six GPIO key inputs, debounces state, resolves four layers and QMK-compatible keycodes, and emits keyboard, consumer/system, mouse, and Raw HID reports through STM32duino/libmaple USB. VIA v13 messages arrive through the Raw HID transport and are handled by `via::Protocol` from the external `VIA-Arduino` library.
- **Original/QMK path:** `FIRMWARE/positron/osupad/` is a QMK keyboard definition. QMK builds the hardware/keymap through `make`/`qmk`; its default keymap defines four six-key layers.

Clone firmware flow:

1. Six physical keys on `PB0`, `PA7`, `PA6`, `PB12`, `PB13`, and `PB14` are sampled and debounced.
2. `process_key()` resolves the active/default/oneshot layer and dispatches QMK-style keycodes, tap-hold/layer actions, macros, RGB controls, mouse, consumer, or system controls.
3. USBComposite/libmaple sends HID reports; VIA Raw HID remains a separate vendor-defined interface.
4. VIA keymap, macro, RGB custom value, and default-layer changes are staged in RAM and committed after the configured save delay.
5. `OsupadStorage` stores a CRC-protected VIA-Arduino record in one of two final 1 KiB flash pages (`0x08007800` and `0x08007C00`), preserving the last valid record across interrupted writes. Older OSVP records are migrated by `osupadConvertRecord()`.
6. RGB state is rendered to eight WS2812 LEDs on `PA5`; timing assumes exactly 72 MHz and uses the Cortex-M3 DWT cycle counter.

Important invariant: clone release binaries must stay within the first 30 KiB of the generic STM32F103C6 application region so the two final 1 KiB pages remain available for settings.

## Key Directories

- `FIRMWARE/OSUpadCloneVIA/` — supported Arduino clone firmware, VIA v3 definition, flash instructions, and release QA checklist.
- `FIRMWARE/positron/osupad/` — QMK keyboard definition and default keymap for the original STM32 OSUpad.
- `tests/port/` — dependency-light C++ port tests for storage, migration, CRC, and VIA custom values.
- `tools/` — repository contract/structure checks; currently `verify_osupad_clone_via.py`.
- `.github/workflows/` — separate Arduino clone and QMK build pipelines.
- `DOC/` — Indonesian user, USB-investigation, and hardware documentation.
- `HARDWARE/` — PCB, case, dimensions, and 3D model source files.
- `build/` — local/generated Arduino output; ignored by Git.

## Development Commands

### Clone firmware contract check

```bash
python3 tools/verify_osupad_clone_via.py
```

This validates the VIA definition identity/layout and required firmware/storage/Raw HID contract tokens without compiling C++.

### Clone firmware build

Install Arduino CLI, `stm32duino:STM32F1@2022.9.26`, USBComposite library `1.0.8`, and the external `VIA-Arduino` library. The CI-equivalent compile command is:

```bash
arduino-cli compile \
  --fqbn stm32duino:STM32F1:genericSTM32F103C6:upload_method=STLinkMethod,cpu_speed=speed_72mhz,opt=osstd \
  --libraries <arduino-library-path> \
  --libraries <path-to-VIA-Arduino> \
  --output-dir build \
  FIRMWARE/OSUpadCloneVIA
```

The resulting `build/OSUpadCloneVIA.ino.bin` must be no larger than `30720` bytes. Release binaries are direct ST-Link images programmed at `0x08000000`; they are not USB-bootloader images.

### Port tests

Each port test is compiled and run independently with C++11 and strict warnings:

```bash
for t in tests/port/*_test.cpp; do
  g++ -std=c++11 -Wall -Wextra -Werror \
    -I via-arduino/src -I FIRMWARE/OSUpadCloneVIA \
    "$t" FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.cpp -o port_test
  ./port_test
done
```

On Windows, use an equivalent shell/toolchain or run the commands in WSL. The repository does not define a package manifest or a local test runner.

### QMK reference firmware

With a QMK build environment:

```bash
make positron/osupad:via
make positron/osupad:via:flash
```

CI instead copies `FIRMWARE/positron/osupad/*` into `qmk_firmware/keyboards/positron/osupad/` and runs `qmk compile -kb positron/osupad -km via`.

## Code Conventions & Common Patterns

- Match the existing C++ style: small `static` helpers in the Arduino sketch, fixed-width integer types, compile-time constants for hardware/protocol limits, and explicit byte layouts for persisted data.
- Keep hardware/protocol adapters in `osupad_via_adapters.cpp/.h`; keep key scanning, HID dispatch, layers, macros, RGB, and the main `setup()`/`loop()` flow in `OSUpadCloneVIA.ino`.
- Preserve protocol compatibility with QMK/VIA numeric keycodes and byte order. Do not substitute arbitrary local keycode values.
- Treat persisted records as binary contracts: update sizes, offsets, magic/version values, CRC validation, migration, and tests together.
- Use boolean success/failure returns for storage, protocol, and HID boundary operations; reject malformed or unsupported packets rather than silently accepting them.
- Keep timing-sensitive WS2812 code deterministic. The build must use 72 MHz, and the transmitter must not acquire function-call delay overhead.
- Follow QMK conventions in the reference keymap: `LAYOUT(...)`, layer indices in brackets, and QMK keycode macros such as `MO(1)`, `TG(1)`, and `QK_MACRO_0`.
- Tests use `assert()` in standalone `main()` programs. Add focused boundary/invariant coverage for storage, migration, CRC, or protocol changes instead of introducing a framework.

## Important Files

- `FIRMWARE/OSUpadCloneVIA/OSUpadCloneVIA.ino` — clone firmware entry point and main runtime state machine.
- `FIRMWARE/OSUpadCloneVIA/osupad_via_adapters.h/.cpp` — VIA custom-value handling, flash storage, CRC, and OSVP-to-VIAA migration.
- `FIRMWARE/OSUpadCloneVIA/via_raw_hid.h/.cpp` — Raw HID transport used by VIA.
- `FIRMWARE/OSUpadCloneVIA/via-definition.json` — VIA v3 device identity, 2x3 matrix, and six-key layout.
- `FIRMWARE/OSUpadCloneVIA/FLASH_STLINK.md` — programming/recovery procedure and flash-layout warnings.
- `FIRMWARE/OSUpadCloneVIA/RELEASE_QA.md` — memory limits, protocol assumptions, and physical production checklist.
- `FIRMWARE/positron/osupad/keyboard.json` — QMK keyboard metadata and matrix/layout definition.
- `FIRMWARE/positron/osupad/keymaps/default/keymap.c` — default four-layer QMK keymap.
- `tools/verify_osupad_clone_via.py` — dependency-free firmware/JSON contract validation.
- `.github/workflows/build-osupad-clone-via.yml` — Arduino toolchain setup, contract check, size/timing guard, artifact upload, and port-test loop.
- `.github/workflows/build-qmk.yml` — QMK setup, source copy, compile, and artifact upload.

## Runtime/Tooling Preferences

- Clone builds require Arduino CLI, the pinned STM32duino STM32F1 core, USBComposite `1.0.8`, and the external `VIA-Arduino` library. CI obtains VIA-Arduino from `juarendra/VIA-Arduino` and installs the STM32F1 core at `2022.9.26`.
- QMK builds require the QMK CLI/build environment; there is no Node, Bun, npm, or Python package requirement in the repository.
- Python 3 is used for the standard-library-only contract script.
- Use a 72 MHz STM32F103C6/ST-Link configuration for the clone build. Keep the firmware’s application image below `0x08007800`.
- Flash clone releases with ST-Link at `0x08000000`. Do not use the QMK `FIRMWARE/osupad_via.bin` reference binary on the problematic clone target, and do not modify BOOT pins, Option Bytes, or RDP during normal flashing.
- Generated `.elf`, `.map`, `.hex`, and `build/` output are ignored; clone `.bin` release files under `FIRMWARE/OSUpadCloneVIA/` are also ignored and published as release artifacts.

## Testing & QA

- CI runs `python3 tools/verify_osupad_clone_via.py` before compiling the clone firmware.
- CI compiles every `tests/port/*_test.cpp` with `g++ -std=c++11 -Wall -Wextra -Werror`, links `osupad_via_adapters.cpp`, and executes each binary. Tests cover:
  - `crc_test.cpp` — packet/record size constants and known CRC32 behavior.
  - `custom_value_test.cpp` — VIA RGB packet formats, get/set behavior, state save/load, validation, and callback application.
  - `migration_test.cpp` — OSVP v1/v2 and legacy macro migration, RGB effect mapping, malformed input, and output-capacity rejection.
  - `storage_test.cpp` — dual-page validity, commit/erase behavior, CRC protection, and record selection (see the source for exact scenarios).
- Clone CI rejects binaries over 30,720 bytes and checks the disassembly for forbidden external calls to `ws2812_delay`, protecting fixed-cycle LED timing. It also emits SHA-256 checksums for the firmware binary and VIA JSON.
- QMK CI builds the `via` keymap but does not run a separate unit-test suite.
- Hardware QA is documented in `FIRMWARE/OSUpadCloneVIA/RELEASE_QA.md`: test all six switches, VIA remapping/macro persistence, layer and HID behaviors, RGB effects, repeated USB reconnects, a 30-minute soak, and page-erase versus mass-erase behavior.
- There is no configured coverage tool or reported coverage threshold. For changes to binary layout, keycodes, storage, USB descriptors, or timing, update the focused port/contract tests and the relevant QA checklist.
