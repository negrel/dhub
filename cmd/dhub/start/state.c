#include <uv.h>

#include "debug.h"
#include "start/state.h"
#include "tllist.h"
#define LOG_MODULE "dhub-start"
#include "log.h"

void dhub_start(dhub_state_t *dhub) {
  // Load power_udev module.
  const char *err_msg = NULL;
  char *modname = "echo";
  if (dhub_load(dhub, modname, &err_msg) == -1) {
    LOG_FATAL("failed to load '%s' module: %s", modname, err_msg);
  }

  LOG_INFO("starting event loop");
  while (uv_loop_alive(&dhub->loop))
    uv_run(&dhub->loop, UV_RUN_DEFAULT);
}

void dhub_stop(uv_idle_t *handle) {
  dhub_state_t *dhub = handle->data;

  // Unload modules.
  tll_foreach(dhub->modules, it) {
    if (it->item.state != DHUB_MODULE_UNLOADING)
      dhub_unload(dhub, it->item.name);
  }

  // All modules have been unloaded.
  if (tll_length(dhub->modules) == 0) {
    // Stop and close D-Bus poll handle.
    uv_poll_stop(&dhub->bus_poll);
    uv_close((uv_handle_t *)&dhub->bus_poll, NULL);

    // Close dhub_stop idle handle.
    uv_idle_stop(handle);
    uv_close((uv_handle_t *)handle, NULL);
  }
}

static void on_sigint(uv_signal_t *handle, int signum) {
  (void)signum;
  if (signum == SIGINT) {
    LOG_INFO("SIGINT received, stopping event loop");
    uv_close((uv_handle_t *)handle, NULL);

    // Start stop sequence.
    dhub_state_t *dhub = handle->loop->data;
    dhub->stop_idler.data = dhub;
    uv_idle_init(handle->loop, &dhub->stop_idler);
    uv_idle_start(&dhub->stop_idler, dhub_stop);
  }
}

static void on_dbus_event(uv_poll_t *handle, int status, int events) {
  (void)handle;
  dhub_state_t *dhub = handle->loop->data;
  LOG_DBG("dbus event status=%d events=%d", status, events);
  int r = 1;
  while (r > 0) {
    r = sd_bus_process(dhub->bus, NULL);
    LOG_DBG("dbus process r=%d", r);
  }
  if (r < 0)
    NEG_MUST(r, "failed to process dbus messages");
}

void dhub_init(dhub_state_t *dhub) {
  // Setup loop.
  dhub->loop.data = dhub;
  UV_MUST(uv_loop_init(&dhub->loop), "failed to init libuv loop");

  // Setup signal handler.
  uv_signal_init(&dhub->loop, &dhub->sig);
  uv_signal_start_oneshot(&dhub->sig, on_sigint, SIGINT);

  // Setup D-Bus.
  NEG_MUST(sd_bus_open_user(&dhub->bus), "failed to connect to session bus");
  int fd = sd_bus_get_fd(dhub->bus);
  if (fd < 0) {
    NEG_MUST(fd, "failed to retrieve D-BUS file descriptor");
  }
  UV_MUST(uv_poll_init(&dhub->loop, &dhub->bus_poll, fd),
          "failed to initialize poll handle for DBUS file descriptor");
  uv_poll_start(&dhub->bus_poll, UV_READABLE, on_dbus_event);

  char *name = "dev.negrel.dhub";
  NEG_MUST(sd_bus_request_name(dhub->bus, name, 0),
           "failed to acquire D-BUS name");
}

static void print_handle_info(uv_handle_t *handle, void *arg) {
  (void)arg;
  const char *type_name = uv_handle_type_name(handle->type);

  LOG_DBG("Handle %p:", (void *)handle);
  LOG_DBG("  Type: %s", type_name);
  LOG_DBG("  Active: %s", uv_is_active(handle) ? "yes" : "no");
  LOG_DBG("  Closing: %s", uv_is_closing(handle) ? "yes" : "no");
  LOG_DBG("  Has ref: %s", uv_has_ref(handle) ? "yes" : "no");
}

void dhub_deinit(dhub_state_t *dhub) {
  NEG_TRY(sd_bus_flush(dhub->bus), "failed to flush D-BUS");
  sd_bus_close(dhub->bus);
  sd_bus_unref(dhub->bus);

  int r = uv_loop_close(&dhub->loop);
  if (r == UV_EBUSY)
    uv_walk(&dhub->loop, print_handle_info, NULL);
  UV_TRY(r, "failed to close event loop")
}

