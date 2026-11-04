/* Generated: no-op definitions for entry points this build
 * declares but does not link. */
#include "mxhttp.h"
#include "mxjson.h"
#include "mxapi.h"
#include "mxroom.h"
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
