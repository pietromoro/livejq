/* tree - the outputs as foldable lines, state kept by path. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tree.h"

#define EXPAND_LIMIT 1000
#define MARK_OPEN    "\xe2\x96\xbe "
#define MARK_CLOSED  "\xe2\x96\xb8 "
#define GUTTER       "\xe2\x94\x83"

/* how far containers open when the path has no fold of its own. */
typedef enum {
  EXPAND_ALL = 0,
  EXPAND_ROOTS,
  EXPAND_NONE,
} expand_mode;

typedef struct {
  char* s;
  usz len;
  usz cap;
} strbuf;

typedef enum {
  FOLD_NONE = 0,
  FOLD_CLOSED,
  FOLD_OPEN,
} fold;

/* what the user set on a path. */
typedef struct {
  char* key; /* NULL when the slot is free */
  fold fold;
  view_id view;
  time_unit unit;
  b8 viewed; /* view set on this path, overriding the rule */
} node;

typedef struct {
  jv value;
  jv key;     /* member name, element index, or root index */
  i32 parent; /* -1 for a root */
  i32 depth;
  b8 open;
  view_id view;
  time_unit unit;
} row;

struct tree {
  jv roots;
  i32 nroots;
  expand_mode mode;

  row* rows;
  i32 nrows;
  i32 rows_cap;

  node* nodes; /* open addressing, keyed by path */
  usz nnodes;
  usz nodes_cap;

  char* cursor_key; /* where the cursor wants to be */
  i32 cursor;
  i32 top;
  i32 page;

  view_ctx ctx;

  strbuf key;     /* path of the row being built */
  strbuf pattern; /* its rule's key */
  i32 best;       /* deepest row on the way to cursor_key */
  usz best_len;
};

static void rebuild(tree* t);
static void add_rows(tree* t, jv value, jv key, i32 parent, i32 depth);
static void match_cursor(tree* t, i32 r);
static b8 is_open(tree* t, jv value, i32 depth);
static b8 has_children(jv v);
static i32 count_rows(jv v, i32 levels, i32 budget);
static expand_mode pick_mode(tree* t);
static void set_subtree(tree* t, jv value, b8 open);
static void fold_or_leave(tree* t);
static void fold_row(tree* t, b8 open);
static void remember_cursor(tree* t);
static void row_key(tree* t, i32 r);
static void key_segment(strbuf* b, jv key, i32 depth);
static void cycle_view(tree* t, i32 step);
static void cycle_unit(tree* t);
static void set_view(tree* t, view_id id, time_unit unit);
static void spread_view(tree* t);
static node* view_node(tree* t);
static void pattern_of(const char* key, strbuf* b);
static node* node_find(tree* t, const char* key);
static node* node_at(tree* t, const char* key);
static void fold_set(tree* t, const char* key, b8 open);
static u32 hash(const char* s);
static void render_row(tree* t, grid* g, i32 r, i32 x, i32 y, i32 max_x, u8 flag, b8 dim);
static void path_display(tree* t, i32 r, strbuf* b);
static b8 is_ident(const char* s);
static u8 kind_style(jv v);
static void sb_reserve(strbuf* b, usz more);
static void sb_add(strbuf* b, const char* s, usz len);
static void sb_cut(strbuf* b, usz len);

tree* tree_new(void) {
  tree* t = calloc(1, sizeof(*t));

  if (!t) abort();

  t->roots = jv_array();
  t->page = 1;
  return t;
}

void tree_free(tree* t) {
  if (!t) return;

  for (i32 i = 0; i < t->nrows; i++) {
    jv_free(t->rows[i].value);
    jv_free(t->rows[i].key);
  }

  for (usz i = 0; i < t->nodes_cap; i++) free(t->nodes[i].key);

  jv_free(t->roots);
  free(t->rows);
  free(t->nodes);
  free(t->cursor_key);
  free(t->key.s);
  free(t->pattern.s);
  free(t);
}

