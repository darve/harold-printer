#include "bridge.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "escpos.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "log.h"
#include "photos.h"
#include "pico/stdlib.h"
#include "printer.h"
#include "transport.h"
#include "tspl.h"

#define UART        uart1
#define UART_IRQ    UART1_IRQ
#define LINE_MAX    200
#define RX_BUF_SIZE 4096  // power of two
#define IMAGE_TIMEOUT_MS 3000
#define MAX_LABEL_DOTS   864

// The UART FIFO is only 32 bytes, so the RX interrupt moves bytes into a
// ring buffer and bridge_task() drains it from the main loop.
static uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint32_t rx_head, rx_tail;

static char line[LINE_MAX + 1];
static size_t line_len;

static uint8_t image[BRIDGE_MAX_IMAGE];
static size_t image_len, image_got;
static uint16_t image_w, image_h;
static uint32_t image_last_ms;
static bool receiving_image;

static void on_uart_rx(void) {
  while (uart_is_readable(UART)) {
    uint8_t c = (uint8_t) uart_get_hw(UART)->dr;
    uint32_t next = (rx_head + 1) & (RX_BUF_SIZE - 1);
    if (next != rx_tail) {  // drop the byte if full
      rx_buf[rx_head] = c;
      rx_head = next;
    }
  }
}

static bool rx_get(uint8_t *c) {
  if (rx_tail == rx_head) return false;
  *c = rx_buf[rx_tail];
  rx_tail = (rx_tail + 1) & (RX_BUF_SIZE - 1);
  return true;
}

static void reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void reply(const char *fmt, ...) {
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int) sizeof(buf) - 2) n = sizeof(buf) - 2;
  buf[n++] = '\n';
  uart_write_blocking(UART, (const uint8_t *) buf, (size_t) n);
}

static void reply_result(bool ok) {
  if (ok) reply("OK");
  else reply("ERR print failed");
}

static void printable_area(uint32_t *w, uint32_t *h) {
  if (printer_active_mode() == PRINTER_MODE_TSPL) {
    tspl_label_t l;
    tspl_get_label(&l);
    *w = (uint32_t) l.width_dmm * TSPL_DPMM / 10;
    *h = (uint32_t) l.height_dmm * TSPL_DPMM / 10;
    if (*w > MAX_LABEL_DOTS) *w = MAX_LABEL_DOTS;
  } else {
    *w = ESCPOS_PRINT_WIDTH_PX;
    *h = 0;
  }
}

static void start_image(const char *args) {
  unsigned w = 0, h = 0;
  if (sscanf(args, "%u %u", &w, &h) != 2 || w == 0 || h == 0 || w > 0xFFFF || h > 0xFFFF) {
    reply("ERR usage: IMG <w> <h>");
    return;
  }
  size_t len = (size_t) ((w + 7) / 8) * h;
  if (len > sizeof(image)) {
    reply("ERR image too big (%u bytes, max %u)", (unsigned) len, (unsigned) sizeof(image));
    return;
  }
  if (!transport_ready()) {
    reply("ERR no printer");
    return;
  }
  image_w = (uint16_t) w;
  image_h = (uint16_t) h;
  image_len = len;
  image_got = 0;
  image_last_ms = to_ms_since_boot(get_absolute_time());
  receiving_image = true;
  reply("READY");
}

static void run(char *cmd) {
  char *args = strchr(cmd, ' ');
  if (args) *args++ = '\0';
  else args = "";

  if (strcmp(cmd, "HELLO") == 0) {
    uint32_t w, h;
    printable_area(&w, &h);
    reply("OK ready=%d mode=%s width=%lu height=%lu", transport_ready() ? 1 : 0,
          printer_mode_name(printer_active_mode()), (unsigned long) w, (unsigned long) h);
    return;
  }
  if (strcmp(cmd, "IMG") == 0) {
    start_image(args);
    return;
  }

  bool known = strcmp(cmd, "TEXT") == 0 || strcmp(cmd, "FEED") == 0 ||
               strcmp(cmd, "PHOTO") == 0 || strcmp(cmd, "TEST") == 0;
  if (!known) {
    reply("ERR unknown command");
    return;
  }
  if (!transport_ready()) {
    reply("ERR no printer");
    return;
  }

  log_printf("bridge: %s\r\n", cmd);
  if (strcmp(cmd, "TEXT") == 0) {
    reply_result(printer_text(args));
  } else if (strcmp(cmd, "FEED") == 0) {
    int n = atoi(args);
    if (n < 1) n = 1;
    if (n > 20) n = 20;
    reply_result(printer_feed((uint8_t) n));
  } else if (strcmp(cmd, "PHOTO") == 0) {
    reply_result(photos_print(-1));
  } else {
    reply_result(printer_test());
  }
}

void bridge_init(void) {
  uart_init(UART, BRIDGE_BAUD);
  gpio_set_function(BRIDGE_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(BRIDGE_RX_PIN, GPIO_FUNC_UART);
  gpio_pull_up(BRIDGE_RX_PIN);  // idle high if nothing is wired up
  uart_set_fifo_enabled(UART, true);

  irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
  irq_set_enabled(UART_IRQ, true);
  uart_set_irq_enables(UART, true, false);
}

void bridge_task(void) {
  uint8_t c;

  if (receiving_image) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    while (image_got < image_len && rx_get(&c)) {
      image[image_got++] = c;
      image_last_ms = now;
    }
    if (image_got < image_len) {
      if (now - image_last_ms > IMAGE_TIMEOUT_MS) {
        log_printf("bridge: image timed out at %u/%u bytes\r\n",
                   (unsigned) image_got, (unsigned) image_len);
        receiving_image = false;
        reply("ERR image timed out");
      }
      return;
    }
    receiving_image = false;
    log_printf("bridge: printing %ux%u image\r\n", image_w, image_h);
    reply_result(printer_image(image, image_w, image_h));
    return;
  }

  while (rx_get(&c)) {
    if (c == '\n') {
      line[line_len] = '\0';
      line_len = 0;
      run(line);
      return;  // one command per call
    }
    if (c == '\r') continue;
    if (line_len < LINE_MAX) line[line_len++] = (char) c;
  }
}
