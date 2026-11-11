/* livejq - the whole of a long string, wrapped over a few rows. */
#ifndef LIVEJQ_DETAIL_H
#define LIVEJQ_DETAIL_H

#include "defines.h"
#include "grid.h"

/* Draws the len bytes of s into rows y..y+h-1 between columns x and max_x,
 * a row breaking after its last space when it has one, control characters
 * as escapes, and an ellipsis at the end when s does not fit. A NULL g only
 * measures. Returns the rows s takes, h at most. */
i32 detail_render(grid* g, i32 x, i32 y, i32 max_x, i32 h, const char* s, usz len);

#endif /* LIVEJQ_DETAIL_H */
