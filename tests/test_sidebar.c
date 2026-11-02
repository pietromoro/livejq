/* sidebar tests - the saved queries panel, over a temporary folder. */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app.h"
#include "defines.h"

#include "test_helper.in"

#define W         72
#define H         12
#define TOP       4 /* the first name's row, and the tree's */
#define BOTTOM    (H - 3)
#define SIDE_W    20
#define QUERY_X   4 /* the query's first column, after the prompt */
#define SETTLE_MS 5000
#define CTRL(c)   ((c) & 0x1f)
#define GUTTER    0x2503

static char* make_folder(void);
static void remove_folder(char* dir);
static const char* in(const char* dir, const char* file);
static const char* read_back(const char* dir, const char* file);
static void put(const char* dir, const char* file, const char* text);
static app* open_in(const char* dir, b8 with_folder, i64 watch_ms);
static void press(app* a, u32 key);
static b8 tick(app* a, i64 now);
static b8 click(app* a, i32 x, i32 y);
static const char* cells(app* a, i32 x, i32 y, i32 max_x);
static i32 gutter_row(app* a, i32 x);
static void type(app* a, const char* s);
static void test_absent_without_folder(void);
static void test_bad_folder(void);
static void test_lists_names(void);
static void test_empty_hint(void);
static void test_width(void);
static void test_toggle_and_focus(void);
static void test_navigation(void);
static void test_tab_in_help(void);
static void test_click(void);
static void test_follows_folder(void);
static void test_load(void);
static void test_modified_label(void);
static void test_follows_loaded_file(void);
static void test_save_overwrites(void);

static grid* screen;

static char* make_folder(void) {
  char tmpl[] = "/tmp/livejq-sidebar-XXXXXX";
  char* dir = mkdtemp(tmpl);

  if (!dir) FAIL("mkdtemp failed");

  return dir ? strdup(dir) : NULL;
}

static void remove_folder(char* dir) {
  DIR* d = opendir(dir);
  struct dirent* de;

  while (d && (de = readdir(d)) != NULL)
    if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) remove(in(dir, de->d_name));

  if (d) closedir(d);

  rmdir(dir);
  free(dir);
}

static const char* in(const char* dir, const char* file) {
  static char path[512];

  snprintf(path, sizeof(path), "%s/%s", dir, file);
  return path;
}

/* the file's bytes, "" when it cannot be read. */
static const char* read_back(const char* dir, const char* file) {
  static char buf[256];
  FILE* f = fopen(in(dir, file), "rb");
  usz n = 0;

  if (f) {
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
  }

  buf[n] = '\0';
  return buf;
}

static void put(const char* dir, const char* file, const char* text) {
  FILE* f = fopen(in(dir, file), "wb");

  if (!f) {
    FAIL("cannot write %s", in(dir, file));
    return;
  }

  fputs(text, f);
  fclose(f);
}

/* the data is data.json in dir, which also holds the queries. */
static app* open_in(const char* dir, b8 with_folder, i64 watch_ms) {
  app_opts o;
  char data[512];

  snprintf(data, sizeof(data), "%s", in(dir, "data.json"));
  put(dir, "data.json", "{\"a\":1,\"b\":[2,3]}");
  memset(&o, 0, sizeof(o));
  o.path = data;
  o.args = jv_invalid();
  o.watch_ms = watch_ms;
  o.query_folder = with_folder ? dir : NULL;

  char err[256];
  app* a = app_new(&o, err, sizeof(err));
  if (!a) FAIL("app_new(%s): %s", dir, err);
  else CHECK(app_settle(a, SETTLE_MS));

  return a;
}

static void press(app* a, u32 key) {
  app_event ev = { 0 };

  ev.kind = EV_KEY;
  ev.key = key;
  app_feed(a, ev);
}

static b8 tick(app* a, i64 now) {
  app_event ev = { 0 };

  ev.kind = EV_TICK;
  ev.now_ms = now;
  return app_feed(a, ev);
}

static b8 click(app* a, i32 x, i32 y) {
  app_event ev = { 0 };

  ev.kind = EV_MOUSE;
  ev.mouse = APP_MOUSE_PRESS;
  ev.x = x;
  ev.y = y;
  return app_feed(a, ev);
}

/* row y from column x up to max_x, without trailing blanks. */
static const char* cells(app* a, i32 x, i32 y, i32 max_x) {
  static char buf[W * 4 + 1];
  usz n = 0;

  if (!screen) screen = grid_new(W, H);
  app_render(a, screen);
  for (i32 i = x; i < max_x && i < W; i++)
    n += utf8_put(grid_at(screen, i, y).ch, buf + n);

  while (n > 0 && buf[n - 1] == ' ') n--;

  buf[n] = '\0';
  return buf;
}

