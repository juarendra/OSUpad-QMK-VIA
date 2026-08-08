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
