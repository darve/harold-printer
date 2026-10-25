#include "shell.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "escpos.h"
#include "log.h"
#include "photos.h"
#include "printer_usb.h"
#include "test_page.h"
#include "transport.h"
#include "tusb.h"

#define LINE_MAX 200

static char line[LINE_MAX + 1];
static size_t line_len;

static void prompt(void) {
  con_printf("> ");
}

//--------------------------------------------------------------------+
// Commands
//--------------------------------------------------------------------+

static void report(bool ok) {
  con_printf("%s\r\n", ok ? "ok" : "FAILED (see log)");
}

static bool need_printer(void) {
  if (transport_ready()) return true;
  con_printf("no printer connected\r\n");
  return false;
}

static void cmd_help(void) {
  con_printf(
    "Commands:\r\n"
    "  info            printer and endpoint details\r\n"
    "  status          ask the printer for paper/error status\r\n"
    "  print <text>    print a line of text\r\n"
    "  feed <n>        feed n lines\r\n"
    "  test            print the test page\r\n"
    "  raw <hex>       send raw bytes, e.g. raw 1b 40 or raw 1b40\r\n"
    "  photos          list built-in photos\r\n"
    "  photo [n]       print photo n, or a random one\r\n");
}

static void cmd_info(void) {
  printer_usb_info_t i;
  printer_usb_get_info(&i);
  if (!i.mounted) {
    con_printf("printer: not connected\r\n");
    return;
  }
  con_printf("printer: connected, address %u, VID:PID %04x:%04x\r\n", i.daddr, i.vid, i.pid);
  con_printf("  interface %u, protocol %u (%s)\r\n", i.itf_num, i.protocol,
             i.protocol == 1 ? "unidirectional" : i.protocol == 2 ? "bidirectional" : "other");
  con_printf("  bulk OUT 0x%02x mps %u, bulk IN ", i.ep_out, i.ep_out_mps);
  if (i.ep_in) con_printf("0x%02x\r\n", i.ep_in); else con_printf("none\r\n");
  con_printf("  sent %lu bytes, %lu transfer errors, %u bytes queued\r\n",
             (unsigned long) i.bytes_sent, (unsigned long) i.xfer_errors,
             (unsigned) transport_pending());
  con_printf("  %u photo(s) built in\r\n", photo_count);
}

static void status_cb(bool ok, uint8_t s) {
  if (!ok) {
    con_printf("status: request failed\r\n");
    return;
  }
  con_printf("status: 0x%02x%s%s%s\r\n", s,
             (s & PRINTER_STATUS_PAPER_EMPTY) ? " PAPER-EMPTY" : "",
             (s & PRINTER_STATUS_SELECTED) ? " selected" : "",
             (s & PRINTER_STATUS_NOT_ERROR) ? " no-error" : " ERROR");
}

static void cmd_status(void) {
  if (!need_printer()) return;
  if (!printer_usb_request_status(status_cb)) con_printf("status: could not send request\r\n");
}

static void cmd_print(const char *args) {
  if (!need_printer()) return;
  report(escpos_text(args) && escpos_newline() && transport_flush());
}

static void cmd_feed(const char *args) {
  if (!need_printer()) return;
  int n = *args ? atoi(args) : 1;
  if (n < 0) n = 0;
  if (n > 255) n = 255;
  report(escpos_feed((uint8_t) n) && transport_flush());
}

static void cmd_test(void) {
  if (!need_printer()) return;
  report(test_page_print());
}

static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = (char) tolower((unsigned char) c);
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static void cmd_raw(const char *args) {
  uint8_t buf[LINE_MAX / 2];
  size_t n = 0;
  int hi = -1;

  for (const char *p = args; *p; p++) {
    if (*p == ' ' || *p == ',') continue;
    int v = hex_val(*p);
    if (v < 0) {
      con_printf("raw: bad hex character '%c'\r\n", *p);
      return;
    }
    if (hi < 0) {
      hi = v;
    } else {
      buf[n++] = (uint8_t) ((hi << 4) | v);
      hi = -1;
    }
  }
  if (hi >= 0) {
    con_printf("raw: odd number of hex digits\r\n");
    return;
  }
  if (n == 0) {
    con_printf("usage: raw <hex bytes>\r\n");
    return;
  }
  if (!need_printer()) return;
  con_printf("raw: sending %u byte(s)\r\n", (unsigned) n);
  report(escpos_write(buf, n) && transport_flush());
}

static void cmd_photos(void) {
  if (photo_count == 0) {
    con_printf("no photos built in. Add images to photos/ and rebuild.\r\n");
    return;
  }
  for (unsigned i = 0; i < photo_count; i++) {
    con_printf("  %2u  %-32s %ux%u\r\n", i, photos[i].name, photos[i].width, photos[i].height);
  }
}

static void cmd_photo(const char *args) {
  if (!need_printer()) return;
  report(photos_print(*args ? atoi(args) : -1));
}

static void run(char *cmd) {
  while (*cmd == ' ') cmd++;
  char *args = cmd;
  while (*args && *args != ' ') args++;
  if (*args) *args++ = '\0';
  while (*args == ' ') args++;

  if (*cmd == '\0')                   return;
  else if (!strcmp(cmd, "help"))      cmd_help();
  else if (!strcmp(cmd, "info"))      cmd_info();
  else if (!strcmp(cmd, "status"))    cmd_status();
  else if (!strcmp(cmd, "print"))     cmd_print(args);
  else if (!strcmp(cmd, "feed"))      cmd_feed(args);
  else if (!strcmp(cmd, "test"))      cmd_test();
  else if (!strcmp(cmd, "raw"))       cmd_raw(args);
  else if (!strcmp(cmd, "photos"))    cmd_photos();
  else if (!strcmp(cmd, "photo"))     cmd_photo(args);
  else con_printf("unknown command '%s', try help\r\n", cmd);
}

//--------------------------------------------------------------------+
// Line editing
//--------------------------------------------------------------------+

void shell_init(void) {
  line_len = 0;
}

void shell_task(void) {
  static bool was_connected;
  bool connected = tud_cdc_connected();
  if (connected && !was_connected) {
    con_printf("\r\nharold-printer shell. Type help.\r\n");
    prompt();
  }
  was_connected = connected;

  while (tud_cdc_available()) {
    char c = (char) tud_cdc_read_char();

    if (c == '\r' || c == '\n') {
      if (c == '\n' && line_len == 0) continue;  // swallow LF after CR
      con_printf("\r\n");
      line[line_len] = '\0';
      line_len = 0;
      run(line);
      prompt();
      return;  // one command per call; the rest waits in the CDC FIFO
    } else if (c == '\b' || c == 0x7F) {
      if (line_len) {
        line_len--;
        con_printf("\b \b");
      }
    } else if (c >= 0x20 && c < 0x7F && line_len < LINE_MAX) {
      line[line_len++] = c;
      log_write(&c, 1);
    }
  }
}
