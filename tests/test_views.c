/* views tests - each value in each view, straight from the library. */
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <jv.h>

#include "defines.h"
#include "views.h"

#include "test_helper.in"

static const char* shown(const char* json, view_id id);
static const char* offered(const char* json);
static void test_bases(void);
static void test_negative_and_huge(void);
static void test_exponent(void);
static void test_bytes(void);
static void test_duration(void);
static void test_time(void);
static void test_offered(void);
static void test_names(void);

/* json parsed, then in view id; "-" when the view does not apply. */
static const char* shown(const char* json, view_id id) {
  static char buf[128];
  jv v = jv_parse(json);
  jv text = view_text(v, id);

  if (jv_is_valid(text)) snprintf(buf, sizeof(buf), "%s", jv_string_value(text));
  else strcpy(buf, "-");

  jv_free(text);
  jv_free(v);
  return buf;
}

/* the names of the views json is offered in, space separated. */
static const char* offered(const char* json) {
  static char buf[128];
  view_id ids[VIEW_COUNT];
  jv v = jv_parse(json);
  i32 n = views_of(v, ids, VIEW_COUNT);

  buf[0] = '\0';
  for (i32 i = 0; i < n; i++) {
    strcat(buf, i ? " " : "");
    strcat(buf, ids[i] == VIEW_DEFAULT ? "default" : view_name(ids[i]));
  }

  jv_free(v);
  return buf;
}

static void test_bases(void) {
  ASSERT_STREQ(shown("255", VIEW_HEX), "0xff");
  ASSERT_STREQ(shown("255", VIEW_OCTAL), "0o377");
  ASSERT_STREQ(shown("5", VIEW_BINARY), "0b101");
  ASSERT_STREQ(shown("0", VIEW_HEX), "0x0");
  ASSERT_STREQ(shown("0", VIEW_BINARY), "0b0");
  ASSERT_STREQ(shown("1730300000", VIEW_HEX), "0x67224860");
  ASSERT_STREQ(shown("1e3", VIEW_HEX), "0x3e8");
  ASSERT_STREQ(shown("2.0", VIEW_HEX), "0x2");
  ASSERT_STREQ(shown("1.5", VIEW_HEX), "-");
  ASSERT_STREQ(shown("\"ff\"", VIEW_HEX), "-");
}

static void test_negative_and_huge(void) {
  ASSERT_STREQ(shown("-255", VIEW_HEX), "-0xff");
  ASSERT_STREQ(shown("-1", VIEW_BINARY), "-0b1");
  ASSERT_STREQ(shown("-9223372036854775808", VIEW_HEX), "-0x8000000000000000");
  ASSERT_STREQ(shown("9007199254740993", VIEW_HEX), "0x20000000000001");
  ASSERT_STREQ(shown("9223372036854775807", VIEW_OCTAL), "0o777777777777777777777");
  ASSERT_STREQ(shown("18446744073709551616", VIEW_HEX), "-");
  ASSERT_STREQ(shown("1e300", VIEW_HEX), "-");
}

static void test_exponent(void) {
  ASSERT_STREQ(shown("1500", VIEW_EXPONENT), "1.5e3");
  ASSERT_STREQ(shown("0.00012", VIEW_EXPONENT), "1.2e-4");
  ASSERT_STREQ(shown("-7", VIEW_EXPONENT), "-7e0");
  ASSERT_STREQ(shown("1e300", VIEW_EXPONENT), "1e300");
  ASSERT_STREQ(shown("0.1", VIEW_EXPONENT), "1e-1");
  ASSERT_STREQ(shown("3.141592653589793", VIEW_EXPONENT), "3.141592653589793e0");
  ASSERT_STREQ(shown("0", VIEW_EXPONENT), "-");
}

static void test_bytes(void) {
  ASSERT_STREQ(shown("1572864", VIEW_BYTES), "1.5 MiB");
  ASSERT_STREQ(shown("1572864", VIEW_BYTES_SI), "1.57 MB");
  ASSERT_STREQ(shown("1000", VIEW_BYTES), "1000 B");
  ASSERT_STREQ(shown("1000", VIEW_BYTES_SI), "1 kB");
  ASSERT_STREQ(shown("1536", VIEW_BYTES), "1.5 KiB");
  ASSERT_STREQ(shown("1536", VIEW_BYTES_SI), "1.54 kB");
  ASSERT_STREQ(shown("0", VIEW_BYTES), "0 B");
  ASSERT_STREQ(shown("-2048", VIEW_BYTES), "-2 KiB");
  ASSERT_STREQ(shown("123456", VIEW_BYTES), "121 KiB");
  ASSERT_STREQ(shown("1048575", VIEW_BYTES), "1 MiB");
  ASSERT_STREQ(shown("999999", VIEW_BYTES_SI), "1 MB");
  ASSERT_STREQ(shown("9223372036854775807", VIEW_BYTES), "8 EiB");
  ASSERT_STREQ(shown("1.5", VIEW_BYTES), "-");
}