/* the row of the cursor's mark in column x, -1 when there is none. */
static i32 gutter_row(app* a, i32 x) {
  cells(a, 0, 0, 0);
  for (i32 y = TOP; y < BOTTOM; y++)
    if (grid_at(screen, x, y).ch == GUTTER) return y;

  return -1;
}

static void type(app* a, const char* s) {
  app_event ev = { 0 };

  ev.kind = EV_KEY;
  while (*s) {
    ev.key = (u8)*s++;
    app_feed(a, ev);
  }
}

static void test_absent_without_folder(void) {
  char* dir = make_folder();

  put(dir, "a.query", ".a");

  app* a = open_in(dir, 0, 0);
  if (!a) return;

  ASSERT_STREQ(cells(a, 0, TOP - 1, W), "");
  ASSERT_EQ(gutter_row(a, 1), TOP);
  press(a, CTRL('b'));
  ASSERT_STREQ(cells(a, 0, TOP - 1, W), "");

  press(a, '\t');
  press(a, '\t');
  ASSERT_EQ(app_focused(a), FOCUS_QUERY);
  press(a, APP_KEY_BACKTAB);
  ASSERT_EQ(app_focused(a), FOCUS_TREE);

  app_free(a);
  remove_folder(dir);
}

static void test_bad_folder(void) {
  char* dir = make_folder();
  app_opts o;

  put(dir, "data.json", "1");
  memset(&o, 0, sizeof(o));
  o.path = in(dir, "data.json");
  o.args = jv_invalid();
  o.query_folder = "/nonexistent/livejq";

  char err[256];
  CHECK(app_new(&o, err, sizeof(err)) == NULL);
  CHECK(strstr(err, "/nonexistent/livejq: ") == err);
  remove_folder(dir);
}

static void test_lists_names(void) {
  char* dir = make_folder();

  put(dir, "beta.query", ".b");
  put(dir, "Alpha.query", ".a");

  app* a = open_in(dir, 1, 0);
  if (!a) return;

  ASSERT_STREQ(
    cells(a, 0, TOP - 1, SIDE_W),
    "\xe2\x94\x80 saved \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
                                             "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
                                             "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x95\xae"
  );
  ASSERT_STREQ(cells(a, 0, TOP, SIDE_W), " \xe2\x94\x83" "Alpha            \xe2\x94\x82");
  ASSERT_STREQ(cells(a, 0, TOP + 1, SIDE_W), "  beta             \xe2\x94\x82");
  ASSERT_STREQ(cells(a, 0, TOP + 2, SIDE_W), "                   \xe2\x94\x82");
  ASSERT_STREQ(cells(a, SIDE_W - 1, BOTTOM, SIDE_W), "\xe2\x95\xaf");
  ASSERT_EQ(gutter_row(a, SIDE_W + 1), TOP);

  app_free(a);
  remove_folder(dir);
}

static void test_empty_hint(void) {
  char* dir = make_folder();
  app* a = open_in(dir, 1, 0);

  if (!a) return;

  ASSERT_STREQ(cells(a, 0, TOP, SIDE_W - 1), "  no saved queries");
  ASSERT_STREQ(cells(a, 0, TOP + 1, SIDE_W - 1), "  ctrl-s saves one");
  ASSERT_EQ(gutter_row(a, 1), -1);

  app_free(a);
  remove_folder(dir);
}

static void test_width(void) {
  char* dir = make_folder();

  put(dir, "eighteen-columns-x.query", ".");

  app* a = open_in(dir, 1, 0);
  if (!a) return;

  ASSERT_STREQ(
    cells(a, 0, TOP, W),
    " \xe2\x94\x83" "eighteen-columns-x \xe2\x94\x82 \xe2\x94\x83\xe2\x96\xbe {2}"
  );

  put(dir, "a-name-much-too-long-for-the-panel.query", ".");
  CHECK(tick(a, 0));
  ASSERT_STREQ(cells(a, 0, TOP, W / 3), "  a-name-much-too-lon\xe2\x80\xa6 \xe2\x94\x82");

  app_free(a);
  remove_folder(dir);
}

