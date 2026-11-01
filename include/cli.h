/* livejq - the command line. */
#ifndef LIVEJQ_CLI_H
#define LIVEJQ_CLI_H

#include "app.h"
#include "defines.h"

typedef enum {
  CLI_RUN = 0,
  CLI_HELP,
  CLI_VERSION,
  CLI_USAGE, /* a usage error, explained in err */
} cli_action;

typedef struct {
  app_opts app;
  const char* query_folder; /* NULL without a sidebar */
} cli_opts;

/* The text --help prints. */
extern const char cli_usage[];
/* The text --version prints: versions, license and acknowledgments. */
extern const char cli_version[];

/* Parses the arguments after argv[0] into opts, pointing into argv. On
 * CLI_RUN opts->app.args is an object for app_new to own; on any other
 * action nothing is left to free. */
cli_action cli_parse(i32 argc, char** argv, cli_opts* opts, char* err, usz errlen);

#endif /* LIVEJQ_CLI_H */
