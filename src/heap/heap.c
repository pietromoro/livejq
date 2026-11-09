/* heap - size-classed spans owned by threads, for jq's many small values.
 *
 * A span is 64 KB aligned to 64 KB, so a block finds its span by masking its
 * address, and holds blocks of one size. Each thread allocates from its own
 * spans without locks; a block freed by another thread goes on its span's
 * remote list, and the span on its owner's pending stack, both lock-free.
 * Empty spans go back to the system. A block above SMALL_MAX gets a region
 * of its own, laid out like a span.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* curufmt variant */
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "defines.h"
#include "heap.h"

#define SPAN      ((usz)64 * 1024)
#define HEADER    128  /* bytes before a span's first block */
#define FINE_MAX  512  /* classes step by 8 bytes up to here, then by 64 */
#define SMALL_MAX 8192
#define FINE      (FINE_MAX / 8)
#define CLASSES   (FINE + (SMALL_MAX - FINE_MAX) / 64 + 1)
#define PAGE      4096

typedef struct block {
  struct block* next;
} block;

typedef struct heap heap;

typedef struct span {
  usz size;      /* of its blocks, 0 for a region of one large block */
  usz bytes;     /* mapped */
  heap* owner;
  block* local;  /* free blocks, owner only */
  char* bump;    /* from here to end, never handed out yet */
  char* end;
  struct span* prev; /* in the owner's partial list */
  struct span* next;
  b8 partial;
  block* remote;               /* atomic: freed by other threads */
  struct span* next_pending;   /* on the owner's pending stack */
  i32 pending;                 /* atomic: on that stack */
  i32 used;                    /* atomic: handed out, not freed */
  i32 released;                /* atomic: unmapped by whoever set it */
} span;

struct heap {
  span* current[CLASSES]; /* allocated from */
  span* partial[CLASSES]; /* others with free blocks */
  span* pending;          /* atomic: spans with remote frees to look at */
  i32 dead;               /* atomic: its thread exited */
};

static __thread heap* mine;
static pthread_key_t exit_key;
static pthread_once_t exit_once = PTHREAD_ONCE_INIT;

static heap* heap_new(void);
static void heap_exit(void* arg);
static void make_key(void) { pthread_key_create(&exit_key, heap_exit); }
static void* large(usz n);
static void* take(span* s);
static span* next_span(heap* h, i32 c);
static span* new_span(heap* h, i32 c);
static void drain(heap* h);
static void settle(heap* h, span* s);
static void free_local(heap* h, span* s, block* b);
static void free_remote(span* s, block* b);
static void add_partial(heap* h, span* s);
static void drop_partial(heap* h, span* s);
static b8 empty(span* s);
static void release(heap* h, span* s);
static i32 class_of(usz n);
static usz class_size(i32 c);
static span* span_of(void* p) { return (span*)((uintptr_t)p & ~(uintptr_t)(SPAN - 1)); }
static void* map(usz bytes);
static void unmap(void* p, usz bytes);

void* heap_alloc(usz n) {
  if (n > SMALL_MAX) return large(n);

  heap* h = mine ? mine : heap_new();
  i32 c = class_of(n);
  span* s = h->current[c];

  for (;;) {
    void* p = s ? take(s) : NULL;
    if (p) return p;

    s = next_span(h, c);
  }
}

void heap_free(void* p) {
  if (!p) return;

  span* s = span_of(p);
  if (s->size == 0) {
    unmap(s, s->bytes);
    return;
  }

  if (mine && s->owner == mine) free_local(mine, s, p);
  else free_remote(s, p);
}

static heap* heap_new(void) {
  heap* h = calloc(1, sizeof(*h));

  if (!h) {
    fputs("livejq: out of memory\n", stderr);
    abort();
  }

  pthread_once(&exit_once, make_key);
  pthread_setspecific(exit_key, h);
  mine = h;
  return h;
}

/* gives back the spans already empty. the others stay until their last
 * block is freed, by a thread that then sees dead set; the heap itself is
 * never freed, as those threads still read it. */
static void heap_exit(void* arg) {
  heap* h = arg;

  drain(h);
  for (i32 c = 1; c < CLASSES; c++) {
    span* s = h->current[c];

    h->current[c] = NULL;
    if (s && empty(s)) release(h, s);
    else if (s) add_partial(h, s);

    for (s = h->partial[c]; s;) {
      span* next = s->next;

      if (empty(s)) release(h, s);

      s = next;
    }
  }

  __atomic_store_n(&h->dead, 1, __ATOMIC_RELEASE);
  if (mine == h) mine = NULL;
}

static void* large(usz n) {
  usz bytes = (HEADER + n + PAGE - 1) / PAGE * PAGE;
  span* s = map(bytes);

  s->size = 0;
  s->bytes = bytes;
  return (char*)s + HEADER;
}

/* a free block, a remote one, or a fresh one; NULL when the span is full. */
static void* take(span* s) {
  block* b = s->local;

  if (!b && __atomic_load_n(&s->remote, __ATOMIC_RELAXED))
    b = __atomic_exchange_n(&s->remote, NULL, __ATOMIC_ACQUIRE);

  if (b) {
    s->local = b->next;
  } else if ((usz)(s->end - s->bump) >= s->size) {
    b = (block*)s->bump;
    s->bump += s->size;
  } else
    return NULL;

  __atomic_fetch_add(&s->used, 1, __ATOMIC_RELAXED);
  return b;
}

