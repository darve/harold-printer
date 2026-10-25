#include "test_page.h"

#include <string.h>

#include "escpos.h"
#include "transport.h"

#define BMP_W       ESCPOS_PRINT_WIDTH_PX
#define BMP_H       96
#define BMP_ROW     (BMP_W / 8)
#define BORDER      3
#define CHECK       16

static uint8_t bitmap[BMP_ROW * BMP_H];

static void set_px(int x, int y) {
  bitmap[y * BMP_ROW + x / 8] |= (uint8_t) (0x80 >> (x % 8));
}

// 384 px wide so a skewed or truncated row shows up as a broken border, with a
// checkerboard inside to show any byte or bit order mistakes.
static void build_bitmap(void) {
  memset(bitmap, 0, sizeof(bitmap));
  for (int y = 0; y < BMP_H; y++) {
    for (int x = 0; x < BMP_W; x++) {
      bool border = x < BORDER || x >= BMP_W - BORDER || y < BORDER || y >= BMP_H - BORDER;
      bool check = ((x / CHECK) + (y / CHECK)) % 2 == 0;
      if (border || (x >= 2 * BORDER && x < BMP_W - 2 * BORDER &&
                     y >= 2 * BORDER && y < BMP_H - 2 * BORDER && check)) {
        set_px(x, y);
      }
    }
  }
}

bool test_page_print(void) {
  static bool built;
  if (!built) {
    build_bitmap();
    built = true;
  }

  return escpos_init()
      && escpos_align(ESCPOS_ALIGN_CENTER)
      && escpos_size(2, 2) && escpos_bold(true)
      && escpos_text("harold-printer\n")
      && escpos_size(1, 1) && escpos_bold(false)
      && escpos_text("test page\n\n")
      && escpos_align(ESCPOS_ALIGN_LEFT)
      && escpos_text("Normal 1x1\n")
      && escpos_bold(true) && escpos_text("Bold 1x1\n") && escpos_bold(false)
      && escpos_size(2, 1) && escpos_text("Wide 2x1\n")
      && escpos_size(1, 2) && escpos_text("Tall 1x2\n")
      && escpos_size(3, 3) && escpos_text("3x3\n")
      && escpos_size(1, 1)
      && escpos_align(ESCPOS_ALIGN_LEFT) && escpos_text("left\n")
      && escpos_align(ESCPOS_ALIGN_CENTER) && escpos_text("center\n")
      && escpos_align(ESCPOS_ALIGN_RIGHT) && escpos_text("right\n")
      && escpos_align(ESCPOS_ALIGN_LEFT)
      && escpos_raster_image(bitmap, BMP_W, BMP_H)
      && escpos_text("0123456789012345678901234567890\n")
      && escpos_feed(4)
      && transport_flush();
}
