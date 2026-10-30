/* livejq tests - unit tests for the livejq starter project. */
#include "defines.h"

#include "test_helper.in"

static void test_array_len(void) {
  i32 xs[3];

  ASSERT_EQ(ARRAY_LEN(xs), 3);
  ASSERT_NE(ARRAY_LEN(xs), 0);
}

static void test_primitive_sizes(void) {
  ASSERT_EQ(sizeof(u8), 1);
  ASSERT_EQ(sizeof(u16), 2);
  ASSERT_EQ(sizeof(u32), 4);
  ASSERT_EQ(sizeof(u64), 8);
  ASSERT_EQ(sizeof(i8), 1);
  ASSERT_EQ(sizeof(i16), 2);
  ASSERT_EQ(sizeof(i32), 4);
  ASSERT_EQ(sizeof(i64), 8);
}

#define TESTS(f) \
  f(test_array_len) \
  f(test_primitive_sizes)

#define TEST_HELPER_MAIN

#include "test_helper.in"
