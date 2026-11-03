/* livejq - the keys of a focus zone, as an overlay or a bar. */
#ifndef LIVEJQ_HELP_H
#define LIVEJQ_HELP_H

#include "app.h"
#include "grid.h"

/* how far the overlay is scrolled, and how far it can be, as of its last
 * render. */
typedef struct {
  i32 top;  /* first row shown */
  i32 max;  /* top at the last row */
  i32 page; /* rows shown */
} help_view;

/* the sidebar's inline prompts. */
typedef enum {
  HELP_PROMPT_NAME = 0, /* the name row */
  HELP_PROMPT_DELETE,   /* the confirmation on the selected name */
  HELP_PROMPT_SEARCH,   /* the tree's search, its keys on the right */
} help_prompt;

/* Draws a box centered over g with the keys of the zone focus, tab leading
 * to the zone next, from row v->top on, scrolled when g is too small for all
 * of them. Clamps v->top and sets v->max and v->page. */
void help_render(grid* g, app_focus focus, app_focus next, help_view* v);
/* Scrolls v for key: rows, pages and ends. False when key does not scroll. */
b8 help_key(help_view* v, u32 key);
/* Scrolls v by n rows. False when it was already at that end. */
b8 help_scroll(help_view* v, i32 n);
/* Draws the main keys of the zone focus on row y, as many as fit, tab
 * leading to the zone next. */
void help_bar(grid* g, i32 y, app_focus focus, app_focus next);
/* Draws the keys of prompt p on row y. Returns the column they start at. */
i32 help_prompt_bar(grid* g, i32 y, help_prompt p);

#endif /* LIVEJQ_HELP_H */
