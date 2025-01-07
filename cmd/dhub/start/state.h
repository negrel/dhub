#ifndef HUB_STATE_H_INCLUDE
#define HUB_STATE_H_INCLUDE

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>
#include <basu/sd-bus.h>

#include "debug.h"
#include "dhub.h"
#include "log.h"
#include "tllist.h"
#include "xuv.h"

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

void dhub_start(dhub_state_t *dhub)
{
	uv_run(&dhub->loop, UV_RUN_DEFAULT);
}

void dhub_stop(dhub_state_t *dhub)
{
	uv_stop(&dhub->loop);
	uv_poll_stop(&dhub->bus_poll);
	uv_close((uv_handle_t *) & dhub->bus_poll, NULL);
}

static void on_sigint(uv_signal_t *handle, int signum)
{
	(void)signum;
	if (signum == SIGINT) {
		LOG_INFO("SIGINT received, stopping event loop");
		uv_close((uv_handle_t *) handle, NULL);
		dhub_stop(handle->loop->data);
	}
}

static void on_dbus_event(uv_poll_t *handle, int status, int events)
{
	(void)handle;
	dhub_state_t *dhub = handle->loop->data;
	LOG_DBG("dbus event status=%d events=%d", status, events);
	int r = 1;
	while (r > 0) {
		r = sd_bus_process(dhub->bus, NULL);
		LOG_DBG("dbus process r=%d", r);
	}
	if (r < 0) NEG_MUST(r, "failed to process dbus messages");
}

void dhub_init(dhub_state_t *dhub)
{
	// Setup loop.
	dhub->loop.data = dhub;
	UV_MUST(uv_loop_init(&dhub->loop), "failed to init libuv loop");

	// Setup signal handler.
	uv_signal_init(&dhub->loop, &dhub->sig);
	uv_signal_start_oneshot(&dhub->sig, on_sigint, SIGINT);

	// Setup D-Bus.
	NEG_MUST(sd_bus_open_user(&dhub->bus),
		 "failed to connect to session bus");
	int fd = sd_bus_get_fd(dhub->bus);
	if (fd < 0) {
		NEG_MUST(fd, "failed to retrieve D-BUS file descriptor");
	}
	UV_MUST(uv_poll_init(&dhub->loop, &dhub->bus_poll, fd),
		"failed to initialize poll handle for DBUS file descriptor");
	uv_poll_start(&dhub->bus_poll, UV_READABLE, on_dbus_event);

	char *name = "dev.negrel.dhub";
	NEG_MUST(sd_bus_request_name(dhub->bus, name, 0), "failed to acquire D-BUS name");

	// Load ping module.
	if (dhub_load(dhub, "ping", NULL) == -1) {
		LOG_ERR("failed to load ping module");
		abort();
	}
}

void dhub_deinit(dhub_state_t *dhub)
{
	tll_foreach(dhub->modules, it) {
		dhub_unload(dhub, it->item.name);
		tll_remove(dhub->modules, it);
	}

	NEG_TRY(sd_bus_flush(dhub->bus), "failed to flush D-BUS");
	sd_bus_close(dhub->bus);
	sd_bus_unref(dhub->bus);

	UV_TRY(uv_loop_close(&dhub->loop), "failed to close event loop");
}

sd_bus *dhub_bus(dhub_state_t *state)
{
	return state->bus;
}

const uv_loop_t *dhub_loop(dhub_state_t *state)
{
	return &state->loop;
}

static uv_lib_t *dlopen(const char *modname, const char **err)
{
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
	if (path != NULL) free(path);
	free(lib);
	return NULL;
}

int dhub_load(dhub_state_t *dhub, const char *modname, const char **err)
{
	LOG_INFO("trying to load module '%s'...", modname);
	char *err_msg = NULL;

	tll_foreach(dhub->modules, it) {
		if (strcmp(it->item.name, modname) == 0) {
			LOG_INFO("module '%s' already loaded", modname);
			return 0;
		}
	}

	uv_lib_t *lib = dlopen(modname, err);
	if (lib == NULL) return -1;

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
		LOG_ERR("failed to load '%s' module: load() returned non zero exit code (%d)", modname, code);
		err_msg = "module 'load' function failed";
		goto err;
	}

	tll_push_back(dhub->modules, ((dhub_module_t) {
		.name = strdup(modname),
		.lib = lib,
		.data = data,
		.load = load,
		.unload = unload,
	}));

	LOG_INFO("module '%s' loaded", modname);
	return 1;

err:
	if (err != NULL) *err = err_msg;
	return -1;
}

int dhub_unload(dhub_state_t *dhub, const char *modname) {
	LOG_INFO("trying to unload module '%s'...", modname);

	tll_foreach(dhub->modules, it) {
		if (strcmp(it->item.name, modname) == 0) {
			it->item.unload(dhub, it->item.data);
			it->item.data = NULL;
			uv_dlclose(it->item.lib);
			free(it->item.lib);
			LOG_INFO("module '%s' unloaded", modname);
			free((void *)it->item.name);
			return 1;
		}
	}

	LOG_INFO("module '%s' wasn't loaded", modname);
	return 0;
}

#endif
