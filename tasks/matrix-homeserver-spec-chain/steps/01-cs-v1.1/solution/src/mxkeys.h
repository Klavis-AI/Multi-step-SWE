#ifndef MXKEYS_H
#define MXKEYS_H

#include <stdbool.h>
#include <stdint.h>

#include "mxhttp.h"
#include "mxjson.h"

void api_register_keys(http_server *s);

bool keys_handle_edu(const char *origin, const json *edu);

void keys_device_list_changed(const char *user_id, const char *device_id,
                              bool deleted);

json *keys_user_device_keys(const char *user_id);

json *keys_sync_device_lists(const char *user_id, int64_t since, int64_t now);
json *keys_sync_otk_counts(const char *user_id, const char *device_id);
json *keys_sync_unused_fallback_key_types(const char *user_id,
                                          const char *device_id);

#endif
