#ifndef ESCPOS_H_
#define ESCPOS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ESC/POS command layer. Every function writes through transport.h and
// returns false if the printer went away or stopped accepting data.

#define ESCPOS_PRINT_WIDTH_PX  384  // 58 mm head, 8 dots/mm
#define ESCPOS_BAND_ROWS       24   // raster rows per GS v 0 command

typedef enum {
  ESCPOS_ALIGN_LEFT   = 0,
  ESCPOS_ALIGN_CENTER = 1,
  ESCPOS_ALIGN_RIGHT  = 2,
} escpos_align_t;

bool escpos_init(void);                       // ESC @
bool escpos_text(const char *s);
bool escpos_write(const uint8_t *buf, size_t len);  // raw bytes
bool escpos_newline(void);
bool escpos_feed(uint8_t lines);              // ESC d n
bool escpos_bold(bool on);                    // ESC E n
bool escpos_align(escpos_align_t align);      // ESC a n
bool escpos_size(uint8_t width, uint8_t height);  // GS ! n, each 1..8

// Prints a 1 bpp image (1 = black, MSB first, rows padded to whole bytes)
// with GS v 0, in bands of ESCPOS_BAND_ROWS so tall images don't overflow
// the printer's buffer. Images wider than ESCPOS_PRINT_WIDTH_PX are refused.
bool escpos_raster_image(const uint8_t *bits, uint16_t width_px, uint16_t height_px);

#endif
