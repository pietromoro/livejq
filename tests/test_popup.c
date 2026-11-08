/* popup tests - completing keys in the query through the core. */
#include <string.h>

#include "app.h"
#include "defines.h"
#include "popup.h"

#include "test_helper.in"

#define W          80
#define H          24
#define BOX_Y      3 /* the popup's top border, under the query box */
#define QUERY_X    4
#define SETTLE_MS  5000
#define CTRL_SPACE 0
#define ESC        0x1b
#define CORNER     0x256d /* ╭ */
#define CORNER_END 0x256e /* ╮ */

static app* open_app(const char* path, const char* query);
static void press(app* a, u32 key);
static void type(app* a, const char* s);
static const char* item(app* a, i32 n);
static const char* query(app* a);
static void test_keys_at_path(void);
static void test_keys_filtered(void);
static void test_pick_and_take(void);
static void test_close(void);
static void test_quoted_key(void);
static void test_keys_inside_map(void);
static void test_fallback(void);
static void test_tab_closes(void);
static void test_builtins(void);
static void test_keys_then_builtins(void);
static void test_merge_takes_lists(void);

static grid* screen;

static app* open_app(const char* path, const char* query) {
  app_opts o;

  memset(&o, 0, sizeof(o));
  o.path = path;
  o.query = query;
  o.args = jv_invalid();

  char err[256];
  app* a = app_new(&o, err, sizeof(err));
  if (!a) FAIL("app_new(%s): %s", path, err);
  else CHECK(app_settle(a, SETTLE_MS));

  if (!screen) screen = grid_new(W, H);

  return a;
}

static void press(app* a, u32 key) {
  app_event ev = { 0 };

  ev.kind = EV_KEY;
  ev.key = key;
  app_feed(a, ev);
  app_settle(a, SETTLE_MS);
}

static void type(app* a, const char* s) {
  usz n = strlen(s);

  for (usz i = 0; i < n; i++) press(a, (u8)s[i]);
}

/* row n of the popup with its runs of blanks squeezed, the gutter as ┃,
 * NULL past its last row or while it is closed. */
static const char* item(app* a, i32 n) {
  static char buf[W * 4 + 1];
  i32 x = 0;
  i32 end = 0;

  app_render(a, screen);
  while (x < W && grid_at(screen, x, BOX_Y).ch != CORNER) x++;
  end = x;
  while (end < W && grid_at(screen, end, BOX_Y).ch != CORNER_END) end++;
  if (x == W || end == W || grid_at(screen, x, BOX_Y + 1 + n).ch != 0x2502) return NULL;

  usz len = 0;
  b8 blank = 1;
  for (i32 i = x + 1; i < end; i++) {
    u32 c = grid_at(screen, i, BOX_Y + 1 + n).ch;

    if (c == ' ' && blank) continue;

    blank = c == ' ';
    len += utf8_put(c, buf + len);
  }

  while (len > 0 && buf[len - 1] == ' ') len--;
  buf[len] = '\0';
  return buf;
}

static const char* query(app* a) {
  static char buf[W * 4 + 1];
  usz len = 0;

  app_render(a, screen);
  for (i32 x = QUERY_X; x < W - 1; x++) len += utf8_put(grid_at(screen, x, 1).ch, buf + len);

  while (len > 0 && buf[len - 1] == ' ') len--;
  buf[len] = '\0';
  return buf;
}

static void test_keys_at_path(void) {
  app* a = open_app("tests/data/sample.json", ".users[0].");

  if (!a) return;

  CHECK(item(a, 0) == NULL);
  press(a, CTRL_SPACE);
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83id number");
  ASSERT_STREQ(item(a, 1), "name string");
  ASSERT_STREQ(item(a, 2), "ts number");
  CHECK(item(a, 3) == NULL);
  app_free(a);
}

/* the first input only, prefixes before other matches, smart case. */
static void test_keys_filtered(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, APP_KEY_DOWN);
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83users array");
  ASSERT_STREQ(item(a, 1), "ok boolean");
  CHECK(item(a, 2) == NULL);
  type(a, "users[].");
  CHECK(item(a, 0) == NULL);
  press(a, CTRL_SPACE);
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83id number");
  ASSERT_STREQ(item(a, 3), "tags array");
  type(a, "s");
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83ts number");
  ASSERT_STREQ(item(a, 1), "tags array");
  CHECK(item(a, 2) == NULL);
  press(a, 'X');
  CHECK(item(a, 0) == NULL);
  press(a, APP_KEY_BACKSPACE);
  ASSERT_STREQ(item(a, 1), "tags array");
  app_free(a);
}

