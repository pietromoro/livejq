/* app - application state driven by events, rendered into a grid. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "eval.h"
#include "loader.h"
#include "tree.h"

#define DEBOUNCE_MS 150
#define DEFAULT_CAP 10000

struct app {
  const char* name;
  usz cap;
  eval* ev;
  tree* tree;

  char* query;
  usz qlen;
  usz qcap;
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

static b8 edit(app* a, u32 key);
static void query_insert(app* a, u32 c);
static void query_backspace(app* a);
static void submit(app* a);
static void apply(app* a);
static void render_status(app* a, grid* g);

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
  a->name = slash ? slash + 1 : opts->path;
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

  while (*q) query_insert(a, (u8)*q++);

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
  free(a->query);
  free(a);
}

void app_feed(app* a, app_event ev) {
  switch (ev.kind) {
    case EV_KEY:
      if (!edit(a, ev.key)) return;

      a->edited = 1;
      a->submit_at = ev.now_ms + DEBOUNCE_MS;
      break;
    case EV_TICK:
      if (a->edited && ev.now_ms >= a->submit_at) submit(a);
      break;
  }

  apply(a);
}

b8 app_settle(app* a, i64 timeout_ms) {
  if (a->edited) submit(a);
  if (a->pending && eval_wait(a->ev, a->pending, timeout_ms)) apply(a);
  return a->pending == 0;
}

void app_render(app* a, grid* g) {
  i32 n = jv_array_length(jv_copy(a->outputs));

  grid_clear(g);
  grid_put(g, grid_puts(g, 0, 0, "QUERY ", STYLE_LABEL), 0, g->w, a->query, a->qlen, STYLE_NORMAL);

  if (a->has_result && n == 0 && jv_get_kind(a->error) == JV_KIND_NULL)
    grid_puts(g, 0, 1, "(no output)", STYLE_EMPTY);

  tree_render(a->tree, g, 1, g->h - 2, a->stale);
  render_status(a, g);
}

static b8 edit(app* a, u32 key) {
  if (key == APP_KEY_BACKSPACE) {
    query_backspace(a);
    return 1;
  }

  if (key < 0x20 || key == 0x7f || key >= 0x110000) return 0;

  query_insert(a, key);
  return 1;
}

static void query_insert(app* a, u32 c) {
  char buf[4];
  usz n;

  if (c < 0x80) {
    buf[0] = (char)c;
    n = 1;
  } else if (c < 0x800) {
    buf[0] = (char)(0xc0 | (c >> 6));
    buf[1] = (char)(0x80 | (c & 0x3f));
    n = 2;
  } else if (c < 0x10000) {
    buf[0] = (char)(0xe0 | (c >> 12));
    buf[1] = (char)(0x80 | ((c >> 6) & 0x3f));
    buf[2] = (char)(0x80 | (c & 0x3f));
    n = 3;
  } else {
    buf[0] = (char)(0xf0 | (c >> 18));
    buf[1] = (char)(0x80 | ((c >> 12) & 0x3f));
    buf[2] = (char)(0x80 | ((c >> 6) & 0x3f));
    buf[3] = (char)(0x80 | (c & 0x3f));
    n = 4;
  }

  if (a->qlen + n + 1 > a->qcap) {
    a->qcap = (a->qlen + n + 1) * 2;
    a->query = realloc(a->query, a->qcap);
    if (!a->query) abort();
  }

  memcpy(a->query + a->qlen, buf, n);
  a->qlen += n;
  a->query[a->qlen] = '\0';
}

static void query_backspace(app* a) {
  while (a->qlen > 0 && ((u8)a->query[--a->qlen] & 0xc0) == 0x80) {}

  if (a->query) a->query[a->qlen] = '\0';
}

static void submit(app* a) {
  a->edited = 0;
  a->pending = eval_submit(a->ev, a->query ? a->query : "", a->cap);
}

static void apply(app* a) {
  eval_result r;

  if (!eval_take(a->ev, &r)) return;

  if (r.id != a->pending) {
    eval_result_free(&r);
    return;
  }

  a->pending = 0;
  a->has_result = 1;
  jv_free(a->error);
  a->error = r.error;
  if (r.compile_failed) {
    a->stale = 1;
    jv_free(r.outputs);
    return;
  }

  a->stale = 0;
  a->capped = r.capped;
  a->elapsed_us = r.elapsed_us;
  jv_free(a->outputs);
  a->outputs = r.outputs;
  tree_set(a->tree, jv_copy(a->outputs));
}

static void render_status(app* a, grid* g) {
  i32 y = g->h - 1;
  jv err = jv_get_kind(a->error) == JV_KIND_STRING ? a->error : a->load_error;
  char left[256];
  i32 x;

  grid_fill(g, y, STYLE_STATUS);
  if (a->has_result) {
    snprintf(
      left,
      sizeof(left),
      "%s \xc2\xb7 %d outputs%s \xc2\xb7 %.1f ms",
      a->name,
      jv_array_length(jv_copy(a->outputs)),
      a->capped ? " (capped)" : "",
      (f64)a->elapsed_us / 1000.0
    );
  } else
    snprintf(left, sizeof(left), "%s \xc2\xb7 running", a->name);

  x = grid_puts(g, 0, y, left, STYLE_STATUS);
  if (jv_get_kind(err) == JV_KIND_STRING) {
    const char* s = jv_string_value(err);
    const char* nl = strchr(s, '\n');
    usz len = nl ? (usz)(nl - s) : strlen(s);
    i32 at = g->w - (i32)len;

    if (at < x + 2) at = x + 2;
    grid_put(g, at, y, g->w, s, len, STYLE_ERROR);
  }
}
