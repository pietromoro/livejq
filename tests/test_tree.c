/* tree tests - navigation, folds and state kept by path, through the core. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "clip.h"
#include "defines.h"
#include "loader.h"
#include "thread.h"
#include "views.h"

#include "test_helper.in"

#define TOKEN                                                                                                   \
   "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJ1PjE_IiwiaWF0IjoxNzMwMzAwMDAwLCJleHAiOjE3MzAzMDM2MDAsIm4i"  \
   "OiJcdTAwZTgifQ.c2ln_-"
#define W         80
#define H         16
#define TOP       4 /* the tree's first row */
#define LEFT      2 /* and column */
#define ROWS      (H - TOP - 2)
#define QUERY_X   4 /* the query's first column, after the prompt */
#define SETTLE_MS 5000
#define WALL_MS   1730300090000 /* 90 s after the sample's ts */
#define OPEN      "\xe2\x96\xbe "
#define CLOSED    "\xe2\x96\xb8 "

static app* open_app(const char* path, const char* query);
static app* open_watched(const char* path, const char* query, i64 watch_ms);
static void press(app* a, u32 key);
static b8 tick(app* a, i64 now);
static b8 mouse(app* a, app_mouse m, i32 x, i32 y);
static void type(app* a, const char* s);
static void retype(app* a, const char* s);
static void write_file(const char* path, const char* text);
static const char* line(app* a, i32 y);
static const char* cells(app* a, i32 x, i32 y, i32 max_x);
static const char* row(app* a, i32 n);
static const char* query(app* a);
static const char* status(app* a);
static b8 selected(app* a, i32 n);
static u8 style_at(app* a, i32 x, i32 n);
static b8 copied(app* a, const char* text);
static void test_focus(void);
static void test_click_focuses(void);
static void test_click_picks_lines(void);
static void test_wheel_scrolls(void);
static void test_quit_keys(void);
static void test_navigation(void);
static void test_fold_and_unfold(void);
static void test_fold_subtree(void);
static void test_page_up_and_down(void);
static void test_query_edit_keeps_state(void);
static void test_cursor_falls_back_to_ancestor(void);
static void test_reload_keeps_state(void);
static void test_reload_keeps_views(void);
static void test_watch_reloads(void);
static void test_watch_keeps_last_good(void);
static void test_watch_missing_file(void);
static void test_watch_times(void);
static void test_default_expansion(void);
static void test_single_root_unlabeled(void);
static void test_truncation(void);
static void test_path_quoting(void);
static void test_view_cycling(void);
static void test_view_kept_by_path(void);
static void test_view_keys(void);
static void test_view_siblings(void);
static void test_json_subtree(void);
static void test_jwt_subtree(void);
static void test_query_subtree(void);
static void test_time_unit(void);
static void test_search(void);
static void test_search_reveals(void);
static void test_search_values(void);
static void test_search_marks(void);
static void test_copy_value(void);

static grid* screen;

static app* open_app(const char* path, const char* query) { return open_watched(path, query, 0); }

static app* open_watched(const char* path, const char* query, i64 watch_ms) {
  app_opts o;

  memset(&o, 0, sizeof(o));
  o.path = path;
  o.query = query;
  o.args = jv_invalid();
  o.watch_ms = watch_ms;

  char err[256];
  app* a = app_new(&o, err, sizeof(err));
  if (!a) FAIL("app_new(%s): %s", path, err);
  else CHECK(app_settle(a, SETTLE_MS));

  return a;
}

static void press(app* a, u32 key) {
  app_event ev = { 0 };

  ev.kind = EV_KEY;
  ev.key = key;
  ev.wall_ms = WALL_MS;
  app_feed(a, ev);
}

static b8 tick(app* a, i64 now) {
  app_event ev = { 0 };

  ev.kind = EV_TICK;
  ev.now_ms = now;
  ev.wall_ms = WALL_MS;
  return app_feed(a, ev);
}

static b8 mouse(app* a, app_mouse m, i32 x, i32 y) {
  app_event ev = { 0 };

  ev.kind = EV_MOUSE;
  ev.mouse = m;
  ev.x = x;
  ev.y = y;
  ev.now_ms = 0;
  return app_feed(a, ev);
}

static void type(app* a, const char* s) {
  app_event ev = { 0 };

  ev.kind = EV_KEY;
  ev.wall_ms = WALL_MS;
  while (*s) {
    ev.key = (u8)*s++;
    app_feed(a, ev);
  }
}

/* replaces the query from the tree, and comes back to the tree. */
static void retype(app* a, const char* s) {
  press(a, '\t');
  for (i32 i = 0; i < 64; i++) press(a, APP_KEY_BACKSPACE);

  type(a, s);
  press(a, '\t');
  CHECK(app_settle(a, SETTLE_MS));
}

static void write_file(const char* path, const char* text) {
  FILE* f = fopen(path, "wb");

  if (!f) {
    FAIL("cannot write %s", path);
    return;
  }

  fputs(text, f);
  fclose(f);
}

static const char* line(app* a, i32 y) {
  static char buf[W * 4 + 1];

  if (!screen) screen = grid_new(W, H);
  app_render(a, screen);
  grid_line(screen, y, buf, sizeof(buf));
  return buf;
}

/* row y from column x up to max_x, without trailing blanks. */
static const char* cells(app* a, i32 x, i32 y, i32 max_x) {
  static char buf[W * 4 + 1];
  usz n = 0;
  usz keep = 0;

  line(a, y);
  for (; x < max_x; x++) {
    u32 c = grid_at(screen, x, y).ch;

    n += utf8_put(c, buf + n);
    if (c != ' ') keep = n;
  }

  buf[keep] = '\0';
  return buf;
}

/* line n of the tree's view. */
static const char* row(app* a, i32 n) {
  i32 y = TOP + n;

  return cells(a, LEFT, y, W);
}

static const char* query(app* a) {
  i32 max_x = W - 1;

  return cells(a, QUERY_X, 1, max_x);
}

/* the cursor's path on the status bar. */
static const char* status(app* a) {
  static char buf[W * 4 + 1];
  usz n = 0;

  line(a, H - 2);
  for (i32 x = 0; x < W; x++) {
    cell c = grid_at(screen, x, H - 2);

    if (c.style == STYLE_STATUS_FAINT) n += utf8_put(c.ch, buf + n);
  }

  buf[n] = '\0';
  return buf;
}

static b8 selected(app* a, i32 n) {
  line(a, TOP + n);
  return (grid_at(screen, LEFT - 1, TOP + n).style & STYLE_SELECTED) != 0;
}

