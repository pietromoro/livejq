/* eval - the query worker, one jq run at a time, probes when idle. */
#include <stdlib.h>
#include <string.h>

#include <jq.h>

#include "complete.h"
#include "eval.h"
#include "thread.h"

/* the requests of one kind, queries or probes. */
typedef struct {
  u64 submitted;
  u64 done;    /* the last id run to its end */
  char* query; /* of the newest request */
  usz cap;
  i32 cancel;  /* stops its running job */
  b8 ready;
  eval_result result;
} slot;

enum { QUERY = 0, PROBE };

struct eval {
  mutex* lock;
  condvar* changed;
  thread* worker;
  b8 quit;
  slot slots[2]; /* by QUERY and PROBE */
  jv new_inputs;
  jv new_args;

  jv inputs; /* worker only */
  jv args;   /* worker only */
};

typedef struct {
  u64 id;
  char* query;
  usz cap;
  b8 probe;
  i32* cancel;
} job;

/* what a probe has handed to the debug callback so far. */
typedef struct {
  jv hits;
  usz cap;
  b8 full; /* stopped the job on reaching cap */
  i32* cancel;
} probed;

static u64 submit(eval* e, i32 kind, const char* query, usz cap);
static b8 take(eval* e, i32 kind, eval_result* out);
static b8 wait_for(eval* e, i32 kind, u64 id, i64 timeout_ms);
static void worker_main(void* arg);
static i32 next_job(eval* e, job* j);
static b8 run(eval* e, const job* j, eval_result* out);
static void collect_error(void* data, jv msg);
static void collect_hit(void* data, jv msg);
static b8 key_list(jv v);
static void ignore_msg(void* data, jv msg);
static jv error_text(jv msg);

void jq_set_cancel_flag(jq_state* jq, i32* flag);

eval* eval_new(void) {
  eval* e = calloc(1, sizeof(*e));

  if (!e) return NULL;

  e->lock = mutex_new();
  e->changed = condvar_new();
  e->new_inputs = jv_invalid();
  e->new_args = jv_invalid();
  e->inputs = jv_array();
  e->args = jv_object();
  for (i32 k = 0; k < 2; k++) {
    e->slots[k].result.outputs = jv_null();
    e->slots[k].result.error = jv_null();
  }

  e->worker = thread_start(worker_main, e);
  if (!e->lock || !e->changed || !e->worker) abort();
  return e;
}

void eval_free(eval* e) {
  mutex_lock(e->lock);
  e->quit = 1;
  atomic_set(&e->slots[QUERY].cancel, 1);
  atomic_set(&e->slots[PROBE].cancel, 1);
  condvar_broadcast(e->changed);
  mutex_unlock(e->lock);
  thread_join(e->worker);

  for (i32 k = 0; k < 2; k++) {
    free(e->slots[k].query);
    eval_result_free(&e->slots[k].result);
  }

  jv_free(e->new_inputs);
  jv_free(e->new_args);
  jv_free(e->inputs);
  jv_free(e->args);
  condvar_free(e->changed);
  mutex_free(e->lock);
  free(e);
}

void eval_set_inputs(eval* e, jv inputs) {
  mutex_lock(e->lock);
  jv_free(e->new_inputs);
  e->new_inputs = inputs;
  mutex_unlock(e->lock);
}

void eval_set_args(eval* e, jv named) {
  mutex_lock(e->lock);
  jv_free(e->new_args);
  e->new_args = named;
  mutex_unlock(e->lock);
}

u64 eval_submit(eval* e, const char* query, usz cap) { return submit(e, QUERY, query, cap); }

u64 eval_probe(eval* e, const char* query, usz cap) { return submit(e, PROBE, query, cap); }

b8 eval_take(eval* e, eval_result* out) { return take(e, QUERY, out); }
b8 eval_take_probe(eval* e, eval_result* out) { return take(e, PROBE, out); }
b8 eval_wait(eval* e, u64 id, i64 timeout_ms) { return wait_for(e, QUERY, id, timeout_ms); }
b8 eval_wait_probe(eval* e, u64 id, i64 timeout_ms) { return wait_for(e, PROBE, id, timeout_ms); }

void eval_result_free(eval_result* r) {
  jv_free(r->outputs);
  jv_free(r->error);
  r->outputs = jv_null();
  r->error = jv_null();
}

