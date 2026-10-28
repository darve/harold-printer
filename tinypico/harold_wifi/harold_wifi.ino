// harold_wifi: Wi-Fi front end for harold-printer, for an Unexpected Maker
// TinyPICO (ESP32).
//
// Serves a web page where you pick a photo or type some text, and makes the
// Pico's button ask Claude for a printout on a theme (beer jokes by default). The browser
// dithers photos to 1 bit at the printer's resolution, and this sketch passes
// the job to the Pico over a serial link (see src/bridge.h for the protocol).
// The Pico is the USB host that talks to the printer.
//
// Wiring: TinyPICO IO25 (TX) -> Pico GP5 (pin 7)
//         TinyPICO IO26 (RX) <- Pico GP4 (pin 6)
//         TinyPICO GND       -- Pico GND (pin 8)
//
// First boot: join the "harold-printer" Wi-Fi network from your phone, open
// http://192.168.4.1 and enter your home Wi-Fi details. After that the page is
// at http://harold.local

#include <ESPmDNS.h>
#include <time.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "claude.h"
#include "page.h"

#define BRIDGE_TX_PIN  25
#define BRIDGE_RX_PIN  26
#define BRIDGE_BAUD    460800
#define MAX_IMAGE      (128 * 1024)  // must match BRIDGE_MAX_IMAGE on the Pico

#define SETUP_AP_NAME  "harold-printer"
#define HOSTNAME       "harold"

static WebServer server(80);
static Preferences prefs;
static bool setup_mode;

static uint8_t *image_buf;
static size_t image_len;
static bool image_overflow;

//--------------------------------------------------------------------+
// Serial link to the Pico
//--------------------------------------------------------------------+

static String rx_line;
static bool button_event;  // the Pico sent EVENT BUTTON

// Reads whatever the Pico has sent. Returns true with a complete line in
// `line`. EVENT lines are handled here and never returned.
static bool pico_poll_line(String &line) {
  while (Serial1.available()) {
    char c = (char) Serial1.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (rx_line.length() < 512) rx_line += c;
      continue;
    }
    String l = rx_line;
    rx_line = "";
    if (l.startsWith("EVENT ")) {
      if (l == "EVENT BUTTON") button_event = true;
      continue;
    }
    line = l;
    return true;
  }
  return false;
}

// Waits for one reply line from the Pico, or returns "" on timeout.
static String pico_read_line(uint32_t timeout_ms) {
  String line;
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (pico_poll_line(line)) return line;
    delay(1);
  }
  return "";
}

// Sends one command and waits for its OK/ERR reply. Printing can take a while
// on a 4x6 label, hence the long default timeout.
static String pico_command(const String &cmd, uint32_t timeout_ms = 60000) {
  String stale;
  while (pico_poll_line(stale)) {}  // drop leftover replies, keep events
  Serial1.print(cmd);
  Serial1.print('\n');
  String r = pico_read_line(timeout_ms);
  return r.length() ? r : String("ERR no reply from the Pico (check the wiring)");
}

static String pico_image(const uint8_t *bits, size_t len, unsigned w, unsigned h) {
  String r = pico_command("IMG " + String(w) + " " + String(h), 2000);
  if (r != "READY") return r;
  Serial1.write(bits, len);
  r = pico_read_line(60000);
  return r.length() ? r : String("ERR no reply from the Pico");
}

//--------------------------------------------------------------------+
// Claude
//--------------------------------------------------------------------+

// Themes for "surprise me". The page has the same list.
static const char *const THEMES[] = {
  "a joke about beer",
  "a terrible pun about beer",
  "a haiku about beer",
  "tasting notes for an imaginary craft beer with a silly name",
  "a fortune cookie fortune for someone at the pub",
  "a pub quiz question, with the answer underneath",
  "a motivational quote from a very tired barman",
  "a fake but plausible beer fact",
};
static const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

static bool claude_busy;

