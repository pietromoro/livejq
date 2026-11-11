/* detail - a string wrapped over rows, as its text rather than as json. */
#include <stdio.h>
#include <string.h>

#include "detail.h"

#define ELLIPSIS "\xe2\x80\xa6"

static usz lay_row(const char* s, usz len, usz from, i32 width, b8 words);
static i32 draw_row(grid* g, i32 x, i32 y, i32 max_x, const char* s, usz from, usz to);
static usz piece(const char* s, usz len, usz i, char* esc, i32* w);

i32 detail_render(grid* g, i32 x, i32 y, i32 max_x, i32 h, const char* s, usz len) {
  i32 width = max_x - x;
  usz at = 0;
  i32 rows = 0;

  if (width < 2) return 0;

  while (rows < h && at < len) {
    usz end = lay_row(s, len, at, width, 1);
    b8 last = end < len && rows == h - 1;

    if (last) end = lay_row(s, len, at, width - 1, 0);
    if (g) {
      i32 col = draw_row(g, x, y + rows, max_x, s, at, end);

      if (last) grid_put(g, col, y + rows, max_x, ELLIPSIS, 3, STYLE_EMPTY);
    }

    at = end;
    rows++;
  }

  return rows;
}

/* where a row of width columns from byte from ends: after its last space
 * when words is set and it has one, else after the last character that
 * fits, a space past the edge included, and never before the first one. */
static usz lay_row(const char* s, usz len, usz from, i32 width, b8 words) {
  usz i = from;
  usz space = from;
  i32 col = 0;
  char esc[8];

  while (i < len) {
    i32 w;
    usz next = piece(s, len, i, esc, &w);

    if (col + w > width && i > from) {
      if (words && s[i] == ' ') return next;

      break;
    }

    col += w;
    i = next;
    if (s[i - 1] == ' ') space = i;
  }

  return i < len && words && space > from ? space : i;
}

/* draws bytes from..to of s from column x, escapes faint. Returns the
 * column after them. */
static i32 draw_row(grid* g, i32 x, i32 y, i32 max_x, const char* s, usz from, usz to) {
  usz run = from;
  usz i = from;
  char esc[8];

  while (i < to) {
    i32 w;
    usz next = piece(s, to, i, esc, &w);

    if (esc[0]) {
      x = grid_put(g, x, y, max_x, s + run, i - run, STYLE_STRING);
      x = grid_put(g, x, y, max_x, esc, strlen(esc), STYLE_EMPTY);
      run = next;
    }

    i = next;
  }

  return grid_put(g, x, y, max_x, s + run, to - run, STYLE_STRING);
}

/* the character at byte i, the escape standing for it in esc, "" when it
 * stands for itself, and its columns in *w. Returns the byte after it. */
static usz piece(const char* s, usz len, usz i, char* esc, i32* w) {
  u32 c = utf8_next(s, len, &i);

  esc[0] = '\0';
  if (c == '\n') strcpy(esc, "\\n");
  else if (c == '\t') strcpy(esc, "\\t");
  else if (c == '\r') strcpy(esc, "\\r");
  else if (c < 0x20 || (c >= 0x7f && c < 0xa0)) snprintf(esc, 8, "\\u%04x", c);

  *w = esc[0] ? (i32)strlen(esc) : utf8_width(c);
  return i;
}
