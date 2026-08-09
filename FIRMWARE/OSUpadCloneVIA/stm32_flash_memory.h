#pragma once

#include <Arduino.h>
#include <libmaple/flash.h>
#include "osupad_via_adapters.h"

class Stm32FlashMemory : public via::FlashMemory {
 public:
  bool read(uint32_t addr, void* out, uint16_t length) override {
    memcpy(out, reinterpret_cast<const void*>(addr), length);
    return true;
  }
  bool write(uint32_t addr, const void* data, uint16_t length) override {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    noInterrupts();
    FLASH_Unlock();
    for (uint16_t off = 0; off < length; off += 2) {
      const uint16_t word = (uint16_t)bytes[off] | ((uint16_t)bytes[off + 1] << 8);
      if (FLASH_ProgramHalfWord(addr + off, word) != FLASH_COMPLETE) {
        FLASH_Lock();
        interrupts();
        return false;
      }
    }
    FLASH_Lock();
    interrupts();
    return true;
  }
  bool erasePage(uint32_t addr) override {
    noInterrupts();
    FLASH_Unlock();
    const bool ok = FLASH_ErasePage(addr) == FLASH_COMPLETE;
    FLASH_Lock();
    interrupts();
    return ok;
  }
  bool commit() override { return true; }
};
