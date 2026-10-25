#include "log.h"

#include <stdarg.h>
#include <stdio.h>

#include "pico/time.h"
#include "tusb.h"

#define LOG_BUF_SIZE 8192  // power of two

static char ring[LOG_BUF_SIZE];
static uint32_t head;  // write index (free-running)
static uint32_t tail;  // read index (free-running)
static uint32_t dropped;

void log_write(const char *buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (head - tail >= LOG_BUF_SIZE) {
      dropped += (uint32_t) (len - i);
      return;
    }
    ring[head++ & (LOG_BUF_SIZE - 1)] = buf[i];
  }
}

void log_printf(const char *fmt, ...) {
  char line[256];
  uint32_t ms = to_ms_since_boot(get_absolute_time());
  int n = snprintf(line, sizeof(line), "[%5lu.%03lu] ",
                   (unsigned long) (ms / 1000), (unsigned long) (ms % 1000));

  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf(line + n, sizeof(line) - (size_t) n, fmt, ap);
  va_end(ap);

  if (n > (int) sizeof(line) - 1) n = sizeof(line) - 1;
  log_write(line, (size_t) n);
}

void con_printf(const char *fmt, ...) {
  char line[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int) sizeof(line) - 1) n = sizeof(line) - 1;
  log_write(line, (size_t) n);
}

void log_hexdump(const uint8_t *buf, size_t len) {
  char line[80];
  for (size_t off = 0; off < len; off += 16) {
    int n = snprintf(line, sizeof(line), "    %04x:", (unsigned) off);
    for (size_t i = off; i < off + 16 && i < len; i++) {
      n += snprintf(line + n, sizeof(line) - (size_t) n, " %02x", buf[i]);
    }
    n += snprintf(line + n, sizeof(line) - (size_t) n, "\r\n");
    log_write(line, (size_t) n);
  }
}

void log_task(void) {
  if (!tud_cdc_connected()) return;

  if (dropped) {
    char msg[48];
    int n = snprintf(msg, sizeof(msg), "\r\n[log: %lu bytes dropped]\r\n",
                     (unsigned long) dropped);
    if (tud_cdc_write_available() < (uint32_t) n) return;
    tud_cdc_write(msg, (uint32_t) n);
    dropped = 0;
  }

  while (tail != head) {
    uint32_t avail = tud_cdc_write_available();
    if (avail == 0) break;

    // Write the contiguous run up to the end of the ring or the space available.
    uint32_t idx = tail & (LOG_BUF_SIZE - 1);
    uint32_t run = head - tail;
    if (run > LOG_BUF_SIZE - idx) run = LOG_BUF_SIZE - idx;
    if (run > avail) run = avail;

    tail += tud_cdc_write(&ring[idx], run);
  }
  tud_cdc_write_flush();
}
