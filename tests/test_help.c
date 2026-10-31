/* help tests - the overlay through the core. */
#include <string.h>

#include "app.h"
#include "defines.h"

#include "test_helper.in"

#define W         80
#define H         24
#define SETTLE_MS 5000

static app* open_app(const char* path);
static void press(app* a, u32 key);
static b8 shows(app* a, const char* s);
static void test_help_in_tree(void);
static void test_help_in_query(void);
static void test_help_clipped(void);

static grid* screen;

static app* open_app(const char* path) {
  app_opts o;
  char err[256];
  app* a;

  memset(&o, 0, sizeof(o));
  o.path = path;
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

/* true when some line of the screen holds s. */
static b8 shows(app* a, const char* s) {
  char buf[W * 4 + 1];

  app_render(a, screen);
  for (i32 y = 0; y < screen->h; y++) {
    grid_line(screen, y, buf, sizeof(buf));
    if (strstr(buf, s)) return 1;
  }

  return 0;
}

static void test_help_in_tree(void) {
  app* a = open_app("tests/data/sample.json");

  if (!a) return;

  screen = grid_new(W, H);
  press(a, '\t');
  CHECK(!shows(a, "tree keys"));
  press(a, '?');
  CHECK(shows(a, "tree keys"));
  CHECK(shows(a, "fold / unfold subtree"));
  CHECK(!shows(a, "delete word"));
  ASSERT_EQ(screen->cursor_x, -1);
  press(a, 'j');
  CHECK(!shows(a, "tree keys"));
  ASSERT_EQ(app_focused(a), FOCUS_TREE);
  CHECK(shows(a, "QUERY ."));
  grid_free(screen);
  app_free(a);
}

static void test_help_in_query(void) {
  app* a = open_app("tests/data/sample.json");

  if (!a) return;

  screen = grid_new(W, H);
  press(a, '?');
  CHECK(!shows(a, "query keys"));
  CHECK(shows(a, "QUERY .?"));
  press(a, APP_KEY_F1);
  CHECK(shows(a, "query keys"));
  CHECK(shows(a, "delete word"));
  press(a, 'x');
  CHECK(!shows(a, "query keys"));
  CHECK(shows(a, "QUERY .?"));
  ASSERT_EQ(screen->cursor_x, 8);
  grid_free(screen);
  app_free(a);
}

static void test_help_clipped(void) {
  app* a = open_app("tests/data/sample.json");

  if (!a) return;

  screen = grid_new(20, 6);
  press(a, APP_KEY_F1);
  CHECK(shows(a, "query keys"));
  CHECK(!shows(a, "any key closes"));
  grid_free(screen);
  app_free(a);
}

#define TESTS(f)              \
  f(test_help_in_tree)        \
  f(test_help_in_query)       \
  f(test_help_clipped)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