// Reads cols= from HELLO, falling back to a receipt printer's 32.
static unsigned printer_cols() {
  String r = pico_command("HELLO", 1000);
  int i = r.indexOf("cols=");
  unsigned cols = i >= 0 ? r.substring(i + 5).toInt() : 0;
  return cols ? cols : 32;
}

// Asks Claude for a printout and prints it. Returns the bridge reply.
static String claude_print() {
  if (claude_busy) return "ERR already working on one";
  claude_busy = true;

  String theme = prefs.getString("theme", THEMES[0]);
  if (theme == "surprise") theme = THEMES[esp_random() % THEME_COUNT];
  unsigned cols = printer_cols();

  Serial.printf("Asking Claude for %s\n", theme.c_str());
  String err;
  String text = claude_ask(prefs.getString("key", ""), theme, cols, err);
  String result;
  if (text.isEmpty()) {
    Serial.printf("Claude: %s\n", err.c_str());
    // Print something anyway, so a button press is never ignored.
    result = pico_command("TEXT " + wrap_for_printer("harold's brain is offline.\n(" + err + ")", cols));
    if (result.startsWith("OK")) result = "ERR " + err;
  } else {
    Serial.printf("Claude says:\n%s\n", text.c_str());
    result = pico_command("TEXT " + wrap_for_printer(text + "\n\n- Claude", cols) + "\\n\\n");
  }
  claude_busy = false;
  return result;
}

//--------------------------------------------------------------------+
// Web handlers
//--------------------------------------------------------------------+

static void send_result(const String &r) {
  server.send(r.startsWith("OK") ? 200 : 500, "text/plain", r);
}

static void handle_root() {
  if (setup_mode) server.send_P(200, "text/html", SETUP_PAGE);
  else server.send_P(200, "text/html", MAIN_PAGE);
}

static void handle_status() {
  send_result(pico_command("HELLO", 1000));
}

static void handle_text() {
  String text = server.arg("text");
  text.replace("\n", " ");
  text.replace("\r", " ");
  if (text.length() > 190) text = text.substring(0, 190);
  send_result(pico_command("TEXT " + text));
}

static void handle_simple(const char *cmd) {
  send_result(pico_command(cmd));
}

// The browser posts the packed bitmap as a file upload. Collect it here.
static void handle_image_upload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    image_len = 0;
    image_overflow = false;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (image_len + up.currentSize > MAX_IMAGE) {
      image_overflow = true;
    } else {
      memcpy(image_buf + image_len, up.buf, up.currentSize);
      image_len += up.currentSize;
    }
  }
}

static void handle_image_done() {
  unsigned w = server.arg("w").toInt();
  unsigned h = server.arg("h").toInt();
  size_t expected = (size_t) ((w + 7) / 8) * h;
  if (image_overflow) {
    send_result("ERR image too big");
  } else if (w == 0 || h == 0 || image_len != expected) {
    send_result("ERR got " + String(image_len) + " bytes, expected " + String(expected));
  } else {
    send_result(pico_image(image_buf, image_len, w, h));
  }
}

static void handle_claude_get() {
  String theme = prefs.getString("theme", THEMES[0]);
  String json = "{\"button\":" + String(prefs.getBool("button", true) ? "true" : "false") +
                ",\"has_key\":" + String(prefs.getString("key", "").isEmpty() ? "false" : "true") +
                ",\"theme\":\"" + json_escape(theme) + "\"}";
  server.send(200, "application/json", json);
}

static void handle_claude_save() {
  if (server.hasArg("key") && server.arg("key").length()) prefs.putString("key", server.arg("key"));
  if (server.hasArg("theme") && server.arg("theme").length()) {
    String t = server.arg("theme");
    t.replace("\n", " ");
    prefs.putString("theme", t.substring(0, 200));
  }
  if (server.hasArg("button")) prefs.putBool("button", server.arg("button") == "1");
  server.send(200, "text/plain", "OK");
}

