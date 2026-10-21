#include "usb_probe.h"

#include <string.h>

#include "log.h"
#include "pico/time.h"
#include "tusb.h"

#define LANGID_EN_US          0x0409
#define NO_DEVICE_TIMEOUT_MS  5000
#define CONFIG_BUF_SIZE       512

enum {
  STEP_DEVICE,
  STEP_CONFIG_HEADER,
  STEP_CONFIG_FULL,
  STEP_STR_MANUFACTURER,
  STEP_STR_PRODUCT,
  STEP_STR_SERIAL,
  STEP_DONE,
};

static struct {
  uint8_t daddr;
  uint32_t gen;  // bumped on every mount/unmount so stale callbacks are ignored
  tusb_desc_device_t dev;
  uint8_t config[CONFIG_BUF_SIZE] __attribute__((aligned(4)));
  uint16_t config_len;
  uint16_t str[64];
} probe;

static bool ever_mounted;
static bool timeout_reported;

static void probe_step(uint8_t step);

//--------------------------------------------------------------------+
// Descriptor printing
//--------------------------------------------------------------------+

static const char *class_name(uint8_t cls) {
  switch (cls) {
    case 0x00: return "per-interface";
    case 0x02: return "CDC control";
    case 0x03: return "HID";
    case 0x07: return "Printer";
    case 0x08: return "Mass storage";
    case 0x09: return "Hub";
    case 0x0A: return "CDC data";
    case 0xEF: return "Misc";
    case 0xFF: return "Vendor specific";
    default:   return "other";
  }
}

static const char *xfer_type_name(uint8_t attr) {
  static const char *names[] = {"control", "isochronous", "bulk", "interrupt"};
  return names[attr & 0x03];
}

static const char *bridge_chip(uint16_t vid) {
  switch (vid) {
    case 0x1A86: return "WCH CH34x";
    case 0x10C4: return "Silicon Labs CP210x";
    case 0x0403: return "FTDI";
    case 0x067B: return "Prolific PL2303";
    default:     return NULL;
  }
}

static void log_utf16(const char *label, uint8_t index, const uint16_t *desc, bool ok) {
  char ascii[64];
  size_t n = 0;
  if (ok && index) {
    size_t chars = ((desc[0] & 0xFF) - 2) / 2;
    for (size_t i = 0; i < chars && n < sizeof(ascii) - 1; i++) {
      uint16_t c = desc[1 + i];
      ascii[n++] = (c >= 0x20 && c < 0x7F) ? (char) c : '?';
    }
  }
  ascii[n] = '\0';
  log_printf("  %-13s (idx %u): %s\r\n", label, index,
             index == 0 ? "<none>" : (ok ? ascii : "<read failed>"));
}

static void log_device(void) {
  const tusb_desc_device_t *d = &probe.dev;
  const char *chip = bridge_chip(d->idVendor);

  log_printf("Device descriptor:\r\n");
  log_printf("  VID:PID       %04x:%04x%s%s\r\n", d->idVendor, d->idProduct,
             chip ? "  <- " : "", chip ? chip : "");
  log_printf("  bcdUSB        %04x\r\n", d->bcdUSB);
  log_printf("  class         0x%02x (%s) sub 0x%02x proto 0x%02x\r\n",
             d->bDeviceClass, class_name(d->bDeviceClass),
             d->bDeviceSubClass, d->bDeviceProtocol);
  log_printf("  maxPacket0    %u\r\n", d->bMaxPacketSize0);
  log_printf("  bcdDevice     %04x\r\n", d->bcdDevice);
  log_printf("  configs       %u\r\n", d->bNumConfigurations);
}

