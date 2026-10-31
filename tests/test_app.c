/* app tests - loading, evaluation and cancellation through the core. */
#include <string.h>

#include "app.h"
#include "defines.h"
#include "thread.h"

#include "test_helper.in"

#define W         80
#define H         16
#define TOP       4 /* the tree's first row */
#define LEFT      2 /* and column */
#define QUERY_X   4 /* the query's first column, after the prompt */
#define SETTLE_MS 5000

static app* open_app(const char* path, const char* query, b8 raw, b8 slurp, jv args, usz cap);
static void type(app* a, const char* s, i64 now);
static void retype(app* a, const char* s);
static const char* line(app* a, i32 y);
static const char* cells(app* a, i32 x, i32 y, i32 max_x);
static const char* row(app* a, i32 n);
static const char* query(app* a);
static u8 style_at(app* a, i32 x, i32 y);
static void test_multi_document(void);
static void test_raw_lines(void);
static void test_raw_slurp(void);
static void test_slurp(void);
static void test_args(void);
static void test_compile_error_keeps_last_good(void);
static void test_runtime_error_keeps_partial_outputs(void);
static void test_empty_output(void);
static void test_query_highlighted(void);
static void test_output_cap(void);
static void test_debounce(void);
static void test_changed_only_when_shown(void);
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

static u8 style_at(app* a, i32 x, i32 y) {
  line(a, y);
  return grid_at(screen, x, y).style & ~(STYLE_SELECTED | STYLE_SELECTED_IDLE);
}

