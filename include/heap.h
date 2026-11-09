/* livejq - the allocator of jq's values. */
#ifndef LIVEJQ_HEAP_H
#define LIVEJQ_HEAP_H

#include "defines.h"

/* A block of n bytes, aligned to 8, from the calling thread's heap. Aborts
 * when the system has no memory left. */
void* heap_alloc(usz n);
/* Gives back a block from heap_alloc, from any thread. NULL does nothing. */
void heap_free(void* p);

#endif /* LIVEJQ_HEAP_H */
