/* livejq - threads, locks, atomics and a monotonic clock. */
#ifndef LIVEJQ_THREAD_H
#define LIVEJQ_THREAD_H

#include "defines.h"

typedef struct thread thread;
typedef struct mutex mutex;
typedef struct condvar condvar;

thread* thread_start(void (*fn)(void*), void* arg);
void thread_join(thread* t);

mutex* mutex_new(void);
void mutex_free(mutex* m);
void mutex_lock(mutex* m);
void mutex_unlock(mutex* m);

condvar* condvar_new(void);
void condvar_free(condvar* c);
void condvar_broadcast(condvar* c);
/* A negative timeout waits forever. False when the timeout expired. */
b8 condvar_wait(condvar* c, mutex* m, i64 timeout_ms);

i32 atomic_get(const i32* p);
void atomic_set(i32* p, i32 v);

i64 clock_us(void);

#endif /* LIVEJQ_THREAD_H */
