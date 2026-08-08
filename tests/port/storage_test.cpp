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
