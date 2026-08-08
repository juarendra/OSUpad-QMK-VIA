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
