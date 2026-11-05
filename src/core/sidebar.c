/* sidebar - lists the saved queries in a panel open on its left side. */
#include <string.h>

#include "app.h"
#include "sidebar.h"

#define H_LINE     "\xe2\x94\x80"
#define V_LINE     "\xe2\x94\x82"
#define TOP_END    "\xe2\x95\xae"
#define BOTTOM_END "\xe2\x95\xaf"
#define GUTTER     "\xe2\x94\x83"
#define TITLE      " saved "
#define TITLE_NEW  " save as "
#define PROMPT     "\xe2\x9d\xaf "
#define ASK        "delete "
#define HINT       "no saved queries"
#define HINT_SAVE  "ctrl-s saves one"
#define MIN_WIDTH  20
#define PADDING    4 /* a blank, the gutter, the name, a blank, the border */

static void border(grid* g, i32 y, i32 w, const char* end, u8 st);
static void clamp(sidebar* s);
static void name_row(sidebar* s, grid* g, i32 y, i32 w);
static void ask(grid* g, i32 y, i32 w, const char* name, u8 flag);

i32 sidebar_width(const sidebar* s, i32 screen_w) {
  if (!s->store || !s->shown) return 0;

  i32 w = MIN_WIDTH;
  for (usz i = 0; i < store_count(s->store); i++) {
    const char* name = store_name(s->store, i);
    i32 need = utf8_columns(name, strlen(name)) + PADDING;

    if (need > w) w = need;
  }

  if (w > screen_w / 3) w = screen_w / 3;
  return w;
}

void sidebar_render(sidebar* s, grid* g, i32 y, i32 h, const char* loaded, b8 focused) {
  i32 w = sidebar_width(s, g->w);
  u8 st = focused ? STYLE_BORDER_FOCUS : STYLE_BORDER;

  if (w < 4 || h < 3) return;

  border(g, y, w, TOP_END, st);
  border(g, y + h - 1, w, BOTTOM_END, st);
  const char* title = s->naming ? TITLE_NEW : TITLE;
  grid_put(g, 1, y, w - 1, title, strlen(title), focused ? STYLE_ACCENT : STYLE_LABEL);
  for (i32 i = 1; i < h - 1; i++)
    grid_put(g, w - 1, y + i, w, V_LINE, strlen(V_LINE), st);

  if (s->naming) {
    name_row(s, g, y + 1, w);
    y++;
    h--;
  }

  i32 n = (i32)store_count(s->store);
  s->page = h - 2;
  if (n == 0 && s->naming) return;

  if (n == 0) {
    grid_put_fit(g, 2, y + 1, w - 2, HINT, strlen(HINT), STYLE_EMPTY);
    grid_put_fit(g, 2, y + 2, w - 2, HINT_SAVE, strlen(HINT_SAVE), STYLE_EMPTY);
    return;
  }

  clamp(s);
  if (s->top > n - s->page) s->top = n - s->page;
  if (s->top < 0) s->top = 0;
  if (s->cursor < s->top) s->top = s->cursor;
  if (s->cursor >= s->top + s->page) s->top = s->cursor - s->page + 1;

  u8 selected = focused ? STYLE_SELECTED : STYLE_SELECTED_IDLE;
  for (i32 i = 0; i < s->page && s->top + i < n; i++) {
    i32 r = s->top + i;
    const char* name = store_name(s->store, (usz)r);
    u8 flag = r == s->cursor ? selected : 0;
    u8 name_st = loaded && strcmp(name, loaded) == 0 ? STYLE_ACCENT : STYLE_NORMAL;

    if (flag) {
      grid_fill_span(g, 1, w - 1, y + 1 + i, STYLE_NORMAL | flag);
      grid_put(g, 1, y + 1 + i, w - 1, GUTTER, strlen(GUTTER), STYLE_GUTTER | flag);
    }

    if (flag && s->deleting) ask(g, y + 1 + i, w, name, flag);
    else grid_put_fit(g, 2, y + 1 + i, w - 2, name, strlen(name), name_st | flag);
  }
}

b8 sidebar_key(sidebar* s, u32 key) {
  i32 n = (i32)store_count(s->store);
  i32 page = s->page > 1 ? s->page - 1 : 1;

  switch (key) {
    case 'k':
    case APP_KEY_UP:
      s->cursor--;
      break;
    case 'j':
    case APP_KEY_DOWN:
      s->cursor++;
      break;
    case 'g':
    case APP_KEY_HOME:
      s->cursor = 0;
      break;
    case 'G':
    case APP_KEY_END:
      s->cursor = n - 1;
      break;
    case APP_KEY_PAGE_UP:
      s->cursor -= page;
      break;
    case APP_KEY_PAGE_DOWN:
      s->cursor += page;
      break;
    default: return 0;
  }

  clamp(s);
  return 1;
}

b8 sidebar_click(sidebar* s, i32 n) {
  i32 r = s->top + n;

  if (n < 0 || n >= s->page || r >= (i32)store_count(s->store) || r == s->cursor) return 0;

  s->cursor = r;
  return 1;
}

b8 sidebar_scroll(sidebar* s, i32 n) {
  i32 top = s->top + n;
  i32 count = (i32)store_count(s->store);

  if (top > count - s->page) top = count - s->page;
  if (top < 0) top = 0;
  if (top == s->top) return 0;

  s->top = top;
  if (s->cursor < top) s->cursor = top;
  if (s->cursor >= top + s->page) s->cursor = top + s->page - 1;
  return 1;
}

const char* sidebar_selected(const sidebar* s) {
  if (s->cursor < 0 || s->cursor >= (i32)store_count(s->store)) return NULL;

  return store_name(s->store, (usz)s->cursor);
}

void sidebar_select(sidebar* s, const char* name) {
  i32 i = name ? store_find(s->store, name) : -1;

  if (i >= 0) s->cursor = i;

  clamp(s);
}

/* a horizontal border from column 0, ending in a corner at column w-1. */
static void border(grid* g, i32 y, i32 w, const char* end, u8 st) {
  for (i32 x = 0; x < w - 1; x++)
    grid_put(g, x, y, w, H_LINE, strlen(H_LINE), st);

  grid_put(g, w - 1, y, w, end, strlen(end), st);
}

/* the prompt and the name being typed, with the terminal cursor. */
static void name_row(sidebar* s, grid* g, i32 y, i32 w) {
  i32 x = grid_put(g, 2, y, w - 2, PROMPT, strlen(PROMPT), STYLE_ACCENT);

  g->cursor_x = editor_render(&s->name, g, x, y, w - 2, NULL);
  g->cursor_y = y;
}

/* "delete name?" in place of the name, the name cut to fit. */
static void ask(grid* g, i32 y, i32 w, const char* name, u8 flag) {
  i32 x = grid_put(g, 2, y, w - 2, ASK, strlen(ASK), STYLE_STATUS_ERROR | flag);

  x = grid_put_fit(g, x, y, w - 3, name, strlen(name), STYLE_ACCENT | flag);
  grid_put(g, x, y, w - 2, "?", 1, STYLE_STATUS_ERROR | flag);
}

static void clamp(sidebar* s) {
  i32 n = (i32)store_count(s->store);

  if (s->cursor >= n) s->cursor = n - 1;
  if (s->cursor < 0) s->cursor = 0;
}