// Walks the configuration descriptor, logging every interface and endpoint,
// and logs a verdict on which transport the printer probably needs.
static void log_config(void) {
  const uint8_t *p = probe.config;
  const uint8_t *end = probe.config + probe.config_len;
  const tusb_desc_configuration_t *cfg = (const tusb_desc_configuration_t *) p;

  log_printf("Configuration descriptor (%u bytes):\r\n", probe.config_len);
  log_hexdump(probe.config, probe.config_len);
  log_printf("  interfaces %u, bmAttributes 0x%02x (%s), maxPower %u mA\r\n",
             cfg->bNumInterfaces, cfg->bmAttributes,
             (cfg->bmAttributes & 0x40) ? "self-powered" : "bus-powered",
             cfg->bMaxPower * 2);

  bool printer_itf = false, printer_bulk_out = false;
  bool vendor_itf = false, cdc_itf = false;
  uint8_t cur_class = 0;

  while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end) {
    uint8_t type = p[1];
    if (type == TUSB_DESC_INTERFACE) {
      const tusb_desc_interface_t *itf = (const tusb_desc_interface_t *) p;
      cur_class = itf->bInterfaceClass;
      log_printf("  Interface %u alt %u: class 0x%02x (%s) sub 0x%02x proto 0x%02x, %u endpoint(s)\r\n",
                 itf->bInterfaceNumber, itf->bAlternateSetting,
                 itf->bInterfaceClass, class_name(itf->bInterfaceClass),
                 itf->bInterfaceSubClass, itf->bInterfaceProtocol, itf->bNumEndpoints);
      if (cur_class == 0x07) printer_itf = true;
      if (cur_class == 0xFF) vendor_itf = true;
      if (cur_class == 0x02 || cur_class == 0x0A) cdc_itf = true;
    } else if (type == TUSB_DESC_ENDPOINT) {
      const tusb_desc_endpoint_t *ep = (const tusb_desc_endpoint_t *) p;
      bool in = ep->bEndpointAddress & 0x80;
      log_printf("    Endpoint 0x%02x %-3s %-11s maxPacket %u interval %u\r\n",
                 ep->bEndpointAddress, in ? "IN" : "OUT",
                 xfer_type_name(ep->bmAttributes.xfer),
                 tu_edpt_packet_size(ep), ep->bInterval);
      if (cur_class == 0x07 && !in && ep->bmAttributes.xfer == TUSB_XFER_BULK) {
        printer_bulk_out = true;
      }
    } else if (type != TUSB_DESC_CONFIGURATION) {
      log_printf("    (descriptor type 0x%02x, %u bytes)\r\n", type, p[0]);
    }
    p += p[0];
  }

  const char *chip = bridge_chip(probe.dev.idVendor);
  if (printer_itf && printer_bulk_out) {
    log_printf("VERDICT: USB printer class with bulk OUT. Phase 2 = printer-class transport.\r\n");
  } else if (printer_itf) {
    log_printf("VERDICT: printer class, but no bulk OUT endpoint found. Needs a closer look.\r\n");
  } else if (chip || cdc_itf) {
    log_printf("VERDICT: USB-serial bridge (%s). Phase 2 = host CDC/serial transport.\r\n",
               chip ? chip : "CDC-ACM");
  } else if (vendor_itf) {
    log_printf("VERDICT: vendor-specific interface. Probably raw bulk ESC/POS; needs a closer look.\r\n");
  } else {
    log_printf("VERDICT: no printer or serial interface recognised.\r\n");
  }
}

//--------------------------------------------------------------------+
// Async descriptor chain
//--------------------------------------------------------------------+

static void probe_cb(tuh_xfer_t *xfer) {
  uint32_t gen = (uint32_t) (xfer->user_data >> 8);
  uint8_t step = (uint8_t) (xfer->user_data & 0xFF);
  if (gen != probe.gen) return;  // device went away mid-probe

  bool ok = xfer->result == XFER_RESULT_SUCCESS;

  switch (step) {
    case STEP_DEVICE:
      if (!ok) { log_printf("ERROR: device descriptor read failed (%d)\r\n", xfer->result); return; }
      log_device();
      probe_step(STEP_CONFIG_HEADER);
      break;

    case STEP_CONFIG_HEADER: {
      if (!ok) { log_printf("ERROR: config descriptor header read failed (%d)\r\n", xfer->result); return; }
      uint16_t total = tu_le16toh(((const tusb_desc_configuration_t *) probe.config)->wTotalLength);
      if (total > CONFIG_BUF_SIZE) {
        log_printf("WARN: config descriptor is %u bytes, truncating to %u\r\n", total, CONFIG_BUF_SIZE);
        total = CONFIG_BUF_SIZE;
      }
      probe.config_len = total;
      probe_step(STEP_CONFIG_FULL);
      break;
    }

    case STEP_CONFIG_FULL:
      if (!ok) { log_printf("ERROR: full config descriptor read failed (%d)\r\n", xfer->result); return; }
      probe.config_len = (uint16_t) xfer->actual_len;
      probe_step(STEP_STR_MANUFACTURER);
      break;

    case STEP_STR_MANUFACTURER:
    case STEP_STR_PRODUCT:
    case STEP_STR_SERIAL: {
      static const char *labels[] = {"Manufacturer", "Product", "Serial"};
      uint8_t idx = step == STEP_STR_MANUFACTURER ? probe.dev.iManufacturer
                  : step == STEP_STR_PRODUCT      ? probe.dev.iProduct
                                                  : probe.dev.iSerialNumber;
      if (step == STEP_STR_MANUFACTURER) log_printf("Strings:\r\n");
      log_utf16(labels[step - STEP_STR_MANUFACTURER], idx, probe.str, ok);
      probe_step((uint8_t) (step + 1));
      break;
    }

    default:
      break;
  }
}

