# harold-printer

Firmware for a Raspberry Pi Pico 2 (RP2350) that acts as a USB host and drives a MUNBYN IMP001B 58 mm thermal printer with ESC/POS.

**Current status: phase 1 (discovery).** The firmware logs what the printer exposes over USB. Printing comes in phase 2.

## Wiring

The printer plugs into a female USB-A breakout, driven by Pico-PIO-USB:

| USB-A pin | Pico 2 |
|---|---|
| 1 VBUS | VBUS (phys pin 40) |
| 2 D− | GP27 (phys pin 32) |
| 3 D+ | GP26 (phys pin 31) |
| 4 GND | GND (phys pin 38) |

The Pico's own micro-USB port is a USB CDC serial device. It carries the logs (and later the command shell), and it also powers the board.

## Build

You need Pico SDK 2.x (tested with 2.2.0) and `arm-none-eabi-gcc`.

```sh
git clone --recurse-submodules https://github.com/darve/harold-printer
cd harold-printer
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk   # or set PICO_SDK_PATH in your environment
make -j
```

This produces `build/harold_printer.uf2`. Hold BOOTSEL, plug in the Pico, and copy the file onto the `RP2350` drive.

If you cloned without `--recurse-submodules`, run `git submodule update --init` first.

## Using phase 1

1. Flash the firmware and open the Pico's serial port with a terminal at any baud rate, for example `screen /dev/tty.usbmodem* 115200` on macOS or `picocom /dev/ttyACM0` on Linux.
2. Turn the printer on and plug it into the USB-A socket.
3. You'll see a `=== MOUNT ===` block with the VID:PID, the full configuration descriptor as hex, every interface and endpoint, and a `VERDICT:` line.
4. Unplug it to see `=== UNMOUNT ===`. Replug as often as you like.

Logs are buffered in RAM (8 KB), so anything printed before you opened the terminal still appears.

The onboard LED blinks fast while nothing is mounted and stays solid while a device is mounted.

## Clock choice

Pico-PIO-USB bit-bangs full-speed USB and needs the system clock to be a multiple of 12 MHz. The RP2350 defaults to 150 MHz, so the firmware sets **120 MHz**. 240 MHz would also work, but it's an overclock. 120 MHz stays in spec, needs no core voltage change, and is the clock TinyUSB's own RP2xxx board support uses for PIO-USB.

## Layout

| File | What it does |
|---|---|
| `src/main.c` | Clock setup, TinyUSB dual-role init, main loop |
| `src/usb_probe.c` | Phase 1: non-blocking descriptor dump on mount/unmount |
| `src/log.c` | Ring-buffered logging to the CDC port |
| `src/usb_descriptors.c` | Descriptors for the Pico's own CDC serial port |
| `src/tusb_config.h` | TinyUSB config: device on rhport 0, PIO-USB host on rhport 1 |
| `lib/Pico-PIO-USB` | Submodule, pinned to 0.7.2 for the RP2350-E9 fix |
