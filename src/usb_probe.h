#ifndef USB_PROBE_H_
#define USB_PROBE_H_

// Phase 1 discovery: when a device enumerates on the PIO-USB host port, read
// its device, configuration and string descriptors (non-blocking, callback
// chain) and log them over CDC, followed by a one-line verdict on what kind
// of printer interface it appears to expose.

void usb_probe_init(void);

// Call often from the main loop. Handles the "nothing enumerated" timeout.
void usb_probe_task(void);

#endif
