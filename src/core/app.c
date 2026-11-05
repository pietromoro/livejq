/* app - application state driven by events, rendered into a grid. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "clip.h"
#include "editor.h"
#include "eval.h"
#include "help.h"
#include "highlight.h"
#include "loader.h"
#include "sidebar.h"
#include "store.h"
#include "tree.h"
#include "views.h"

#define DEBOUNCE_MS 150
#define DEFAULT_CAP 10000
#define WHEEL_ROWS  3
#define SCAN_MS     1000 /* between looks at the query folder when not watching */
#define QUERY_ROWS  3 /* the query's box */
#define TREE_Y      4
#define TREE_X      2
#define PROMPT      "\xe2\x9d\xaf "
#define PLACEHOLDER "a jq filter"
#define WATCHING    "\xe2\x9f\xb3 "
#define MODIFIED    "\xe2\x97\x8f"
#define MIN_PATH    16 /* columns the path keeps before the times go, an error keeps all */
#define MAX_PATTERN 20 /* columns the search pattern takes on the status bar */
#define CTRL(c)     ((c) & 0x1f)
#define ESC         0x1b

struct app {
  const char* path;
  const char* name;
  b8 raw;
  b8 slurp;
  usz cap;
  i64 watch_ms;
  i64 poll_at;     /* next look at the file, 0 before the first tick */
  file_stamp stamp;
  b8 missing;      /* the watched file is gone, the last data stays */
  i64 wall_ms;     /* of the last event */
  i64 loaded_ms;   /* when the data was last read, 0 before the first event */
  eval* ev;
  tree* tree;
  app_focus focus;
  b8 help;         /* the overlay is shown */
  help_view help_at;
  b8 quit;
  char* clip;      /* for app_take_clipboard */
  char note[64];   /* on the status bar until the next key */
  i32 w;           /* columns of the last render */
  i32 h;           /* and rows */

  sidebar side;
  i64 scan_at;       /* next look at the query folder */
  char* loaded;      /* the saved query in the box, NULL for none */
  char* loaded_text; /* its text as last read */
  file_stamp loaded_stamp;
  app_focus named_from; /* the focus to return to from the name row */
  b8 side_opened;       /* the name row showed the sidebar */

  b8 searching;   /* the search prompt is open */
  editor search;

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
  jv store_error;
  i64 elapsed_us;
};

static void key(app* a, app_event ev);
static app_focus next_focus(const app* a, i32 step);
static void tree_key(app* a, u32 key);
static void start_search(app* a);
static void copy_value(app* a, b8 raw);
static void copy_path(app* a);
static void pipe_path(app* a);
static void copy(app* a, const char* s, usz len);
static void search_key(app* a, u32 key);
static void sidebar_enter(app* a);
static void save(app* a);
static void start_naming(app* a, const char* name);
static void new_query(app* a);
static b8 write_as(app* a, const char* name);
static void name_key(app* a, u32 key);
static void save_as(app* a);
static void stop_naming(app* a);
static void delete_selected(app* a);
static b8 rescan(app* a, i64 now);
static b8 modified(const app* a);
static void set_loaded(app* a, const char* name, char* text);
static void store_failed(app* a, const char* err);
static b8 mouse(app* a, app_event ev);
static b8 poll(app* a, i64 now);
static void reload(app* a);
static b8 edit(editor* e, u32 key);
static void submit(app* a);
static b8 apply(app* a);
static b8 failed(const app* a);
static void render_query(app* a, grid* g);
static void render_search(app* a, grid* g);
static void render_status(app* a, grid* g);
static i32 search_width(const char* pattern, const char* found);
static i32 found_text(app* a, char* buf, usz cap);
static void times(const app* a, char* buf, usz cap, i32 room);
static b8 add_part(char* buf, usz cap, usz* n, const char* part, i32 room);
static i32 segment(grid* g, i32 x, const char* s, u8 st);