static void test_multi_document(void) {
  app* a = open_app("tests/data/sample.json", "keys", 0, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(query(a), "keys");
  ASSERT_STREQ(row(a, 0), "\xe2\x96\xbe #0 [2]");
  ASSERT_STREQ(row(a, 1), "    0: \"ok\"");
  ASSERT_STREQ(row(a, 2), "    1: \"users\"");
  ASSERT_STREQ(row(a, 3), "\xe2\x96\xbe #1 [1]");
  ASSERT_STREQ(row(a, 4), "    0: \"second\"");
  ASSERT_STREQ(row(a, 5), "");
  CHECK(strstr(line(a, H - 2), "sample.json  #0 .   ") != NULL);
  app_free(a);
}

static void test_raw_lines(void) {
  app* a = open_app("tests/data/lines.txt", "length", 1, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(row(a, 0), "  #0 10");
  ASSERT_STREQ(row(a, 1), "  #1 11");
  ASSERT_STREQ(row(a, 2), "  #2 0");
  ASSERT_STREQ(row(a, 3), "  #3 6");
  ASSERT_STREQ(row(a, 4), "");
  app_free(a);
}

static void test_raw_slurp(void) {
  app* a = open_app("tests/data/lines.txt", ".", 1, 1, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(row(a, 0), "  \"first line\\nsecond line\\n\\nfourth\"");
  ASSERT_STREQ(row(a, 1), "");
  app_free(a);
}

static void test_slurp(void) {
  app* a = open_app("tests/data/sample.json", "map(keys[0])", 0, 1, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(row(a, 0), "\xe2\x96\xbe [2]");
  ASSERT_STREQ(row(a, 1), "    0: \"ok\"");
  ASSERT_STREQ(row(a, 2), "    1: \"second\"");
  CHECK(strstr(line(a, H - 2), "1 outputs") != NULL);
  app_free(a);
}

static void test_args(void) {
  jv args = jv_object_set(jv_object(), jv_string("name"), jv_string("x"));
  app* a;

  args = jv_object_set(args, jv_string("n"), jv_parse("[3]"));
  a = open_app("tests/data/sample.json", "[$name, $n]", 0, 0, args, 0);
  if (!a) return;

  ASSERT_STREQ(row(a, 1), "    0: \"x\"");
  ASSERT_STREQ(row(a, 2), "  \xe2\x96\xbe 1: [1]");
  ASSERT_STREQ(row(a, 3), "      0: 3");
  ASSERT_STREQ(row(a, 4), "\xe2\x96\xbe #1 [2]");
  app_free(a);
}

static void test_compile_error_keeps_last_good(void) {
  app* a = open_app("tests/data/sample.json", ".ok", 0, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(row(a, 0), "  #0 true");
  ASSERT_EQ(style_at(a, LEFT + 5, TOP), STYLE_TRUE);

  type(a, " | [", 0);
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(query(a), ".ok | [");
  ASSERT_STREQ(row(a, 0), "  #0 true");
  ASSERT_EQ(style_at(a, LEFT + 5, TOP), STYLE_DIM);
  CHECK(strstr(line(a, H - 2), "syntax error") != NULL);

  retype(a, ".ok");
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_EQ(style_at(a, LEFT + 5, TOP), STYLE_TRUE);
  CHECK(strstr(line(a, H - 2), "error") == NULL);
  app_free(a);
}

static void test_runtime_error_keeps_partial_outputs(void) {
  app* a = open_app("tests/data/sample.json", "1, error(\"boom\"), 3", 0, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(row(a, 0), "  #0 1");
  ASSERT_STREQ(row(a, 1), "  #1 1");
  ASSERT_STREQ(row(a, 2), "");
  CHECK(strstr(line(a, H - 2), "2 outputs") != NULL);
  CHECK(strstr(line(a, H - 2), "boom") != NULL);
  ASSERT_EQ(style_at(a, 1, H - 2), STYLE_PILL_ERROR);
  app_free(a);
}

static void test_empty_output(void) {
  app* a = open_app("tests/data/sample.json", "empty", 0, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(row(a, 0), "(no output)");
  CHECK(strstr(line(a, H - 2), "0 outputs") != NULL);
  app_free(a);
}

static void test_query_highlighted(void) {
  app* a = open_app("tests/data/sample.json", ".a | length", 0, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_EQ(style_at(a, QUERY_X, 1), STYLE_KEY);
  ASSERT_EQ(style_at(a, QUERY_X + 3, 1), STYLE_OPERATOR);
  ASSERT_EQ(style_at(a, QUERY_X + 5, 1), STYLE_BUILTIN);
  retype(a, "[.a");
  ASSERT_EQ(style_at(a, QUERY_X, 1), STYLE_ERROR);
  app_free(a);
}

static void test_output_cap(void) {
  app* a = open_app("tests/data/sample.json", "range(100)", 0, 0, jv_invalid(), 3);

  if (!a) return;

  ASSERT_STREQ(row(a, 0), "  #0 0");
  ASSERT_STREQ(row(a, 2), "  #2 2");
  ASSERT_STREQ(row(a, 3), "");
  CHECK(strstr(line(a, H - 2), "3 outputs (capped)") != NULL);
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
  ASSERT_STREQ(row(a, 0), "\xe2\x96\xbe #0 {2}");

  tick.now_ms = 150;
  app_feed(a, tick);
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(row(a, 0), "  #0 true");
  app_free(a);
}

/* a tick reports a change only when a result arrives, so the front-end
 * draws only then. */
static void test_changed_only_when_shown(void) {
  app* a = open_app("tests/data/sample.json", ".", 0, 0, jv_invalid(), 0);
  app_event ev;
  i64 deadline = clock_us() / 1000 + SETTLE_MS;
  b8 changed = 0;

  if (!a) return;

  ev.kind = EV_TICK;
  ev.key = 0;
  ev.now_ms = 0;
  CHECK(!app_feed(a, ev));
  ev.kind = EV_KEY;
  ev.key = 'k';
  CHECK(app_feed(a, ev));
  ev.kind = EV_TICK;
  ev.now_ms = 1000;
  while (!changed && clock_us() / 1000 < deadline) changed = app_feed(a, ev);

  CHECK(changed);
  ASSERT_STREQ(row(a, 0), "  #0 null");
  CHECK(!app_feed(a, ev));
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
  ASSERT_STREQ(row(a, 0), "  #0 2");
  app_free(a);
}

static void test_invalid_json_opens(void) {
  app* a = open_app("tests/data/broken.json", ".", 0, 0, jv_invalid(), 0);

  if (!a) return;

  ASSERT_STREQ(row(a, 0), "\xe2\x96\xbe {1}");
  ASSERT_STREQ(row(a, 1), "    a: 1");
  CHECK(strstr(line(a, H - 2), "Unfinished JSON term") != NULL);
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
  f(test_query_highlighted)                    \
  f(test_output_cap)                           \
  f(test_debounce)                             \
  f(test_changed_only_when_shown)              \
  f(test_cancel_on_edit)                       \
  f(test_invalid_json_opens)                   \
  f(test_missing_file)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
