/* editor tests - cursor moves, edits and horizontal scrolling. */
#include <string.h>

#include "defines.h"
#include "editor.h"
#include "grid.h"

#include "test_helper.in"

#define W 12

static void type(editor* e, const char* s);
static const char* render(editor* e, i32* cursor);
static void test_insert_at_cursor(void);
static void test_backspace(void);
static void test_multibyte(void);
static void test_word_moves(void);
static void test_deletions(void);
static void test_scroll_follows_cursor(void);
static void test_scroll_comes_back(void);

static grid* screen;

static void type(editor* e, const char* s) { while (*s) editor_insert(e, (u8)*s++); }

/* the text drawn after a 2-column label, as the query box does. */
static const char* render(editor* e, i32* cursor) {
  static char buf[W * 4 + 1];

  if (!screen) screen = grid_new(W, 1);
  grid_clear(screen);
  grid_puts(screen, 0, 0, "> ", STYLE_LABEL);
  *cursor = editor_render(e, screen, 2, 0, W, NULL);
  grid_line(screen, 0, buf, sizeof(buf));
  return buf;
}

static void test_insert_at_cursor(void) {
  editor e = { 0 };
  i32 at;

  editor_set(&e, ".a");
  ASSERT_EQ(e.cursor, 2);
  editor_home(&e);
  type(&e, "[]");
  ASSERT_STREQ(e.text, "[].a");
  ASSERT_EQ(e.cursor, 2);
  editor_end(&e);
  type(&e, "b");
  ASSERT_STREQ(e.text, "[].ab");
  editor_left(&e);
  editor_left(&e);
  ASSERT_STREQ(render(&e, &at), "> [].ab");
  ASSERT_EQ(at, 5);
  editor_free(&e);
}

static void test_backspace(void) {
  editor e = { 0 };

  editor_set(&e, "abc");
  editor_left(&e);
  CHECK(editor_backspace(&e));
  ASSERT_STREQ(e.text, "ac");
  ASSERT_EQ(e.cursor, 1);
  editor_home(&e);
  CHECK(!editor_backspace(&e));
  ASSERT_STREQ(e.text, "ac");
  editor_free(&e);
}

static void test_multibyte(void) {
  editor e = { 0 };
  i32 at;

  editor_set(&e, ".\"\xc3\xa9t\xc3\xa9\"");
  editor_left(&e);
  editor_left(&e);
  ASSERT_EQ(e.cursor, 5);
  editor_insert(&e, 0x1f600);
  ASSERT_STREQ(e.text, ".\"\xc3\xa9t\xf0\x9f\x98\x80\xc3\xa9\"");
  ASSERT_STREQ(render(&e, &at), "> .\"\xc3\xa9t\xf0\x9f\x98\x80\xc3\xa9\"");
  ASSERT_EQ(at, 7);
  CHECK(editor_backspace(&e));
  CHECK(editor_backspace(&e));
  ASSERT_STREQ(e.text, ".\"\xc3\xa9\xc3\xa9\"");
  editor_right(&e);
  ASSERT_EQ(e.cursor, 6);
  editor_free(&e);
}

static void test_word_moves(void) {
  editor e = { 0 };

  editor_set(&e, ".users[0].na\xc3\xafve | $x_1");
  editor_word_left(&e);
  ASSERT_EQ(e.cursor, 20);
  editor_word_left(&e);
  ASSERT_EQ(e.cursor, 10);
  editor_word_left(&e);
  ASSERT_EQ(e.cursor, 7);
  editor_word_left(&e);
  ASSERT_EQ(e.cursor, 1);
  editor_word_left(&e);
  ASSERT_EQ(e.cursor, 0);
  editor_word_left(&e);
  ASSERT_EQ(e.cursor, 0);
  editor_word_right(&e);
  ASSERT_EQ(e.cursor, 6);
  editor_word_right(&e);
  ASSERT_EQ(e.cursor, 8);
  editor_word_right(&e);
  ASSERT_EQ(e.cursor, 16);
  editor_word_right(&e);
  ASSERT_EQ(e.cursor, 23);
  editor_word_right(&e);
  ASSERT_EQ(e.cursor, 23);
  editor_free(&e);
}

static void test_deletions(void) {
  editor e = { 0 };

  editor_set(&e, ".a.bc | keys");
  CHECK(editor_delete_word(&e));
  ASSERT_STREQ(e.text, ".a.bc | ");
  CHECK(editor_delete_word(&e));
  ASSERT_STREQ(e.text, ".a.");
  editor_home(&e);
  CHECK(!editor_delete_word(&e));
  CHECK(editor_delete(&e));
  ASSERT_STREQ(e.text, "a.");
  ASSERT_EQ(e.cursor, 0);
  editor_end(&e);
  CHECK(!editor_delete(&e));
  CHECK(!editor_delete_to_end(&e));
  editor_left(&e);
  CHECK(editor_delete_to_start(&e));
  ASSERT_STREQ(e.text, ".");
  ASSERT_EQ(e.cursor, 0);
  CHECK(editor_delete_to_end(&e));
  ASSERT_STREQ(e.text, "");
  CHECK(!editor_delete_to_start(&e));
  editor_free(&e);
}

/* ten columns of text area, the last one kept free for the cursor. */
static void test_scroll_follows_cursor(void) {
  editor e = { 0 };
  i32 at;

  editor_set(&e, "0123456789abc");
  ASSERT_STREQ(render(&e, &at), "> 456789abc");
  ASSERT_EQ(at, 11);
  editor_home(&e);
  ASSERT_STREQ(render(&e, &at), "> 0123456789");
  ASSERT_EQ(at, 2);
  for (i32 i = 0; i < 10; i++) editor_right(&e);
  ASSERT_STREQ(render(&e, &at), "> 123456789a");
  ASSERT_EQ(at, 11);
  editor_free(&e);
}

static void test_scroll_comes_back(void) {
  editor e = { 0 };
  i32 at;

  editor_set(&e, "0123456789abc");
  render(&e, &at);
  for (i32 i = 0; i < 6; i++) editor_backspace(&e);
  ASSERT_STREQ(render(&e, &at), "> 0123456");
  ASSERT_EQ(at, 9);
  editor_free(&e);
}

#define TESTS(f)                    \
  f(test_insert_at_cursor)          \
  f(test_backspace)                 \
  f(test_multibyte)                 \
  f(test_word_moves)                \
  f(test_deletions)                 \
  f(test_scroll_follows_cursor)     \
  f(test_scroll_comes_back)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
