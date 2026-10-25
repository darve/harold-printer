#ifndef BUTTON_H_
#define BUTTON_H_

#include <stdbool.h>

// Push button between BUTTON_PIN and GND, using the internal pull-up.
#define BUTTON_PIN 15  // phys pin 20

void button_init(void);

// Call often from the main loop. Returns true once per debounced press.
bool button_pressed(void);

#endif
