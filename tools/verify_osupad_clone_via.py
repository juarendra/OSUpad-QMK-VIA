#!/usr/bin/env python3
"""Fast, dependency-free contract checks for the OSUpad clone firmware.

This intentionally complements compilation.  It guards the values that a
successful C++ build cannot prove: the VIA definition identity, Raw HID
collection, and the flash pages reserved for persistent settings.
"""

import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "FIRMWARE" / "OSUpadCloneVIA"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def require(text: str, needle: str, source: Path) -> None:
    if needle not in text:
        fail(f"{source.relative_to(ROOT)} must contain {needle!r}")


def main() -> None:
    definition_path = FIRMWARE / "via-definition.json"
    sketch_path = FIRMWARE / "OSUpadCloneVIA.ino"
    raw_path = FIRMWARE / "via_raw_hid.cpp"

    try:
        definition = json.loads(definition_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot parse {definition_path.relative_to(ROOT)}: {exc}")

    expected_definition = {
        "vendorId": "0x7877",
        "productId": "0x1004",
        "firmwareVersion": 1,
    }
    for key, value in expected_definition.items():
        if definition.get(key) != value:
            fail(f"VIA definition {key} must be {value!r}")
    if definition.get("matrix") != {"rows": 2, "cols": 3}:
        fail("VIA definition matrix must be exactly 2 rows by 3 columns")
    layout = definition.get("layouts", {}).get("keymap")
    if not isinstance(layout, list) or sum(len(row) for row in layout) != 6:
        fail("VIA definition must expose exactly six keys")

    sketch = sketch_path.read_text(encoding="utf-8")
    for token in (
        "MACRO_BYTES = 512",
        "LEGACY_MACRO_BYTES = 192",
        "SETTINGS_PAGE_A = 0x08007800UL",
        "SETTINGS_PAGE_B = 0x08007C00UL",
        "static_assert(sizeof(PersistentImage) <= SETTINGS_PAGE_BYTES",
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
        "case 0x0D: data[1] = MACRO_BYTES >> 8; data[2] = MACRO_BYTES",
        "case 0x0E: {",
        "case 0x0F: {",
    ):
        require(sketch, token, sketch_path)

    raw = raw_path.read_text(encoding="utf-8")
    for token in (
        "0x06, 0x60, 0xFF",  # QMK Raw HID usage page 0xFF60
        "0x09, 0x61",        # QMK Raw HID usage 0x61
        "constexpr uint8_t kPacketSize = 32",
        "USB_EP_TYPE_INTERRUPT",
    ):
        require(raw, token, raw_path)

    print("OSUpad Clone VIA contract: PASS")
    print("  VIA: 0x7877:0x1004, V3 definition, 2x3 matrix")
    print("  storage: application ends before 0x08007800; two 1 KiB settings pages")
    print("  Raw HID: vendor page 0xFF60, usage 0x61, 32-byte packets")
    print("  input: 8-button mouse, vertical/horizontal scroll, system controls")


if __name__ == "__main__":
    main()
