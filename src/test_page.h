#ifndef TEST_PAGE_H_
#define TEST_PAGE_H_

#include <stdbool.h>

// Prints a test page: text sizes, bold, alignment, the built-in test bitmap
// (full-width border plus checkerboard), then a feed. Blocks (yielding to the
// USB stacks) until everything has been sent.
bool test_page_print(void);

#endif
