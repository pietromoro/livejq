/* livejq - syntax highlighting of jq queries, tolerant of partial input. */
#ifndef LIVEJQ_HIGHLIGHT_H
#define LIVEJQ_HIGHLIGHT_H

#include "defines.h"

/* Gives each of the len bytes of s a style into styles. An unmatched
 * bracket, the quote of an unterminated string, an unclosed interpolation
 * and a character jq does not know get STYLE_ERROR. */
void highlight(const char* s, usz len, u8* styles);

#endif /* LIVEJQ_HIGHLIGHT_H */
