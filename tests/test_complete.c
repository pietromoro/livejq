/* complete tests - the word at the cursor, and what its probe reports. */
#include <stdlib.h>
#include <string.h>

#include <jq.h>

#include "complete.h"
#include "defines.h"

#include "test_helper.in"

#define INPUT "{\"users\": [{\"name\": \"a\", \"age\": 1}, {\"name\": \"b\", \"tags\": []}], \"n\": 2}"

static complete_ctx at_end(const char* q);
static const char* reported(const char* probe);
static void collect(void* data, jv msg);
static void quiet(void* data, jv msg);
static void test_kinds(void);
static void test_word_bounds(void);
static void test_probe_paths(void);
static void test_probe_inner_inputs(void);
static void test_probe_closes(void);
static void test_fallback(void);

static complete_ctx at_end(const char* q) {
  complete_ctx c;

  complete_at(q, strlen(q), strlen(q), &c);
  return c;
}

/* the keys the probe reports on INPUT, as "key:type ..." in the order seen,
 * or "!" when it does not compile. */
static const char* reported(const char* probe) {
  static char buf[512];
  jq_state* jq = jq_init();
  jv seen = jv_array();

  buf[0] = '\0';
  jq_set_debug_cb(jq, collect, &seen);
  jq_set_error_cb(jq, quiet, NULL);
  if (!jq_compile(jq, probe)) {
    jq_teardown(&jq);
    jv_free(seen);
    return "!";
  }

  jq_start(jq, jv_parse(INPUT), 0);
  jv v;
  while (jv_is_valid(v = jq_next(jq))) jv_free(v);
  jv_free(v);

  jv_array_foreach(seen, i, hit) {
    jv_array_foreach(jv_array_get(jv_copy(hit), 1), k, pair) {
      jv key = jv_array_get(jv_copy(pair), 0);
      jv type = jv_array_get(pair, 1);
      usz n = strlen(buf);

      snprintf(
        buf + n,
        sizeof(buf) - n,
        "%s%s:%s",
        n ? " " : "",
        jv_string_value(key),
        jv_string_value(type)
      );
      jv_free(key);
      jv_free(type);
      UNUSED(k);
    }

    jv_free(hit);
    UNUSED(i);
  }

  jv_free(seen);
  jq_teardown(&jq);
  return buf;
}

static void collect(void* data, jv msg) {
  jv* seen = data;
  jv tag = jv_array_get(jv_copy(msg), 0);

  if (jv_get_kind(tag) == JV_KIND_STRING && strcmp(jv_string_value(tag), COMPLETE_TAG) == 0)
    *seen = jv_array_append(*seen, msg);
  else jv_free(msg);

  jv_free(tag);
}

static void quiet(void* data, jv msg) {
  UNUSED(data);
  jv_free(msg);
}

static void test_kinds(void) {
  static const struct {
    const char* q;
    complete_kind kind;
  } cases[] = {
      { ".", COMPLETE_KEY },
      { ".us", COMPLETE_KEY },
      { ".users[0].na", COMPLETE_KEY },
      { "$x.a", COMPLETE_KEY },
      { "..", COMPLETE_NONE },
      { "1.5", COMPLETE_NONE },
      { "1.", COMPLETE_NONE },
      { "\"a.b", COMPLETE_NONE },
      { ". # .a", COMPLETE_NONE },
      { "$na", COMPLETE_NONE },
      { "@bas", COMPLETE_NONE },
      { "ma", COMPLETE_BUILTIN },
      { ".a | ma", COMPLETE_BUILTIN },
      { "map(sel", COMPLETE_BUILTIN },
      { "if . then le", COMPLETE_BUILTIN },
      { ".a | ", COMPLETE_BUILTIN },
      { "{na", COMPLETE_NONE },
      { "{a: 1, na", COMPLETE_NONE },
      { "{a: le", COMPLETE_BUILTIN },
      { ".a le", COMPLETE_NONE },
      { "def le", COMPLETE_NONE },
      { "\"\\(.a", COMPLETE_KEY },
  };

  for (usz k = 0; k < ARRAY_LEN(cases); k++) {
    complete_ctx c = at_end(cases[k].q);

    if (c.kind != cases[k].kind) FAIL("%s: kind %d, want %d", cases[k].q, c.kind, cases[k].kind);
    else CHECK(1);

    complete_free(&c);
  }
}