/* a query stops a running probe, which runs again after it. */
static u64 submit(eval* e, i32 kind, const char* query, usz cap) {
  usz n = strlen(query);
  char* copy = malloc(n + 1);
  slot* sl = &e->slots[kind];
  u64 id;

  if (!copy) abort();
  memcpy(copy, query, n + 1);

  mutex_lock(e->lock);
  free(sl->query);
  sl->query = copy;
  sl->cap = cap;
  id = ++sl->submitted;
  atomic_set(&sl->cancel, 1);
  if (kind == QUERY) atomic_set(&e->slots[PROBE].cancel, 1);

  condvar_broadcast(e->changed);
  mutex_unlock(e->lock);
  return id;
}

static b8 take(eval* e, i32 kind, eval_result* out) {
  slot* sl = &e->slots[kind];
  b8 got;

  mutex_lock(e->lock);
  got = sl->ready;
  if (got) {
    *out = sl->result;
    sl->ready = 0;
    sl->result.outputs = jv_null();
    sl->result.error = jv_null();
  }

  mutex_unlock(e->lock);
  return got;
}

static b8 wait_for(eval* e, i32 kind, u64 id, i64 timeout_ms) {
  slot* sl = &e->slots[kind];
  i64 deadline = clock_us() + timeout_ms * 1000;
  b8 done;

  mutex_lock(e->lock);
  while (!(done = sl->ready && sl->result.id >= id)) {
    i64 left = timeout_ms < 0 ? -1 : (deadline - clock_us()) / 1000;

    if (timeout_ms >= 0 && left <= 0) break;

    condvar_wait(e->changed, e->lock, left);
  }

  mutex_unlock(e->lock);
  return done;
}

/* a result is posted only while it is the newest of its kind; a job stopped
 * by a query or by a newer request is left pending. */
static void worker_main(void* arg) {
  eval* e = arg;

  for (;;) {
    mutex_lock(e->lock);

    job j;
    i32 kind;
    while ((kind = next_job(e, &j)) < 0 && !e->quit)
      condvar_wait(e->changed, e->lock, -1);

    if (e->quit) {
      if (kind >= 0) free(j.query);

      mutex_unlock(e->lock);
      return;
    }

    if (jv_is_valid(e->new_inputs)) {
      jv_free(e->inputs);
      e->inputs = e->new_inputs;
      e->new_inputs = jv_invalid();
    }

    if (jv_is_valid(e->new_args)) {
      jv_free(e->args);
      e->args = e->new_args;
      e->new_args = jv_invalid();
    }

    slot* sl = &e->slots[kind];
    atomic_set(&sl->cancel, 0);
    mutex_unlock(e->lock);

    eval_result r;
    b8 finished = run(e, &j, &r);
    mutex_lock(e->lock);
    if (finished && r.id == sl->submitted) {
      eval_result_free(&sl->result);
      sl->result = r;
      sl->ready = 1;
      condvar_broadcast(e->changed);
    } else if (finished) {
      eval_result_free(&r);
    }

    if (finished) sl->done = j.id;

    mutex_unlock(e->lock);
    free(j.query);
  }
}

/* the pending query, else the pending probe, as a job owning a copy of its
 * text. -1 when neither is pending. called locked. */
static i32 next_job(eval* e, job* j) {
  for (i32 kind = QUERY; kind <= PROBE; kind++) {
    slot* sl = &e->slots[kind];

    if (sl->done == sl->submitted) continue;

    j->id = sl->submitted;
    j->query = strdup(sl->query);
    if (!j->query) abort();

    j->cap = sl->cap;
    j->probe = kind == PROBE;
    j->cancel = &sl->cancel;
    return kind;
  }

  return -1;
}

