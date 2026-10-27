#ifndef TSPL_H_
#define TSPL_H_

#include <stdbool.h>
#include <stdint.h>

// TSPL command layer for TSC-style label printers (e.g. TPL TD-403T).
// Every function writes through transport.h, finishes with PRINT, and returns
// false if the printer went away or stopped accepting data.

#define TSPL_DPMM  8  // 203 dpi

// Sizes are in tenths of a millimetre, so a 4x6 inch label is exact.
typedef struct {
  uint16_t width_dmm;
  uint16_t height_dmm;
  uint16_t gap_dmm;
} tspl_label_t;

void tspl_set_label(const tspl_label_t *label);
void tspl_get_label(tspl_label_t *label);

// One label with a line of text.
bool tspl_print_text(const char *text);

// Feeds `n` blank labels.
bool tspl_feed(uint8_t n);

// One label with a 1 bpp image (1 = black, MSB first, rows padded to whole
// bytes), scaled up by the largest whole number that fits and centred.
bool tspl_print_image(const uint8_t *bits, uint16_t width_px, uint16_t height_px);

// A test label: border, text in three sizes, and a checkerboard.
bool tspl_test_label(void);

#endif
