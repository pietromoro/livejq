/* cli tests - the command line parsed into options. */
#include <stdarg.h>
#include <string.h>

#include "cli.h"

#include "test_helper.in"

static cli_action parse(cli_opts* o, ...);
static void test_file_only(void);
static void test_file_required(void);
static void test_one_file(void);
static void test_unknown_option(void);
static void test_help_and_version(void);
static void test_query(void);
static void test_input_modes(void);
static void test_watch(void);
static void test_watch_malformed(void);
static void test_args(void);
static void test_argjson_malformed(void);
static void test_end_of_options(void);
static void test_short_forms(void);

static char err[256];

/* Parses the NULL-terminated arguments after a program name. */
static cli_action parse(cli_opts* o, ...) {
  char* argv[16] = { "livejq" };
  i32 argc = 1;
  va_list ap;

  va_start(ap, o);
  while ((argv[argc] = va_arg(ap, char*)) != NULL)
    argc++;
  va_end(ap);
  err[0] = '\0';
  return cli_parse(argc, argv, o, err, sizeof(err));
}

static void test_file_only(void) {
  cli_opts o;

  ASSERT_EQ(parse(&o, "data.json", NULL), CLI_RUN);
  CHECK(strcmp(o.app.path, "data.json") == 0);
  CHECK(o.app.query == NULL);
  CHECK(!o.app.slurp && !o.app.raw);
  ASSERT_EQ(o.app.watch_ms, 0);
  CHECK(o.query_folder == NULL);
  ASSERT_EQ(jv_object_length(jv_copy(o.app.args)), 0);
  jv_free(o.app.args);
}

static void test_file_required(void) {
  cli_opts o;

  ASSERT_EQ(parse(&o, NULL), CLI_USAGE);
  CHECK(strstr(err, "no file") != NULL);
  ASSERT_EQ(parse(&o, "--slurp", NULL), CLI_USAGE);
}

static void test_one_file(void) {
  cli_opts o;

  ASSERT_EQ(parse(&o, "a.json", "b.json", NULL), CLI_USAGE);
  CHECK(strstr(err, "b.json") != NULL);
}

static void test_unknown_option(void) {
  cli_opts o;

  ASSERT_EQ(parse(&o, "--nope", "a.json", NULL), CLI_USAGE);
  CHECK(strstr(err, "--nope") != NULL);
  ASSERT_EQ(parse(&o, "-x", "a.json", NULL), CLI_USAGE);
  CHECK(strstr(err, "-x") != NULL);
}

static void test_help_and_version(void) {
  cli_opts o;

  ASSERT_EQ(parse(&o, "--help", NULL), CLI_HELP);
  ASSERT_EQ(parse(&o, "a.json", "--version", NULL), CLI_VERSION);
  CHECK(strstr(cli_usage, "--query TEXT") != NULL);
  CHECK(strncmp(cli_version, "livejq ", 7) == 0);
  CHECK(strstr(cli_version, "GNU GPL version 3") != NULL);
  CHECK(strstr(cli_version, "  jq 1.8.2,") != NULL);
  CHECK(strstr(cli_version, "  Oniguruma 6.9.10,") != NULL);
  CHECK(strstr(cli_version, "  PDCursesMod 4.5.4,") != NULL);
}

static void test_query(void) {
  cli_opts o;

  ASSERT_EQ(parse(&o, "--query", ".users[0]", "a.json", NULL), CLI_RUN);
  CHECK(strcmp(o.app.query, ".users[0]") == 0);
  CHECK(strcmp(o.app.path, "a.json") == 0);
  jv_free(o.app.args);

  ASSERT_EQ(parse(&o, "a.json", "--query", NULL), CLI_USAGE);
  CHECK(strstr(err, "--query needs a value") != NULL);
}

static void test_input_modes(void) {
  cli_opts o;

  ASSERT_EQ(parse(&o, "--slurp", "--raw-input", "--query-folder", "q", "a.json", NULL), CLI_RUN);
  CHECK(o.app.slurp && o.app.raw);
  CHECK(strcmp(o.query_folder, "q") == 0);
  jv_free(o.app.args);
}

