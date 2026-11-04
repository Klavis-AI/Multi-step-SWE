#ifndef MXPUSH_H
#define MXPUSH_H

#include <stdbool.h>
#include <stdint.h>

#include "mxhttp.h"
#include "mxjson.h"

void api_register_push(http_server *s);

json *push_ruleset(const char *user_id);

json *push_actions_for(const char *user_id, const char *room_id,
                       const json *event, bool *notify, bool *highlight);

bool push_decision(const char *user_id, const char *room_id, int64_t stream_id,
                   const char *event_id, bool *notify, bool *highlight);

void push_unread_counts(const char *user_id, const char *room_id, int64_t upto,
                        int64_t *notif, int64_t *highlight);

#endif
