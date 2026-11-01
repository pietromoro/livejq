/* tree tests - navigation, folds and state kept by path, through the core. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "defines.h"
#include "thread.h"

#include "test_helper.in"

#define W         60
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
static void press(app* a, u32 key);
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
static void test_default_expansion(void);
static void test_single_root_unlabeled(void);
static void test_truncation(void);
static void test_path_quoting(void);
static void test_view_cycling(void);
static void test_view_kept_by_path(void);
static void test_view_keys(void);
static void test_view_siblings(void);

static grid* screen;

static app* open_app(const char* path, const char* query) {
  app_opts o;
  char err[256];
  app* a;

  memset(&o, 0, sizeof(o));
  o.path = path;
  o.query = query;
  o.args = jv_invalid();
  a = app_new(&o, err, sizeof(err));
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
  app* a = open_app("tests/data/sample.json", "select(.ok) | {name: \"a string far too long to fit\"}");
  if (!a) return;

  grid* narrow = grid_new(20, H);
  char buf[128];

  app_render(a, narrow);
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

#define TESTS(f)                          \
  f(test_focus)                           \
  f(test_click_focuses)                   \
  f(test_click_picks_lines)               \
  f(test_wheel_scrolls)                   \
  f(test_quit_keys)                       \
  f(test_navigation)                      \
  f(test_fold_and_unfold)                 \
  f(test_fold_subtree)                    \
  f(test_page_up_and_down)                \
  f(test_query_edit_keeps_state)          \
  f(test_cursor_falls_back_to_ancestor)   \
  f(test_reload_keeps_state)              \
  f(test_default_expansion)               \
  f(test_single_root_unlabeled)           \
  f(test_truncation)                      \
  f(test_path_quoting)                    \
  f(test_view_cycling)                    \
  f(test_view_kept_by_path)               \
  f(test_view_keys)                       \
  f(test_view_siblings)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
