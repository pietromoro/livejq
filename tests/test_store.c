/* store tests - the saved queries of a temporary folder. */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "defines.h"
#include "store.h"

#include "test_helper.in"

static char* make_folder(void);
static void remove_folder(char* dir);
static const char* in(const char* dir, const char* file);
static void put(const char* dir, const char* file, const char* text);
static b8 exists(const char* dir, const char* file);
static const char* names(const store* s);
static void test_missing_folder(void);
static void test_lists_query_files(void);
static void test_read_trims_newlines(void);
static void test_save_new_and_overwrite(void);
static void test_bad_names(void);
static void test_delete(void);
static void test_outside_changes(void);

static char* make_folder(void) {
  char tmpl[] = "/tmp/livejq-store-XXXXXX";
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

static void put(const char* dir, const char* file, const char* text) {
  FILE* f = fopen(in(dir, file), "wb");

  if (!f) {
    FAIL("cannot write %s", in(dir, file));
    return;
  }

  fputs(text, f);
  fclose(f);
}

static b8 exists(const char* dir, const char* file) {
  struct stat st;

  return stat(in(dir, file), &st) == 0;
}

static const char* names(const store* s) {
  static char buf[512];

  buf[0] = '\0';
  for (usz i = 0; i < store_count(s); i++) {
    if (i) strcat(buf, ",");

    strcat(buf, store_name(s, i));
  }

  return buf;
}

static void test_missing_folder(void) {
  char err[256] = "";

  CHECK(store_open("/nonexistent/livejq", err, sizeof(err)) == NULL);
  CHECK(strstr(err, "/nonexistent/livejq: ") == err);
}

static void test_lists_query_files(void) {
  char* dir = make_folder();

  put(dir, "users.query", ".users\n");
  put(dir, "Beta.query", ".b");
  put(dir, "alpha.query", ".a");
  put(dir, "notes.txt", "x");
  put(dir, ".hidden.query", ".h");
  put(dir, ".query", ".");
  mkdir(in(dir, "dir.query"), 0700);

  char err[256];
  store* s = store_open(dir, err, sizeof(err));
  CHECK(s != NULL);
  CHECK(strcmp(names(s), "alpha,Beta,users") == 0);
  CHECK(store_find(s, "users") == 2);
  CHECK(store_find(s, "notes") == -1);
  CHECK(store_stamp(s, 2).size == 7);

  store_free(s);
  remove_folder(dir);
}

static void test_read_trims_newlines(void) {
  char* dir = make_folder();

  put(dir, "a.query", ".a | .b\r\n\n");
  put(dir, "empty.query", "");

  char err[256];
  store* s = store_open(dir, err, sizeof(err));
  char* text = store_read(s, "a", err, sizeof(err));
  CHECK(text && strcmp(text, ".a | .b") == 0);
  free(text);

  text = store_read(s, "empty", err, sizeof(err));
  CHECK(text && strcmp(text, "") == 0);
  free(text);

  CHECK(store_read(s, "gone", err, sizeof(err)) == NULL);
  CHECK(strstr(err, "gone.query: ") != NULL);

  store_free(s);
  remove_folder(dir);
}

static void test_save_new_and_overwrite(void) {
  char* dir = make_folder();

  char err[256];
  store* s = store_open(dir, err, sizeof(err));
  CHECK(store_count(s) == 0);

  CHECK(store_write(s, "by region", ".[] | .region", err, sizeof(err)));
  CHECK(strcmp(names(s), "by region") == 0);

  char* text = store_read(s, "by region", err, sizeof(err));
  CHECK(text && strcmp(text, ".[] | .region") == 0);
  free(text);
  CHECK(store_stamp(s, 0).size == 14);

  CHECK(store_write(s, "by region", ".", err, sizeof(err)));
  CHECK(store_count(s) == 1);
  text = store_read(s, "by region", err, sizeof(err));
  CHECK(text && strcmp(text, ".") == 0);
  free(text);

  store_free(s);
  remove_folder(dir);
}

static void test_bad_names(void) {
  static const char* const bad[] = {
      "",
      ".",
      "..",
      ".hidden",
      "../up",
      "a/b",
      "a\\b",
      "c:",
      "what?",
      "a*",
      "tab\there",
      "dot.",
      "space ",
      "con",
      "NUL",
      "com1",
      "Lpt9",
      "aux.txt",
  };
  static const char* const good[] = { "users", "by region", "a.b", "console", "com0", "émoji ✓" };
  char* dir = make_folder();

  for (usz i = 0; i < ARRAY_LEN(bad); i++)
    if (store_name_ok(bad[i])) FAIL("accepted \"%s\"", bad[i]);

  for (usz i = 0; i < ARRAY_LEN(good); i++)
    if (!store_name_ok(good[i])) FAIL("refused \"%s\"", good[i]);

  char err[256];
  store* s = store_open(dir, err, sizeof(err));
  CHECK(!store_write(s, "../escape", ".", err, sizeof(err)));
  CHECK(strcmp(err, "not a valid name: ../escape") == 0);
  CHECK(!exists(dir, "../escape.query"));
  CHECK(store_count(s) == 0);

  store_free(s);
  remove_folder(dir);
}

static void test_delete(void) {
  char* dir = make_folder();

  put(dir, "a.query", ".a");
  put(dir, "b.query", ".b");

  char err[256];
  store* s = store_open(dir, err, sizeof(err));
  CHECK(store_delete(s, "a", err, sizeof(err)));
  CHECK(!exists(dir, "a.query"));
  CHECK(strcmp(names(s), "b") == 0);

  CHECK(!store_delete(s, "a", err, sizeof(err)));
  CHECK(strstr(err, "a.query: ") != NULL);

  store_free(s);
  remove_folder(dir);
}

static void test_outside_changes(void) {
  char* dir = make_folder();

  put(dir, "a.query", ".a");

  char err[256];
  store* s = store_open(dir, err, sizeof(err));
  CHECK(!store_rescan(s));

  put(dir, "b.query", ".b");
  CHECK(store_rescan(s));
  CHECK(strcmp(names(s), "a,b") == 0);
  CHECK(!store_rescan(s));

  put(dir, "a.query", ".a | keys");
  CHECK(store_rescan(s));
  CHECK(store_stamp(s, 0).size == 9);

  put(dir, "c.txt", "");
  CHECK(!store_rescan(s));

  remove(in(dir, "b.query"));
  CHECK(store_rescan(s));
  CHECK(strcmp(names(s), "a") == 0);

  store_free(s);
  remove_folder(dir);
}

#define TESTS(f)                    \
   f(test_missing_folder)           \
   f(test_lists_query_files)        \
   f(test_read_trims_newlines)      \
   f(test_save_new_and_overwrite)   \
   f(test_bad_names)                \
   f(test_delete)                   \
   f(test_outside_changes)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
