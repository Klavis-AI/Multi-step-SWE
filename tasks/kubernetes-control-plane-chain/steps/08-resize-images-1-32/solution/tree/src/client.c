#define _GNU_SOURCE
#include "client.h"
#include "http.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *field_after(const char *text, const char *key){
    const char *p = strstr(text, key);
    if(!p) return NULL;
    p += strlen(key);
    while(*p == ' ' || *p == '\t') p++;
    const char *e = p;
    while(*e && *e != '\n' && *e != '\r') e++;
    while(e > p && (e[-1] == ' ' || e[-1] == '"')) e--;
    if(*p == '"') p++;
    size_t n = (size_t)(e - p);
    char *out = malloc(n + 1);
    memcpy(out, p, n); out[n] = 0;
    return out;
}

int kc_from_kubeconfig(kclient *c, const char *path){
    FILE *f = fopen(path, "rb");
    if(!f) return -1;
    buf b = {0};
    char tmp[4096]; size_t n;
    while((n = fread(tmp, 1, sizeof tmp, f)) > 0) buf_add(&b, tmp, n);
    fclose(f);
    char *server = field_after(b.p, "server:");
    char *token = field_after(b.p, "token:");
    buf_free(&b);
    if(!server) return -1;
    c->tls = str_has_prefix(server, "https://");
    const char *h = server + (c->tls ? 8 : 7);
    char *colon = strrchr(h, ':');
    if(!colon){ free(server); return -1; }
    size_t hl = (size_t)(colon - h);
    c->host = malloc(hl + 1); memcpy(c->host, h, hl); c->host[hl] = 0;
    c->port = atoi(colon + 1);
    c->token = token ? token : xstrdup2("");
    free(server);
    return 0;
}

json *kc_send(kclient *c, const char *method, const char *path, json *body,
              const char *ctype, int *status){
    char auth[1024];
    snprintf(auth, sizeof auth, "Authorization: Bearer %s", c->token);
    char ct[128];
    snprintf(ct, sizeof ct, "Content-Type: %s", ctype ? ctype : "application/json");
    const char *hdrs[] = { auth, "Accept: application/json", ct };
    char *text = body ? j_dump(body) : NULL;
    hresp r;
    int rc = http_request(c->host, c->port, c->tls, method, path, hdrs,
                          body ? 3 : 2, text ? text : "", text ? strlen(text) : 0, &r, 30);
    free(text);
    if(body) j_free(body);
    if(rc < 0){ if(status) *status = 0; return NULL; }
    if(status) *status = r.status;
    json *out = r.body ? j_parse(r.body, r.blen) : NULL;
    hresp_free(&r);
    return out;
}

json *kc_get(kclient *c, const char *path, int *status){
    return kc_send(c, "GET", path, NULL, NULL, status);
}
