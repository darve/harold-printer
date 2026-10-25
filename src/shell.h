#ifndef SHELL_H_
#define SHELL_H_

// Line-based command shell on the native USB CDC port. Type `help`.
void shell_init(void);

// Call from the main loop (not from app_yield). Runs at most one command.
void shell_task(void);

#endif
