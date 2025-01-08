/**
 * This files contains a simple echo module that serves as an example. It is
 * well documented and serves as a base for other modules.
 */

#include <stdlib.h>

#define LOG_MODULE "mod-echo"
#include "dhub.h"

/**
 * Helper macro for error sd-bus library error handling.
 * This macro logs the error, and jump to the given label if sd_bus_* function
 * returned an error.
 */
#define SD_LOG_ERR_GOTO(err, msg, label)                                       \
  if (err < 0) {                                                               \
    LOG_ERR("%s: %s", msg, strerror(-err));                                    \
    goto label;                                                                \
  }

/**
 * Echo module data.
 */
typedef struct {
  sd_bus_slot *slot;
} echo_data_t;

/**
 * This is the ping method implementation of our D-Bus object.
 */
static int method_ping(sd_bus_message *m, void *userdata,
                       sd_bus_error *ret_error) {
  (void)userdata;
  (void)ret_error;
  return sd_bus_reply_method_return(m, DHUB_STRING, "PONG");
}

/**
 * This is the echo method implementation of our D-Bus object.
 */
static int method_echo(sd_bus_message *m, void *userdata,
                       sd_bus_error *ret_error) {
  (void)userdata;
  (void)ret_error;
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

  // Free reply.
  sd_bus_message_unref(reply);
  return r;

err:
  sd_bus_message_unref(reply);
ret:
  return r;
}

// Define the virtual table for our echo interface.
static const sd_bus_vtable echo_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Ping", "", DHUB_STRING, method_ping,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Echo", DHUB_ARRAY(DHUB_STRING), DHUB_ARRAY(DHUB_STRING),
                  method_echo, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END};

/**
 * Unload global function is used by D-Hub to unload the module.
 * You should clean up any allocated resources in this function.
 */
void unload(dhub_state_t *dhub, void *mod_data) {
  (void)dhub;
  echo_data_t *data = (echo_data_t *)mod_data;

  if (data != NULL) {
    sd_bus_slot_unref(data->slot);
    free(data);
  }
}

/**
 * Load global function is used by D-Hub to initialize the module.
 */
int load(dhub_state_t *dhub, void **mod_data) {
  // Allocate our module data.
  echo_data_t *data = calloc(1, sizeof(*data));
  if (data == NULL) {
    LOG_ERR("failed to allocate echo module data");
    goto err;
  }

  // Store it.
  *mod_data = data;

  // Get reference to D-Bus.
  sd_bus *bus = dhub_bus(dhub);

  // Add our echo object to the bus.
  int r = sd_bus_add_object_vtable(bus, &data->slot, "/dev/negrel/dhub/echo",
                                   "dev.negrel.dhub.Echoer", echo_vtable, NULL);
  SD_LOG_ERR_GOTO(r, "failed to add Echoer object to D-Bus", err);

  return 0;

err:
  // Free allocated resources on error.
  unload(dhub, NULL);
  return 1;
}
