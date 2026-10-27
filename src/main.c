// harold-printer: Pico 2 (or Pico W) as a USB host for a MUNBYN thermal printer.
//
// Native USB port: CDC serial for logs and the command shell.
// PIO-USB port (GP26/GP27): USB host, talks to the printer.
// GP15 button: prints a random built-in photo.
// UART1 on GP4/GP5: print jobs from the TinyPICO Wi-Fi front end.

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pio_usb.h"
#include "tusb.h"

#include "app.h"
#include "bridge.h"
#include "button.h"
#include "log.h"
#include "photos.h"
#include "shell.h"
#include "transport.h"
#include "usb_probe.h"

#define PIO_USB_DP_PIN  26  // D- is GP27; Pico-PIO-USB requires D- = D+ + 1

#ifdef PICO_DEFAULT_LED_PIN
#define LED_PIN PICO_DEFAULT_LED_PIN
#endif

static void led_task(void) {
#ifdef LED_PIN
  // Fast blink while no printer is ready, solid when it is.
  static uint32_t last_ms;
  static bool on;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (transport_ready()) {
    gpio_put(LED_PIN, 1);
  } else if (now - last_ms >= 250) {
    last_ms = now;
    on = !on;
    gpio_put(LED_PIN, on);
  }
#endif
}

void app_yield(void) {
  tud_task();
  tuh_task();
  transport_task();
  log_task();
  led_task();
}

int main(void) {
  // Pico-PIO-USB bit-bangs full speed USB (12 Mbit/s) and needs a system
  // clock that is a multiple of 12 MHz. RP2350 defaults to 150 MHz, which is
  // not. 120 MHz is used rather than 240 MHz because it is within spec (no
  // overclock or core voltage bump) and is the clock TinyUSB's own RP2xxx
  // board support uses for PIO-USB.
  set_sys_clock_khz(120000, true);

#ifdef LED_PIN
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);
#endif

  log_printf("harold-printer, sys clk %lu Hz, %u photo(s) built in\r\n",
             (unsigned long) clock_get_hz(clk_sys), photo_count);
  usb_probe_init();
  transport_init();
  shell_init();
  button_init();
  bridge_init();

  // Host on PIO-USB. tuh_configure() must run before the host stack starts.
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = PIO_USB_DP_PIN;
  tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

  tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
  tusb_init(BOARD_TUD_RHPORT, &dev_init);

  tusb_rhport_init_t host_init = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO};
  tusb_init(BOARD_TUH_RHPORT, &host_init);

  log_printf("Waiting for a device on the USB-A port (D+ GP%u, D- GP%u)...\r\n",
             PIO_USB_DP_PIN, PIO_USB_DP_PIN + 1);

  while (true) {
    app_yield();
    usb_probe_task();
    shell_task();
    bridge_task();

    if (button_pressed()) {
      if (transport_ready()) {
        photos_print(-1);
      } else {
        log_printf("button: no printer connected\r\n");
      }
    }
  }
}
