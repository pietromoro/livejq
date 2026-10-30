/* tree - the outputs as lines, containers opened by size. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tree.h"

#define EXPAND_LIMIT 1000
#define MARK_OPEN    "\xe2\x96\xbe "
#define MARK_CLOSED  "\xe2\x96\xb8 "

/* how far containers open, picked from the size of the outputs. */
typedef enum {
  EXPAND_ALL = 0,
  EXPAND_ROOTS,
  EXPAND_NONE,
} expand_mode;

typedef struct {
  jv value;
  jv key;     /* member name, element index, or root index */
  i32 parent; /* -1 for a root */
  i32 depth;
  b8 open;
} row;

struct tree {
  jv roots;
  i32 nroots;
  expand_mode mode;

  row* rows;
  i32 nrows;
  i32 rows_cap;
};

static void rebuild(tree* t);
static void add_rows(tree* t, jv value, jv key, i32 parent, i32 depth);
static b8 is_open(tree* t, jv value, i32 depth);
static b8 has_children(jv v);
static i32 count_rows(jv v, i32 levels, i32 budget);
static expand_mode pick_mode(tree* t);
static void render_row(tree* t, grid* g, i32 r, i32 y, b8 dim);
static u8 kind_style(jv v);

tree* tree_new(void) {
  tree* t = calloc(1, sizeof(*t));

  if (!t) abort();

  t->roots = jv_array();
  return t;
}

void tree_free(tree* t) {
  if (!t) return;

  for (i32 i = 0; i < t->nrows; i++) {
    jv_free(t->rows[i].value);
    jv_free(t->rows[i].key);
  }

  jv_free(t->roots);
  free(t->rows);
  free(t);
}

void tree_set(tree* t, jv outputs) {
  jv_free(t->roots);
  t->roots = outputs;
  t->nroots = jv_array_length(jv_copy(outputs));
  t->mode = pick_mode(t);
  rebuild(t);
}

void tree_render(tree* t, grid* g, i32 y, i32 h, b8 dim) {
  for (i32 i = 0; i < h && i < t->nrows; i++) render_row(t, g, i, y + i, dim);
}

static void rebuild(tree* t) {
  for (i32 i = 0; i < t->nrows; i++) {
    jv_free(t->rows[i].value);
    jv_free(t->rows[i].key);
  }

  t->nrows = 0;
  for (i32 i = 0; i < t->nroots; i++)
    add_rows(t, jv_array_get(jv_copy(t->roots), i), jv_number(i), -1, 0);
}

static void add_rows(tree* t, jv value, jv key, i32 parent, i32 depth) {
  i32 r = t->nrows;

  if (t->nrows == t->rows_cap) {
    t->rows_cap = t->rows_cap ? t->rows_cap * 2 : 64;
    t->rows = realloc(t->rows, sizeof(row) * (usz)t->rows_cap);
    if (!t->rows) abort();
  }

  t->rows[r].value = value;
  t->rows[r].key = key;
  t->rows[r].parent = parent;
  t->rows[r].depth = depth;
  t->rows[r].open = is_open(t, value, depth);
  t->nrows++;

  if (t->rows[r].open && jv_get_kind(value) == JV_KIND_ARRAY) {
    jv_array_foreach(value, i, item) add_rows(t, item, jv_number(i), r, depth + 1);
  } else if (t->rows[r].open) {
    jv_object_foreach(value, k, item) add_rows(t, item, k, r, depth + 1);
  }
}

static b8 is_open(tree* t, jv value, i32 depth) {
  if (!has_children(value)) return 0;

  return t->mode == EXPAND_ALL || (t->mode == EXPAND_ROOTS && depth == 0);
}

static b8 has_children(jv v) {
  switch (jv_get_kind(v)) {
    case JV_KIND_ARRAY: return jv_array_length(jv_copy(v)) > 0;
    case JV_KIND_OBJECT: return jv_object_length(jv_copy(v)) > 0;
    default: return 0;
  }
}

