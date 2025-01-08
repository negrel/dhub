#ifndef HUB_H_INCLUDE
#define HUB_H_INCLUDE

#include <basu/sd-bus.h>
#include <uv.h>

/**
 * dhub_type_t define a data type. It is used to dynamically represent types
 * of object's properties, methods signatures and signals. It is a subset of
 * D-BUS signature encoding.
 *
 * See https://pythonhosted.org/txdbus/dbus_overview.html
 */
typedef const char *dhub_type_t;

#define DHUB_BYTE "y"
#define DHUB_BOOL "b"
#define DHUB_INT16 "n"
#define DHUB_UINT16 "q"
#define DHUB_INT32 "i"
#define DHUB_UINT32 "u"
#define DHUB_INT64 "x"
#define DHUB_UINT64 "t"
#define DHUB_DOUBLE "d"
#define DHUB_STRING "s"
#define DHUB_VARIANT "v"
#define DHUB_ARRAY_CTR 'a'
#define DHUB_ARRAY(t) "a" t
#define DHUB_STRUCT(fields) "(" fields ")"
#define DHUB_DICT(fields) "{" fields "}"

/**
 * D-Hub state holds daemon global states and shared resources used across all
 * modules. It serves as a the central point for configuration, runtime
 * information and global singletons such as the event loop.
 *
 * This structure is opaque and data must be accessed through dhub_state_*
 * functions.
 */
typedef struct dhub_state dhub_state_t;

/**
 * Loads a D-Hub module into the system if it is not already loaded.
 *
 * This function returns -1, log and write error message `err` if an error
 * occurred. It returns 0 if module was already loaded and 1 otherwise.
 */
int dhub_load(dhub_state_t *dhub, const char *modname, const char **err);

/**
 * Unloads a D-Hub module from the system if it is currently loaded.
 *
 * It returns 1 if module was loaded and 0 otherwise.
 */
int dhub_unload(dhub_state_t *dhub, const char *modname);

sd_bus *dhub_bus(dhub_state_t *dhub);
uv_loop_t *dhub_loop(dhub_state_t *dhub);

#endif
