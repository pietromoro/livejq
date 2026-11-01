/* help - the keys of the focused zone, in a box or on one line. */
#include <string.h>

#include "help.h"

#define SEP    " \xe2\x80\xa2 "
#define FOOTER "any key closes"

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
    { "t", "utc / local / date / ago, then back" },
    { "d", "base64 url hex uuid json jwt query, then back" },
    { "u", "time in s / ms / \xc2\xb5s / ns, then guessed" },
    { "0", "value as jq prints it" },
    { "a", "this view on every sibling with the same key" },
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

static i32 columns(const char* s);

void help_render(grid* g, app_focus focus) {
  const entry* keys = focus == FOCUS_TREE ? tree_keys : query_keys;
  usz n = focus == FOCUS_TREE ? ARRAY_LEN(tree_keys) : ARRAY_LEN(query_keys);
  const char* title = focus == FOCUS_TREE ? " tree keys " : " query keys ";
  i32 key_w = 0;
  i32 what_w = columns(FOOTER);
  i32 w;
  i32 h = (i32)n + 4;
  i32 x;
  i32 y;

  for (usz i = 0; i < n; i++) {
    if (columns(keys[i].keys) > key_w) key_w = columns(keys[i].keys);
    if (columns(keys[i].what) > what_w) what_w = columns(keys[i].what);
  }

  w = key_w + what_w + 7;
  if (w > g->w) w = g->w;
  if (h > g->h) h = g->h;

  x = (g->w - w) / 2;
  y = (g->h - h) / 2;
  for (i32 i = 1; i < h - 1; i++) grid_fill_span(g, x + 1, x + w - 1, y + i, STYLE_NORMAL);

  grid_box(g, x, y, w, h, STYLE_BORDER_FOCUS);
  grid_put_fit(g, x + 2, y, x + w - 1, title, strlen(title), STYLE_PILL);
  for (usz i = 0; i < n && (i32)i + 1 < h - 1; i++) {
    i32 at = x + 2;

    grid_put_fit(g, at, y + 1 + (i32)i, x + w - 1, keys[i].keys, strlen(keys[i].keys), STYLE_ACCENT);
    at += key_w + 3;
    grid_put_fit(g, at, y + 1 + (i32)i, x + w - 1, keys[i].what, strlen(keys[i].what), STYLE_NORMAL);
  }

  if (h == (i32)n + 4) grid_put_fit(g, x + 2, y + h - 2, x + w - 1, FOOTER, strlen(FOOTER), STYLE_HELP);
}

void help_bar(grid* g, i32 y, app_focus focus) {
  const entry* keys = focus == FOCUS_TREE ? tree_bar : query_bar;
  usz n = focus == FOCUS_TREE ? ARRAY_LEN(tree_bar) : ARRAY_LEN(query_bar);
  i32 x = 1;

  for (usz i = 0; i < n; i++) {
    i32 sep = i ? columns(SEP) : 0;

    if (x + sep + columns(keys[i].keys) + 1 + columns(keys[i].what) >= g->w) return;

    if (i) x = grid_puts(g, x, y, SEP, STYLE_HELP_SEP);
    x = grid_puts(g, x, y, keys[i].keys, STYLE_HELP_KEY);
    x = grid_puts(g, x + 1, y, keys[i].what, STYLE_HELP);
  }
}

static i32 columns(const char* s) {
  usz len = strlen(s);

  return utf8_columns(s, len);
}
