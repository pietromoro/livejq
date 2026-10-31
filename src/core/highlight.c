/* highlight - a tolerant tokenizer of jq, styling each byte of a query. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "grid.h"
#include "highlight.h"

typedef struct {
  const char* s;
  usz len;
  u8* styles;
  usz* opens; /* offsets of open brackets, quotes and interpolations */
  usz depth;
} lexer;

static usz code(lexer* lx, usz i);
static usz string(lexer* lx, usz i);
static usz opening(lexer* lx, usz i, usz n, u8 st);
static usz closing(lexer* lx, usz i);
static usz dot(lexer* lx, usz i);
static usz word(lexer* lx, usz i);
static usz number(lexer* lx, usz i);
static usz operator(lexer* lx, usz i);
static usz invalid(lexer* lx, usz i);
static usz paint(lexer* lx, usz from, usz to, u8 st);
static usz name_end(const lexer* lx, usz i);
static b8 starts_key(const lexer* lx, usz i);
static u8 word_style(const char* s, usz n);
static b8 in_string(const lexer* lx);
static b8 name_start(char c);
static b8 name_byte(char c);

void highlight(const char* s, usz len, u8* styles) {
  lexer lx = { s, len, styles, malloc(sizeof(usz) * (len + 1)), 0 };
  usz i = 0;

  if (!lx.opens) abort();

  memset(styles, STYLE_NORMAL, len);
  while (i < len) i = in_string(&lx) ? string(&lx, i) : code(&lx, i);

  while (lx.depth > 0) {
    usz at = lx.opens[--lx.depth];

    paint(&lx, at, at + (s[at] == '\\' ? 2 : 1), STYLE_ERROR);
  }

  free(lx.opens);
}

static usz code(lexer* lx, usz i) {
  char c = lx->s[i];

  if (c == ' ' || c == '\t') return i + 1;
  if (c == '#') return paint(lx, i, lx->len, STYLE_COMMENT);
  if (c == '"') return opening(lx, i, 1, STYLE_STRING);
  if (c == '(' || c == '[' || c == '{') return opening(lx, i, 1, STYLE_PUNCT);
  if (c == ')' || c == ']' || c == '}') return closing(lx, i);
  if (c == ':' || c == ';') return paint(lx, i, i + 1, STYLE_PUNCT);
  if (c == '.') return dot(lx, i);
  if (c == '$') return paint(lx, i, name_end(lx, i + 1), STYLE_VARIABLE);
  if (c == '@') return paint(lx, i, name_end(lx, i + 1), STYLE_FORMAT);
  if (isdigit((u8)c)) return number(lx, i);
  if (name_start(c)) return word(lx, i);
  return operator(lx, i);
}

static usz string(lexer* lx, usz i) {
  while (i < lx->len) {
    char c = lx->s[i];

    if (c == '"') {
      lx->depth--;
      return paint(lx, i, i + 1, STYLE_STRING);
    }

    if (c == '\\' && i + 1 < lx->len && lx->s[i + 1] == '(') return opening(lx, i, 2, STYLE_PUNCT);

    i = paint(lx, i, c == '\\' && i + 1 < lx->len ? i + 2 : i + 1, STYLE_STRING);
  }

  return i;
}

static usz opening(lexer* lx, usz i, usz n, u8 st) {
  lx->opens[lx->depth++] = i;
  return paint(lx, i, i + n, st);
}

/* a closer that does not match the innermost opener is an error and
 * leaves the opener open. */
static usz closing(lexer* lx, usz i) {
  if (lx->depth == 0) return paint(lx, i, i + 1, STYLE_ERROR);

  char c = lx->s[i];
  char want = c == ')' ? '(' : c == ']' ? '[' : '{';
  char top = lx->s[lx->opens[lx->depth - 1]];

  if (top != want && !(c == ')' && top == '\\')) return paint(lx, i, i + 1, STYLE_ERROR);

  lx->depth--;
  return paint(lx, i, i + 1, STYLE_PUNCT);
}

static usz dot(lexer* lx, usz i) {
  if (i + 1 < lx->len && isdigit((u8)lx->s[i + 1])) return number(lx, i);
  if (i + 1 < lx->len && lx->s[i + 1] == '.') return paint(lx, i, i + 2, STYLE_KEY);
  if (i + 1 < lx->len && name_start(lx->s[i + 1])) return paint(lx, i, name_end(lx, i + 1), STYLE_KEY);
  return paint(lx, i, i + 1, STYLE_KEY);
}

