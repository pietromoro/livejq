/* livejq - what the query can be completed with at the cursor. */
#ifndef LIVEJQ_COMPLETE_H
#define LIVEJQ_COMPLETE_H

#include <jv.h>

#include "defines.h"

/* the tag of the values a probe hands to jq's debug callback. */
#define COMPLETE_TAG "livejq-probe"

typedef enum {
  COMPLETE_NONE = 0,
  COMPLETE_KEY,     /* a name after a dot */
  COMPLETE_BUILTIN, /* a name where a function can be called */
} complete_kind;

typedef struct {
  complete_kind kind;
  usz start;      /* the word's first byte, after the dot for a key */
  usz end;        /* past the word, which may go on after the cursor */
  char* probe;    /* for a key, NULL otherwise */
  char* fallback; /* for a key, used when the probe does not compile */
} complete_ctx;

/* Finds what the word before cursor in the len bytes of s completes to.
 * For a key the probe is the query cut at the cursor, the half-typed key
 * replaced by a call that sends the keys of its input to the debug
 * callback as [COMPLETE_TAG, [[key, type], ...]], the brackets and ifs
 * left open closed; the fallback sends the keys of the plain path before
 * the dot, or of the input. */
void complete_at(const char* s, usz len, usz cursor, complete_ctx* out);
void complete_free(complete_ctx* c);
/* jq's builtins as a list of [name, arities] for a popup, sorted, one per
 * name, with the arities as "/0 /1"; internal names left out. */
jv complete_builtins(void);

#endif /* LIVEJQ_COMPLETE_H */
