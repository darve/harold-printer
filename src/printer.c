#include "printer.h"

#include "escpos.h"
#include "printer_usb.h"
#include "test_page.h"
#include "transport.h"
#include "tspl.h"

// Printers known to speak TSPL rather than ESC/POS.
static const struct { uint16_t vid, pid; } tspl_printers[] = {
  {0x36FC, 0x0413},  // TPL "Label" TD-403T
};

static printer_mode_t mode = PRINTER_MODE_AUTO;

void printer_set_mode(printer_mode_t m) {
  mode = m;
}

printer_mode_t printer_get_mode(void) {
  return mode;
}

printer_mode_t printer_active_mode(void) {
  if (mode != PRINTER_MODE_AUTO) return mode;

  printer_usb_info_t info;
  printer_usb_get_info(&info);
  for (unsigned i = 0; i < sizeof(tspl_printers) / sizeof(tspl_printers[0]); i++) {
    if (info.mounted && info.vid == tspl_printers[i].vid && info.pid == tspl_printers[i].pid) {
      return PRINTER_MODE_TSPL;
    }
  }
  return PRINTER_MODE_ESCPOS;
}

const char *printer_mode_name(printer_mode_t m) {
  switch (m) {
    case PRINTER_MODE_ESCPOS: return "escpos";
    case PRINTER_MODE_TSPL:   return "tspl";
    default:                  return "auto";
  }
}

bool printer_text(const char *text) {
  if (printer_active_mode() == PRINTER_MODE_TSPL) return tspl_print_text(text);
  return escpos_text(text) && escpos_newline() && transport_flush();
}

bool printer_feed(uint8_t n) {
  if (printer_active_mode() == PRINTER_MODE_TSPL) return tspl_feed(n);
  return escpos_feed(n) && transport_flush();
}

bool printer_test(void) {
  if (printer_active_mode() == PRINTER_MODE_TSPL) return tspl_test_label();
  return test_page_print();
}

bool printer_image(const uint8_t *bits, uint16_t width_px, uint16_t height_px) {
  if (printer_active_mode() == PRINTER_MODE_TSPL) return tspl_print_image(bits, width_px, height_px);
  return escpos_init()
      && escpos_raster_image(bits, width_px, height_px)
      && escpos_feed(4)
      && transport_flush();
}
