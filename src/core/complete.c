/* complete - finds the word at the cursor and the probe for its keys. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "complete.h"

#define NO_OPENER ((usz) - 1)
#define PROBE     "__lq_probe"
#define PUT(t, s) put(t, s, sizeof(s) - 1)
#define PROBE_DEF                                                                              \
   "def " PROBE ": [\"" COMPLETE_TAG "\", if type == \"object\" then [to_entries[] | [.key, "  \
   "(.value | type)]] else [] end] | debug | empty; "

/* what each open bracket, string or if is: ( [ { ", i for an interpolation,
 * c for an if's condition, t for its branches. */
typedef struct {
  const char* s;
  usz len;     /* up to the cursor */
  char* kinds;
  usz* at;     /* where each opened */
  usz depth;
  usz* opener; /* per closing byte, where what it closes opened */
  b8 comment;
} scan;

typedef struct {
  char* s;
  usz n;
  usz cap;
} text;

static void scan_run(scan* sc);
static usz scan_string(scan* sc, usz i);
static usz scan_code(scan* sc, usz i);
static usz closing(scan* sc, usz i);
static void push(scan* sc, char kind, usz at);
static void pop(scan* sc, usz i);
static void keyword(scan* sc, usz i, usz end);
static usz term_start(const scan* sc, usz p);
static b8 fresh(const scan* sc, usz p);
static char* probe_of(const scan* sc, usz dot, usz term);
static char* fallback_of(const scan* sc, usz dot, usz term);
static void close_all(const scan* sc, text* t);
static void put(text* t, const char* s, usz n);
static usz word_back(const char* s, usz p);
static b8 is_word(const char* s, usz from, usz to, const char* w);
static b8 leads_term(const char* s, usz from, usz to);
static b8 name_start(char c);
static b8 name_byte(char c);

void complete_at(const char* s, usz len, usz cursor, complete_ctx* out) {
  memset(out, 0, sizeof(*out));
  if (cursor > len) return;

  scan sc = { 0 };
  sc.s = s;
  sc.len = cursor;
  sc.kinds = malloc(cursor + 1);
  sc.at = malloc(sizeof(usz) * (cursor + 1));
  sc.opener = malloc(sizeof(usz) * (cursor + 1));
  if (!sc.kinds || !sc.at || !sc.opener) abort();

  scan_run(&sc);

  usz w = word_back(s, cursor);
  out->start = w;
  out->end = cursor;
  while (out->end < len && name_byte(s[out->end])) out->end++;

  b8 open = sc.comment || (sc.depth > 0 && sc.kinds[sc.depth - 1] == '"');
  if (open || (w < cursor && isdigit((u8)s[w]))) {
    out->kind = COMPLETE_NONE;
  } else if (w > 0 && s[w - 1] == '.') {
    usz dot = w - 1;
    usz before = word_back(s, dot);

    b8 number = before < dot && isdigit((u8)s[before]);
    if ((dot > 0 && s[dot - 1] == '.') || number) {
      out->kind = COMPLETE_NONE;
    } else {
      usz term = term_start(&sc, dot);

      out->kind = COMPLETE_KEY;
      out->probe = probe_of(&sc, dot, term);
      out->fallback = fallback_of(&sc, dot, term);
    }
  } else if (w > 0 && (s[w - 1] == '$' || s[w - 1] == '@')) {
    out->kind = COMPLETE_NONE;
  } else
    out->kind = fresh(&sc, w) ? COMPLETE_BUILTIN : COMPLETE_NONE;

  free(sc.kinds);
  free(sc.at);
  free(sc.opener);
}

void complete_free(complete_ctx* c) {
  free(c->probe);
  free(c->fallback);
  c->probe = NULL;
  c->fallback = NULL;
}

static void scan_run(scan* sc) {
  usz i = 0;

  for (usz k = 0; k < sc->len; k++) sc->opener[k] = NO_OPENER;

  while (i < sc->len && !sc->comment)
    if (sc->depth > 0 && sc->kinds[sc->depth - 1] == '"') i = scan_string(sc, i);
    else i = scan_code(sc, i);
}

static usz scan_string(scan* sc, usz i) {
  char c = sc->s[i];

  if (c == '"') {
    pop(sc, i);
    return i + 1;
  }

  if (c != '\\' || i + 1 >= sc->len) return i + 1;
  if (sc->s[i + 1] == '(') push(sc, 'i', i);

  return i + 2;
}

static usz scan_code(scan* sc, usz i) {
  char c = sc->s[i];

  if (c == '#') {
    sc->comment = 1;
    return sc->len;
  }

  if (c == '"' || c == '(' || c == '[' || c == '{') {
    push(sc, c, i);
    return i + 1;
  }

  if (c == ')' || c == ']' || c == '}') return closing(sc, i);
  if (!name_byte(c)) return i + 1;

  usz end = i;
  while (end < sc->len && name_byte(sc->s[end])) end++;

  b8 plain = i == 0 || (sc->s[i - 1] != '.' && sc->s[i - 1] != '$' && sc->s[i - 1] != '@');
  if (plain && name_start(c)) keyword(sc, i, end);

  return end;
}

/* a closer that does not match leaves the opener open, as jq would fail
 * there anyway. */
static usz closing(scan* sc, usz i) {
  char c = sc->s[i];
  char top = sc->depth > 0 ? sc->kinds[sc->depth - 1] : 0;
  char want = c == ')' ? '(' : c == ']' ? '[' : '{';

  if (top == want || (c == ')' && top == 'i')) pop(sc, i);

  return i + 1;
}

