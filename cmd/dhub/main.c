#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#define LOG_MODULE "dhub-main"
#include "log.h"

int start(int argc, char *argv[]);

static void print_usage(char *prog_name) {
  static const char header[] =
      "dhub v0.1.0\n"
      "Alexandre Negrel <alexandre@negrel.dev>\n\n"
      "D-Hub is a modular system daemon that provides standardized access to\n"
      "system information and controls through a unified D-Bus interface.\n"
      "";

  static const char options[] =
      "Options:\n"
      "  -h, --help                               Print this message and exit\n"
      "";

  static const char commands[] =
      "Commands:\n"
      "  start                                    Start D-Hub service\n"
      "  stop                                     Send stop message to D-Hub "
      "server\n"
      "  ping                                     Ping D-Hub server";

  puts(header);
  printf("Usage: %s [OPTIONS...] command [CMD OPTIONS...] [ARGS...]\n",
         prog_name);
  puts(options);
  puts(commands);
}

int main(int argc, char *argv[]) {
  log_init(LOG_COLORIZE_AUTO, false, LOG_FACILITY_USER, LOG_CLASS_DEBUG);

  char *prog_name = argv[0];

  while (1) {
    static struct option long_options[] = {{"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    int c = getopt_long(argc, argv, "h", long_options, NULL);
    if (c == -1)
      break;

    switch (c) {
    case 'h':
      print_usage(prog_name);
      return EXIT_SUCCESS;

    default:
      BUG("unhandled option -%c", c);
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "no command provided\n");
    print_usage(prog_name);
    return EXIT_FAILURE;
  }

  char *cmd = argv[optind];
  LOG_DBG("executing command %s", cmd);

  int code = EXIT_SUCCESS;
  if (strcmp(cmd, "start") == 0) {
    code = start(argc - optind, argv + optind);
  } else {
    fprintf(stderr, "unknown command '%s'\n", cmd);
    print_usage(prog_name);
    return EXIT_FAILURE;
  }

  log_deinit();

  return code;
}
