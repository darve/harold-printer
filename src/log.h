#ifndef LOG_H_
#define LOG_H_

#include <stddef.h>
#include <stdint.h>

// Buffered logging to the native USB CDC port.
//
// Output is queued in a RAM ring buffer and drained by log_task() from the
// main loop, so logging never blocks the USB stacks. Messages written before
// a terminal opens the port are kept (up to the buffer size) and appear once
// it connects, which means boot-time output isn't lost.

void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_write(const char *buf, size_t len);
void log_hexdump(const uint8_t *buf, size_t len);

// Call often from the main loop.
void log_task(void);

#endif
