/* Generated: no-op definitions for entry points this build
 * declares but does not link. */
#include "mxhttp.h"
#include "mxjson.h"
#include "mxapi.h"
#include "mxroom.h"
void api_register_federation(http_server *s) { (void)0; }
void fed_shutdown(void) { (void)0; }
bool fed_is_local_server(const char *server_name) { (void)0; return true; }
bool fed_is_local_user(const char *user_id) { (void)0; return true; }
char *fed_base_url(const char *server_name) { (void)0; return 0; }
json *fed_local_keys(void) { (void)0; return 0; }
bool fed_fetch_key(const char *server_name, const char *key_id, unsigned char pub[32]) { (void)0; return false; }
http_client_resp *fed_request(const char *destination, const char *method, const char *path, const json *body) { (void)0; return 0; }
bool fed_authenticate(http_req *r, http_resp *w, char **origin_out) { (void)0; return false; }
void fed_wake(void) { (void)0; }
char *fed_join_remote(const char *user_id, const char *room_id_or_alias, const char *const *servers, int nservers, int *status, const char **errcode, char **errmsg) { (void)0; return 0; }
bool fed_leave_remote(const char *user_id, const char *room_id, const char *const *servers, int nservers, int *status, const char **errcode, char **errmsg) { (void)0; return false; }
bool fed_knock_remote(const char *user_id, const char *room_id_or_alias, const char *const *servers, int nservers, json *content, int *status, const char **errcode, char **errmsg) { (void)0; return false; }
json *fed_invite_remote(const char *room_id, const char *event_id, const json *invite_event, const json *invite_room_state, int *status, const char **errcode, char **errmsg) { (void)0; return 0; }
json *fed_query_profile(const char *user_id, const char *field) { (void)0; return 0; }
json *fed_query_directory(const char *room_alias) { (void)0; return 0; }
json *fed_query_user_devices(const char *user_id) { (void)0; return 0; }
json *fed_room_servers(const char *room_id) { (void)0; return 0; }
void fed_broadcast_edu(const char *room_id, const char *edu_type, json *content) { (void)0; }
void api_register_keys(http_server *s) { (void)0; }
bool keys_handle_edu(const char *origin, const json *edu) { (void)0; return false; }
void keys_device_list_changed(const char *user_id, const char *device_id, bool deleted) { (void)0; }
json *keys_user_device_keys(const char *user_id) { (void)0; return 0; }
json *keys_sync_device_lists(const char *user_id, int64_t since, int64_t now) { (void)0; return 0; }
json *keys_sync_otk_counts(const char *user_id, const char *device_id) { (void)0; return 0; }
json *keys_sync_unused_fallback_key_types(const char *user_id, const char *device_id) { (void)0; return 0; }
void api_register_media2(http_server *s) { (void)0; }
int64_t media_freeze_instant(void) { (void)0; return 0; }
void media2_membership_hook(json *ev, json *un, const char *user_id) { (void)0; }
void media2_erasure_record(const char *user_id) { (void)0; }
bool media2_erased_from(const json *ev, const char *viewer) { (void)0; return false; }
void api_register_oauth(http_server *s) { (void)0; }
bool oauth_delegated(void) { (void)0; return false; }
const char *oauth_issuer(void) { (void)0; return 0; }
const char *oauth_account_management_url(void) { (void)0; return 0; }
bool oauth_is_admin_token(const char *token) { (void)0; return false; }
bool oauth_before(http_req *r, http_resp *w) { (void)0; return true; }
void oauth_after(http_req *r, http_resp *w) { (void)0; }
void api_register_push(http_server *s) { (void)0; }
json *push_ruleset(const char *user_id) { (void)0; return 0; }
json *push_actions_for(const char *user_id, const char *room_id, const json *event, bool *notify, bool *highlight) { (void)0; return 0; }
bool push_decision(const char *user_id, const char *room_id, int64_t stream_id, const char *event_id, bool *notify, bool *highlight) { (void)0; return false; }
void push_unread_counts(const char *user_id, const char *room_id, int64_t upto, int64_t *notif, int64_t *highlight) { (void)0; }
void api_register_relations(http_server *s) { (void)0; }
void rel_client_event_hook(json *ev, json *un, const char *user_id) { (void)0; }
bool rel_relation_allowed(const char *room_id, const json *content, const char **errcode, const char **message) { (void)0; return true; }
bool rel_timeline_allows(const json *f_timeline, const char *room_id, const json *stored) { (void)0; return true; }
bool rel_room_has_news(const char *room_id, const char *user_id, int64_t since, int64_t now) { (void)0; return false; }
void rel_sync_room_section(json *sec, const char *room_id, const char *user_id, const json *filter, int64_t since, int64_t now) { (void)0; }
void api_register_slide(http_server *s) { (void)0; }
bool slide_install_gate(http_server *s) { (void)0; return false; }
bool slide_user_locked(const char *user_id) { (void)0; return false; }
bool slide_user_suspended(const char *user_id) { (void)0; return false; }
void slide_set_user_flag(const char *user_id, const char *flag, bool on) { (void)0; }
bool slide_get_user_flag(const char *user_id, const char *flag) { (void)0; return false; }
char *slide_rawpath(const http_req *r) { (void)0; return 0; }
int slide_query_rename(http_req *r, const char *from, const char *to) { (void)0; return 0; }
void slide_set_body(http_req *r, const json *v) { (void)0; }
json *slide_resp_json(const http_resp *w) { (void)0; return 0; }
void slide_resp_replace(http_resp *w, json *v) { (void)0; }
char *slide_bearer_token(const http_req *r) { (void)0; return 0; }
