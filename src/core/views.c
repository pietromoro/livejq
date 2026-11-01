/* views - pure functions from a value to the ways it can be shown. */
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "views.h"

#define MAX_EXACT   9007199254740992.0 /* 2^53 */
#define MAX_SECONDS 1e15               /* its milliseconds still fit an i64 */
#define DAY         86400
#define MIN_BINARY  16  /* base64 shorter than this must decode to text */
#define MIN_HEX     8   /* hex digits below this get no view */
#define MAX_DUMP    256 /* bytes shown as hex, the rest cut */
#define GREGORIAN   122192928000000000 /* 100 ns ticks from 1582-10-15 to the epoch */

/* a moment, from a number whose unit was guessed. */
typedef struct {
  i64 sec;    /* since the epoch, floored */
  i64 frac;   /* below the second, in the unit */
  i32 digits; /* the unit's: 0, 3, 6 or 9 */
} instant;

typedef struct {
  i64 year;
  i32 mon; /* 1..12 */
  i32 day;
  i32 hour;
  i32 min;
  i32 sec;
} civil;

static b8 as_int(jv v, i64* out);
static jv in_base(i64 n, u32 base, const char* prefix);
static jv exponent(f64 d);
static jv bytes(i64 n, f64 base, const char* const* units);
static jv duration(f64 d);
static jv short_duration(f64 d);
static b8 is_time(view_id id);
static jv in_time(instant at, view_id id, const view_ctx* ctx);
static jv timestamp(instant at, view_id id);
static jv relative(instant at, i64 now_ms);
static instant to_instant(i64 n, time_unit unit);
static b8 parse_iso(const char* s, instant* at);
static b8 parse_rfc2822(const char* s, instant* at);
static b8 clock_time(const char** s, civil* c);
static b8 parse_zone(const char* s, i64* off);
static b8 named_zone(const char* s, i64* off);
static b8 from_civil(const civil* c, i64 off, instant* at);
static b8 digits(const char** s, i32 n, i32* out);
static i32 name_in(const char* names, const char* s);
static const char* skip_spaces(const char* s);
static void split(i64 t, civil* c);
static i64 days_of(i64 y, i32 m, i32 d);
static b8 local_offset(i64 t, i64* off);
static b8 local_tm(time_t t, struct tm* out);
static f64 round3(f64 v, i32* dec);
static void put_trimmed(char* buf, usz cap, f64 v, i32 dec);
static jv base64(jv v);
static jv url_decode(jv v);
static i32 hex_digit(char c);
static jv unhex(jv v);
static i64 from_hex(const char* s, usz len, u8* out);
static i64 from_base64(const char* s, usz len, u8* out);
static i32 base64_digit(char c, char* alphabet);
static b8 is_text(const u8* b, usz len);
static jv hex_dump(const u8* b, usz len);
static jv uuid(jv v);
static b8 uuid_instant(jv v, instant* at);
static b8 parse_uuid(jv v, u8* out);
static b8 uuid_time(const u8* b, instant* at);
static u64 be(const u8* b, i32 n);
static jv embedded_json(jv v);
static jv jwt(jv v);
static jv jwt_part(const char* s, usz len);
static jv query_string(jv v);
static b8 add_field(jv* fields, const char* s, usz len);
static jv unescape(const char* s, usz len, b8 form);

