/* editor - a single-line text editor, the cursor kept on code points. */
#include <stdlib.h>
#include <string.h>

#include "editor.h"

static i32 columns(const char* s, usz from, usz to);
static usz skip_columns(const char* s, usz len, i32 n);

void editor_set(editor* e, const char* s) {
  usz n = strlen(s);

  editor_free(e);
  e->text = malloc(n + 1);
  if (!e->text) abort();

  memcpy(e->text, s, n + 1);
  e->len = n;
  e->cap = n + 1;
  e->cursor = n;
}

void editor_free(editor* e) {
  free(e->text);
  memset(e, 0, sizeof(*e));
}

void editor_insert(editor* e, u32 c) {
  char buf[4];
  usz n = utf8_put(c, buf);

  if (e->len + n + 1 > e->cap) {
    e->cap = (e->len + n + 1) * 2;
    e->text = realloc(e->text, e->cap);
    if (!e->text) abort();
  }

  memmove(e->text + e->cursor + n, e->text + e->cursor, e->len - e->cursor);
  memcpy(e->text + e->cursor, buf, n);
  e->len += n;
  e->cursor += n;
  e->text[e->len] = '\0';
}

b8 editor_backspace(editor* e) {
  usz end = e->cursor;

  if (end == 0) return 0;

  editor_left(e);
  memmove(e->text + e->cursor, e->text + end, e->len - end);
  e->len -= end - e->cursor;
  e->text[e->len] = '\0';
  return 1;
}

void editor_left(editor* e) {
  while (e->cursor > 0 && ((u8)e->text[--e->cursor] & 0xc0) == 0x80) {}
}

void editor_right(editor* e) {
  if (e->cursor >= e->len) return;

  while (++e->cursor < e->len && ((u8)e->text[e->cursor] & 0xc0) == 0x80) {}
}

void editor_home(editor* e) { e->cursor = 0; }

void editor_end(editor* e) { e->cursor = e->len; }

/* the cell after the text is kept for the cursor, and the text is not left
 * scrolled further than it needs. */
i32 editor_render(editor* e, grid* g, i32 x, i32 y, i32 max_x, u8 st) {
  i32 width = max_x - x;
  i32 at = columns(e->text, 0, e->cursor);
  i32 total = at + columns(e->text, e->cursor, e->len);
  usz from;

  if (width <= 0) return x;

  if (at < e->scroll) e->scroll = at;
  if (at >= e->scroll + width) e->scroll = at - width + 1;
  if (total - e->scroll < width - 1) e->scroll = total - width + 1;
  if (e->scroll < 0) e->scroll = 0;

  from = skip_columns(e->text, e->len, e->scroll);
  grid_put(g, x, y, max_x, e->text + from, e->len - from, st);
  return x + at - e->scroll;
}

static i32 columns(const char* s, usz from, usz to) {
  i32 n = 0;

  for (usz i = from; i < to; i++) n += ((u8)s[i] & 0xc0) != 0x80;

  return n;
}

static usz skip_columns(const char* s, usz len, i32 n) {
  usz i = 0;

  while (i < len && n > 0) {
    i++;
    while (i < len && ((u8)s[i] & 0xc0) == 0x80) i++;
    n--;
  }

  return i;
}