static span* next_span(heap* h, i32 c) {
  drain(h);

  span* s = h->partial[c];
  if (s) drop_partial(h, s);
  else s = new_span(h, c);

  h->current[c] = s;
  return s;
}

static span* new_span(heap* h, i32 c) {
  span* s = map(SPAN);

  memset(s, 0, sizeof(*s));
  s->size = class_size(c);
  s->bytes = SPAN;
  s->owner = h;
  s->bump = (char*)s + HEADER;
  s->end = (char*)s + SPAN;
  return s;
}

/* takes the pending stack at once, so no span on it is popped twice. */
static void drain(heap* h) {
  span* s = __atomic_exchange_n(&h->pending, NULL, __ATOMIC_ACQUIRE);

  while (s) {
    span* next = s->next_pending;

    __atomic_store_n(&s->pending, 0, __ATOMIC_RELEASE);
    settle(h, s);
    s = next;
  }
}

/* a span the owner is not allocating from goes back when empty, or among
 * the partial ones when it has free blocks. */
static void settle(heap* h, span* s) {
  if (s == h->current[class_of(s->size)]) return;

  if (empty(s)) {
    release(h, s);
    return;
  }

  b8 free_blocks = s->local || __atomic_load_n(&s->remote, __ATOMIC_RELAXED);
  if (free_blocks) add_partial(h, s);
}

static void free_local(heap* h, span* s, block* b) {
  b->next = s->local;
  s->local = b;
  __atomic_sub_fetch(&s->used, 1, __ATOMIC_ACQ_REL);
  settle(h, s);
}

/* the block goes on the span's remote list before the span on its owner's
 * stack, and both before the count drops, so an owner that sees the count
 * at zero also sees the span pending, and leaves it to be drained. */
static void free_remote(span* s, block* b) {
  heap* h = s->owner;
  block* head = __atomic_load_n(&s->remote, __ATOMIC_RELAXED);

  do {
    b->next = head;
  } while (!__atomic_compare_exchange_n(
    &s->remote,
    &head, b, 1,
    __ATOMIC_RELEASE,
    __ATOMIC_RELAXED
  ));

  i32 idle = 0;
  if (__atomic_compare_exchange_n(&s->pending, &idle, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
    span* top = __atomic_load_n(&h->pending, __ATOMIC_RELAXED);

    do {
      s->next_pending = top;
    } while (!__atomic_compare_exchange_n(
      &h->pending,
      &top, s, 1,
      __ATOMIC_RELEASE,
      __ATOMIC_RELAXED
    ));
  }

  if (__atomic_sub_fetch(&s->used, 1, __ATOMIC_ACQ_REL) != 0) return;
  if (!__atomic_load_n(&h->dead, __ATOMIC_ACQUIRE)) return;

  i32 kept = 0;
  if (__atomic_compare_exchange_n(&s->released, &kept, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
    unmap(s, s->bytes);
}

static void add_partial(heap* h, span* s) {
  i32 c = class_of(s->size);

  if (s->partial) return;

  s->partial = 1;
  s->prev = NULL;
  s->next = h->partial[c];
  if (s->next) s->next->prev = s;

  h->partial[c] = s;
}

static void drop_partial(heap* h, span* s) {
  if (!s->partial) return;

  if (s->prev) s->prev->next = s->next;
  else h->partial[class_of(s->size)] = s->next;

  if (s->next) s->next->prev = s->prev;

  s->partial = 0;
}

/* no block in use and none of its frees still to be drained. */
static b8 empty(span* s) {
  return __atomic_load_n(&s->used, __ATOMIC_ACQUIRE) == 0
    && !__atomic_load_n(&s->pending, __ATOMIC_ACQUIRE);
}

static void release(heap* h, span* s) {
  drop_partial(h, s);
  unmap(s, s->bytes);
}

static i32 class_of(usz n) {
  if (n <= FINE_MAX) return n ? (i32)((n + 7) / 8) : 1;

  return FINE + (i32)((n - FINE_MAX + 63) / 64);
}

static usz class_size(i32 c) {
  if (c <= FINE) return (usz)c * 8;

  return FINE_MAX + (usz)(c - FINE) * 64;
}

/* bytes aligned to SPAN: windows maps on 64 KB boundaries, elsewhere the
 * mapping is cut down to an aligned one. */
static void* map(usz bytes) {
#ifdef _WIN32
  void* p = VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

  if (p) return p;
#else
  char* p = mmap(NULL, bytes + SPAN, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p != MAP_FAILED) {
    char* at = (char*)(((uintptr_t)p + SPAN - 1) & ~(uintptr_t)(SPAN - 1));

    if (at > p) munmap(p, (usz)(at - p));
    if (p + bytes + SPAN > at + bytes) munmap(at + bytes, (usz)(p + bytes + SPAN - (at + bytes)));

    return at;
  }
#endif
  fputs("livejq: out of memory\n", stderr);
  abort();
}

static void unmap(void* p, usz bytes) {
#ifdef _WIN32
  UNUSED(bytes);
  VirtualFree(p, 0, MEM_RELEASE);
#else
  munmap(p, bytes);
#endif
}
