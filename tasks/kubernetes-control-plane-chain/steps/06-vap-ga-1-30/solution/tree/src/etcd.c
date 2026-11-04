#define _GNU_SOURCE
#include "etcd.h"
#include "http.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int etcd_init(etcd_t *e, const char *servers){
    const char *p = servers;
    if(str_has_prefix(p, "http://")) p += 7;
    else if(str_has_prefix(p, "https://")) p += 8;
    char *comma = strchr(p, ',');
    size_t n = comma ? (size_t)(comma - p) : strlen(p);
    char *one = malloc(n+1); memcpy(one, p, n); one[n] = 0;
    char *colon = strrchr(one, ':');
    if(!colon){ free(one); return -1; }
    *colon = 0;
    e->host = xstrdup2(one);
    e->port = atoi(colon+1);
    free(one);
    return 0;
}

static json *call(etcd_t *e, const char *path, json *req){
    char *body = j_dump(req);
    j_free(req);
    const char *hdrs[] = { "Content-Type: application/json" };
    hresp r;
    int rc = http_request(e->host, e->port, 0, "POST", path, hdrs, 1, body, strlen(body), &r, 60);
    free(body);
    if(rc < 0) return NULL;
    json *out = (r.status == 200 && r.body) ? j_parse(r.body, r.blen) : NULL;
    hresp_free(&r);
    return out;
}

static long long hdr_rev(json *resp){
    if(!resp) return -1;
    json *h = j_get(resp, "header");
    const char *rv = h ? j_gets(h, "revision") : NULL;
    return rv ? strtoll(rv, NULL, 10) : -1;
}

static char *b64s(const char *s){ return b64_encode((const unsigned char*)s, strlen(s)); }

static char *range_end(const char *prefix){
    size_t n = strlen(prefix);
    char *e = malloc(n+1); memcpy(e, prefix, n+1);
    for(size_t i = n; i > 0; i--){
        if((unsigned char)e[i-1] < 0xff){ e[i-1]++; e[i] = 0; return e; }
    }
    free(e);
    return xstrdup2("\0");
}

long long etcd_put(etcd_t *e, const char *key, const char *val){
    json *req = j_obj();
    char *k = b64s(key), *v = b64s(val);
    j_set(req, "key", j_str(k)); j_set(req, "value", j_str(v));
    free(k); free(v);
    json *resp = call(e, "/v3/kv/put", req);
    long long rev = hdr_rev(resp);
    j_free(resp);
    return rev;
}

/* txn: succeed only when mod_revision matches */
long long etcd_put_cas(etcd_t *e, const char *key, const char *val, long long mod){
    char *k = b64s(key), *v = b64s(val);
    json *cmp = j_obj();
    j_set(cmp, "key", j_str(k));
    j_set(cmp, "target", j_str("MOD"));
    j_set(cmp, "result", j_str("EQUAL"));
    { char b[32]; snprintf(b, sizeof b, "%lld", mod); j_set(cmp, "mod_revision", j_str(b)); }
    json *put = j_obj();
    j_set(put, "key", j_str(k)); j_set(put, "value", j_str(v));
    json *op = j_obj(); j_set(op, "requestPut", put);
    json *req = j_obj();
    json *ca = j_arr(); j_push(ca, cmp); j_set(req, "compare", ca);
    json *su = j_arr(); j_push(su, op); j_set(req, "success", su);
    free(k); free(v);
    json *resp = call(e, "/v3/kv/txn", req);
    long long rev = -1;
    if(resp && j_getb(resp, "succeeded", 0)) rev = hdr_rev(resp);
    j_free(resp);
    return rev;
}

