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
  if (packet[1] != 0x01 && packet[1] != 0x02) return false;
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
  if (packet[1] != 0x01 && packet[1] != 0x02) return false;
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

namespace {
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
  for (size_t off = 0; off < kRecordSize; off += 2) {
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
  // Workaround for High-Density 256KB clone chips where Page Size = 2KB:
  // 0x7800 and 0x7C00 fall into the same 2KB page, so programRecord's erase
  // already cleared oldSlot. Only erase oldSlot if it is not already empty.
  uint32_t oldMagic = 0;
  if (flash_.read(oldSlot, &oldMagic, 4) && oldMagic != 0xFFFFFFFFUL) {
    if (!flash_.erasePage(oldSlot)) {
      activeValid_ = false;
      return false;
    }
  }
  provisionalReady_ = false;
  activeValid_ = true;
  return true;
}