/* the style of column x of line n of the tree's view, without the cursor's
 * flags. */
static u8 style_at(app* a, i32 x, i32 n) {
  line(a, TOP + n);
  return grid_at(screen, LEFT + x, TOP + n).style & ~(STYLE_SELECTED | STYLE_SELECTED_IDLE);
}

/* whether the last key put text on the clipboard. */
static b8 copied(app* a, const char* text) {
  char* osc = app_take_clipboard(a);
  char* want = clip_osc52(text, strlen(text));
  b8 same = osc && strcmp(osc, want) == 0;

  free(osc);
  free(want);
  return same;
}

static void test_focus(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  ASSERT_EQ(app_focused(a), FOCUS_QUERY);
  line(a, 0);
  ASSERT_EQ(screen->cursor_x, 5);
  ASSERT_EQ(screen->cursor_y, 1);
  ASSERT_EQ(grid_at(screen, LEFT - 1, TOP).style, STYLE_GUTTER | STYLE_SELECTED_IDLE);
  press(a, '\t');
  ASSERT_EQ(app_focused(a), FOCUS_TREE);
  CHECK(selected(a, 0));
  type(a, "jx");
  ASSERT_STREQ(query(a), ".");
  ASSERT_EQ(screen->cursor_x, -1);
  press(a, APP_KEY_BACKTAB);
  ASSERT_EQ(app_focused(a), FOCUS_QUERY);
  press(a, APP_KEY_HOME);
  type(a, "[");
  press(a, APP_KEY_END);
  type(a, "]");
  ASSERT_STREQ(query(a), "[.]");
  ASSERT_EQ(screen->cursor_x, 7);
  press(a, 'w' & 0x1f);
  ASSERT_STREQ(query(a), "a jq filter");
  ASSERT_EQ(grid_at(screen, QUERY_X, 1).style, STYLE_EMPTY);
  ASSERT_EQ(screen->cursor_x, QUERY_X);
  app_free(a);
}

/* the status line is in no zone, and a press over the help only closes it. */
static void test_click_focuses(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  line(a, 0);
  CHECK(mouse(a, APP_MOUSE_PRESS, LEFT + 5, TOP + 2));
  ASSERT_EQ(app_focused(a), FOCUS_TREE);
  CHECK(!mouse(a, APP_MOUSE_PRESS, 5, H - 2));
  ASSERT_EQ(app_focused(a), FOCUS_TREE);
  CHECK(mouse(a, APP_MOUSE_PRESS, 20, 1));
  ASSERT_EQ(app_focused(a), FOCUS_QUERY);
  press(a, APP_KEY_F1);
  CHECK(mouse(a, APP_MOUSE_PRESS, LEFT + 5, TOP + 2));
  ASSERT_EQ(app_focused(a), FOCUS_QUERY);
  line(a, 0);
  ASSERT_EQ(screen->cursor_x, 5);
  app_free(a);
}

/* a press on a fold mark toggles its line, and so does a second press on
 * the selected line once the tree has focus. */
static void test_click_picks_lines(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  line(a, 0);
  CHECK(mouse(a, APP_MOUSE_PRESS, LEFT + 10, TOP + 3));
  ASSERT_EQ(app_focused(a), FOCUS_TREE);
  CHECK(selected(a, 3));
  ASSERT_STREQ(status(a), "#0 .users[0].id");
  mouse(a, APP_MOUSE_PRESS, LEFT + 10, TOP + 3);
  ASSERT_STREQ(row(a, 3), "        id: 1");

  mouse(a, APP_MOUSE_PRESS, LEFT + 4, TOP + 2);
  CHECK(selected(a, 2));
  ASSERT_STREQ(row(a, 2), "    " CLOSED "0: {3}");
  ASSERT_STREQ(row(a, 3), "    " OPEN "1: {3}");
  mouse(a, APP_MOUSE_PRESS, LEFT + 20, TOP + 2);
  ASSERT_STREQ(row(a, 2), "    " OPEN "0: {3}");

  press(a, '\t');
  mouse(a, APP_MOUSE_PRESS, LEFT + 20, TOP + 2);
  ASSERT_EQ(app_focused(a), FOCUS_TREE);
  ASSERT_STREQ(row(a, 2), "    " OPEN "0: {3}");
  app_free(a);

  a = open_app("tests/data/sample.json", "1");
  if (!a) return;

  press(a, '\t');
  line(a, 0);
  CHECK(!mouse(a, APP_MOUSE_PRESS, LEFT + 5, TOP + 4));
  ASSERT_STREQ(status(a), "#0 .");
  app_free(a);
}

/* the cursor stays on its row while it is in view. */
static void test_wheel_scrolls(void) {
  app* a = open_app("tests/data/sample.json", "select(.ok) | [range(30)]");

  if (!a) return;

  line(a, 0);
  CHECK(mouse(a, APP_MOUSE_WHEEL_DOWN, LEFT + 5, TOP + 3));
  ASSERT_EQ(app_focused(a), FOCUS_QUERY);
  ASSERT_STREQ(row(a, 0), "    2: 2");
  ASSERT_STREQ(status(a), ".[2]");

  press(a, '\t');
  for (i32 i = 0; i < 5; i++) press(a, 'j');
  CHECK(mouse(a, APP_MOUSE_WHEEL_UP, LEFT + 5, TOP + 3));
  ASSERT_STREQ(row(a, 0), OPEN "[30]");
  ASSERT_STREQ(status(a), ".[7]");
  CHECK(!mouse(a, APP_MOUSE_WHEEL_UP, LEFT + 5, TOP + 3));

  for (i32 i = 0; i < 7; i++) CHECK(mouse(a, APP_MOUSE_WHEEL_DOWN, LEFT + 5, TOP + 3));
  CHECK(!mouse(a, APP_MOUSE_WHEEL_DOWN, LEFT + 5, TOP + 3));
  ASSERT_STREQ(row(a, ROWS - 1), "    29: 29");
  ASSERT_STREQ(status(a), ".[20]");

  CHECK(!mouse(a, APP_MOUSE_WHEEL_UP, 5, 1));
  CHECK(!mouse(a, APP_MOUSE_WHEEL_UP, 5, H - 2));
  press(a, '?');
  CHECK(!mouse(a, APP_MOUSE_WHEEL_UP, LEFT + 5, TOP + 3));
  app_free(a);
}

