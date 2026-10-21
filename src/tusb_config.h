#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

// Dual-role TinyUSB:
//   rhport 0 = native USB controller, device mode (CDC log/command port)
//   rhport 1 = Pico-PIO-USB on GP26/GP27, host mode (printer)

#define BOARD_TUD_RHPORT          0
#define BOARD_TUH_RHPORT          1

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS               OPT_OS_NONE
#endif
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG            0
#endif

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN        __attribute__((aligned(4)))

//------------- Device -------------//
#define CFG_TUD_ENABLED           1
#define CFG_TUD_ENDPOINT0_SIZE    64

#define CFG_TUD_CDC               1
#define CFG_TUD_CDC_RX_BUFSIZE    256
#define CFG_TUD_CDC_TX_BUFSIZE    1024
#define CFG_TUD_CDC_EP_BUFSIZE    64

//------------- Host -------------//
#define CFG_TUH_ENABLED           1
#define CFG_TUH_RPI_PIO_USB       1

// Big enough to hold a printer's full configuration descriptor in one go.
#define CFG_TUH_ENUMERATION_BUFSIZE 512

// No hub support: the printer plugs straight into the USB-A socket.
#define CFG_TUH_HUB               0
#define CFG_TUH_DEVICE_MAX        1

// Phase 1 claims no host classes; we only read descriptors.
#define CFG_TUH_CDC               0
#define CFG_TUH_HID               0
#define CFG_TUH_MSC               0
#define CFG_TUH_VENDOR            0

#endif