app* app_new(const app_opts* opts, char* err, usz errlen) {
  const char* q = opts->query ? opts->query : ".";
  load_result loaded;
  file_stamp stamp;
  app* a;

  store* saved = NULL;
  if (opts->query_folder && !(saved = store_open(opts->query_folder, err, errlen))) {
    jv_free(opts->args);
    return NULL;
  }

  if (!load_file(opts->path, opts->raw, opts->slurp, &loaded, &stamp, err, errlen)) {
    store_free(saved);
    jv_free(opts->args);
    return NULL;
  }

  a = calloc(1, sizeof(*a));
  if (!a) abort();

  a->side.store = saved;
  a->side.shown = saved != NULL;

  const char* slash = strrchr(opts->path, '/');
#ifdef _WIN32
  if (strrchr(opts->path, '\\') > slash) slash = strrchr(opts->path, '\\');
#endif
  a->path = opts->path;
  a->name = slash ? slash + 1 : opts->path;
  a->raw = opts->raw;
  a->slurp = opts->slurp;
  a->cap = opts->cap ? opts->cap : DEFAULT_CAP;
  a->watch_ms = opts->watch_ms;
  a->stamp = stamp;
  a->outputs = jv_array();
  a->error = jv_null();
  a->load_error = loaded.error;
  a->store_error = jv_null();
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
  jv_free(a->store_error);
  editor_free(&a->query);
  editor_free(&a->search);
  editor_free(&a->side.name);
  store_free(a->side.store);
  free(a->loaded);
  free(a->loaded_text);
  free(a->clip);
  free(a);
}

