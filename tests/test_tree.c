/* tree tests - navigation, folds and state kept by path, through the core. */
#include <string.h>

#include "app.h"
#include "defines.h"
#include "thread.h"

#include "test_helper.in"

#define W         60
#define H         12
#define SETTLE_MS 5000
#define OPEN      "\xe2\x96\xbe "
#define CLOSED    "\xe2\x96\xb8 "

static app* open_app(const char* path, const char* query);
static void press(app* a, u32 key);
static void type(app* a, const char* s);
static void retype(app* a, const char* s);
static const char* line(app* a, i32 y);
static const char* status(app* a);
static b8 selected(app* a, i32 y);
static void test_focus(void);
static void test_navigation(void);
static void test_fold_and_unfold(void);
static void test_fold_subtree(void);
static void test_page_up_and_down(void);
static void test_default_expansion(void);
static void test_single_root_unlabeled(void);
static void test_truncation(void);
static void test_path_quoting(void);

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
  app_event ev;

  ev.kind = EV_KEY;
  ev.key = key;
  ev.now_ms = 0;
  app_feed(a, ev);
}

static void type(app* a, const char* s) {
  app_event ev;

  ev.kind = EV_KEY;
  ev.now_ms = 0;
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

static const char* line(app* a, i32 y) {
  static char buf[W * 4 + 1];

  if (!screen) screen = grid_new(W, H);
  app_render(a, screen);
  grid_line(screen, y, buf, sizeof(buf));
  return buf;
}

/* the right side of the status line. */
static const char* status(app* a) {
  const char* s = line(a, H - 1);
  const char* gap = strstr(s, "  ");

  while (gap && *gap == ' ') gap++;

  return gap ? gap : "";
}

static b8 selected(app* a, i32 y) {
  line(a, y);
  return (grid_at(screen, W - 1, y).style & STYLE_SELECTED) != 0;
}

static void test_focus(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  ASSERT_EQ(app_focused(a), FOCUS_QUERY);
  press(a, '\t');
  ASSERT_EQ(app_focused(a), FOCUS_TREE);
  type(a, "jx");
  ASSERT_STREQ(line(a, 0), "QUERY .");
  press(a, APP_KEY_BACKTAB);
  ASSERT_EQ(app_focused(a), FOCUS_QUERY);
  app_free(a);
}

static void test_navigation(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, '\t');
  ASSERT_STREQ(line(a, 1), OPEN "#0 {2}");
  ASSERT_STREQ(line(a, 2), "  " OPEN "users: [2]");
  ASSERT_STREQ(line(a, 3), "    " OPEN "0: {3}");
  ASSERT_STREQ(line(a, 4), "        id: 1");
  ASSERT_STREQ(line(a, 5), "        name: \"alice\"");
  CHECK(selected(a, 1));
  ASSERT_STREQ(status(a), "#0 .");

  press(a, 'k');
  CHECK(selected(a, 1));
  press(a, 'j');
  press(a, APP_KEY_DOWN);
  press(a, 'j');
  CHECK(selected(a, 4));
  CHECK(!selected(a, 1));
  ASSERT_STREQ(status(a), "#0 .users[0].id");

  press(a, APP_KEY_UP);
  ASSERT_STREQ(status(a), "#0 .users[0]");

  press(a, 'G');
  ASSERT_STREQ(status(a), "#1 .second");
  ASSERT_STREQ(line(a, H - 2), "    second: null");
  CHECK(selected(a, H - 2));
  ASSERT_STREQ(line(a, H - 3), OPEN "#1 {1}");

  press(a, APP_KEY_HOME);
  ASSERT_STREQ(line(a, 1), OPEN "#0 {2}");
  CHECK(selected(a, 1));
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
  ASSERT_STREQ(line(a, 2), "  " CLOSED "users: [2]");
  ASSERT_STREQ(line(a, 3), "    ok: true");
  CHECK(selected(a, 2));

  press(a, 'h');
  ASSERT_STREQ(status(a), "#0 .");
  press(a, 'j');
  press(a, APP_KEY_RIGHT);
  ASSERT_STREQ(line(a, 2), "  " OPEN "users: [2]");
  ASSERT_STREQ(line(a, 3), "    " OPEN "0: {3}");

  press(a, ' ');
  ASSERT_STREQ(line(a, 3), "    ok: true");
  press(a, ' ');
  ASSERT_STREQ(line(a, 3), "    " OPEN "0: {3}");

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
  ASSERT_STREQ(line(a, 1), CLOSED "#0 {2}");
  ASSERT_STREQ(line(a, 2), OPEN "#1 {1}");

  press(a, 'l');
  ASSERT_STREQ(line(a, 2), "  " CLOSED "users: [2]");
  ASSERT_STREQ(line(a, 3), "    ok: true");

  press(a, 'L');
  ASSERT_STREQ(line(a, 2), "  " OPEN "users: [2]");
  ASSERT_STREQ(line(a, 3), "    " OPEN "0: {3}");
  ASSERT_STREQ(status(a), "#0 .");

  press(a, 'j');
  press(a, 'H');
  press(a, 'l');
  ASSERT_STREQ(line(a, 3), "    " CLOSED "0: {3}");
  ASSERT_STREQ(line(a, 4), "    " CLOSED "1: {3}");
  app_free(a);
}

