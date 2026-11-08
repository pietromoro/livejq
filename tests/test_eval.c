/* eval tests - probes reporting through the debug callback. */
#include <string.h>

#include "thread.h"

#include "complete.h"
#include "defines.h"
#include "eval.h"

#include "test_helper.in"

#define WAIT_MS 5000
#define INPUT   "{\"users\": [{\"name\": \"a\"}, {\"name\": \"b\", \"age\": 2}]}"
#define TAGGED  "[\"" COMPLETE_TAG "\", 5], [\"" COMPLETE_TAG "\", [[1, 2]]] "

static eval_result probe(eval* e, const char* q, usz cap);
static void test_probe_hits(void);
static void test_probe_cap_stops(void);
static void test_probe_compile_error(void);
static void test_probe_ignores_debug(void);
static void test_probe_first_input(void);
static void test_query_stops_probe(void);

static eval_result probe(eval* e, const char* q, usz cap) {
  complete_ctx c;
  eval_result r = { 0 };

  complete_at(q, strlen(q), strlen(q), &c);
  u64 id = eval_probe(e, c.probe, cap);
  complete_free(&c);
  if (!eval_wait_probe(e, id, WAIT_MS) || !eval_take_probe(e, &r)) FAIL("%s: no result", q);

  return r;
}

static void test_probe_hits(void) {
  eval* e = eval_new();

  eval_set_inputs(e, JV_ARRAY(jv_parse(INPUT)));

  eval_result r = probe(e, ".users[].", 10);
  jv want = jv_parse("[[[\"name\", \"string\"]], [[\"name\", \"string\"], [\"age\", \"number\"]]]");
  CHECK(!r.compile_failed);
  CHECK(jv_equal(jv_copy(r.outputs), want));
  eval_result_free(&r);
  eval_free(e);
}

static void test_probe_cap_stops(void) {
  eval* e = eval_new();

  eval_set_inputs(e, JV_ARRAY(jv_parse(INPUT)));

  eval_result r = probe(e, "range(1e12) as $i | .users[0].", 3);
  CHECK(jv_array_length(jv_copy(r.outputs)) == 3);
  eval_result_free(&r);
  eval_free(e);
}

static void test_probe_compile_error(void) {
  eval* e = eval_new();

  eval_set_inputs(e, JV_ARRAY(jv_parse(INPUT)));

  eval_result r = probe(e, "def f: .users.", 10);
  CHECK(r.compile_failed);
  CHECK(jv_array_length(jv_copy(r.outputs)) == 0);
  eval_result_free(&r);
  eval_free(e);
}

/* the query's own debug calls and outputs never show up, even tagged like
 * the probe's. */
static void test_probe_ignores_debug(void) {
  eval* e = eval_new();

  eval_set_inputs(e, JV_ARRAY(jv_parse(INPUT)));

  eval_result r = probe(e, "debug | 1, .users[0].", 10);
  jv want = jv_parse("[[[\"name\", \"string\"]]]");
  CHECK(jv_equal(jv_copy(r.outputs), jv_copy(want)));
  eval_result_free(&r);

  r = probe(e, "(" TAGGED "| debug | empty), .users[0].", 10);
  CHECK(jv_equal(jv_copy(r.outputs), want));
  eval_result_free(&r);
  eval_free(e);
}

static void test_probe_first_input(void) {
  eval* e = eval_new();

  eval_set_inputs(e, JV_ARRAY(jv_parse("{\"a\": 1}"), jv_parse("{\"b\": 2}")));

  eval_result r = probe(e, ".", 10);
  jv want = jv_parse("[[[\"a\", \"number\"]]]");
  CHECK(jv_equal(jv_copy(r.outputs), want));
  eval_result_free(&r);
  eval_free(e);
}

/* a slow probe gives way to a query, then runs again to its end. */
static void test_query_stops_probe(void) {
  eval* e = eval_new();
  complete_ctx c;
  const char* slow = "(range(3e6) | empty), .users[0].";

  eval_set_inputs(e, JV_ARRAY(jv_parse(INPUT)));
  complete_at(slow, strlen(slow), strlen(slow), &c);

  u64 p = eval_probe(e, c.probe, 10);
  complete_free(&c);

  u64 q = eval_submit(e, "1", 10);
  i64 start = clock_us();
  CHECK(eval_wait(e, q, WAIT_MS));
  CHECK(!eval_wait_probe(e, p, 0));
  i64 query_us = clock_us() - start;

  CHECK(eval_wait_probe(e, p, WAIT_MS * 4));
  CHECK(clock_us() - start > query_us * 2);

  eval_result r;
  CHECK(eval_take(e, &r));
  ASSERT_EQ(jv_array_length(jv_copy(r.outputs)), 1);
  eval_result_free(&r);
  CHECK(eval_take_probe(e, &r));
  ASSERT_EQ(jv_array_length(jv_copy(r.outputs)), 1);
  eval_result_free(&r);
  eval_free(e);
}

#define TESTS(f)                \
   f(test_probe_hits)           \
   f(test_probe_cap_stops)      \
   f(test_probe_compile_error)  \
   f(test_probe_ignores_debug)  \
   f(test_probe_first_input)    \
   f(test_query_stops_probe)
#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