b8 app_feed(app* a, app_event ev) {
  b8 changed = ev.kind != EV_TICK;

  tree_clock(a->tree, ev.wall_ms);
  a->wall_ms = ev.wall_ms;
  if (!a->loaded_ms) a->loaded_ms = ev.wall_ms;

  switch (ev.kind) {
    case EV_KEY:
      key(a, ev);
      break;
    case EV_TICK:
      if (a->edited && ev.now_ms >= a->submit_at) submit(a);
      changed = a->watch_ms && poll(a, ev.now_ms);
      changed = rescan(a, ev.now_ms) || changed;
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
  i32 left = sidebar_width(&a->side, g->w) + TREE_X;

  a->w = g->w;
  a->h = g->h;
  grid_clear(g);
  render_query(a, g);
  sidebar_render(
    &a->side,
    g,
    TREE_Y - 1,
    g->h - TREE_Y - 1,
    a->loaded,
    a->focus == FOCUS_SIDEBAR
  );
  if (a->has_result && n == 0 && jv_get_kind(a->error) == JV_KIND_NULL)
    grid_put_fit(g, left, TREE_Y, g->w - 1, "(no output)", 11, STYLE_EMPTY);

  tree_render(
    a->tree, g,
    left,
    TREE_Y,
    g->w - 1,
    g->h - TREE_Y - 2,
    a->stale || a->missing,
    a->focus == FOCUS_TREE
  );

  render_status(a, g);
  if (a->searching) render_search(a, g);
  else if (a->side.naming) help_prompt_bar(g, g->h - 1, HELP_PROMPT_NAME);
  else if (a->side.deleting) help_prompt_bar(g, g->h - 1, HELP_PROMPT_DELETE);
  else help_bar(g, g->h - 1, a->focus, next_focus(a, 1));

  if (!a->help) return;

  help_render(g, a->focus, next_focus(a, 1), &a->help_at);
  g->cursor_x = -1;
}

app_focus app_focused(const app* a) { return a->focus; }

char* app_take_clipboard(app* a) {
  char* clip = a->clip;

  a->clip = NULL;
  return clip;
}

b8 app_quitting(const app* a) { return a->quit; }

static void key(app* a, app_event ev) {
  a->note[0] = '\0';
  if (ev.key == CTRL('c')) {
    a->quit = 1;
    return;
  }

  if (a->help) {
    a->help = help_key(&a->help_at, ev.key);
    return;
  }

  if (a->searching) {
    search_key(a, ev.key);
    return;
  }

  if (a->side.naming) {
    name_key(a, ev.key);
    return;
  }

  if (a->side.deleting) {
    a->side.deleting = 0;
    if (ev.key == 'y') delete_selected(a);

    return;
  }

  if (ev.key == CTRL('b') && a->side.store) {
    a->side.shown = !a->side.shown;
    if (!a->side.shown && a->focus == FOCUS_SIDEBAR) a->focus = FOCUS_TREE;

    return;
  }

  if (ev.key == CTRL('s') && a->side.store) {
    save(a);
    return;
  }

  if (ev.key == APP_KEY_ALT_S && a->side.store) {
    start_naming(a, a->loaded ? a->loaded : "");
    return;
  }

  if (ev.key == CTRL('n') && a->side.store) {
    new_query(a);
    return;
  }

  if (ev.key == APP_KEY_F1 || (ev.key == '?' && a->focus != FOCUS_QUERY)) {
    a->help = 1;
    a->help_at.top = 0;
    return;
  }

  if (ev.key == '\t' || ev.key == APP_KEY_BACKTAB) {
    a->focus = next_focus(a, ev.key == '\t' ? 1 : -1);
    return;
  }

  if (ev.key == 'q' && a->focus != FOCUS_QUERY) {
    a->quit = 1;
    return;
  }

  if (a->focus == FOCUS_SIDEBAR) {
    if (ev.key == APP_KEY_ENTER) sidebar_enter(a);
    else if (ev.key == 'd') a->side.deleting = sidebar_selected(&a->side) != NULL;
    else sidebar_key(&a->side, ev.key);

    return;
  }

  if (a->focus == FOCUS_TREE) {
    tree_key(a, ev.key);
    return;
  }

  if (!edit(&a->query, ev.key)) return;

  a->edited = 1;
  a->submit_at = ev.now_ms + DEBOUNCE_MS;
}

/* the zones in order query, tree, sidebar, skipping a hidden sidebar. */
static app_focus next_focus(const app* a, i32 step) {
  i32 n = a->side.shown ? 3 : 2;

  return (app_focus)(((i32)a->focus + step + n) % n);
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
    case 'D':
      tree_view(a->tree, (view_id[]){ VIEW_DURATION }, 1);
      break;
    case 's':
      tree_view(a->tree, (view_id[]){ VIEW_BYTES, VIEW_BYTES_SI }, 2);
      break;
    case 't':
      tree_view(a->tree, (view_id[]){ VIEW_UTC, VIEW_LOCAL, VIEW_DATE, VIEW_AGO }, 4);
      break;
    case 'd':
      tree_view(
        a->tree,
        (view_id[]){ VIEW_BASE64, VIEW_URL, VIEW_UNHEX, VIEW_UUID, VIEW_JSON, VIEW_JWT, VIEW_QUERY },
        7
      );
      break;
    case 'u':
      tree_do(a->tree, TREE_UNIT_NEXT);
      break;
    case '/':
      start_search(a);
      break;
    case 'n':
      tree_find(a->tree, 1);
      break;
    case 'N':
      tree_find(a->tree, -1);
      break;
    case ESC:
      tree_search(a->tree, "");
      break;
    case 'y':
    case 'r':
      copy_value(a, key == 'r');
      break;
    case 'Y':
      copy_path(a);
      break;
    case 'p':
      pipe_path(a);
      break;
    default: break;
  }
}

/* opens the prompt on the key bar; the tree follows what is typed. */
static void start_search(app* a) {
  a->searching = 1;
  editor_set(&a->search, "");
  tree_search_begin(a->tree);
}

/* enter keeps the match, esc goes back to where the search began. */
static void search_key(app* a, u32 key) {
  if (key == ESC || key == APP_KEY_ENTER) {
    tree_search_end(a->tree, key == ESC);
    a->searching = 0;
    return;
  }

  if (edit(&a->search, key)) tree_search(a->tree, a->search.text ? a->search.text : "");
}

/* puts the cursor's value on the clipboard as jq prints it, or with raw a
 * string's text as it is, like jq -r. */
