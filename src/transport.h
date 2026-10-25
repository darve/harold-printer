#ifndef TRANSPORT_H_
#define TRANSPORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Byte-stream transport to the printer.
//
// Bytes are queued in a RAM ring buffer and sent by transport_task() one bulk
// transfer at a time, each waiting for the previous to complete. This is the
// only layer that knows the printer is on USB; a TTL serial or BLE backend can
// replace transport.c without touching the ESC/POS or shell layers.

void transport_init(void);

// Call often from the main loop (it is also called by app_yield()).
void transport_task(void);

// True when a printer is connected and accepting data.
bool transport_ready(void);

// Queues up to `len` bytes without blocking. Returns how many were accepted.
size_t transport_write(const uint8_t *buf, size_t len);

// Queues all `len` bytes, yielding to the USB stacks while the queue is full.
// Gives up and returns false if the printer disconnects or makes no progress
// for TRANSPORT_STALL_TIMEOUT_MS.
bool transport_write_all(const uint8_t *buf, size_t len);

// Waits (yielding) until everything queued has been sent. Same failure rules.
bool transport_flush(void);

// Bytes waiting to be sent.
size_t transport_pending(void);

#define TRANSPORT_STALL_TIMEOUT_MS 10000

#endif
