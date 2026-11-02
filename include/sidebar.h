/* livejq - the saved queries panel, left of the tree. */
#ifndef LIVEJQ_SIDEBAR_H
#define LIVEJQ_SIDEBAR_H

#include "defines.h"
#include "editor.h"
#include "grid.h"
#include "store.h"

typedef struct {
  store* store;
  b8 shown;
  i32 cursor;  /* index of the selected name */
  i32 top;     /* first name shown */
  i32 page;    /* names shown by the last render */
  b8 naming;   /* the name row for a new saved query is open */
  editor name; /* its text */
  b8 deleting; /* the selected name asks to be deleted */
} sidebar;

/* The columns the panel takes on a screen screen_w wide, 0 while hidden. */
i32 sidebar_width(const sidebar* s, i32 screen_w);
/* Draws the panel from column 0 into rows y..y+h-1, the name loaded marked,
 * scrolled to show the cursor. While naming, the name row comes first and
 * takes the terminal cursor; while deleting, the selected name asks for
 * confirmation. */
void sidebar_render(sidebar* s, grid* g, i32 y, i32 h, const char* loaded, b8 focused);
/* Moves for j k g G, arrows, Home End, PgUp PgDn. False when key is not one
 * of them. */
b8 sidebar_key(sidebar* s, u32 key);
/* Selects the name on row n of the list. False when there is none, or it
 * was already selected. */
b8 sidebar_click(sidebar* s, i32 n);
/* The selected name, NULL when the folder is empty. */
const char* sidebar_selected(const sidebar* s);
/* Selects name if it is listed, else keeps the cursor in range. */
void sidebar_select(sidebar* s, const char* name);

#endif /* LIVEJQ_SIDEBAR_H */