static void test_toggle_and_focus(void) {
  char* dir = make_folder();

  put(dir, "a.query", ".a");

  app* a = open_in(dir, 1, 0);
  if (!a) return;

  press(a, '\t');
  press(a, '\t');
  ASSERT_EQ(app_focused(a), FOCUS_SIDEBAR);
  press(a, '\t');
  ASSERT_EQ(app_focused(a), FOCUS_QUERY);
  press(a, APP_KEY_BACKTAB);
  ASSERT_EQ(app_focused(a), FOCUS_SIDEBAR);

  press(a, CTRL('b'));
  ASSERT_EQ(app_focused(a), FOCUS_TREE);
  ASSERT_STREQ(cells(a, 0, TOP - 1, W), "");
  ASSERT_EQ(gutter_row(a, 1), TOP);
  press(a, '\t');
  press(a, '\t');
  ASSERT_EQ(app_focused(a), FOCUS_TREE);

  press(a, CTRL('b'));
  ASSERT_STREQ(cells(a, 0, TOP, SIDE_W), " \xe2\x94\x83" "a                \xe2\x94\x82");
  ASSERT_EQ(app_focused(a), FOCUS_TREE);

  app_free(a);
  remove_folder(dir);
}

static void test_navigation(void) {
  char* dir = make_folder();

  put(dir, "a.query", ".");
  put(dir, "b.query", ".");
  put(dir, "c.query", ".");

  app* a = open_in(dir, 1, 0);
  if (!a) return;

  press(a, APP_KEY_BACKTAB);
  press(a, 'j');
  ASSERT_EQ(gutter_row(a, 1), TOP + 1);
  press(a, 'j');
  press(a, APP_KEY_DOWN);
  ASSERT_EQ(gutter_row(a, 1), TOP + 2);
  press(a, 'k');
  ASSERT_EQ(gutter_row(a, 1), TOP + 1);
  press(a, 'g');
  ASSERT_EQ(gutter_row(a, 1), TOP);
  press(a, 'G');
  ASSERT_EQ(gutter_row(a, 1), TOP + 2);
  ASSERT_EQ(gutter_row(a, SIDE_W + 1), TOP);

  press(a, 'q');
  CHECK(app_quitting(a));

  app_free(a);
  remove_folder(dir);
}

static void test_tab_in_help(void) {
  char* dir = make_folder();
  app* a = open_in(dir, 1, 0);

  if (!a) return;

  press(a, '\t');
  CHECK(strstr(cells(a, 0, H - 1, W), "tab saved queries") != NULL);
  press(a, CTRL('b'));
  CHECK(strstr(cells(a, 0, H - 1, W), "tab query") != NULL);
  press(a, APP_KEY_BACKTAB);
  CHECK(strstr(cells(a, 0, H - 1, W), "tab tree") != NULL);

  app_free(a);
  remove_folder(dir);
}

static void test_click(void) {
  char* dir = make_folder();

  put(dir, "a.query", ".");
  put(dir, "b.query", ".");

  app* a = open_in(dir, 1, 0);
  if (!a) return;

  cells(a, 0, 0, 0);
  CHECK(click(a, 3, TOP + 1));
  ASSERT_EQ(app_focused(a), FOCUS_SIDEBAR);
  ASSERT_EQ(gutter_row(a, 1), TOP + 1);
  CHECK(!click(a, 3, TOP + 1));
  CHECK(!click(a, 3, TOP + 4));
  ASSERT_EQ(gutter_row(a, 1), TOP + 1);

  CHECK(click(a, SIDE_W + 2, TOP + 1));
  ASSERT_EQ(app_focused(a), FOCUS_TREE);
  ASSERT_EQ(gutter_row(a, SIDE_W + 1), TOP + 1);

  app_free(a);
  remove_folder(dir);
}

static void test_follows_folder(void) {
  char* dir = make_folder();

  put(dir, "b.query", ".b");

  app* a = open_in(dir, 1, 2000);
  if (!a) return;

  CHECK(!tick(a, 0));
  put(dir, "a.query", ".a");
  put(dir, "notes.txt", "");
  CHECK(!tick(a, 1999));
  ASSERT_STREQ(cells(a, 0, TOP, SIDE_W - 1), " \xe2\x94\x83" "b");

  CHECK(tick(a, 2000));
  ASSERT_STREQ(cells(a, 0, TOP, SIDE_W - 1), "  a");
  ASSERT_STREQ(cells(a, 0, TOP + 1, SIDE_W - 1), " \xe2\x94\x83" "b");
  CHECK(!tick(a, 4000));

  remove(in(dir, "b.query"));
  CHECK(tick(a, 6000));
  ASSERT_STREQ(cells(a, 0, TOP, SIDE_W - 1), " \xe2\x94\x83" "a");
  ASSERT_STREQ(cells(a, 0, TOP + 1, SIDE_W - 1), "");

  app_free(a);
  remove_folder(dir);
}