static void push(scan* sc, char kind, usz at) {
  sc->kinds[sc->depth] = kind;
  sc->at[sc->depth] = at;
  sc->depth++;
}

static void pop(scan* sc, usz i) {
  sc->depth--;
  sc->opener[i] = sc->at[sc->depth];
}

static void keyword(scan* sc, usz i, usz end) {
  char* top = sc->depth > 0 ? &sc->kinds[sc->depth - 1] : NULL;

  if (is_word(sc->s, i, end, "if")) push(sc, 'c', i);
  else if (is_word(sc->s, i, end, "then") && top && *top == 'c') *top = 't';
  else if (is_word(sc->s, i, end, "elif") && top && *top == 't') *top = 'c';
  else if (is_word(sc->s, i, end, "end") && top && (*top == 'c' || *top == 't')) sc->depth--;
}

/* where the term ending at p begins, walking back over its suffixes, as in
 * `.users[0]?`, `$x."a"` or `map(.a)`. p itself when no term ends there. */
static usz term_start(const scan* sc, usz p) {
  const char* s = sc->s;

  for (;;) {
    if (p == 0) return p;

    char c = s[p - 1];
    if (c == '?') {
      p--;
    } else if (c == ']' || c == ')' || c == '"') {
      if (sc->opener[p - 1] == NO_OPENER) return p;

      p = sc->opener[p - 1];
      if (c == '"' && (p == 0 || s[p - 1] != '.')) return p;
    } else if (name_byte(c)) {
      usz q = word_back(s, p);

      if (q > 0 && s[q - 1] == '.') p = q;
      else if (q > 0 && s[q - 1] == '$') return q - 1;
      else return leads_term(s, q, p) ? p : q;
    } else if (c == '.') {
      p--;
      if (p > 0 && s[p - 1] == '.') return p - 1;
      if (p == 0 || (!name_byte(s[p - 1]) && !strchr("?])\"", s[p - 1]))) return p;
    } else
      return p;
  }
}

/* true when a function can be called at p: at the start, after an operator
 * or an opener, or after a keyword that leads a term. */
static b8 fresh(const scan* sc, usz p) {
  const char* s = sc->s;

  while (p > 0 && (s[p - 1] == ' ' || s[p - 1] == '\t')) p--;
  if (p == 0) return 1;

  char c = s[p - 1];
  b8 in_object = sc->depth > 0 && sc->kinds[sc->depth - 1] == '{';
  if ((c == '{' || c == ',') && in_object) return 0;
  if (strchr("|,(;:[{+-*/%=<>!", c)) return 1;
  if (!name_byte(c)) return 0;

  usz q = word_back(s, p);
  b8 plain = q == 0 || (s[q - 1] != '.' && s[q - 1] != '$' && s[q - 1] != '@');
  return plain && leads_term(s, q, p);
}

/* the query cut at the cursor with the dot and the half-typed key replaced
 * by the probe, applied to the term before the dot when there is one. */
static char* probe_of(const scan* sc, usz dot, usz term) {
  text t = { 0 };

  PUT(&t, PROBE_DEF);
  put(&t, sc->s, term);
  if (term == dot) {
    PUT(&t, PROBE);
  } else {
    PUT(&t, "(");
    put(&t, sc->s + term, dot - term);
    PUT(&t, " | " PROBE ")");
  }

  close_all(sc, &t);
  return t.s;
}

static char* fallback_of(const scan* sc, usz dot, usz term) {
  text t = { 0 };

  PUT(&t, PROBE_DEF);
  if (term < dot && sc->s[term] == '.') {
    put(&t, sc->s + term, dot - term);
    PUT(&t, " | ");
  }

  PUT(&t, PROBE);
  return t.s;
}

static void close_all(const scan* sc, text* t) {
  for (usz k = sc->depth; k-- > 0;) {
    switch (sc->kinds[k]) {
      case '(':
      case 'i':
        PUT(t, ")");
        break;
      case '[':
        PUT(t, "]");
        break;
      case '{':
        PUT(t, "}");
        break;
      case '"':
        PUT(t, "\"");
        break;
      case 'c':
        PUT(t, " then . end");
        break;
      default:
        PUT(t, " end");
        break;
    }
  }
}

static void put(text* t, const char* s, usz n) {
  if (t->n + n + 1 > t->cap) {
    t->cap = (t->n + n + 1) * 2;
    t->s = realloc(t->s, t->cap);
    if (!t->s) abort();
  }

  memcpy(t->s + t->n, s, n);
  t->n += n;
  t->s[t->n] = '\0';
}

/* the start of the name bytes that end at p. */
static usz word_back(const char* s, usz p) {
  while (p > 0 && name_byte(s[p - 1])) p--;

  return p;
}

static b8 is_word(const char* s, usz from, usz to, const char* w) {
  return strlen(w) == to - from && memcmp(s + from, w, to - from) == 0;
}

static b8 leads_term(const char* s, usz from, usz to) {
  static const char* const words[] = {
      "if", "then", "elif", "else", "and", "or", "reduce", "foreach", "try", "catch",
  };

  for (usz k = 0; k < ARRAY_LEN(words); k++)
    if (is_word(s, from, to, words[k])) return 1;

  return 0;
}

static b8 name_start(char c) {
  u8 b = (u8)c;

  return isalpha(b) || b == '_';
}

static b8 name_byte(char c) {
  u8 b = (u8)c;

  return isalnum(b) || b == '_';
}
