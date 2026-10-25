#include "printer_usb.h"

#include <string.h>

#include "tusb.h"
#include "host/usbh_pvt.h"  // must follow tusb.h
#include "log.h"

#define PRINTER_REQ_GET_PORT_STATUS  0x01

static printer_usb_info_t dev;
static volatile bool out_busy;

static printer_usb_status_cb_t status_cb;
static uint8_t status_buf __attribute__((aligned(4)));

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

void printer_usb_get_info(printer_usb_info_t *info) {
  *info = dev;
}

bool printer_usb_mounted(void) {
  return dev.mounted;
}

bool printer_usb_busy(void) {
  return out_busy;
}

bool printer_usb_send(const uint8_t *buf, uint16_t len) {
  if (!dev.mounted || out_busy || len == 0) return false;
  if (!usbh_edpt_claim(dev.daddr, dev.ep_out)) return false;

  out_busy = true;
  if (!usbh_edpt_xfer(dev.daddr, dev.ep_out, (uint8_t *) buf, len)) {
    out_busy = false;
    usbh_edpt_release(dev.daddr, dev.ep_out);
    return false;
  }
  return true;
}

static void status_complete(tuh_xfer_t *xfer) {
  printer_usb_status_cb_t cb = status_cb;
  status_cb = NULL;
  if (cb) cb(xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len >= 1, status_buf);
}

bool printer_usb_request_status(printer_usb_status_cb_t cb) {
  if (!dev.mounted || status_cb) return false;

  static tusb_control_request_t req;
  req = (tusb_control_request_t) {
    .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_INTERFACE,
      .type      = TUSB_REQ_TYPE_CLASS,
      .direction = TUSB_DIR_IN,
    },
    .bRequest = PRINTER_REQ_GET_PORT_STATUS,
    .wValue   = 0,
    .wIndex   = dev.itf_num,
    .wLength  = 1,
  };

  tuh_xfer_t xfer = {
    .daddr       = dev.daddr,
    .ep_addr     = 0,
    .setup       = &req,
    .buffer      = &status_buf,
    .complete_cb = status_complete,
    .user_data   = 0,
  };

  status_cb = cb;
  if (!tuh_control_xfer(&xfer)) {
    status_cb = NULL;
    return false;
  }
  return true;
}

//--------------------------------------------------------------------+
// TinyUSB application class driver
//--------------------------------------------------------------------+

static bool prn_init(void) {
  memset(&dev, 0, sizeof(dev));
  out_busy = false;
  return true;
}

static bool prn_deinit(void) {
  return true;
}

static bool prn_open(uint8_t rhport, uint8_t daddr, tusb_desc_interface_t const *itf, uint16_t max_len) {
  (void) rhport;
  if (itf->bInterfaceClass != TUSB_CLASS_PRINTER) return false;
  if (dev.mounted || dev.ep_out) return false;  // one printer at a time

  uint8_t ep_out = 0, ep_in = 0;
  uint16_t out_mps = 0;

  // Walk the descriptors belonging to this interface looking for bulk endpoints.
  uint8_t const *p = (uint8_t const *) itf;
  uint8_t const *end = p + max_len;
  p = tu_desc_next(p);
  while (p < end && tu_desc_type(p) != TUSB_DESC_INTERFACE) {
    if (tu_desc_type(p) == TUSB_DESC_ENDPOINT) {
      tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *) p;
      if (ep->bmAttributes.xfer == TUSB_XFER_BULK) {
        if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_OUT && !ep_out) {
          TU_ASSERT(tuh_edpt_open(daddr, ep));
          ep_out = ep->bEndpointAddress;
          out_mps = tu_edpt_packet_size(ep);
        } else if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN && !ep_in) {
          TU_ASSERT(tuh_edpt_open(daddr, ep));
          ep_in = ep->bEndpointAddress;
        }
      }
    }
    p = tu_desc_next(p);
  }

  if (!ep_out) {
    log_printf("printer: interface %u has no bulk OUT endpoint, not claiming\r\n", itf->bInterfaceNumber);
    return false;
  }

  dev.daddr      = daddr;
  dev.itf_num    = itf->bInterfaceNumber;
  dev.protocol   = itf->bInterfaceProtocol;
  dev.ep_out     = ep_out;
  dev.ep_in      = ep_in;
  dev.ep_out_mps = out_mps;
  return true;
}

static bool prn_set_config(uint8_t daddr, uint8_t itf_num) {
  tuh_vid_pid_get(daddr, &dev.vid, &dev.pid);
  dev.bytes_sent = 0;
  dev.xfer_errors = 0;
  dev.mounted = true;
  log_printf("printer: ready (itf %u, OUT 0x%02x mps %u, IN 0x%02x)\r\n",
             dev.itf_num, dev.ep_out, dev.ep_out_mps, dev.ep_in);
  usbh_driver_set_config_complete(daddr, itf_num);
  return true;
}

static bool prn_xfer_cb(uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred) {
  (void) daddr;
  if (ep_addr == dev.ep_out) {
    if (result == XFER_RESULT_SUCCESS) {
      dev.bytes_sent += xferred;
    } else {
      dev.xfer_errors++;
      log_printf("printer: OUT transfer failed (result %d, %lu bytes)\r\n",
                 result, (unsigned long) xferred);
    }
    out_busy = false;
  }
  return true;
}

static void prn_close(uint8_t daddr) {
  if (daddr != dev.daddr) return;
  if (dev.mounted) log_printf("printer: disconnected\r\n");
  memset(&dev, 0, sizeof(dev));
  out_busy = false;
  status_cb = NULL;
}

static usbh_class_driver_t const printer_driver = {
  .name       = "PRINTER",
  .init       = prn_init,
  .deinit     = prn_deinit,
  .open       = prn_open,
  .set_config = prn_set_config,
  .xfer_cb    = prn_xfer_cb,
  .close      = prn_close,
};

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count) {
  *driver_count = 1;
  return &printer_driver;
}
