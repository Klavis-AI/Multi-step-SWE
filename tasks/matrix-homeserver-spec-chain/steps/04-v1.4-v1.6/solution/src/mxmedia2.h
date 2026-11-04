#ifndef MXMEDIA2_H
#define MXMEDIA2_H

#include <stdbool.h>
#include <stdint.h>

#include "mxhttp.h"

void api_register_media2(http_server *s);

int64_t media_freeze_instant(void);

void media2_membership_hook(json *ev, json *un, const char *user_id);

void media2_erasure_record(const char *user_id);
bool media2_erased_from(const json *ev, const char *viewer);

#endif
