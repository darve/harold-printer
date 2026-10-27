#include "tspl.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "transport.h"

#define MAX_LABEL_DOTS  864  // 108 mm print width

// Default: 4 x 6 inch shipping labels with a 3 mm gap.
static tspl_label_t label = {.width_dmm = 1016, .height_dmm = 1524, .gap_dmm = 30};

#define DMM(v)       (unsigned) ((v) / 10), (unsigned) ((v) % 10)
#define DMM_TO_DOTS(v) ((uint32_t) (v) * TSPL_DPMM / 10)

void tspl_set_label(const tspl_label_t *l) {
  label = *l;
}

void tspl_get_label(tspl_label_t *l) {
  *l = label;
}

static bool cmd(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static bool cmd(const char *fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
  va_end(ap);
  if (n < 0) return false;
  if (n > (int) sizeof(buf) - 3) n = sizeof(buf) - 3;
  buf[n++] = '\r';
  buf[n++] = '\n';
  return transport_write_all((const uint8_t *) buf, (size_t) n);
}

// SIZE / GAP / DIRECTION / CLS: the preamble every label starts with.
static bool begin_label(void) {
  return cmd("SIZE %u.%u mm,%u.%u mm", DMM(label.width_dmm), DMM(label.height_dmm))
      && cmd("GAP %u.%u mm,0 mm", DMM(label.gap_dmm))
      && cmd("DIRECTION 1")
      && cmd("CLS");
}

static bool end_label(void) {
  return cmd("PRINT 1,1") && transport_flush();
}

// TSPL strings can't contain a double quote, so swap it for a single one.
static void sanitize(char *dst, size_t n, const char *src) {
  size_t i = 0;
  for (; src[i] && i < n - 1; i++) dst[i] = src[i] == '"' ? '\'' : src[i];
  dst[i] = '\0';
}

bool tspl_print_text(const char *text) {
  char safe[120];
  sanitize(safe, sizeof(safe), text);
  return begin_label()
      && cmd("TEXT 40,40,\"3\",0,1,1,\"%s\"", safe)
      && end_label();
}

bool tspl_feed(uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    if (!cmd("FORMFEED")) return false;
  }
  return transport_flush();
}

bool tspl_print_image(const uint8_t *bits, uint16_t width_px, uint16_t height_px) {
  uint32_t label_w = DMM_TO_DOTS(label.width_dmm);
  uint32_t label_h = DMM_TO_DOTS(label.height_dmm);
  if (label_w > MAX_LABEL_DOTS) label_w = MAX_LABEL_DOTS;

  uint32_t scale = label_w / width_px;
  if (label_h / height_px < scale) scale = label_h / height_px;
  if (scale == 0) scale = 1;  // too big even at 1x: crop to the label

  uint32_t out_w = width_px * scale;
  uint32_t out_h = height_px * scale;
  if (out_w > label_w) out_w = label_w;
  if (out_h > label_h) out_h = label_h;
  uint32_t out_row = (out_w + 7) / 8;
  uint32_t x0 = (label_w - out_w) / 2;
  uint32_t y0 = (label_h - out_h) / 2;
  uint32_t in_row = (width_px + 7) / 8;

  log_printf("tspl: image %ux%u at %lux on %u.%ux%u.%u mm label\r\n", width_px, height_px,
             (unsigned long) scale, DMM(label.width_dmm), DMM(label.height_dmm));

  // BITMAP x,y,width_bytes,height,mode,data. Mode 0 = overwrite.
  char hdr[64];
  int n = snprintf(hdr, sizeof(hdr), "BITMAP %lu,%lu,%lu,%lu,0,",
                   (unsigned long) x0, (unsigned long) y0,
                   (unsigned long) out_row, (unsigned long) out_h);
  if (!begin_label() || !transport_write_all((const uint8_t *) hdr, (size_t) n)) return false;

  // Stream the scaled image a row at a time. TSPL bitmaps print 0 bits as
  // black, the opposite of ours, so each output byte is inverted.
  uint8_t row[MAX_LABEL_DOTS / 8];
  for (uint32_t y = 0; y < out_h; y++) {
    const uint8_t *src = bits + (y / scale) * in_row;
    memset(row, 0, out_row);
    for (uint32_t x = 0; x < out_w; x++) {
      uint32_t sx = x / scale;
      if (src[sx / 8] & (0x80 >> (sx % 8))) row[x / 8] |= (uint8_t) (0x80 >> (x % 8));
    }
    for (uint32_t i = 0; i < out_row; i++) row[i] = (uint8_t) ~row[i];
    if (!transport_write_all(row, out_row)) return false;
  }

  return cmd("") && end_label();  // CRLF ends the BITMAP data
}

bool tspl_test_label(void) {
  uint32_t w = DMM_TO_DOTS(label.width_dmm);
  uint32_t h = DMM_TO_DOTS(label.height_dmm);
  if (w > MAX_LABEL_DOTS) w = MAX_LABEL_DOTS;

  if (!begin_label()) return false;
  if (!cmd("BOX 8,8,%lu,%lu,4", (unsigned long) (w - 8), (unsigned long) (h - 8))) return false;
  if (!cmd("TEXT 40,40,\"4\",0,1,1,\"harold-printer\"")) return false;
  if (!cmd("TEXT 40,110,\"3\",0,1,1,\"TSPL test label\"")) return false;
  if (!cmd("TEXT 40,160,\"2\",0,1,1,\"%u.%u x %u.%u mm, gap %u.%u mm\"",
           DMM(label.width_dmm), DMM(label.height_dmm), DMM(label.gap_dmm))) return false;

  // Checkerboard of filled 32-dot squares under the text.
  for (uint32_t y = 220; y + 32 <= h - 40 && y < 220 + 32 * 6; y += 32) {
    for (uint32_t x = 40; x + 32 <= w - 40; x += 32) {
      if (((x - 40) / 32 + (y - 220) / 32) % 2 == 0) {
        if (!cmd("BAR %lu,%lu,32,32", (unsigned long) x, (unsigned long) y)) return false;
      }
    }
  }
  return end_label();
}
