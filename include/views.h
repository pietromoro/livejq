/* livejq - other ways to show a value: bases, sizes, times, decodings. */
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
  VIEW_UTC,      /* integers as times, the unit guessed from their size, dates and uuids */
  VIEW_LOCAL,
  VIEW_DATE,
  VIEW_AGO,
  VIEW_BASE64,   /* base64 or base64url as text, or as bytes */
  VIEW_URL,      /* percent-encoding undone */
  VIEW_UNHEX,    /* hex digits as text, or as bytes */
  VIEW_UUID,     /* version, and time for the versions that hold one */
  VIEW_JSON,     /* a string holding an object or array, as that tree */
  VIEW_JWT,      /* a json web token's header and payload */
  VIEW_COUNT,
} view_id;

typedef enum {
  UNIT_AUTO = 0, /* guessed from the size */
  UNIT_S,
  UNIT_MS,
  UNIT_US,
  UNIT_NS,
  UNIT_COUNT,
} time_unit;

/* what a view needs besides the value. */
typedef struct {
  i64 now_ms;     /* since the epoch */
  time_unit unit; /* of integers shown as times */
} view_ctx;

/* The views that apply to v into out, VIEW_DEFAULT first. Returns how many.
 * v is not consumed. */
i32 views_of(jv v, view_id* out, i32 cap);
/* Whether view id applies to v, as text or as a tree. v is not consumed. */
b8 view_applies(jv v, view_id id);
/* The view's short name, as shown next to the value. */
const char* view_name(view_id id);
/* Whether view id reads v as a time in a time_unit, as it does integers.
 * v is not consumed. */
b8 view_takes_unit(jv v, view_id id);
/* The unit's short name, "" for UNIT_AUTO. */
const char* unit_name(time_unit unit);
/* v as a string in view id, invalid when the view does not apply to it or
 * shows it as a tree. v is not consumed. ctx may be NULL when only whether
 * it applies matters. */
jv view_text(jv v, view_id id, const view_ctx* ctx);
/* v as the object or array view id derives from it, invalid when the view
 * shows no tree for it. v is not consumed. */
jv view_tree(jv v, view_id id);
/* The jq filter that derives view id's tree from the value, NULL when the
 * view makes none. */
const char* view_jq(view_id id);

#endif /* LIVEJQ_VIEWS_H */