static void test_load(void) {
  char* dir = make_folder();

  put(dir, "a.query", ".a\n");
  put(dir, "b.query", ".b[]");

  app* a = open_in(dir, 1, 0);
  if (!a) return;

  press(a, APP_KEY_BACKTAB);
  press(a, APP_KEY_ENTER);
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(cells(a, QUERY_X, 1, W - 1), ".a");
  ASSERT_STREQ(cells(a, 1, 0, 6), "\xe2\x94\x80 a \xe2\x94\x80");
  ASSERT_STREQ(cells(a, SIDE_W + 4, TOP, W), "1");
  ASSERT_EQ(app_focused(a), FOCUS_SIDEBAR);

  press(a, 'j');
  press(a, APP_KEY_ENTER);
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(cells(a, QUERY_X, 1, W - 1), ".b[]");
  ASSERT_STREQ(cells(a, 1, 0, 6), "\xe2\x94\x80 b \xe2\x94\x80");
  ASSERT_STREQ(cells(a, SIDE_W + 4, TOP, W), "#0 2");

  app_free(a);
  remove_folder(dir);
}

static void test_modified_label(void) {
  char* dir = make_folder();

  put(dir, "a.query", ".a");

  app* a = open_in(dir, 1, 0);
  if (!a) return;

  ASSERT_STREQ(cells(a, 1, 0, 6), "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80");
  press(a, APP_KEY_BACKTAB);
  press(a, APP_KEY_ENTER);
  press(a, '\t');
  type(a, " ");
  ASSERT_STREQ(cells(a, 1, 0, 8), "\xe2\x94\x80 a \xe2\x97\x8f \xe2\x94\x80");
  press(a, APP_KEY_BACKSPACE);
  ASSERT_STREQ(cells(a, 1, 0, 6), "\xe2\x94\x80 a \xe2\x94\x80");

  app_free(a);
  remove_folder(dir);
}

static void test_follows_loaded_file(void) {
  char* dir = make_folder();

  put(dir, "a.query", ".a");

  app* a = open_in(dir, 1, 0);
  if (!a) return;

  tick(a, 0);
  press(a, APP_KEY_BACKTAB);
  press(a, APP_KEY_ENTER);
  put(dir, "a.query", ".b[0]");
  CHECK(tick(a, 1000));
  CHECK(app_settle(a, SETTLE_MS));
  ASSERT_STREQ(cells(a, QUERY_X, 1, W - 1), ".b[0]");
  ASSERT_STREQ(cells(a, SIDE_W + 4, TOP, W), "2");

  press(a, '\t');
  type(a, " | .");
  put(dir, "a.query", ".a | tostring");
  CHECK(tick(a, 2000));
  ASSERT_STREQ(cells(a, QUERY_X, 1, W - 1), ".b[0] | .");
  ASSERT_STREQ(cells(a, 1, 0, 8), "\xe2\x94\x80 a \xe2\x97\x8f \xe2\x94\x80");

  remove(in(dir, "a.query"));
  CHECK(tick(a, 3000));
  ASSERT_STREQ(cells(a, 1, 0, 6), "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80");
  ASSERT_STREQ(cells(a, QUERY_X, 1, W - 1), ".b[0] | .");

  app_free(a);
  remove_folder(dir);
}

static void test_save_overwrites(void) {
  char* dir = make_folder();

  put(dir, "a.query", ".a");

  app* a = open_in(dir, 1, 0);
  if (!a) return;

  press(a, CTRL('s'));
  ASSERT_STREQ(read_back(dir, "a.query"), ".a");

  press(a, APP_KEY_BACKTAB);
  press(a, APP_KEY_ENTER);
  press(a, '\t');
  type(a, " | .");
  press(a, CTRL('s'));
  ASSERT_STREQ(read_back(dir, "a.query"), ".a | .\n");
  ASSERT_STREQ(cells(a, 1, 0, 6), "\xe2\x94\x80 a \xe2\x94\x80");
  CHECK(!tick(a, 0));

  remove(in(dir, "a.query"));
  mkdir(in(dir, "a.query"), 0700);
  press(a, APP_KEY_BACKSPACE);
  press(a, CTRL('s'));
  ASSERT_STREQ(cells(a, 1, H - 2, 8), "ERROR");
  ASSERT_STREQ(cells(a, 1, 0, 8), "\xe2\x94\x80 a \xe2\x97\x8f \xe2\x94\x80");

  app_free(a);
  remove_folder(dir);
}

#define TESTS(f)                    \
   f(test_absent_without_folder)    \
   f(test_bad_folder)               \
   f(test_lists_names)              \
   f(test_empty_hint)               \
   f(test_width)                    \
   f(test_toggle_and_focus)         \
   f(test_navigation)               \
   f(test_tab_in_help)              \
   f(test_click)                    \
   f(test_follows_folder)           \
   f(test_load)                     \
   f(test_modified_label)           \
   f(test_follows_loaded_file)      \
   f(test_save_overwrites)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
