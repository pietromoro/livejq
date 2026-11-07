/* eval - the query worker, one jq run at a time. */
#include <stdlib.h>
#include <string.h>

#include <jq.h>

#include "complete.h"
#include "eval.h"
#include "thread.h"

struct eval {
  mutex* lock;
  condvar* changed;
  thread* worker;
  b8 quit;

  u64 submitted;
  char* query;
  usz cap;
  b8 probe;
  jv new_inputs;
  jv new_args;

  i32 cancel; /* stops the running job */

  b8 ready;
  eval_result result;

  jv inputs; /* worker only */
  jv args;   /* worker only */
};

typedef struct {
  u64 id;
  char* query;
  usz cap;
  b8 probe;
} job;

/* what a probe has handed to the debug callback so far. */
typedef struct {
  jv hits;
  usz cap;
  b8 full; /* stopped the job on reaching cap */
  i32* cancel;
  usz* visited;
} probed;

static u64 submit(eval* e, const char* query, usz cap, b8 probe);
static void worker_main(void* arg);
static b8 run(eval* e, const job* j, eval_result* out);
static void collect_error(void* data, jv msg);
static void collect_hit(void* data, jv msg);
static b8 key_list(jv v);
static void ignore_msg(void* data, jv msg);
static jv error_text(jv msg);
static jv clone(jv v, const i32* cancel, usz* visited);
static jv clone_number(jv v);
static jv clone_string(jv v);
static jv clone_array(jv v, const i32* cancel, usz* visited);
static jv clone_object(jv v, const i32* cancel, usz* visited);

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
  e->result.outputs = jv_null();
  e->result.error = jv_null();
  e->worker = thread_start(worker_main, e);
  if (!e->lock || !e->changed || !e->worker) abort();
  return e;
}