static void test_duration(void) {
  ASSERT_STREQ(shown("3723", VIEW_DURATION), "1h 2m 3s");
  ASSERT_STREQ(shown("90061", VIEW_DURATION), "1d 1h 1m 1s");
  ASSERT_STREQ(shown("3600", VIEW_DURATION), "1h");
  ASSERT_STREQ(shown("86400.5", VIEW_DURATION), "1d 0.5s");
  ASSERT_STREQ(shown("61.25", VIEW_DURATION), "1m 1.25s");
  ASSERT_STREQ(shown("-90", VIEW_DURATION), "-1m 30s");
  ASSERT_STREQ(shown("0.25", VIEW_DURATION), "250ms");
  ASSERT_STREQ(shown("0.9996", VIEW_DURATION), "1s");
  ASSERT_STREQ(shown("0.0000015", VIEW_DURATION), "1.5\xc2\xb5s");
  ASSERT_STREQ(shown("0.0009996", VIEW_DURATION), "1ms");
  ASSERT_STREQ(shown("-0.000000002", VIEW_DURATION), "-2ns");
  ASSERT_STREQ(shown("0", VIEW_DURATION), "0s");
  ASSERT_STREQ(shown("1e300", VIEW_DURATION), "-");
}

static void test_time(void) {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  ASSERT_STREQ(shown("1730300000", VIEW_UTC), "2024-10-30T14:53:20Z");
  ASSERT_STREQ(shown("1730300000", VIEW_LOCAL), "2024-10-30 15:53:20 +01:00");
  ASSERT_STREQ(shown("1720000000", VIEW_LOCAL), "2024-07-03 11:46:40 +02:00");
  ASSERT_STREQ(shown("1768924800", VIEW_DATE), "20 Jan 2026 17:00:00");
  ASSERT_STREQ(shown("1730300000123", VIEW_UTC), "2024-10-30T14:53:20.123Z");
  ASSERT_STREQ(shown("1730300000000123", VIEW_UTC), "2024-10-30T14:53:20.000123Z");
  ASSERT_STREQ(shown("1730300000000000001", VIEW_UTC), "2024-10-30T14:53:20.000000001Z");
  ASSERT_STREQ(shown("0", VIEW_UTC), "1970-01-01T00:00:00Z");
  ASSERT_STREQ(shown("-1", VIEW_UTC), "1969-12-31T23:59:59Z");
  ASSERT_STREQ(shown("951782400", VIEW_UTC), "2000-02-29T00:00:00Z");
  ASSERT_STREQ(shown("-99999999999", VIEW_UTC), "-");
  ASSERT_STREQ(shown("1.5", VIEW_UTC), "-");
  setenv("TZ", "<-0330>3:30", 1);
  tzset();
  ASSERT_STREQ(shown("0", VIEW_LOCAL), "1969-12-31 20:30:00 -03:30");
}

static void test_offered(void) {
  ASSERT_STREQ(offered("42"), "default hex oct bin exp iec si dur utc local date");
  ASSERT_STREQ(offered("1.5"), "default exp dur");
  ASSERT_STREQ(offered("0"), "default hex oct bin iec si dur utc local date");
  ASSERT_STREQ(offered("\"hello\""), "default");
  ASSERT_STREQ(offered("null"), "default");
  ASSERT_STREQ(offered("[1]"), "default");
}

static void test_names(void) {
  ASSERT_STREQ(view_name(VIEW_HEX), "hex");
  ASSERT_STREQ(view_name(VIEW_DEFAULT), "");
}

#define TESTS(f)               \
  f(test_bases)                \
  f(test_negative_and_huge)    \
  f(test_exponent)             \
  f(test_bytes)                \
  f(test_duration)             \
  f(test_time)                 \
  f(test_offered)              \
  f(test_names)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
