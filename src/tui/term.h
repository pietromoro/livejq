/* livejq - escape sequences sent past curses, straight to the terminal. */
#ifndef LIVEJQ_TERM_H
#define LIVEJQ_TERM_H

/* Writes s to the terminal as it is, for it to act on, even where curses
 * draws through the windows console. */
void term_write(const char* s);

#endif /* LIVEJQ_TERM_H */
