// harold-printer: Pico 2 as a USB host for a MUNBYN thermal printer.
//
// Phase 1 (discovery): log whatever the printer exposes over USB.

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pio_usb.h"
#include "tusb.h"

#include "log.h"
#include "usb_probe.h"

#define PIO_USB_DP_PIN  26  // D- is GP27; Pico-PIO-USB requires D- = D+ + 1

#ifdef PICO_DEFAULT_LED_PIN
#define LED_PIN PICO_DEFAULT_LED_PIN
#endif

static void led_task(void) {
#ifdef LED_PIN
  // Fast blink while nothing is plugged into the host port, solid when mounted.
  static uint32_t last_ms;
  static bool on;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (tuh_mounted(1)) {
    gpio_put(LED_PIN, 1);
  } else if (now - last_ms >= 250) {
    last_ms = now;
    on = !on;
    gpio_put(LED_PIN, on);
  }
#endif
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

  log_printf("harold-printer phase 1 (USB discovery), sys clk %lu Hz\r\n",
             (unsigned long) clock_get_hz(clk_sys));
  usb_probe_init();

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
    tud_task();
    tuh_task();
    usb_probe_task();
    log_task();
    led_task();
  }
}