static void test_quit_keys(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  type(a, "q");
  CHECK(!app_quitting(a));
  ASSERT_STREQ(query(a), ".q");
  press(a, '\t');
  press(a, '?');
  press(a, 'q');
  CHECK(!app_quitting(a));
  press(a, 'q');
  CHECK(app_quitting(a));
  app_free(a);

  a = open_app("tests/data/sample.json", ".");
  if (!a) return;

  press(a, APP_KEY_F1);
  press(a, 'c' & 0x1f);
  CHECK(app_quitting(a));
  app_free(a);
}

static void test_navigation(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, '\t');
  ASSERT_STREQ(row(a, 0), OPEN "#0 {2}");
  ASSERT_STREQ(row(a, 1), "  " OPEN "users: [2]");
  ASSERT_STREQ(row(a, 2), "    " OPEN "0: {3}");
  ASSERT_STREQ(row(a, 3), "        id: 1");
  ASSERT_STREQ(row(a, 4), "        name: \"alice\"");
  CHECK(selected(a, 0));
  ASSERT_STREQ(status(a), "#0 .");

  press(a, 'k');
  CHECK(selected(a, 0));
  press(a, 'j');
  press(a, APP_KEY_DOWN);
  press(a, 'j');
  CHECK(selected(a, 3));
  CHECK(!selected(a, 0));
  ASSERT_STREQ(status(a), "#0 .users[0].id");

  press(a, APP_KEY_UP);
  ASSERT_STREQ(status(a), "#0 .users[0]");

  press(a, 'G');
  ASSERT_STREQ(status(a), "#1 .second");
  ASSERT_STREQ(row(a, ROWS - 1), "    second: null");
  CHECK(selected(a, ROWS - 1));
  ASSERT_STREQ(row(a, ROWS - 2), OPEN "#1 {1}");

  press(a, APP_KEY_HOME);
  ASSERT_STREQ(row(a, 0), OPEN "#0 {2}");
  CHECK(selected(a, 0));
  press(a, APP_KEY_END);
  ASSERT_STREQ(status(a), "#1 .second");
  press(a, 'g');
  ASSERT_STREQ(status(a), "#0 .");
  app_free(a);
}

static void test_fold_and_unfold(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, '\t');
  press(a, 'j');
  press(a, 'h');
  ASSERT_STREQ(row(a, 1), "  " CLOSED "users: [2]");
  ASSERT_STREQ(row(a, 2), "    ok: true");
  CHECK(selected(a, 1));

  press(a, 'h');
  ASSERT_STREQ(status(a), "#0 .");
  press(a, 'j');
  press(a, APP_KEY_RIGHT);
  ASSERT_STREQ(row(a, 1), "  " OPEN "users: [2]");
  ASSERT_STREQ(row(a, 2), "    " OPEN "0: {3}");

  press(a, ' ');
  ASSERT_STREQ(row(a, 2), "    ok: true");
  press(a, ' ');
  ASSERT_STREQ(row(a, 2), "    " OPEN "0: {3}");
  press(a, APP_KEY_ENTER);
  ASSERT_STREQ(row(a, 2), "    ok: true");
  press(a, APP_KEY_ENTER);
  ASSERT_STREQ(row(a, 2), "    " OPEN "0: {3}");

  press(a, 'j');
  press(a, 'j');
  ASSERT_STREQ(status(a), "#0 .users[0].id");
  press(a, APP_KEY_LEFT);
  ASSERT_STREQ(status(a), "#0 .users[0]");
  press(a, 'l');
  press(a, 'j');
  ASSERT_STREQ(status(a), "#0 .users[0].id");
  app_free(a);
}

static void test_fold_subtree(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, '\t');
  press(a, 'H');
  ASSERT_STREQ(row(a, 0), CLOSED "#0 {2}");
  ASSERT_STREQ(row(a, 1), OPEN "#1 {1}");

  press(a, 'l');
  ASSERT_STREQ(row(a, 1), "  " CLOSED "users: [2]");
  ASSERT_STREQ(row(a, 2), "    ok: true");

  press(a, 'L');
  ASSERT_STREQ(row(a, 1), "  " OPEN "users: [2]");
  ASSERT_STREQ(row(a, 2), "    " OPEN "0: {3}");
  ASSERT_STREQ(status(a), "#0 .");

  press(a, 'j');
  press(a, 'H');
  press(a, 'l');
  ASSERT_STREQ(row(a, 2), "    " CLOSED "0: {3}");
  ASSERT_STREQ(row(a, 3), "    " CLOSED "1: {3}");
  app_free(a);
}

static void test_page_up_and_down(void) {
  app* a = open_app("tests/data/sample.json", "select(.ok) | [range(30)]");

  if (!a) return;

  press(a, '\t');
  ASSERT_STREQ(row(a, 0), OPEN "[30]");
  press(a, APP_KEY_PAGE_DOWN);
  ASSERT_STREQ(status(a), ".[9]");
  ASSERT_STREQ(row(a, 0), "    9: 9");
  CHECK(selected(a, 0));

  press(a, APP_KEY_PAGE_DOWN);
  press(a, APP_KEY_PAGE_DOWN);
  ASSERT_STREQ(status(a), ".[29]");
  ASSERT_STREQ(row(a, ROWS - 1), "    29: 29");
  CHECK(selected(a, ROWS - 1));

  press(a, APP_KEY_PAGE_DOWN);
  ASSERT_STREQ(status(a), ".[29]");

  press(a, APP_KEY_PAGE_UP);
  ASSERT_STREQ(status(a), ".[19]");
  press(a, APP_KEY_PAGE_UP);
  press(a, APP_KEY_PAGE_UP);
  press(a, APP_KEY_PAGE_UP);
  ASSERT_STREQ(status(a), ".");
  ASSERT_STREQ(row(a, 0), OPEN "[30]");
  app_free(a);
}

static void test_query_edit_keeps_state(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, '\t');
  press(a, 'j');
  press(a, 'j');
  press(a, 'h');
  type(a, "jjj");
  ASSERT_STREQ(status(a), "#0 .users[1].name");

  retype(a, ". + {extra: 1}");
  ASSERT_STREQ(row(a, 2), "    " CLOSED "0: {3}");
  ASSERT_STREQ(row(a, 3), "    " OPEN "1: {3}");
  ASSERT_STREQ(status(a), "#0 .users[1].name");
  CHECK(selected(a, 5));
  app_free(a);
}

static void test_cursor_falls_back_to_ancestor(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, '\t');
  type(a, "jjjjjjjj");
  ASSERT_STREQ(status(a), "#0 .users[1].name");

  retype(a, "del(.users[1])");
  ASSERT_STREQ(status(a), "#0 .users");

  retype(a, "1");
  ASSERT_STREQ(status(a), "#0 .");
  app_free(a);
}

