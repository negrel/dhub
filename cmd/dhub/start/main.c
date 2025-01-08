#include <stdlib.h>
#include <uv.h>

#include "start/state.h"

int start(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  dhub_state_t dhub = {0};
  dhub_init(&dhub);

  dhub_start(&dhub);

  dhub_deinit(&dhub);

  return EXIT_SUCCESS;
}
