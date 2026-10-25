#ifndef APP_H_
#define APP_H_

// Runs one pass of the background work (USB device and host stacks, logging,
// transport) without the shell. Long-running commands call this while they
// wait, so the USB stacks keep being serviced.
void app_yield(void);

#endif
