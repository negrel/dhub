#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#include "debug.h"
#include "log.h"

#if defined(__SANITIZE_ADDRESS__) || HAS_FEATURE(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define ASAN_ENABLED 1
#endif

static void print_stack_trace(void) {
#ifdef ASAN_ENABLED
  fputs("\nStack trace:\n", stderr);
  __sanitizer_print_stack_trace();
#endif
}

noreturn void fatal_error(const char *file, int line, const char *msg,
                          int err) {
  log_msg(LOG_CLASS_ERROR, "debug", file, line, "%s: %s", msg, strerror(err));
  print_stack_trace();
  fflush(stderr);
  abort();
}

noreturn void uv_fatal_error(const char *file, int line, char *msg, int err) {
  const char *err_name = uv_err_name(err);
  const char *strerr = uv_strerror(err);

  log_msg(LOG_CLASS_ERROR, "debug", file, line, "%s: libuv error(%s): %s", msg,
          err_name, strerr);
  print_stack_trace();
  fflush(stderr);
  abort();
}

noreturn void bug(const char *file, int line, const char *func, const char *fmt,
                  ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  const char *msg = likely(n >= 0) ? buf : "??";
  log_msg(LOG_CLASS_ERROR, "debug", file, line, "BUG in %s(): %s", func, msg);
  print_stack_trace();
  fflush(stderr);
  abort();
}
