# harold-printer

Firmware for a Raspberry Pi Pico 2 (RP2350) that acts as a USB host and drives a MUNBYN IMP001B 58 mm thermal printer with ESC/POS. Press a button and it prints a random photo.

## Wiring

The printer plugs into a female USB-A breakout, driven by Pico-PIO-USB:

| Breakout | Pico 2 |
|---|---|
| + (VBUS) | VBUS (phys pin 40) |
| D− | GP27 (phys pin 32) |
| D+ | GP26 (phys pin 31) |
| − (GND) | GND (phys pin 38) |
| S (shield) | unconnected, or GND |

The photo button is a momentary push button between **GP15 (phys pin 20)** and any GND pin. No resistor is needed, because the firmware enables the internal pull-up.

The Pico's own micro-USB port is a USB CDC serial device. It carries the logs and the command shell, and it also powers the board.

## Build

You need Pico SDK 2.x (tested with 2.2.0), `arm-none-eabi-gcc`, and Python 3 with Pillow (`pip3 install pillow`) for the photo converter.

```sh
git clone --recurse-submodules https://github.com/darve/harold-printer
cd harold-printer
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk   # or set PICO_SDK_PATH in your environment
make -j
```

This produces `build/harold_printer.uf2`. Hold BOOTSEL, plug in the Pico, and copy the file onto the `RP2350` drive.

If you cloned without `--recurse-submodules`, run `git submodule update --init` first.

## Photos

Put JPEGs or PNGs in `photos/` and run `make` again. The build converts them to 384 px wide, dithered black and white, and bakes them into the firmware. See [photos/README.md](photos/README.md) for details and a preview command.

## Using it

Open the Pico's serial port at any baud rate: `screen /dev/tty.usbmodem* 115200` on macOS, or `picocom /dev/ttyACM0` on Linux. Turn the printer on and plug it in. The LED blinks fast while there's no printer and goes solid when one is ready.

Each time a device is plugged in, its USB descriptors are logged (`=== MOUNT ===` … `VERDICT:`), followed by `printer: ready`. Unplugging logs `=== UNMOUNT ===`. Logs are buffered (8 KB), so anything from before you opened the terminal still appears.

### Shell commands

| Command | What it does |
|---|---|
| `help` | List commands |
| `info` | Printer VID:PID, endpoints, bytes sent, errors, queued bytes |
| `status` | GET_PORT_STATUS: paper empty, selected, error |
| `print <text>` | Print a line of text |
| `feed <n>` | Feed n lines |
| `test` | Print the test page: sizes, bold, alignment, bitmap, feed |
| `raw <hex>` | Send raw bytes, e.g. `raw 1b 40` or `raw 1b40` |
| `photos` | List built-in photos |
| `photo [n]` | Print photo n, or a random one |

Every printing command waits until the data has been sent and then prints `ok` or `FAILED`. A write gives up after 10 s with no progress (for example, out of paper) or if the printer is unplugged.

## Clock choice

Pico-PIO-USB bit-bangs full-speed USB and needs the system clock to be a multiple of 12 MHz. The RP2350 defaults to 150 MHz, so the firmware sets **120 MHz**. 240 MHz would also work, but it's an overclock. 120 MHz stays in spec, needs no core voltage change, and is the clock TinyUSB's own RP2xxx board support uses for PIO-USB.

## Layout

The layers are kept separate so the USB transport can be swapped for TTL serial or BLE later. Only `transport.c` and `printer_usb.c` know about USB.

| File | What it does |
|---|---|
| `src/main.c` | Clock setup, TinyUSB dual-role init, main loop, button handling |
| `src/printer_usb.c` | USB printer class (0x07) host driver, registered with TinyUSB as an app driver |
| `src/transport.c` | 16 KB TX queue, sends one bulk transfer (8 packets) at a time and waits for each to complete |
| `src/escpos.c` | ESC/POS commands, including GS v 0 raster images sent in 24-row bands |
| `src/test_page.c` | Test page and its 384 px border-and-checkerboard bitmap |
| `src/photos.c` | Prints a built-in photo, chosen at random |
| `src/shell.c` | Line-based command shell |
| `src/button.c` | Debounced push button on GP15 |
| `src/usb_probe.c` | Logs every device's descriptors on mount, and unplug events |
| `src/log.c` | Ring-buffered output to the CDC port |
| `src/usb_descriptors.c` | Descriptors for the Pico's own CDC serial port |
| `src/tusb_config.h` | TinyUSB config: device on rhport 0, PIO-USB host on rhport 1 |
| `tools/convert_photos.py` | Converts `photos/` to a C table at build time |
| `lib/Pico-PIO-USB` | Submodule, pinned to 0.7.2 for the RP2350-E9 fix (unplug detection) |

## Not yet verified on hardware

- Printing of any kind: the test page, bitmap alignment, and running `test` 10 times in a row.
- Whether the IMP001B centres or left-aligns raster images. The photos are full width, so it shouldn't matter.
- `status` (GET_PORT_STATUS). Some printers return fixed values.
- Photo tone. If prints come out too dark or too light, change `--gamma` in the CMake converter command (default 1.4, higher is lighter).
