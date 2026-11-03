/* grid - a width by height array of styled code points. */
#include <stdlib.h>
#include <string.h>

#include "grid.h"

#define H_LINE "\xe2\x94\x80"
#define V_LINE "\xe2\x94\x82"

static u32 utf8_next(const u8* s, usz len, usz* i);

grid* grid_new(i32 w, i32 h) {
  grid* g = malloc(sizeof(*g));

  if (!g) return NULL;

  g->w = w;
  g->h = h;
  g->cells = malloc(sizeof(cell) * (usz)(w * h));
  if (!g->cells) {
    free(g);
    return NULL;
  }

  grid_clear(g);
  return g;
}

void grid_free(grid* g) {
  if (!g) return;

  free(g->cells);
  free(g);
}

void grid_clear(grid* g) {
  g->cursor_x = -1;
  g->cursor_y = -1;
  for (i32 y = 0; y < g->h; y++)
    grid_fill(g, y, STYLE_NORMAL);
}

void grid_fill(grid* g, i32 y, u8 st) { grid_fill_span(g, 0, g->w, y, st); }

void grid_fill_span(grid* g, i32 x, i32 max_x, i32 y, u8 st) {
  if (y < 0 || y >= g->h) return;

  if (x < 0) x = 0;
  if (max_x > g->w) max_x = g->w;
  for (; x < max_x; x++) {
    g->cells[y * g->w + x].ch = ' ';
    g->cells[y * g->w + x].style = st;
  }
}

void grid_restyle(grid* g, i32 x, i32 max_x, i32 y, u8 st) {
  if (y < 0 || y >= g->h) return;

  if (x < 0) x = 0;
  if (max_x > g->w) max_x = g->w;
  for (; x < max_x; x++) g->cells[y * g->w + x].style = st;
}

void grid_box(grid* g, i32 x, i32 y, i32 w, i32 h, u8 st) {
  if (w < 2 || h < 2) return;

  static const char* const corners[] = { "\xe2\x95\xad", "\xe2\x95\xae", "\xe2\x95\xb0", "\xe2\x95\xaf" };
  for (i32 i = 1; i < w - 1; i++) {
    grid_put(g, x + i, y, x + w, H_LINE, 3, st);
    grid_put(g, x + i, y + h - 1, x + w, H_LINE, 3, st);
  }

  for (i32 i = 1; i < h - 1; i++) {
    grid_put(g, x, y + i, x + w, V_LINE, 3, st);
    grid_put(g, x + w - 1, y + i, x + w, V_LINE, 3, st);
  }

  grid_put(g, x, y, x + w, corners[0], 3, st);
  grid_put(g, x + w - 1, y, x + w, corners[1], 3, st);
  grid_put(g, x, y + h - 1, x + w, corners[2], 3, st);
  grid_put(g, x + w - 1, y + h - 1, x + w, corners[3], 3, st);
}

i32 grid_put(grid* g, i32 x, i32 y, i32 max_x, const char* s, usz len, u8 st) {
  usz i = 0;

  if (max_x > g->w) max_x = g->w;
  if (y < 0 || y >= g->h) return x;

  while (i < len && x < max_x) {
    u32 c = utf8_next((const u8*)s, len, &i);

    if (c < 0x20 || c == 0x7f) c = 0xfffd;
    if (x >= 0) {
      g->cells[y * g->w + x].ch = c;
      g->cells[y * g->w + x].style = st;
    }

    x++;
  }

  return x;
}

i32 grid_put_fit(grid* g, i32 x, i32 y, i32 max_x, const char* s, usz len, u8 st) {
  i32 n = utf8_columns(s, len);

  if (max_x > g->w) max_x = g->w;
  if (x >= max_x) return x;

  if (x + n <= max_x) return grid_put(g, x, y, max_x, s, len, st);

  grid_put(g, x, y, max_x - 1, s, len, st);
  return grid_put(g, max_x - 1, y, max_x, "\xe2\x80\xa6", 3, st);
}

i32 grid_puts(grid* g, i32 x, i32 y, const char* s, u8 st) {
  return grid_put(g, x, y, g->w, s, strlen(s), st);
}

i32 utf8_columns(const char* s, usz len) {
  i32 n = 0;

  for (usz i = 0; i < len; i++) n += ((u8)s[i] & 0xc0) != 0x80;

  return n;
}

cell grid_at(const grid* g, i32 x, i32 y) { return g->cells[y * g->w + x]; }

void grid_line(const grid* g, i32 y, char* buf, usz cap) {
  usz n = 0;
  usz keep = 0;

  for (i32 x = 0; x < g->w && n + 4 < cap; x++) {
    u32 c = g->cells[y * g->w + x].ch;

    n += utf8_put(c, buf + n);
    if (c != ' ') keep = n;
  }

  buf[keep] = '\0';
}

usz utf8_put(u32 c, char* out) {
  if (c < 0x80) {
    out[0] = (char)c;
    return 1;
  }

  if (c < 0x800) {
    out[0] = (char)(0xc0 | (c >> 6));
    out[1] = (char)(0x80 | (c & 0x3f));
    return 2;
  }

  if (c < 0x10000) {
    out[0] = (char)(0xe0 | (c >> 12));
    out[1] = (char)(0x80 | ((c >> 6) & 0x3f));
    out[2] = (char)(0x80 | (c & 0x3f));
    return 3;
  }

  out[0] = (char)(0xf0 | (c >> 18));
  out[1] = (char)(0x80 | ((c >> 12) & 0x3f));
  out[2] = (char)(0x80 | ((c >> 6) & 0x3f));
  out[3] = (char)(0x80 | (c & 0x3f));
  return 4;
}

static u32 utf8_next(const u8* s, usz len, usz* i) {
  u32 c = s[(*i)++];
  i32 more;

  if (c < 0x80) return c;

  if ((c & 0xe0) == 0xc0) {
    c &= 0x1f;
    more = 1;
  } else if ((c & 0xf0) == 0xe0) {
    c &= 0x0f;
    more = 2;
  } else if ((c & 0xf8) == 0xf0) {
    c &= 0x07;
    more = 3;
  } else
    return 0xfffd;

  while (more-- > 0) {
    if (*i >= len || (s[*i] & 0xc0) != 0x80) return 0xfffd;

    c = (c << 6) | (s[(*i)++] & 0x3f);
  }

  return c;
}