static i32 count_rows(jv v, i32 levels, i32 budget) {
  i32 n = 1;

  if (levels == 0 || !has_children(v)) return n;

  if (jv_get_kind(v) == JV_KIND_ARRAY) {
    jv_array_foreach(v, i, item) {
      n += count_rows(item, levels - 1, budget - n);
      jv_free(item);
      if (n >= budget) break;
    }
  } else {
    jv_object_foreach(v, k, item) {
      n += count_rows(item, levels - 1, budget - n);
      jv_free(k);
      jv_free(item);
      if (n >= budget) break;
    }
  }

  return n;
}

static expand_mode pick_mode(tree* t) {
  i32 all = 0;
  i32 roots = 0;

  for (i32 i = 0; i < t->nroots && all < EXPAND_LIMIT; i++) {
    jv v = jv_array_get(jv_copy(t->roots), i);

    all += count_rows(v, -1, EXPAND_LIMIT - all);
    jv_free(v);
  }

  if (all < EXPAND_LIMIT) return EXPAND_ALL;

  for (i32 i = 0; i < t->nroots && roots < EXPAND_LIMIT; i++) {
    jv v = jv_array_get(jv_copy(t->roots), i);

    roots += count_rows(v, 1, EXPAND_LIMIT - roots);
    jv_free(v);
  }

  return roots < EXPAND_LIMIT ? EXPAND_ROOTS : EXPAND_NONE;
}

static void render_row(tree* t, grid* g, i32 r, i32 y, b8 dim) {
  row* w = &t->rows[r];
  jv_kind kind = jv_get_kind(w->value);
  i32 x = w->depth * 2;
  char buf[32];
  u8 punct = (dim ? STYLE_DIM : STYLE_PUNCT);
  jv text;

  if (has_children(w->value)) grid_puts(g, x, y, w->open ? MARK_OPEN : MARK_CLOSED, punct);

  x += 2;
  if (w->parent < 0 && t->nroots > 1) {
    snprintf(buf, sizeof(buf), "#%d ", (i32)jv_number_value(w->key));
    x = grid_put_fit(g, x, y, g->w, buf, strlen(buf), (dim ? STYLE_DIM : STYLE_LABEL));
  } else if (w->parent >= 0 && jv_get_kind(w->key) == JV_KIND_STRING) {
    const char* k = jv_string_value(w->key);

    x = grid_put_fit(g, x, y, g->w, k, strlen(k), (dim ? STYLE_DIM : STYLE_KEY));
    x = grid_put_fit(g, x, y, g->w, ": ", 2, punct);
  } else if (w->parent >= 0) {
    snprintf(buf, sizeof(buf), "%d: ", (i32)jv_number_value(w->key));
    x = grid_put_fit(g, x, y, g->w, buf, strlen(buf), punct);
  }

  if (kind == JV_KIND_ARRAY || kind == JV_KIND_OBJECT) {
    i32 n = kind == JV_KIND_ARRAY
      ? jv_array_length(jv_copy(w->value))
      : jv_object_length(jv_copy(w->value));

    if (kind == JV_KIND_ARRAY) snprintf(buf, sizeof(buf), n ? "[%d]" : "[]", n);
    else snprintf(buf, sizeof(buf), n ? "{%d}" : "{}", n);

    grid_put_fit(g, x, y, g->w, buf, strlen(buf), punct);
    return;
  }

  text = jv_dump_string(jv_copy(w->value), 0);
  grid_put_fit(
    g,
    x,
    y,
    g->w,
    jv_string_value(text),
    (usz)jv_string_length_bytes(jv_copy(text)),
    (dim ? STYLE_DIM : kind_style(w->value))
  );
  jv_free(text);
}

static u8 kind_style(jv v) {
  switch (jv_get_kind(v)) {
    case JV_KIND_NULL: return STYLE_NULL;
    case JV_KIND_FALSE: return STYLE_FALSE;
    case JV_KIND_TRUE: return STYLE_TRUE;
    case JV_KIND_NUMBER: return STYLE_NUMBER;
    case JV_KIND_STRING: return STYLE_STRING;
    default: return STYLE_PUNCT;
  }
}

