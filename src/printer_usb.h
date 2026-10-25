#ifndef PRINTER_USB_H_
#define PRINTER_USB_H_

#include <stdbool.h>
#include <stdint.h>

// Minimal USB printer class (0x07) host driver, registered with TinyUSB as an
// application class driver. TinyUSB has no built-in printer driver.
//
// It claims the first printer interface, opens its bulk OUT (and bulk IN if
// present) endpoints, and offers one outstanding OUT transfer at a time.
// Higher layers should go through transport.h rather than calling this.

typedef struct {
  bool     mounted;
  uint8_t  daddr;
  uint16_t vid, pid;
  uint8_t  itf_num;
  uint8_t  protocol;      // 1 = unidirectional, 2 = bidirectional, 3 = IEEE 1284.4
  uint8_t  ep_out;
  uint8_t  ep_in;         // 0 if none
  uint16_t ep_out_mps;
  uint32_t bytes_sent;    // since mount
  uint32_t xfer_errors;   // since mount
} printer_usb_info_t;

// GET_PORT_STATUS bits (USB Printer Class 1.1, section 4.2.2)
#define PRINTER_STATUS_PAPER_EMPTY  0x20
#define PRINTER_STATUS_SELECTED     0x10
#define PRINTER_STATUS_NOT_ERROR    0x08

typedef void (*printer_usb_status_cb_t)(bool ok, uint8_t status);

void printer_usb_get_info(printer_usb_info_t *info);

bool printer_usb_mounted(void);

// True while an OUT transfer is in flight.
bool printer_usb_busy(void);

// Starts one bulk OUT transfer. `buf` must stay valid until printer_usb_busy()
// goes false. Returns false if not mounted, already busy, or the stack refused.
bool printer_usb_send(const uint8_t *buf, uint16_t len);

// Issues GET_PORT_STATUS (class request 0x01). `cb` runs from tuh_task().
bool printer_usb_request_status(printer_usb_status_cb_t cb);

#endif