void tree_set(tree* t, jv outputs) {
  remember_cursor(t);
  jv_free(t->roots);
  t->roots = outputs;
  t->nroots = jv_array_length(jv_copy(outputs));
  t->mode = pick_mode(t);
  rebuild(t);
}

void tree_do(tree* t, tree_op op) {
  i32 last = t->nrows - 1;

  if (t->nrows == 0) return;

  switch (op) {
    case TREE_UP:
      t->cursor--;
      break;
    case TREE_DOWN:
      t->cursor++;
      break;
    case TREE_PAGE_UP:
      t->cursor -= t->page;
      t->top -= t->page;
      break;
    case TREE_PAGE_DOWN:
      t->cursor += t->page;
      t->top += t->page;
      break;
    case TREE_TOP:
      t->cursor = 0;
      break;
    case TREE_BOTTOM:
      t->cursor = last;
      break;
    case TREE_FOLD:
      fold_or_leave(t);
      break;
    case TREE_UNFOLD:
      if (has_children(t->rows[t->cursor].value)) fold_row(t, 1);
      break;
    case TREE_TOGGLE:
      if (has_children(t->rows[t->cursor].value)) fold_row(t, !t->rows[t->cursor].open);
      break;
    case TREE_FOLD_ALL:
    case TREE_UNFOLD_ALL:
      row_key(t, t->cursor);
      set_subtree(t, t->rows[t->cursor].value, op == TREE_UNFOLD_ALL);
      remember_cursor(t);
      rebuild(t);
      break;
    case TREE_VIEW_NEXT:
      cycle_view(t, 1);
      break;
    case TREE_VIEW_PREV:
      cycle_view(t, -1);
      break;
    case TREE_VIEW_RESET:
      cycle_view(t, 0);
      break;
    case TREE_VIEW_SIBLINGS:
      spread_view(t);
      break;
    case TREE_UNIT_NEXT:
      cycle_unit(t);
      break;
  }

  if (t->cursor < 0) t->cursor = 0;
  if (t->cursor > t->nrows - 1) t->cursor = t->nrows - 1;
  if (t->top < 0) t->top = 0;

  remember_cursor(t);
}

void tree_clock(tree* t, i64 wall_ms) { t->ctx.now_ms = wall_ms; }

void tree_view(tree* t, const view_id* ids, i32 n) {
  i32 at = -1;

  if (t->nrows == 0) return;

  for (i32 i = 0; i < n; i++)
    if (ids[i] == t->rows[t->cursor].view) at = i;

  for (i32 i = at + 1; i < n; i++) {
    jv text = view_text(t->rows[t->cursor].value, ids[i], NULL);
    b8 applies = jv_is_valid(text);

    jv_free(text);
    if (applies) {
      set_view(t, ids[i], t->rows[t->cursor].unit);
      return;
    }
  }

  if (at >= 0) set_view(t, VIEW_DEFAULT, t->rows[t->cursor].unit);
}

b8 tree_scroll(tree* t, i32 n) {
  i32 top = t->top + n;

  if (top > t->nrows - t->page) top = t->nrows - t->page;
  if (top < 0) top = 0;
  if (top == t->top) return 0;

  t->top = top;
  if (t->cursor < top) t->cursor = top;
  if (t->cursor >= top + t->page) t->cursor = top + t->page - 1;
  remember_cursor(t);
  return 1;
}

b8 tree_click(tree* t, i32 x, i32 n, b8 again) {
  i32 r = t->top + n;
  i32 mark;

  if (n < 0 || r >= t->nrows) return 0;

  mark = t->rows[r].depth * 2;
  again = again && r == t->cursor;
  t->cursor = r;
  remember_cursor(t);
  if (again || (x >= mark && x < mark + 2)) tree_do(t, TREE_TOGGLE);

  return 1;
}

