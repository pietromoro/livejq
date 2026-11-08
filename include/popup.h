/* livejq - the completion popup under the query. */
#ifndef LIVEJQ_POPUP_H
#define LIVEJQ_POPUP_H

#include <jv.h>

#include "defines.h"
#include "grid.h"

typedef struct {
  jv items;   /* [[name, detail], ...] in the order found */
  jv seen;    /* name -> its index in items */
  i32* shown; /* indices of the items matching what is typed, best first */
  i32 n;      /* how many */
  i32 cursor;
  i32 top;   /* first shown */
  u8 style;  /* of the names */
} popup;

void popup_init(popup* p);
void popup_free(popup* p);
/* Drops every item. */
void popup_clear(popup* p);
/* Adds the [name, detail] pairs of each list in lists that are not in yet; a
 * known name with a "null" detail takes the new one. Consumes lists. */
void popup_merge(popup* p, jv lists);
/* Shows the items matching the len bytes of typed: those starting with it,
 * then those containing it, ignoring case unless typed has a capital. The
 * cursor goes back to the first. */
void popup_filter(popup* p, const char* typed, usz len);
/* Moves for up and down, wrapping around. False when key is not one of
 * them. */
b8 popup_key(popup* p, u32 key);
/* The selected name, NULL when nothing matches. */
const char* popup_selected(const popup* p);
/* Draws the box with its top left corner at x, y, moved left to fit, at
 * most rows names high. Nothing when nothing matches. */
void popup_render(popup* p, grid* g, i32 x, i32 y, i32 rows);

#endif /* LIVEJQ_POPUP_H */
