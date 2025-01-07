#ifndef XUV_H_INCLUDE
#define XUV_H_INCLUDE

#include <uv.h>

static void free_after_close(uv_handle_t *handle)
{
	free(handle);
}

void xuv_close_then_free(uv_handle_t *handle)
{
	uv_close(handle, free_after_close);
}

#endif
