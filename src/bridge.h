#ifndef BRIDGE_H_
#define BRIDGE_H_

#include <stdbool.h>

// Serial link to a Wi-Fi front end (the TinyPICO sketch in tinypico/).
//
// UART1 at BRIDGE_BAUD: GP4 (phys pin 6) is TX, GP5 (phys pin 7) is RX.
// The protocol is one ASCII command per line. Every command gets exactly one
// reply line starting with OK or ERR:
//
//   HELLO              OK ready=<0|1> mode=<escpos|tspl> width=<dots> height=<dots> cols=<n>
//                      (height=0 means a continuous roll; cols is characters
//                      per line for TEXT)
//   TEXT <text>        print text; \n in the text is a line break, \\ a backslash
//   BUTTON REMOTE      for the next 15 s, a button press sends "EVENT BUTTON"
//                      instead of printing a photo. Re-send it as a heartbeat.
//   BUTTON LOCAL       give the button back to the photo printer now
//   FEED <n>           feed n lines, or n labels in TSPL mode
//   PHOTO              print a random built-in photo
//   TEST               print the test page or test label
//   IMG <w> <h>        replies READY, then expects ((w + 7) / 8) * h bytes of
//                      1 bpp bitmap (1 = black, MSB first), then prints it
//
// The Pico may also send "EVENT <name>" lines at any time between replies.

#define BRIDGE_BAUD       460800
#define BRIDGE_TX_PIN     4
#define BRIDGE_RX_PIN     5
#define BRIDGE_MAX_IMAGE  (128 * 1024)  // a 4x6 in label at 203 dpi is ~124 KB

void bridge_init(void);

// Call from the main loop (not from app_yield). Runs at most one command.
void bridge_task(void);

// True while the front end has claimed the button (see BUTTON REMOTE).
bool bridge_button_remote(void);

// Sends "EVENT <name>" to the front end.
void bridge_send_event(const char *name);

#endif
