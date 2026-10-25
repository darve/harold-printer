#include "button.h"

#include "hardware/gpio.h"
#include "pico/time.h"

#define DEBOUNCE_MS 30

static bool stable_down;
static bool last_raw;
static uint32_t changed_ms;

void button_init(void) {
  gpio_init(BUTTON_PIN);
  gpio_set_dir(BUTTON_PIN, GPIO_IN);
  gpio_pull_up(BUTTON_PIN);
  stable_down = last_raw = false;
  changed_ms = 0;
}

bool button_pressed(void) {
  bool raw = !gpio_get(BUTTON_PIN);  // active low
  uint32_t now = to_ms_since_boot(get_absolute_time());

  if (raw != last_raw) {
    last_raw = raw;
    changed_ms = now;
    return false;
  }
  if (raw != stable_down && now - changed_ms >= DEBOUNCE_MS) {
    stable_down = raw;
    return stable_down;  // report the press edge only
  }
  return false;
}
