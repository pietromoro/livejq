/* livejq - the terminal put back when livejq dies with the screen taken. */
#ifndef LIVEJQ_GUARD_H
#define LIVEJQ_GUARD_H

/* Saves the terminal's modes before curses changes them. Until
 * guard_disarm, a fatal signal or exit restores them and leaves the
 * alternate screen; the signal then goes on to kill livejq as usual. */
void guard_arm(void);
void guard_disarm(void);

#endif /* LIVEJQ_GUARD_H */
