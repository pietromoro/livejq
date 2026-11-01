/* thread - the thread shim over posix threads (winpthreads on windows). */
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include "thread.h"

struct thread {
  pthread_t id;
  void (*fn)(void*);
  void* arg;
};

struct mutex {
  pthread_mutex_t m;
};

struct condvar {
  pthread_cond_t c;
};

static void* trampoline(void* p);

thread* thread_start(void (*fn)(void*), void* arg) {
  thread* t = malloc(sizeof(*t));

  if (!t) return NULL;

  t->fn = fn;
  t->arg = arg;
  if (pthread_create(&t->id, NULL, trampoline, t) != 0) {
    free(t);
    return NULL;
  }

  return t;
}

void thread_join(thread* t) {
  pthread_join(t->id, NULL);
  free(t);
}

mutex* mutex_new(void) {
  mutex* m = malloc(sizeof(*m));

  if (m) pthread_mutex_init(&m->m, NULL);
  return m;
}

void mutex_free(mutex* m) {
  pthread_mutex_destroy(&m->m);
  free(m);
}

void mutex_lock(mutex* m) { pthread_mutex_lock(&m->m); }

void mutex_unlock(mutex* m) { pthread_mutex_unlock(&m->m); }

condvar* condvar_new(void) {
  condvar* c = malloc(sizeof(*c));

  if (c) pthread_cond_init(&c->c, NULL);
  return c;
}

void condvar_free(condvar* c) {
  pthread_cond_destroy(&c->c);
  free(c);
}

void condvar_broadcast(condvar* c) { pthread_cond_broadcast(&c->c); }

b8 condvar_wait(condvar* c, mutex* m, i64 timeout_ms) {
  struct timespec ts;

  if (timeout_ms < 0) {
    pthread_cond_wait(&c->c, &m->m);
    return 1;
  }

  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += (time_t)(timeout_ms / 1000);
  ts.tv_nsec += (timeout_ms % 1000) * 1000000;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000L;
  }

  return pthread_cond_timedwait(&c->c, &m->m, &ts) == 0;
}

i32 atomic_get(const i32* p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }

void atomic_set(i32* p, i32 v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }

i64 clock_us(void) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (i64)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

i64 clock_wall_ms(void) {
  struct timespec ts;

  clock_gettime(CLOCK_REALTIME, &ts);
  return (i64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void* trampoline(void* p) {
  thread* t = p;

  t->fn(t->arg);
  return NULL;
}