static void test_page_up_and_down(void) {
  app* a = open_app("tests/data/sample.json", "select(.ok) | [range(30)]");

  if (!a) return;

  press(a, '\t');
  ASSERT_STREQ(line(a, 1), OPEN "[30]");
  press(a, APP_KEY_PAGE_DOWN);
  ASSERT_STREQ(status(a), ".[9]");
  ASSERT_STREQ(line(a, 1), "    9: 9");
  CHECK(selected(a, 1));

  press(a, APP_KEY_PAGE_DOWN);
  press(a, APP_KEY_PAGE_DOWN);
  ASSERT_STREQ(status(a), ".[29]");
  ASSERT_STREQ(line(a, H - 2), "    29: 29");
  CHECK(selected(a, H - 2));

  press(a, APP_KEY_PAGE_DOWN);
  ASSERT_STREQ(status(a), ".[29]");

  press(a, APP_KEY_PAGE_UP);
  ASSERT_STREQ(status(a), ".[19]");
  press(a, APP_KEY_PAGE_UP);
  press(a, APP_KEY_PAGE_UP);
  press(a, APP_KEY_PAGE_UP);
  ASSERT_STREQ(status(a), ".");
  ASSERT_STREQ(line(a, 1), OPEN "[30]");
  app_free(a);
}

static void test_default_expansion(void) {
  app* a = open_app("tests/data/sample.json", "select(.ok) | {a: [range(2000)], b: [1]}");

  if (!a) return;

  ASSERT_STREQ(line(a, 1), OPEN "{2}");
  ASSERT_STREQ(line(a, 2), "  " CLOSED "a: [2000]");
  ASSERT_STREQ(line(a, 3), "  " CLOSED "b: [1]");

  press(a, '\t');
  retype(a, "select(.ok) | [range(600)], [range(600)]");
  ASSERT_STREQ(line(a, 1), CLOSED "#0 [600]");
  ASSERT_STREQ(line(a, 2), CLOSED "#1 [600]");

  retype(a, "select(.ok) | [range(5)]");
  ASSERT_STREQ(line(a, 2), "    0: 0");
  app_free(a);
}

static void test_single_root_unlabeled(void) {
  app* a = open_app("tests/data/sample.json", "select(.ok) | .users[1]");

  if (!a) return;

  ASSERT_STREQ(line(a, 1), OPEN "{3}");
  ASSERT_STREQ(line(a, 4), "  " OPEN "tags: [1]");
  ASSERT_STREQ(line(a, 5), "      0: \"x\"");
  CHECK(strstr(line(a, H - 1), "1 outputs") != NULL);
  app_free(a);
}

static void test_truncation(void) {
  app* a = open_app("tests/data/sample.json", "select(.ok) | {name: \"a string far too long to fit\"}");
  if (!a) return;

  grid* narrow = grid_new(20, H);
  char buf[128];

  app_render(a, narrow);
  grid_line(narrow, 2, buf, sizeof(buf));
  ASSERT_STREQ(buf, "    name: \"a string\xe2\x80\xa6");
  ASSERT_EQ(grid_at(narrow, 19, 2).style, STYLE_STRING);
  grid_free(narrow);

  narrow = grid_new(7, H);
  app_render(a, narrow);
  grid_line(narrow, 2, buf, sizeof(buf));
  ASSERT_STREQ(buf, "    na\xe2\x80\xa6");
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

#define TESTS(f)                          \
  f(test_focus)                           \
  f(test_navigation)                      \
  f(test_fold_and_unfold)                 \
  f(test_fold_subtree)                    \
  f(test_page_up_and_down)                \
  f(test_default_expansion)               \
  f(test_single_root_unlabeled)           \
  f(test_truncation)                      \
  f(test_path_quoting)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
