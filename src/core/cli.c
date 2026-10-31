/* cli - turns the command line into the options livejq runs with. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"

#define DEFAULT_WATCH_MS 1000
#define MIN_WATCH_SECS   0.001
#define MAX_WATCH_SECS   1000000.0

static b8 option(i32 argc, char** argv, i32* i, cli_opts* opts, char* err, usz errlen);
static const char* value(i32 argc, char** argv, i32* i, char* err, usz errlen);
static b8 watch(i32 argc, char** argv, i32* i, cli_opts* opts, char* err, usz errlen);
static b8 bind_arg(i32 argc, char** argv, i32* i, cli_opts* opts, char* err, usz errlen);
static cli_action drop_args(cli_opts* opts, cli_action action);

const char cli_usage[] =
  "usage: livejq [options] <file>\n"
  "\n"
  "Runs a jq query live over a JSON file.\n"
  "\n"
  "options:\n"
  "  --query TEXT         start with this query instead of .\n"
  "  --watch [SECS]       reload the file when it changes, checked every SECS\n"
  "                       seconds (default 1, fractions allowed)\n"
  "  --query-folder PATH  list the .query files in PATH as saved queries\n"
  "  --slurp              read all inputs into one array\n"
  "  --raw-input          read each line of the file as a string\n"
  "  --arg NAME VALUE     bind $NAME to the string VALUE\n"
  "  --argjson NAME JSON  bind $NAME to the JSON value\n"
  "  --help               show this help\n"
  "  --version            show the livejq and jq versions\n"
  "  --                   end of options, the next argument is the file\n";

cli_action cli_parse(i32 argc, char** argv, cli_opts* opts, char* err, usz errlen) {
  b8 options_done = 0;

  memset(opts, 0, sizeof(*opts));
  opts->app.args = jv_object();
  for (i32 i = 1; i < argc; i++) {
    const char* s = argv[i];

    if (options_done || s[0] != '-' || s[1] == '\0') {
      if (opts->app.path) {
        snprintf(err, errlen, "only one file, got %s and %s", opts->app.path, s);
        return drop_args(opts, CLI_USAGE);
      }

      opts->app.path = s;
      continue;
    }

    if (strcmp(s, "--") == 0) {
      options_done = 1;
      continue;
    }

    if (strcmp(s, "--help") == 0) return drop_args(opts, CLI_HELP);
    if (strcmp(s, "--version") == 0) return drop_args(opts, CLI_VERSION);
    if (!option(argc, argv, &i, opts, err, errlen)) return drop_args(opts, CLI_USAGE);
  }

  if (!opts->app.path) {
    snprintf(err, errlen, "no file given");
    return drop_args(opts, CLI_USAGE);
  }

  return CLI_RUN;
}

void cli_version(char* buf, usz len) {
  snprintf(buf, len, "livejq %s\njq %s\n", LIVEJQ_VERSION, JQ_VERSION);
}

static b8 option(i32 argc, char** argv, i32* i, cli_opts* opts, char* err, usz errlen) {
  const char* s = argv[*i];

  if (strcmp(s, "--slurp") == 0) {
    opts->app.slurp = 1;
    return 1;
  }

  if (strcmp(s, "--raw-input") == 0) {
    opts->app.raw = 1;
    return 1;
  }

  if (strcmp(s, "--query") == 0) return (opts->app.query = value(argc, argv, i, err, errlen)) != NULL;
  if (strcmp(s, "--query-folder") == 0)
    return (opts->query_folder = value(argc, argv, i, err, errlen)) != NULL;
  if (strcmp(s, "--watch") == 0) return watch(argc, argv, i, opts, err, errlen);
  if (strcmp(s, "--arg") == 0 || strcmp(s, "--argjson") == 0) return bind_arg(argc, argv, i, opts, err, errlen);

  snprintf(err, errlen, "unknown option %s", s);
  return 0;
}

/* Takes the argument after the flag at *i. */
static const char* value(i32 argc, char** argv, i32* i, char* err, usz errlen) {
  if (*i + 1 >= argc) {
    snprintf(err, errlen, "%s needs a value", argv[*i]);
    return NULL;
  }

  *i += 1;
  return argv[*i];
}

/* The interval is optional: only an argument that starts like a number is
 * taken as one, anything else is left for the next round. */
static b8 watch(i32 argc, char** argv, i32* i, cli_opts* opts, char* err, usz errlen) {
  const char* s;
  char* end;
  f64 secs;

  opts->watch_ms = DEFAULT_WATCH_MS;
  if (*i + 1 >= argc) return 1;

  s = argv[*i + 1];
  if (!((s[0] >= '0' && s[0] <= '9') || s[0] == '.')) return 1;

  *i += 1;
  secs = strtod(s, &end);
  if (*end != '\0' || !(secs >= MIN_WATCH_SECS && secs <= MAX_WATCH_SECS)) {
    snprintf(err, errlen, "--watch: %s is not a number of seconds between 0.001 and 1000000", s);
    return 0;
  }

  opts->watch_ms = (i64)(secs * 1000 + 0.5);
  return 1;
}

static b8 bind_arg(i32 argc, char** argv, i32* i, cli_opts* opts, char* err, usz errlen) {
  const char* flag = argv[*i];
  b8 json = strcmp(flag, "--argjson") == 0;
  jv v;

  if (*i + 2 >= argc) {
    snprintf(err, errlen, "%s needs a name and a value", flag);
    return 0;
  }

  v = json ? jv_parse(argv[*i + 2]) : jv_string(argv[*i + 2]);
  if (!jv_is_valid(v)) {
    jv msg = jv_invalid_get_msg(v);

    snprintf(
      err, errlen,
      "--argjson %s: %s",
      argv[*i + 1],
      jv_get_kind(msg) == JV_KIND_STRING ? jv_string_value(msg) : "invalid JSON"
    );
    jv_free(msg);
    return 0;
  }

  opts->app.args = jv_object_set(opts->app.args, jv_string(argv[*i + 1]), v);
  *i += 2;
  return 1;
}

static cli_action drop_args(cli_opts* opts, cli_action action) {
  jv_free(opts->app.args);
  opts->app.args = jv_invalid();
  return action;
}
