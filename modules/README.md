# Modules

This directory contains modules that can be dynamically loaded by D-Hub daemon.

## Writing your own modules

Every D-Hub module must implement two key functions: `load()` and `unload()`.
The `load()` function initializes your module when D-Hub starts, while
`unload()` cleans up resources when the module is stopped.

When writing modules, you can only include `dhub.h` and headers of external
libraries that are linked with the modules. Internal D-Hub headers are not
accessible to modules, ensuring proper encapsulation and separation of concerns.

Documented [`echo`](./echo.c) example module is a good starting point if you
don't want to start from scratch.

To create a new module, start by defining a structure to hold your module's
data. For example:

```c
typedef struct {
    sd_bus_slot *slot;  // Store bus-related data
} module_data_t;
```

In your `load()` function:
1. Allocate memory for your module data
2. Get a reference to the D-Bus using `dhub_bus()`
3. Add your D-Bus object and interface using `sd_bus_add_object_vtable()`
4. Implement your interface methods to handle D-Bus calls

Make sure to properly handle errors and clean up resources if initialization
fails. The example echo module demonstrates best practices like using error
handling macros and following a clear initialization sequence.

Your module's interface methods will receive D-Bus messages that you can respond
to. Use the sd-bus API to parse incoming messages and construct replies, similar
to how the `method_echo()` function processes string arrays in the example.

### Manual testing

While developing, you may want to test your code manually from a terminal. You
can do so using `dbus-send` and `dbus-monitor` CLIs.

Calling a method:

```
dbus-send --session --type=method_call --print-reply \
    --dest=dev.negrel.dhub /dev/negrel/dhub/echo \
    dev.negrel.dhub.Echoer.Broadcast string:"foo"
```

Reading a property:

```
dbus-send --session --type=method_call --print-reply \
    --dest=dev.negrel.dhub /dev/negrel/dhub/power \
    org.freedesktop.DBus.Properties.Get \
    string:"dev.negrel.dhub.Power" \
    string:"Devices"
```

Subscribe to signals:

```
dbus-monitor --session \
    "type='signal',interface='dev.negrel.dhub.Echoer',path='/dev/negrel/dhub/echo'"
```