static void test_watch(void) {
  cli_opts o;

  ASSERT_EQ(parse(&o, "--watch", "a.json", NULL), CLI_RUN);
  ASSERT_EQ(o.app.watch_ms, 1000);
  CHECK(strcmp(o.app.path, "a.json") == 0);
  jv_free(o.app.args);

  ASSERT_EQ(parse(&o, "--watch", "2.5", "a.json", NULL), CLI_RUN);
  ASSERT_EQ(o.app.watch_ms, 2500);
  jv_free(o.app.args);

  ASSERT_EQ(parse(&o, "--watch", ".1", "a.json", NULL), CLI_RUN);
  ASSERT_EQ(o.app.watch_ms, 100);
  jv_free(o.app.args);

  ASSERT_EQ(parse(&o, "a.json", "--watch", NULL), CLI_RUN);
  ASSERT_EQ(o.app.watch_ms, 1000);
  jv_free(o.app.args);
}

static void test_watch_malformed(void) {
  cli_opts o;

  ASSERT_EQ(parse(&o, "--watch", "0", "a.json", NULL), CLI_USAGE);
  CHECK(strstr(err, "--watch") != NULL);
  ASSERT_EQ(parse(&o, "--watch", "1x", "a.json", NULL), CLI_USAGE);
  ASSERT_EQ(parse(&o, "--watch", "1e9", "a.json", NULL), CLI_USAGE);
}

static void test_args(void) {
  cli_opts o;
  jv want = jv_parse("{\"name\":\"x\",\"n\":[3],\"s\":\"[3]\"}");

  ASSERT_EQ(
    parse(&o, "--arg", "name", "x", "--argjson", "n", "[3]", "--arg", "s", "[3]", "a.json", NULL),
    CLI_RUN
  );
  CHECK(jv_equal(o.app.args, want));
}

static void test_argjson_malformed(void) {
  cli_opts o;

  ASSERT_EQ(parse(&o, "--argjson", "n", "[3", "a.json", NULL), CLI_USAGE);
  CHECK(strstr(err, "--argjson n") != NULL);
  ASSERT_EQ(parse(&o, "--arg", "name", NULL), CLI_USAGE);
  CHECK(strstr(err, "needs a name and a value") != NULL);
}

static void test_end_of_options(void) {
  cli_opts o;

  ASSERT_EQ(parse(&o, "--watch", "--", "-odd.json", NULL), CLI_RUN);
  ASSERT_EQ(o.app.watch_ms, 1000);
  CHECK(strcmp(o.app.path, "-odd.json") == 0);
  jv_free(o.app.args);
}

static void test_short_forms(void) {
  cli_opts o;

  ASSERT_EQ(parse(&o, "-h", NULL), CLI_HELP);
  ASSERT_EQ(parse(&o, "-V", NULL), CLI_VERSION);
  ASSERT_EQ(
    parse(&o, "-s", "-R", "-q", ".a", "-d", "q", "-w", "0.5", "a.json", NULL),
    CLI_RUN
  );
  CHECK(o.app.slurp && o.app.raw);
  CHECK(strcmp(o.app.query, ".a") == 0);
  CHECK(strcmp(o.query_folder, "q") == 0);
  ASSERT_EQ(o.app.watch_ms, 500);
  CHECK(strcmp(o.app.path, "a.json") == 0);
  jv_free(o.app.args);

  ASSERT_EQ(parse(&o, "a.json", "-q", NULL), CLI_USAGE);
  CHECK(strstr(err, "-q needs a value") != NULL);
  ASSERT_EQ(parse(&o, "-sR", "a.json", NULL), CLI_USAGE);
}

#define TESTS(f)              \
  f(test_file_only)           \
  f(test_file_required)       \
  f(test_one_file)            \
  f(test_unknown_option)      \
  f(test_help_and_version)    \
  f(test_query)               \
  f(test_input_modes)         \
  f(test_watch)               \
  f(test_watch_malformed)     \
  f(test_args)                \
  f(test_argjson_malformed)   \
  f(test_end_of_options)      \
  f(test_short_forms)

#define TEST_HELPER_MAIN

/* curufmt skip translation-unit-order */
#include "test_helper.in"
