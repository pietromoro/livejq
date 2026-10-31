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
/* The deletions return false when nothing was deleted. */
b8 editor_backspace(editor* e);
b8 editor_delete(editor* e);
/* Deletes back to where editor_word_left would land. */
b8 editor_delete_word(editor* e);
b8 editor_delete_to_start(editor* e);
b8 editor_delete_to_end(editor* e);
void editor_left(editor* e);
void editor_right(editor* e);
/* Moves to the start of the word before the cursor, or past the end of the
 * word after it. */
void editor_word_left(editor* e);
void editor_word_right(editor* e);
void editor_home(editor* e);
void editor_end(editor* e);
/* Draws the text into row y from column x up to max_x, scrolled to keep the
 * cursor visible, each byte in its style from styles (STYLE_NORMAL when
 * NULL). Returns the cursor's column. */
i32 editor_render(editor* e, grid* g, i32 x, i32 y, i32 max_x, const u8* styles);

#endif /* LIVEJQ_EDITOR_H */
