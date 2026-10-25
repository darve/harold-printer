#ifndef PHOTOS_H_
#define PHOTOS_H_

#include <stdbool.h>
#include <stdint.h>

// Photos baked into the firmware. The table is generated at build time by
// tools/convert_photos.py from the images in photos/.

typedef struct {
  const char    *name;
  uint16_t       width;   // always ESCPOS_PRINT_WIDTH_PX
  uint16_t       height;
  const uint8_t *bits;    // 1 bpp, 1 = black, MSB first
} photo_t;

extern const photo_t photos[];
extern const unsigned photo_count;

// Prints photo `index`, or a random one (never the same twice in a row) if
// index < 0. Blocks, yielding to the USB stacks, until it has all been sent.
bool photos_print(int index);

#endif