// Kicks off the transfer for `step`. Steps with nothing to fetch fall through
// to the next one.
static void probe_step(uint8_t step) {
  uintptr_t ud = ((uintptr_t) probe.gen << 8) | step;
  uint8_t d = probe.daddr;
  bool queued = true;

  switch (step) {
    case STEP_DEVICE:
      queued = tuh_descriptor_get_device(d, &probe.dev, sizeof(probe.dev), probe_cb, ud);
      break;
    case STEP_CONFIG_HEADER:
      queued = tuh_descriptor_get_configuration(d, 0, probe.config, 9, probe_cb, ud);
      break;
    case STEP_CONFIG_FULL:
      queued = tuh_descriptor_get_configuration(d, 0, probe.config, probe.config_len, probe_cb, ud);
      break;
    case STEP_STR_MANUFACTURER:
    case STEP_STR_PRODUCT:
    case STEP_STR_SERIAL: {
      uint8_t idx = step == STEP_STR_MANUFACTURER ? probe.dev.iManufacturer
                  : step == STEP_STR_PRODUCT      ? probe.dev.iProduct
                                                  : probe.dev.iSerialNumber;
      memset(probe.str, 0, sizeof(probe.str));
      if (idx == 0) {
        // No string: log it and move on without a transfer.
        tuh_xfer_t fake = {.daddr = d, .result = XFER_RESULT_FAILED, .user_data = ud};
        probe_cb(&fake);
        return;
      }
      queued = tuh_descriptor_get_string(d, idx, LANGID_EN_US, probe.str, sizeof(probe.str), probe_cb, ud);
      break;
    }
    case STEP_DONE:
      log_config();
      log_printf("=== end of device %u ===\r\n", d);
      return;
  }

  if (!queued) log_printf("ERROR: could not queue descriptor request (step %u)\r\n", step);
}

//--------------------------------------------------------------------+
// TinyUSB host callbacks
//--------------------------------------------------------------------+

void tuh_mount_cb(uint8_t daddr) {
  ever_mounted = true;
  probe.gen++;
  probe.daddr = daddr;
  probe.config_len = 0;
  log_printf("=== MOUNT: device address %u ===\r\n", daddr);
  probe_step(STEP_DEVICE);
}

void tuh_umount_cb(uint8_t daddr) {
  probe.gen++;
  log_printf("=== UNMOUNT: device address %u ===\r\n", daddr);
}

//--------------------------------------------------------------------+
// Public
//--------------------------------------------------------------------+

void usb_probe_init(void) {
  memset(&probe, 0, sizeof(probe));
  ever_mounted = false;
  timeout_reported = false;
}

void usb_probe_task(void) {
  if (!ever_mounted && !timeout_reported &&
      to_ms_since_boot(get_absolute_time()) > NO_DEVICE_TIMEOUT_MS) {
    timeout_reported = true;
    log_printf("No USB device enumerated within %u s of power-up. "
               "Check wiring, that the printer is on, and that its port isn't charge-only.\r\n",
               NO_DEVICE_TIMEOUT_MS / 1000);
  }
}