sd_bus *dhub_bus(dhub_state_t *state) { return state->bus; }

uv_loop_t *dhub_loop(dhub_state_t *state) { return &state->loop; }

static uv_lib_t *load_module(const char *modname, const char **err) {
  uv_lib_t *lib = calloc(1, sizeof(*lib));

  char *path = NULL;
  if (asprintf(&path, "%s/%s.so", MODDIR, modname) == -1) {
    LOG_ERR("failed to allocate module path");
    abort();
  }

  LOG_DBG("dlopen '%s'...", path);
  if (uv_dlopen(path, lib) == -1) {
    free(path);
    path = NULL;

    const char *moddir = getenv("DHUB_MODULES_DIR");
    if (moddir == NULL) {
      goto err;
    }

    if (asprintf(&path, "%s/%s.so", moddir, modname) == -1) {
      LOG_ERR("failed to allocate module path");
      abort();
    }

    LOG_DBG("dlopen '%s'...", path);
    if (uv_dlopen(path, lib) == -1) {
      goto err;
    }
  }

  LOG_INFO("module '%s' found at '%s'...", modname, path);
  free(path);

  return lib;

err:
  if (err != NULL)
    *err = uv_dlerror(lib);
  if (path != NULL)
    free(path);
  free(lib);
  return NULL;
}

int dhub_load(dhub_state_t *dhub, const char *modname, const char **err) {
  LOG_INFO("trying to load module '%s'...", modname);
  char *err_msg = NULL;

  tll_foreach(dhub->modules, it) {
    if (strcmp(it->item.name, modname) == 0 &&
        it->item.state == DHUB_MODULE_LOADED) {
      LOG_INFO("module '%s' already loaded", modname);
      return 0;
    }
  }

  uv_lib_t *lib = load_module(modname, err);
  if (lib == NULL)
    return -1;

  load_fn_t load = NULL;
  unload_fn_t unload = NULL;

  if (uv_dlsym(lib, "load", (void **)&load) == -1) {
    LOG_ERR("load function not found");
    err_msg = "load function not found";
    goto err;
  }

  if (uv_dlsym(lib, "unload", (void **)&unload) == -1) {
    LOG_ERR("unload function not found");
    err_msg = "unload function not found";
    goto err;
  }

  void *data = NULL;
  int code = load(dhub, &data);
  if (code != 0) {
    LOG_ERR(
        "failed to load '%s' module: load() returned non zero exit code (%d)",
        modname, code);
    err_msg = "module 'load' function failed";
    goto err;
  }

  tll_push_back(dhub->modules, ((dhub_module_t){
                                   .name = strdup(modname),
                                   .lib = lib,
                                   .data = data,
                                   .load = load,
                                   .unload = unload,
                                   .state = DHUB_MODULE_LOADED,
                               }));

  LOG_INFO("module '%s' loaded", modname);
  return 1;

err:
  if (err != NULL)
    *err = err_msg;
  return -1;
}

int dhub_unload(dhub_state_t *dhub, const char *modname) {
  LOG_INFO("trying to unload module '%s'...", modname);

  tll_foreach(dhub->modules, it) {
    if (strcmp(it->item.name, modname) == 0) {
      if (it->item.state == DHUB_MODULE_UNLOADING) {
        LOG_INFO("already unloading module '%s'...", modname);
        return 0;
      }

      it->item.state = DHUB_MODULE_UNLOADING;
      it->item.unload(dhub, it->item.data, it->item.lib);
      // it->item might be deallocated now if unload() called dhub_close.

      return 1;
    }
  }

  LOG_INFO("module '%s' wasn't loaded", modname);
  return 0;
}

void dhub_close(dhub_state_t *dhub, void *tag) {
  tll_foreach(dhub->modules, it) {
    if (it->item.lib == tag && it->item.state == DHUB_MODULE_UNLOADING) {
      uv_dlclose(it->item.lib);
      free(it->item.lib);
      LOG_INFO("module '%s' unloaded", it->item.name);
      free((void *)it->item.name);

      tll_remove(dhub->modules, it);
    }
  }
}
