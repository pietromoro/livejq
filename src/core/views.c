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
#define MAX_DUMP    256 /* bytes shown as hex, the rest cut */

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
static i64 from_base64(const char* s, usz len, u8* out);
static i32 base64_digit(char c, char* alphabet);
static b8 is_text(const u8* b, usz len);
static jv hex_dump(const u8* b, usz len);

static const char* const iec_units[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", NULL };
static const char* const si_units[] = { "B", "kB", "MB", "GB", "TB", "PB", "EB", NULL };
static const char* const second_units[] = { "ms", "\xc2\xb5s", "ns" };
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

  for (i32 id = 0; id < VIEW_COUNT && n < cap; id++) {
    jv text = id == VIEW_DEFAULT ? jv_true() : view_text(v, (view_id)id, NULL);

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
    case VIEW_BYTES: return "iec";
    case VIEW_BYTES_SI: return "si";
    case VIEW_DURATION: return "dur";
    case VIEW_UTC: return "utc";
    case VIEW_LOCAL: return "local";
    case VIEW_DATE: return "date";
    case VIEW_AGO: return "ago";
    case VIEW_BASE64: return "b64";
    default: return "";
  }
}

b8 view_takes_unit(jv v, view_id id) { return jv_get_kind(v) == JV_KIND_NUMBER && is_time(id); }

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

    return parse_iso(s, &at) || parse_rfc2822(s, &at) ? in_time(at, id, ctx) : jv_invalid();
  }

  if (jv_get_kind(v) == JV_KIND_STRING && id == VIEW_BASE64) return base64(v);

  if (jv_get_kind(v) != JV_KIND_NUMBER) return jv_invalid();

  switch (id) {
    case VIEW_HEX: return as_int(v, &n) ? in_base(n, 16, "0x") : jv_invalid();
    case VIEW_OCTAL: return as_int(v, &n) ? in_base(n, 8, "0o") : jv_invalid();
    case VIEW_BINARY: return as_int(v, &n) ? in_base(n, 2, "0b") : jv_invalid();
    case VIEW_EXPONENT: return exponent(jv_number_value(v));
    case VIEW_BYTES: return as_int(v, &n) ? bytes(n, 1024, iec_units) : jv_invalid();
    case VIEW_BYTES_SI: return as_int(v, &n) ? bytes(n, 1000, si_units) : jv_invalid();
    case VIEW_DURATION: return duration(jv_number_value(v));
    case VIEW_UTC:
    case VIEW_LOCAL:
    case VIEW_DATE:
    case VIEW_AGO: return as_int(v, &n) ? in_time(to_instant(n, unit), id, ctx) : jv_invalid();
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

/* n bytes in the largest unit it reaches, to three significant digits. */
static jv bytes(i64 n, f64 base, const char* const* units) {
  f64 v = fabs((f64)n);
  i32 u = 0;
  i32 dec;
  char num[32];
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
  if (ms / 3600000 % 24) n += (usz)snprintf(buf + n, sizeof(buf) - n, "%" PRId64 "h ", ms / 3600000 % 24);
  if (ms / 60000 % 60) n += (usz)snprintf(buf + n, sizeof(buf) - n, "%" PRId64 "m ", ms / 60000 % 60);

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
  char num[32];
  char buf[48];

  while (v < 1 && u < 2) {
    v *= 1000;
    u++;
  }

  v = round3(v, &dec);
  if (v >= 1000 && u > 0) {
    v = round3(v / 1000, &dec);
    u--;
  }

  put_trimmed(num, sizeof(num), v, dec);
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
  char buf[64];

  split(at.sec + off, &c);
  if (c.year < 0 || c.year > 9999) return jv_invalid();

  if (at.digits) snprintf(frac, sizeof(frac), ".%0*" PRId64, at.digits, at.frac);

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
  char buf[64];
  usz len = 0;
  i32 parts = 0;

  if (m == 0) return jv_string("now");

  if (d < 0) len += (usz)snprintf(buf, sizeof(buf), "in ");
  for (i32 i = 0; i < 4 && parts < 2; i++) {
    if (m < (u64)sizes[i] && parts == 0) continue;

    if (m < (u64)sizes[i]) break;

    len += (usz)snprintf(buf + len, sizeof(buf) - len, "%" PRIu64 "%c ", m / (u64)sizes[i], "dhms"[i]);
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
  if (!digits(&s, 4, &y) || *s++ != '-' || !digits(&s, 2, &c.mon) || *s++ != '-' || !digits(&s, 2, &c.day))
    return 0;

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
 * hex otherwise; short strings must be text, since most words decode. */
static jv base64(jv v) {
  usz len = (usz)jv_string_length_bytes(jv_copy(v));
  u8* buf = malloc(len / 4 * 3 + 3);
  i64 n = from_base64(jv_string_value(v), len, buf);
  jv out = jv_invalid();

  if (n > 0 && is_text(buf, (usz)n)) out = jv_dump_string(jv_string_sized((const char*)buf, (i32)n), 0);
  else if (n > 0 && len >= MIN_BINARY) out = hex_dump(buf, (usz)n);

  free(buf);
  return out;
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
  b8 same = (usz)jv_string_length_bytes(jv_copy(s)) == len && memcmp(jv_string_value(s), b, len) == 0;

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

  for (usz i = 0; i < len && i < MAX_DUMP; i++) n += (usz)snprintf(buf + n, sizeof(buf) - n, i ? " %02x" : "%02x", b[i]);

  if (len > MAX_DUMP) snprintf(buf + n, sizeof(buf) - n, " \xe2\x80\xa6");

  return jv_string(buf);
}
