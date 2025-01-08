/**
 * This files contains a simple echo module that serves as an example. It is
 * well documented and serves as a base for other modules.
 */

#include "basu/sd-bus.h"
#include <stdlib.h>

#define LOG_MODULE "mod-echo"
#include "dhub.h"

#define DBUS_PATH "/dev/negrel/dhub/echo"
#define DBUS_IFACE "dev.negrel.dhub.Echoer"

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
  SD_LOG_ERR_GOTO(r, "failed to create echo reply", ret);

  // Open array container in reply.
  r = sd_bus_message_open_container(reply, DHUB_ARRAY_CTR, DHUB_STRING);
  SD_LOG_ERR_GOTO(r, "failed to open echo reply", err);

  // Start reading string array.
  r = sd_bus_message_enter_container(m, DHUB_ARRAY_CTR, DHUB_STRING);
  SD_LOG_ERR_GOTO(r, "failed to enter echo message", err);

  // Copy message to reply.
  const char *input = NULL;
  while ((r = sd_bus_message_read(m, DHUB_STRING, &input)) > 0) {
    r = sd_bus_message_append(reply, DHUB_STRING, input);
    SD_LOG_ERR_GOTO(r, "failed to append to echo reply", err);
  }

  // Stop reading string array.
  r = sd_bus_message_exit_container(m);
  SD_LOG_ERR_GOTO(r, "failed to exit echo message", err);

  // Close reply array.
  r = sd_bus_message_close_container(reply);
  SD_LOG_ERR_GOTO(r, "failed to close echo reply", err);

  // Send reply.
  sd_bus_send(sd_bus_message_get_bus(reply), reply, NULL);
  SD_LOG_ERR_GOTO(r, "failed to send echo reply", err);

  // Free reply.
  sd_bus_message_unref(reply);
  return r;

err:
  sd_bus_message_unref(reply);
ret:
  return r;
}

/**
 * This is the echo method implementation of our D-Bus object.
 */
static int method_broadcast(sd_bus_message *m, void *userdata,
                            sd_bus_error *ret_error) {
  (void)userdata;
  (void)ret_error;
  int r = 0;

  // Retrieve bus.
  sd_bus *bus = sd_bus_message_get_bus(m);

  LOG_DBG("dbus %p", (void *)bus);

  // Read string.
  const char *msg = NULL;
  r = sd_bus_message_read(m, DHUB_STRING, &msg);
  SD_LOG_ERR_GOTO(r, "failed to read message", ret);

  // Emit signal.
  r = sd_bus_emit_signal(bus, DBUS_PATH, DBUS_IFACE, "BroadcastSignal",
                         DHUB_STRING, msg);

  return sd_bus_reply_method_return(m, NULL);

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
    SD_BUS_METHOD("Broadcast", DHUB_STRING, "", method_broadcast,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("BroadcastSignal", DHUB_ARRAY(DHUB_STRING), 0),
    SD_BUS_VTABLE_END};

/**
 * Unload global function is used by D-Hub to unload the module.
 * You should clean up any allocated resources in this function.
 */
void unload(dhub_state_t *dhub, void *mod_data, void *tag) {
  (void)dhub;
  echo_data_t *data = (echo_data_t *)mod_data;

  if (data != NULL) {
    sd_bus_slot_unref(data->slot);
    free(data);
    dhub_close(dhub, tag);
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
  int r = sd_bus_add_object_vtable(bus, &data->slot, DBUS_PATH, DBUS_IFACE,
                                   echo_vtable,
                                   data); // data will be passed as user data to
                                          // D-Bus methods, getters and setters.
  SD_LOG_ERR_GOTO(r, "failed to add Echoer object to D-Bus", err);

  return 0;

err:
  // Free allocated resources on error.
  unload(dhub, NULL, NULL);
  return 1;
}
