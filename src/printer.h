#ifndef PRINTER_H_
#define PRINTER_H_

#include <stdbool.h>
#include <stdint.h>

// Printer front end: picks the command language for the connected printer
// and routes the high-level operations to escpos.c or tspl.c.
//
// In auto mode the language comes from the printer's VID:PID: the TPL
// TD-403T uses TSPL, everything else ESC/POS.

typedef enum {
  PRINTER_MODE_AUTO,
  PRINTER_MODE_ESCPOS,
  PRINTER_MODE_TSPL,
} printer_mode_t;

void printer_set_mode(printer_mode_t mode);
printer_mode_t printer_get_mode(void);

// The language in use right now (never AUTO).
printer_mode_t printer_active_mode(void);
const char *printer_mode_name(printer_mode_t mode);

bool printer_text(const char *text);
bool printer_feed(uint8_t n);
bool printer_test(void);
bool printer_image(const uint8_t *bits, uint16_t width_px, uint16_t height_px);

#endif