void tree_render(tree* t, grid* g, i32 x, i32 y, i32 max_x, i32 h, b8 dim, b8 focused) {
  u8 selected = focused ? STYLE_SELECTED : STYLE_SELECTED_IDLE;

  if (h < 1) return;

  t->page = h;
  if (t->top > t->nrows - h) t->top = t->nrows - h;
  if (t->top < 0) t->top = 0;
  if (t->cursor < t->top) t->top = t->cursor;
  if (t->cursor >= t->top + h) t->top = t->cursor - h + 1;

  for (i32 i = 0; i < h && t->top + i < t->nrows; i++) {
    i32 r = t->top + i;
    u8 flag = r == t->cursor ? selected : 0;

    if (flag) {
      grid_fill_span(g, x - 1, max_x, y + i, STYLE_NORMAL | flag);
      grid_put(g, x - 1, y + i, max_x, GUTTER, 3, STYLE_GUTTER | flag);
    }

    render_row(t, g, r, x, y + i, max_x, flag, dim);
  }
}

void tree_path(tree* t, char* buf, usz cap) {
  if (cap == 0) return;

  buf[0] = '\0';
  if (t->nrows == 0) return;

  strbuf b = { 0 };

  path_display(t, t->cursor, &b);
  if (t->rows[t->cursor].parent < 0) sb_add(&b, ".", 1);

  snprintf(buf, cap, "%s", b.s);
  free(b.s);
}

static void rebuild(tree* t) {
  char num[24];

  for (i32 i = 0; i < t->nrows; i++) {
    jv_free(t->rows[i].value);
    jv_free(t->rows[i].key);
  }

  t->nrows = 0;
  t->best = 0;
  t->best_len = 0;
  for (i32 i = 0; i < t->nroots; i++) {
    sb_cut(&t->key, 0);
    snprintf(num, sizeof(num), "#%d", i);
    sb_add(&t->key, num, strlen(num));
    add_rows(t, jv_array_get(jv_copy(t->roots), i), jv_number(i), -1, 0);
  }

  t->cursor = t->best;
}

static void add_rows(tree* t, jv value, jv key, i32 parent, i32 depth) {
  usz mark = t->key.len;
  i32 r = t->nrows;

  if (depth > 0) key_segment(&t->key, key, depth);

  node* shown = view_node(t);

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
  t->rows[r].view = shown->view;
  t->rows[r].unit = shown->unit;
  t->nrows++;
  match_cursor(t, r);

  if (t->rows[r].open && jv_get_kind(value) == JV_KIND_ARRAY) {
    jv_array_foreach(value, i, item) add_rows(t, item, jv_number(i), r, depth + 1);
  } else if (t->rows[r].open) {
    jv_object_foreach(value, k, item) add_rows(t, item, k, r, depth + 1);
  }

  sb_cut(&t->key, mark);
}

static void match_cursor(tree* t, i32 r) {
  const char* want = t->cursor_key;
  usz len = t->key.len;

  if (!want || len < t->best_len || strncmp(want, t->key.s, len) != 0) return;

  if (want[len] != '\0' && want[len] != '.' && want[len] != '[') return;

  t->best = r;
  t->best_len = len + 1;
}

