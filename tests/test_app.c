/* app tests - loading, evaluation and cancellation through the core. */
#include <string.h>

#include "app.h"
#include "defines.h"
#include "thread.h"

#include "test_helper.in"

#define W         80
#define H         12
#define SETTLE_MS 5000

static app* open_app(const char* path, const char* query, b8 raw, b8 slurp, jv args, usz cap);
static void type(app* a, const char* s, i64 now);
static void retype(app* a, const char* s);
static const char* line(app* a, i32 y);
static u8 style_at(app* a, i32 x, i32 y);
static void test_multi_document(void);
static void test_raw_lines(void);
static void test_raw_slurp(void);
static void test_slurp(void);
static void test_args(void);
static void test_compile_error_keeps_last_good(void);
static void test_runtime_error_keeps_partial_outputs(void);
static void test_empty_output(void);
static void test_output_cap(void);
static void test_debounce(void);
static void test_cancel_on_edit(void);
static void test_invalid_json_opens(void);
static void test_missing_file(void);

static grid* screen;

static app* open_app(const char* path, const char* query, b8 raw, b8 slurp, jv args, usz cap) {
  app_opts o;
  char err[256];
  app* a;

  o.path = path;
  o.query = query;
  o.raw = raw;
  o.slurp = slurp;
  o.args = args;
  o.cap = cap;
  a = app_new(&o, err, sizeof(err));
  if (!a) FAIL("app_new(%s): %s", path, err);
  else CHECK(app_settle(a, SETTLE_MS));

  return a;
}

static void type(app* a, const char* s, i64 now) {
  app_event ev;

  ev.kind = EV_KEY;
  ev.now_ms = now;
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

  type(a, s, 0);
}

static const char* line(app* a, i32 y) {
  static char buf[W * 4 + 1];

  if (!screen) screen = grid_new(W, H);
  app_render(a, screen);
  grid_line(screen, y, buf, sizeof(buf));
  return buf;
}

static u8 style_at(app* a, i32 x, i32 y) {
  line(a, y);
  return grid_at(screen, x, y).style & ~STYLE_SELECTED;
}

