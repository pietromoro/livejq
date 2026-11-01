/* app - application state driven by events, rendered into a grid. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "editor.h"
#include "eval.h"
#include "help.h"
#include "highlight.h"
#include "loader.h"
#include "tree.h"

#define DEBOUNCE_MS 150
#define DEFAULT_CAP 10000
#define WHEEL_ROWS  3
#define QUERY_ROWS  3 /* the query's box */
#define TREE_Y      4
#define TREE_X      2
#define PROMPT      "\xe2\x9d\xaf "
#define PLACEHOLDER "a jq filter"
#define CTRL(c)     ((c) & 0x1f)

struct app {
  const char* path;
  const char* name;
  b8 raw;
  b8 slurp;
  usz cap;
  eval* ev;
  tree* tree;
  app_focus focus;
  b8 help;         /* the overlay is shown */
  b8 quit;
  i32 h;           /* rows of the last render */

  editor query;
  b8 edited;      /* not submitted yet */
  i64 submit_at;  /* debounce deadline */
  u64 pending;    /* submitted id awaiting its result */

  b8 has_result;
  b8 stale;       /* outputs are from an older query */
  b8 capped;
  jv outputs;
  jv error;
  jv load_error;
  i64 elapsed_us;
};

static void key(app* a, app_event ev);
static void tree_key(app* a, u32 key);
static b8 mouse(app* a, app_event ev);
static void reload(app* a);
static b8 edit(app* a, u32 key);
static void submit(app* a);
static b8 apply(app* a);
static b8 failed(const app* a);
static void render_query(app* a, grid* g);
static void render_status(app* a, grid* g);
static i32 segment(grid* g, i32 x, const char* s, u8 st);

app* app_new(const app_opts* opts, char* err, usz errlen) {
  const char* q = opts->query ? opts->query : ".";
  const char* slash;
  load_result loaded;
  file_stamp stamp;
  app* a;

  if (!load_file(opts->path, opts->raw, opts->slurp, &loaded, &stamp, err, errlen)) {
    jv_free(opts->args);
    return NULL;
  }

  a = calloc(1, sizeof(*a));
  if (!a) abort();

  slash = strrchr(opts->path, '/');
#ifdef _WIN32
  if (strrchr(opts->path, '\\') > slash) slash = strrchr(opts->path, '\\');
#endif
  a->path = opts->path;
  a->name = slash ? slash + 1 : opts->path;
  a->raw = opts->raw;
  a->slurp = opts->slurp;
  a->cap = opts->cap ? opts->cap : DEFAULT_CAP;
  a->outputs = jv_array();
  a->error = jv_null();
  a->load_error = loaded.error;
  a->ev = eval_new();
  if (!a->ev) abort();

  a->tree = tree_new();

  eval_set_inputs(a->ev, loaded.inputs);
  if (jv_get_kind(opts->args) == JV_KIND_OBJECT)
    eval_set_args(a->ev, opts->args);
  else jv_free(opts->args);

  editor_set(&a->query, q);

  submit(a);
  return a;
}

void app_free(app* a) {
  if (!a) return;

  eval_free(a->ev);
  tree_free(a->tree);
  jv_free(a->outputs);
  jv_free(a->error);
  jv_free(a->load_error);
  editor_free(&a->query);
  free(a);
}

b8 app_feed(app* a, app_event ev) {
  b8 changed = ev.kind != EV_TICK;

  tree_clock(a->tree, ev.wall_ms);
  switch (ev.kind) {
    case EV_KEY:
      key(a, ev);
      break;
    case EV_TICK:
      if (a->edited && ev.now_ms >= a->submit_at) submit(a);
      break;
    case EV_FILE_CHANGED:
      reload(a);
      break;
    case EV_MOUSE:
      changed = mouse(a, ev);
      break;
  }

  return apply(a) || changed;
}

b8 app_settle(app* a, i64 timeout_ms) {
  if (a->edited) submit(a);
  if (a->pending && eval_wait(a->ev, a->pending, timeout_ms)) apply(a);
  return a->pending == 0;
}

