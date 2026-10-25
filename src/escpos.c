#include "escpos.h"

#include <string.h>

#include "log.h"
#include "transport.h"

#define ESC 0x1B
#define GS  0x1D

bool escpos_write(const uint8_t *buf, size_t len) {
  return transport_write_all(buf, len);
}

bool escpos_init(void) {
  static const uint8_t cmd[] = {ESC, '@'};
  return escpos_write(cmd, sizeof(cmd));
}

bool escpos_text(const char *s) {
  return escpos_write((const uint8_t *) s, strlen(s));
}

bool escpos_newline(void) {
  static const uint8_t lf = '\n';
  return escpos_write(&lf, 1);
}

bool escpos_feed(uint8_t lines) {
  uint8_t cmd[] = {ESC, 'd', lines};
  return escpos_write(cmd, sizeof(cmd));
}

bool escpos_bold(bool on) {
  uint8_t cmd[] = {ESC, 'E', on ? 1 : 0};
  return escpos_write(cmd, sizeof(cmd));
}

bool escpos_align(escpos_align_t align) {
  uint8_t cmd[] = {ESC, 'a', (uint8_t) align};
  return escpos_write(cmd, sizeof(cmd));
}

bool escpos_size(uint8_t width, uint8_t height) {
  if (width < 1) width = 1;
  if (width > 8) width = 8;
  if (height < 1) height = 1;
  if (height > 8) height = 8;
  uint8_t cmd[] = {GS, '!', (uint8_t) (((width - 1) << 4) | (height - 1))};
  return escpos_write(cmd, sizeof(cmd));
}

bool escpos_raster_image(const uint8_t *bits, uint16_t width_px, uint16_t height_px) {
  if (width_px == 0 || height_px == 0) return true;
  if (width_px > ESCPOS_PRINT_WIDTH_PX) {
    log_printf("escpos: image %u px wide exceeds %u px print width\r\n",
               width_px, ESCPOS_PRINT_WIDTH_PX);
    return false;
  }

  uint16_t row_bytes = (uint16_t) ((width_px + 7) / 8);

  for (uint16_t y = 0; y < height_px; y += ESCPOS_BAND_ROWS) {
    uint16_t rows = height_px - y;
    if (rows > ESCPOS_BAND_ROWS) rows = ESCPOS_BAND_ROWS;

    // GS v 0 m xL xH yL yH, m = 0 (normal density); x is in bytes.
    uint8_t hdr[] = {GS, 'v', '0', 0,
                     (uint8_t) (row_bytes & 0xFF), (uint8_t) (row_bytes >> 8),
                     (uint8_t) (rows & 0xFF), (uint8_t) (rows >> 8)};
    if (!escpos_write(hdr, sizeof(hdr))) return false;
    if (!escpos_write(bits + (size_t) y * row_bytes, (size_t) rows * row_bytes)) return false;
  }
  return true;
}
