#ifndef HUB_STATE_H_INCLUDE
#define HUB_STATE_H_INCLUDE

#include <basu/sd-bus.h>
#include <unistd.h>
#include <uv.h>

#include "dhub.h"
#include "tllist.h"

#ifndef MODDIR
#define MODDIR "/etc/dhub/modules.d"
#endif

struct dhub_state;
typedef int (*load_fn_t)(struct dhub_state *, void **);
typedef void (*unload_fn_t)(struct dhub_state *, void *);

typedef struct dhub_module {
  const char *name;
  uv_lib_t *lib;
  void *data;
  load_fn_t load;
  unload_fn_t unload;
} dhub_module_t;

typedef struct dhub_state {
  uv_loop_t loop;
  uv_signal_t sig;
  sd_bus *bus;
  uv_poll_t bus_poll;
  tll(dhub_module_t) modules;
} dhub_state_t;

void dhub_init(dhub_state_t *dhub);
void dhub_start(dhub_state_t *dhub);
void dhub_deinit(dhub_state_t *dhub);

#endif
