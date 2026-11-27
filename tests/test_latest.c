/* latest tests - the pattern, the newest match, and the app following it. */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include "app.h"
#include "grid.h"
#include "latest.h"

#include "test_helper.in"

#define W         80
#define H         16
#define SETTLE_MS 5000
#define WALL_MS   1730300090000

static char* make_folder(void);
static void remove_folder(char* dir);
static const char* in(const char* dir, const char* file);
static void put(const char* dir, const char* file, const char* text, i64 age_s);
static app* open_latest(const char* dir, const char* pattern);
static b8 tick(app* a, i64 now);
static b8 shows(app* a, const char* text);
static const char* status(app* a);
static void test_match(void);
static void test_find_newest(void);
static void test_find_tie_by_name(void);
static void test_find_nothing(void);
static void test_follow_newer(void);
static void test_follow_grace(void);
static void test_follow_deleted(void);

static char* make_folder(void) {
  char tmpl[] = "/tmp/livejq-latest-XXXXXX";
  char* dir = mkdtemp(tmpl);

  if (!dir) FAIL("mkdtemp failed");

  return dir ? strdup(dir) : NULL;
}

static void remove_folder(char* dir) {
  DIR* d = opendir(dir);
  struct dirent* de;
  char path[512];

  while (d && (de = readdir(d)) != NULL) {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

    snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
    if (remove(path) != 0) rmdir(path);
  }

  if (d) closedir(d);

  rmdir(dir);
  free(dir);
}

static const char* in(const char* dir, const char* file) {
  static char path[512];

  snprintf(path, sizeof(path), "%s/%s", dir, file);
  return path;
}

/* writes the file, modified age_s seconds ago. */
static void put(const char* dir, const char* file, const char* text, i64 age_s) {
  const char* path = in(dir, file);
  FILE* f = fopen(path, "wb");
  struct utimbuf t;

  if (!f) {
    FAIL("cannot write %s", path);
    return;
  }

  fputs(text, f);
  fclose(f);
  t.actime = t.modtime = time(NULL) - (time_t)age_s;
  if (utime(path, &t) != 0) FAIL("cannot date %s", path);
}

static app* open_latest(const char* dir, const char* pattern) {
  app_opts o;

  memset(&o, 0, sizeof(o));
  o.path = dir;
  o.latest = pattern;
  o.args = jv_invalid();
  o.watch_ms = 1000;

  char err[256];
  app* a = app_new(&o, err, sizeof(err));
  if (!a) FAIL("app_new(%s, %s): %s", dir, pattern, err);
  else CHECK(app_settle(a, SETTLE_MS));

  return a;
}

static b8 tick(app* a, i64 now) {
  app_event ev = { 0 };

  ev.kind = EV_TICK;
  ev.now_ms = now;
  ev.wall_ms = WALL_MS;

  b8 changed = app_feed(a, ev);
  CHECK(app_settle(a, SETTLE_MS));
  return changed;
}

/* true when a row of the screen holds text. */
static b8 shows(app* a, const char* text) {
  grid* g = grid_new(W, H);
  char buf[W * 4 + 1];
  b8 found = 0;

  app_render(a, g);
  for (i32 y = 0; y < H && !found; y++) {
    grid_line(g, y, buf, sizeof(buf));
    found = strstr(buf, text) != NULL;
  }

  grid_free(g);
  return found;
}

static const char* status(app* a) {
  static char buf[W * 4 + 1];
  grid* g = grid_new(W, H);

  app_render(a, g);
  grid_line(g, H - 2, buf, sizeof(buf));
  grid_free(g);
  return buf;
}

static void test_match(void) {
  CHECK(latest_match("*.save", "00000000000000000042.save"));
  CHECK(latest_match("*.save", ".save"));
  CHECK(!latest_match("*.save", "42.save.tmp"));
  CHECK(!latest_match("*.save", "42.sav"));
  CHECK(latest_match("*", "anything"));
  CHECK(latest_match("*", ""));
  CHECK(latest_match("", ""));
  CHECK(!latest_match("", "a"));
  CHECK(latest_match("a?c", "abc"));
  CHECK(!latest_match("a?c", "ac"));
  CHECK(latest_match("app-*.log", "app-2026-11-25.log"));
  CHECK(latest_match("*a*b*c", "xxaybzbc"));
  CHECK(!latest_match("*a*b*c", "xxaybzb"));
  CHECK(latest_match("**x", "abx"));
  CHECK(latest_match("data.json", "data.json"));
  CHECK(!latest_match("data.json", "data.jsonl"));
#ifndef _WIN32
  CHECK(!latest_match("*.SAVE", "1.save"));
#endif
}

static void test_find_newest(void) {
  char* dir = make_folder();
  file_stamp st;
  char err[256];

  if (!dir) return;

  put(dir, "1.save", "1", 300);
  put(dir, "2.save", "22", 100);
  put(dir, "3.save", "333", 200);
  put(dir, "4.json", "4444", 0);
  mkdir(in(dir, "5.save"), 0700);

  char* path = latest_find(dir, "*.save", &st, err, sizeof(err));
  ASSERT_NOT_NULL(path);
  if (path) ASSERT_STREQ(path, in(dir, "2.save"));
  ASSERT_EQ(st.size, 2);
  free(path);

  /* a trailing separator is not doubled. */
  char slashed[512];
  snprintf(slashed, sizeof(slashed), "%s/", dir);
  path = latest_find(slashed, "*.json", &st, err, sizeof(err));
  if (path) ASSERT_STREQ(path, in(dir, "4.json"));
  else FAIL("no match in %s", slashed);
  free(path);

  remove_folder(dir);
}