void eval_free(eval* e) {
  mutex_lock(e->lock);
  e->quit = 1;
  atomic_set(&e->cancel, 1);
  condvar_broadcast(e->changed);
  mutex_unlock(e->lock);
  thread_join(e->worker);

  free(e->query);
  jv_free(e->new_inputs);
  jv_free(e->new_args);
  jv_free(e->inputs);
  jv_free(e->args);
  eval_result_free(&e->result);
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

u64 eval_submit(eval* e, const char* query, usz cap) { return submit(e, query, cap, 0); }

u64 eval_probe(eval* e, const char* query, usz cap) { return submit(e, query, cap, 1); }

b8 eval_take(eval* e, eval_result* out) {
  b8 got;

  mutex_lock(e->lock);
  got = e->ready;
  if (got) {
    *out = e->result;
    e->ready = 0;
    e->result.outputs = jv_null();
    e->result.error = jv_null();
  }

  mutex_unlock(e->lock);
  return got;
}

b8 eval_wait(eval* e, u64 id, i64 timeout_ms) {
  i64 deadline = clock_us() + timeout_ms * 1000;
  b8 done;

  mutex_lock(e->lock);
  while (!(done = e->ready && e->result.id >= id)) {
    i64 left = timeout_ms < 0 ? -1 : (deadline - clock_us()) / 1000;

    if (timeout_ms >= 0 && left <= 0) break;

    condvar_wait(e->changed, e->lock, left);
  }

  mutex_unlock(e->lock);
  return done;
}

void eval_result_free(eval_result* r) {
  jv_free(r->outputs);
  jv_free(r->error);
  r->outputs = jv_null();
  r->error = jv_null();
}

jv eval_clone(jv v) {
  i32 never = 0;
  usz visited = 0;

  return clone(v, &never, &visited);
}

static u64 submit(eval* e, const char* query, usz cap, b8 probe) {
  usz n = strlen(query);
  char* copy = malloc(n + 1);
  u64 id;

  if (!copy) abort();
  memcpy(copy, query, n + 1);

  mutex_lock(e->lock);
  free(e->query);
  e->query = copy;
  e->cap = cap;
  e->probe = probe;
  id = ++e->submitted;
  atomic_set(&e->cancel, 1);
  condvar_broadcast(e->changed);
  mutex_unlock(e->lock);
  return id;
}

static void worker_main(void* arg) {
  eval* e = arg;
  u64 done = 0;

  for (;;) {
    mutex_lock(e->lock);
    while (!e->quit && e->submitted == done)
      condvar_wait(e->changed, e->lock, -1);

    if (e->quit) {
      mutex_unlock(e->lock);
      return;
    }

    job j;
    j.id = done = e->submitted;
    j.query = e->query;
    j.cap = e->cap;
    j.probe = e->probe;
    e->query = NULL;
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

    atomic_set(&e->cancel, 0);
    mutex_unlock(e->lock);

    eval_result r;
    if (run(e, &j, &r)) {
      mutex_lock(e->lock);
      if (r.id == e->submitted) {
        eval_result_free(&e->result);
        e->result = r;
        e->ready = 1;
        condvar_broadcast(e->changed);
      } else
        eval_result_free(&r);

      mutex_unlock(e->lock);
    }

    free(j.query);
  }
}

/* false when the job was cancelled and produced nothing worth posting. */
static b8 run(eval* e, const job* j, eval_result* out) {
  i64 start = clock_us();
  jq_state* jq = jq_init();
  jv compile_error = jv_null();
  i32 n_inputs = jv_array_length(jv_copy(e->inputs));
  usz visited = 0;
  b8 cancelled = 0;
  probed hits = { jv_array(), j->cap, 0, &e->cancel, &visited };

  if (!jq) abort();

  out->id = j->id;
  out->compile_failed = 0;
  out->capped = 0;
  out->outputs = jv_array();
  out->error = jv_null();

  jq_set_error_cb(jq, collect_error, &compile_error);
  if (j->probe) jq_set_debug_cb(jq, collect_hit, &hits);
  else jq_set_debug_cb(jq, ignore_msg, NULL);

  jq_set_stderr_cb(jq, ignore_msg, NULL);
  jq_set_cancel_flag(jq, &e->cancel);

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

      v = clone(v, &e->cancel, &visited);
      if (!jv_is_valid(v)) break;

      out->outputs = jv_array_append(out->outputs, v);
    }

    cancelled = atomic_get(&e->cancel) != 0 && !hits.full;
    if (cancelled || out->capped || hits.full) {
      jv_free(v);
    } else if (jq_halted(jq)) {
      jv msg = jq_get_error_message(jq);

      jv_free(v);
      if (jv_get_kind(out->error) == JV_KIND_NULL && jv_is_valid(msg))
        out->error = clone(error_text(msg), &e->cancel, &visited);
      else jv_free(msg);

      break;
    } else if (jv_invalid_has_msg(jv_copy(v))) {
      if (jv_get_kind(out->error) == JV_KIND_NULL)
        out->error = clone(error_text(jv_invalid_get_msg(v)), &e->cancel, &visited);
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

  jv hit = clone(keys, p->cancel, p->visited);
  if (!jv_is_valid(hit)) return;

  p->hits = jv_array_append(p->hits, hit);
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

/* rebuilds v without sharing any allocation, so the result can leave the
 * worker while the inputs it came from stay in use. consumes v. returns an
 * invalid value when the job is cancelled midway. */
static jv clone(jv v, const i32* cancel, usz* visited) {
  jv out;

  *visited += 1;
  if ((*visited & 4095) == 0 && atomic_get(cancel)) {
    jv_free(v);
    return jv_invalid();
  }

  switch (jv_get_kind(v)) {
    case JV_KIND_NUMBER:
      out = clone_number(v); break;
    case JV_KIND_STRING:
      out = clone_string(v); break;
    case JV_KIND_ARRAY:
      out = clone_array(v, cancel, visited); break;
    case JV_KIND_OBJECT:
      out = clone_object(v, cancel, visited); break;
    default: return v;
  }

  jv_free(v);
  return out;
}

static jv clone_number(jv v) {
  if (jv_number_has_literal(v))
    return jv_number_with_literal(jv_number_get_literal(v));

  return jv_number(jv_number_value(v));
}

static jv clone_string(jv v) {
  return jv_string_sized(jv_string_value(v), jv_string_length_bytes(jv_copy(v)));
}

static jv clone_array(jv v, const i32* cancel, usz* visited) {
  jv out = jv_array();

  jv_array_foreach(v, i, item) {
    jv c = clone(item, cancel, visited);

    if (!jv_is_valid(c)) {
      jv_free(out);
      return c;
    }

    out = jv_array_append(out, c);
  }

  return out;
}

static jv clone_object(jv v, const i32* cancel, usz* visited) {
  jv out = jv_object();

  jv_object_foreach(v, key, item) {
    jv c = clone(item, cancel, visited);

    if (!jv_is_valid(c)) {
      jv_free(key);
      jv_free(out);
      return c;
    }

    out = jv_object_set(out, clone_string(key), c);
    jv_free(key);
  }

  return out;
}
