#include "transport.h"

#include <string.h>

#include "app.h"
#include "log.h"
#include "pico/time.h"
#include "printer_usb.h"

#define TX_RING_SIZE   16384  // power of two
#define CHUNK_PACKETS  8      // packets per bulk transfer

static uint8_t ring[TX_RING_SIZE];
static uint32_t head, tail;  // free-running indices

// A transfer's data must stay put until it completes, so it is copied out of
// the ring into this buffer first.
static uint8_t chunk[CHUNK_PACKETS * 64] __attribute__((aligned(4)));

static bool was_ready;

void transport_init(void) {
  head = tail = 0;
  was_ready = false;
}

bool transport_ready(void) {
  return printer_usb_mounted();
}

size_t transport_pending(void) {
  return head - tail;
}

size_t transport_write(const uint8_t *buf, size_t len) {
  if (!transport_ready()) return 0;
  size_t space = TX_RING_SIZE - (head - tail);
  if (len > space) len = space;
  for (size_t i = 0; i < len; i++) {
    ring[(head + i) & (TX_RING_SIZE - 1)] = buf[i];
  }
  head += (uint32_t) len;
  return len;
}

void transport_task(void) {
  bool ready = transport_ready();
  if (was_ready && !ready && head != tail) {
    log_printf("transport: printer gone, dropped %lu queued bytes\r\n",
               (unsigned long) (head - tail));
  }
  if (!ready) head = tail;  // nothing can be sent; start clean on next mount
  was_ready = ready;

  if (!ready || head == tail || printer_usb_busy()) return;

  printer_usb_info_t info;
  printer_usb_get_info(&info);
  uint32_t max = (uint32_t) info.ep_out_mps * CHUNK_PACKETS;
  if (max == 0 || max > sizeof(chunk)) max = sizeof(chunk);

  uint32_t n = head - tail;
  if (n > max) n = max;
  for (uint32_t i = 0; i < n; i++) {
    chunk[i] = ring[(tail + i) & (TX_RING_SIZE - 1)];
  }
  if (printer_usb_send(chunk, (uint16_t) n)) {
    tail += n;
  }
}

// Shared wait loop: yields until `done()` or a failure condition.
static bool wait_progress(const uint8_t **buf, size_t *len) {
  absolute_time_t deadline = make_timeout_time_ms(TRANSPORT_STALL_TIMEOUT_MS);
  uint32_t last_tail = tail;

  while (buf ? *len > 0 : (head != tail || printer_usb_busy())) {
    if (!transport_ready()) {
      log_printf("transport: printer disconnected during write\r\n");
      return false;
    }
    if (buf) {
      size_t n = transport_write(*buf, *len);
      *buf += n;
      *len -= n;
      if (*len == 0) break;
    }
    app_yield();
    if (tail != last_tail) {
      last_tail = tail;
      deadline = make_timeout_time_ms(TRANSPORT_STALL_TIMEOUT_MS);
    } else if (time_reached(deadline)) {
      log_printf("transport: printer stopped accepting data (paper out?), gave up after %u s\r\n",
                 TRANSPORT_STALL_TIMEOUT_MS / 1000);
      return false;
    }
  }
  return true;
}

bool transport_write_all(const uint8_t *buf, size_t len) {
  if (!transport_ready()) return false;
  return wait_progress(&buf, &len);
}

bool transport_flush(void) {
  if (!transport_ready()) return false;
  return wait_progress(NULL, NULL);
}