static void copy_value(app* a, b8 raw) {
  jv v = tree_value(a->tree);

  if (!jv_is_valid(v)) {
    jv_free(v);
    return;
  }

  b8 text_only = raw && jv_get_kind(v) == JV_KIND_STRING;
  jv text = text_only ? v : jv_dump_string(v, JV_PRINT_INDENT_FLAGS(2));
  copy(a, jv_string_value(text), (usz)jv_string_length_bytes(jv_copy(text)));
  jv_free(text);
}

static void copy_path(app* a) {
  char* path = tree_jq_path(a->tree);

  if (!path) return;

  copy(a, path, strlen(path));
  free(path);
}

/* pipes the query into the cursor's path, or replaces a bare "." with it,
 * and runs it at once. */
static void pipe_path(app* a) {
  char* path = tree_jq_path(a->tree);

  if (!path || strcmp(path, ".") == 0) {
    free(path);
    return;
  }

  const char* q = a->query.text ? a->query.text : "";
  usz start = strspn(q, " \t");
  usz end = strlen(q);
  while (end > start && (q[end - 1] == ' ' || q[end - 1] == '\t')) end--;

  b8 bare = end == start || (end - start == 1 && q[start] == '.');
  usz cap = end + strlen(path) + 4;
  char* text = malloc(cap);
  if (!text) abort();

  if (bare) snprintf(text, cap, "%s", path);
  else snprintf(text, cap, "%.*s | %s", (i32)end, q, path);

  editor_set(&a->query, text);
  free(text);
  free(path);
  submit(a);
}

/* hands the len bytes of s to the front-end for the clipboard, and says
 * how many on the status bar. */
static void copy(app* a, const char* s, usz len) {
  free(a->clip);
  a->clip = clip_osc52(s, len);

  jv size = view_text(jv_number((f64)len), VIEW_BYTES, NULL);
  snprintf(a->note, sizeof(a->note), "copied %s", jv_string_value(size));
  jv_free(size);
}

/* a press focuses the zone under it and picks the tree line or the saved
 * query there, loading the query when it was already picked, or closes the
 * help or a sidebar prompt; the wheel scrolls the help, or the tree or the
 * sidebar under it without focusing it. true when the screen changed. */
static b8 mouse(app* a, app_event ev) {
  app_focus was = a->focus;
  i32 side_w = sidebar_width(&a->side, a->w);
  b8 in_side = ev.x < side_w && ev.y >= TREE_Y - 1 && ev.y < a->h - 2;
  b8 in_tree = !in_side && ev.y >= TREE_Y && ev.y < a->h - 2;

  if (ev.mouse != APP_MOUSE_PRESS && a->help)
    return help_scroll(&a->help_at, ev.mouse == APP_MOUSE_WHEEL_UP ? -WHEEL_ROWS : WHEEL_ROWS);

  if (ev.mouse != APP_MOUSE_PRESS) {
    i32 rows = ev.mouse == APP_MOUSE_WHEEL_UP ? -WHEEL_ROWS : WHEEL_ROWS;

    if (in_side) return sidebar_scroll(&a->side, rows);
    if (!in_tree) return 0;

    return tree_scroll(a->tree, rows);
  }

  if (a->help) {
    a->help = 0;
    return 1;
  }

  if (a->searching) {
    search_key(a, ESC);
    return 1;
  }

  if (a->side.naming) {
    name_key(a, ESC);
    return 1;
  }

  if (a->side.deleting) {
    a->side.deleting = 0;
    return 1;
  }

  if (ev.y < QUERY_ROWS) a->focus = FOCUS_QUERY;
  if (in_side) {
    a->focus = FOCUS_SIDEBAR;
    if (sidebar_click(&a->side, ev.y - TREE_Y)) return 1;
    if (was != FOCUS_SIDEBAR || ev.y - TREE_Y != a->side.cursor - a->side.top)
      return a->focus != was;

    sidebar_enter(a);
    return 1;
  }

  if (!in_tree) return a->focus != was;

  a->focus = FOCUS_TREE;
  return tree_click(
    a->tree,
    ev.x - side_w - TREE_X,
    ev.y - TREE_Y,
    was == FOCUS_TREE
  ) || a->focus != was;
}

