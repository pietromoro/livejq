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
static void test_help_scrolls(void);
static i32 thumb_at(app* a);

static grid* screen;

static app* open_app(const char* path) {
  app_opts o;

  memset(&o, 0, sizeof(o));
  o.path = path;
  o.args = jv_invalid();

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
  ev.now_ms = 0;
  app_feed(a, ev);
}

/* true when some line of the screen holds s. */
static b8 shows(app* a, const char* s) {
  app_render(a, screen);
  for (i32 y = 0; y < screen->h; y++) {
    char buf[W * 4 + 1];

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
  CHECK(shows(a, "any key closes"));
  press(a, 'j');
  CHECK(shows(a, "tree keys"));
  press(a, 'x');
  CHECK(!shows(a, "tree keys"));
  ASSERT_EQ(app_focused(a), FOCUS_TREE);
  CHECK(shows(a, "\xe2\x9d\xaf ."));
  grid_free(screen);
  app_free(a);
}

static void test_help_in_query(void) {
  app* a = open_app("tests/data/sample.json");

  if (!a) return;

  screen = grid_new(W, H);
  press(a, '?');
  CHECK(!shows(a, "query keys"));
  CHECK(shows(a, "\xe2\x9d\xaf .?"));
  press(a, APP_KEY_F1);
  CHECK(shows(a, "query keys"));
  CHECK(shows(a, "delete word"));
  press(a, 'x');
  CHECK(!shows(a, "query keys"));
  CHECK(shows(a, "\xe2\x9d\xaf .?"));
  ASSERT_EQ(screen->cursor_x, 6);
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

/* the first row of the scrollbar's thick stretch, below its up arrow; -1
 * when there is none. */
static i32 thumb_at(app* a) {
  app_render(a, screen);
  for (i32 y = 0; y < screen->h; y++) {
    for (i32 x = 0; x < screen->w; x++) {
      if (grid_at(screen, x, y).ch != 0x25b2) continue;

      for (i32 i = y + 1; i < screen->h; i++)
        if (grid_at(screen, x, i).ch == 0x2588) return i;

      return -1;
    }
  }

  return -1;
}

static void test_help_scrolls(void) {
  app* a = open_app("tests/data/sample.json");
  app_event wheel = { 0 };

  if (!a) return;

  screen = grid_new(W, 12);
  press(a, '\t');
  press(a, '?');
  CHECK(shows(a, "\xe2\x86\x91\xe2\x86\x93 scroll"));
  CHECK(shows(a, "move"));
  CHECK(!shows(a, "quit"));
  ASSERT_EQ(thumb_at(a), 2);
  press(a, 'j');
  CHECK(!shows(a, "j k"));
  press(a, 'G');
  CHECK(shows(a, "quit"));
  ASSERT_EQ(thumb_at(a), 6);
  press(a, APP_KEY_PAGE_UP);
  CHECK(!shows(a, "quit"));
  press(a, 'g');
  CHECK(shows(a, "j k"));
  wheel.kind = EV_MOUSE;
  wheel.mouse = APP_MOUSE_WHEEL_DOWN;
  CHECK(app_feed(a, wheel));
  CHECK(!shows(a, "j k"));
  press(a, 'q');
  CHECK(!shows(a, "tree keys"));
  CHECK(!app_quitting(a));
  press(a, '?');
  CHECK(shows(a, "j k"));
  grid_free(screen);
  app_free(a);
}

#define TESTS(f)          \
   f(test_help_in_tree)   \
   f(test_help_in_query)  \
   f(test_help_clipped)   \
   f(test_help_scrolls)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
