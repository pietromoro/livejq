/* help - the keys of the focused zone, in a box or on one line. */
#include <string.h>

#include "help.h"

#define SEP       " \xe2\x80\xa2 "
#define FOOTER    "any key closes"
#define SCROLL    "\xe2\x86\x91\xe2\x86\x93 scroll \xe2\x80\xa2 other keys close"
#define MORE_UP   "\xe2\x96\xb2"
#define MORE_DOWN "\xe2\x96\xbc"
#define THUMB     "\xe2\x96\x88"

typedef struct {
  const char* keys;
  const char* what;
} entry;

static const entry query_keys[] = {
    { "\xe2\x86\x90 \xe2\x86\x92", "move" },
    { "ctrl-\xe2\x86\x90 \xe2\x86\x92  alt-b f", "move by word" },
    { "home end  ctrl-a e", "start / end" },
    { "backspace delete", "delete" },
    { "ctrl-w  alt-backspace", "delete word" },
    { "ctrl-u  ctrl-k", "delete to start / end" },
    { "tab", "tree" },
    { "F1", "help" },
    { "ctrl-c", "quit" },
};

static const entry tree_keys[] = {
    { "j k  \xe2\x86\x93 \xe2\x86\x91", "move" },
    { "h l  \xe2\x86\x90 \xe2\x86\x92", "fold / unfold, \xe2\x86\x90 on a leaf: parent" },
    { "space enter", "toggle" },
    { "H L", "fold / unfold subtree" },
    { "g G  home end", "top / bottom" },
    { "pgup pgdn", "page" },
    { "v V", "next / previous view" },
    { "x o b e", "hex / octal / binary / exponent, again: back" },
    { "D", "duration in seconds, again: back" },
    { "s", "size in iec / si units, then back" },
    { "t", "utc / local / date / ago, then back" },
    { "d", "base64 url hex uuid json jwt query, then back" },
    { "u", "time or duration in s / ms / \xc2\xb5s / ns, then back" },
    { "0", "value as jq prints it" },
    { "a", "this view on every sibling with the same key" },
    { "tab", "query" },
    { "? F1", "help" },
    { "q  ctrl-c", "quit" },
};

static const entry sidebar_keys[] = {
    { "j k  \xe2\x86\x93 \xe2\x86\x91", "move" },
    { "g G  home end", "top / bottom" },
    { "pgup pgdn", "page" },
    { "enter", "load into the query" },
    { "ctrl-b", "hide / show" },
    { "tab", "query" },
    { "? F1", "help" },
    { "q  ctrl-c", "quit" },
};

static const entry query_bar[] = {
    { "\xe2\x86\x90\xe2\x86\x92", "move" },
    { "ctrl-w", "delete word" },
    { "tab", "tree" },
    { "F1", "help" },
    { "ctrl-c", "quit" },
};

static const entry tree_bar[] = {
    { "\xe2\x86\x91\xe2\x86\x93", "move" },
    { "\xe2\x86\x90\xe2\x86\x92", "fold" },
    { "space", "toggle" },
    { "tab", "query" },
    { "?", "help" },
    { "q", "quit" },
};

static const entry sidebar_bar[] = {
    { "\xe2\x86\x91\xe2\x86\x93", "move" },
    { "enter", "load" },
    { "ctrl-b", "hide" },
    { "tab", "query" },
    { "?", "help" },
    { "q", "quit" },
};

/* by app_focus. */
static const struct {
  const entry* keys;
  i32 n;
  const char* title;
  const entry* bar;
  usz bar_n;
} zones[] = {
    { query_keys, ARRAY_LEN(query_keys), " query keys ", query_bar, ARRAY_LEN(query_bar) },
    { tree_keys, ARRAY_LEN(tree_keys), " tree keys ", tree_bar, ARRAY_LEN(tree_bar) },
    { sidebar_keys, ARRAY_LEN(sidebar_keys), " saved query keys ", sidebar_bar, ARRAY_LEN(sidebar_bar) },
};

static void scrollbar(grid* g, i32 x, i32 y, const help_view* v, i32 n);
static i32 key_rows(const help_view* v, u32 key);
static i32 columns(const char* s);