static b8 is_open(tree* t, jv value, i32 depth) {
  if (!has_children(value)) return 0;

  fold f = node_find(t, t->key.s)->fold;

  if (f != FOLD_NONE) return f == FOLD_OPEN;

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

/* folds or unfolds every container under the path in t->key. */
static void set_subtree(tree* t, jv value, b8 open) {
  usz mark = t->key.len;

  if (!has_children(value)) return;

  fold_set(t, t->key.s, open);
  if (jv_get_kind(value) == JV_KIND_ARRAY) {
    jv_array_foreach(value, i, item) {
      key_segment(&t->key, jv_number(i), 1);
      set_subtree(t, item, open);
      jv_free(item);
      sb_cut(&t->key, mark);
    }
  } else {
    jv_object_foreach(value, k, item) {
      key_segment(&t->key, k, 1);
      set_subtree(t, item, open);
      jv_free(k);
      jv_free(item);
      sb_cut(&t->key, mark);
    }
  }
}

static void fold_or_leave(tree* t) {
  if (t->rows[t->cursor].open) fold_row(t, 0);
  else if (t->rows[t->cursor].parent >= 0) t->cursor = t->rows[t->cursor].parent;
}

static void fold_row(tree* t, b8 open) {
  row_key(t, t->cursor);
  fold_set(t, t->key.s, open);
  remember_cursor(t);
  rebuild(t);
}

static void remember_cursor(tree* t) {
  if (t->nrows == 0) return;

  row_key(t, t->cursor);
  free(t->cursor_key);
  t->cursor_key = malloc(t->key.len + 1);
  if (!t->cursor_key) abort();

  memcpy(t->cursor_key, t->key.s, t->key.len + 1);
}

/* the path of row r into t->key, in the encoding folds are keyed by. */
static void row_key(tree* t, i32 r) {
  char num[24];

  if (t->rows[r].parent >= 0) {
    row_key(t, t->rows[r].parent);
    key_segment(&t->key, t->rows[r].key, t->rows[r].depth);
    return;
  }

  sb_cut(&t->key, 0);
  snprintf(num, sizeof(num), "#%d", (i32)jv_number_value(t->rows[r].key));
  sb_add(&t->key, num, strlen(num));
}

/* [n] for an index, ."name" for a member: no segment is a prefix of
 * another, so a path is a prefix of its descendants' paths. */
static void key_segment(strbuf* b, jv key, i32 depth) {
  if (depth == 0) return;

  char num[24];

  if (jv_get_kind(key) == JV_KIND_NUMBER) {
    snprintf(num, sizeof(num), "[%d]", (i32)jv_number_value(key));
    sb_add(b, num, strlen(num));
    return;
  }

  sb_add(b, ".\"", 2);
  for (const char* s = jv_string_value(key); *s; s++) {
    if (*s == '"' || *s == '\\') sb_add(b, "\\", 1);
    sb_add(b, s, 1);
  }

  sb_add(b, "\"", 1);
}

/* the step-th view after the cursor's, or the default one for 0. */
static void cycle_view(tree* t, i32 step) {
  view_id ids[VIEW_COUNT];
  i32 n = views_of(t->rows[t->cursor].value, ids, VIEW_COUNT);
  i32 at = 0;

  for (i32 i = 0; i < n; i++)
    if (ids[i] == t->rows[t->cursor].view) at = i;

  at = step == 0 ? 0 : (at + step + n) % n;
  set_view(t, ids[at], step == 0 ? UNIT_AUTO : t->rows[t->cursor].unit);
}

/* the next of s, ms, µs and ns the cursor's time view applies in, then
 * back to the guessed one. */
static void cycle_unit(tree* t) {
  row* w = &t->rows[t->cursor];
  view_ctx ctx = t->ctx;

  if (!view_takes_unit(w->value, w->view)) return;

  for (i32 u = (i32)w->unit + 1; u < UNIT_COUNT; u++) {
    ctx.unit = (time_unit)u;

    jv text = view_text(w->value, w->view, &ctx);
    b8 applies = jv_is_valid(text);

    jv_free(text);
    if (applies) {
      set_view(t, w->view, ctx.unit);
      return;
    }
  }

  set_view(t, w->view, UNIT_AUTO);
}

static void set_view(tree* t, view_id id, time_unit unit) {
  node* n;

  t->rows[t->cursor].view = id;
  t->rows[t->cursor].unit = unit;
  row_key(t, t->cursor);
  n = node_at(t, t->key.s);
  n->view = id;
  n->unit = unit;
  n->viewed = 1;
}

/* makes the cursor's view the rule for its pattern, dropping the views set
 * on the paths the pattern matches. */
static void spread_view(tree* t) {
  view_id id = t->rows[t->cursor].view;
  time_unit unit = t->rows[t->cursor].unit;
  strbuf other = { 0 };
  node* rule;

  row_key(t, t->cursor);
  pattern_of(t->key.s, &t->pattern);
  for (usz i = 0; i < t->nodes_cap; i++) {
    if (!t->nodes[i].viewed) continue;

    pattern_of(t->nodes[i].key, &other);
    if (strcmp(other.s, t->pattern.s) != 0) continue;

    t->nodes[i].viewed = 0;
    t->nodes[i].view = VIEW_DEFAULT;
    t->nodes[i].unit = UNIT_AUTO;
  }

  free(other.s);
  rule = node_at(t, t->pattern.s);
  rule->view = id;
  rule->unit = unit;
  remember_cursor(t);
  rebuild(t);
}

/* the node whose view the path in t->key shows: its own when one was set
 * on it, else its rule's. */
static node* view_node(tree* t) {
  node* n = node_find(t, t->key.s);

  if (n->viewed) return n;

  pattern_of(t->key.s, &t->pattern);
  return node_find(t, t->pattern.s);
}

/* key without its indices, #3."a"[0] -> #."a"[], so that keys differing
 * only in indices share it. */
static void pattern_of(const char* key, strbuf* b) {
  b8 quoted = 0;

  sb_cut(b, 0);
  for (const char* s = key; *s; s++) {
    if (quoted && *s == '\\') {
      sb_add(b, s, 2);
      s++;
      continue;
    }

    if (*s == '"') quoted = !quoted;
    if (quoted || *s < '0' || *s > '9') sb_add(b, s, 1);
  }
}

static node* node_find(tree* t, const char* key) {
  static node none;
  usz i;

  if (t->nodes_cap == 0) return &none;

  i = hash(key) & (t->nodes_cap - 1);
  while (t->nodes[i].key && strcmp(t->nodes[i].key, key) != 0) i = (i + 1) & (t->nodes_cap - 1);

  return &t->nodes[i];
}

/* the node for key, added when missing. */
static node* node_at(tree* t, const char* key) {
  node* f;

  if ((t->nnodes + 1) * 4 > t->nodes_cap * 3) {
    node* old = t->nodes;
    usz old_cap = t->nodes_cap;

    t->nodes_cap = old_cap ? old_cap * 2 : 64;
    t->nodes = calloc(t->nodes_cap, sizeof(node));
    if (!t->nodes) abort();

    for (usz i = 0; i < old_cap; i++) {
      if (!old[i].key) continue;

      *node_find(t, old[i].key) = old[i];
    }

    free(old);
  }

  f = node_find(t, key);
  if (f->key) return f;

  f->key = malloc(strlen(key) + 1);
  if (!f->key) abort();

  strcpy(f->key, key);
  t->nnodes++;
  return f;
}

static void fold_set(tree* t, const char* key, b8 open) {
  node_at(t, key)->fold = open ? FOLD_OPEN : FOLD_CLOSED;
}

static u32 hash(const char* s) {
  u32 h = 2166136261u;

  while (*s) h = (h ^ (u8)*s++) * 16777619u;

  return h;
}

static void render_row(tree* t, grid* g, i32 r, i32 x, i32 y, i32 max_x, u8 flag, b8 dim) {
  row* w = &t->rows[r];
  jv_kind kind = jv_get_kind(w->value);
  char buf[32];
  u8 punct = (dim ? STYLE_DIM : STYLE_PUNCT) | flag;
  view_ctx ctx = t->ctx;
  char tag[16];
  i32 tag_w;
  jv text;

  x += w->depth * 2;
  if (has_children(w->value)) grid_put(g, x, y, max_x, w->open ? MARK_OPEN : MARK_CLOSED, 4, punct);

  x += 2;
  if (w->parent < 0 && t->nroots > 1) {
    snprintf(buf, sizeof(buf), "#%d ", (i32)jv_number_value(w->key));
    x = grid_put_fit(g, x, y, max_x, buf, strlen(buf), (dim ? STYLE_DIM : STYLE_LABEL) | flag);
  } else if (w->parent >= 0 && jv_get_kind(w->key) == JV_KIND_STRING) {
    const char* k = jv_string_value(w->key);

    x = grid_put_fit(g, x, y, max_x, k, strlen(k), (dim ? STYLE_DIM : STYLE_KEY) | flag);
    x = grid_put_fit(g, x, y, max_x, ": ", 2, punct);
  } else if (w->parent >= 0) {
    snprintf(buf, sizeof(buf), "%d: ", (i32)jv_number_value(w->key));
    x = grid_put_fit(g, x, y, max_x, buf, strlen(buf), punct);
  }

  if (kind == JV_KIND_ARRAY || kind == JV_KIND_OBJECT) {
    i32 n = kind == JV_KIND_ARRAY
      ? jv_array_length(jv_copy(w->value))
      : jv_object_length(jv_copy(w->value));

    if (kind == JV_KIND_ARRAY) snprintf(buf, sizeof(buf), n ? "[%d]" : "[]", n);
    else snprintf(buf, sizeof(buf), n ? "{%d}" : "{}", n);

    grid_put_fit(g, x, y, max_x, buf, strlen(buf), punct);
    return;
  }

  ctx.unit = w->unit;
  text = w->view == VIEW_DEFAULT ? jv_invalid() : view_text(w->value, w->view, &ctx);
  if (w->unit != UNIT_AUTO && view_takes_unit(w->value, w->view)) {
    snprintf(tag, sizeof(tag), "%s %s", view_name(w->view), unit_name(w->unit));
  } else {
    snprintf(tag, sizeof(tag), "%s", view_name(w->view));
  }

  if (!jv_is_valid(text)) {
    jv_free(text);
    text = jv_dump_string(jv_copy(w->value), 0);
    tag[0] = '\0';
  }

  tag_w = tag[0] ? utf8_columns(tag, strlen(tag)) + 2 : 0;
  x = grid_put_fit(
    g,
    x,
    y,
    max_x - tag_w > x ? max_x - tag_w : max_x,
    jv_string_value(text),
    (usz)jv_string_length_bytes(jv_copy(text)),
    (dim ? STYLE_DIM : kind_style(w->value)) | flag
  );
  jv_free(text);
  if (tag_w) grid_put(g, x + 2, y, max_x, tag, strlen(tag), STYLE_EMPTY | flag);
}

/* the path of row r as jq writes it, after "#n " if there are more roots. */
static void path_display(tree* t, i32 r, strbuf* b) {
  row* w = &t->rows[r];
  char num[24];

  if (w->parent < 0) {
    sb_cut(b, 0);
    if (t->nroots < 2) return;

    snprintf(num, sizeof(num), "#%d ", (i32)jv_number_value(w->key));
    sb_add(b, num, strlen(num));
    return;
  }

  path_display(t, w->parent, b);
  if (jv_get_kind(w->key) == JV_KIND_NUMBER && t->rows[w->parent].parent < 0) sb_add(b, ".", 1);

  if (jv_get_kind(w->key) == JV_KIND_NUMBER) {
    snprintf(num, sizeof(num), "[%d]", (i32)jv_number_value(w->key));
    sb_add(b, num, strlen(num));
  } else if (is_ident(jv_string_value(w->key))) {
    sb_add(b, ".", 1);
    sb_add(b, jv_string_value(w->key), strlen(jv_string_value(w->key)));
  } else {
    jv text = jv_dump_string(jv_copy(w->key), 0);

    sb_add(b, ".", 1);
    sb_add(b, jv_string_value(text), strlen(jv_string_value(text)));
    jv_free(text);
  }
}

static b8 is_ident(const char* s) {
  if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_')) return 0;

  for (s++; *s; s++)
    if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_'))
      return 0;

  return 1;
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

static void sb_reserve(strbuf* b, usz more) {
  if (b->len + more + 1 <= b->cap) return;

  b->cap = (b->len + more + 1) * 2;
  b->s = realloc(b->s, b->cap);
  if (!b->s) abort();
}

static void sb_add(strbuf* b, const char* s, usz len) {
  sb_reserve(b, len);
  memcpy(b->s + b->len, s, len);
  b->len += len;
  b->s[b->len] = '\0';
}

static void sb_cut(strbuf* b, usz len) {
  sb_reserve(b, 0);
  b->len = len;
  b->s[len] = '\0';
}