static void test_find_tie_by_name(void) {
  char* dir = make_folder();
  file_stamp st;
  char err[256];

  if (!dir) return;

  put(dir, "00000000000000000009.save", "a", 50);
  put(dir, "00000000000000000011.save", "b", 50);
  put(dir, "00000000000000000010.save", "c", 50);

  char* path = latest_find(dir, "*.save", &st, err, sizeof(err));
  if (path) ASSERT_STREQ(path, in(dir, "00000000000000000011.save"));
  else FAIL("no match: %s", err);
  free(path);
  remove_folder(dir);
}

static void test_find_nothing(void) {
  char* dir = make_folder();
  file_stamp st;
  char err[256];

  if (!dir) return;

  put(dir, "a.json", "{}", 0);
  ASSERT_NULL(latest_find(dir, "*.save", &st, err, sizeof(err)));
  CHECK(strstr(err, "no file matches *.save") != NULL);

  ASSERT_NULL(latest_find(in(dir, "nope"), "*", &st, err, sizeof(err)));
  CHECK(strstr(err, "nope") != NULL);

  app_opts o;
  memset(&o, 0, sizeof(o));
  o.path = dir;
  o.latest = "*.save";
  o.args = jv_invalid();
  ASSERT_NULL(app_new(&o, err, sizeof(err)));
  CHECK(strstr(err, "no file matches") != NULL);
  remove_folder(dir);
}

static void test_follow_newer(void) {
  char* dir = make_folder();

  if (!dir) return;

  put(dir, "1.save", "{\"n\":111}", 20);
  app* a = open_latest(dir, "*.save");
  if (!a) {
    remove_folder(dir);
    return;
  }

  CHECK(shows(a, "n: 111"));
  CHECK(strstr(status(a), "1.save") != NULL);

  CHECK(!tick(a, 0));
  put(dir, "2.save", "{\"n\":222}", 10);
  CHECK(tick(a, 1000));
  CHECK(shows(a, "n: 222"));
  CHECK(strstr(status(a), "2.save") != NULL);
  CHECK(strstr(status(a), " LIVEJQ ") == status(a));

  /* the followed file itself changing is read again. */
  put(dir, "2.save", "{\"n\":2222}", 5);
  CHECK(tick(a, 2000));
  CHECK(shows(a, "n: 2222"));

  CHECK(!tick(a, 3000));
  app_free(a);
  remove_folder(dir);
}

static void test_follow_grace(void) {
  char* dir = make_folder();

  if (!dir) return;

  put(dir, "1.save", "{\"n\":111}", 30);
  app* a = open_latest(dir, "*.save");
  if (!a) {
    remove_folder(dir);
    return;
  }

  tick(a, 0);

  /* caught mid-write: skipped without an error, then read once whole. */
  put(dir, "2.save", "{\"n\":", 20);
  CHECK(!tick(a, 1000));
  CHECK(shows(a, "n: 111"));
  CHECK(strstr(status(a), "1.save") != NULL);
  CHECK(strstr(status(a), " LIVEJQ ") == status(a));

  put(dir, "2.save", "{\"n\":222}", 20);
  CHECK(tick(a, 2000));
  CHECK(shows(a, "n: 222"));
  CHECK(strstr(status(a), "2.save") != NULL);

  /* still growing at the next look: more grace. */
  put(dir, "3.save", "{\"n\"", 10);
  CHECK(!tick(a, 3000));
  put(dir, "3.save", "{\"n\":3", 10);
  CHECK(!tick(a, 4000));
  CHECK(shows(a, "n: 222"));

  /* the same and still broken: it is taken, with the reason. */
  CHECK(tick(a, 5000));
  CHECK(shows(a, "n: 222"));
  CHECK(strstr(status(a), "3.save") != NULL);
  CHECK(strstr(status(a), " ERROR ") == status(a));
  CHECK(!tick(a, 6000));

  put(dir, "3.save", "{\"n\":333}", 10);
  CHECK(tick(a, 7000));
  CHECK(shows(a, "n: 333"));
  CHECK(strstr(status(a), " LIVEJQ ") == status(a));

  app_free(a);
  remove_folder(dir);
}

static void test_follow_deleted(void) {
  char* dir = make_folder();

  if (!dir) return;

  put(dir, "1.save", "{\"n\":111}", 30);
  put(dir, "2.save", "{\"n\":222}", 20);
  app* a = open_latest(dir, "*.save");
  if (!a) {
    remove_folder(dir);
    return;
  }

  CHECK(shows(a, "n: 222"));
  tick(a, 0);

  remove(in(dir, "2.save"));
  CHECK(tick(a, 1000));
  CHECK(shows(a, "n: 111"));
  CHECK(strstr(status(a), "1.save") != NULL);

  remove(in(dir, "1.save"));
  CHECK(tick(a, 2000));
  CHECK(shows(a, "n: 111"));
  CHECK(strstr(status(a), " MISSING ") == status(a));
  CHECK(strstr(status(a), "no file matches") != NULL);
  CHECK(!tick(a, 3000));

  put(dir, "3.save", "{\"n\":333}", 0);
  CHECK(tick(a, 4000));
  CHECK(shows(a, "n: 333"));
  CHECK(strstr(status(a), " LIVEJQ ") == status(a));

  app_free(a);
  remove_folder(dir);
}

/* curufmt skip translation-unit-order */
#define TESTS(f)              \
   f(test_match)              \
   f(test_find_newest)        \
   f(test_find_tie_by_name)   \
   f(test_find_nothing)       \
   f(test_follow_newer)       \
   f(test_follow_grace)       \
   f(test_follow_deleted)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