/* the query in a box, the tree under it, a status bar and a line of keys. */
void app_render(app* a, grid* g) {
  i32 n = jv_array_length(jv_copy(a->outputs));

  a->h = g->h;
  grid_clear(g);
  render_query(a, g);
  if (a->has_result && n == 0 && jv_get_kind(a->error) == JV_KIND_NULL)
    grid_put_fit(g, TREE_X, TREE_Y, g->w - 1, "(no output)", 11, STYLE_EMPTY);

  tree_render(a->tree, g, TREE_X, TREE_Y, g->w - 1, g->h - TREE_Y - 2, a->stale, a->focus == FOCUS_TREE);
  render_status(a, g);
  help_bar(g, g->h - 1, a->focus);
  if (!a->help) return;

  help_render(g, a->focus);
  g->cursor_x = -1;
}

app_focus app_focused(const app* a) { return a->focus; }

b8 app_quitting(const app* a) { return a->quit; }

static void key(app* a, app_event ev) {
  if (ev.key == CTRL('c')) {
    a->quit = 1;
    return;
  }

  if (a->help) {
    a->help = 0;
    return;
  }

  if (ev.key == APP_KEY_F1 || (ev.key == '?' && a->focus == FOCUS_TREE)) {
    a->help = 1;
    return;
  }

  if (ev.key == '\t' || ev.key == APP_KEY_BACKTAB) {
    a->focus = a->focus == FOCUS_QUERY ? FOCUS_TREE : FOCUS_QUERY;
    return;
  }

  if (ev.key == 'q' && a->focus == FOCUS_TREE) {
    a->quit = 1;
    return;
  }

  if (a->focus == FOCUS_TREE) {
    tree_key(a, ev.key);
    return;
  }

  if (!edit(a, ev.key)) return;

  a->edited = 1;
  a->submit_at = ev.now_ms + DEBOUNCE_MS;
}

static void tree_key(app* a, u32 key) {
  switch (key) {
    case 'k':
    case APP_KEY_UP:
      tree_do(a->tree, TREE_UP);
      break;
    case 'j':
    case APP_KEY_DOWN:
      tree_do(a->tree, TREE_DOWN);
      break;
    case 'h':
    case APP_KEY_LEFT:
      tree_do(a->tree, TREE_FOLD);
      break;
    case 'l':
    case APP_KEY_RIGHT:
      tree_do(a->tree, TREE_UNFOLD);
      break;
    case ' ':
    case APP_KEY_ENTER:
      tree_do(a->tree, TREE_TOGGLE);
      break;
    case 'H':
      tree_do(a->tree, TREE_FOLD_ALL);
      break;
    case 'L':
      tree_do(a->tree, TREE_UNFOLD_ALL);
      break;
    case 'g':
    case APP_KEY_HOME:
      tree_do(a->tree, TREE_TOP);
      break;
    case 'G':
    case APP_KEY_END:
      tree_do(a->tree, TREE_BOTTOM);
      break;
    case APP_KEY_PAGE_UP:
      tree_do(a->tree, TREE_PAGE_UP);
      break;
    case APP_KEY_PAGE_DOWN:
      tree_do(a->tree, TREE_PAGE_DOWN);
      break;
    case 'v':
      tree_do(a->tree, TREE_VIEW_NEXT);
      break;
    case 'V':
      tree_do(a->tree, TREE_VIEW_PREV);
      break;
    case '0':
      tree_do(a->tree, TREE_VIEW_RESET);
      break;
    case 'a':
      tree_do(a->tree, TREE_VIEW_SIBLINGS);
      break;
    case 'x':
      tree_view(a->tree, (view_id[]){ VIEW_HEX }, 1);
      break;
    case 'o':
      tree_view(a->tree, (view_id[]){ VIEW_OCTAL }, 1);
      break;
    case 'b':
      tree_view(a->tree, (view_id[]){ VIEW_BINARY }, 1);
      break;
    case 'e':
      tree_view(a->tree, (view_id[]){ VIEW_EXPONENT }, 1);
      break;
    case 't':
      tree_view(a->tree, (view_id[]){ VIEW_UTC, VIEW_LOCAL, VIEW_DATE, VIEW_AGO }, 4);
      break;
    case 'd':
      tree_view(a->tree, (view_id[]){ VIEW_BASE64, VIEW_URL, VIEW_UNHEX, VIEW_UUID, VIEW_JSON, VIEW_JWT, VIEW_QUERY }, 7);
      break;
    case 'u':
      tree_do(a->tree, TREE_UNIT_NEXT);
      break;
    default: break;
  }
}