static void test_reload_keeps_state(void) {
  const char* dir = getenv("TMPDIR");
  char path[512];
  app_event ev = { 0 };
  app* a;

  snprintf(path, sizeof(path), "%s/livejq-tree-%" PRId64 ".json", dir ? dir : "/tmp", clock_us());
  write_file(path, "{\"a\":{\"b\":1},\"c\":[1,2]}");
  a = open_app(path, ".");
  if (!a) return;

  press(a, '\t');
  press(a, 'j');
  press(a, 'h');
  press(a, 'G');
  ASSERT_STREQ(status(a), ".c[1]");

  write_file(path, "{\"a\":{\"b\":2},\"c\":[1,2,3]}");
  ev.kind = EV_FILE_CHANGED;
  ev.key = 0;
  ev.now_ms = 0;
  app_feed(a, ev);
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(row(a, 1), "  " CLOSED "a: {1}");
  ASSERT_STREQ(row(a, 2), "  " OPEN "c: [3]");
  ASSERT_STREQ(status(a), ".c[1]");
  CHECK(selected(a, 4));

  remove(path);
  app_feed(a, ev);
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(row(a, 2), "  " OPEN "c: [3]");
  line(a, H - 2);
  ASSERT_EQ(grid_at(screen, 1, H - 2).style, STYLE_PILL_ERROR);
  app_free(a);
}

static void test_reload_keeps_views(void) {
  const char* dir = getenv("TMPDIR");
  char path[512];
  app_event ev = { 0 };
  app* a;

  snprintf(path, sizeof(path), "%s/livejq-views-%" PRId64 ".json", dir ? dir : "/tmp", clock_us());
  write_file(path, "{\"n\":255,\"s\":\"{\\\"k\\\":1}\"}");
  a = open_app(path, ".");
  if (!a) return;

  press(a, '\t');
  type(a, "jxjd");
  ASSERT_STREQ(row(a, 1), "    n: 0xff  hex");
  ASSERT_STREQ(row(a, 2), "  " OPEN "s: {1}  json");

  write_file(path, "{\"n\":4096,\"s\":\"{\\\"k\\\":2,\\\"m\\\":3}\"}");
  ev.kind = EV_FILE_CHANGED;
  app_feed(a, ev);
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(row(a, 1), "    n: 0x1000  hex");
  ASSERT_STREQ(row(a, 2), "  " OPEN "s: {2}  json");
  ASSERT_STREQ(row(a, 3), "      k: 2");
  ASSERT_STREQ(row(a, 4), "      m: 3");
  ASSERT_STREQ(status(a), ".s");
  remove(path);
  app_free(a);
}

static void test_watch_reloads(void) {
  const char* dir = getenv("TMPDIR");
  char path[512];
  app* a;

  snprintf(path, sizeof(path), "%s/livejq-watch-%" PRId64 ".json", dir ? dir : "/tmp", clock_us());
  write_file(path, "{\"n\":255}");
  a = open_watched(path, ".", 1000);
  if (!a) return;

  press(a, '\t');
  type(a, "jx");
  CHECK(!tick(a, 0));
  write_file(path, "{\"n\":4096,\"m\":1}");
  CHECK(!tick(a, 999));
  ASSERT_STREQ(row(a, 1), "    n: 0xff  hex");

  tick(a, 1000);
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(row(a, 1), "    n: 0x1000  hex");
  ASSERT_STREQ(row(a, 2), "    m: 1");
  ASSERT_STREQ(status(a), ".n");

  CHECK(!tick(a, 2000));
  remove(path);
  app_free(a);
}

static void test_watch_keeps_last_good(void) {
  const char* dir = getenv("TMPDIR");
  char path[512];
  app* a;

  snprintf(
    path,
    sizeof(path),
    "%s/livejq-midwrite-%" PRId64 ".json",
    dir ? dir : "/tmp",
    clock_us()
  );
  write_file(path, "{\"a\":1,\"b\":2}");
  a = open_watched(path, ".", 1000);
  if (!a) return;

  tick(a, 0);
  write_file(path, "{\"a\":3,\"b\":");
  CHECK(tick(a, 1000));
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(row(a, 1), "    a: 1");
  ASSERT_STREQ(row(a, 2), "    b: 2");
  CHECK(strstr(line(a, H - 2), "Unfinished") != NULL);
  ASSERT_EQ(grid_at(screen, 1, H - 2).style, STYLE_PILL_ERROR);

  CHECK(!tick(a, 2000));
  write_file(path, "{\"a\":3,\"b\":4}");
  tick(a, 3000);
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(row(a, 1), "    a: 3");
  ASSERT_STREQ(row(a, 2), "    b: 4");
  CHECK(strstr(line(a, H - 2), "Unfinished") == NULL);
  ASSERT_EQ(grid_at(screen, 1, H - 2).style, STYLE_PILL);
  remove(path);
  app_free(a);
}

static void test_watch_missing_file(void) {
  const char* dir = getenv("TMPDIR");
  char path[512];

  snprintf(
    path,
    sizeof(path),
    "%s/livejq-missing-%" PRId64 ".json",
    dir ? dir : "/tmp",
    clock_us()
  );
  write_file(path, "{\"a\":true}");

  app* a = open_watched(path, ".", 1000);
  if (!a) return;

  tick(a, 0);
  remove(path);
  CHECK(tick(a, 1000));
  ASSERT_STREQ(row(a, 1), "    a: true");
  ASSERT_EQ(grid_at(screen, LEFT + 7, TOP + 1).style, STYLE_DIM);
  CHECK(strstr(line(a, H - 2), " MISSING ") == line(a, H - 2));
  ASSERT_EQ(grid_at(screen, 1, H - 2).style, STYLE_PILL_ERROR);
  CHECK(!tick(a, 2000));

  write_file(path, "{\"a\":false}");
  CHECK(tick(a, 3000));
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(row(a, 1), "    a: false");
  ASSERT_EQ(grid_at(screen, LEFT + 7, TOP + 1).style, STYLE_FALSE);
  CHECK(strstr(line(a, H - 2), " LIVEJQ ") == line(a, H - 2));
  remove(path);
  app_free(a);
}

