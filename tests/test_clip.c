/* clip tests - the OSC 52 sequence around base64. */
#include <stdlib.h>
#include <string.h>

#include "clip.h"
#include "defines.h"

#include "test_helper.in"

static void check(const char* s, usz len, const char* seq);
static void test_padding(void);
static void test_bytes(void);

static void check(const char* s, usz len, const char* seq) {
  char* osc = clip_osc52(s, len);

  ASSERT_STREQ(osc, seq);
  free(osc);
}

static void test_padding(void) {
  check("", 0, "\033]52;c;\a");
  check("a", 1, "\033]52;c;YQ==\a");
  check("ab", 2, "\033]52;c;YWI=\a");
  check("abc", 3, "\033]52;c;YWJj\a");
  check("hello", 5, "\033]52;c;aGVsbG8=\a");
}

static void test_bytes(void) {
  check("\xc3\xa8", 2, "\033]52;c;w6g=\a");
  check("\xff\xfe\x00", 3, "\033]52;c;//4A\a");
  check("{\n  \"a\": 1\n}", 12, "\033]52;c;ewogICJhIjogMQp9\a");
}

#define TESTS(f)    \
   f(test_padding)  \
   f(test_bytes)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
