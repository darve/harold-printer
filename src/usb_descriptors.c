// Device-side descriptors for the native USB port: a single CDC-ACM interface
// used for logs (and, later, the command shell).

#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

#define USB_VID   0x2E8A  // Raspberry Pi
#define USB_PID   0x000A  // Pico SDK CDC stdio PID, so hosts pick a CDC driver
#define USB_BCD   0x0200

enum {
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_CDC,
};

static tusb_desc_device_t const desc_device = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = USB_BCD,
  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor           = USB_VID,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0100,
  .iManufacturer      = STRID_MANUFACTURER,
  .iProduct           = STRID_PRODUCT,
  .iSerialNumber      = STRID_SERIAL,
  .bNumConfigurations = 1
};

static uint8_t const desc_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EPNUM_CDC_NOTIF, 8,
                     EPNUM_CDC_OUT, EPNUM_CDC_IN, CFG_TUD_CDC_EP_BUFSIZE),
};

static char const *string_desc_arr[] = {
  [STRID_MANUFACTURER] = "harold-printer",
  [STRID_PRODUCT]      = "Pico 2 printer bridge",
  [STRID_SERIAL]       = NULL,  // filled from the flash unique ID
  [STRID_CDC]          = "Console",
};

static uint16_t desc_str[32 + 1];

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
  char const *str;
  size_t len;

  if (index == STRID_LANGID) {
    desc_str[1] = 0x0409;
    len = 1;
  } else {
    if (index == STRID_SERIAL) {
      pico_get_unique_board_id_string(serial, sizeof(serial));
      str = serial;
    } else if (index < TU_ARRAY_SIZE(string_desc_arr) && string_desc_arr[index]) {
      str = string_desc_arr[index];
    } else {
      return NULL;
    }
    len = strlen(str);
    if (len > 32) len = 32;
    for (size_t i = 0; i < len; i++) desc_str[1 + i] = (uint16_t) str[i];
  }

  desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * len + 2));
  return desc_str;
}