static void test_watch_times(void) {
  const char* dir = getenv("TMPDIR");
  char path[512];

  snprintf(path, sizeof(path), "%s/livejq-times-%" PRId64 ".json", dir ? dir : "/tmp", clock_us());
  write_file(path, "[1]");

  app* a = open_watched(path, ".", 1000);
  if (!a) return;

  tick(a, 0);

  file_stamp st;
  file_stat(path, &st);

  grid* wide = grid_new(140, H);
  app_render(a, wide);

  char buf[1024];
  grid_line(wide, H - 2, buf, sizeof(buf));
  CHECK(strstr(buf, " \xe2\x9f\xb3 1s  mod ") != NULL);

  char at[32];
  view_clock(st.mtime_ns / 1000000, WALL_MS, at, sizeof(at));

  char want[96];
  snprintf(want, sizeof(want), " mod %s  loaded ", at);
  CHECK(strstr(buf, want) != NULL);
  view_clock(WALL_MS, WALL_MS, at, sizeof(at));
  snprintf(want, sizeof(want), " loaded %s ", at);
  CHECK(strstr(buf, want) != NULL);

  app_event ev = { 0 };
  write_file(path, "[1,2]");
  ev.kind = EV_TICK;
  ev.now_ms = 1000;
  ev.wall_ms = WALL_MS + 60000;
  app_feed(a, ev);
  CHECK(app_settle(a, SETTLE_MS));
  app_render(a, wide);
  grid_line(wide, H - 2, buf, sizeof(buf));
  view_clock(WALL_MS + 60000, WALL_MS + 60000, at, sizeof(at));
  snprintf(want, sizeof(want), " loaded %s ", at);
  CHECK(strstr(buf, want) != NULL);
  CHECK(strstr(line(a, H - 2), " mod ") == NULL);
  grid_free(wide);
  remove(path);
  app_free(a);
}

static void test_default_expansion(void) {
  app* a = open_app("tests/data/sample.json", "select(.ok) | {a: [range(2000)], b: [1]}");

  if (!a) return;

  ASSERT_STREQ(row(a, 0), OPEN "{2}");
  ASSERT_STREQ(row(a, 1), "  " CLOSED "a: [2000]");
  ASSERT_STREQ(row(a, 2), "  " CLOSED "b: [1]");

  press(a, '\t');
  retype(a, "select(.ok) | [range(600)], [range(600)]");
  ASSERT_STREQ(row(a, 0), CLOSED "#0 [600]");
  ASSERT_STREQ(row(a, 1), CLOSED "#1 [600]");

  retype(a, "select(.ok) | [range(5)]");
  ASSERT_STREQ(row(a, 1), "    0: 0");
  app_free(a);
}

static void test_single_root_unlabeled(void) {
  app* a = open_app("tests/data/sample.json", "select(.ok) | .users[1]");

  if (!a) return;

  ASSERT_STREQ(row(a, 0), OPEN "{3}");
  ASSERT_STREQ(row(a, 3), "  " OPEN "tags: [1]");
  ASSERT_STREQ(row(a, 4), "      0: \"x\"");
  CHECK(strstr(line(a, H - 2), "1 outputs") != NULL);
  app_free(a);
}

static void test_truncation(void) {
  app* a = open_app(
    "tests/data/sample.json",
    "select(.ok) | {name: \"a string far too long to fit\"}"
  );
  if (!a) return;

  grid* narrow = grid_new(20, H);
  app_render(a, narrow);

  char buf[128];
  grid_line(narrow, TOP + 1, buf, sizeof(buf));
  ASSERT_STREQ(buf, "      name: \"a str\xe2\x80\xa6");
  ASSERT_EQ(grid_at(narrow, 18, TOP + 1).style, STYLE_STRING);
  grid_free(narrow);

  narrow = grid_new(9, H);
  app_render(a, narrow);
  grid_line(narrow, TOP + 1, buf, sizeof(buf));
  ASSERT_STREQ(buf, "      n\xe2\x80\xa6");
  grid_free(narrow);
  app_free(a);
}

static void test_path_quoting(void) {
  app* a = open_app("tests/data/sample.json", "select(.ok) | {\"a b\": [{\"x\": 1}]}");

  if (!a) return;

  press(a, '\t');
  type(a, "jjj");
  ASSERT_STREQ(status(a), ".\"a b\"[0].x");

  retype(a, "select(.ok) | [[1]]");
  ASSERT_STREQ(status(a), ".");
  press(a, 'j');
  ASSERT_STREQ(status(a), ".[0]");
  app_free(a);
}

static void test_view_cycling(void) {
  app* a = open_app("tests/data/sample.json", ".users[0]");

  if (!a) return;

  press(a, '\t');
  type(a, "jjj");
  ASSERT_STREQ(row(a, 3), "    ts: 1730300000");
  press(a, 'v');
  ASSERT_STREQ(row(a, 3), "    ts: 0x67224860  hex");
  press(a, 'v');
  ASSERT_STREQ(row(a, 3), "    ts: 0o14710444140  oct");
  press(a, 'V');
  press(a, 'V');
  ASSERT_STREQ(row(a, 3), "    ts: 1730300000");
  press(a, 'V');
  ASSERT_STREQ(row(a, 3), "    ts: 1m 30s ago  ago");
  type(a, "VVV");
  ASSERT_STREQ(row(a, 3), "    ts: 2024-10-30T14:53:20Z  utc");
  press(a, '0');
  ASSERT_STREQ(row(a, 3), "    ts: 1730300000");

  press(a, 'k');
  press(a, 'v');
  ASSERT_STREQ(row(a, 2), "    name: \"alice\"");
  press(a, 'k');
  press(a, 'v');
  ASSERT_STREQ(row(a, 1), "    id: 0x1  hex");
  ASSERT_STREQ(row(a, 3), "    ts: 1730300000");
  app_free(a);
}

static void test_view_kept_by_path(void) {
  app* a = open_app("tests/data/sample.json", ".users[0]");

  if (!a) return;

  press(a, '\t');
  type(a, "jjjv");
  ASSERT_STREQ(row(a, 3), "    ts: 0x67224860  hex");

  retype(a, ".users[0] + {a: 1}");
  ASSERT_STREQ(row(a, 3), "    ts: 0x67224860  hex");
  ASSERT_STREQ(row(a, 4), "    a: 1");

  retype(a, ".users[0] | .ts = \"soon\"");
  ASSERT_STREQ(row(a, 3), "    ts: \"soon\"");

  retype(a, ".users[0] | .ts += 1");
  ASSERT_STREQ(row(a, 3), "    ts: 0x67224861  hex");
  app_free(a);
}

