/* livejq - other ways to show a value: number bases, notations. */
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
  VIEW_COUNT,
} view_id;

/* The views that apply to v into out, VIEW_DEFAULT first. Returns how many.
 * v is not consumed. */
i32 views_of(jv v, view_id* out, i32 cap);
/* The view's short name, as shown next to the value. */
const char* view_name(view_id id);
/* v as a string in view id, invalid when the view does not apply to it.
 * v is not consumed. */
jv view_text(jv v, view_id id);

#endif /* LIVEJQ_VIEWS_H */