/* loads the selected saved query into the box and runs it. */
static void sidebar_enter(app* a) {
  const char* name = sidebar_selected(&a->side);

  if (!name) return;

  char err[256];
  char* text = store_read(a->side.store, name, err, sizeof(err));
  if (!text) {
    store_failed(a, err);
    return;
  }

  store_failed(a, NULL);
  editor_set(&a->query, text);
  set_loaded(a, name, text);
  submit(a);
}

/* writes the box over the loaded saved query, or asks for a name. */
static void save(app* a) {
  if (a->loaded) write_as(a, a->loaded);
  else start_naming(a, "");
}

/* opens the name row with name in it, showing the sidebar if hidden. */
static void start_naming(app* a, const char* name) {
  a->named_from = a->focus;
  a->side_opened = !a->side.shown;
  a->side.shown = 1;
  a->side.naming = 1;
  a->focus = FOCUS_SIDEBAR;
  editor_set(&a->side.name, name);
  store_failed(a, NULL);
}

/* drops the loaded query and starts over from . in the box. */
static void new_query(app* a) {
  editor_set(&a->query, ".");
  set_loaded(a, NULL, NULL);
  store_failed(a, NULL);
  a->focus = FOCUS_QUERY;
  submit(a);
}

/* saves the box under name and loads it from there. false on a write
 * error, shown in the status. */
static b8 write_as(app* a, const char* name) {
  const char* text = a->query.text ? a->query.text : "";
  char err[256];

  if (!store_write(a->side.store, name, text, err, sizeof(err))) {
    store_failed(a, err);
    return 0;
  }

  char* copy = strdup(text);
  if (!copy) abort();

  store_failed(a, NULL);
  set_loaded(a, name, copy);
  return 1;
}

static void name_key(app* a, u32 key) {
  if (key == ESC) {
    stop_naming(a);
    if (a->side_opened) a->side.shown = 0;

    store_failed(a, NULL);
    return;
  }

  if (key == APP_KEY_ENTER) save_as(a);
  else edit(&a->side.name, key);
}

/* saves the box under the typed name and loads it. a name saved before,
 * other than the loaded one, or one the store refuses, keeps the row open
 * with the reason. */
static void save_as(app* a) {
  const char* name = a->side.name.text;

  if (!name || !*name) return;

  b8 same = a->loaded && strcmp(name, a->loaded) == 0;
  if (!same && store_find(a->side.store, name) >= 0) {
    char err[256];

    snprintf(err, sizeof(err), "%s is already saved, load it to overwrite it", name);
    store_failed(a, err);
    return;
  }

  if (!write_as(a, name)) return;

  sidebar_select(&a->side, name);
  stop_naming(a);
}

static void stop_naming(app* a) {
  a->side.naming = 0;
  a->focus = a->named_from;
}

/* the box keeps the text of a deleted loaded query, without its label. */
static void delete_selected(app* a) {
  char* name = strdup(sidebar_selected(&a->side));

  if (!name) abort();

  char err[256];
  b8 ok = store_delete(a->side.store, name, err, sizeof(err));
  store_failed(a, ok ? NULL : err);

  if (a->loaded && strcmp(a->loaded, name) == 0 && store_find(a->side.store, name) < 0)
    set_loaded(a, NULL, NULL);

  sidebar_select(&a->side, NULL);
  free(name);
}

/* looks at the query folder once a watch interval, or a second: the list
 * follows it, and the box follows the loaded query's file unless edited.
 * a change cancels a delete, so y never removes a name not asked about.
 * true when something shown changed. */
