#include <Arduino.h>
#include <USBComposite.h>

#include <usb_core.h>
#include <usb_def.h>
#include <usb_hid.h>
#include <usb_type.h>

#include "via_raw_hid.h"

namespace {

constexpr uint8_t kPacketSize = 32;

/* This is QMK's Raw HID collection, with no report ID.  Its own HID
 * interface is the detail VIA needs; putting it in the keyboard interface
 * makes WebHID enumerate but prevents VIA's initial handshake. */
static const uint8_t raw_report_descriptor[] = {
    0x06, 0x60, 0xFF,             // Usage Page: vendor 0xFF60
    0x09, 0x61,                   // Usage: 0x61
    0xA1, 0x01,                   // Collection: Application
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, kPacketSize,
    0x09, 0x62,
    0x81, 0x02,                   // Input: 32 bytes
    0x95, kPacketSize,
    0x09, 0x63,
    0x91, 0x02,                   // Output: 32 bytes
    0xC0,
};

struct __attribute__((packed)) RawHidDescriptor {
    usb_descriptor_interface interface;
    HIDDescriptor hid;
    usb_descriptor_endpoint in_endpoint;
    usb_descriptor_endpoint out_endpoint;
};

static const RawHidDescriptor raw_hid_descriptor = {
    {
        sizeof(usb_descriptor_interface), USB_DESCRIPTOR_TYPE_INTERFACE,
        0, 0, 2, USB_INTERFACE_CLASS_HID, 0, 0, 0,
    },
    {
        9, HID_DESCRIPTOR_TYPE, 0x10, 0x01, 0, 1, REPORT_DESCRIPTOR,
        sizeof(raw_report_descriptor) & 0xFF,
        sizeof(raw_report_descriptor) >> 8,
    },
    {
        sizeof(usb_descriptor_endpoint), USB_DESCRIPTOR_TYPE_ENDPOINT,
        USB_DESCRIPTOR_ENDPOINT_IN, USB_EP_TYPE_INTERRUPT, kPacketSize, 1,
    },
    {
        sizeof(usb_descriptor_endpoint), USB_DESCRIPTOR_TYPE_ENDPOINT,
        USB_DESCRIPTOR_ENDPOINT_OUT, USB_EP_TYPE_INTERRUPT, kPacketSize, 1,
    },
};

static ONE_DESCRIPTOR raw_hid_descriptor_only = {
    (uint8_t *)&raw_hid_descriptor.hid,
    sizeof(raw_hid_descriptor.hid),
};

static uint8_t rx_buffer[kPacketSize];
static volatile uint8_t rx_ready = 0;
static volatile uint8_t tx_busy = 0;
static uint8_t control_buffer[kPacketSize];
static volatile uint8_t control_ready = 0;
static uint8_t idle_value = 0;
static uint8_t protocol_value = 1;

static void raw_hid_tx_callback();
static void raw_hid_rx_callback();
static void raw_hid_reset();
static void raw_hid_get_descriptor(uint8_t *out);
static RESULT raw_hid_data_setup(uint8 request, uint8 interface,
                                 uint8 request_type, uint8 w_value0,
                                 uint8 w_value1, uint16 w_index,
                                 uint16 w_length);
static RESULT raw_hid_no_data_setup(uint8 request, uint8 interface,
                                    uint8 request_type, uint8 w_value0,
                                    uint8 w_value1, uint16 w_index);

static USBEndpointInfo raw_hid_endpoints[2] = {
    {raw_hid_tx_callback, nullptr, kPacketSize, 0,
     USB_GENERIC_ENDPOINT_TYPE_INTERRUPT, 0, 1, 0, 0},
    {raw_hid_rx_callback, nullptr, kPacketSize, 0,
     USB_GENERIC_ENDPOINT_TYPE_INTERRUPT, 0, 0, 0, 0},
};

static USBCompositePart raw_hid_part = {
    1, 2, 0, sizeof(raw_hid_descriptor), raw_hid_get_descriptor,
    nullptr, raw_hid_reset, nullptr, nullptr, nullptr,
    raw_hid_data_setup, raw_hid_no_data_setup, raw_hid_endpoints,
};

static void raw_hid_get_descriptor(uint8_t *out) {
    memcpy(out, &raw_hid_descriptor, sizeof(raw_hid_descriptor));
    const uint8_t interface_offset =
        (uint8_t *) &raw_hid_descriptor.interface.bInterfaceNumber -
        (uint8_t *) &raw_hid_descriptor;
    const uint8_t in_endpoint_offset =
        (uint8_t *) &raw_hid_descriptor.in_endpoint.bEndpointAddress -
        (uint8_t *) &raw_hid_descriptor;
    const uint8_t out_endpoint_offset =
        (uint8_t *) &raw_hid_descriptor.out_endpoint.bEndpointAddress -
        (uint8_t *) &raw_hid_descriptor;
    out[interface_offset] += raw_hid_part.startInterface;
    out[in_endpoint_offset] |= raw_hid_endpoints[0].address;
    out[out_endpoint_offset] |= raw_hid_endpoints[1].address;
}

static void raw_hid_tx_callback() {
    tx_busy = 0;
}

static void raw_hid_rx_callback() {
    const uint32_t received =
        usb_generic_read_to_buffer(&raw_hid_endpoints[1], rx_buffer,
                                   sizeof(rx_buffer));
    if (received == kPacketSize) rx_ready = 1;
    usb_generic_enable_rx(&raw_hid_endpoints[1]);
}

static void raw_hid_reset() {
    rx_ready = 0;
    tx_busy = 0;
    control_ready = 0;
}

static RESULT raw_hid_data_setup(uint8 request, uint8 interface,
                                 uint8 request_type, uint8 w_value0,
                                 uint8 w_value1, uint16 w_index,
                                 uint16 w_length) {
    (void)interface;
    (void)w_index;
    (void)w_length;
    if ((request_type & (REQUEST_TYPE | RECIPIENT)) ==
        (STANDARD_REQUEST | INTERFACE_RECIPIENT)) {
        if (request == GET_DESCRIPTOR) {
            if (w_value1 == REPORT_DESCRIPTOR) {
                usb_generic_control_tx_setup((void *)raw_report_descriptor,
                                             sizeof(raw_report_descriptor), nullptr);
                return USB_SUCCESS;
            }
            if (w_value1 == HID_DESCRIPTOR_TYPE) {
                usb_generic_control_descriptor_tx(&raw_hid_descriptor_only);
                return USB_SUCCESS;
            }
        }
        if (request == GET_PROTOCOL) {
            usb_generic_control_tx_setup(&protocol_value, 1, nullptr);
            return USB_SUCCESS;
        }
        if (request == GET_IDLE) {
            usb_generic_control_tx_setup(&idle_value, 1, nullptr);
            return USB_SUCCESS;
        }
    }
    if ((request_type & (REQUEST_TYPE | RECIPIENT)) ==
        (CLASS_REQUEST | INTERFACE_RECIPIENT) && request == SET_REPORT &&
        w_value1 == HID_REPORT_TYPE_OUTPUT && w_value0 == 0) {
        control_ready = 0;
        usb_generic_control_rx_setup(control_buffer, sizeof(control_buffer),
                                     &control_ready);
        return USB_SUCCESS;
    }
    return USB_UNSUPPORT;
}

static RESULT raw_hid_no_data_setup(uint8 request, uint8 interface,
                                    uint8 request_type, uint8 w_value0,
                                    uint8 w_value1, uint16 w_index) {
    (void)interface;
    (void)w_value1;
    (void)w_index;
    if ((request_type & (REQUEST_TYPE | RECIPIENT)) !=
        (CLASS_REQUEST | INTERFACE_RECIPIENT)) return USB_UNSUPPORT;
    if (request == SET_PROTOCOL) {
        protocol_value = w_value0;
        return USB_SUCCESS;
    }
    if (request == SET_IDLE) {
        idle_value = w_value0;
        return USB_SUCCESS;
    }
    return USB_UNSUPPORT;
}

}  // namespace

bool via_raw_hid_register() {
    return USBComposite.add(&raw_hid_part, &raw_hid_part);
}

bool via_raw_hid_receive(uint8_t *data) {
    if (control_ready == USB_CONTROL_DONE) {
        noInterrupts();
        memcpy(data, control_buffer, kPacketSize);
        control_ready = 0;
        interrupts();
        return true;
    }
    if (!rx_ready) return false;
    noInterrupts();
    memcpy(data, rx_buffer, kPacketSize);
    rx_ready = 0;
    interrupts();
    return true;
}

bool via_raw_hid_send(const uint8_t *data) {
    if (tx_busy) return false;
    tx_busy = 1;
    if (usb_generic_send_from_buffer(&raw_hid_endpoints[0],
                                     const_cast<uint8_t *>(data),
                                     kPacketSize) != kPacketSize) {
        tx_busy = 0;
        return false;
    }
    return true;
}
