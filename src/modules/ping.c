#include <stdlib.h>

#include "dhub.h"
#include "basu/sd-bus.h"

#define LOG_MODULE "ping"
#include "log.h"

#define SD_LOG_ERR_GOTO(err, msg, label) if (r < 0) { LOG_ERR("%s: %s", msg, strerror(-err)); goto label; }

typedef struct {
	sd_bus_slot *slot;
} ping_data_t;

static int method_ping(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
	(void) userdata;
	(void) ret_error;
	return sd_bus_reply_method_return(m, DHUB_STRING, "PONG");
}

static int method_repeat(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
	(void) userdata;
	(void) ret_error;
	int r = 0;

	sd_bus_message *reply = NULL;

	// Create reply message.
	r = sd_bus_message_new_method_return(m, &reply);
	SD_LOG_ERR_GOTO(r, "failed to create repeat reply", ret);

	// Open array container in reply.
	r = sd_bus_message_open_container(reply, DHUB_ARRAY_CTR, DHUB_STRING);
	SD_LOG_ERR_GOTO(r, "failed to open repeat reply", err);

	// Start reading string array.
	r = sd_bus_message_enter_container(m, DHUB_ARRAY_CTR, DHUB_STRING);
	SD_LOG_ERR_GOTO(r, "failed to enter repeat message", err);

	// Copy message to reply.
	const char *input = NULL;
	while ((r = sd_bus_message_read(m, DHUB_STRING, &input)) > 0) {
		r = sd_bus_message_append(reply, DHUB_STRING, input);
		SD_LOG_ERR_GOTO(r, "failed to append to repeat reply", err);
	}

	// Stop reading string array.
	r = sd_bus_message_exit_container(m);
	SD_LOG_ERR_GOTO(r, "failed to exit repeat message", err);

	// Close reply array.
	r = sd_bus_message_close_container(reply);
	SD_LOG_ERR_GOTO(r, "failed to close repeat reply", err);

	// Send reply.
	sd_bus_send(sd_bus_message_get_bus(reply), reply, NULL);
	SD_LOG_ERR_GOTO(r, "failed to send repeat reply", err);

	sd_bus_message_unref(reply);
	return r;

err:
	sd_bus_message_unref(reply);
ret:
	return r;
}

// Define the virtual table for our ping interface.
static const sd_bus_vtable ping_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Ping", "", DHUB_STRING, method_ping, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("Repeat", DHUB_ARRAY(DHUB_STRING), DHUB_ARRAY(DHUB_STRING), method_repeat, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END
};

void unload(dhub_state_t *dhub, void *mod_data)
{
	(void) dhub;
	ping_data_t *data = (ping_data_t *)mod_data;

	if (data != NULL) {
		sd_bus_slot_unref(data->slot);
		free(data);
	}
}

int load(dhub_state_t *dhub, void **mod_data)
{
	ping_data_t *data = calloc(1, sizeof(*data));
	if (data == NULL) {
		LOG_ERR("failed to allocate ping module data");
		goto err;
	}
	*mod_data = data;

	sd_bus *bus = dhub_bus(dhub);

	int r = sd_bus_add_object_vtable(bus,
		&data->slot,
		"/dev/negrel/dhub/ping",
		"dev.negrel.dhub.Pinger",
		ping_vtable,
		NULL);
	if (r < 0) {
		LOG_ERR("failed to add ping vtable");
		goto err;
	}

	return 0;

err:
	unload(dhub, NULL);
	return 1;
}

