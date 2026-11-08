/* popup - the candidates for the word at the query's cursor. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "popup.h"

#define GUTTER  "\xe2\x94\x83"
#define CTRL(c) ((c) & 0x1f)
#define PADDING 6 /* the borders, the gutter, two blanks between, one after */

static void clamp(popup* p, i32 rows);
static b8 matches(const char* name, const char* typed, usz len, b8 fold, b8 prefix);
static const char* name_at(const popup* p, i32 i);
static const char* detail_at(const popup* p, i32 i);
static i32 columns(const char* s);

void popup_init(popup* p) {
  memset(p, 0, sizeof(*p));
  p->items = jv_array();
  p->seen = jv_object();
  p->style = STYLE_KEY;
}

void popup_free(popup* p) {
  jv_free(p->items);
  jv_free(p->seen);
  free(p->shown);
  memset(p, 0, sizeof(*p));
}

void popup_clear(popup* p) {
  u8 style = p->style;

  popup_free(p);
  popup_init(p);
  p->style = style;
}

void popup_merge(popup* p, jv lists) {
  jv_array_foreach(lists, i, list) {
    jv_array_foreach(list, k, pair) {
      jv name = jv_array_get(jv_copy(pair), 0);
      jv at = jv_object_get(jv_copy(p->seen), jv_copy(name));

      if (jv_get_kind(at) == JV_KIND_NUMBER) {
        i32 idx = (i32)jv_number_value(at);
        jv old = jv_array_get(jv_array_get(jv_copy(p->items), idx), 1);

        if (strcmp(jv_string_value(old), "null") == 0)
          p->items = jv_array_set(p->items, idx, jv_copy(pair));

        jv_free(old);
        jv_free(name);
        jv_free(pair);
      } else {
        i32 n = jv_array_length(jv_copy(p->items));

        p->seen = jv_object_set(p->seen, name, jv_number(n));
        p->items = jv_array_append(p->items, pair);
      }

      jv_free(at);
      UNUSED(k);
    }

    jv_free(list);
    UNUSED(i);
  }

  jv_free(lists);
}

void popup_filter(popup* p, const char* typed, usz len) {
  i32 total = jv_array_length(jv_copy(p->items));
  b8 fold = 1;

  for (usz k = 0; k < len; k++)
    if (isupper((u8)typed[k])) fold = 0;

  free(p->shown);
  p->shown = malloc(sizeof(i32) * (usz)(total + 1));
  if (!p->shown) abort();

  p->n = 0;
  for (i32 i = 0; i < total; i++)
    if (matches(name_at(p, i), typed, len, fold, 1)) p->shown[p->n++] = i;

  for (i32 i = 0; i < total; i++) {
    const char* name = name_at(p, i);

    if (!matches(name, typed, len, fold, 1) && matches(name, typed, len, fold, 0))
      p->shown[p->n++] = i;
  }

  p->name_w = 0;
  p->detail_w = 0;
  for (i32 i = 0; i < p->n; i++) {
    i32 nw = columns(name_at(p, p->shown[i]));
    i32 dw = columns(detail_at(p, p->shown[i]));

    if (nw > p->name_w) p->name_w = nw;
    if (dw > p->detail_w) p->detail_w = dw;
  }

  p->cursor = 0;
  p->top = 0;
}

b8 popup_key(popup* p, u32 key) {
  if (p->n == 0) return key == APP_KEY_UP || key == APP_KEY_DOWN;

  switch (key) {
    case APP_KEY_UP:
    case CTRL('p'):
      p->cursor = (p->cursor + p->n - 1) % p->n;
      return 1;
    case APP_KEY_DOWN:
    case CTRL('n'):
      p->cursor = (p->cursor + 1) % p->n;
      return 1;
    default: return 0;
  }
}

const char* popup_selected(const popup* p) {
  if (p->n == 0) return NULL;

  return name_at(p, p->shown[p->cursor]);
}

void popup_render(popup* p, grid* g, i32 x, i32 y, i32 rows) {
  if (p->n == 0 || rows < 1) return;

  i32 w = p->name_w + p->detail_w + PADDING;
  if (w > g->w) w = g->w;
  if (x + w > g->w) x = g->w - w;
  if (x < 0) x = 0;

  clamp(p, rows);

  i32 shown = p->n < rows ? p->n : rows;
  for (i32 i = 0; i < shown; i++) {
    i32 r = p->top + i;
    const char* detail = detail_at(p, p->shown[r]);
    u8 flag = r == p->cursor ? STYLE_SELECTED : 0;

    grid_fill_span(g, x + 1, x + w - 1, y + 1 + i, STYLE_NORMAL | flag);
    if (flag) grid_put(g, x + 1, y + 1 + i, x + 2, GUTTER, strlen(GUTTER), STYLE_GUTTER | flag);

    const char* name = name_at(p, p->shown[r]);
    i32 dx = x + w - 2 - columns(detail);
    grid_put_fit(g, x + 2, y + 1 + i, dx - 1, name, strlen(name), p->style | flag);
    grid_put(g, dx, y + 1 + i, x + w - 1, detail, strlen(detail), STYLE_EMPTY | flag);
  }

  grid_box(g, x, y, w, shown + 2, STYLE_BORDER_FOCUS);
  if (p->n <= rows) return;

  char count[32];
  snprintf(count, sizeof(count), " %d/%d ", p->cursor + 1, p->n);
  i32 cx = x + w - 1 - columns(count);
  if (cx > x) grid_put(g, cx, y + shown + 1, x + w - 1, count, strlen(count), STYLE_BORDER);
}

static void clamp(popup* p, i32 rows) {
  if (p->cursor < p->top) p->top = p->cursor;
  if (p->cursor >= p->top + rows) p->top = p->cursor - rows + 1;
  if (p->top > p->n - rows) p->top = p->n - rows;
  if (p->top < 0) p->top = 0;
}

static b8 matches(const char* name, const char* typed, usz len, b8 fold, b8 prefix) {
  usz n = strlen(name);

  for (usz at = 0; at + len <= n; at++) {
    usz k = 0;

    while (k < len) {
      char a = name[at + k];
      char b = typed[k];

      if (fold ? tolower((u8)a) != tolower((u8)b) : a != b) break;
      k++;
    }

    if (k == len) return 1;
    if (prefix) return 0;
  }

  return 0;
}

/* items keeps the strings alive. */
static const char* name_at(const popup* p, i32 i) {
  jv pair = jv_array_get(jv_copy(p->items), i);
  jv name = jv_array_get(jv_copy(pair), 0);
  const char* s = jv_string_value(name);

  jv_free(name);
  jv_free(pair);
  return s;
}

static const char* detail_at(const popup* p, i32 i) {
  jv pair = jv_array_get(jv_copy(p->items), i);
  jv detail = jv_array_get(jv_copy(pair), 1);
  const char* s = jv_string_value(detail);

  jv_free(detail);
  jv_free(pair);
  return s;
}

static i32 columns(const char* s) {
  usz len = strlen(s);

  return utf8_columns(s, len);
}