static void test_multi_document(void) {
  app* a = open_app("tests/data/sample.json", "keys", 0, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(line(a, 0), "QUERY keys");
  ASSERT_STREQ(line(a, 1), "\xe2\x96\xbe #0 [2]");
  ASSERT_STREQ(line(a, 2), "    0: \"ok\"");
  ASSERT_STREQ(line(a, 3), "    1: \"users\"");
  ASSERT_STREQ(line(a, 4), "\xe2\x96\xbe #1 [1]");
  ASSERT_STREQ(line(a, 5), "    0: \"second\"");
  ASSERT_STREQ(line(a, 6), "");
  CHECK(strstr(line(a, H - 1), "sample.json \xc2\xb7 2 outputs") != NULL);
  app_free(a);
}

static void test_raw_lines(void) {
  app* a = open_app("tests/data/lines.txt", "length", 1, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(line(a, 1), "  #0 10");
  ASSERT_STREQ(line(a, 2), "  #1 11");
  ASSERT_STREQ(line(a, 3), "  #2 0");
  ASSERT_STREQ(line(a, 4), "  #3 6");
  ASSERT_STREQ(line(a, 5), "");
  app_free(a);
}

static void test_raw_slurp(void) {
  app* a = open_app("tests/data/lines.txt", ".", 1, 1, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(line(a, 1), "  \"first line\\nsecond line\\n\\nfourth\"");
  ASSERT_STREQ(line(a, 2), "");
  app_free(a);
}

static void test_slurp(void) {
  app* a = open_app("tests/data/sample.json", "map(keys[0])", 0, 1, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(line(a, 1), "\xe2\x96\xbe [2]");
  ASSERT_STREQ(line(a, 2), "    0: \"ok\"");
  ASSERT_STREQ(line(a, 3), "    1: \"second\"");
  CHECK(strstr(line(a, H - 1), "1 outputs") != NULL);
  app_free(a);
}

static void test_args(void) {
  jv args = jv_object_set(jv_object(), jv_string("name"), jv_string("x"));
  app* a;

  args = jv_object_set(args, jv_string("n"), jv_parse("[3]"));
  a = open_app("tests/data/sample.json", "[$name, $n]", 0, 0, args, 0);
  if (!a) return;

  ASSERT_STREQ(line(a, 2), "    0: \"x\"");
  ASSERT_STREQ(line(a, 3), "  \xe2\x96\xbe 1: [1]");
  ASSERT_STREQ(line(a, 4), "      0: 3");
  ASSERT_STREQ(line(a, 5), "\xe2\x96\xbe #1 [2]");
  app_free(a);
}

static void test_compile_error_keeps_last_good(void) {
  app* a = open_app("tests/data/sample.json", ".ok", 0, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(line(a, 1), "  #0 true");
  ASSERT_EQ(style_at(a, 5, 1), STYLE_TRUE);

  type(a, " | [", 0);
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(line(a, 0), "QUERY .ok | [");
  ASSERT_STREQ(line(a, 1), "  #0 true");
  ASSERT_EQ(style_at(a, 5, 1), STYLE_DIM);
  CHECK(strstr(line(a, H - 1), "syntax error") != NULL);

  retype(a, ".ok");
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_EQ(style_at(a, 5, 1), STYLE_TRUE);
  CHECK(strstr(line(a, H - 1), "error") == NULL);
  app_free(a);
}

static void test_runtime_error_keeps_partial_outputs(void) {
  app* a = open_app("tests/data/sample.json", "1, error(\"boom\"), 3", 0, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(line(a, 1), "  #0 1");
  ASSERT_STREQ(line(a, 2), "  #1 1");
  ASSERT_STREQ(line(a, 3), "");
  CHECK(strstr(line(a, H - 1), "2 outputs") != NULL);
  CHECK(strstr(line(a, H - 1), "boom") != NULL);
  ASSERT_EQ(style_at(a, W - 1, H - 1), STYLE_ERROR);
  app_free(a);
}

static void test_empty_output(void) {
  app* a = open_app("tests/data/sample.json", "empty", 0, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(line(a, 1), "(no output)");
  CHECK(strstr(line(a, H - 1), "0 outputs") != NULL);
  app_free(a);
}

static void test_output_cap(void) {
  app* a = open_app("tests/data/sample.json", "range(100)", 0, 0, jv_invalid(), 3);

  if (!a) return;

  ASSERT_STREQ(line(a, 1), "  #0 0");
  ASSERT_STREQ(line(a, 3), "  #2 2");
  ASSERT_STREQ(line(a, 4), "");
  CHECK(strstr(line(a, H - 1), "3 outputs (capped)") != NULL);
  app_free(a);
}

static void test_debounce(void) {
  app* a = open_app("tests/data/sample.json", ".", 0, 0, jv_invalid(), 0);
  app_event tick;

  if (!a) return;

  retype(a, ".ok");
  tick.kind = EV_TICK;
  tick.key = 0;
  tick.now_ms = 149;
  app_feed(a, tick);
  ASSERT_STREQ(line(a, 1), "\xe2\x96\xbe #0 {2}");

  tick.now_ms = 150;
  app_feed(a, tick);
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(line(a, 1), "  #0 true");
  app_free(a);
}

static void test_cancel_on_edit(void) {
  app* a = open_app("tests/data/sample.json", "1", 0, 0, jv_invalid(), 0);
  i64 start;

  if (!a) return;

  retype(a, "last(range(1e18))");
  CHECK(!app_settle(a, 30));

  retype(a, "2");
  start = clock_us();
  CHECK(app_settle(a, SETTLE_MS));
  CHECK(clock_us() - start < 50000);
  ASSERT_STREQ(line(a, 1), "  #0 2");
  app_free(a);
}

static void test_invalid_json_opens(void) {
  app* a = open_app("tests/data/broken.json", ".", 0, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(line(a, 1), "\xe2\x96\xbe {1}");
  ASSERT_STREQ(line(a, 2), "    a: 1");
  CHECK(strstr(line(a, H - 1), "Unfinished JSON term") != NULL);
  app_free(a);
}

static void test_missing_file(void) {
  app_opts o;
  char err[256];

  memset(&o, 0, sizeof(o));
  o.path = "tests/data/nope.json";
  o.args = jv_invalid();
  ASSERT_NULL(app_new(&o, err, sizeof(err)));
  CHECK(strstr(err, "tests/data/nope.json: ") == err);
}

#define TESTS(f)                               \
  f(test_multi_document)                       \
  f(test_raw_lines)                            \
  f(test_raw_slurp)                            \
  f(test_slurp)                                \
  f(test_args)                                 \
  f(test_compile_error_keeps_last_good)        \
  f(test_runtime_error_keeps_partial_outputs)  \
  f(test_empty_output)                         \
  f(test_output_cap)                           \
  f(test_debounce)                             \
  f(test_cancel_on_edit)                       \
  f(test_invalid_json_opens)                   \
  f(test_missing_file)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