static void handle_setup_save() {
  prefs.putString("ssid", server.arg("ssid"));
  prefs.putString("pass", server.arg("pass"));
  server.send(200, "text/html",
              "<meta name=viewport content='width=device-width'>"
              "<p style='font:18px sans-serif'>Saved. Rejoin your home Wi-Fi, then open "
              "<b>http://" HOSTNAME ".local</b></p>");
  delay(1000);
  ESP.restart();
}

//--------------------------------------------------------------------+
// Wi-Fi
//--------------------------------------------------------------------+

static bool join_wifi() {
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  if (ssid.isEmpty()) return false;

  Serial.printf("Joining %s", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(ssid.c_str(), pass.c_str());
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFi.setSleep(false);  // keeps the page responsive
  Serial.printf("Connected. Open http://%s.local or http://%s\n", HOSTNAME,
                WiFi.localIP().toString().c_str());
  return true;
}

static void start_setup_ap() {
  setup_mode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(SETUP_AP_NAME);
  Serial.printf("Setup mode: join Wi-Fi \"%s\" and open http://%s\n", SETUP_AP_NAME,
                WiFi.softAPIP().toString().c_str());
}

//--------------------------------------------------------------------+

void setup() {
  Serial.begin(115200);
  Serial1.setRxBufferSize(1024);
  Serial1.setTxBufferSize(4096);
  Serial1.begin(BRIDGE_BAUD, SERIAL_8N1, BRIDGE_RX_PIN, BRIDGE_TX_PIN);

  // The TinyPICO has 4 MB of PSRAM; fall back to normal RAM if it's off.
  image_buf = (uint8_t *) ps_malloc(MAX_IMAGE);
  if (!image_buf) image_buf = (uint8_t *) malloc(MAX_IMAGE);

  prefs.begin("harold", false);
  if (!join_wifi()) start_setup_ap();
  if (!setup_mode) {
    MDNS.begin(HOSTNAME);
    // TLS certificate checks need the real date.
    configTime(0, 0, "pool.ntp.org", "time.google.com");
  }

  server.on("/", HTTP_GET, handle_root);
  server.on("/setup", HTTP_POST, handle_setup_save);
  server.on("/api/status", HTTP_GET, handle_status);
  server.on("/api/text", HTTP_POST, handle_text);
  server.on("/api/feed", HTTP_POST, [] { handle_simple("FEED 1"); });
  server.on("/api/photo", HTTP_POST, [] { handle_simple("PHOTO"); });
  server.on("/api/test", HTTP_POST, [] { handle_simple("TEST"); });
  server.on("/api/image", HTTP_POST, handle_image_done, handle_image_upload);
  server.on("/api/claude", HTTP_GET, handle_claude_get);
  server.on("/api/claude", HTTP_POST, handle_claude_save);
  server.on("/api/claude/print", HTTP_POST, [] { send_result(claude_print()); });
  server.on("/forget", HTTP_POST, [] {
    prefs.clear();
    server.send(200, "text/plain", "Wi-Fi details cleared. Restarting into setup mode.");
    delay(1000);
    ESP.restart();
  });
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void loop() {
  server.handleClient();

  // Keep the Pico's button pointed at us while the Claude button is on. The
  // Pico hands it back to the photo printer if these heartbeats stop.
  static uint32_t last_heartbeat;
  if (!setup_mode && millis() - last_heartbeat > 5000) {
    last_heartbeat = millis();
    pico_command(prefs.getBool("button", true) ? "BUTTON REMOTE" : "BUTTON LOCAL", 500);
  }

  String stray;
  while (pico_poll_line(stray)) {}  // picks up EVENT lines
  if (button_event) {
    button_event = false;
    claude_print();
  }

  // If home Wi-Fi drops, keep trying; the page comes back when it reconnects.
  if (!setup_mode && WiFi.status() != WL_CONNECTED) {
    static uint32_t last_try;
    if (millis() - last_try > 10000) {
      last_try = millis();
      WiFi.reconnect();
    }
  }
  delay(1);
}