static b8 rescan(app* a, i64 now) {
  if (!a->side.store || now < a->scan_at) return 0;

  a->scan_at = now + (a->watch_ms ? a->watch_ms : SCAN_MS);

  const char* name = sidebar_selected(&a->side);
  char* selected = name ? strdup(name) : NULL;
  if (!store_rescan(a->side.store)) {
    free(selected);
    return 0;
  }

  sidebar_select(&a->side, selected);
  free(selected);
  a->side.deleting = 0;
  if (!a->loaded) return 1;

  i32 i = store_find(a->side.store, a->loaded);
  if (i < 0) {
    set_loaded(a, NULL, NULL);
    return 1;
  }

  file_stamp st = store_stamp(a->side.store, (usz)i);
  if (st.mtime_ns == a->loaded_stamp.mtime_ns && st.size == a->loaded_stamp.size) return 1;

  char err[256];
  char* text = store_read(a->side.store, a->loaded, err, sizeof(err));
  if (!text) return 1;

  if (!modified(a)) {
    editor_set(&a->query, text);
    submit(a);
  }

  set_loaded(a, a->loaded, text);
  return 1;
}

static b8 modified(const app* a) {
  return a->loaded && strcmp(a->query.text ? a->query.text : "", a->loaded_text) != 0;
}

/* takes text; name may be a->loaded. */
static void set_loaded(app* a, const char* name, char* text) {
  char* copy = name ? strdup(name) : NULL;

  free(a->loaded);
  free(a->loaded_text);
  a->loaded = copy;
  a->loaded_text = text;
  if (!copy) return;

  i32 i = store_find(a->side.store, copy);
  if (i >= 0) a->loaded_stamp = store_stamp(a->side.store, (usz)i);
}

static void store_failed(app* a, const char* err) {
  jv_free(a->store_error);
  a->store_error = err ? jv_string(err) : jv_null();
}

/* true when the file changed, went missing or came back. */
static b8 poll(app* a, i64 now) {
  b8 first = a->poll_at == 0;
  file_stamp st;

  if (!first && now < a->poll_at) return 0;

  a->poll_at = now + a->watch_ms;
  if (first) return 0;

  if (!file_stat(a->path, &st)) {
    if (a->missing) return 0;

    a->missing = 1;
    jv_free(a->load_error);
    a->load_error = jv_string_fmt("%s: %s", a->path, strerror(errno));
    return 1;
  }

  if (!a->missing && st.mtime_ns == a->stamp.mtime_ns && st.size == a->stamp.size) return 0;

  a->missing = 0;
  reload(a);
  return 1;
}

/* a file that cannot be read or parsed keeps the last data, with the
 * reason shown. */
static void reload(app* a) {
  load_result loaded;
  file_stamp stamp;

  jv_free(a->load_error);

  char err[256];
  if (!load_file(a->path, a->raw, a->slurp, &loaded, &stamp, err, sizeof(err))) {
    a->load_error = jv_string(err);
    return;
  }

  a->stamp = stamp;
  a->load_error = loaded.error;
  if (jv_get_kind(loaded.error) == JV_KIND_STRING) {
    jv_free(loaded.inputs);
    return;
  }

  a->loaded_ms = a->wall_ms;
  eval_set_inputs(a->ev, loaded.inputs);
  submit(a);
}