long long etcd_create(etcd_t *e, const char *key, const char *val){
    char *k = b64s(key), *v = b64s(val);
    json *cmp = j_obj();
    j_set(cmp, "key", j_str(k));
    j_set(cmp, "target", j_str("MOD"));
    j_set(cmp, "result", j_str("EQUAL"));
    j_set(cmp, "mod_revision", j_str("0"));
    json *put = j_obj();
    j_set(put, "key", j_str(k)); j_set(put, "value", j_str(v));
    json *op = j_obj(); j_set(op, "requestPut", put);
    json *req = j_obj();
    json *ca = j_arr(); j_push(ca, cmp); j_set(req, "compare", ca);
    json *su = j_arr(); j_push(su, op); j_set(req, "success", su);
    free(k); free(v);
    json *resp = call(e, "/v3/kv/txn", req);
    long long rev = -1;
    if(resp && j_getb(resp, "succeeded", 0)) rev = hdr_rev(resp);
    j_free(resp);
    return rev;
}

long long etcd_delete(etcd_t *e, const char *key, long long mod){
    char *k = b64s(key);
    json *req = j_obj();
    if(mod > 0){
        json *cmp = j_obj();
        j_set(cmp, "key", j_str(k));
        j_set(cmp, "target", j_str("MOD"));
        j_set(cmp, "result", j_str("EQUAL"));
        { char b[32]; snprintf(b, sizeof b, "%lld", mod); j_set(cmp, "mod_revision", j_str(b)); }
        json *del = j_obj(); j_set(del, "key", j_str(k));
        json *op = j_obj(); j_set(op, "requestDeleteRange", del);
        json *ca = j_arr(); j_push(ca, cmp); j_set(req, "compare", ca);
        json *su = j_arr(); j_push(su, op); j_set(req, "success", su);
        free(k);
        json *resp = call(e, "/v3/kv/txn", req);
        long long rev = -1;
        if(resp && j_getb(resp, "succeeded", 0)) rev = hdr_rev(resp);
        j_free(resp);
        return rev;
    }
    j_set(req, "key", j_str(k)); free(k);
    json *resp = call(e, "/v3/kv/deleterange", req);
    long long rev = hdr_rev(resp);
    j_free(resp);
    return rev;
}

json *etcd_range(etcd_t *e, const char *prefix, long long *rev){
    char *end = range_end(prefix);
    char *k = b64s(prefix), *ke = b64s(end);
    free(end);
    json *req = j_obj();
    j_set(req, "key", j_str(k));
    j_set(req, "range_end", j_str(ke));
    j_set(req, "limit", j_str("0"));
    free(k); free(ke);
    json *resp = call(e, "/v3/kv/range", req);
    if(!resp){ if(rev) *rev = -1; return NULL; }
    if(rev) *rev = hdr_rev(resp);
    json *out = j_arr();
    json *kvs = j_get(resp, "kvs");
    if(j_is(kvs, J_ARR)){
        for(size_t i = 0; i < kvs->nitems; i++){
            json *kv = kvs->items[i];
            size_t kn = 0, vn = 0;
            unsigned char *kb = b64_decode(j_gets(kv, "key") ?: "", &kn);
            const char *vs = j_gets(kv, "value");
            unsigned char *vb = vs ? b64_decode(vs, &vn) : NULL;
            json *o = j_obj();
            j_set(o, "key", j_strn((char*)kb, kn));
            j_set(o, "value", vb ? j_strn((char*)vb, vn) : j_str(""));
            const char *mr = j_gets(kv, "mod_revision");
            j_set(o, "mod", j_int(mr ? strtoll(mr, NULL, 10) : 0));
            j_push(out, o);
            free(kb); free(vb);
        }
    }
    j_free(resp);
    return out;
}

long long etcd_revision(etcd_t *e){
    json *req = j_obj();
    char *k = b64s("/registry/");
    j_set(req, "key", j_str(k)); free(k);
    j_set(req, "limit", j_str("1"));
    j_set(req, "keys_only", j_bool(1));
    json *resp = call(e, "/v3/kv/range", req);
    long long rev = hdr_rev(resp);
    j_free(resp);
    return rev;
}
