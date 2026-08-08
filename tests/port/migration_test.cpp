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
  assert(out[kStateHeaderSize + 48 + 512 + 4 + 1] == expectedNew);  // migrated rgb.effect
  assert(out[kStateHeaderSize + 48 + 512 + 4 + 5] == 2);            // default_layer kept
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
  assert(out[kStateHeaderSize + 48 + 512 + 4 + 0] == 0x30);  // brightness
  assert(out[kStateHeaderSize + 48 + 512 + 4 + 1] == 21);    // effect unchanged (v2)
  assert(out[kStateHeaderSize + 48 + 512 + 4 + 5] == 3);     // default_layer

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
  assert(out[kStateHeaderSize + 48 + 512 + 4 + 5] == 0);      // default_layer 0

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
