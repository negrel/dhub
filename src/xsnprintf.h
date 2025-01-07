#ifndef HUB_XSNPRINTF_H_INCLUDE
#define HUB_XSNPRINTF_H_INCLUDE

#include <stdarg.h>
#include <stddef.h>
#include "macros.h"

size_t xsnprintf(char *restrict buf, size_t n, const char *restrict format,
		 ...) PRINTF(3) NONNULL_ARGS;

#endif
