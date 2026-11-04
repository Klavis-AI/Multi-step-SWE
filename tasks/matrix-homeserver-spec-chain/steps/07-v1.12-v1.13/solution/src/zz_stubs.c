/* Generated: no-op definitions for entry points this build
 * declares but does not link. */
#include "mxhttp.h"
#include "mxjson.h"
#include "mxapi.h"
#include "mxroom.h"
void api_register_oauth(http_server *s) { (void)0; }
bool oauth_delegated(void) { (void)0; return false; }
const char *oauth_issuer(void) { (void)0; return 0; }
const char *oauth_account_management_url(void) { (void)0; return 0; }
bool oauth_is_admin_token(const char *token) { (void)0; return false; }
bool oauth_before(http_req *r, http_resp *w) { (void)0; return true; }
void oauth_after(http_req *r, http_resp *w) { (void)0; }
