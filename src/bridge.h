#ifndef BRIDGE_H_
#define BRIDGE_H_

// Serial link to a Wi-Fi front end (the TinyPICO sketch in tinypico/).
//
// UART1 at BRIDGE_BAUD: GP4 (phys pin 6) is TX, GP5 (phys pin 7) is RX.
// The protocol is one ASCII command per line. Every command gets exactly one
// reply line starting with OK or ERR:
//
//   HELLO              OK ready=<0|1> mode=<escpos|tspl> width=<dots> height=<dots>
//                      (height=0 means a continuous roll)
//   TEXT <text>        print a line of text
//   FEED <n>           feed n lines, or n labels in TSPL mode
//   PHOTO              print a random built-in photo
//   TEST               print the test page or test label
//   IMG <w> <h>        replies READY, then expects ((w + 7) / 8) * h bytes of
//                      1 bpp bitmap (1 = black, MSB first), then prints it

#define BRIDGE_BAUD       460800
#define BRIDGE_TX_PIN     4
#define BRIDGE_RX_PIN     5
#define BRIDGE_MAX_IMAGE  (128 * 1024)  // a 4x6 in label at 203 dpi is ~124 KB

void bridge_init(void);

// Call from the main loop (not from app_yield). Runs at most one command.
void bridge_task(void);

#endif
