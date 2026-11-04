/* livejq - text for the terminal's clipboard. */
#ifndef LIVEJQ_CLIP_H
#define LIVEJQ_CLIP_H

#include "defines.h"

/* The OSC 52 sequence that sets the clipboard to the len bytes of s, which
 * the terminal handles even over ssh. The caller frees it. */
char* clip_osc52(const char* s, usz len);

#endif /* LIVEJQ_CLIP_H */
