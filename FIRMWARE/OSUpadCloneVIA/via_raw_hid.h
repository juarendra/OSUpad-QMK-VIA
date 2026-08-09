#pragma once

#include <stdint.h>

/*
 * Dedicated QMK-compatible Raw HID interface for VIA.
 *
 * This is intentionally separate from the boot keyboard HID interface.  VIA
 * selects the vendor-defined interface (usage page 0xFF60, usage 0x61) and
 * exchanges fixed 32-byte packets with report ID zero.
 */
bool via_raw_hid_register();
bool via_raw_hid_receive(uint8_t *data);
bool via_raw_hid_send(const uint8_t *data);

#include "VIA_Protocol.h"

class OsupadTransport : public via::Transport {
 public:
  bool receive(uint8_t packet[via::kPacketSize]) override { return via_raw_hid_receive(packet); }
  bool send(const uint8_t packet[via::kPacketSize]) override { return via_raw_hid_send(packet); }
};
