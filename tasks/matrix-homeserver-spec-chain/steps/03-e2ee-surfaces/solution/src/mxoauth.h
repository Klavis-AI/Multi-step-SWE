#ifndef MXOAUTH_H
#define MXOAUTH_H

#include <stdbool.h>

#include "mxhttp.h"
#include "mxjson.h"

void api_register_oauth(http_server *s);

bool oauth_delegated(void);

const char *oauth_issuer(void);

const char *oauth_account_management_url(void);

bool oauth_is_admin_token(const char *token);

bool oauth_before(http_req *r, http_resp *w);

void oauth_after(http_req *r, http_resp *w);

#endif
