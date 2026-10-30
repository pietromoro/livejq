/* tree tests - the outputs drawn as a tree, through the core. */
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
static void type(app* a, const char* s);
static void retype(app* a, const char* s);
static const char* line(app* a, i32 y);
static void test_default_expansion(void);
static void test_single_root_unlabeled(void);
static void test_truncation(void);

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

static void type(app* a, const char* s) {
  app_event ev;

  ev.kind = EV_KEY;
  ev.now_ms = 0;
  while (*s) {
    ev.key = (u8)*s++;
    app_feed(a, ev);
  }
}

static void retype(app* a, const char* s) {
  app_event ev;

  ev.kind = EV_KEY;
  ev.key = APP_KEY_BACKSPACE;
  ev.now_ms = 0;
  for (i32 i = 0; i < 64; i++) app_feed(a, ev);

  type(a, s);
  CHECK(app_settle(a, SETTLE_MS));
}

static const char* line(app* a, i32 y) {
  static char buf[W * 4 + 1];

  if (!screen) screen = grid_new(W, H);
  app_render(a, screen);
  grid_line(screen, y, buf, sizeof(buf));
  return buf;
}

static void test_default_expansion(void) {
  app* a = open_app("tests/data/sample.json", "select(.ok) | {a: [range(2000)], b: [1]}");

  if (!a) return;

  ASSERT_STREQ(line(a, 1), OPEN "{2}");
  ASSERT_STREQ(line(a, 2), "  " CLOSED "a: [2000]");
  ASSERT_STREQ(line(a, 3), "  " CLOSED "b: [1]");

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

#define TESTS(f)                          \
  f(test_default_expansion)               \
  f(test_single_root_unlabeled)           \
  f(test_truncation)                      \

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