static void test_pick_and_take(void) {
  app* a = open_app("tests/data/sample.json", ".users[1].");

  if (!a) return;

  press(a, CTRL_SPACE);
  press(a, APP_KEY_DOWN);
  press(a, APP_KEY_DOWN);
  ASSERT_STREQ(item(a, 2), "\xe2\x94\x83tags array");
  press(a, APP_KEY_DOWN);
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83id number");
  press(a, APP_KEY_UP);
  press(a, APP_KEY_ENTER);
  CHECK(item(a, 0) == NULL);
  ASSERT_STREQ(query(a), ".users[1].tags");

  press(a, APP_KEY_HOME);
  type(a, "[");
  press(a, APP_KEY_END);
  for (i32 i = 0; i < 4; i++) press(a, APP_KEY_LEFT);
  press(a, CTRL_SPACE);
  press(a, APP_KEY_DOWN);
  press(a, APP_KEY_ENTER);
  ASSERT_STREQ(query(a), "[.users[1].name");
  app_free(a);
}

static void test_close(void) {
  app* a = open_app("tests/data/sample.json", ".users[0].");

  if (!a) return;

  press(a, CTRL_SPACE);
  press(a, ESC);
  CHECK(item(a, 0) == NULL);
  ASSERT_STREQ(query(a), ".users[0].");
  press(a, CTRL_SPACE);
  type(a, "id |");
  CHECK(item(a, 0) == NULL);
  press(a, '.');
  CHECK(item(a, 0) == NULL);
  app_free(a);
}

static void test_quoted_key(void) {
  app* a = open_app("tests/data/keys.json", ".");

  if (!a) return;

  press(a, CTRL_SPACE);
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83" "a b number");
  ASSERT_STREQ(item(a, 1), "9x number");
  press(a, APP_KEY_ENTER);
  ASSERT_STREQ(query(a), ".\"a b\"");
  app_free(a);
}

static void test_keys_inside_map(void) {
  app* a = open_app("tests/data/sample.json", ".users | map(.");

  if (!a) return;

  press(a, CTRL_SPACE);
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83id number");
  ASSERT_STREQ(item(a, 3), "tags array");
  app_free(a);
}

static void test_fallback(void) {
  app* a = open_app("tests/data/sample.json", "def f: .users[0].");

  if (!a) return;

  press(a, CTRL_SPACE);
  ASSERT_STREQ(item(a, 1), "name string");
  app_free(a);
}

static void test_tab_closes(void) {
  app* a = open_app("tests/data/sample.json", ".");

  if (!a) return;

  press(a, CTRL_SPACE);
  press(a, '\t');
  CHECK(item(a, 0) == NULL);
  press(a, APP_KEY_BACKTAB);
  CHECK(item(a, 0) == NULL);
  app_free(a);
}

static void test_builtins(void) {
  app* a = open_app("tests/data/sample.json", ".users | ma");

  if (!a) return;

  press(a, CTRL_SPACE);
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83map /1");
  ASSERT_STREQ(item(a, 1), "map_values /1");
  type(a, "p_");
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83map_values /1");
  press(a, APP_KEY_ENTER);
  ASSERT_STREQ(query(a), ".users | map_values");

  type(a, "(ra");
  press(a, CTRL_SPACE);
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83range /1 /2 /3");
  press(a, ESC);
  type(a, " | _");
  press(a, CTRL_SPACE);
  CHECK(item(a, 0) != NULL && strncmp(item(a, 0), "\xe2\x94\x83_", 4) != 0);
  app_free(a);
}

/* one popup follows the cursor from a builtin to a key and back. */
static void test_keys_then_builtins(void) {
  app* a = open_app("tests/data/sample.json", ".users[0] | le");

  if (!a) return;

  press(a, CTRL_SPACE);
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83length /0");
  for (i32 i = 0; i < 5; i++) press(a, APP_KEY_LEFT);
  CHECK(item(a, 0) == NULL);
  press(a, APP_KEY_HOME);
  press(a, APP_KEY_RIGHT);
  press(a, CTRL_SPACE);
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83users array");
  press(a, APP_KEY_END);
  ASSERT_STREQ(item(a, 0), "\xe2\x94\x83length /0");
  app_free(a);
}

/* merging gives back every list it was handed. */
static void test_merge_takes_lists(void) {
  jv list = jv_parse("[[\"name\", \"string\"], [\"age\", \"number\"]]");
  popup p;

  popup_init(&p);
  popup_merge(&p, JV_ARRAY(jv_copy(list)));
  CHECK(jv_get_refcnt(list) == 1);
  popup_free(&p);
  jv_free(list);
}

#define TESTS(f)               \
   f(test_keys_at_path)        \
   f(test_keys_filtered)       \
   f(test_pick_and_take)       \
   f(test_close)               \
   f(test_quoted_key)          \
   f(test_keys_inside_map)     \
   f(test_fallback)            \
   f(test_tab_closes)          \
   f(test_builtins)            \
   f(test_keys_then_builtins)  \
   f(test_merge_takes_lists)
#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