static usz word(lexer* lx, usz i) {
  usz end = name_end(lx, i);

  if (starts_key(lx, i)) return paint(lx, i, end, STYLE_KEY);
  return paint(lx, i, end, word_style(lx->s + i, end - i));
}

static usz number(lexer* lx, usz i) {
  usz start = i;

  while (i < lx->len && isdigit((u8)lx->s[i])) i++;
  if (i < lx->len && lx->s[i] == '.') {
    i++;
    while (i < lx->len && isdigit((u8)lx->s[i])) i++;
  }

  if (i < lx->len && (lx->s[i] == 'e' || lx->s[i] == 'E')) {
    usz j = i + 1;

    if (j < lx->len && (lx->s[j] == '+' || lx->s[j] == '-')) j++;
    if (j < lx->len && isdigit((u8)lx->s[j])) {
      i = j;
      while (i < lx->len && isdigit((u8)lx->s[i])) i++;
    }
  }

  return paint(lx, start, i, STYLE_NUMBER);
}

/* longest first, so that `//=` is not read as `//` and `=`. */
static usz operator(lexer* lx, usz i) {
  static const char* const ops[] = {
      "?//", "//=", "|=", "+=", "-=", "*=", "/=", "%=", "==", "!=", "<=", ">=", "//",
      "|",   ",",   "+",  "-",  "*",  "/",  "%",  "=",  "<",  ">",  "?",
  };

  for (usz k = 0; k < ARRAY_LEN(ops); k++) {
    usz n = strlen(ops[k]);

    if (n <= lx->len - i && memcmp(lx->s + i, ops[k], n) == 0) return paint(lx, i, i + n, STYLE_OPERATOR);
  }

  return invalid(lx, i);
}

static usz invalid(lexer* lx, usz i) {
  usz end = i + 1;

  while (end < lx->len && ((u8)lx->s[end] & 0xc0) == 0x80) end++;

  return paint(lx, i, end, STYLE_ERROR);
}

static usz paint(lexer* lx, usz from, usz to, u8 st) {
  memset(lx->styles + from, st, to - from);
  return to;
}

/* names may be qualified by a module, as in `lib::f`. */
static usz name_end(const lexer* lx, usz i) {
  for (;;) {
    while (i < lx->len && name_byte(lx->s[i])) i++;
    if (i + 2 >= lx->len || lx->s[i] != ':' || lx->s[i + 1] != ':' || !name_start(lx->s[i + 2])) return i;

    i += 2;
  }
}

/* a word right after `{` or `,` inside an object is a key, as in `{a: 1}`
 * or `{a, b}`. */
static b8 starts_key(const lexer* lx, usz i) {
  if (lx->depth == 0 || lx->s[lx->opens[lx->depth - 1]] != '{') return 0;

  while (i > 0 && (lx->s[i - 1] == ' ' || lx->s[i - 1] == '\t')) i--;

  return i > 0 && (lx->s[i - 1] == '{' || lx->s[i - 1] == ',');
}

/* any other word names a function, builtin or defined by the query. */
static u8 word_style(const char* s, usz n) {
  static const char* const keywords[] = {
      "def",     "as",    "if",  "then",  "elif", "else",  "end",    "and",     "or",     "reduce",
      "foreach", "try",   "catch", "label", "break", "import", "include", "module", "__loc__",
  };
  static const struct {
    const char* name;
    u8 style;
  } literals[] = {
      { "null", STYLE_NULL },
      { "true", STYLE_TRUE },
      { "false", STYLE_FALSE },
  };

  for (usz k = 0; k < ARRAY_LEN(keywords); k++)
    if (strlen(keywords[k]) == n && memcmp(s, keywords[k], n) == 0) return STYLE_KEYWORD;

  for (usz k = 0; k < ARRAY_LEN(literals); k++)
    if (strlen(literals[k].name) == n && memcmp(s, literals[k].name, n) == 0) return literals[k].style;

  return STYLE_BUILTIN;
}

static b8 in_string(const lexer* lx) {
  if (lx->depth == 0) return 0;

  return lx->s[lx->opens[lx->depth - 1]] == '"';
}

static b8 name_start(char c) {
  u8 b = (u8)c;

  return isalpha(b) || b == '_';
}

static b8 name_byte(char c) {
  u8 b = (u8)c;

  return isalnum(b) || b == '_';
}
