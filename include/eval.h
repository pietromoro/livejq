/* livejq - runs queries on a worker thread. */
#ifndef LIVEJQ_EVAL_H
#define LIVEJQ_EVAL_H

#include <jv.h>

#include "defines.h"

typedef struct eval eval;

typedef struct {
  u64 id;
  b8 compile_failed;
  b8 capped;
  jv outputs; /* may share values with the inputs */
  jv error;   /* first error, string or null */
  i64 elapsed_us;
} eval_result;

eval* eval_new(void);
void eval_free(eval* e);

/* Inputs and args are handed over, and take effect from the next submitted
 * query. Values cross threads freely: jq's reference counts and lazy caches
 * are made atomic by build/patches/jq/jv.c.patch. */
void eval_set_inputs(eval* e, jv inputs);
void eval_set_args(eval* e, jv named);

/* Cancels any running query. Returns the request id. */
u64 eval_submit(eval* e, const char* query, usz cap);

/* Runs query on the first input once no query is pending, cancelling the
 * running probe; a query submitted meanwhile stops it and it runs again
 * after. Its outputs are dropped: the result's outputs hold instead what it
 * handed to the debug callback tagged with COMPLETE_TAG, the tag left out,
 * up to cap of them. Returns the probe's id, counted apart from queries'. */
u64 eval_probe(eval* e, const char* query, usz cap);

/* The newest finished result not taken yet; older ones are dropped. */
b8 eval_take(eval* e, eval_result* out);
/* Waits until the result of request id is ready to take. */
b8 eval_wait(eval* e, u64 id, i64 timeout_ms);
/* The same for probes. */
b8 eval_take_probe(eval* e, eval_result* out);
b8 eval_wait_probe(eval* e, u64 id, i64 timeout_ms);
void eval_result_free(eval_result* r);

#endif /* LIVEJQ_EVAL_H */