/* a press focuses the zone under it and picks the tree line there, or
 * closes the help; the wheel scrolls the tree under it without focusing
 * it. true when the screen changed. */
static b8 mouse(app* a, app_event ev) {
  app_focus was = a->focus;
  b8 in_tree = ev.y >= TREE_Y && ev.y < a->h - 2;

  if (ev.mouse != APP_MOUSE_PRESS) {
    if (a->help || !in_tree) return 0;

    return tree_scroll(a->tree, ev.mouse == APP_MOUSE_WHEEL_UP ? -WHEEL_ROWS : WHEEL_ROWS);
  }

  if (a->help) {
    a->help = 0;
    return 1;
  }

  if (ev.y < QUERY_ROWS) a->focus = FOCUS_QUERY;
  if (!in_tree) return a->focus != was;

  a->focus = FOCUS_TREE;
  return tree_click(a->tree, ev.x - TREE_X, ev.y - TREE_Y, was == FOCUS_TREE) || a->focus != was;
}

/* a file that cannot be read keeps the last data, with the reason shown. */
static void reload(app* a) {
  load_result loaded;
  file_stamp stamp;
  char err[256];

  jv_free(a->load_error);
  if (!load_file(a->path, a->raw, a->slurp, &loaded, &stamp, err, sizeof(err))) {
    a->load_error = jv_string(err);
    return;
  }

  a->load_error = loaded.error;
  eval_set_inputs(a->ev, loaded.inputs);
  submit(a);
}

/* true when the text changed. */
static b8 edit(app* a, u32 key) {
  switch (key) {
    case APP_KEY_BACKSPACE: return editor_backspace(&a->query);
    case APP_KEY_DELETE: return editor_delete(&a->query);
    case APP_KEY_WORD_BACKSPACE:
    case CTRL('w'): return editor_delete_word(&a->query);
    case CTRL('u'): return editor_delete_to_start(&a->query);
    case CTRL('k'): return editor_delete_to_end(&a->query);
    case APP_KEY_WORD_LEFT:
      editor_word_left(&a->query);
      return 0;
    case APP_KEY_WORD_RIGHT:
      editor_word_right(&a->query);
      return 0;
    case APP_KEY_LEFT:
      editor_left(&a->query);
      return 0;
    case APP_KEY_RIGHT:
      editor_right(&a->query);
      return 0;
    case CTRL('a'):
    case APP_KEY_HOME:
      editor_home(&a->query);
      return 0;
    case CTRL('e'):
    case APP_KEY_END:
      editor_end(&a->query);
      return 0;
    default: break;
  }

  if (key < 0x20 || key == 0x7f || key >= 0x110000) return 0;

  editor_insert(&a->query, key);
  return 1;
}

static void submit(app* a) {
  a->edited = 0;
  a->pending = eval_submit(a->ev, a->query.text ? a->query.text : "", a->cap);
}

static b8 apply(app* a) {
  eval_result r;

  if (!eval_take(a->ev, &r)) return 0;

  if (r.id != a->pending) {
    eval_result_free(&r);
    return 0;
  }

  a->pending = 0;
  a->has_result = 1;
  jv_free(a->error);
  a->error = r.error;
  if (r.compile_failed) {
    a->stale = 1;
    jv_free(r.outputs);
    return 1;
  }

  a->stale = 0;
  a->capped = r.capped;
  a->elapsed_us = r.elapsed_us;
  jv_free(a->outputs);
  a->outputs = r.outputs;
  tree_set(a->tree, jv_copy(a->outputs));
  return 1;
}