/* true when the text changed. */
static b8 edit(editor* e, u32 key) {
  switch (key) {
    case APP_KEY_BACKSPACE: return editor_backspace(e);
    case APP_KEY_DELETE: return editor_delete(e);
    case APP_KEY_WORD_BACKSPACE:
    case CTRL('w'): return editor_delete_word(e);
    case CTRL('u'): return editor_delete_to_start(e);
    case CTRL('k'): return editor_delete_to_end(e);
    case APP_KEY_WORD_LEFT:
      editor_word_left(e);
      return 0;
    case APP_KEY_WORD_RIGHT:
      editor_word_right(e);
      return 0;
    case APP_KEY_LEFT:
      editor_left(e);
      return 0;
    case APP_KEY_RIGHT:
      editor_right(e);
      return 0;
    case CTRL('a'):
    case APP_KEY_HOME:
      editor_home(e);
      return 0;
    case CTRL('e'):
    case APP_KEY_END:
      editor_end(e);
      return 0;
    default: break;
  }

  if (key < 0x20 || key == 0x7f || key >= 0x110000) return 0;

  editor_insert(e, key);
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
  return jv_get_kind(a->error) == JV_KIND_STRING || jv_get_kind(a->load_error) == JV_KIND_STRING
    || jv_get_kind(a->store_error) == JV_KIND_STRING;
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
  if (a->loaded) {
    x = grid_put(g, 2, 0, g->w - 2, " ", 1, STYLE_LABEL);
    x = grid_put(g, x, 0, g->w - 2, a->loaded, strlen(a->loaded), STYLE_LABEL);
    if (modified(a)) {
      x = grid_put(g, x, 0, g->w - 2, " ", 1, STYLE_LABEL);
      x = grid_put(g, x, 0, g->w - 2, MODIFIED, strlen(MODIFIED), STYLE_ACCENT);
    }

    grid_put(g, x, 0, g->w - 2, " ", 1, STYLE_LABEL);
  }

  x = grid_put(
    g, 2, 1,
    g->w - 2,
    PROMPT,
    strlen(PROMPT),
    a->focus == FOCUS_QUERY ? STYLE_ACCENT : STYLE_BORDER
  );
  highlight(a->query.text, a->query.len, styles);
  if (a->query.len == 0)
    grid_put_fit(g, x, 1, g->w - 2, PLACEHOLDER, strlen(PLACEHOLDER), STYLE_EMPTY);

  x = editor_render(&a->query, g, x, 1, g->w - 2, styles);
  free(styles);
  if (a->focus != FOCUS_QUERY) return;

  g->cursor_x = x;
  g->cursor_y = 1;
}

/* the pattern being typed after a slash, which match the cursor is on, and
 * the prompt's keys on the right. */
static void render_search(app* a, grid* g) {
  i32 y = g->h - 1;
  i32 keys;
  i32 x;

  if (y < 0) return;

  keys = help_prompt_bar(g, y, HELP_PROMPT_SEARCH);
  x = grid_put(g, 1, y, keys, "/", 1, STYLE_ACCENT);
  x = editor_render(&a->search, g, x, y, keys - 12, NULL);
  g->cursor_x = x;
  g->cursor_y = y;
  if (!a->search.text) return;

  char found[32];
  i32 total = found_text(a, found, sizeof(found));
  grid_put(g, keys - 12, y, keys - 1, found, strlen(found), total ? STYLE_HELP : STYLE_ERROR);
}

/* a pill, the file, then the cursor's path or the error, the search, and the
 * outputs with their time on the right. */
