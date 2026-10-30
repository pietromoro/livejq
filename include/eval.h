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
  jv outputs; /* shares nothing with the worker */
  jv error;   /* first error, string or null */
  i64 elapsed_us;
} eval_result;

eval* eval_new(void);
void eval_free(eval* e);

/* jq values are not thread-safe: inputs and args are handed over, and the
 * caller must hold no other reference to them or to anything inside them.
 * They take effect from the next submitted query. */
void eval_set_inputs(eval* e, jv inputs);
void eval_set_args(eval* e, jv named);

/* Cancels any running query. Returns the request id. */
u64 eval_submit(eval* e, const char* query, usz cap);

/* The newest finished result not taken yet; older ones are dropped. */
b8 eval_take(eval* e, eval_result* out);
/* Waits until the result of request id is ready to take. */
b8 eval_wait(eval* e, u64 id, i64 timeout_ms);
void eval_result_free(eval_result* r);

#endif /* LIVEJQ_EVAL_H */