static b8 failed(const app* a) {
  return jv_get_kind(a->error) == JV_KIND_STRING || jv_get_kind(a->load_error) == JV_KIND_STRING;
}

/* the box takes the accent while the query has focus, and turns red when
 * the query does not compile. a runtime error comes from the data, not from
 * the query. */
static void render_query(app* a, grid* g) {
  u8 border = a->stale ? STYLE_BORDER_ERROR : a->focus == FOCUS_QUERY ? STYLE_BORDER_FOCUS : STYLE_BORDER;
  u8* styles = malloc(a->query.len + 1);
  i32 x;

  if (!styles) abort();

  grid_box(g, 0, 0, g->w, QUERY_ROWS, border);
  x = grid_put(g, 2, 1, g->w - 2, PROMPT, strlen(PROMPT), a->focus == FOCUS_QUERY ? STYLE_ACCENT : STYLE_BORDER);
  highlight(a->query.text, a->query.len, styles);
  if (a->query.len == 0) grid_put_fit(g, x, 1, g->w - 2, PLACEHOLDER, strlen(PLACEHOLDER), STYLE_EMPTY);

  x = editor_render(&a->query, g, x, 1, g->w - 2, styles);
  free(styles);
  if (a->focus != FOCUS_QUERY) return;

  g->cursor_x = x;
  g->cursor_y = 1;
}

/* a pill, the file, then the cursor's path or the error, and the outputs
 * with their time on the right. */
static void render_status(app* a, grid* g) {
  i32 y = g->h - 2;
  jv err = jv_get_kind(a->error) == JV_KIND_STRING ? a->error : a->load_error;
  char outputs[64];
  char time[32];
  char path[512];
  i32 name_w;
  i32 right;
  i32 x;

  if (y < 0) return;

  grid_fill(g, y, STYLE_STATUS);
  if (a->has_result) {
    snprintf(
      outputs,
      sizeof(outputs),
      " %d outputs%s ",
      jv_array_length(jv_copy(a->outputs)),
      a->capped ? " (capped)" : ""
    );
    snprintf(time, sizeof(time), " %.1f ms ", (f64)a->elapsed_us / 1000.0);
  } else {
    snprintf(outputs, sizeof(outputs), " running ");
    time[0] = '\0';
  }

  x = segment(g, 0, failed(a) ? " ERROR " : " LIVEJQ ", failed(a) ? STYLE_PILL_ERROR : STYLE_PILL);
  right = g->w - utf8_columns(outputs, strlen(outputs)) - utf8_columns(time, strlen(time));
  if (right < x) right = x;

  name_w = utf8_columns(a->name, strlen(a->name));
  if (name_w > (right - x) / 2 - 2) name_w = (right - x) / 2 - 2;
  if (name_w > 0) x = grid_put_fit(g, x + 1, y, x + 1 + name_w, a->name, strlen(a->name), STYLE_STATUS) + 1;

  if (jv_get_kind(err) == JV_KIND_STRING) {
    const char* s = jv_string_value(err);
    const char* nl = strchr(s, '\n');

    grid_put_fit(g, x + 1, y, right - 1, s, nl ? (usz)(nl - s) : strlen(s), STYLE_STATUS_ERROR);
  } else {
    tree_path(a->tree, path, sizeof(path));
    grid_put_fit(g, x + 1, y, right - 1, path, strlen(path), STYLE_STATUS_FAINT);
  }

  segment(g, segment(g, right, outputs, STYLE_STATUS_INFO), time, STYLE_STATUS_TIME);
}

static i32 segment(grid* g, i32 x, const char* s, u8 st) {
  return grid_put(g, x, g->h - 2, g->w, s, strlen(s), st);
}
