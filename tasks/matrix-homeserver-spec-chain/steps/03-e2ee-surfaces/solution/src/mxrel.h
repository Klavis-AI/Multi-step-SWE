#ifndef MXREL_H
#define MXREL_H

#include <stdbool.h>
#include <stdint.h>

#include "mxhttp.h"
#include "mxjson.h"

void api_register_relations(http_server *s);

void rel_client_event_hook(json *ev, json *un, const char *user_id);

bool rel_relation_allowed(const char *room_id, const json *content,
                          const char **errcode, const char **message);

bool rel_timeline_allows(const json *f_timeline, const char *room_id,
                         const json *stored);

bool rel_room_has_news(const char *room_id, const char *user_id,
                       int64_t since, int64_t now);

void rel_sync_room_section(json *sec, const char *room_id, const char *user_id,
                           const json *filter, int64_t since, int64_t now);

#endif
