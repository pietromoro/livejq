/* heap tests - blocks of every size, freed from any thread. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "heap.h"
#include "thread.h"

#include "test_helper.in"

#define MANY 200000

typedef struct {
  void** blocks;
  usz n;
} batch;

static void fill(void* p, usz n, u8 seed);
static b8 intact(const void* p, usz n, u8 seed);
static void alloc_batch(void* arg);
static void free_batch(void* arg);
static i64 rss_kb(void);
static void test_gives_memory_back(void);
static void test_sizes(void);
static void test_reuse(void);
static void test_other_thread_frees(void);
static void test_dead_thread_blocks(void);
static void test_stress(void);

static void fill(void* p, usz n, u8 seed) {
  for (usz i = 0; i < n; i++) ((u8*)p)[i] = (u8)(seed + i);
}

static b8 intact(const void* p, usz n, u8 seed) {
  for (usz i = 0; i < n; i++)
    if (((const u8*)p)[i] != (u8)(seed + i)) return 0;

  return 1;
}

static void alloc_batch(void* arg) {
  batch* b = arg;

  for (usz i = 0; i < b->n; i++) {
    b->blocks[i] = heap_alloc(24 + i % 200);
    fill(b->blocks[i], 24 + i % 200, (u8)i);
  }
}

static void free_batch(void* arg) {
  batch* b = arg;

  for (usz i = 0; i < b->n; i++) heap_free(b->blocks[i]);
}

/* the memory the process holds, -1 where it cannot be read. */
static i64 rss_kb(void) {
  FILE* f = fopen("/proc/self/status", "r");
  i64 kb = -1;

  if (!f) return -1;

  char line[256];
  while (fgets(line, sizeof(line), f))
    if (strncmp(line, "VmRSS:", 6) == 0) kb = atol(line + 6);

  fclose(f);
  return kb;
}

/* every class boundary and the large regions past them, aligned to 8 and
 * kept intact while the others are written. */
static void test_sizes(void) {
  static const usz sizes[] = { 0, 1, 7, 8, 9, 24, 511, 512, 513, 575, 576, 8191, 8192, 8193, 65536, 200000, 3000000 };
  void* p[ARRAY_LEN(sizes)];

  for (usz k = 0; k < ARRAY_LEN(sizes); k++) {
    p[k] = heap_alloc(sizes[k]);
    CHECK(p[k] != NULL);
    CHECK(((uintptr_t)p[k] & 7) == 0);
    fill(p[k], sizes[k], (u8)k);
  }

  for (usz k = 0; k < ARRAY_LEN(sizes); k++) {
    if (!intact(p[k], sizes[k], (u8)k)) FAIL("size %zu was overwritten", sizes[k]);
    else CHECK(1);

    heap_free(p[k]);
  }

  heap_free(NULL);
}

static void test_reuse(void) {
  void* a = heap_alloc(40);

  heap_free(a);
  CHECK(heap_alloc(40) == a);
  heap_free(a);
}

static void test_other_thread_frees(void) {
  batch b = { malloc(sizeof(void*) * MANY), MANY };

  thread_join(thread_start(alloc_batch, &b));

  b8 ok = 1;
  for (usz i = 0; i < b.n; i++) ok = ok && intact(b.blocks[i], 24 + i % 200, (u8)i);
  CHECK(ok);

  thread_join(thread_start(free_batch, &b));
  alloc_batch(&b);
  thread_join(thread_start(free_batch, &b));
  free(b.blocks);
}

/* blocks outlive the thread that made them, and their spans go when the
 * last is freed. */
static void test_dead_thread_blocks(void) {
  batch b = { malloc(sizeof(void*) * MANY), MANY };

  thread_join(thread_start(alloc_batch, &b));

  b8 ok = 1;
  for (usz i = 0; i < b.n; i++) ok = ok && intact(b.blocks[i], 24 + i % 200, (u8)i);
  CHECK(ok);

  free_batch(&b);
  free(b.blocks);
}

/* two threads allocating while each frees the other's blocks. */
static void test_stress(void) {
  batch a = { malloc(sizeof(void*) * MANY), MANY };
  batch b = { malloc(sizeof(void*) * MANY), MANY };

  for (i32 round = 0; round < 4; round++) {
    thread* t = thread_start(alloc_batch, &a);

    alloc_batch(&b);
    thread_join(t);
    t = thread_start(free_batch, &b);
    free_batch(&a);
    thread_join(t);
  }

  free(a.blocks);
  free(b.blocks);
  CHECK(1);
}

static void test_gives_memory_back(void) {
  batch b = { calloc(MANY, sizeof(void*)), MANY };
  i64 before = rss_kb();

  if (before < 0) {
    free(b.blocks);
    return;
  }

  alloc_batch(&b);

  i64 full = rss_kb();
  free_batch(&b);

  i64 after = rss_kb();
  CHECK(full - before > 20000);
  if (after - before > (full - before) / 10) {
    FAIL(
      "rss %d kB before, %d full, %d after freeing",
      (i32)before,
      (i32)full,
      (i32)after
    );
  } else CHECK(1);

  free(b.blocks);
}

#define TESTS(f)                \
   f(test_gives_memory_back)    \
   f(test_sizes)                \
   f(test_reuse)                \
   f(test_other_thread_frees)   \
   f(test_dead_thread_blocks)   \
   f(test_stress)
#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