static void test_view_keys(void) {
  app* a = open_app("tests/data/sample.json", ".users[0]");

  if (!a) return;

  press(a, '\t');
  type(a, "jjjx");
  ASSERT_STREQ(row(a, 3), "    ts: 0x67224860  hex");
  press(a, 'b');
  ASSERT_STREQ(row(a, 3), "    ts: 0b1100111001000100100100001100000  bin");
  press(a, 'b');
  ASSERT_STREQ(row(a, 3), "    ts: 1730300000");
  press(a, 's');
  ASSERT_STREQ(row(a, 3), "    ts: 1.61 GiB  iec");
  press(a, 's');
  ASSERT_STREQ(row(a, 3), "    ts: 1.73 GB  si");
  press(a, 's');
  ASSERT_STREQ(row(a, 3), "    ts: 1730300000");
  press(a, 'D');
  ASSERT_STREQ(row(a, 3), "    ts: 20026d 14h 53m 20s  dur");
  press(a, 'D');
  ASSERT_STREQ(row(a, 3), "    ts: 1730300000");
  press(a, 'o');
  press(a, 'e');
  ASSERT_STREQ(row(a, 3), "    ts: 1.7303e9  exp");
  press(a, '0');
  ASSERT_STREQ(row(a, 3), "    ts: 1730300000");

  retype(a, "{a: 1.5}");
  press(a, 'j');
  press(a, 'x');
  ASSERT_STREQ(row(a, 1), "    a: 1.5");
  press(a, 'e');
  ASSERT_STREQ(row(a, 1), "    a: 1.5e0  exp");
  press(a, 'x');
  ASSERT_STREQ(row(a, 1), "    a: 1.5e0  exp");
  press(a, 't');
  ASSERT_STREQ(row(a, 1), "    a: 1.5e0  exp");

  retype(a, "{a: 1730300000}");
  press(a, 't');
  ASSERT_STREQ(row(a, 1), "    a: 2024-10-30T14:53:20Z  utc");
  type(a, "tt");
  CHECK(strcmp(row(a, 1) + strlen(row(a, 1)) - 6, "  date") == 0);
  press(a, 't');
  ASSERT_STREQ(row(a, 1), "    a: 1m 30s ago  ago");
  press(a, 't');
  ASSERT_STREQ(row(a, 1), "    a: 1730300000");
  type(a, "xt");
  ASSERT_STREQ(row(a, 1), "    a: 2024-10-30T14:53:20Z  utc");
  press(a, 'd');
  ASSERT_STREQ(row(a, 1), "    a: 2024-10-30T14:53:20Z  utc");

  retype(a, "{a: \"68656c6c6f\"}");
  press(a, 'd');
  ASSERT_STREQ(row(a, 1), "    a: \"hello\"  unhex");
  press(a, 'd');
  ASSERT_STREQ(row(a, 1), "    a: \"68656c6c6f\"");

  retype(a, "{a: \"017f22e2-79b0-7cc3-98c4-dc0c0c07398f\"}");
  press(a, 'd');
  ASSERT_STREQ(row(a, 1), "    a: v7 2022-02-22T19:22:22.000Z  uuid");
  press(a, 't');
  ASSERT_STREQ(row(a, 1), "    a: 2022-02-22T19:22:22.000Z  utc");
  app_free(a);
}

static void test_json_subtree(void) {
  app* a = open_app(
    "tests/data/sample.json",
    "select(.ok) | {p: ({a: 1730300000, b: [2]} | tojson), q: 1}"
  );

  if (!a) return;

  press(a, '\t');
  type(a, "jd");
  ASSERT_STREQ(row(a, 1), "  \xe2\x96\xbe p: {2}  json");
  ASSERT_STREQ(row(a, 2), "      a: 1730300000");
  ASSERT_STREQ(row(a, 3), "    \xe2\x96\xbe b: [1]");
  ASSERT_STREQ(row(a, 4), "        0: 2");
  ASSERT_STREQ(row(a, 5), "    q: 1");
  type(a, "jj");
  ASSERT_STREQ(status(a), ".p | fromjson | .b");
  type(a, "kt");
  ASSERT_STREQ(row(a, 2), "      a: 2024-10-30T14:53:20Z  utc");
  ASSERT_STREQ(status(a), ".p | fromjson | .a");

  retype(a, "select(.ok) | {p: ({a: 1730300000, b: [3, 4]} | tojson), q: 1}");
  ASSERT_STREQ(row(a, 2), "      a: 2024-10-30T14:53:20Z  utc");
  ASSERT_STREQ(row(a, 5), "        1: 4");
  type(a, "hh");
  ASSERT_STREQ(row(a, 1), "  \xe2\x96\xb8 p: {2}  json");
  ASSERT_STREQ(row(a, 2), "    q: 1");
  press(a, '0');
  ASSERT_STREQ(row(a, 1), "    p: \"{\\\"a\\\":1730300000,\\\"b\\\":[3,4]}\"");
  ASSERT_STREQ(status(a), ".p");

  retype(a, "select(.ok) | [1] | tojson");
  press(a, 'd');
  ASSERT_STREQ(row(a, 0), "\xe2\x96\xbe [1]  json");
  press(a, 'j');
  ASSERT_STREQ(status(a), "fromjson | .[0]");
  app_free(a);
}

static void test_jwt_subtree(void) {
  app* a = open_app("tests/data/sample.json", "select(.ok) | {t: \"" TOKEN "\"}");

  if (!a) return;

  press(a, '\t');
  type(a, "jd");
  ASSERT_STREQ(row(a, 1), "  \xe2\x96\xbe t: {2}  jwt");
  ASSERT_STREQ(row(a, 2), "    \xe2\x96\xbe header: {2}");
  ASSERT_STREQ(row(a, 3), "        alg: \"HS256\"");
  ASSERT_STREQ(row(a, 6), "        sub: \"u>1?\"");
  CHECK(strcmp(row(a, 7) + strlen(row(a, 7)) - 6, "  date") == 0);
  CHECK(strcmp(row(a, 8) + strlen(row(a, 8)) - 6, "  date") == 0);
  ASSERT_STREQ(row(a, 9), "        n: \"\xc3\xa8\"");
  type(a, "jjjjjjjx");
  ASSERT_STREQ(row(a, 8), "        exp: 0x67225670  hex");
  press(a, '0');
  ASSERT_STREQ(row(a, 8), "        exp: 1730303600");
  press(a, 'k');
  CHECK(strcmp(row(a, 7) + strlen(row(a, 7)) - 6, "  date") == 0);
  app_free(a);
}

