/* views tests - each value in each view, straight from the library. */
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <jv.h>

#include "defines.h"
#include "views.h"

#include "test_helper.in"

#define TOKEN                                                                                                 \
  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJ1PjE_IiwiaWF0IjoxNzMwMzAwMDAwLCJleHAiOjE3MzAzMDM2MDAsIm4i" \
  "OiJcdTAwZTgifQ.c2ln_-"
#define NOW_MS 1730300000000 /* 2024-10-30T14:53:20Z */

static const char* shown(const char* json, view_id id);
static const char* shown_in(const char* json, view_id id, time_unit unit);
static const char* offered(const char* json);
static void test_bases(void);
static void test_negative_and_huge(void);
static void test_exponent(void);
static void test_bytes(void);
static void test_duration(void);
static void test_time(void);
static void test_relative(void);
static void test_units(void);
static void test_iso_strings(void);
static void test_rfc2822_strings(void);
static void test_base64(void);
static void test_url(void);
static void test_unhex(void);
static void test_uuid(void);
static void test_json(void);
static void test_jwt(void);
static void test_offered(void);
static void test_names(void);

/* json parsed, then in view id; "-" when the view does not apply. */
static const char* shown(const char* json, view_id id) { return shown_in(json, id, UNIT_AUTO); }

/* shown, with times read in unit. */
static const char* shown_in(const char* json, view_id id, time_unit unit) {
  static char buf[128];
  jv v = jv_parse(json);
  view_ctx ctx = { NOW_MS, unit };
  jv text = view_text(v, id, &ctx);

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

static void test_relative(void) {
  ASSERT_STREQ(shown("1730300000", VIEW_AGO), "now");
  ASSERT_STREQ(shown("1730299995", VIEW_AGO), "5s ago");
  ASSERT_STREQ(shown("1730299910", VIEW_AGO), "1m 30s ago");
  ASSERT_STREQ(shown("1730120000", VIEW_AGO), "2d 2h ago");
  ASSERT_STREQ(shown("1730127200", VIEW_AGO), "2d ago");
  ASSERT_STREQ(shown("1730300302", VIEW_AGO), "in 5m 2s");
  ASSERT_STREQ(shown("1730299999500", VIEW_AGO), "1s ago");
  ASSERT_STREQ(shown("1730300000999", VIEW_AGO), "now");
  ASSERT_STREQ(shown("0", VIEW_AGO), "20026d 14h ago");
  ASSERT_STREQ(shown("1.5", VIEW_AGO), "-");
}

static void test_units(void) {
  ASSERT_STREQ(shown_in("1730300000", VIEW_UTC, UNIT_S), "2024-10-30T14:53:20Z");
  ASSERT_STREQ(shown_in("1730300000", VIEW_UTC, UNIT_MS), "1970-01-21T00:38:20.000Z");
  ASSERT_STREQ(shown_in("1730300000", VIEW_UTC, UNIT_US), "1970-01-01T00:28:50.300000Z");
  ASSERT_STREQ(shown_in("1730300000", VIEW_UTC, UNIT_NS), "1970-01-01T00:00:01.730300000Z");
  ASSERT_STREQ(shown_in("1730300000000", VIEW_UTC, UNIT_S), "-");
  ASSERT_STREQ(shown_in("1730300000000", VIEW_AGO, UNIT_MS), "now");
  ASSERT_STREQ(shown_in("1730300000", VIEW_AGO, UNIT_MS), "20006d 14h ago");
  ASSERT_STREQ(shown_in("1730300000", VIEW_HEX, UNIT_MS), "0x67224860");
  ASSERT_STREQ(unit_name(UNIT_US), "\xc2\xb5s");
  ASSERT_STREQ(unit_name(UNIT_AUTO), "");
}

static void test_iso_strings(void) {
  ASSERT_STREQ(shown("\"2024-10-30T14:53:20Z\"", VIEW_UTC), "2024-10-30T14:53:20Z");
  ASSERT_STREQ(shown("\"2024-10-30t15:53:20+01:00\"", VIEW_UTC), "2024-10-30T14:53:20Z");
  ASSERT_STREQ(shown("\"2024-10-30T11:23:20-0330\"", VIEW_UTC), "2024-10-30T14:53:20Z");
  ASSERT_STREQ(shown("\"2024-10-30 14:53:20.5Z\"", VIEW_UTC), "2024-10-30T14:53:20.500Z");
  ASSERT_STREQ(shown("\"2024-10-30T14:53:20,1234Z\"", VIEW_UTC), "2024-10-30T14:53:20.123400Z");
  ASSERT_STREQ(shown("\"2024-10-30T14:53:20.1234567891Z\"", VIEW_UTC), "2024-10-30T14:53:20.123456789Z");
  ASSERT_STREQ(shown("\"2024-10-30T14:53\"", VIEW_UTC), "2024-10-30T14:53:00Z");
  ASSERT_STREQ(shown("\"2024-10-30\"", VIEW_UTC), "2024-10-30T00:00:00Z");
  ASSERT_STREQ(shown("\"2024-02-29\"", VIEW_UTC), "2024-02-29T00:00:00Z");
  ASSERT_STREQ(shown("\"2024-10-30T14:51:50Z\"", VIEW_AGO), "1m 30s ago");
  ASSERT_STREQ(shown("\"2023-02-29\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"2024-13-01\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"2024-10-30T24:00Z\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"2024-10-30T14:53:20.Z\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"2024-10-30T14:53:20+1\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"2024-10-30 \"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"2024-10-30x\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"24-10-30\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"2024-10-30\"", VIEW_HEX), "-");
  ASSERT_STREQ(shown_in("\"2024-10-30\"", VIEW_UTC, UNIT_MS), "2024-10-30T00:00:00Z");
}

static void test_rfc2822_strings(void) {
  ASSERT_STREQ(shown("\"Wed, 30 Oct 2024 14:53:20 GMT\"", VIEW_UTC), "2024-10-30T14:53:20Z");
  ASSERT_STREQ(shown("\"30 Oct 2024 15:53:20 +0100\"", VIEW_UTC), "2024-10-30T14:53:20Z");
  ASSERT_STREQ(shown("\"Wed,  30 Oct 2024 10:53 EDT\"", VIEW_UTC), "2024-10-30T14:53:00Z");
  ASSERT_STREQ(shown("\"Sat, 2 Nov 2024 06:00:00 PST\"", VIEW_UTC), "2024-11-02T14:00:00Z");
  ASSERT_STREQ(shown("\"30 Oct 2024 14:53:20\"", VIEW_UTC), "2024-10-30T14:53:20Z");
  ASSERT_STREQ(shown("\"Wed, 30 Oct 2024 14:51:50 UT\"", VIEW_AGO), "1m 30s ago");
  ASSERT_STREQ(shown("\"Wed, 31 Feb 2024 14:53:20 GMT\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"Wed, 30 oct 2024 14:53:20 GMT\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"Wed 30 Oct 2024 14:53:20 GMT\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"30 Oct 24 14:53:20 GMT\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"30 Oct 2024 14:53:20 CET\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"30 Oct 2024 14:53:20 \"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"30 Oct 2024\"", VIEW_UTC), "-");
}

static void test_base64(void) {
  ASSERT_STREQ(shown("\"aGVsbG8gd29ybGQ=\"", VIEW_BASE64), "\"hello world\"");
  ASSERT_STREQ(shown("\"aGVsbG8gd29ybGQ\"", VIEW_BASE64), "\"hello world\"");
  ASSERT_STREQ(shown("\"YT4/fn5+\"", VIEW_BASE64), "\"a>?~~~\"");
  ASSERT_STREQ(shown("\"YT4_fn5-\"", VIEW_BASE64), "\"a>?~~~\"");
  ASSERT_STREQ(shown("\"bGluZQpicmVhaw==\"", VIEW_BASE64), "\"line\\nbreak\"");
  ASSERT_STREQ(shown("\"w6h0w6A=\"", VIEW_BASE64), "\"\xc3\xa8t\xc3\xa0\"");
  ASSERT_STREQ(shown("\"3q2+7wABAgMEBQYHCAkKCw==\"", VIEW_BASE64), "de ad be ef 00 01 02 03 04 05 06 07 08 09 0a 0b");
  ASSERT_STREQ(shown("\"3q2+7w==\"", VIEW_BASE64), "-");
  ASSERT_STREQ(shown("\"test\"", VIEW_BASE64), "-");
  ASSERT_STREQ(shown("\"YT4/fn5-\"", VIEW_BASE64), "-");
  ASSERT_STREQ(shown("\"aGVsbG8=x\"", VIEW_BASE64), "-");
  ASSERT_STREQ(shown("\"aGVsbG8\"", VIEW_BASE64), "\"hello\"");
  ASSERT_STREQ(shown("\"aGVsbG9\"", VIEW_BASE64), "-");
  ASSERT_STREQ(shown("\"aGVsbG\"", VIEW_BASE64), "-");
  ASSERT_STREQ(shown("\"aGVsbA=\"", VIEW_BASE64), "-");
  ASSERT_STREQ(shown("\"hello world!\"", VIEW_BASE64), "-");
  ASSERT_STREQ(shown("\"\"", VIEW_BASE64), "-");
  ASSERT_STREQ(shown("42", VIEW_BASE64), "-");
}

static void test_url(void) {
  ASSERT_STREQ(shown("\"hello%20world\"", VIEW_URL), "\"hello world\"");
  ASSERT_STREQ(shown("\"a%2Fb%2fc+d\"", VIEW_URL), "\"a/b/c+d\"");
  ASSERT_STREQ(shown("\"%C3%A8t%C3%A0\"", VIEW_URL), "\"\xc3\xa8t\xc3\xa0\"");
  ASSERT_STREQ(shown("\"https://x.io/?q=%22a%22\"", VIEW_URL), "\"https://x.io/?q=\\\"a\\\"\"");
  ASSERT_STREQ(shown("\"line%0Abreak\"", VIEW_URL), "\"line\\nbreak\"");
  ASSERT_STREQ(shown("\"plain\"", VIEW_URL), "-");
  ASSERT_STREQ(shown("\"100%\"", VIEW_URL), "-");
  ASSERT_STREQ(shown("\"50%2\"", VIEW_URL), "-");
  ASSERT_STREQ(shown("\"%zz\"", VIEW_URL), "-");
  ASSERT_STREQ(shown("\"%00\"", VIEW_URL), "-");
  ASSERT_STREQ(shown("\"%C3\"", VIEW_URL), "-");
}

static void test_unhex(void) {
  ASSERT_STREQ(shown("\"68656c6c6f\"", VIEW_UNHEX), "\"hello\"");
  ASSERT_STREQ(shown("\"0x48494A4b\"", VIEW_UNHEX), "\"HIJK\"");
  ASSERT_STREQ(shown("\"6869\"", VIEW_UNHEX), "-");
  ASSERT_STREQ(shown("\"deadbeef\"", VIEW_UNHEX), "de ad be ef");
  ASSERT_STREQ(shown("\"0xdeadbeef\"", VIEW_UNHEX), "de ad be ef");
  ASSERT_STREQ(shown("\"dead\"", VIEW_UNHEX), "-");
  ASSERT_STREQ(shown("\"1234\"", VIEW_UNHEX), "-");
  ASSERT_STREQ(shown("\"deadbee\"", VIEW_UNHEX), "-");
  ASSERT_STREQ(shown("\"deadbeeg\"", VIEW_UNHEX), "-");
  ASSERT_STREQ(shown("\"0x\"", VIEW_UNHEX), "-");
  ASSERT_STREQ(shown("\"d41d8cd98f00b204e9800998ecf8427e\"", VIEW_BASE64), "-");
}

static void test_uuid(void) {
  ASSERT_STREQ(shown("\"919108f7-52d1-4320-9bac-f847db4148a8\"", VIEW_UUID), "v4");
  ASSERT_STREQ(shown("\"017F22E2-79B0-7CC3-98C4-DC0C0C07398F\"", VIEW_UUID), "v7 2022-02-22T19:22:22.000Z");
  ASSERT_STREQ(shown("\"C232AB00-9414-11EC-B3C8-9F6BDECED846\"", VIEW_UUID), "v1 2022-02-22T19:22:22.0000000Z");
  ASSERT_STREQ(shown("\"1EC9414C-232A-6B00-B3C8-9F6BDECED846\"", VIEW_UUID), "v6 2022-02-22T19:22:22.0000000Z");
  ASSERT_STREQ(shown("\"00000000-0000-0000-0000-000000000000\"", VIEW_UUID), "nil");
  ASSERT_STREQ(shown("\"FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF\"", VIEW_UUID), "max");
  ASSERT_STREQ(shown("\"919108f7-52d1-4320-cbac-f847db4148a8\"", VIEW_UUID), "microsoft");
  ASSERT_STREQ(shown("\"919108f7-52d1-4320-1bac-f847db4148a8\"", VIEW_UUID), "ncs");
  ASSERT_STREQ(shown("\"919108f752d143209bacf847db4148a8\"", VIEW_UUID), "-");
  ASSERT_STREQ(shown("\"919108f7-52d1-4320-9bac-f847db4148ag\"", VIEW_UUID), "-");
  ASSERT_STREQ(shown("\"919108f7-52d14-320-9bac-f847db4148a8\"", VIEW_UUID), "-");
  ASSERT_STREQ(shown("42", VIEW_UUID), "-");
  ASSERT_STREQ(shown("\"017F22E2-79B0-7CC3-98C4-DC0C0C07398F\"", VIEW_UTC), "2022-02-22T19:22:22.000Z");
  ASSERT_STREQ(shown("\"017F22E2-79B0-7CC3-98C4-DC0C0C07398F\"", VIEW_AGO), "980d 19h ago");
  ASSERT_STREQ(shown("\"C232AB00-9414-11EC-B3C8-9F6BDECED846\"", VIEW_UTC), "2022-02-22T19:22:22.0000000Z");
  ASSERT_STREQ(shown("\"919108f7-52d1-4320-9bac-f847db4148a8\"", VIEW_UTC), "-");
  ASSERT_STREQ(shown("\"919108f7-52d1-4320-9bac-f847db4148a8\"", VIEW_BASE64), "-");
}

static void test_json(void) {
  jv v = jv_parse("\"{\\\"a\\\": [1, 2]}\"");
  jv tree = view_tree(v, VIEW_JSON);

  ASSERT_EQ(jv_get_kind(tree), JV_KIND_OBJECT);
  CHECK(jv_equal(tree, jv_parse("{\"a\": [1, 2]}")));
  ASSERT_STREQ(shown("\"{\\\"a\\\": 1}\"", VIEW_JSON), "-");
  ASSERT_STREQ(view_jq(VIEW_JSON), "fromjson");
  CHECK(view_jq(VIEW_UUID) == NULL);
  CHECK(view_applies(jv_string("[]"), VIEW_JSON));
  CHECK(!view_applies(jv_string("42"), VIEW_JSON));
  CHECK(!view_applies(jv_string("\"x\""), VIEW_JSON));
  CHECK(!view_applies(jv_string("{\"a\":"), VIEW_JSON));
  CHECK(!view_applies(jv_string("[1] 2"), VIEW_JSON));
  CHECK(!view_applies(jv_number(1), VIEW_JSON));
  jv_free(v);
}

static void test_jwt(void) {
  jv tree = view_tree(jv_string(TOKEN), VIEW_JWT);
  const char* json = "{\"header\": {\"alg\": \"HS256\", \"typ\": \"JWT\"}, \"payload\":"
                     " {\"sub\": \"u>1?\", \"iat\": 1730300000, \"exp\": 1730303600, \"n\": \"\xc3\xa8\"}}";
  jv want = jv_parse(json);

  CHECK(jv_equal(tree, want));
  CHECK(view_applies(jv_string("eyJhbGciOiJub25lIn0.eyJhIjoxfQ."), VIEW_JWT));
  CHECK(!view_applies(jv_string("eyJhbGciOiJub25lIn0.eyJhIjoxfQ"), VIEW_JWT));
  CHECK(!view_applies(jv_string("eyJhbGciOiJub25lIn0.eyJhIjoxfQ.a.b"), VIEW_JWT));
  CHECK(!view_applies(jv_string("eyJhbGciOiJub25lIn0.eyJhIjoxfQ.a+b"), VIEW_JWT));
  CHECK(!view_applies(jv_string("eyJhbGciOiJub25lIn0.WzFd.x"), VIEW_JWT));
  CHECK(!view_applies(jv_string("a.b.c"), VIEW_JWT));
  CHECK(!view_applies(jv_string(".."), VIEW_JWT));
  ASSERT_STREQ(view_name(VIEW_JWT), "jwt");
}

static void test_offered(void) {
  ASSERT_STREQ(offered("42"), "default hex oct bin exp iec si dur utc local date ago");
  ASSERT_STREQ(offered("1.5"), "default exp dur");
  ASSERT_STREQ(offered("0"), "default hex oct bin iec si dur utc local date ago");
  ASSERT_STREQ(offered("\"hello\""), "default");
  ASSERT_STREQ(offered("\"2024-10-30\""), "default utc local date ago");
  ASSERT_STREQ(offered("\"aGVsbG8gd29ybGQ=\""), "default b64");
  ASSERT_STREQ(offered("\"a%20b\""), "default url");
  ASSERT_STREQ(offered("\"d41d8cd98f00b204e9800998ecf8427e\""), "default unhex");
  ASSERT_STREQ(offered("\"919108f7-52d1-4320-9bac-f847db4148a8\""), "default uuid");
  ASSERT_STREQ(offered("\"017f22e2-79b0-7cc3-98c4-dc0c0c07398f\""), "default utc local date ago uuid");
  ASSERT_STREQ(offered("\"{\\\"a\\\": 1}\""), "default json");
  ASSERT_STREQ(offered("\"" TOKEN "\""), "default jwt");
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
  f(test_relative)             \
  f(test_units)                \
  f(test_iso_strings)          \
  f(test_rfc2822_strings)      \
  f(test_base64)               \
  f(test_url)                  \
  f(test_unhex)                \
  f(test_uuid)                 \
  f(test_json)                 \
  f(test_jwt)                  \
  f(test_offered)              \
  f(test_names)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
