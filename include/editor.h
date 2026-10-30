/* livejq - a single-line text editor over UTF-8. */
#ifndef LIVEJQ_EDITOR_H
#define LIVEJQ_EDITOR_H

#include "defines.h"
#include "grid.h"

typedef struct {
  char* text; /* NUL-terminated, NULL while empty */
  usz len;
  usz cap;
  usz cursor; /* byte offset, on a code point boundary */
  i32 scroll; /* first visible column */
} editor;

void editor_set(editor* e, const char* s);
void editor_free(editor* e);
void editor_insert(editor* e, u32 c);
/* False when there is nothing before the cursor. */
b8 editor_backspace(editor* e);
void editor_left(editor* e);
void editor_right(editor* e);
void editor_home(editor* e);
void editor_end(editor* e);
/* Draws the text into row y from column x up to max_x, scrolled to keep the
 * cursor visible. Returns the cursor's column. */
i32 editor_render(editor* e, grid* g, i32 x, i32 y, i32 max_x, u8 st);

#endif /* LIVEJQ_EDITOR_H */
