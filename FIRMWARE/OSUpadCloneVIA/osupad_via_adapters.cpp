#include "osupad_via_adapters.h"
#include <string.h>

uint32_t osupadCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1U)));
  }
  return ~crc;
}

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
  // VIAA payload layout: keymap(48) + macros(512) + layoutOptions(4) + custom(6).
  // Custom state (rgb5 + default_layer1) lives in the final 6 bytes, after
  // layoutOptions; layoutOptions itself stays zero.
  const size_t customBase = 48 + kMacroBytes + sizeof(uint32_t);  // 564
  outPayload[customBase + 0] = payload[48 + srcMacroBytes + 0];  // brightness
  outPayload[customBase + 1] = effect;                            // rgb.effect (migrated)
  outPayload[customBase + 2] = payload[48 + srcMacroBytes + 2];  // speed
  outPayload[customBase + 3] = payload[48 + srcMacroBytes + 3];  // hue
  outPayload[customBase + 4] = payload[48 + srcMacroBytes + 4];  // saturation
  outPayload[customBase + 5] = legacy ? 0 : payload[48 + srcMacroBytes + 5];  // default_layer

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
