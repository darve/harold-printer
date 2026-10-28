# harold_wifi (TinyPICO)

A Wi-Fi front end for harold-printer. The TinyPICO serves a web page, and the
Pico does the printing over USB. Photos are dithered in your browser, so the
TinyPICO only passes the finished 1-bit image along.

## Wiring

| TinyPICO   | Pico (2 or W)   |
|------------|-----------------|
| IO25 (TX)  | GP5, pin 7      |
| IO26 (RX)  | GP4, pin 6      |
| GND        | GND, pin 8      |

Power each board from its own USB cable while you're developing. For a
standalone build, the TinyPICO's 5V pin can take the Pico's VBUS (pin 40)
instead, as long as the TinyPICO's own USB cable is unplugged.

## Flashing

1. Install the Arduino IDE.
2. In Settings, add this to "Additional boards manager URLs":
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. In Boards Manager, install "esp32 by Espressif Systems".
4. Open `tinypico/harold_wifi/harold_wifi.ino`.
5. Choose Tools > Board > esp32 > "UM TinyPICO", and the TinyPICO's port.
6. Click Upload.

No extra libraries are needed.

`certs.h` holds the root certificates for api.anthropic.com, taken from the
Mozilla CA bundle.

## First run

1. On your phone, join the Wi-Fi network `harold-printer`.
2. Open http://192.168.4.1 and enter your home Wi-Fi details.
3. Rejoin your home Wi-Fi and open http://harold.local

If `harold.local` doesn't open (some Android phones), the Arduino Serial
Monitor at 115200 baud shows the IP address at boot.

If the home network can't be joined at boot, the TinyPICO goes back to setup
mode. To clear the saved details from a computer on the same network, run
`curl -X POST http://harold.local/forget`.

## Claude printouts

The page's Claude section makes the big button ask Claude for a short
printout on a theme, beer jokes by default. Pick a theme, or "Surprise me"
for a different one each press, or type your own.

1. Make an API key at https://console.anthropic.com (Settings > API keys).
2. On the page, tap "API key needed", paste it, and tap Save key.
3. Press the button.

The key is stored on the TinyPICO only, and the page never shows it again.
Each press is one request to Claude Opus 5.5 at low effort, so a fraction of
a cent. If the request fails (no key, no internet), it prints "harold's brain
is offline" with the reason, and the Serial Monitor shows the details.

Untick "The big button asks Claude" to make the button print photos again.
The button also goes back to photos by itself about 15 seconds after the
TinyPICO stops responding.

## Serial protocol

See `src/bridge.h`. It's plain text at 460800 baud, so you can also drive it
from any other board or a USB serial adapter.
