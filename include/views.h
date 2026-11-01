/* livejq - other ways to show a value: bases, notations, sizes, times. */
#ifndef LIVEJQ_VIEWS_H
#define LIVEJQ_VIEWS_H

#include <jv.h>

#include "defines.h"

typedef enum {
  VIEW_DEFAULT = 0,
  VIEW_HEX,
  VIEW_OCTAL,
  VIEW_BINARY,
  VIEW_EXPONENT,
  VIEW_BYTES,    /* 1024-based units */
  VIEW_BYTES_SI, /* 1000-based units */
  VIEW_DURATION, /* the number as seconds */
  VIEW_UTC,      /* integers as times, the unit guessed from their size */
  VIEW_LOCAL,
  VIEW_DATE,
  VIEW_AGO,
  VIEW_COUNT,
} view_id;

/* what a view needs besides the value. */
typedef struct {
  i64 now_ms; /* since the epoch */
} view_ctx;

/* The views that apply to v into out, VIEW_DEFAULT first. Returns how many.
 * v is not consumed. */
i32 views_of(jv v, view_id* out, i32 cap);
/* The view's short name, as shown next to the value. */
const char* view_name(view_id id);
/* v as a string in view id, invalid when the view does not apply to it.
 * v is not consumed. ctx may be NULL when only whether it applies matters. */
jv view_text(jv v, view_id id, const view_ctx* ctx);

#endif /* LIVEJQ_VIEWS_H */
