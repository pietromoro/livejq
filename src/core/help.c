/* help - a box over the screen with the keys of the focused zone. */
#include <string.h>

#include "help.h"

#define H_LINE "\xe2\x94\x80"
#define V_LINE "\xe2\x94\x82"
#define FOOTER "any key closes"

typedef struct {
  const char* keys;
  const char* what;
} entry;

static const entry query_keys[] = {
    { "\xe2\x86\x90 \xe2\x86\x92", "move" },
    { "Ctrl-\xe2\x86\x90 \xe2\x86\x92  Alt-b f", "move by word" },
    { "Home End  Ctrl-A E", "start / end" },
    { "Backspace Delete", "delete" },
    { "Ctrl-W  Alt-Backspace", "delete word" },
    { "Ctrl-U  Ctrl-K", "delete to start / end" },
    { "Tab", "tree" },
    { "F1", "help" },
    { "Ctrl-C", "quit" },
};

static const entry tree_keys[] = {
    { "j k  \xe2\x86\x93 \xe2\x86\x91", "move" },
    { "h l  \xe2\x86\x90 \xe2\x86\x92", "fold / unfold, \xe2\x86\x90 on a leaf: parent" },
    { "Space Enter", "toggle" },
    { "H L", "fold / unfold subtree" },
    { "g G  Home End", "top / bottom" },
    { "PgUp PgDn", "page" },
    { "Tab", "query" },
    { "? F1", "help" },
    { "q  Ctrl-C", "quit" },
};

static i32 columns(const char* s);
static void border(grid* g, i32 x, i32 y, i32 w, const char* left, const char* right, const char* title);
static void row(grid* g, i32 x, i32 y, i32 w);

void help_render(grid* g, app_focus focus) {
  const entry* keys = focus == FOCUS_TREE ? tree_keys : query_keys;
  usz n = focus == FOCUS_TREE ? ARRAY_LEN(tree_keys) : ARRAY_LEN(query_keys);
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
  border(g, x, y, w, "\xe2\x94\x8c", "\xe2\x94\x90", focus == FOCUS_TREE ? " tree keys " : " query keys ");
  for (usz i = 0; i < n && (i32)i + 1 < h - 1; i++) {
    i32 at = x + 2;

    row(g, x, y + 1 + (i32)i, w);
    grid_put_fit(g, at, y + 1 + (i32)i, x + w - 1, keys[i].keys, strlen(keys[i].keys), STYLE_KEY);
    at += key_w + 3;
    grid_put_fit(g, at, y + 1 + (i32)i, x + w - 1, keys[i].what, strlen(keys[i].what), STYLE_NORMAL);
  }

  if (h == (i32)n + 4) {
    row(g, x, y + h - 3, w);
    row(g, x, y + h - 2, w);
    grid_put_fit(g, x + 2, y + h - 2, x + w - 1, FOOTER, strlen(FOOTER), STYLE_DIM);
  }

  border(g, x, y + h - 1, w, "\xe2\x94\x94", "\xe2\x94\x98", NULL);
}

static i32 columns(const char* s) {
  i32 n = 0;

  for (; *s; s++) n += ((u8)*s & 0xc0) != 0x80;

  return n;
}

static void border(grid* g, i32 x, i32 y, i32 w, const char* left, const char* right, const char* title) {
  grid_put(g, x, y, x + w, left, strlen(left), STYLE_PUNCT);
  for (i32 i = 1; i < w - 1; i++) grid_put(g, x + i, y, x + w, H_LINE, strlen(H_LINE), STYLE_PUNCT);

  grid_put(g, x + w - 1, y, x + w, right, strlen(right), STYLE_PUNCT);
  if (title) grid_put_fit(g, x + 2, y, x + w - 1, title, strlen(title), STYLE_LABEL);
}

static void row(grid* g, i32 x, i32 y, i32 w) {
  grid_put(g, x, y, x + w, V_LINE, strlen(V_LINE), STYLE_PUNCT);
  for (i32 i = 1; i < w - 1; i++) grid_put(g, x + i, y, x + w, " ", 1, STYLE_NORMAL);

  grid_put(g, x + w - 1, y, x + w, V_LINE, strlen(V_LINE), STYLE_PUNCT);
}