static void test_word_bounds(void) {
  complete_ctx c;

  complete_at(".users[0].name | x", 18, 12, &c);
  CHECK(c.kind == COMPLETE_KEY);
  CHECK(c.start == 10);
  CHECK(c.end == 14);
  complete_free(&c);

  c = at_end(".a | ma");
  CHECK(c.start == 5);
  CHECK(c.end == 7);
  complete_free(&c);

  c = at_end(".");
  CHECK(c.start == 1);
  CHECK(c.end == 1);
  CHECK(c.probe != NULL);
  complete_free(&c);
  CHECK(c.probe == NULL);
}

static void test_probe_paths(void) {
  complete_ctx c = at_end(".");
  ASSERT_STREQ(reported(c.probe), "users:array n:number");
  complete_free(&c);

  c = at_end(".users[0].na");
  ASSERT_STREQ(reported(c.probe), "name:string age:number");
  complete_free(&c);

  c = at_end(".users[].");
  ASSERT_STREQ(reported(c.probe), "name:string age:number name:string tags:array");
  complete_free(&c);

  c = at_end(".users | .[1]?.");
  ASSERT_STREQ(reported(c.probe), "name:string tags:array");
  complete_free(&c);

  c = at_end(".n.");
  ASSERT_STREQ(reported(c.probe), "");
  complete_free(&c);
}

static void test_probe_inner_inputs(void) {
  complete_ctx c = at_end(".users | map(.na");
  ASSERT_STREQ(reported(c.probe), "name:string age:number name:string tags:array");
  complete_free(&c);

  c = at_end(".users[] | select(.age");
  ASSERT_STREQ(reported(c.probe), "name:string age:number name:string tags:array");
  complete_free(&c);

  c = at_end("{x: .users[0].");
  ASSERT_STREQ(reported(c.probe), "name:string age:number");
  complete_free(&c);

  c = at_end("[.users[] | .]");
  CHECK(c.kind == COMPLETE_NONE);
  complete_free(&c);

  c = at_end(".users[0] as $u | $u.");
  ASSERT_STREQ(reported(c.probe), "name:string age:number");
  complete_free(&c);
}

static void test_probe_closes(void) {
  complete_ctx c = at_end("if .n then .users[0].");
  ASSERT_STREQ(reported(c.probe), "name:string age:number");
  complete_free(&c);

  c = at_end("if .us");
  ASSERT_STREQ(reported(c.probe), "users:array n:number");
  complete_free(&c);

  c = at_end("if .none then 1 elif .us");
  ASSERT_STREQ(reported(c.probe), "users:array n:number");
  complete_free(&c);

  c = at_end("if .n then 1 end | .us");
  ASSERT_STREQ(reported(c.probe), "");
  complete_free(&c);

  c = at_end("\"\\(.users[1].");
  ASSERT_STREQ(reported(c.probe), "name:string tags:array");
  complete_free(&c);

  c = at_end("[(.users[0].");
  ASSERT_STREQ(reported(c.probe), "name:string age:number");
  complete_free(&c);
}

static void test_fallback(void) {
  complete_ctx c = at_end("def f: .users[0].");
  ASSERT_STREQ(reported(c.probe), "!");
  ASSERT_STREQ(reported(c.fallback), "name:string age:number");
  complete_free(&c);

  c = at_end("def f: .");
  ASSERT_STREQ(reported(c.fallback), "users:array n:number");
  complete_free(&c);
}

#define TESTS(f)               \
   f(test_kinds)               \
   f(test_word_bounds)         \
   f(test_probe_paths)         \
   f(test_probe_inner_inputs)  \
   f(test_probe_closes)        \
   f(test_fallback)
#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