static const char* const iec_units[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", NULL };
static const char* const si_units[] = { "B", "kB", "MB", "GB", "TB", "PB", "EB", NULL };
static const char* const second_units[] = { "ms", "\xc2\xb5s", "ns" };
static const f64 per_second[] = { 1, 1, 1e3, 1e6, 1e9 }; /* by time_unit, seconds without one */
static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
static const char weekdays[] = "MonTueWedThuFriSatSun";

static const struct {
  const char* name;
  i32 hours; /* east of UTC */
} zones[] = {
    { "GMT", 0 },
    { "UT", 0 },
    { "UTC", 0 },
    { "EST", -5 },
    { "EDT", -4 },
    { "CST", -6 },
    { "CDT", -5 },
    { "MST", -7 },
    { "MDT", -6 },
    { "PST", -8 },
    { "PDT", -7 },
};

i32 views_of(jv v, view_id* out, i32 cap) {
  i32 n = 0;

  for (i32 id = 0; id < VIEW_COUNT && n < cap; id++)
    if (view_applies(v, (view_id)id)) out[n++] = (view_id)id;

  return n;
}

b8 view_applies(jv v, view_id id) {
  if (id == VIEW_DEFAULT) return 1;

  jv out = view_jq(id) ? view_tree(v, id) : view_text(v, id, NULL);
  b8 applies = jv_is_valid(out);

  jv_free(out);
  return applies;
}

const char* view_name(view_id id) {
  switch (id) {
    case VIEW_HEX: return "hex";
    case VIEW_OCTAL: return "oct";
    case VIEW_BINARY: return "bin";
    case VIEW_EXPONENT: return "exp";
    case VIEW_BYTES: return "iec";
    case VIEW_BYTES_SI: return "si";
    case VIEW_DURATION: return "dur";
    case VIEW_UTC: return "utc";
    case VIEW_LOCAL: return "local";
    case VIEW_DATE: return "date";
    case VIEW_AGO: return "ago";
    case VIEW_BASE64: return "b64";
    case VIEW_URL: return "url";
    case VIEW_UNHEX: return "unhex";
    case VIEW_UUID: return "uuid";
    case VIEW_JSON: return "json";
    case VIEW_JWT: return "jwt";
    case VIEW_QUERY: return "query";
    default: return "";
  }
}

b8 view_takes_unit(jv v, view_id id) {
  return jv_get_kind(v) == JV_KIND_NUMBER && (is_time(id) || id == VIEW_DURATION);
}

const char* unit_name(time_unit unit) {
  switch (unit) {
    case UNIT_S: return "s";
    case UNIT_MS: return "ms";
    case UNIT_US: return "\xc2\xb5s";
    case UNIT_NS: return "ns";
    default: return "";
  }
}

jv view_text(jv v, view_id id, const view_ctx* ctx) {
  time_unit unit = ctx ? ctx->unit : UNIT_AUTO;
  instant at;
  i64 n;

  if (jv_get_kind(v) == JV_KIND_STRING && is_time(id)) {
    const char* s = jv_string_value(v);

    if (!parse_iso(s, &at) && !parse_rfc2822(s, &at) && !uuid_instant(v, &at)) return jv_invalid();

    return in_time(at, id, ctx);
  }

  if (jv_get_kind(v) == JV_KIND_STRING && id == VIEW_BASE64) return base64(v);
  if (jv_get_kind(v) == JV_KIND_STRING && id == VIEW_URL) return url_decode(v);
  if (jv_get_kind(v) == JV_KIND_STRING && id == VIEW_UNHEX) return unhex(v);
  if (jv_get_kind(v) == JV_KIND_STRING && id == VIEW_UUID) return uuid(v);
  if (jv_get_kind(v) != JV_KIND_NUMBER) return jv_invalid();

  switch (id) {
    case VIEW_HEX: return as_int(v, &n) ? in_base(n, 16, "0x") : jv_invalid();
    case VIEW_OCTAL: return as_int(v, &n) ? in_base(n, 8, "0o") : jv_invalid();
    case VIEW_BINARY: return as_int(v, &n) ? in_base(n, 2, "0b") : jv_invalid();
    case VIEW_EXPONENT: return exponent(jv_number_value(v));
    case VIEW_BYTES: return as_int(v, &n) ? bytes(n, 1024, iec_units) : jv_invalid();
    case VIEW_BYTES_SI: return as_int(v, &n) ? bytes(n, 1000, si_units) : jv_invalid();
    case VIEW_DURATION:
      return unit == UNIT_S ? jv_invalid() : duration(jv_number_value(v) / per_second[unit]);
    case VIEW_UTC:
    case VIEW_LOCAL:
    case VIEW_DATE:
    case VIEW_AGO: return as_int(v, &n) ? in_time(to_instant(n, unit), id, ctx) : jv_invalid();
    default: return jv_invalid();
  }
}

jv view_tree(jv v, view_id id) {
  if (jv_get_kind(v) != JV_KIND_STRING) return jv_invalid();

  switch (id) {
    case VIEW_JSON: return embedded_json(v);
    case VIEW_JWT: return jwt(v);
    case VIEW_QUERY: return query_string(v);
    default: return jv_invalid();
  }
}

const char* view_jq(view_id id) {
  switch (id) {
    case VIEW_JSON: return "fromjson";
    case VIEW_JWT:
      return "split(\".\") | {header: .[0], payload: .[1]}"
             " | map_values(gsub(\"-\"; \"+\") | gsub(\"_\"; \"/\") | @base64d | fromjson)";
    case VIEW_QUERY:
      return "sub(\"^[^?]*\\\\?\"; \"\") | sub(\"#.*\"; \"\") | split(\"&\")"
             " | map(select(. != \"\") | split(\"=\") | {(.[0]): (.[1:] | join(\"=\"))}) | add";
    default: return NULL;
  }
}

void view_clock(i64 ms, i64 now_ms, char* buf, usz cap) {
  i64 t = ms >= 0 ? ms / 1000 : (ms - 999) / 1000;
  i64 now = now_ms >= 0 ? now_ms / 1000 : (now_ms - 999) / 1000;
  i64 off = 0;
  i64 now_off = 0;

  local_offset(t, &off);
  local_offset(now, &now_off);

  civil c;

  split(t + off, &c);

  civil today;

  split(now + now_off, &today);

  const char* mon = months + (c.mon - 1) * 3;
  char date[32] = "";

  if (c.year != today.year)
    snprintf(date, sizeof(date), "%d %.3s %" PRId64 " ", c.day, mon, c.year);
  else if (c.mon != today.mon || c.day != today.day)
    snprintf(date, sizeof(date), "%d %.3s ", c.day, mon);

  snprintf(buf, cap, "%s%02d:%02d:%02d", date, c.hour, c.min, c.sec);
}

/* the literal when the file wrote one, since a double loses digits past
 * 2^53; otherwise the double, if it is exact. */
static b8 as_int(jv v, i64* out) {
  f64 d = jv_number_value(v);

  if (jv_number_has_literal(v)) {
    const char* lit = jv_number_get_literal(v);

    errno = 0;

    char* end;
    i64 n = strtoll(lit, &end, 10);
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
  usz i = sizeof(digits) - 1;

  digits[i] = '\0';
  do {
    digits[--i] = "0123456789abcdef"[m % base];
    m /= base;
  } while (m);

  char buf[72];
  snprintf(buf, sizeof(buf), "%s%s%s", n < 0 ? "-" : "", prefix, digits + i);
  return jv_string(buf);
}

/* the fewest digits that read back as d, as 1.5e3 rather than 1.5e+03. */
static jv exponent(f64 d) {
  i32 p = 0;

  if (!isfinite(d) || d == 0) return jv_invalid();

  char buf[40];
  do snprintf(buf, sizeof(buf), "%.*e", p, d);
  while (strtod(buf, NULL) != d && ++p < 17);

  char* e = strchr(buf, 'e');
  *e = '\0';

  char out[48];
  snprintf(out, sizeof(out), "%se%d", buf, atoi(e + 1));
  return jv_string(out);
}

/* n bytes in the largest unit it reaches, to three significant digits. */
static jv bytes(i64 n, f64 base, const char* const* units) {
  f64 v = fabs((f64)n);
  i32 u = 0;
  i32 dec;
  char buf[48];

  if (v < base) {
    snprintf(buf, sizeof(buf), "%" PRId64 " B", n);
    return jv_string(buf);
  }

  while (v >= base && units[u + 1]) {
    v /= base;
    u++;
  }

  v = round3(v, &dec);
  if (v >= base && units[u + 1]) {
    v = round3(v / base, &dec);
    u++;
  }

  char num[32];
  put_trimmed(num, sizeof(num), v, dec);
  snprintf(buf, sizeof(buf), "%s%s %s", n < 0 ? "-" : "", num, units[u]);
  return jv_string(buf);
}

/* d seconds as days, hours, minutes and seconds, the zero ones left out,
 * to the millisecond. */
static jv duration(f64 d) {
  i64 ms;
  i64 sec;
  char buf[96];
  usz n;

  if (!isfinite(d) || fabs(d) >= MAX_SECONDS) return jv_invalid();

  ms = llround(fabs(d) * 1000);
  if (ms < 1000) return short_duration(d);

  n = (usz)snprintf(buf, sizeof(buf), "%s", d < 0 ? "-" : "");
  if (ms >= 86400000) n += (usz)snprintf(buf + n, sizeof(buf) - n, "%" PRId64 "d ", ms / 86400000);
  if (ms / 3600000 % 24) {
    n += (usz)snprintf(
      buf + n,
      sizeof(buf) - n,
      "%" PRId64 "h ",
      ms / 3600000 % 24
    );
  }

  if (ms / 60000 % 60) {
    n += (usz)snprintf(
      buf + n,
      sizeof(buf) - n,
      "%" PRId64 "m ",
      ms / 60000 % 60
    );
  }

  sec = ms % 60000;
  if (sec % 1000) {
    char num[16];

    put_trimmed(num, sizeof(num), (f64)sec / 1000, 3);
    n += (usz)snprintf(buf + n, sizeof(buf) - n, "%ss ", num);
  } else if (sec) {
    n += (usz)snprintf(buf + n, sizeof(buf) - n, "%" PRId64 "s ", sec / 1000);
  }

  buf[n - 1] = '\0';
  return jv_string(buf);
}

/* d, under a second, in the largest of ms, µs and ns it reaches. */
static jv short_duration(f64 d) {
  if (d == 0) return jv_string("0s");

  f64 v = fabs(d) * 1000;
  i32 u = 0;
  i32 dec;

  while (v < 1 && u < 2) {
    v *= 1000;
    u++;
  }

  v = round3(v, &dec);
  if (v >= 1000 && u > 0) {
    v = round3(v / 1000, &dec);
    u--;
  }

  char num[32];
  put_trimmed(num, sizeof(num), v, dec);

  char buf[48];
  snprintf(buf, sizeof(buf), "%s%s%s", d < 0 ? "-" : "", num, second_units[u]);
  return jv_string(buf);
}

static b8 is_time(view_id id) {
  return id == VIEW_UTC || id == VIEW_LOCAL || id == VIEW_DATE || id == VIEW_AGO;
}

static jv in_time(instant at, view_id id, const view_ctx* ctx) {
  return id == VIEW_AGO ? relative(at, ctx ? ctx->now_ms : 0) : timestamp(at, id);
}

/* at as an ISO-8601 time in UTC, in local time with its offset, or as a
 * date like 20 Jan 2026 15:53:20. */
static jv timestamp(instant at, view_id id) {
  i64 off = 0;

  if (id != VIEW_UTC && !local_offset(at.sec, &off)) return jv_invalid();

  civil c;
  char frac[16] = "";

  split(at.sec + off, &c);
  if (c.year < 0 || c.year > 9999) return jv_invalid();

  if (at.digits) snprintf(frac, sizeof(frac), ".%0*" PRId64, at.digits, at.frac);

  char buf[64];
  if (id == VIEW_UTC) {
    snprintf(
      buf,
      sizeof(buf),
      "%04" PRId64 "-%02d-%02dT%02d:%02d:%02d%sZ",
      c.year, c.mon, c.day, c.hour,
      c.min, c.sec, frac
    );
  } else if (id == VIEW_LOCAL) {
    i64 m = off < 0 ? -off / 60 : off / 60;

    snprintf(
      buf,
      sizeof(buf),
      "%04" PRId64 "-%02d-%02d %02d:%02d:%02d%s %c%02d:%02d",
      c.year, c.mon, c.day, c.hour,
      c.min, c.sec, frac,
      off < 0 ? '-' : '+',
      (i32)(m / 60),
      (i32)(m % 60)
    );
  } else {
    snprintf(
      buf,
      sizeof(buf),
      "%d %.3s %" PRId64 " %02d:%02d:%02d",
      c.day,
      months + (c.mon - 1) * 3,
      c.year, c.hour, c.min, c.sec
    );
  }

  return jv_string(buf);
}

/* at against now as its two largest parts, 2d 3h ago or in 5m 2s. */
static jv relative(instant at, i64 now_ms) {
  static const i64 sizes[] = { DAY, 3600, 60, 1 };
  i64 now = now_ms >= 0 ? now_ms / 1000 : (now_ms - 999) / 1000;
  i64 d = now - at.sec;
  u64 m = d < 0 ? (u64)0 - (u64)d : (u64)d;
  usz len = 0;
  i32 parts = 0;

  if (m == 0) return jv_string("now");

  char buf[64];
  if (d < 0) len += (usz)snprintf(buf, sizeof(buf), "in ");
  for (i32 i = 0; i < 4 && parts < 2; i++) {
    if (m < (u64)sizes[i] && parts == 0) continue;

    if (m < (u64)sizes[i]) break;

    len += (usz)snprintf(
      buf + len,
      sizeof(buf) - len,
      "%" PRIu64 "%c ",
      m / (u64)sizes[i],
      "dhms"[i]
    );
    m %= (u64)sizes[i];
    parts++;
  }

  if (d > 0) snprintf(buf + len, sizeof(buf) - len, "ago");
  else buf[len - 1] = '\0';

  return jv_string(buf);
}

/* n in unit, or with the unit guessed from its size: seconds below 1e11
 * (year 5138), then milliseconds, microseconds and nanoseconds. */
static instant to_instant(i64 n, time_unit unit) {
  static const i64 per[] = { 1, 1000, 1000000, 1000000000 };
  u64 m = n < 0 ? (u64)0 - (u64)n : (u64)n;
  i32 u = m < 100000000000ull ? 0 : m < 100000000000000ull ? 1 : m < 100000000000000000ull ? 2 : 3;

  if (unit != UNIT_AUTO) u = (i32)unit - 1;
  instant at = { n / per[u], n % per[u], u * 3 };

  if (at.frac < 0) {
    at.frac += per[u];
    at.sec--;
  }

  return at;
}

/* an RFC 3339 time, or ISO 8601's 2024-10-30, 2024-10-30 14:53 and the
 * like; UTC when it names no zone. */
static b8 parse_iso(const char* s, instant* at) {
  civil c = { 0 };
  i32 y;
  i64 off = 0;

  *at = (instant){ 0 };
  if (
    !digits(&s, 4, &y)
    || *s++ != '-'
    || !digits(&s, 2, &c.mon)
    || *s++ != '-'
    || !digits(&s, 2, &c.day)
  ) return 0;

  c.year = y;
  if (*s == '\0') return from_civil(&c, 0, at);

  if (*s != 'T' && *s != 't' && *s != ' ') return 0;

  s++;
  if (!clock_time(&s, &c)) return 0;

  if (*s == '.' || *s == ',') {
    i32 n = 0;

    for (s++; *s >= '0' && *s <= '9'; s++, n++)
      if (n < 9) at->frac = at->frac * 10 + (*s - '0');

    if (n == 0) return 0;

    at->digits = n <= 3 ? 3 : n <= 6 ? 6 : 9;
    for (i32 i = n < 9 ? n : 9; i < at->digits; i++) at->frac *= 10;
  }

  return parse_zone(s, &off) && from_civil(&c, off, at);
}

/* an RFC 2822 time, as mail and HTTP headers write it:
 * Wed, 30 Oct 2024 14:53:20 GMT. UTC when it names no zone. */
static b8 parse_rfc2822(const char* s, instant* at) {
  civil c = { 0 };
  i32 y;
  i64 off = 0;

  *at = (instant){ 0 };
  if (name_in(weekdays, s) >= 0 && s[3] == ',') s = skip_spaces(s + 4);

  if (!digits(&s, s[0] && s[1] >= '0' && s[1] <= '9' ? 2 : 1, &c.day) || *s++ != ' ') return 0;

  c.mon = name_in(months, s) + 1;
  if (c.mon == 0 || s[3] != ' ') return 0;

  s += 4;
  if (!digits(&s, 4, &y) || *s++ != ' ' || !clock_time(&s, &c)) return 0;

  c.year = y;
  if (*s == '\0') return from_civil(&c, 0, at);

  if (*s != ' ') return 0;

  s = skip_spaces(s);
  return (named_zone(s, &off) || (*s && parse_zone(s, &off))) && from_civil(&c, off, at);
}

/* hh:mm or hh:mm:ss at *s into c, moving past it. */
static b8 clock_time(const char** s, civil* c) {
  if (!digits(s, 2, &c->hour) || *(*s)++ != ':' || !digits(s, 2, &c->min)) return 0;

  if (**s != ':') return 1;

  (*s)++;
  return digits(s, 2, &c->sec);
}

/* Z, +01:00, -0330 or nothing, the whole of s, as seconds east of UTC. */
static b8 parse_zone(const char* s, i64* off) {
  i32 h;
  i32 m;
  i32 sign = *s == '-' ? -1 : 1;

  *off = 0;
  if (*s == '\0') return 1;

  if ((*s == 'Z' || *s == 'z') && s[1] == '\0') return 1;

  if (*s != '+' && *s != '-') return 0;

  s++;
  if (!digits(&s, 2, &h)) return 0;

  if (*s == ':') s++;
  if (!digits(&s, 2, &m) || *s || h > 23 || m > 59) return 0;

  *off = sign * (h * 3600 + m * 60);
  return 1;
}

/* one of RFC 2822's zone names, the whole of s, as seconds east of UTC. */
static b8 named_zone(const char* s, i64* off) {
  for (usz i = 0; i < ARRAY_LEN(zones); i++) {
    if (strcmp(s, zones[i].name) != 0) continue;

    *off = zones[i].hours * 3600;
    return 1;
  }

  return 0;
}

/* c, read in a zone off seconds east of UTC, as the seconds of at. False
 * when c is no real date and time. */
static b8 from_civil(const civil* c, i64 off, instant* at) {
  if (c->mon < 1 || c->mon > 12 || c->day < 1 || c->hour > 23 || c->min > 59 || c->sec > 60) return 0;

  civil back;

  at->sec = days_of(c->year, c->mon, c->day) * DAY;
  split(at->sec, &back);
  if (back.day != c->day) return 0;

  at->sec += c->hour * 3600 + c->min * 60 + c->sec - off;
  return 1;
}

/* exactly n decimal digits at *s, moving past them. */
static b8 digits(const char** s, i32 n, i32* out) {
  *out = 0;
  for (i32 i = 0; i < n; i++, (*s)++) {
    if (**s < '0' || **s > '9') return 0;

    *out = *out * 10 + (**s - '0');
  }

  return 1;
}

/* the index of the three letters at s in names, -1 when they are not
 * there. */
static i32 name_in(const char* names, const char* s) {
  for (i32 i = 0; names[i * 3]; i++)
    if (strncmp(names + i * 3, s, 3) == 0) return i;

  return -1;
}

static const char* skip_spaces(const char* s) {
  while (*s == ' ') s++;

  return s;
}

/* t seconds since the epoch as a proleptic gregorian date and time. */
static void split(i64 t, civil* c) {
  i64 days = t >= 0 ? t / DAY : (t - DAY + 1) / DAY;
  i64 rem = t - days * DAY;
  i64 z = days + 719468;
  i64 era = (z >= 0 ? z : z - 146096) / 146097;
  i64 doe = z - era * 146097;
  i64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  i64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  i64 mp = (5 * doy + 2) / 153;

  c->day = (i32)(doy - (153 * mp + 2) / 5 + 1);
  c->mon = (i32)(mp < 10 ? mp + 3 : mp - 9);
  c->year = yoe + era * 400 + (c->mon <= 2);
  c->hour = (i32)(rem / 3600);
  c->min = (i32)(rem / 60 % 60);
  c->sec = (i32)(rem % 60);
}

/* days from 1970-01-01 to y-m-d, the inverse of split. */
static i64 days_of(i64 y, i32 m, i32 d) {
  y -= m <= 2;

  i64 era = (y >= 0 ? y : y - 399) / 400;
  i64 yoe = y - era * 400;
  i64 doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  i64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

  return era * 146097 + doe - 719468;
}

/* the local zone's offset from UTC at t, in seconds. */
static b8 local_offset(i64 t, i64* off) {
  struct tm tm;
  i64 local;

  if (!local_tm((time_t)t, &tm)) return 0;

  local = days_of((i64)tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * DAY + tm.tm_hour * 3600
    + tm.tm_min * 60 + tm.tm_sec;

  *off = local - t;
  return 1;
}

/* curufmt variant */
#ifdef _WIN32

/* the CRT keeps localtime's result per thread. */
static b8 local_tm(time_t t, struct tm* out) {
  struct tm* tm = localtime(&t);

  if (!tm) return 0;

  *out = *tm;
  return 1;
}

#else

static b8 local_tm(time_t t, struct tm* out) {
  tzset();
  return localtime_r(&t, out) != NULL;
}

#endif

/* v rounded to three significant digits, dec of them after the point. */
static f64 round3(f64 v, i32* dec) {
  f64 scale;

  *dec = v >= 100 ? 0 : v >= 10 ? 1 : 2;
  scale = pow(10, *dec);
  return round(v * scale) / scale;
}

/* v with dec decimals, less its trailing zeros. */
static void put_trimmed(char* buf, usz cap, f64 v, i32 dec) {
  usz len;

  snprintf(buf, cap, "%.*f", dec, v);
  if (!strchr(buf, '.')) return;

  len = strlen(buf);
  while (buf[len - 1] == '0') buf[--len] = '\0';
  if (buf[len - 1] == '.') buf[--len] = '\0';
}

/* the bytes v decodes to from base64, as a string when they are text and as
 * hex otherwise; short strings must be text, since most words decode, and
 * so must hex strings, which unhex reads instead. A uuid is base64url too,
 * and never meant as such. */
static jv base64(jv v) {
  u8 id[16];

  if (parse_uuid(v, id)) return jv_invalid();

  usz len = (usz)jv_string_length_bytes(jv_copy(v));
  u8* buf = malloc(len / 4 * 3 + 3);
  i64 n = from_base64(jv_string_value(v), len, buf);
  b8 binary = len >= MIN_BINARY && from_hex(jv_string_value(v), len, NULL) < 0;
  jv out = jv_invalid();

  if (n > 0 && is_text(buf, (usz)n))
    out = jv_dump_string(jv_string_sized((const char*)buf, (i32)n), 0);
  else if (n > 0 && binary) out = hex_dump(buf, (usz)n);

  free(buf);
  return out;
}

/* v with its %XX escapes undone, when it has one and the result is text.
 * A + stays a +, as outside form data. */
static jv url_decode(jv v) {
  const char* s = jv_string_value(v);
  usz len = (usz)jv_string_length_bytes(jv_copy(v));
  jv out = memchr(s, '%', len) ? unescape(s, len, 0) : jv_invalid();

  return jv_is_valid(out) ? jv_dump_string(out, 0) : out;
}

static i32 hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';

  if (c >= 'a' && c <= 'f') return c - 'a' + 10;

  if (c >= 'A' && c <= 'F') return c - 'A' + 10;

  return -1;
}

/* the bytes v's hex digits spell, as a string when they are text and as
 * spaced hex otherwise. Shorter than MIN_HEX, too many words and numbers
 * would qualify. */
static jv unhex(jv v) {
  usz len = (usz)jv_string_length_bytes(jv_copy(v));

  if (len < MIN_HEX) return jv_invalid();

  u8* buf = malloc(len / 2 + 1);
  i64 n = from_hex(jv_string_value(v), len, buf);
  jv out = jv_invalid();

  if (n > 0 && is_text(buf, (usz)n))
    out = jv_dump_string(jv_string_sized((const char*)buf, (i32)n), 0);
  else if (n > 0) out = hex_dump(buf, (usz)n);

  free(buf);
  return out;
}

/* s as pairs of hex digits after an optional 0x, into out unless it is
 * NULL. Returns the byte count, -1 when s is not that. */
static i64 from_hex(const char* s, usz len, u8* out) {
  if (len > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
    len -= 2;
  }

  if (len == 0 || len % 2) return -1;

  for (usz i = 0; i < len; i += 2) {
    i32 hi = hex_digit(s[i]);
    i32 lo = hex_digit(s[i + 1]);

    if (hi < 0 || lo < 0) return -1;

    if (out) out[i / 2] = (u8)(hi << 4 | lo);
  }

  return (i64)(len / 2);
}

/* s in base64 or base64url, padded or not, into out. Returns the byte
 * count, -1 when s is neither or mixes the two. */
static i64 from_base64(const char* s, usz len, u8* out) {
  char alphabet = 0;
  u32 acc = 0;
  i64 n = 0;
  usz pad = 0;

  while (pad < 2 && len > pad && s[len - pad - 1] == '=') pad++;
  if (pad && len % 4) return -1;

  len -= pad;
  if (len % 4 == 1) return -1;

  for (usz i = 0; i < len; i++) {
    i32 d = base64_digit(s[i], &alphabet);

    if (d < 0) return -1;

    acc = acc << 6 | (u32)d;
    if (i % 4 == 3) {
      out[n++] = (u8)(acc >> 16);
      out[n++] = (u8)(acc >> 8);
      out[n++] = (u8)acc;
      acc = 0;
    }
  }

  if (len % 4 == 2) {
    if (acc & 0xf) return -1;

    out[n++] = (u8)(acc >> 4);
  } else if (len % 4 == 3) {
    if (acc & 0x3) return -1;

    out[n++] = (u8)(acc >> 10);
    out[n++] = (u8)(acc >> 2);
  }

  return n;
}

/* c's value, -1 when it is no digit or belongs to the other alphabet than
 * the one *alphabet already saw ('+' or '-', 0 for none yet). */
static i32 base64_digit(char c, char* alphabet) {
  if (c >= 'A' && c <= 'Z') return c - 'A';

  if (c >= 'a' && c <= 'z') return c - 'a' + 26;

  if (c >= '0' && c <= '9') return c - '0' + 52;

  char kind = c == '+' || c == '/' ? '+' : c == '-' || c == '_' ? '-' : 0;

  if (!kind || (*alphabet && *alphabet != kind)) return -1;

  *alphabet = kind;
  return c == '+' || c == '-' ? 62 : 63;
}

/* whether b is valid UTF-8 without control characters other than tab and
 * line breaks. */
static b8 is_text(const u8* b, usz len) {
  jv s = jv_string_sized((const char*)b, (i32)len);
  b8 same = (usz)jv_string_length_bytes(jv_copy(s)) == len;
  same = same && memcmp(jv_string_value(s), b, len) == 0;

  jv_free(s);
  if (!same) return 0;

  for (usz i = 0; i < len; i++) {
    if (b[i] == 0x7f || (b[i] < 0x20 && b[i] != '\t' && b[i] != '\n' && b[i] != '\r')) return 0;

    if (b[i] == 0xc2 && i + 1 < len && b[i + 1] < 0xa0) return 0;
  }

  return 1;
}

/* b as hex pairs, one space apart, cut after MAX_DUMP bytes. */
static jv hex_dump(const u8* b, usz len) {
  char buf[MAX_DUMP * 3 + 8];
  usz n = 0;

  for (usz i = 0; i < len && i < MAX_DUMP; i++)
    n += (usz)snprintf(buf + n, sizeof(buf) - n, i ? " %02x" : "%02x", b[i]);

  if (len > MAX_DUMP) snprintf(buf + n, sizeof(buf) - n, " \xe2\x80\xa6");

  return jv_string(buf);
}

/* v's uuid version, with its time in UTC when it holds one; nil, max, or
 * the variant's name when it is not RFC 9562's. */
static jv uuid(jv v) {
  static const char* const variants[] = { "ncs", "ncs", "ncs", "ncs", "", "", "microsoft", "future" };
  u8 b[16];
  b8 zero = 1;
  b8 ones = 1;

  if (!parse_uuid(v, b)) return jv_invalid();

  for (i32 i = 0; i < 16; i++) {
    zero &= b[i] == 0;
    ones &= b[i] == 0xff;
  }

  if (zero || ones) return jv_string(zero ? "nil" : "max");

  if ((b[8] & 0xc0) != 0x80) return jv_string(variants[b[8] >> 5]);

  instant at;
  char buf[48];

  if (!uuid_time(b, &at)) {
    snprintf(buf, sizeof(buf), "v%d", b[6] >> 4);
    return jv_string(buf);
  }

  jv time = timestamp(at, VIEW_UTC);

  snprintf(buf, sizeof(buf), "v%d %s", b[6] >> 4, jv_is_valid(time) ? jv_string_value(time) : "");
  jv_free(time);
  return jv_string(buf);
}

/* the time a v1, v6 or v7 uuid string holds. */
static b8 uuid_instant(jv v, instant* at) {
  u8 b[16];

  return parse_uuid(v, b) && uuid_time(b, at);
}

/* v in the 8-4-4-4-12 hex form, either case, into out. */
static b8 parse_uuid(jv v, u8* out) {
  const char* s = jv_string_value(v);
  usz n = 0;

  if (jv_get_kind(v) != JV_KIND_STRING || jv_string_length_bytes(jv_copy(v)) != 36) return 0;

  for (usz i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-') return 0;

      continue;
    }

    i32 d = hex_digit(s[i]);

    if (d < 0) return 0;

    out[n / 2] = (u8)(n % 2 ? out[n / 2] << 4 | d : d);
    n++;
  }

  return 1;
}

/* the time in uuid b: unix milliseconds for v7, 100 ns ticks since the
 * gregorian reform for v1 and v6, which only order their fields apart. */
static b8 uuid_time(const u8* b, instant* at) {
  i32 version = b[6] >> 4;
  u64 t;

  if ((b[8] & 0xc0) != 0x80) return 0;

  if (version == 7) {
    *at = to_instant((i64)be(b, 6), UNIT_MS);
    return 1;
  }

  if (version == 1) t = (be(b + 6, 2) & 0xfff) << 48 | be(b + 4, 2) << 32 | be(b, 4);
  else if (version == 6) t = be(b, 6) << 12 | (be(b + 6, 2) & 0xfff);
  else return 0;

  i64 ticks = (i64)t - GREGORIAN;

  at->sec = ticks >= 0 ? ticks / 10000000 : (ticks - 9999999) / 10000000;
  at->frac = ticks - at->sec * 10000000;
  at->digits = 7;
  return 1;
}

/* n bytes at b as a big-endian integer. */
static u64 be(const u8* b, i32 n) {
  u64 t = 0;

  for (i32 i = 0; i < n; i++) t = t << 8 | b[i];

  return t;
}

/* the object or array string v holds; other values would make a tree of
 * one leaf. */
static jv embedded_json(jv v) {
  jv out = jv_parse_sized(jv_string_value(v), jv_string_length_bytes(jv_copy(v)));
  jv_kind kind = jv_get_kind(out);

  if (kind == JV_KIND_OBJECT || kind == JV_KIND_ARRAY) return out;

  jv_free(out);
  return jv_invalid();
}

/* the header and payload of the json web token v: three base64url parts
 * split by dots, the first two objects, the signature possibly empty. */
static jv jwt(jv v) {
  const char* s = jv_string_value(v);
  const char* dot = strchr(s, '.');
  const char* sig = dot ? strchr(dot + 1, '.') : NULL;

  if (
    !sig
    || strspn(
      sig + 1,
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
    ) != strlen(sig + 1)
  ) return jv_invalid();

  jv header = jwt_part(s, (usz)(dot - s));
  jv payload = jwt_part(dot + 1, (usz)(sig - dot - 1));

  if (!jv_is_valid(header) || !jv_is_valid(payload)) {
    jv_free(header);
    jv_free(payload);
    return jv_invalid();
  }

  return jv_object_set(
    jv_object_set(jv_object(), jv_string("header"), header),
    jv_string("payload"),
    payload
  );
}

/* the object len bytes of base64 at s hold. */
static jv jwt_part(const char* s, usz len) {
  u8* buf = malloc(len / 4 * 3 + 3);
  i64 n = from_base64(s, len, buf);
  jv out = n > 0 ? jv_parse_sized((const char*)buf, (i32)n) : jv_invalid();

  free(buf);
  if (jv_get_kind(out) == JV_KIND_OBJECT) return out;

  jv_free(out);
  return jv_invalid();
}

/* the fields of query string v, or of the query in url v, each name to its
 * value, or to an array of them when it repeats. Needs a field with both a
 * name and a value, so that base64 padding is no query. */
static jv query_string(jv v) {
  const char* s = jv_string_value(v);
  const char* q = strchr(s, '?');
  jv fields = jv_object();
  b8 full = 0;

  if (q) s = q + 1;

  usz len = strcspn(s, "#");

  for (usz i = 0; i < len; i++) {
    if ((u8)s[i] <= ' ' || s[i] == 0x7f) {
      jv_free(fields);
      return jv_invalid();
    }
  }

  for (usz i = 0; i < len;) {
    usz n = strcspn(s + i, "&#");
    const char* eq = memchr(s + i, '=', n);

    if (n && !add_field(&fields, s + i, n)) {
      jv_free(fields);
      return jv_invalid();
    }

    full |= eq && eq > s + i && eq < s + i + n - 1;
    i += n + 1;
  }

  if (full) return fields;

  jv_free(fields);
  return jv_invalid();
}

/* name=value, or a bare name for "", from len bytes at s into fields. */
static b8 add_field(jv* fields, const char* s, usz len) {
  const char* eq = memchr(s, '=', len);
  usz klen = eq ? (usz)(eq - s) : len;
  jv key = unescape(s, klen, 1);
  jv val = eq ? unescape(eq + 1, len - klen - 1, 1) : jv_string("");

  if (!jv_is_valid(key) || !jv_is_valid(val) || jv_string_length_bytes(jv_copy(key)) == 0) {
    jv_free(key);
    jv_free(val);
    return 0;
  }

  jv old = jv_object_get(jv_copy(*fields), jv_copy(key));

  if (jv_get_kind(old) == JV_KIND_ARRAY) val = jv_array_append(old, val);
  else if (jv_is_valid(old)) val = jv_array_append(jv_array_append(jv_array(), old), val);
  else jv_free(old);

  *fields = jv_object_set(*fields, key, val);
  return 1;
}

/* len bytes at s with their %XX escapes undone, and + as a space in form
 * data, when the result is text. */
static jv unescape(const char* s, usz len, b8 form) {
  u8* buf = malloc(len + 1);
  usz n = 0;
  jv out = jv_invalid();

  for (usz i = 0; i < len; i++) {
    if (s[i] != '%') {
      buf[n++] = form && s[i] == '+' ? ' ' : (u8)s[i];
      continue;
    }

    i32 hi = i + 2 < len ? hex_digit(s[i + 1]) : -1;
    i32 lo = hi >= 0 ? hex_digit(s[i + 2]) : -1;

    if (lo < 0) {
      free(buf);
      return jv_invalid();
    }

    buf[n++] = (u8)(hi << 4 | lo);
    i += 2;
  }

  if (is_text(buf, n)) out = jv_string_sized((const char*)buf, (i32)n);

  free(buf);
  return out;
}
