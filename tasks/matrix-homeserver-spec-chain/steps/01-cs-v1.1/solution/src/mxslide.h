#ifndef MXSLIDE_H
#define MXSLIDE_H

#include <stdbool.h>
#include <stdint.h>

#include "mxhttp.h"
#include "mxjson.h"

void api_register_slide(http_server *s);

bool slide_install_gate(http_server *s);

bool slide_user_locked(const char *user_id);
bool slide_user_suspended(const char *user_id);

void slide_set_user_flag(const char *user_id, const char *flag, bool on);
bool slide_get_user_flag(const char *user_id, const char *flag);

char *slide_rawpath(const http_req *r);

int slide_query_rename(http_req *r, const char *from, const char *to);

void slide_set_body(http_req *r, const json *v);

json *slide_resp_json(const http_resp *w);
void slide_resp_replace(http_resp *w, json *v);

char *slide_bearer_token(const http_req *r);

#endif
