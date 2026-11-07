/* eval tests - probes reporting through the debug callback. */
#include <string.h>

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
static void test_clone(void);

static eval_result probe(eval* e, const char* q, usz cap) {
  complete_ctx c;
  eval_result r = { 0 };

  complete_at(q, strlen(q), strlen(q), &c);
  u64 id = eval_probe(e, c.probe, cap);
  complete_free(&c);
  if (!eval_wait(e, id, WAIT_MS) || !eval_take(e, &r)) FAIL("%s: no result", q);

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

static void test_clone(void) {
  jv v = jv_parse(INPUT);
  jv c = eval_clone(jv_copy(v));

  CHECK(jv_equal(jv_copy(v), jv_copy(c)));
  CHECK(!jv_identical(jv_object_get(jv_copy(v), jv_string("users")), jv_object_get(jv_copy(c), jv_string("users"))));
  jv_free(v);
  jv_free(c);
}

#define TESTS(f)                \
   f(test_probe_hits)           \
   f(test_probe_cap_stops)      \
   f(test_probe_compile_error)  \
   f(test_probe_ignores_debug)  \
   f(test_clone)
#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
