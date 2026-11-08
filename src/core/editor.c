/* editor - a single-line text editor, the cursor kept on code points. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"

static b8 cut(editor* e, usz from, usz to);
static b8 word_byte(char c);
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

  editor_left(e);
  return cut(e, e->cursor, end);
}

b8 editor_delete(editor* e) {
  usz from = e->cursor;

  editor_right(e);
  return cut(e, from, e->cursor);
}

b8 editor_delete_word(editor* e) {
  usz end = e->cursor;

  editor_word_left(e);
  return cut(e, e->cursor, end);
}

b8 editor_delete_to_start(editor* e) { return cut(e, 0, e->cursor); }

b8 editor_delete_to_end(editor* e) { return cut(e, e->cursor, e->len); }

void editor_left(editor* e) {
  while (e->cursor > 0 && ((u8)e->text[--e->cursor] & 0xc0) == 0x80) {}
}

void editor_right(editor* e) {
  if (e->cursor >= e->len) return;

  while (++e->cursor < e->len && ((u8)e->text[e->cursor] & 0xc0) == 0x80) {}
}

/* a word is a run of letters, digits, underscores and non-ascii text. */
void editor_word_left(editor* e) {
  while (e->cursor > 0 && !word_byte(e->text[e->cursor - 1])) e->cursor--;
  while (e->cursor > 0 && word_byte(e->text[e->cursor - 1])) e->cursor--;
}

void editor_word_right(editor* e) {
  while (e->cursor < e->len && !word_byte(e->text[e->cursor])) e->cursor++;
  while (e->cursor < e->len && word_byte(e->text[e->cursor])) e->cursor++;
}

void editor_home(editor* e) { e->cursor = 0; }

void editor_end(editor* e) { e->cursor = e->len; }

/* the cell after the text is kept for the cursor, and the text is not left
 * scrolled further than it needs. */
i32 editor_render(editor* e, grid* g, i32 x, i32 y, i32 max_x, const u8* styles) {
  i32 width = max_x - x;
  i32 at = columns(e->text, 0, e->cursor);
  i32 total = at + columns(e->text, e->cursor, e->len);
  i32 col = x;
  usz from;

  if (width <= 0) return x;

  if (at < e->scroll) e->scroll = at;
  if (at >= e->scroll + width) e->scroll = at - width + 1;
  if (total - e->scroll < width - 1) e->scroll = total - width + 1;
  if (e->scroll < 0) e->scroll = 0;

  from = skip_columns(e->text, e->len, e->scroll);
  col += columns(e->text, 0, from) - e->scroll;
  while (from < e->len && col < max_x) {
    u8 st = styles ? styles[from] : STYLE_NORMAL;
    usz to = from + 1;

    while (to < e->len && (!styles || styles[to] == st || ((u8)e->text[to] & 0xc0) == 0x80)) to++;

    col = grid_put(g, col, y, max_x, e->text + from, to - from, st);
    from = to;
  }

  return x + at - e->scroll;
}

static b8 cut(editor* e, usz from, usz to) {
  if (from == to) return 0;

  memmove(e->text + from, e->text + to, e->len - to);
  e->len -= to - from;
  e->text[e->len] = '\0';
  e->cursor = from;
  return 1;
}

static b8 word_byte(char c) {
  u8 b = (u8)c;

  return b >= 0x80 || isalnum(b) || b == '_';
}

static i32 columns(const char* s, usz from, usz to) {
  if (!s) return 0;

  return utf8_columns(s + from, to - from);
}

/* the byte where the text drawn from column n on starts, past a wide
 * character that column n would cut. */
static usz skip_columns(const char* s, usz len, i32 n) {
  usz i = 0;

  while (i < len && n > 0) n -= utf8_width(utf8_next(s, len, &i));

  return i;
}