void help_render(grid* g, app_focus focus, help_view* v) {
  const entry* keys = zones[focus].keys;
  i32 n = zones[focus].n;
  const char* title = zones[focus].title;
  i32 key_w = 0;
  i32 what_w = columns(SCROLL);
  i32 w;
  i32 h = n + 4;
  i32 x;
  i32 y;

  for (i32 i = 0; i < n; i++) {
    if (columns(keys[i].keys) > key_w) key_w = columns(keys[i].keys);
    if (columns(keys[i].what) > what_w) what_w = columns(keys[i].what);
  }

  w = key_w + what_w + 7;
  if (w > g->w) w = g->w;
  if (h > g->h) h = g->h;

  v->page = h - 4 > 0 ? h - 4 : h > 2 ? h - 2 : 1;
  v->max = n - v->page > 0 ? n - v->page : 0;
  if (v->top > v->max) v->top = v->max;
  if (v->top < 0) v->top = 0;

  x = (g->w - w) / 2;
  y = (g->h - h) / 2;
  for (i32 i = 1; i < h - 1; i++) grid_fill_span(g, x + 1, x + w - 1, y + i, STYLE_NORMAL);

  grid_box(g, x, y, w, h, STYLE_BORDER_FOCUS);
  grid_put_fit(g, x + 2, y, x + w - 1, title, strlen(title), STYLE_PILL);
  for (i32 i = 0; i < v->page && v->top + i < n; i++) {
    const entry* e = &keys[v->top + i];
    i32 at = x + 2;

    grid_put_fit(g, at, y + 1 + i, x + w - 1, e->keys, strlen(e->keys), STYLE_ACCENT);
    at += key_w + 3;
    grid_put_fit(g, at, y + 1 + i, x + w - 1, e->what, strlen(e->what), STYLE_NORMAL);
  }

  if (v->max) scrollbar(g, x + w - 1, y + 1, v, n);

  if (h - 4 < 1) return;

  const char* footer = v->max ? SCROLL : FOOTER;

  grid_put_fit(g, x + 2, y + h - 2, x + w - 1, footer, strlen(footer), STYLE_HELP);
}

b8 help_key(help_view* v, u32 key) {
  i32 n = key_rows(v, key);

  if (n == 0) return 0;

  help_scroll(v, n);
  return 1;
}

b8 help_scroll(help_view* v, i32 n) {
  i32 top = v->top + n;

  if (top > v->max) top = v->max;
  if (top < 0) top = 0;
  if (top == v->top) return 0;

  v->top = top;
  return 1;
}

void help_bar(grid* g, i32 y, app_focus focus) {
  const entry* keys = zones[focus].bar;
  usz n = zones[focus].bar_n;
  i32 x = 1;

  for (usz i = 0; i < n; i++) {
    i32 sep = i ? columns(SEP) : 0;

    if (x + sep + columns(keys[i].keys) + 1 + columns(keys[i].what) >= g->w) return;

    if (i) x = grid_puts(g, x, y, SEP, STYLE_HELP_SEP);
    x = grid_puts(g, x, y, keys[i].keys, STYLE_HELP_KEY);
    x = grid_puts(g, x + 1, y, keys[i].what, STYLE_HELP);
  }
}

/* arrows at the ends of the v->page rows of border from x,y, and between
 * them a thick stretch as long and as far down as the rows shown of n. */
static void scrollbar(grid* g, i32 x, i32 y, const help_view* v, i32 n) {
  i32 track = v->page - 2;
  i32 size = (track * v->page + n / 2) / n;
  i32 at;

  grid_put(g, x, y, x + 1, MORE_UP, strlen(MORE_UP), STYLE_BORDER_FOCUS);
  grid_put(g, x, y + v->page - 1, x + 1, MORE_DOWN, strlen(MORE_DOWN), STYLE_BORDER_FOCUS);
  if (track < 1) return;

  if (size < 1) size = 1;
  at = ((track - size) * v->top + v->max / 2) / v->max;
  for (i32 i = 0; i < size; i++)
    grid_put(g, x, y + 1 + at + i, x + 1, THUMB, strlen(THUMB), STYLE_BORDER_FOCUS);
}

/* the rows key scrolls v by, past the end for the ends, 0 when it does
 * not scroll. */
static i32 key_rows(const help_view* v, u32 key) {
  switch (key) {
    case 'k':
    case APP_KEY_UP: return -1;
    case 'j':
    case APP_KEY_DOWN: return 1;
    case APP_KEY_PAGE_UP: return -v->page;
    case APP_KEY_PAGE_DOWN: return v->page;
    case 'g':
    case APP_KEY_HOME: return -v->max - 1;
    case 'G':
    case APP_KEY_END: return v->max + 1;
    default: return 0;
  }
}

static i32 columns(const char* s) {
  usz len = strlen(s);

  return utf8_columns(s, len);
}