/* false when the job was cancelled and produced nothing worth posting. */
static b8 run(eval* e, const job* j, eval_result* out) {
  i64 start = clock_us();
  jq_state* jq = jq_init();
  jv compile_error = jv_null();
  i32 n_inputs = jv_array_length(jv_copy(e->inputs));
  b8 cancelled = 0;
  probed hits = { jv_array(), j->cap, 0, j->cancel };

  if (!jq) abort();
  if (j->probe && n_inputs > 1) n_inputs = 1;

  out->id = j->id;
  out->compile_failed = 0;
  out->capped = 0;
  out->outputs = jv_array();
  out->error = jv_null();

  jq_set_error_cb(jq, collect_error, &compile_error);
  if (j->probe) jq_set_debug_cb(jq, collect_hit, &hits);
  else jq_set_debug_cb(jq, ignore_msg, NULL);

  jq_set_stderr_cb(jq, ignore_msg, NULL);
  jq_set_cancel_flag(jq, j->cancel);

  if (!jq_compile_args(jq, j->query, jv_copy(e->args))) {
    out->compile_failed = 1;
    out->error = compile_error;
    compile_error = jv_null();
    n_inputs = 0;
  }

  for (i32 i = 0; i < n_inputs && !out->capped && !cancelled && !hits.full; i++) {
    jv v;

    jq_start(jq, jv_array_get(jv_copy(e->inputs), i), 0);
    while (jv_is_valid(v = jq_next(jq))) {
      if (j->probe) {
        jv_free(v);
        continue;
      }

      if ((usz)jv_array_length(jv_copy(out->outputs)) >= j->cap) {
        out->capped = 1;
        break;
      }

      out->outputs = jv_array_append(out->outputs, v);
    }

    cancelled = atomic_get(j->cancel) != 0 && !hits.full;
    if (cancelled || out->capped || hits.full) {
      jv_free(v);
    } else if (jq_halted(jq)) {
      jv msg = jq_get_error_message(jq);

      jv_free(v);
      if (jv_get_kind(out->error) == JV_KIND_NULL && jv_is_valid(msg))
        out->error = error_text(msg);
      else jv_free(msg);

      break;
    } else if (jv_invalid_has_msg(jv_copy(v))) {
      if (jv_get_kind(out->error) == JV_KIND_NULL)
        out->error = error_text(jv_invalid_get_msg(v));
      else jv_free(v);
    } else
      jv_free(v);
  }

  if (j->probe) {
    jv_free(out->outputs);
    out->outputs = hits.hits;
  } else
    jv_free(hits.hits);

  jv_free(compile_error);
  jq_teardown(&jq);
  out->elapsed_us = clock_us() - start;
  if (cancelled) eval_result_free(out);
  return !cancelled;
}

static void collect_error(void* data, jv msg) {
  jv* first = data;

  if (jv_get_kind(*first) == JV_KIND_NULL && jv_get_kind(msg) == JV_KIND_STRING) {
    const char* s = jv_string_value(msg);

    if (strncmp(s, "jq: ", 4) == 0) s += 4;
    *first = jv_string(s);
  }

  jv_free(msg);
}

/* keeps what a probe tagged, and stops the job once cap are kept. */
static void collect_hit(void* data, jv msg) {
  probed* p = data;
  b8 pair = jv_get_kind(msg) == JV_KIND_ARRAY && jv_array_length(jv_copy(msg)) == 2;
  jv tag = pair ? jv_array_get(jv_copy(msg), 0) : jv_null();
  jv keys = pair ? jv_array_get(jv_copy(msg), 1) : jv_null();

  b8 ours = jv_get_kind(tag) == JV_KIND_STRING && strcmp(jv_string_value(tag), COMPLETE_TAG) == 0;
  ours = ours && key_list(keys);
  jv_free(tag);
  jv_free(msg);
  if (!ours || p->full) {
    jv_free(keys);
    return;
  }

  p->hits = jv_array_append(p->hits, keys);
  if ((usz)jv_array_length(jv_copy(p->hits)) < p->cap) return;

  p->full = 1;
  atomic_set(p->cancel, 1);
}

/* whether the borrowed v is a list of [name, type] pairs, as the probe
 * reports keys; the query's own debug calls may send anything. */
static b8 key_list(jv v) {
  b8 ok = jv_get_kind(v) == JV_KIND_ARRAY;

  for (i32 i = 0; ok && i < jv_array_length(jv_copy(v)); i++) {
    jv item = jv_array_get(jv_copy(v), i);
    b8 two = jv_get_kind(item) == JV_KIND_ARRAY && jv_array_length(jv_copy(item)) == 2;
    jv name = two ? jv_array_get(jv_copy(item), 0) : jv_null();
    jv type = two ? jv_array_get(jv_copy(item), 1) : jv_null();

    ok = jv_get_kind(name) == JV_KIND_STRING && jv_get_kind(type) == JV_KIND_STRING;
    jv_free(name);
    jv_free(type);
    jv_free(item);
  }

  return ok;
}

static void ignore_msg(void* data, jv msg) {
  UNUSED(data);
  jv_free(msg);
}

static jv error_text(jv msg) {
  if (jv_get_kind(msg) == JV_KIND_STRING)
    return msg;

  return jv_string_concat(jv_string("error (not a string): "), jv_dump_string(msg, 0));
}
