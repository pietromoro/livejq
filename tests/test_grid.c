/* grid tests - characters as wide as a terminal draws them. */
#include <string.h>

#include "defines.h"
#include "editor.h"
#include "grid.h"

#include "test_helper.in"

#define CJK   "\xe4\xb8\xad" /* 中, two columns */
#define EMOJI "\xf0\x9f\x98\x80" /* 😀, two columns */
#define ACUTE "\xcc\x81" /* a combining acute accent, none */

static const char* row(grid* g, i32 y);
static void test_widths(void);
static void test_wide_put(void);
static void test_wide_clipped(void);
static void test_wide_overwritten(void);
static void test_wide_fit(void);
static void test_editor_wide(void);
static void test_marks(void);

/* row y with each tail as '>' and each blank as '.'. */
static const char* row(grid* g, i32 y) {
  static char buf[256];
  usz n = 0;

  for (i32 x = 0; x < g->w; x++) {
    u32 c = grid_at(g, x, y).ch;

    if (c == GRID_TAIL) buf[n++] = '>';
    else if (c == ' ') buf[n++] = '.';
    else n += utf8_put(c, buf + n);
  }

  buf[n] = '\0';
  return buf;
}

static void test_widths(void) {
  ASSERT_EQ(utf8_width('a'), 1);
  ASSERT_EQ(utf8_width(0xe8), 1);
  ASSERT_EQ(utf8_width(0x4e2d), 2);
  ASSERT_EQ(utf8_width(0x1f600), 2);
  ASSERT_EQ(utf8_width(0xff21), 2);
  ASSERT_EQ(utf8_width(0x301), 0);
  ASSERT_EQ(utf8_width(0x200b), 0);
  ASSERT_EQ(utf8_width(0x2502), 1);
  ASSERT_EQ(utf8_columns("a" CJK "b" EMOJI "e" ACUTE, 12), 7);
}

static void test_wide_put(void) {
  grid* g = grid_new(8, 1);

  ASSERT_EQ(grid_put(g, 0, 0, 8, "a" CJK "b" ACUTE EMOJI, 11, STYLE_NORMAL), 6);
  ASSERT_STREQ(row(g, 0), "a" CJK ">b" EMOJI ">..");

  char buf[64];
  grid_line(g, 0, buf, sizeof(buf));
  ASSERT_STREQ(buf, "a" CJK "b" ACUTE EMOJI);
  grid_free(g);
}

/* a wide character with one column left becomes a blank, and one cut by
 * the left edge leaves its tail column blank. */
static void test_wide_clipped(void) {
  grid* g = grid_new(4, 1);

  ASSERT_EQ(grid_put(g, 0, 0, 4, "ab" CJK CJK, 8, STYLE_NORMAL), 4);
  ASSERT_STREQ(row(g, 0), "ab" CJK ">");
  ASSERT_EQ(grid_put(g, 0, 0, 3, "ab" CJK, 5, STYLE_NORMAL), 3);
  ASSERT_STREQ(row(g, 0), "ab..");
  ASSERT_EQ(grid_put(g, -1, 0, 4, CJK "x", 4, STYLE_NORMAL), 2);
  ASSERT_STREQ(row(g, 0), ".x..");
  grid_free(g);
}

/* writing over either half of a wide character blanks the other. */
static void test_wide_overwritten(void) {
  grid* g = grid_new(6, 1);

  grid_put(g, 0, 0, 6, CJK CJK CJK, 9, STYLE_NORMAL);
  grid_put(g, 1, 0, 6, "x", 1, STYLE_NORMAL);
  ASSERT_STREQ(row(g, 0), ".x" CJK ">" CJK ">");
  grid_put(g, 2, 0, 6, "y", 1, STYLE_NORMAL);
  ASSERT_STREQ(row(g, 0), ".xy." CJK ">");
  grid_put(g, 3, 0, 6, CJK, 3, STYLE_NORMAL);
  ASSERT_STREQ(row(g, 0), ".xy" CJK ">.");
  grid_fill_span(g, 3, 4, 0, STYLE_NORMAL);
  ASSERT_STREQ(row(g, 0), ".xy...");
  grid_free(g);
}

static void test_wide_fit(void) {
  grid* g = grid_new(6, 1);

  ASSERT_EQ(grid_put_fit(g, 0, 0, 6, CJK CJK CJK "a", 10, STYLE_NORMAL), 6);
  ASSERT_STREQ(row(g, 0), CJK ">" CJK ">.\xe2\x80\xa6");
  ASSERT_EQ(grid_put_fit(g, 0, 0, 6, CJK CJK CJK, 9, STYLE_NORMAL), 6);
  ASSERT_STREQ(row(g, 0), CJK ">" CJK ">" CJK ">");
  grid_free(g);
}

/* the cursor lands after the columns of the text before it, scrolled or
 * not. */
static void test_editor_wide(void) {
  grid* g = grid_new(6, 1);
  editor e = { 0 };

  editor_set(&e, CJK CJK);
  ASSERT_EQ(editor_render(&e, g, 0, 0, 6, NULL), 4);
  ASSERT_STREQ(row(g, 0), CJK ">" CJK ">..");
  editor_left(&e);
  ASSERT_EQ(editor_render(&e, g, 0, 0, 6, NULL), 2);

  editor_set(&e, "a" CJK CJK CJK);
  grid_clear(g);
  ASSERT_EQ(editor_render(&e, g, 0, 0, 6, NULL), 5);
  ASSERT_STREQ(row(g, 0), "." CJK ">" CJK ">.");
  editor_free(&e);
  grid_free(g);
}

/* a mark joins the character before it, two at most, and goes with it. */
static void test_marks(void) {
  grid* g = grid_new(6, 1);
  char buf[64];

  ASSERT_EQ(grid_put(g, 0, 0, 6, ACUTE "e" ACUTE ACUTE ACUTE CJK ACUTE, 14, STYLE_NORMAL), 3);
  ASSERT_STREQ(row(g, 0), "e" CJK ">...");
  ASSERT_EQ(grid_at(g, 0, 0).marks[0], 0x301);
  ASSERT_EQ(grid_at(g, 0, 0).marks[1], 0x301);
  ASSERT_EQ(grid_at(g, 1, 0).marks[0], 0x301);
  grid_line(g, 0, buf, sizeof(buf));
  ASSERT_STREQ(buf, "e" ACUTE ACUTE CJK ACUTE);

  grid_put(g, 0, 0, 6, "x", 1, STYLE_NORMAL);
  ASSERT_EQ(grid_at(g, 0, 0).marks[0], 0);
  grid_put(g, 2, 0, 6, "y", 1, STYLE_NORMAL);
  ASSERT_EQ(grid_at(g, 1, 0).marks[0], 0);
  grid_line(g, 0, buf, sizeof(buf));
  ASSERT_STREQ(buf, "x y");
  grid_free(g);
}

#define TESTS(f)            \
   f(test_widths)           \
   f(test_wide_put)         \
   f(test_wide_clipped)     \
   f(test_wide_overwritten) \
   f(test_wide_fit)         \
   f(test_editor_wide)      \
   f(test_marks)
#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
