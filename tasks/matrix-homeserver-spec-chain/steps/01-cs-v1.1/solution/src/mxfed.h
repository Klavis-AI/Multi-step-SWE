#ifndef MXFED_H
#define MXFED_H

#include <stdbool.h>
#include <stdint.h>

#include "mxhttp.h"
#include "mxjson.h"

void api_register_federation(http_server *s);

void fed_shutdown(void);

bool fed_is_local_server(const char *server_name);
bool fed_is_local_user(const char *user_id);

char *fed_base_url(const char *server_name);

json *fed_local_keys(void);

bool fed_fetch_key(const char *server_name, const char *key_id,
                   unsigned char pub[32]);

http_client_resp *fed_request(const char *destination, const char *method,
                              const char *path, const json *body);

bool fed_authenticate(http_req *r, http_resp *w, char **origin_out);

void fed_wake(void);

char *fed_join_remote(const char *user_id, const char *room_id_or_alias,
                      const char *const *servers, int nservers,
                      int *status, const char **errcode, char **errmsg);

bool fed_leave_remote(const char *user_id, const char *room_id,
                      const char *const *servers, int nservers,
                      int *status, const char **errcode, char **errmsg);

bool fed_knock_remote(const char *user_id, const char *room_id_or_alias,
                      const char *const *servers, int nservers, json *content,
                      int *status, const char **errcode, char **errmsg);

json *fed_invite_remote(const char *room_id, const char *event_id,
                        const json *invite_event, const json *invite_room_state,
                        int *status, const char **errcode, char **errmsg);

json *fed_query_profile(const char *user_id, const char *field);
json *fed_query_directory(const char *room_alias);
json *fed_query_user_devices(const char *user_id);

json *fed_room_servers(const char *room_id);

void fed_broadcast_edu(const char *room_id, const char *edu_type, json *content);

#endif
