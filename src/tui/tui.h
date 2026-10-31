/* livejq - the terminal front-end over the core. */
#ifndef LIVEJQ_TUI_H
#define LIVEJQ_TUI_H

#include "app.h"
#include "defines.h"

/* Starts curses on the alternate screen, restoring the cursor on leave. */
void tui_enter(void);
void tui_leave(void);
/* Runs the UI over the options' file until the user quits. Returns the exit
 * status. */
i32 tui_run(const app_opts* opts);

#endif /* LIVEJQ_TUI_H */