static void render_status(app* a, grid* g) {
  i32 y = g->h - 2;
  jv err = jv_get_kind(a->error) == JV_KIND_STRING ? a->error : a->load_error;
  i32 name_w;
  i32 end;
  i32 right;
  i32 x;

  if (y < 0) return;
  if (jv_get_kind(err) != JV_KIND_STRING) err = a->store_error;

  grid_fill(g, y, STYLE_STATUS);

  char outputs[64];
  char time[32];
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

  const char* pill = a->missing ? " MISSING " : failed(a) ? " ERROR " : " LIVEJQ ";
  x = segment(g, 0, pill, failed(a) ? STYLE_PILL_ERROR : STYLE_PILL);
  end = g->w - utf8_columns(outputs, strlen(outputs)) - utf8_columns(time, strlen(time));

  const char* pattern = a->searching ? NULL : tree_pattern(a->tree);
  char found[32];
  i32 total = pattern ? found_text(a, found, sizeof(found)) : 0;
  i32 search_x = end - (pattern ? search_width(pattern, found) : 0);
  end = search_x;
  if (end < x) end = x;

  b8 erred = jv_get_kind(err) == JV_KIND_STRING;
  char path[512];
  const char* mid = path;
  if (erred) mid = jv_string_value(err);
  else if (a->note[0]) mid = a->note;
  else tree_path(a->tree, path, sizeof(path));

  const char* nl = strchr(mid, '\n');
  usz mid_len = nl ? (usz)(nl - mid) : strlen(mid);
  i32 keep = erred ? utf8_columns(mid, mid_len) : MIN_PATH;
  u8 mid_style = erred ? STYLE_STATUS_ERROR : a->note[0] ? STYLE_STATUS : STYLE_STATUS_FAINT;
  char info[160];
  times(a, info, sizeof(info), end - x - utf8_columns(a->name, strlen(a->name)) - 2 - keep);
  right = end - utf8_columns(info, strlen(info));

  name_w = utf8_columns(a->name, strlen(a->name));
  if (name_w > (right - x) / 2 - 2) name_w = (right - x) / 2 - 2;
  if (name_w > 0) {
    x = grid_put_fit(
      g, x + 1, y,
      x + 1 + name_w,
      a->name,
      strlen(a->name),
      STYLE_STATUS
    ) + 1;
  }

  grid_put_fit(g, x + 1, y, right - 1, mid, mid_len, mid_style);
  segment(g, right, info, STYLE_STATUS);
  x = search_x;
  if (pattern) {
    u8 st = total ? STYLE_STATUS : STYLE_STATUS_ERROR;

    x = segment(g, x, " /", st);
    x = grid_put_fit(g, x, y, x + search_width(pattern, "") - 4, pattern, strlen(pattern), st);
    x = segment(g, x, " ", st);
    x = segment(g, x, found, st);
    x = segment(g, x, " ", st);
  }

  segment(g, segment(g, x, outputs, STYLE_STATUS_INFO), time, STYLE_STATUS_TIME);
}

/* the columns " /pattern found " takes, the pattern cut to MAX_PATTERN. */
static i32 search_width(const char* pattern, const char* found) {
  i32 w = utf8_columns(pattern, strlen(pattern));

  if (w > MAX_PATTERN) w = MAX_PATTERN;

  return w + 4 + utf8_columns(found, strlen(found));
}

/* which match the cursor is on and of how many, "no match", or "-/n" off
 * the matches. Returns how many. */
static i32 found_text(app* a, char* buf, usz cap) {
  i32 at;
  i32 total;

  tree_matches(a->tree, &at, &total);
  if (total == 0) snprintf(buf, cap, "no match");
  else if (at == 0) snprintf(buf, cap, "-/%d", total);
  else snprintf(buf, cap, "%d/%d", at, total);

  return total;
}

/* the watch interval, the file's modification time and when it was last
 * read, as many of them as fit in room columns. */
static void times(const app* a, char* buf, usz cap, i32 room) {
  char at[32];
  usz n = 0;

  buf[0] = '\0';

  char part[64];
  if (a->watch_ms) {
    jv every = view_text(jv_number((f64)a->watch_ms / 1000), VIEW_DURATION, NULL);

    snprintf(part, sizeof(part), " " WATCHING "%s ", jv_string_value(every));
    jv_free(every);
    if (!add_part(buf, cap, &n, part, room)) return;
  }

  view_clock(a->stamp.mtime_ns / 1000000, a->wall_ms, at, sizeof(at));
  snprintf(part, sizeof(part), " mod %s ", at);
  if (!add_part(buf, cap, &n, part, room) || !a->loaded_ms) return;

  view_clock(a->loaded_ms, a->wall_ms, at, sizeof(at));
  snprintf(part, sizeof(part), " loaded %s ", at);
  add_part(buf, cap, &n, part, room);
}

/* appends part to the n bytes of buf when they still fit in room columns. */
static b8 add_part(char* buf, usz cap, usz* n, const char* part, i32 room) {
  usz len = strlen(part);

  if (*n + len >= cap || utf8_columns(buf, *n) + utf8_columns(part, len) > room) return 0;

  memcpy(buf + *n, part, len + 1);
  *n += len;
  return 1;
}

static i32 segment(grid* g, i32 x, const char* s, u8 st) {
  return grid_put(g, x, g->h - 2, g->w, s, strlen(s), st);
}
