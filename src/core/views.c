/* views - pure functions from a value to the ways it can be shown. */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "views.h"

#define MAX_EXACT 9007199254740992.0 /* 2^53 */

static b8 as_int(jv v, i64* out);
static jv in_base(i64 n, u32 base, const char* prefix);
static jv exponent(f64 d);

i32 views_of(jv v, view_id* out, i32 cap) {
  i32 n = 0;

  for (i32 id = 0; id < VIEW_COUNT && n < cap; id++) {
    jv text = id == VIEW_DEFAULT ? jv_true() : view_text(v, (view_id)id);

    if (jv_is_valid(text)) out[n++] = (view_id)id;

    jv_free(text);
  }

  return n;
}

const char* view_name(view_id id) {
  switch (id) {
    case VIEW_HEX: return "hex";
    case VIEW_OCTAL: return "oct";
    case VIEW_BINARY: return "bin";
    case VIEW_EXPONENT: return "exp";
    default: return "";
  }
}

jv view_text(jv v, view_id id) {
  i64 n;

  if (jv_get_kind(v) != JV_KIND_NUMBER) return jv_invalid();

  switch (id) {
    case VIEW_HEX: return as_int(v, &n) ? in_base(n, 16, "0x") : jv_invalid();
    case VIEW_OCTAL: return as_int(v, &n) ? in_base(n, 8, "0o") : jv_invalid();
    case VIEW_BINARY: return as_int(v, &n) ? in_base(n, 2, "0b") : jv_invalid();
    case VIEW_EXPONENT: return exponent(jv_number_value(v));
    default: return jv_invalid();
  }
}

/* the literal when the file wrote one, since a double loses digits past
 * 2^53; otherwise the double, if it is exact. */
static b8 as_int(jv v, i64* out) {
  f64 d = jv_number_value(v);

  if (jv_number_has_literal(v)) {
    const char* lit = jv_number_get_literal(v);
    char* end;
    i64 n;

    errno = 0;
    n = strtoll(lit, &end, 10);
    if (errno == 0 && *end == '\0' && end != lit) {
      *out = n;
      return 1;
    }
  }

  if (!isfinite(d) || d != floor(d) || fabs(d) > MAX_EXACT) return 0;

  *out = (i64)d;
  return 1;
}

static jv in_base(i64 n, u32 base, const char* prefix) {
  u64 m = n < 0 ? (u64)0 - (u64)n : (u64)n;
  char digits[65];
  char buf[72];
  usz i = sizeof(digits) - 1;

  digits[i] = '\0';
  do {
    digits[--i] = "0123456789abcdef"[m % base];
    m /= base;
  } while (m);

  snprintf(buf, sizeof(buf), "%s%s%s", n < 0 ? "-" : "", prefix, digits + i);
  return jv_string(buf);
}

/* the fewest digits that read back as d, as 1.5e3 rather than 1.5e+03. */
static jv exponent(f64 d) {
  char buf[40];
  char out[48];
  char* e;
  i32 p = 0;

  if (!isfinite(d) || d == 0) return jv_invalid();

  do snprintf(buf, sizeof(buf), "%.*e", p, d);
  while (strtod(buf, NULL) != d && ++p < 17);

  e = strchr(buf, 'e');
  *e = '\0';
  snprintf(out, sizeof(out), "%se%d", buf, atoi(e + 1));
  return jv_string(out);
}