static void test_query_subtree(void) {
  app* a = open_app("tests/data/sample.json", "select(.ok) | {u: \"/s?q=a%20b&n=1&n=2\"}");

  if (!a) return;

  press(a, '\t');
  type(a, "jd");
  ASSERT_STREQ(row(a, 1), "    u: \"/s?q=a b&n=1&n=2\"  url");
  press(a, 'd');
  ASSERT_STREQ(row(a, 1), "  \xe2\x96\xbe u: {2}  query");
  ASSERT_STREQ(row(a, 2), "      q: \"a b\"");
  ASSERT_STREQ(row(a, 3), "    \xe2\x96\xbe n: [2]");
  ASSERT_STREQ(row(a, 5), "        1: \"2\"");
  press(a, 'd');
  ASSERT_STREQ(row(a, 1), "    u: \"/s?q=a%20b&n=1&n=2\"");
  app_free(a);
}

static void test_view_siblings(void) {
  app* a = open_app("tests/data/sample.json", "[.users[] | {id}]");

  if (!a) return;

  press(a, '\t');
  type(a, "jjxa");
  ASSERT_STREQ(row(a, 2), "      id: 0x1  hex");
  ASSERT_STREQ(row(a, 4), "      id: 0x2  hex");
  type(a, "jj0");
  ASSERT_STREQ(row(a, 2), "      id: 0x1  hex");
  ASSERT_STREQ(row(a, 4), "      id: 2");

  retype(a, "[.users[] | {id}] + [{id: 3, n: 4}]");
  ASSERT_STREQ(row(a, 4), "      id: 2");
  ASSERT_STREQ(row(a, 6), "      id: 0x3  hex");
  ASSERT_STREQ(row(a, 7), "      n: 4");

  type(a, "jjbkkoa");
  ASSERT_STREQ(row(a, 2), "      id: 0o1  oct");
  ASSERT_STREQ(row(a, 4), "      id: 0o2  oct");
  ASSERT_STREQ(row(a, 6), "      id: 0o3  oct");
  ASSERT_STREQ(row(a, 7), "      n: 4");
  app_free(a);
}

static void test_time_unit(void) {
  app* a = open_app("tests/data/sample.json", "{a: 1730300000, b: 1730300090000}");

  if (!a) return;

  press(a, '\t');
  type(a, "jxu");
  ASSERT_STREQ(row(a, 1), "    a: 0x67224860  hex");
  type(a, "tu");
  ASSERT_STREQ(row(a, 1), "    a: 2024-10-30T14:53:20Z  utc s");
  press(a, 'u');
  ASSERT_STREQ(row(a, 1), "    a: 1970-01-21T00:38:20.000Z  utc ms");
  press(a, 'u');
  ASSERT_STREQ(row(a, 1), "    a: 1970-01-01T00:28:50.300000Z  utc \xc2\xb5s");
  press(a, 'u');
  ASSERT_STREQ(row(a, 1), "    a: 1970-01-01T00:00:01.730300000Z  utc ns");
  press(a, 'u');
  ASSERT_STREQ(row(a, 1), "    a: 2024-10-30T14:53:20Z  utc");
  type(a, "uuu");
  type(a, "ttt");
  ASSERT_STREQ(row(a, 1), "    a: 20026d 14h ago  ago \xc2\xb5s");
  type(a, "0t");
  ASSERT_STREQ(row(a, 1), "    a: 2024-10-30T14:53:20Z  utc");

  type(a, "jtu");
  ASSERT_STREQ(row(a, 2), "    b: 2024-10-30T14:54:50.000Z  utc ms");

  retype(a, "{a: \"2024-10-30T14:53:20Z\"}");
  type(a, "tu");
  ASSERT_STREQ(row(a, 1), "    a: 2024-10-30T14:53:20Z  utc");

  retype(a, "{a: 1500}");
  type(a, "j0Du");
  ASSERT_STREQ(row(a, 1), "    a: 1.5s  dur ms");
  press(a, 'u');
  ASSERT_STREQ(row(a, 1), "    a: 1.5ms  dur \xc2\xb5s");
  press(a, 'u');
  ASSERT_STREQ(row(a, 1), "    a: 1.5\xc2\xb5s  dur ns");
  press(a, 'u');
  ASSERT_STREQ(row(a, 1), "    a: 25m  dur");

  retype(a, "[1730300000, 1730300090]");
  type(a, "jtua");
  ASSERT_STREQ(row(a, 1), "    0: 2024-10-30T14:53:20Z  utc s");
  ASSERT_STREQ(row(a, 2), "    1: 2024-10-30T14:54:50Z  utc s");
  app_free(a);
}

static void test_search(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, '\t');
  type(a, "/o");
  ASSERT_STREQ(status(a), "#0 .users[1].name");
  CHECK(strstr(cells(a, 0, H - 1, W), "/o") != NULL);
  CHECK(strstr(cells(a, 0, H - 1, W), "1/3") != NULL);

  press(a, APP_KEY_ENTER);
  CHECK(strstr(line(a, H - 2), "/o 1/3") != NULL);
  press(a, 'n');
  ASSERT_STREQ(status(a), "#0 .ok");
  press(a, 'n');
  ASSERT_STREQ(status(a), "#1 .second");
  CHECK(strstr(line(a, H - 2), "/o 3/3") != NULL);
  press(a, 'n');
  ASSERT_STREQ(status(a), "#0 .users[1].name");
  press(a, 'N');
  ASSERT_STREQ(status(a), "#1 .second");
  press(a, 'k');
  CHECK(strstr(line(a, H - 2), "/o -/3") != NULL);
  press(a, 'n');
  ASSERT_STREQ(status(a), "#1 .second");
  press(a, 'g');
  press(a, 'N');
  ASSERT_STREQ(status(a), "#1 .second");

  press(a, 0x1b);
  CHECK(strstr(line(a, H - 2), "/o") == NULL);
  press(a, 'n');
  ASSERT_STREQ(status(a), "#1 .second");

  press(a, 'g');
  type(a, "/O");
  ASSERT_STREQ(status(a), "#0 .");
  CHECK(strstr(cells(a, 0, H - 1, W), "no match") != NULL);
  press(a, APP_KEY_BACKSPACE);
  type(a, "Ok");
  CHECK(strstr(cells(a, 0, H - 1, W), "no match") != NULL);
  press(a, APP_KEY_BACKSPACE);
  press(a, APP_KEY_BACKSPACE);
  type(a, "oK");
  CHECK(strstr(cells(a, 0, H - 1, W), "no match") != NULL);
  press(a, 0x1b);
  ASSERT_STREQ(status(a), "#0 .");
  CHECK(strstr(line(a, H - 2), "no match") == NULL);
  app_free(a);
}

