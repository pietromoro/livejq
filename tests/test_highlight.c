/* highlight tests - each query drawn as one letter per byte for its style. */
#include <string.h>

#include "defines.h"
#include "grid.h"
#include "highlight.h"

#include "test_helper.in"

static const char* styled(const char* q);
static void test_paths_and_pipes(void);
static void test_keywords_and_variables(void);
static void test_strings(void);
static void test_numbers_and_literals(void);
static void test_formats_and_operators(void);
static void test_object_keys(void);
static void test_definitions(void);
static void test_comment(void);
static void test_unmatched_brackets(void);
static void test_unterminated_strings(void);
static void test_invalid_characters(void);
static void test_empty(void);

/* k key, s string, n number, l literal, v variable, w keyword, b builtin,
 * o operator, p punctuation, f format, c comment, ! error, space normal. */
static const char* styled(const char* q) {
  static const struct {
    u8 style;
    char letter;
  } letters[] = {
      { STYLE_NORMAL, ' ' },  { STYLE_KEY, 'k' },     { STYLE_STRING, 's' },  { STYLE_NUMBER, 'n' },
      { STYLE_NULL, 'l' },    { STYLE_TRUE, 'l' },    { STYLE_FALSE, 'l' },   { STYLE_VARIABLE, 'v' },
      { STYLE_KEYWORD, 'w' }, { STYLE_BUILTIN, 'b' }, { STYLE_OPERATOR, 'o' }, { STYLE_PUNCT, 'p' },
      { STYLE_FORMAT, 'f' },  { STYLE_COMMENT, 'c' }, { STYLE_ERROR, '!' },
  };
  static char buf[128];
  u8 st[128];
  usz n = strlen(q);

  highlight(q, n, st);
  for (usz i = 0; i < n; i++) {
    buf[i] = '?';
    for (usz k = 0; k < ARRAY_LEN(letters); k++)
      if (letters[k].style == st[i]) buf[i] = letters[k].letter;
  }

  buf[n] = '\0';
  return buf;
}

static void test_paths_and_pipes(void) {
  ASSERT_STREQ(styled(".users[0].name | length"), "kkkkkkpnpkkkkk o bbbbbb");
  ASSERT_STREQ(styled("..|.[1:2]"), "kkokpnpnp");
  ASSERT_STREQ(styled(".a? // ."), "kko oo k");
  ASSERT_STREQ(styled("lib::f(1)"), "bbbbbbpnp");
}

static void test_keywords_and_variables(void) {
  ASSERT_STREQ(styled("reduce .[] as $x (0; . + $x)"), "wwwwww kpp ww vv pnp k o vvp");
  ASSERT_STREQ(styled("if . then 1 else $__loc__ end"), "ww k wwww n wwww vvvvvvvv www");
}

static void test_strings(void) {
  ASSERT_STREQ(styled("\"a\\(.b)c\""), "ssppkkpss");
  ASSERT_STREQ(styled("\"x\\\"y\""), "ssssss");
  ASSERT_STREQ(styled("\"\\(\"\\(1)\")\""), "sppsppnpsps");
  ASSERT_STREQ(styled(".\"a b\""), "ksssss");
}

static void test_numbers_and_literals(void) {
  ASSERT_STREQ(styled("[1.5e3, -2, null, true, false]"), "pnnnnno ono llllo llllo lllllp");
  ASSERT_STREQ(styled(". + .5"), "k o nn");
}

static void test_formats_and_operators(void) {
  ASSERT_STREQ(styled("@base64 \"x\" // .a |= 1"), "fffffff sss oo kk oo n");
  ASSERT_STREQ(styled(".a == 1 and .b != 2"), "kk oo n www kk oo n");
}

static void test_object_keys(void) {
  ASSERT_STREQ(styled("{a: 1, b, \"c\": .d, if: 2}"), "pkp no ko sssp kko kkp np");
  ASSERT_STREQ(styled("{a: length}"), "pkp bbbbbbp");
}

static void test_definitions(void) {
  ASSERT_STREQ(styled("def f(x): x; f(1)"), "www bpbpp bp bpnp");
}

static void test_comment(void) {
  ASSERT_STREQ(styled(". # all of \"it\""), "k ccccccccccccc");
  ASSERT_STREQ(styled("\"#\" # x"), "sss ccc");
}

static void test_unmatched_brackets(void) {
  ASSERT_STREQ(styled(".a)"), "kk!");
  ASSERT_STREQ(styled("[.a"), "!kk");
  ASSERT_STREQ(styled("(.a]"), "!kk!");
  ASSERT_STREQ(styled("[(.a)]"), "ppkkpp");
}

static void test_unterminated_strings(void) {
  ASSERT_STREQ(styled("\"ab"), "!ss");
  ASSERT_STREQ(styled("\"a\\(.b"), "!s!!kk");
  ASSERT_STREQ(styled("\"a\\"), "!ss");
}

static void test_invalid_characters(void) {
  ASSERT_STREQ(styled(".a & .b"), "kk ! kk");
  ASSERT_STREQ(styled(". \xc3\xa9"), "k !!");
}

static void test_empty(void) {
  u8 st[1] = { 0xff };

  highlight(NULL, 0, st);
  ASSERT_EQ(st[0], 0xff);
}

#define TESTS(f)                   \
   f(test_paths_and_pipes)         \
   f(test_keywords_and_variables)  \
   f(test_strings)                 \
   f(test_numbers_and_literals)    \
   f(test_formats_and_operators)   \
   f(test_object_keys)             \
   f(test_definitions)             \
   f(test_comment)                 \
   f(test_unmatched_brackets)      \
   f(test_unterminated_strings)    \
   f(test_invalid_characters)      \
   f(test_empty)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
