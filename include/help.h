/* livejq - the keys of a focus zone, as an overlay or a bar. */
#ifndef LIVEJQ_HELP_H
#define LIVEJQ_HELP_H

#include "app.h"
#include "grid.h"

/* Draws a box centered over g with the keys of the zone focus, clipped when
 * g is too small. */
void help_render(grid* g, app_focus focus);
/* Draws the main keys of the zone focus on row y, as many as fit. */
void help_bar(grid* g, i32 y, app_focus focus);

#endif /* LIVEJQ_HELP_H */