static void test_search_reveals(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, '\t');
  press(a, 'j');
  press(a, 'h');
  ASSERT_STREQ(row(a, 1), "  " CLOSED "users: [2]");

  type(a, "/o");
  ASSERT_STREQ(status(a), "#0 .users[1].name");
  ASSERT_STREQ(row(a, 1), "  " OPEN "users: [2]");
  type(a, "k");
  ASSERT_STREQ(status(a), "#0 .ok");
  ASSERT_STREQ(row(a, 1), "  " CLOSED "users: [2]");

  press(a, APP_KEY_BACKSPACE);
  ASSERT_STREQ(status(a), "#0 .users[1].name");
  press(a, 0x1b);
  ASSERT_STREQ(status(a), "#0 .users");
  ASSERT_STREQ(row(a, 1), "  " CLOSED "users: [2]");

  type(a, "/bob");
  press(a, APP_KEY_ENTER);
  ASSERT_STREQ(status(a), "#0 .users[1].name");
  press(a, 'g');
  press(a, 'j');
  ASSERT_STREQ(row(a, 1), "  " OPEN "users: [2]");

  press(a, 'h');
  type(a, "/al");
  press(a, APP_KEY_ENTER);
  type(a, "/zz");
  press(a, 0x1b);
  ASSERT_STREQ(status(a), "#0 .users[0].name");
  CHECK(strstr(line(a, H - 2), "/al 1/1") != NULL);
  app_free(a);
}

static void test_search_values(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, '\t');
  type(a, "/0300");
  ASSERT_STREQ(status(a), "#0 .users[0].ts");
  press(a, APP_KEY_ENTER);
  type(a, "/tru");
  ASSERT_STREQ(status(a), "#0 .ok");
  press(a, APP_KEY_ENTER);
  type(a, "/ull");
  ASSERT_STREQ(status(a), "#1 .second");
  press(a, APP_KEY_ENTER);
  type(a, "/\"");
  CHECK(strstr(cells(a, 0, H - 1, W), "no match") != NULL);
  press(a, 0x1b);

  retype(a, ".users[0]");
  type(a, "/name");
  ASSERT_STREQ(status(a), "#0 .name");
  press(a, APP_KEY_ENTER);
  retype(a, ".users");
  CHECK(strstr(line(a, H - 2), "/name -/2") != NULL);
  press(a, 'n');
  ASSERT_STREQ(status(a), "#0 .[0].name");
  app_free(a);
}

static void test_search_marks(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, '\t');
  type(a, "/am");
  ASSERT_STREQ(row(a, 4), "        name: \"alice\"");
  ASSERT_EQ(style_at(a, 8, 4), STYLE_KEY);
  ASSERT_EQ(style_at(a, 9, 4), STYLE_MATCH);
  ASSERT_EQ(style_at(a, 10, 4), STYLE_MATCH);
  ASSERT_EQ(style_at(a, 11, 4), STYLE_KEY);
  ASSERT_EQ(style_at(a, 9, 8), STYLE_MATCH);

  press(a, APP_KEY_BACKSPACE);
  press(a, APP_KEY_BACKSPACE);
  type(a, "b");
  press(a, APP_KEY_ENTER);
  ASSERT_STREQ(row(a, 8), "        name: \"bob\"");
  ASSERT_EQ(style_at(a, 14, 8), STYLE_STRING);
  ASSERT_EQ(style_at(a, 15, 8), STYLE_MATCH);
  ASSERT_EQ(style_at(a, 16, 8), STYLE_STRING);
  ASSERT_EQ(style_at(a, 17, 8), STYLE_MATCH);
  ASSERT_EQ(style_at(a, 18, 8), STYLE_STRING);
  CHECK(selected(a, 8));

  type(a, "/0300");
  press(a, APP_KEY_ENTER);
  ASSERT_STREQ(row(a, 5), "        ts: 1730300000");
  ASSERT_EQ(style_at(a, 15, 5), STYLE_MATCH);
  press(a, 'x');
  ASSERT_EQ(style_at(a, 15, 5), STYLE_NUMBER);
  press(a, 'x');

  press(a, 0x1b);
  ASSERT_EQ(style_at(a, 15, 8), STYLE_STRING);
  app_free(a);
}

static void test_copy_value(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, '\t');
  type(a, "jjjj");
  press(a, 'y');
  CHECK(copied(a, "\"alice\""));
  CHECK(strstr(line(a, H - 2), "copied 7 B") != NULL);
  CHECK(app_take_clipboard(a) == NULL);

  press(a, 'k');
  CHECK(strstr(line(a, H - 2), "copied") == NULL);
  CHECK(app_take_clipboard(a) == NULL);
  press(a, 'k');
  press(a, 'y');
  CHECK(copied(a, "{\n  \"id\": 1,\n  \"name\": \"alice\",\n  \"ts\": 1730300000\n}"));

  retype(a, "1.000");
  press(a, 'y');
  CHECK(copied(a, "1.000"));
  app_free(a);
}

#define TESTS(f)                          \
   f(test_focus)                          \
   f(test_click_focuses)                  \
   f(test_click_picks_lines)              \
   f(test_wheel_scrolls)                  \
   f(test_quit_keys)                      \
   f(test_navigation)                     \
   f(test_fold_and_unfold)                \
   f(test_fold_subtree)                   \
   f(test_page_up_and_down)               \
   f(test_query_edit_keeps_state)         \
   f(test_cursor_falls_back_to_ancestor)  \
   f(test_reload_keeps_state)             \
   f(test_reload_keeps_views)             \
   f(test_watch_reloads)                  \
   f(test_watch_keeps_last_good)          \
   f(test_watch_missing_file)             \
   f(test_watch_times)                    \
   f(test_default_expansion)              \
   f(test_single_root_unlabeled)          \
   f(test_truncation)                     \
   f(test_path_quoting)                   \
   f(test_view_cycling)                   \
   f(test_view_kept_by_path)              \
   f(test_view_keys)                      \
   f(test_view_siblings)                  \
   f(test_json_subtree)                   \
   f(test_jwt_subtree)                    \
   f(test_query_subtree)                  \
   f(test_time_unit)                      \
   f(test_search)                         \
   f(test_search_reveals)                 \
   f(test_search_values)                  \
   f(test_search_marks)                   \
   f(test_copy_value)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
