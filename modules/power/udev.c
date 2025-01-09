#include "basu/sd-bus.h"
#include "string.h"
#include <libudev.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define LOG_MODULE "mod-power"
#include "dhub.h"
#include "tllist.h"

#define LOG_ERR_GOTO(err, label, fmt, ...)                                     \
  if (err) {                                                                   \
    LOG_ERR(fmt, #__VA_ARGS__);                                                \
    goto label;                                                                \
  }

#define SD_LOG_ERR(err, fmt, ...)                                              \
  if (err < 0) {                                                               \
    LOG_ERR(fmt ": %s", ##__VA_ARGS__, strerror(-err));                        \
  }

#define SD_LOG_ERR_GOTO(err, label, fmt, ...)                                  \
  if (err < 0) {                                                               \
    LOG_ERR(fmt ": %s", ##__VA_ARGS__, strerror(-err));                        \
    goto label;                                                                \
  }

#define UDEV_LOG_ERR_GOTO(err, label, fmt, ...)                                \
  LOG_ERR_GOTO(err < 0, label, fmt, #__VA_ARGS__)

#define UV_LOG_ERR_GOTO(err, label, fmt, ...)                                  \
  if (err < 0) {                                                               \
    LOG_ERR(fmt ": %s", ##__VA_ARGS__, uv_strerror(err));                      \
    goto label;                                                                \
  }

#define DBUS_POWER_PATH "/dev/negrel/dhub/power"
#define DBUS_POWER_IFACE "dev.negrel.dhub.Power"
#define DBUS_POWER_SUPPLY_IFACE "dev.negrel.dhub.PowerSupply"
#define DBUS_POWER_SUPPLY_BATTERY_IFACE "dev.negrel.dhub.PowerSupply.Battery"

typedef struct {
  sd_bus *bus;
  struct udev_device *dev;
  tll(sd_bus_slot **) slots;
  char *by_path_obj_path;
  char *by_name_obj_path;
} power_supply_t;

typedef struct {
  dhub_state_t *dhub;
  void *tag;
  sd_bus *bus;
  struct udev *udev;
  struct udev_monitor *mon;
  uv_poll_t mon_poll;
  tll(power_supply_t *) power_devices;
  sd_bus_slot *slot;
} power_data_t;

static void encode_object_path(char *path) {
  while (*path != '\0') {
    char c = *path;
    if (c != '_' && !(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') &&
        !(c >= '0' && c <= '9') && c != '/') {
      *path = '_';
    }
    path++;
  }
}

static void power_supply_update_device(power_supply_t *power_supply,
                                       struct udev_device *dev) {
#define POWER_SUPPLY_UPDATE(prop, newv, iface, get, ...)                       \
  do {                                                                         \
    const char *oldv = get(old, ##__VA_ARGS__);                                \
    newv = get(dev, ##__VA_ARGS__);                                            \
    if (strcmp(oldv, newv) != 0) {                                             \
      LOG_DBG("properties '%s' changed from '%s' to '%s'", prop, oldv, newv);  \
      int r = sd_bus_emit_properties_changed(power_supply->bus,                \
                                             power_supply->by_path_obj_path,   \
                                             iface, prop, NULL);               \
      SD_LOG_ERR(r,                                                            \
                 "failed to emit properties changed signal for property '%s' " \
                 "on object '%s'",                                             \
                 prop, power_supply->by_path_obj_path);                        \
    }                                                                          \
  } while (0)

  // Update device.
  struct udev_device *old = power_supply->dev;
  power_supply->dev = dev;

  // Check for changes and emit signals.
  const char *newv = NULL;
  POWER_SUPPLY_UPDATE("Name", newv, DBUS_POWER_SUPPLY_IFACE,
                      udev_device_get_sysname);
  POWER_SUPPLY_UPDATE("Path", newv, DBUS_POWER_SUPPLY_IFACE,
                      udev_device_get_syspath);
  POWER_SUPPLY_UPDATE("Type", newv, DBUS_POWER_SUPPLY_IFACE,
                      udev_device_get_property_value, "POWER_SUPPLY_TYPE");

  if (strcmp(newv, "Battery") == 0) {
    POWER_SUPPLY_UPDATE("Status", newv, DBUS_POWER_SUPPLY_BATTERY_IFACE,
                        udev_device_get_property_value, "POWER_SUPPLY_STATUS");
    POWER_SUPPLY_UPDATE("Capacity", newv, DBUS_POWER_SUPPLY_BATTERY_IFACE,
                        udev_device_get_property_value,
                        "POWER_SUPPLY_CAPACITY");
    POWER_SUPPLY_UPDATE("CapacityLevel", newv, DBUS_POWER_SUPPLY_BATTERY_IFACE,
                        udev_device_get_property_value,
                        "POWER_SUPPLY_CAPACITY_LEVEL");
  }

#undef POWER_SUPPLY_UPDATE

  udev_device_unref(old);
}

#define DBUS_POWER_SUPPLY_GETTER(prop, expr)                                   \
  static int dbus_power_supply_get_##prop(                                     \
      struct sd_bus *bus, const char *path, const char *interface,             \
      const char *property, sd_bus_message *reply, void *userdata,             \
      sd_bus_error *error) {                                                   \
    (void)bus;                                                                 \
    (void)path;                                                                \
    (void)interface;                                                           \
    (void)property;                                                            \
    (void)error;                                                               \
                                                                               \
    LOG_DBG("getter %s." #prop, DBUS_POWER_SUPPLY_IFACE);                      \
                                                                               \
    power_supply_t *power_supply = userdata;                                   \
    return sd_bus_message_append(reply, DHUB_STRING, (expr));                  \
  }

DBUS_POWER_SUPPLY_GETTER(Name, udev_device_get_sysname(power_supply->dev))
DBUS_POWER_SUPPLY_GETTER(Path, udev_device_get_syspath(power_supply->dev))
DBUS_POWER_SUPPLY_GETTER(Type,
                         udev_device_get_property_value(power_supply->dev,
                                                        "POWER_SUPPLY_TYPE"))

/**
 * D-Bus base virtual table of power supplies objects.
 */
static const sd_bus_vtable power_supply_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Name", DHUB_STRING, dbus_power_supply_get_Name, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Path", DHUB_STRING, dbus_power_supply_get_Path, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Type", DHUB_STRING, dbus_power_supply_get_Type, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END,
};

DBUS_POWER_SUPPLY_GETTER(Status,
                         udev_device_get_property_value(power_supply->dev,
                                                        "POWER_SUPPLY_STATUS"))
DBUS_POWER_SUPPLY_GETTER(
    Capacity,
    udev_device_get_property_value(power_supply->dev, "POWER_SUPPLY_CAPACITY"))
DBUS_POWER_SUPPLY_GETTER(CapacityLevel,
                         udev_device_get_property_value(
                             power_supply->dev, "POWER_SUPPLY_CAPACITY_LEVEL"))

/**
 * D-Bus virtual table for battery power supplies objects.
 */
static const sd_bus_vtable power_supply_battery_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Status", DHUB_STRING, dbus_power_supply_get_Status, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Capacity", DHUB_STRING, dbus_power_supply_get_Capacity, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CapacityLevel", DHUB_STRING,
                    dbus_power_supply_get_CapacityLevel, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END,
};

/**
 * Getter for Devices D-Bus property.
 */
static int dbus_get_power_devices(struct sd_bus *bus, const char *path,
                                  const char *interface, const char *property,
                                  sd_bus_message *reply, void *userdata,
                                  sd_bus_error *error) {
  (void)bus;
  (void)path;
  (void)interface;
  (void)property;
  (void)error;

  LOG_DBG("getter %s.Devices", DBUS_POWER_IFACE);

  power_data_t *data = userdata;
  int r = 0;

  // Open array container in reply.
  r = sd_bus_message_open_container(reply, DHUB_ARRAY_CTR, DHUB_STRING);
  SD_LOG_ERR_GOTO(r, err, "failed to open reply container");

  tll_foreach(data->power_devices, it) {
    const char *syspath = udev_device_get_syspath(it->item->dev);
    r = sd_bus_message_append(reply, DHUB_STRING, syspath);
    SD_LOG_ERR_GOTO(r, err, "failed to append to reply");
  }

  // Close reply array.
  r = sd_bus_message_close_container(reply);
  SD_LOG_ERR_GOTO(r, err, "failed to close echo reply");

  return r;

err:
  sd_bus_message_unref(reply);
  return r;
}

/**
 * D-Bus virtual table of this module's object.
 */
static const sd_bus_vtable power_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Devices", DHUB_ARRAY(DHUB_STRING), dbus_get_power_devices,
                    0, SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION),
    SD_BUS_SIGNAL("DeviceAdded", DHUB_STRING, 0),
    SD_BUS_SIGNAL("DeviceRemoved", DHUB_STRING, 0),
    SD_BUS_SIGNAL("DeviceUpdated", DHUB_STRING, 0),
    SD_BUS_VTABLE_END,
};

#define DBUS_ADD_POWER_SUPPLY(bus, power_supply, vtable, iface, obj_path)      \
  do {                                                                         \
    sd_bus_slot **slot = calloc(1, sizeof(slot));                              \
    int r = sd_bus_add_object_vtable(bus, slot, obj_path, iface, vtable,       \
                                     power_supply);                            \
    SD_LOG_ERR(r, "failed to add " iface " object");                           \
    if (r < 0)                                                                 \
      free(slot);                                                              \
    else                                                                       \
      tll_push_back(power_supply->slots, slot);                                \
  } while (0)

#define DBUS_ADD_POWER_SUPPLY_FMT(bus, power_supply, vtable, iface, str_dst,   \
                                  fmt, ...)                                    \
  do {                                                                         \
    int r = asprintf(&str_dst, fmt, ##__VA_ARGS__);                            \
    encode_object_path(str_dst);                                               \
    if (r == -1)                                                               \
      LOG_FATAL("failed to allocate power supply D-Bus object path");          \
    DBUS_ADD_POWER_SUPPLY(bus, power_supply, vtable, iface, str_dst);          \
  } while (0)

/**
 * Register power supply device if it is not already registered and returns
 * true if device wasn't registered.
 */
static power_supply_t *register_power_device(power_data_t *data,
                                             struct udev_device *dev) {

  const char *syspath = udev_device_get_syspath(dev);

  tll_foreach(data->power_devices, it) {
    if (strcmp(udev_device_get_syspath(it->item->dev), syspath) == 0) {
      // Update device.
      power_supply_update_device(it->item, dev);

      power_supply_t *power_supply = it->item;

      // Emit DeviceUpdated signal.
      int r = sd_bus_emit_signal(data->bus, DBUS_POWER_PATH, DBUS_POWER_IFACE,
                                 "DeviceUpdated", DHUB_OBJ_PATH,
                                 power_supply->by_path_obj_path);
      SD_LOG_ERR(r, "failed to emit DeviceUpdated signal");
      r = sd_bus_emit_signal(data->bus, DBUS_POWER_PATH, DBUS_POWER_IFACE,
                             "DeviceUpdated", DHUB_OBJ_PATH,
                             power_supply->by_name_obj_path);
      SD_LOG_ERR(r, "failed to emit DeviceUpdated signal");

      return it->item;
    }
  }

  LOG_DBG("new device registered %s", syspath);

  power_supply_t *power_supply = calloc(1, sizeof(*power_supply));
  power_supply->dev = dev;
  power_supply->bus = data->bus;

  // /by_path/ object.
  DBUS_ADD_POWER_SUPPLY_FMT(
      data->bus, power_supply, power_supply_vtable, DBUS_POWER_SUPPLY_IFACE,
      power_supply->by_path_obj_path, "%s/supply/by_path%s", DBUS_POWER_PATH,
      udev_device_get_syspath(power_supply->dev));

  // /by_name/ object.
  DBUS_ADD_POWER_SUPPLY_FMT(
      data->bus, power_supply, power_supply_vtable, DBUS_POWER_SUPPLY_IFACE,
      power_supply->by_name_obj_path, "%s/supply/by_name/%s", DBUS_POWER_PATH,
      udev_device_get_sysname(power_supply->dev));

  // Power supply battery interface.
  if (strcmp(udev_device_get_property_value(power_supply->dev,
                                            "POWER_SUPPLY_TYPE"),
             "Battery") == 0) {

    // /by_path/ object.
    DBUS_ADD_POWER_SUPPLY(data->bus, power_supply, power_supply_battery_vtable,
                          DBUS_POWER_SUPPLY_BATTERY_IFACE,
                          power_supply->by_path_obj_path);
    // /by_name/ object.
    DBUS_ADD_POWER_SUPPLY(data->bus, power_supply, power_supply_battery_vtable,
                          DBUS_POWER_SUPPLY_BATTERY_IFACE,
                          power_supply->by_name_obj_path);
  }

  // Emit property change signal.
  int r = sd_bus_emit_properties_changed(data->bus, DBUS_POWER_PATH,
                                         DBUS_POWER_IFACE, "Devices", NULL);
  SD_LOG_ERR(r, "failed to emit properties changed signal");

  // Emit DeviceAdded signal.
  r = sd_bus_emit_signal(data->bus, DBUS_POWER_PATH, DBUS_POWER_IFACE,
                         "DeviceAdded", DHUB_STRING, syspath);
  SD_LOG_ERR(r, "failed to emit DeviceAdded signal");

  // Add power supply to list.
  tll_push_back(data->power_devices, power_supply);

  return power_supply;
}

/**
 * Unregister power supply device if it is registered and returns
 * true if device was registered.
 */
bool unregister_power_device(power_data_t *data, struct udev_device *dev) {
  tll_foreach(data->power_devices, it) {
    if (it->item->dev == dev) {
      power_supply_t *power_supply = it->item;

      // Remove device from list.
      tll_remove(data->power_devices, it);

      const char *syspath = udev_device_get_syspath(dev);
      LOG_DBG("unregister %s", syspath);

      // Emit property change signal.
      int r = sd_bus_emit_properties_changed(data->bus, DBUS_POWER_PATH,
                                             DBUS_POWER_IFACE, "Devices", NULL);
      SD_LOG_ERR(r, "failed to emit properties changed signal");

      // Emit DeviceRemoved signal.
      r = sd_bus_emit_signal(data->bus, DBUS_POWER_PATH, DBUS_POWER_IFACE,
                             "DeviceRemoved", DHUB_STRING, syspath);
      SD_LOG_ERR(r, "failed to emit DeviceRemoved signal");

      // Free D-Bus slots.
      tll_foreach(power_supply->slots, it) {
        sd_bus_slot_unref(*it->item);
        free(it->item);
        tll_remove(power_supply->slots, it);
      }

      // Free object paths.
      free(power_supply->by_path_obj_path);
      free(power_supply->by_name_obj_path);

      // Free power supply.
      free(power_supply);

      // Free udev device.
      udev_device_unref(dev);

      return true;
    }
  }

  return false;
}

/**
 * Iterates over all power devices and register them if not already
 * registered.
 */
void register_all_power_devices(power_data_t *data) {
  struct udev_enumerate *enumerate = udev_enumerate_new(data->udev);
  udev_enumerate_add_match_subsystem(enumerate, "power_supply");
  udev_enumerate_scan_devices(enumerate);

  struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
  struct udev_list_entry *entry;

  udev_list_entry_foreach(entry, devices) {
    const char *path = udev_list_entry_get_name(entry);
    struct udev_device *dev = udev_device_new_from_syspath(data->udev, path);

    if (dev != NULL)
      register_power_device(data, dev);
  }

  udev_enumerate_unref(enumerate);
}

static void on_udev_event(uv_poll_t *handle, int status, int events) {
  LOG_DBG("udev event status=%d events=%d", status, events);

  power_data_t *data = handle->data;
  if (handle->data == NULL)
    return;

  struct udev_device *dev = udev_monitor_receive_device(data->mon);
  if (dev) {
    const char *action = udev_device_get_action(dev);
    const char *path = udev_device_get_syspath(dev);

    LOG_DBG("udev event '%s' on device '%s'", action, path);

    if (strcmp(action, "remove") == 0) {
      unregister_power_device(data, dev);
    } else {
      register_power_device(data, dev);
    }
  }
}

static void on_poll_close(uv_handle_t *handle) {
  power_data_t *data = handle->data;

  // Free udev monitor.
  if (data->mon != NULL)
    udev_monitor_unref(data->mon);

  // Free udev context.
  if (data->udev != NULL)
    udev_unref(data->udev);

  // Free D-Bus slot.
  if (data->slot != NULL)
    sd_bus_slot_unref(data->slot);

  dhub_state_t *dhub = data->dhub;
  void *tag = data->tag;
  free(data);
  dhub_close(dhub, tag);
}

void unload(dhub_state_t *dhub, void *mod_data, void *tag) {
  (void)dhub;
  power_data_t *data = (power_data_t *)mod_data;

  if (data != NULL) {
    // Free power devices.
    tll_foreach(data->power_devices, it) {
      unregister_power_device(data, it->item->dev);
    }

    // Stop and close poll handle for udev monitor.
    data->tag = tag;
    uv_poll_stop(&data->mon_poll);
    uv_close((uv_handle_t *)&data->mon_poll, on_poll_close);
  }
}

int load(dhub_state_t *dhub, void **mod_data) {
  power_data_t *data = calloc(1, sizeof(*data));
  LOG_ERR_GOTO(data == NULL, err, "failed to allocate power module data");

  *mod_data = data;

  // Store D-Hub reference.
  data->dhub = dhub;

  // Create udev context.
  data->udev = udev_new();
  LOG_ERR_GOTO(data->udev == NULL, err, "failed to create udev context");

  // Create udev monitor.
  data->mon = udev_monitor_new_from_netlink(data->udev, "udev");
  LOG_ERR_GOTO(data->mon == NULL, err, "failed to create udev monitor");

  // Filter for power supply devices.
  int r = udev_monitor_filter_add_match_subsystem_devtype(data->mon,
                                                          "power_supply", NULL);
  UDEV_LOG_ERR_GOTO(
      r, err, "failed to add power_supply subsystem filter to udev monitor");

  // Start monitoring.
  r = udev_monitor_enable_receiving(data->mon);
  UDEV_LOG_ERR_GOTO(r, err, "failed to enable receiving on udev monitor");

  // Get monitor fd for polling
  int fd = udev_monitor_get_fd(data->mon);
  UDEV_LOG_ERR_GOTO(fd, err, "failed to retrieve udev monitor fd");

  // Create libuv poll for FD.
  uv_loop_t *loop = dhub_loop(dhub);

  data->mon_poll.data = data;

  // Poll FD to detect new udev events.
  r = uv_poll_init(loop, &data->mon_poll, fd);
  UV_LOG_ERR_GOTO(r, err, "failed to create libuv poll for udev monitor");

  // Add object to D-Bus.
  data->bus = dhub_bus(dhub);
  r = sd_bus_add_object_vtable(data->bus, &data->slot, DBUS_POWER_PATH,
                               DBUS_POWER_IFACE, power_vtable, data);
  SD_LOG_ERR_GOTO(r, err, "failed to add Power object to D-Bus");

  // Start polling for udev event.
  r = uv_poll_start(&data->mon_poll, UV_READABLE, on_udev_event);
  UV_LOG_ERR_GOTO(r, err,
                  "failed to start polling udev monitor libuv poll handle");

  register_all_power_devices(data);

  return 0;

err:
  // Free allocated resources on error.
  unload(dhub, NULL, NULL);
  return 1;
}
