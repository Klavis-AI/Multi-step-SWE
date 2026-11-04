#define _GNU_SOURCE
#include "api.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>

apiconfig CFG;
json *SURFACE;
#ifndef KCP_RELEASE
#define KCP_RELEASE 25
#endif
int RELEASE_MINOR = KCP_RELEASE;
static etcd_t ETCD;
extern void token_file_load(const char *path);
extern json *rbac_rules_review(const ident *who, const char *ns);
extern void bootstrap_rbac(void);
extern json *table_for(const char *kind, json *list_or_obj);
extern void controllers_note_ready(void);
static __thread int SCALE_REPLY = 0;
extern int STATUS_OMIT_CODE;

static json *scale_view(json *obj, const char *ns, const char *name){
    json *sc = j_obj();
    j_set(sc, "kind", j_str("Scale"));
    j_set(sc, "apiVersion", j_str("autoscaling/v1"));
    json *md = j_obj();
    j_set(md, "name", j_str(name ?: ""));
    if(ns && *ns) j_set(md, "namespace", j_str(ns));
    json *omd = j_get(obj, "metadata");
    for(const char **k = (const char*[]){"uid", "resourceVersion", "creationTimestamp", NULL}; *k; k++)
        if(omd && j_get(omd, *k)) j_set(md, *k, j_clone(j_get(omd, *k)));
    j_set(sc, "metadata", md);
    json *sp = j_obj();
    json *rep = j_path(obj, "spec.replicas");
    json *size = j_path(obj, "spec.size");
    j_set(sp, "replicas", rep ? j_clone(rep) : (size ? j_clone(size) : j_int(0)));
    j_set(sc, "spec", sp);
    json *st = j_obj();
    json *sr = j_path(obj, "status.replicas");
    j_set(st, "replicas", sr ? j_clone(sr) : j_int(0));
    json *sel = j_path(obj, "spec.selector.matchLabels");
    if(sel && sel->t == J_OBJ && sel->nmem){
        buf b = {0};
        for(size_t i = 0; i < sel->nmem; i++)
            buf_printf(&b, "%s%s=%s", i ? "," : "", sel->mem[i].key, j_str_of(sel->mem[i].val) ?: "");
        j_set(st, "selector", j_str(b.p));
        buf_free(&b);
    }
    j_set(sc, "status", st);
    return sc;
}

/* ------------------------------- helpers ---------------------------------- */
json *meta(json *o){ return j_ensure_obj(o, "metadata"); }
const char *meta_str(const json *o, const char *f){
    json *m = j_get(o, "metadata");
    return m ? j_gets(m, f) : NULL;
}

int STATUS_OMIT_CODE = 0;
const char *VALIDATE_SUB = "";
const char *ADMIT_DETAIL_KIND = NULL;
char *ADMIT_DETAIL_NAME = NULL;

/* Upstream registers a web service per *served group*; a request under a group
   with no served version at all reaches go-restful's fallthrough handler, which
   writes plain text rather than a metav1.Status. */
static json *api_group(const char *name);
static int group_is_served(const char *group){
    extern json *crd_all_group_versions(void);
    if(upstream_serves_group(group)) return 1;
    int found = 0;
    store_lock();
    json *g = api_group(group);
    if(g){ found = 1; j_free(g); }
    if(!found){
        json *e = crd_all_group_versions();
        for(size_t i = 0; e && i < e->nitems; i++)
            if(!strcmp(j_gets(e->items[i], "name") ?: "", group)){ found = 1; break; }
        j_free(e);
    }
    store_unlock();
    return found;
}



json *status_obj(int code, const char *reason, const char *msg, const char *group,
                 const char *kind, const char *name, json *causes){
    json *s = j_obj();
    j_set(s, "kind", j_str("Status"));
    j_set(s, "apiVersion", j_str("v1"));
    j_set(s, "metadata", j_obj());
    j_set(s, "status", j_str(code < 400 ? "Success" : "Failure"));
    if(msg) j_set(s, "message", j_str(msg));
    if(reason) j_set(s, "reason", j_str(reason));
    if((group && *group) || kind || name || causes){
        json *d = j_obj();
        if(group && *group) j_set(d, "group", j_str(group));
        if(kind) j_set(d, "kind", j_str(kind));
        if(name) j_set(d, "name", j_str(name));
        if(causes) j_set(d, "causes", causes);
        j_set(s, "details", d);
    }
    if(!STATUS_OMIT_CODE) j_set(s, "code", j_int(code));
    STATUS_OMIT_CODE = 0;
    return s;
}

void reply_obj(hconn *c, int code, json *obj){
    char *t = j_dump(obj);
    h_reply_json(c, code, t);
    free(t);
    j_free(obj);
}
void reply_status(hconn *c, int code, const char *reason, const char *msg,
                  const char *group, const char *kind, const char *name, json *causes){
    reply_obj(c, code, status_obj(code, reason, msg, group, kind, name, causes));
}

char *obj_key(resdef *rd, const char *ns, const char *name){
    if(rd->namespaced) return xasprintf("/registry/%s/%s/%s", rd->prefix, ns, name);
    return xasprintf("/registry/%s/%s", rd->prefix, name);
}

static void set_rv(json *o, long long rv){
    char b[32]; snprintf(b, sizeof b, "%lld", rv);
    j_set(meta(o), "resourceVersion", j_str(b));
}

/* ------------------------------ selectors --------------------------------- */
typedef struct { char *key, *op; json *vals; } selterm;

static json *parse_label_selector(const char *sel, int *bad){
    json *terms = j_arr();
    *bad = 0;
    if(!sel || !*sel) return terms;
    const char *p = sel;
    while(*p){
        while(*p == ' ' || *p == ',') p++;
        if(!*p) break;
        /* find the end of this requirement, respecting (...) */
        const char *q = p; int depth = 0;
        while(*q && !(*q == ',' && depth == 0)){ if(*q=='(') depth++; else if(*q==')') depth--; q++; }
        size_t len = (size_t)(q - p);
        char *req = malloc(len+1); memcpy(req, p, len); req[len] = 0;
        char *s = req;
        while(*s == ' ') s++;
        char *endsp = s + strlen(s);
        while(endsp > s && endsp[-1] == ' ') *--endsp = 0;
        json *t = j_obj();
        char *inpos = strstr(s, " in ");
        char *notin = strstr(s, " notin ");
        if(notin){
            *notin = 0;
            char *key = s; char *vals = notin + 7;
            j_set(t, "key", j_str(key)); j_set(t, "op", j_str("notin"));
            json *vl = j_arr();
            char *lp = strchr(vals, '('); char *rp = strrchr(vals, ')');
            if(lp && rp){ *rp = 0;
                int n=0; char **parts = str_split(lp+1, ',', &n);
                for(int i=0;i<n;i++){ char *v = parts[i]; while(*v==' ') v++; j_push(vl, j_str(v)); }
                str_split_free(parts, n);
            } else *bad = 1;
            j_set(t, "vals", vl);
        } else if(inpos){
            *inpos = 0;
            char *key = s; char *vals = inpos + 4;
            j_set(t, "key", j_str(key)); j_set(t, "op", j_str("in"));
            json *vl = j_arr();
            char *lp = strchr(vals, '('); char *rp = strrchr(vals, ')');
            if(lp && rp){ *rp = 0;
                int n=0; char **parts = str_split(lp+1, ',', &n);
                for(int i=0;i<n;i++){ char *v = parts[i]; while(*v==' ') v++; j_push(vl, j_str(v)); }
                str_split_free(parts, n);
            } else *bad = 1;
            j_set(t, "vals", vl);
        } else if(strstr(s, "!=")){
            char *eq = strstr(s, "!="); *eq = 0;
            char *k = s; char *v = eq + 2;
            while(*k==' ') k++; while(*v==' ') v++;
            j_set(t, "key", j_str(k)); j_set(t, "op", j_str("!="));
            json *vl = j_arr(); j_push(vl, j_str(v)); j_set(t, "vals", vl);
            if(!*k) *bad = 1;
        } else if(strchr(s, '=')){
            char *eq = strchr(s, '=');
            if(eq > s && eq[-1] == '=') { /* == */ }
            char *v = eq + 1; if(*v == '=') v++;
            char save = *eq; (void)save;
            char *k = s;
            char *ke = eq; if(ke > s && ke[-1]=='=') ke--;
            *ke = 0;
            while(*k==' ') k++; while(*v==' ') v++;
            j_set(t, "key", j_str(k)); j_set(t, "op", j_str("="));
            json *vl = j_arr(); j_push(vl, j_str(v)); j_set(t, "vals", vl);
            if(!*k) *bad = 1;
        } else if(s[0] == '!'){
            j_set(t, "key", j_str(s+1)); j_set(t, "op", j_str("!")); j_set(t, "vals", j_arr());
            if(!s[1]) *bad = 1;
        } else {
            j_set(t, "key", j_str(s)); j_set(t, "op", j_str("exists")); j_set(t, "vals", j_arr());
            if(!s[0]) *bad = 1;
        }
        j_push(terms, t);
        free(req);
        p = *q ? q + 1 : q;
    }
    return terms;
}

static int labels_match(json *terms, json *obj){
    json *labels = j_get(j_get(obj, "metadata"), "labels");
    for(size_t i = 0; i < terms->nitems; i++){
        json *t = terms->items[i];
        const char *k = j_gets(t, "key"), *op = j_gets(t, "op");
        json *vals = j_get(t, "vals");
        const char *have = labels ? j_gets(labels, k) : NULL;
        if(!strcmp(op, "exists")){ if(!have) return 0; continue; }
        if(!strcmp(op, "!")){ if(have) return 0; continue; }
        if(!strcmp(op, "=")){ if(!have || strcmp(have, j_str_of(vals->items[0]))) return 0; continue; }
        if(!strcmp(op, "!=")){ if(have && !strcmp(have, j_str_of(vals->items[0]))) return 0; continue; }
        int in = 0;
        for(size_t k2 = 0; have && k2 < vals->nitems; k2++)
            if(!strcmp(have, j_str_of(vals->items[k2]))) in = 1;
        if(!strcmp(op, "in") && !in) return 0;
        if(!strcmp(op, "notin") && in) return 0;
    }
    return 1;
}

/* Field selectors: only the fields upstream indexes are accepted. */
static const char *FIELD_OK[] = { "metadata.name", "metadata.namespace", "status.phase",
                                  "spec.nodeName", "spec.restartPolicy", "involvedObject.name",
                                  "involvedObject.kind", "involvedObject.namespace",
                                  "regarding.name", "reason", "type",
                                  "spec.unschedulable", NULL };

static int field_selector_ok(const char *f){
    for(int i = 0; FIELD_OK[i]; i++) if(!strcmp(FIELD_OK[i], f)) return 1;
    return 0;
}

static json *parse_field_selector(const char *sel, int *bad){
    json *terms = j_arr();
    *bad = 0;
    if(!sel || !*sel) return terms;
    int n = 0; char **parts = str_split(sel, ',', &n);
    for(int i = 0; i < n; i++){
        char *s = parts[i];
        char *neq = strstr(s, "!=");
        char *eq = strchr(s, '=');
        json *t = j_obj();
        if(neq){ *neq = 0; j_set(t, "field", j_str(s)); j_set(t, "op", j_str("!="));
                 j_set(t, "val", j_str(neq+2)); }
        else if(eq){ *eq = 0; j_set(t, "field", j_str(s)); j_set(t, "op", j_str("="));
                     j_set(t, "val", j_str(eq+1)); }
        else { *bad = 1; j_free(t); continue; }
        if(!field_selector_ok(j_gets(t, "field"))) *bad = 1;
        j_push(terms, t);
    }
    str_split_free(parts, n);
    return terms;
}

static const char *field_value(json *obj, const char *f){
    if(!strcmp(f, "metadata.name")) return meta_str(obj, "name");
    if(!strcmp(f, "metadata.namespace")) return meta_str(obj, "namespace");
    json *v = j_path(obj, f);
    return v ? j_str_of(v) : NULL;
}

static int fields_match(json *terms, json *obj){
    for(size_t i = 0; i < terms->nitems; i++){
        json *t = terms->items[i];
        const char *f = j_gets(t, "field"), *op = j_gets(t, "op"), *want = j_gets(t, "val");
        const char *have = field_value(obj, f);
        if(!have) have = "";
        if(!strcmp(op, "=") && strcmp(have, want)) return 0;
        if(!strcmp(op, "!=") && !strcmp(have, want)) return 0;
    }
    return 1;
}

/* --------------------------- list construction ----------------------------- */
typedef struct {
    json *items; json *lterms, *fterms;
    const char *ns;
    resdef *rd; const char *version;
    long long limit; const char *cont;
    int truncated; char last_name[320];
} listctx;

static int list_cb(sobj *o, void *ud){
    listctx *L = ud;
    if(!labels_match(L->lterms, o->obj)) return 1;
    if(!fields_match(L->fterms, o->obj)) return 1;
    j_push(L->items, j_clone(o->obj));
    return 1;
}

static int cmp_items(const void *a, const void *b){
    json *x = *(json**)a, *y = *(json**)b;
    const char *nx = meta_str(x, "namespace") ?: "";
    const char *ny = meta_str(y, "namespace") ?: "";
    int c = strcmp(nx, ny);
    if(c) return c;
    return strcmp(meta_str(x, "name") ?: "", meta_str(y, "name") ?: "");
}

static json *build_list(resdef *rd, const char *version, const char *ns,
                        json *lterms, json *fterms, long long limit, const char *cont,
                        long long rv){
    listctx L = {0};
    L.items = j_arr(); L.lterms = lterms; L.fterms = fterms;
    char pfx[512];
    if(rd->namespaced && ns && *ns) snprintf(pfx, sizeof pfx, "/registry/%s/%s/", rd->prefix, ns);
    else snprintf(pfx, sizeof pfx, "/registry/%s/", rd->prefix);
    store_each(pfx, list_cb, &L);
    qsort(L.items->items, L.items->nitems, sizeof(json*), cmp_items);
    json *out = j_obj();
    char *lk = xasprintf("%sList", rd->kind);
    j_set(out, "kind", j_str(lk)); free(lk);
    j_set(out, "apiVersion", j_str(*rd->group ? xstrdup2("") : ""));
    j_del(out, "apiVersion");
    if(*rd->group){ char *av = xasprintf("%s/%s", rd->group, version); j_set(out, "apiVersion", j_str(av)); free(av); }
    else j_set(out, "apiVersion", j_str(version));
    json *md = j_obj();
    set_rv(out, rv);
    json *m2 = j_get(out, "metadata");
    if(m2){ j_set(md, "resourceVersion", j_clone(j_get(m2, "resourceVersion"))); }
    j_set(out, "metadata", md);
    /* pagination */
    size_t start = 0;
    if(cont && *cont){
        size_t n = 0;
        unsigned char *dec = b64_decode(cont, &n);
        if(!dec){ j_free(out); j_free(L.items); return NULL; }
        char *token = malloc(n + 1); memcpy(token, dec, n); token[n] = 0; free(dec);
        json *tj = j_parse0(token);
        const char *after = tj ? j_gets(tj, "start") : NULL;
        if(!after){ free(token); j_free(tj); j_free(out); j_free(L.items); return NULL; }
        for(size_t i = 0; i < L.items->nitems; i++){
            char full[640];
            snprintf(full, sizeof full, "%s/%s", meta_str(L.items->items[i], "namespace") ?: "",
                     meta_str(L.items->items[i], "name") ?: "");
            if(strcmp(full, after) > 0){ start = i; break; }
            start = i + 1;
        }
        free(token); j_free(tj);
    }
    json *items = j_arr();
    size_t emitted = 0;
    size_t i = start;
    for(; i < L.items->nitems; i++){
        if(limit > 0 && emitted >= (size_t)limit) break;
        json *conv = convert_to(rd, L.items->items[i], version);
        j_del(conv, "apiVersion");
        j_del(conv, "kind");
        set_rv(conv, 0);
        j_del(j_get(conv, "metadata"), "resourceVersion");
        json *src = L.items->items[i];
        const char *srv = meta_str(src, "resourceVersion");
        if(srv) j_set(meta(conv), "resourceVersion", j_str(srv));
        j_push(items, conv);
        emitted++;
    }
    if(limit > 0 && i < L.items->nitems){
        char full[640];
        snprintf(full, sizeof full, "%s/%s", meta_str(L.items->items[i-1], "namespace") ?: "",
                 meta_str(L.items->items[i-1], "name") ?: "");
        json *tj = j_obj();
        j_set(tj, "v", j_str("meta.k8s.io/v1"));
        j_set(tj, "rv", j_int(store_rv()));
        j_set(tj, "start", j_str(full));
        char *t = j_dump(tj); j_free(tj);
        char *b64 = b64_encode((unsigned char*)t, strlen(t)); free(t);
        j_set(j_get(out, "metadata"), "continue", j_str(b64));
        j_set(j_get(out, "metadata"), "remainingItemCount", j_int((long long)(L.items->nitems - i)));
        free(b64);
    }
    j_set(out, "items", items);
    j_free(L.items);
    return out;
}

/* --------------------------- request parsing ------------------------------ */
static int split_path(const char *path, char parts[][256], int max){
    int n = 0;
    const char *p = path;
    while(*p == '/') p++;
    while(*p && n < max){
        const char *q = strchr(p, '/');
        size_t len = q ? (size_t)(q - p) : strlen(p);
        if(len >= 256) len = 255;
        memcpy(parts[n], p, len); parts[n][len] = 0;
        n++;
        if(!q) break;
        p = q + 1;
    }
    return n;
}

/* Exactly the verbs discovery advertises for a resource -- no RBAC wildcard. */
static int serves_verb(json *verbs, const char *verb){
    if(!j_is(verbs, J_ARR)) return 1;
    for(size_t i = 0; i < verbs->nitems; i++){
        const char *v = j_str_of(verbs->items[i]);
        if(v && !strcmp(v, verb)) return 1;
    }
    return 0;
}

static const char *verb_for(const char *method, int has_name, int watch){
    if(!strcmp(method, "GET")) return watch ? "watch" : (has_name ? "get" : "list");
    if(!strcmp(method, "POST")) return "create";
    if(!strcmp(method, "PUT")) return "update";
    if(!strcmp(method, "PATCH")) return "patch";
    if(!strcmp(method, "DELETE")) return has_name ? "delete" : "deletecollection";
    return "get";
}

static int is_true(const char *v){
    return v && (!*v || !strcmp(v, "true") || !strcmp(v, "1") || !strcmp(v, "All"));
}

/* ------------------------------ discovery --------------------------------- */
static json *api_versions(void){
    json *o = j_obj();
    j_set(o, "kind", j_str("APIVersions"));
    json *v = j_arr(); j_push(v, j_str("v1"));
    j_set(o, "versions", v);
    json *cidrs = j_arr();
    json *c = j_obj();
    j_set(c, "clientCIDR", j_str("0.0.0.0/0"));
    char *addr = xasprintf("%s:%d", CFG.bind_address, CFG.secure_port);
    j_set(c, "serverAddress", j_str(addr)); free(addr);
    j_push(cidrs, c);
    j_set(o, "serverAddressByClientCIDRs", cidrs);
    return o;
}

int gv_enabled(const char *gv){
    if(j_get(j_get(SURFACE, "groupVersions"), gv)) return 1;
    json *opt = j_get(SURFACE, "optionalGroupVersions");
    if(opt && j_get(opt, gv)){
        if(!CFG.runtime_config) return 0;
        char needle[256];
        snprintf(needle, sizeof needle, "%s=true", gv);
        return strstr(CFG.runtime_config, needle) != NULL;
    }
    return 0;
}

static void group_versions_of(const char *group, json *out){
    const char *buckets[] = {"groupVersions", "optionalGroupVersions", NULL};
    for(int b = 0; buckets[b]; b++){
        json *m = j_get(SURFACE, buckets[b]);
        for(size_t i = 0; m && i < m->nmem; i++){
            const char *gv = m->mem[i].key;
            const char *slash = strchr(gv, '/');
            if(!*group){
                /* the core group's key is the bare version */
                if(slash || !gv_enabled(gv)) continue;
                j_push(out, j_str(gv));
                continue;
            }
            if(!slash) continue;
            if(strncmp(gv, group, strlen(group)) || slash != gv + strlen(group)) continue;
            if(!gv_enabled(gv)) continue;
            j_push(out, j_str(slash + 1));
        }
    }
}

static int version_rank(const char *v){
    /* GA > beta > alpha, then by number: upstream's kube-aware version ordering */
    int maj = 0, min = 0, kind = 3;
    if(sscanf(v, "v%dalpha%d", &maj, &min) == 2) kind = 1;
    else if(sscanf(v, "v%dbeta%d", &maj, &min) == 2) kind = 2;
    else if(sscanf(v, "v%d", &maj) == 1){ kind = 3; min = 0; }
    return kind * 100000 + maj * 1000 + min;
}

static json *api_group(const char *group){
    json *vers = j_arr();
    group_versions_of(group, vers);
    if(!vers->nitems){ j_free(vers); return NULL; }
    /* sort by preference, best first */
    for(size_t i = 0; i < vers->nitems; i++)
        for(size_t k = i + 1; k < vers->nitems; k++)
            if(version_rank(j_str_of(vers->items[k])) > version_rank(j_str_of(vers->items[i]))){
                json *t = vers->items[i]; vers->items[i] = vers->items[k]; vers->items[k] = t;
            }
    json *g = j_obj();
    j_set(g, "name", j_str(group));
    json *vl = j_arr();
    for(size_t i = 0; i < vers->nitems; i++){
        json *e = j_obj();
        char *gv = xasprintf("%s/%s", group, j_str_of(vers->items[i]));
        j_set(e, "groupVersion", j_str(gv)); free(gv);
        j_set(e, "version", j_clone(vers->items[i]));
        j_push(vl, e);
    }
    j_set(g, "versions", vl);
    json *pref = j_obj();
    char *gv = xasprintf("%s/%s", group, j_str_of(vers->items[0]));
    j_set(pref, "groupVersion", j_str(gv)); free(gv);
    j_set(pref, "version", j_clone(vers->items[0]));
    j_set(g, "preferredVersion", pref);
    j_free(vers);
    return g;
}

static json *api_group_list(void){
    json *o = j_obj();
    j_set(o, "kind", j_str("APIGroupList"));
    j_set(o, "apiVersion", j_str("v1"));
    json *groups = j_arr();
    json *seen = j_obj();
    const char *buckets[] = {"groupVersions", "optionalGroupVersions", NULL};
    json *order = j_arr();
    for(int b = 0; buckets[b]; b++){
        json *m = j_get(SURFACE, buckets[b]);
        for(size_t i = 0; m && i < m->nmem; i++){
            const char *gv = m->mem[i].key;
            const char *slash = strchr(gv, '/');
            if(!slash) continue;
            char g[192];
            size_t n = (size_t)(slash - gv);
            if(n >= sizeof g) n = sizeof g - 1;
            memcpy(g, gv, n); g[n] = 0;
            if(j_get(seen, g)) continue;
            if(!gv_enabled(gv)) continue;
            j_set(seen, g, j_bool(1));
            j_push(order, j_str(g));
        }
    }
    /* upstream serves the group list in its own fixed order; ours is the
     * discovery document's order, which the surface file carries. */
    json *pref = j_get(SURFACE, "groupOrder");
    if(j_is(pref, J_ARR)){
        for(size_t i = 0; i < pref->nitems; i++){
            const char *g = j_str_of(pref->items[i]);
            if(!j_get(seen, g)) continue;
            json *ag = api_group(g);
            if(ag) j_push(groups, ag);
            j_set(seen, g, j_bool(0));
        }
    }
    for(size_t i = 0; i < order->nitems; i++){
        const char *g = j_str_of(order->items[i]);
        if(!j_getb(seen, g, 0)) continue;
        json *ag = api_group(g);
        if(ag) j_push(groups, ag);
    }
    j_free(order); j_free(seen);
    j_set(o, "groups", groups);
    return o;
}

/* ---- aggregated discovery (apidiscovery.k8s.io), from release 1.27 --------- */
/* set when the client asked for an aggregated discovery version this release
   does not serve; that is a 406, not a fall back to the unaggregated document */
static int AGG_UNACCEPTABLE = 0;

static const char *agg_version_wanted(const char *accept){
    AGG_UNACCEPTABLE = 0;
    if(!accept) return NULL;
    if(!strstr(accept, "as=APIGroupDiscoveryList")) return NULL;
    /* Only when the header offers nothing else is an unserved aggregated version
       a 406; a client that also accepts plain JSON gets the ordinary document. */
    AGG_UNACCEPTABLE = !(strstr(accept, "*/*") ||
                         strstr(accept, "application/json,") ||
                         strstr(accept, ",application/json") ||
                         strstr(accept, ", application/json") ||
                         strstr(accept, "application/yaml") ||
                         strstr(accept, "application/vnd.kubernetes.protobuf"));
    if(!strstr(accept, "g=apidiscovery.k8s.io")) return NULL;
    if(strstr(accept, "v=v2beta1") && RELEASE_MINOR >= 27 && RELEASE_MINOR <= 32){
        AGG_UNACCEPTABLE = 0; return "v2beta1";
    }
    if(strstr(accept, "v=v2") && !strstr(accept, "v=v2beta") && RELEASE_MINOR >= 30){
        AGG_UNACCEPTABLE = 0; return "v2";
    }
    return NULL;
}

static json *sub_entry(const char *sub, const char *group, const char *version,
                       const char *kind, json *verbs){
    json *e = j_obj();
    j_set(e, "subresource", j_str(sub));
    json *rk = j_obj();
    if(!strcmp(sub, "scale")){
        j_set(rk, "group", j_str("autoscaling"));
        j_set(rk, "version", j_str("v1"));
        j_set(rk, "kind", j_str("Scale"));
    } else {
        (void)group; (void)version;
        j_set(rk, "group", j_str(""));
        j_set(rk, "version", j_str(""));
        j_set(rk, "kind", j_str(kind));
    }
    j_set(e, "responseKind", rk);
    j_set(e, "verbs", j_clone(verbs));
    return e;
}

static json *agg_group(const char *group, const char *aggv){
    json *versions = j_arr();
    json *vl = j_arr();
    group_versions_of(group, vl);
    for(size_t i = 0; i < vl->nitems; i++)
        for(size_t k = i + 1; k < vl->nitems; k++)
            if(version_rank(j_str_of(vl->items[k])) > version_rank(j_str_of(vl->items[i]))){
                json *t = vl->items[i]; vl->items[i] = vl->items[k]; vl->items[k] = t;
            }
    for(size_t i = 0; i < vl->nitems; i++){
        const char *ver = j_str_of(vl->items[i]);
        char gv[256];
        if(*group) snprintf(gv, sizeof gv, "%s/%s", group, ver);
        else snprintf(gv, sizeof gv, "%s", ver);
        json *entries = j_get(j_get(SURFACE, "groupVersions"), gv);
        if(!entries) entries = j_get(j_get(SURFACE, "optionalGroupVersions"), gv);
        if(!entries) continue;
        json *ve = j_obj();
        j_set(ve, "version", j_str(ver));
        json *res = j_arr();
        for(size_t k = 0; k < entries->nitems; k++){
            json *e = entries->items[k];
            const char *nm = j_gets(e, "name");
            if(!nm || strchr(nm, '/')) continue;
            json *r = j_obj();
            j_set(r, "resource", j_str(nm));
            json *rk = j_obj();
            j_set(rk, "group", j_str(""));
            j_set(rk, "version", j_str(""));
            j_set(rk, "kind", j_str(j_gets(e, "kind") ?: ""));
            j_set(r, "responseKind", rk);
            j_set(r, "scope", j_str(j_getb(e, "namespaced", 0) ? "Namespaced" : "Cluster"));
            j_set(r, "singularResource", j_str(j_gets(e, "singularName") ?: ""));
            j_set(r, "verbs", j_clone(j_get(e, "verbs")));
            if(j_get(e, "shortNames")) j_set(r, "shortNames", j_clone(j_get(e, "shortNames")));
            if(j_get(e, "categories")) j_set(r, "categories", j_clone(j_get(e, "categories")));
            json *subs = j_arr();
            for(size_t z = 0; z < entries->nitems; z++){
                const char *sn = j_gets(entries->items[z], "name");
                if(!sn || strncmp(sn, nm, strlen(nm)) || sn[strlen(nm)] != '/') continue;
                j_push(subs, sub_entry(sn + strlen(nm) + 1, group, ver,
                                       j_gets(entries->items[z], "kind") ?: "",
                                       j_get(entries->items[z], "verbs")));
            }
            if(subs->nitems) j_set(r, "subresources", subs); else j_free(subs);
            j_push(res, r);
        }
        j_set(ve, "resources", res);
        j_set(ve, "freshness", j_str("Current"));
        j_push(versions, ve);
    }
    j_free(vl);
    if(!versions->nitems){ j_free(versions); return NULL; }
    json *g = j_obj();
    json *md = j_obj();
    /* The core group carries no name at all -- not "", not null: upstream's
     * metadata for it is `{"creationTimestamp": null}` at both v2beta1 and v2. */
    if(*group) j_set(md, "name", j_str(group));
    j_set(md, "creationTimestamp", j_null());
    j_set(g, "metadata", md);
    j_set(g, "versions", versions);
    return g;
}

static json *aggregated_discovery(int core, const char *aggv){
    json *o = j_obj();
    j_set(o, "kind", j_str("APIGroupDiscoveryList"));
    char av[64];
    snprintf(av, sizeof av, "apidiscovery.k8s.io/%s", aggv);
    j_set(o, "apiVersion", j_str(av));
    j_set(o, "metadata", j_obj());
    json *items = j_arr();
    if(core){
        json *g = agg_group("", aggv);
        if(g) j_push(items, g);
    } else {
        json *order = j_get(SURFACE, "groupOrder");
        json *seen = j_obj();
        for(size_t i = 0; j_is(order, J_ARR) && i < order->nitems; i++){
            const char *gn = j_str_of(order->items[i]);
            if(!gn || !*gn || j_get(seen, gn)) continue;
            j_set(seen, gn, j_bool(1));
            json *g = agg_group(gn, aggv);
            if(g) j_push(items, g);
        }
        extern json *crd_all_group_versions(void);
        json *extra = crd_all_group_versions();
        for(size_t i = 0; extra && i < extra->nitems; i++){
            extern json *crd_agg_group(const char *group, const char *aggv);
            json *g = crd_agg_group(j_gets(extra->items[i], "name") ?: "", aggv);
            if(g) j_push(items, g);
        }
        j_free(extra);
        j_free(seen);
    }
    j_set(o, "items", items);
    return o;
}

static json *api_resource_list(const char *gv){
    json *entries = NULL;
    json *m = j_get(SURFACE, "groupVersions");
    if(m) entries = j_get(m, gv);
    if(!entries){ json *o2 = j_get(SURFACE, "optionalGroupVersions"); if(o2) entries = j_get(o2, gv); }
    if(!entries || !gv_enabled(gv)) return NULL;
    json *o = j_obj();
    j_set(o, "kind", j_str("APIResourceList"));
    if(strcmp(gv, "v1")) j_set(o, "apiVersion", j_str("v1"));
    j_set(o, "groupVersion", j_str(gv));
    json *res = j_clone(entries);
    /* CustomResourceDefinitions contribute their own group-versions */
    j_set(o, "resources", res);
    return o;
}

/* ------------------------------- REST core -------------------------------- */
extern int res_is_ephemeral(const char *r);
extern json *review_answer(const char *resource, json *body, const ident *who, const char *ns);
extern void crd_register(json *crd);
extern void crd_unregister(const char *name);
extern resdef *crd_lookup(const char *group, const char *version, const char *resource);
extern int crd_validate(resdef *rd, json *obj, json **causes);
extern void crd_default(resdef *rd, json *obj);
extern json *crd_all_group_versions(void);

static int is_crd_res(resdef *rd){ return rd && rd->optional == 2; }
static const char *err_kind(resdef *rd){
    return is_crd_res(rd) ? rd->resource : rd->kind;
}

static void finish_object(resdef *rd, json *obj, const char *ns, long long rv){
    json *md = meta(obj);
    if(rd->namespaced && ns) j_set(md, "namespace", j_str(ns));
    set_rv(obj, rv);
}

static json *decode_body(hreq *r, int *bad){
    *bad = 0;
    if(!r->blen){ *bad = 1; return NULL; }
    const char *ct0 = hreq_header(r, "Content-Type");
    if(ct0 && strstr(ct0, "application/vnd.kubernetes.protobuf")){
        json *pb = k8s_protobuf_decode(r->body, r->blen);
        if(pb) return pb;
        *bad = 1;
        return NULL;
    }
    json *o = j_parse(r->body, r->blen);
    if(!o || o->t != J_OBJ){
        if(getenv("KCP_DEBUG_BODY")){
            const char *ct = hreq_header(r, "Content-Type");
            fprintf(stderr, "DBG body ct=%s len=%zu first=", ct ?: "(none)", r->blen);
            for(size_t i = 0; i < r->blen && i < 48; i++)
                fprintf(stderr, "%02x", (unsigned char)r->body[i]);
            fprintf(stderr, "\n");
        }
        j_free(o); *bad = 1; return NULL;
    }
    return o;
}

/* Does the object's own apiVersion/kind agree with the endpoint? */
static int gvk_ok(resdef *rd, const char *version, json *obj){
    const char *av = j_gets(obj, "apiVersion");
    const char *kd = j_gets(obj, "kind");
    if(!av || !kd) return 0;
    char want[256];
    if(*rd->group) snprintf(want, sizeof want, "%s/%s", rd->group, version);
    else snprintf(want, sizeof want, "%s", version);
    return !strcmp(av, want) && !strcmp(kd, rd->kind);
}

static long long generation_of(json *o){ return j_geti(j_get(o, "metadata"), "generation", 0); }

/* The kinds whose registry strategy is generation-aware, as upstream defines it. */
static const char *GENERATION_KINDS[] = {
    "CustomResourceDefinition", "Deployment", "EndpointSlice", "FlowSchema", "Job",
    "NetworkPolicy", "PriorityClass", "PriorityLevelConfiguration", "ReplicaSet",
    "StatefulSet", "DaemonSet", "CronJob", "ValidatingWebhookConfiguration",
    "MutatingWebhookConfiguration", "ValidatingAdmissionPolicy",
    "ValidatingAdmissionPolicyBinding", NULL
};


/* 1.26's Deployment storage reports the legacy group in an Invalid status;
   1.27 corrected it to the group the resource is actually served under. */
static const char *invalid_group(resdef *rd){
    if(RELEASE_MINOR == 26 && !strcmp(rd->kind, "Deployment")) return "extensions";
    /* NetworkPolicy named the group it was born in until 1.26 */
    if(RELEASE_MINOR <= 25 && !strcmp(rd->kind, "NetworkPolicy")) return "extensions";
    return rd->group;
}

static int wants_generation(resdef *rd){
    if(rd->optional == 2) return 1;          /* custom resources */
    if(RELEASE_MINOR >= 33 && !strcmp(rd->kind, "Pod")) return 1;
    for(int i = 0; GENERATION_KINDS[i]; i++)
        if(!strcmp(rd->kind, GENERATION_KINDS[i])) return 1;
    return 0;
}

static void strip_ro(json *obj){
    json *md = meta(obj);
    j_del(md, "uid"); j_del(md, "creationTimestamp"); j_del(md, "selfLink");
    j_del(md, "generation"); j_del(md, "managedFields");
    j_del(md, "deletionTimestamp"); j_del(md, "deletionGracePeriodSeconds");
}

static void handle_create(hconn *c, hreq *r, reqinfo *ri, ident *who, const char *version){
    resdef *rd = ri->rd;
    int bad = 0;
    json *obj = decode_body(r, &bad);
    if(bad){
        reply_status(c, 400, "BadRequest", "the object provided is unrecognized", "", NULL, NULL, NULL);
        return;
    }
    /* A binding arrives two ways: as the pod's own subresource, and as the
     * standalone `bindings` resource a scheduler posts to, which names the pod
     * in metadata.name instead of in the path. Both land on the same pod. */
    int free_binding = !ri->sub && !strcmp(rd->resource, "bindings");
    if((ri->sub && !strcmp(ri->sub, "binding")) || free_binding){
        const char *node = j_gets(j_get(obj, "target"), "name");
        /* the standalone resource names the pod in the body, so the name has to
         * outlive the body it came from */
        char *pod = xstrdup2(free_binding ? (j_gets(meta(obj), "name") ?: "") : (ri->name ?: ""));
        char *key = *pod ? obj_key(rd, ri->ns, pod) : NULL;
        sobj *o = key ? store_get(key) : NULL;
        if(!o || !node){
            free(key); j_free(obj);
            reply_status(c, 404, "NotFound", "not found", rd->group, "pods", pod, NULL);
            free(pod);
            return;
        }
        if(j_gets(j_get(o->obj, "spec"), "nodeName")){
            free(key); j_free(obj);
            reply_status(c, 409, "Conflict", "pod is already assigned", rd->group,
                         "pods", pod, NULL);
            free(pod);
            return;
        }
        json *neu = j_clone(o->obj);
        j_set(j_ensure_obj(neu, "spec"), "nodeName", j_str(node));
        json *st = j_ensure_obj(neu, "status");
        json *conds = j_get(st, "conditions");
        if(!j_is(conds, J_ARR)){ conds = j_arr(); j_set(st, "conditions", conds); }
        int have = 0;
        for(size_t i = 0; i < conds->nitems; i++)
            if(!strcmp(j_gets(conds->items[i], "type") ?: "", "PodScheduled")){
                j_set(conds->items[i], "status", j_str("True"));
                j_del(conds->items[i], "reason");
                j_del(conds->items[i], "message");
                have = 1;
            }
        if(!have){
            json *cc = j_obj();
            j_set(cc, "type", j_str("PodScheduled"));
            j_set(cc, "status", j_str("True"));
            char *ts = now_rfc3339();
            j_set(cc, "lastProbeTime", j_null());
            j_set(cc, "lastTransitionTime", j_str(ts));
            free(ts);
            j_push(conds, cc);
        }
        j_del(meta(neu), "resourceVersion");
        long long brv = store_update(key, neu, o->mod);
        free(key);
        j_free(obj);
        if(brv < 0){
            reply_status(c, 500, "InternalError", "storage write failed", rd->group,
                         "pods", pod, NULL);
            free(pod);
            return;
        }
        free(pod);
        reply_obj(c, 201, status_obj(201, NULL, NULL, "", NULL, NULL, NULL));
        return;
    }
    if(res_is_ephemeral(rd->resource)){
        json *ans = review_answer(rd->resource, obj, who, ri->ns);
        j_free(obj);
        if(!ans){ reply_status(c, 400, "BadRequest", "bad review", "", NULL, NULL, NULL); return; }
        set_av(ans, rd->group, version);
        j_set(ans, "kind", j_str(rd->kind));
        if(!j_get(ans, "metadata")) j_set(ans, "metadata", j_obj());
        reply_obj(c, 201, ans);
        return;
    }
    if(!gvk_ok(rd, version, obj)){
        j_free(obj);
        reply_status(c, 400, "BadRequest", "the object provided is unrecognized (must be of kind)",
                     "", NULL, NULL, NULL);
        return;
    }
    if(decode_level_error(rd, obj)){
        j_free(obj);
        reply_status(c, 400, "BadRequest",
                     "the object provided could not be decoded", "", NULL, NULL, NULL);
        return;
    }
    const char *fv = hreq_query(r, "fieldValidation");
    if(fv && !strcmp(fv, "Strict")){
        extern int schema_unknown_fields(resdef *rd, const char *version, json *obj, char **what);
        char *what = NULL;
        if(schema_unknown_fields(rd, version, obj, &what)){
            j_free(obj);
            char *m = xasprintf("strict decoding error: unknown field \"%s\"", what ?: "");
            reply_status(c, 400, "BadRequest", m, "", NULL, NULL, NULL);
            free(m); free(what);
            return;
        }
    } else {
        extern void schema_prune_unknown(resdef *rd, const char *version, json *obj);
        schema_prune_unknown(rd, version, obj);
    }
    json *md = meta(obj);
    const char *bns = j_gets(md, "namespace");
    if(rd->namespaced && bns && ri->ns && strcmp(bns, ri->ns)){
        j_free(obj);
        reply_status(c, 400, "BadRequest", "the namespace of the provided object does not match", "",
                     NULL, NULL, NULL);
        return;
    }
    const char *gname = j_gets(md, "generateName");
    char gen[320];
    if((!j_gets(md, "name") || !*j_gets(md, "name")) && gname && *gname){
        char sfx[8]; rand_suffix(sfx, 5);
        snprintf(gen, sizeof gen, "%s%s", gname, sfx);
        j_set(md, "name", j_str(gen));
    }
    strip_ro(obj);
    const char *name = j_gets(md, "name");
    if(rd->namespaced) j_set(md, "namespace", j_str(ri->ns ?: "default"));
    apply_defaults(rd, obj, version);
    if(is_crd_res(rd)) crd_default(rd, obj);
    json *canon = convert_from(rd, obj, version);
    j_free(obj);
    obj = canon;
    char *namecp = xstrdup2(j_gets(meta(obj), "name") ?: "");
    int code = 0; const char *reason = NULL; char *msg = NULL;
    ADMIT_DETAIL_KIND = NULL; free(ADMIT_DETAIL_NAME); ADMIT_DETAIL_NAME = NULL;
    json *acauses = admit(ri, obj, NULL, who, &code, &reason, &msg);
    if(code){
        j_free(obj);
        const char *dk = ADMIT_DETAIL_KIND ?: rd->resource;
        const char *dn = ADMIT_DETAIL_NAME ?: namecp;
        reply_status(c, code, reason, msg,
                     ADMIT_DETAIL_KIND ? "" : rd->group, dk, dn, acauses);
        ADMIT_DETAIL_KIND = NULL; free(ADMIT_DETAIL_NAME); ADMIT_DETAIL_NAME = NULL;
        free(msg); free(namecp);
        return;
    }
    j_free(acauses);
    json *causes = NULL;
    if(is_crd_res(rd)) crd_validate(rd, obj, &causes);
    else if(!strcmp(rd->kind, "CustomResourceDefinition")){
        extern json *crd_object_validate(json *);
        causes = crd_object_validate(obj);
    }
    else causes = validate_obj(rd, obj, NULL, version);
    if(causes){
        j_free(obj);
        reply_status(c, 422, "Invalid", "object is invalid", invalid_group(rd), rd->kind, namecp, causes);
        free(namecp);
        return;
    }
    free(namecp);
    char *uid = new_uid();
    char *ts = now_rfc3339();
    j_set(meta(obj), "uid", j_str(uid));
    j_set(meta(obj), "creationTimestamp", j_str(ts));
    free(uid); free(ts);
    if(wants_generation(rd)) j_set(meta(obj), "generation", j_int(1));
    prepare_status_defaults(rd, obj);
    if(is_true(hreq_query(r, "dryRun"))){
        finish_object(rd, obj, ri->ns, store_rv());
        json *view = convert_to(rd, obj, version);
        j_free(obj);
        reply_obj(c, 201, view);
        return;
    }
    char *key = obj_key(rd, ri->ns, j_gets(meta(obj), "name"));
    if(store_get(key)){
        free(key);
        char *nm = xstrdup2(j_gets(meta(obj), "name") ?: "");
        j_free(obj);
        char *m = *rd->group ? xasprintf("%s.%s \"%s\" already exists", rd->resource,
                                         rd->group, nm)
                             : xasprintf("%s \"%s\" already exists", rd->resource, nm);
        reply_status(c, 409, "AlreadyExists", m, rd->group, rd->resource, nm, NULL);
        free(m);
        free(nm);
        return;
    }
    j_del(meta(obj), "resourceVersion");
    json *stored = j_clone(obj);
    j_free(obj);
    long long got = store_create(key, stored);
    if(got < 0){
        free(key);
        reply_status(c, 500, "InternalError", "storage write failed", rd->group, rd->kind, NULL, NULL);
        return;
    }
    sobj *so = store_get(key);
    free(key);
    json *view = convert_to(rd, so->obj, version);
    set_rv(view, so->mod);
    if(!strcmp(rd->kind, "CustomResourceDefinition")) crd_register(so->obj);
    reply_obj(c, 201, view);
}

static json *view_of(resdef *rd, sobj *o, const char *version){
    json *v = convert_to(rd, o->obj, version);
    set_rv(v, o->mod);
    return v;
}

static void handle_get(hconn *c, hreq *r, reqinfo *ri, const char *version){
    resdef *rd = ri->rd;
    char *key = obj_key(rd, ri->ns, ri->name);
    sobj *o = store_get(key);
    free(key);
    if(!o){
        char *m = xasprintf("%s \"%s\" not found", rd->resource, ri->name ?: "");
        reply_status(c, 404, "NotFound", m, rd->group, rd->resource, ri->name, NULL);
        free(m);
        return;
    }
    json *v = view_of(rd, o, version);
    if(ri->sub && !strcmp(ri->sub, "scale")){
        json *sc = j_obj();
        j_set(sc, "kind", j_str("Scale"));
        j_set(sc, "apiVersion", j_str("autoscaling/v1"));
        json *md = j_obj();
        j_set(md, "name", j_str(ri->name));
        j_set(md, "namespace", j_str(ri->ns));
        j_set(md, "uid", j_clone(j_get(j_get(v, "metadata"), "uid")));
        j_set(md, "resourceVersion", j_clone(j_get(j_get(v, "metadata"), "resourceVersion")));
        j_set(md, "creationTimestamp", j_clone(j_get(j_get(v, "metadata"), "creationTimestamp")));
        j_set(sc, "metadata", md);
        json *sp = j_obj();
        json *rep = j_path(v, "spec.replicas");
        json *sz = NULL;
        const char *spath = NULL;
        if(is_crd_res(rd)){
            extern const char *crd_scale_spec_path(resdef *);
            extern const char *crd_scale_status_path(resdef *);
            spath = crd_scale_spec_path(rd);
            if(spath) rep = j_path(v, spath + 1);
            const char *stp = crd_scale_status_path(rd);
            if(stp) sz = j_path(v, stp + 1);
        }
        j_set(sp, "replicas", rep ? j_clone(rep) : j_int(0));
        j_set(sc, "spec", sp);
        json *st = j_obj();
        json *sr = sz ? sz : j_path(v, "status.replicas");
        j_set(st, "replicas", sr ? j_clone(sr) : j_int(0));
        json *sel = j_path(v, "spec.selector.matchLabels");
        if(sel && sel->t == J_OBJ && sel->nmem){
            buf b = {0};
            for(size_t i = 0; i < sel->nmem; i++)
                buf_printf(&b, "%s%s=%s", i ? "," : "", sel->mem[i].key,
                           j_str_of(sel->mem[i].val) ?: "");
            j_set(st, "selector", j_str(b.p));
            buf_free(&b);
        }
        j_set(sc, "status", st);
        j_free(v);
        reply_obj(c, 200, sc);
        return;
    }
    reply_obj(c, 200, v);
}

static void handle_list(hconn *c, hreq *r, reqinfo *ri, const char *version){
    resdef *rd = ri->rd;
    int badl = 0, badf = 0;
    json *lt = parse_label_selector(hreq_query(r, "labelSelector"), &badl);
    json *ft = parse_field_selector(hreq_query(r, "fieldSelector"), &badf);
    if(badl || badf){
        j_free(lt); j_free(ft);
        reply_status(c, 400, "BadRequest", "unable to parse requirement", "", NULL, NULL, NULL);
        return;
    }
    const char *lim = hreq_query(r, "limit");
    const char *cont = hreq_query(r, "continue");
    json *list = build_list(rd, version, ri->ns, lt, ft, lim ? strtoll(lim, NULL, 10) : 0,
                            cont, store_rv());
    j_free(lt); j_free(ft);
    if(!list){
        reply_status(c, 400, "BadRequest", "continue token is invalid", "", NULL, NULL, NULL);
        return;
    }
    const char *accept = hreq_header(r, "Accept");
    if(accept && strstr(accept, "as=Table")){
        json *t = table_for(rd->kind, list);
        j_free(list);
        reply_obj(c, 200, t);
        return;
    }
    reply_obj(c, 200, list);
}

static void bump_generation(resdef *rd, json *old, json *neu){
    if(!wants_generation(rd)) return;
    long long g = generation_of(old);
    json *os = j_get(old, "spec"), *ns2 = j_get(neu, "spec");
    if(!j_equal(os, ns2)) g++;
    if(g < 1) g = 1;
    j_set(meta(neu), "generation", j_int(g));
}

static void write_object(hconn *c, hreq *r, reqinfo *ri, ident *who, const char *version,
                         json *neu, sobj *o, int is_status_sub){
    resdef *rd = ri->rd;
    json *old = j_clone(o->obj);
    /* carry over the immutable, server-owned parts */
    json *nmd = meta(neu);
    json *omd = j_get(old, "metadata");
    const char *keep[] = {"uid", "creationTimestamp", "name", "namespace", NULL};
    for(int i = 0; keep[i]; i++){
        json *v = j_get(omd, keep[i]);
        if(v) j_set(nmd, keep[i], j_clone(v));
    }
    {   json *fz = j_get(nmd, "finalizers");
        if(fz && j_is(fz, J_ARR) && !fz->nitems) j_del(nmd, "finalizers");
    }
    if(j_get(omd, "deletionTimestamp")) j_set(nmd, "deletionTimestamp", j_clone(j_get(omd, "deletionTimestamp")));
    if(j_get(omd, "deletionGracePeriodSeconds"))
        j_set(nmd, "deletionGracePeriodSeconds", j_clone(j_get(omd, "deletionGracePeriodSeconds")));
    if(is_status_sub){
        /* a condition written without its timestamps gets them here, the way
           each kind's registry strategy fills them in */
        json *conds = j_path(neu, "status.conditions");
        int pod = !strcmp(rd->kind, "Pod"), node = !strcmp(rd->kind, "Node");
        for(size_t i = 0; j_is(conds, J_ARR) && i < conds->nitems; i++){
            if(pod && !j_get(conds->items[i], "lastProbeTime"))
                j_set(conds->items[i], "lastProbeTime", j_null());
            if(node && !j_get(conds->items[i], "lastHeartbeatTime"))
                j_set(conds->items[i], "lastHeartbeatTime", j_null());
            if(!j_get(conds->items[i], "lastTransitionTime")){
                char *ts = now_rfc3339();
                j_set(conds->items[i], "lastTransitionTime", j_str(ts));
                free(ts);
            }
        }
    }
    if(is_status_sub){
        json *st = j_get(neu, "status");
        /* the QoS class is fixed when the Pod is admitted and survives every
           later status write */
        if(RELEASE_MINOR >= 32 && st && !strcmp(rd->kind, "Pod") && !j_get(st, "qosClass")){
            json *q = j_path(old, "status.qosClass");
            if(q) j_set(st, "qosClass", j_clone(q));
        }
        json *merged = j_clone(old);
        if(st) j_set(merged, "status", j_clone(st));
        else j_del(merged, "status");
        json *mmd = meta(merged);
        json *keepm[] = { j_get(nmd, "labels"), j_get(nmd, "annotations"), j_get(nmd, "finalizers") };
        const char *kn[] = { "labels", "annotations", "finalizers" };
        for(int i = 0; i < 3; i++){
            if(keepm[i]) j_set(mmd, kn[i], j_clone(keepm[i]));
            else j_del(mmd, kn[i]);
        }
        j_free(neu);
        neu = merged;
    } else {
        json *ost = j_get(old, "status");
        if(ost) j_set(neu, "status", j_clone(ost));
        else j_del(neu, "status");
        bump_generation(rd, old, neu);
        if(wants_generation(rd) && !j_get(nmd, "generation") && j_get(omd, "generation"))
            j_set(nmd, "generation", j_clone(j_get(omd, "generation")));
    }
    if(!is_status_sub && wants_generation(rd)){
        /* keep generation when nothing but metadata changed */
    } else if(is_status_sub && j_get(omd, "generation")){
        j_set(meta(neu), "generation", j_clone(j_get(omd, "generation")));
    }
    int code = 0; const char *reason = NULL; char *msg = NULL;
    admit(ri, neu, old, who, &code, &reason, &msg);
    if(code){
        j_free(neu); j_free(old);
        reply_status(c, code, reason, msg, rd->group, rd->kind, ri->name, NULL);
        free(msg);
        return;
    }
    json *causes = NULL;
    if(is_crd_res(rd)) crd_validate(rd, neu, &causes);
    else causes = validate_obj(rd, neu, old, version);
    if(causes){
        j_free(neu); j_free(old);
        reply_status(c, 422, "Invalid", "object is invalid", invalid_group(rd), rd->kind, ri->name, causes);
        return;
    }
    /* a finalized object whose finalizers are all gone disappears */
    json *fin = j_get(meta(neu), "finalizers");
    if(j_gets(meta(neu), "deletionTimestamp") && (!j_is(fin, J_ARR) || !fin->nitems)){
        char *key = obj_key(rd, ri->ns, ri->name);
        store_delete(key);
        free(key);
        json *v = j_clone(neu);
        j_free(neu); j_free(old);
        reply_obj(c, 200, v);
        return;
    }
    if(is_true(hreq_query(r, "dryRun"))){
        json *v = convert_to(rd, neu, version);
        set_rv(v, o->mod);
        j_free(neu); j_free(old);
        reply_obj(c, 200, v);
        return;
    }
    j_del(meta(neu), "resourceVersion");
    char *key = obj_key(rd, ri->ns, ri->name);
    long long got = store_update(key, j_clone(neu), o->mod);
    if(got < 0){
        free(key); j_free(neu); j_free(old);
        reply_status(c, 409, "Conflict", "the object has been modified", rd->group, rd->resource,
                     ri->name, NULL);
        return;
    }
    sobj *so = store_get(key);
    free(key);
    json *v;
    if(SCALE_REPLY){ SCALE_REPLY = 0; v = scale_view(so->obj, ri->ns, ri->name); }
    else v = view_of(rd, so, version);
    if(!strcmp(rd->kind, "CustomResourceDefinition")) crd_register(so->obj);
    j_free(neu); j_free(old);
    reply_obj(c, 200, v);
}

static void handle_update(hconn *c, hreq *r, reqinfo *ri, ident *who, const char *version){
    resdef *rd = ri->rd;
    int bad = 0;
    json *neu = decode_body(r, &bad);
    if(bad){ reply_status(c, 400, "BadRequest", "bad body", "", NULL, NULL, NULL); return; }
    char *key = obj_key(rd, ri->ns, ri->name);
    sobj *o = store_get(key);
    free(key);
    if(!o){
        j_free(neu);
        reply_status(c, 404, "NotFound", "not found", rd->group, rd->resource, ri->name, NULL);
        return;
    }
    if(ri->sub && !strcmp(ri->sub, "scale")){
        SCALE_REPLY = 1;
        long long rep = j_geti(j_get(neu, "spec"), "replicas", 0);
        j_free(neu);
        neu = j_clone(o->obj);
        if(is_crd_res(rd)){
            extern const char *crd_scale_spec_path(resdef *);
            const char *sp = crd_scale_spec_path(rd);
            if(sp){
                char buf2[128]; snprintf(buf2, sizeof buf2, "%s", sp + 1);
                char *dot = strrchr(buf2, '.');
                if(dot){ *dot = 0; json *par = j_path(neu, buf2); if(par) j_set(par, dot + 1, j_int(rep)); }
            }
        } else {
            j_set(j_ensure_obj(neu, "spec"), "replicas", j_int(rep));
        }
        write_object(c, r, ri, who, version, neu, o, 0);
        return;
    }
    const char *bname = j_gets(j_get(neu, "metadata"), "name");
    if(bname && ri->name && strcmp(bname, ri->name)){
        j_free(neu);
        reply_status(c, 400, "BadRequest", "the name of the object does not match the name on the URL",
                     "", NULL, NULL, NULL);
        return;
    }
    if(!ri->sub && !gvk_ok(rd, version, neu)){
        j_free(neu);
        reply_status(c, 400, "BadRequest", "the object provided is unrecognized", "", NULL, NULL, NULL);
        return;
    }
    if(is_crd_res(rd) && (!ri->sub || strcmp(ri->sub, "scale")) &&
       !j_gets(j_get(neu, "metadata"), "resourceVersion")){
        json *cs = j_arr();
        json *cc = j_obj();
        j_set(cc, "reason", j_str("FieldValueInvalid"));
        j_set(cc, "field", j_str("metadata.resourceVersion"));
        j_push(cs, cc);
        j_free(neu);
        reply_status(c, 422, "Invalid", "must be specified for an update",
                     rd->group, rd->resource, ri->name, cs);
        return;
    }
    json *canon = convert_from(rd, neu, version);
    j_free(neu);
    if(ri->sub && !strcmp(ri->sub, "ephemeralcontainers")){
        json *merged = j_clone(o->obj);
        json *ec = j_path(canon, "spec.ephemeralContainers");
        if(ec) j_set(j_ensure_obj(merged, "spec"), "ephemeralContainers", j_clone(ec));
        j_free(canon);
        canon = merged;
        apply_defaults(rd, canon, version);
        write_object(c, r, ri, who, version, canon, o, 0);
        return;
    }
    if(!ri->sub) apply_defaults(rd, canon, version);
    if(ri->sub && !strcmp(ri->sub, "finalize")){
        json *merged = j_clone(o->obj);
        json *fin = j_path(canon, "spec.finalizers");
        json *sp = j_ensure_obj(merged, "spec");
        /* an empty finalizer list is no list at all */
        if(fin && j_is(fin, J_ARR) && fin->nitems) j_set(sp, "finalizers", j_clone(fin));
        else j_del(sp, "finalizers");
        j_free(canon);
        write_object(c, r, ri, who, version, merged, o, 0);
        return;
    }
    write_object(c, r, ri, who, version, canon, o,
                 ri->sub && !strcmp(ri->sub, "status"));
}

static void handle_patch(hconn *c, hreq *r, reqinfo *ri, ident *who, const char *version){
    resdef *rd = ri->rd;
    const char *ct = hreq_header(r, "Content-Type");
    json *p = r->blen ? j_parse(r->body, r->blen) : NULL;
    if(!p){ reply_status(c, 400, "BadRequest", "bad patch", "", NULL, NULL, NULL); return; }
    char *key = obj_key(rd, ri->ns, ri->name);
    sobj *o = store_get(key);
    free(key);
    if(!o){
        j_free(p);
        reply_status(c, 404, "NotFound", "not found", rd->group, rd->resource, ri->name, NULL);
        return;
    }
    json *cur = convert_to(rd, o->obj, version);
    set_rv(cur, o->mod);
    json *neu = NULL;
    if(ct && strstr(ct, "json-patch+json")){
        char *err = NULL;
        neu = apply_json_patch(cur, p, &err);
        if(!neu){
            j_free(cur); j_free(p);
            reply_status(c, 422, "Invalid", err ?: "patch failed", "", NULL, NULL, NULL);
            free(err);
            return;
        }
    } else if(ct && strstr(ct, "strategic-merge-patch+json")){
        neu = apply_strategic_patch(rd, cur, p);
    } else {
        neu = apply_merge_patch(cur, p);
    }
    j_free(p);
    j_free(cur);
    if(ri->sub && !strcmp(ri->sub, "scale")){
        SCALE_REPLY = 1;
        long long rep = j_geti(j_get(neu, "spec"), "replicas", 0);
        j_free(neu);
        neu = j_clone(o->obj);
        j_set(j_ensure_obj(neu, "spec"), "replicas", j_int(rep));
        write_object(c, r, ri, who, version, neu, o, 0);
        return;
    }
    json *canon = convert_from(rd, neu, version);
    j_free(neu);
    if(!ri->sub) apply_defaults(rd, canon, version);
    write_object(c, r, ri, who, version, canon, o,
                 ri->sub && !strcmp(ri->sub, "status"));
}

static int pod_graceful(resdef *rd){ return !strcmp(rd->kind, "Pod"); }

static void handle_delete(hconn *c, hreq *r, reqinfo *ri, ident *who, const char *version){
    resdef *rd = ri->rd;
    char *key = obj_key(rd, ri->ns, ri->name);
    sobj *o = store_get(key);
    if(!o){
        free(key);
        reply_status(c, 404, "NotFound", "not found", rd->group, rd->resource, ri->name, NULL);
        return;
    }
    json *opts = r->blen ? j_parse(r->body, r->blen) : NULL;
    json *pre = opts ? j_get(opts, "preconditions") : NULL;
    if(pre){
        const char *uid = j_gets(pre, "uid");
        const char *cur = j_gets(j_get(o->obj, "metadata"), "uid");
        const char *rv = j_gets(pre, "resourceVersion");
        char cr[32]; snprintf(cr, sizeof cr, "%lld", o->mod);
        if((uid && cur && strcmp(uid, cur)) || (rv && strcmp(rv, cr))){
            free(key); j_free(opts);
            reply_status(c, 409, "Conflict", "precondition failed", rd->group, rd->kind, ri->name, NULL);
            return;
        }
    }
    long long grace = -1;
    const char *gq = hreq_query(r, "gracePeriodSeconds");
    if(gq) grace = strtoll(gq, NULL, 10);
    else if(opts && j_get(opts, "gracePeriodSeconds")) grace = j_geti(opts, "gracePeriodSeconds", -1);
    const char *prop = hreq_query(r, "propagationPolicy");
    if(!prop && opts) prop = j_gets(opts, "propagationPolicy");
    j_free(opts);
    int dry = is_true(hreq_query(r, "dryRun"));
    json *fin = j_get(j_get(o->obj, "metadata"), "finalizers");
    int has_fin = j_is(fin, J_ARR) && fin->nitems;
    if(!strcmp(rd->kind, "Namespace")){
        json *sf = j_path(o->obj, "spec.finalizers");
        if(j_is(sf, J_ARR) && sf->nitems) has_fin = 1;
    }
    if(prop && !strcmp(prop, "Orphan")) has_fin = has_fin || 1;
    json *v = view_of(rd, o, version);
    char *uid_copy = xstrdup2(j_gets(j_get(o->obj, "metadata"), "uid") ?: "");
    if(dry){
        free(key);
        STATUS_OMIT_CODE = 1;
        json *st0 = status_obj(200, NULL, NULL, rd->group, rd->resource, ri->name, NULL);
        json *d0 = j_get(st0, "details");
        if(d0) j_set(d0, "uid", j_str(uid_copy));
        free(uid_copy);
        reply_obj(c, 200, st0);
        j_free(v);
        return;
    }
    if(has_fin || (pod_graceful(rd) && grace != 0)){
        json *neu = j_clone(o->obj);
        json *md = meta(neu);
        if(!j_gets(md, "deletionTimestamp")){
            char *ts = now_rfc3339();
            j_set(md, "deletionTimestamp", j_str(ts));
            free(ts);
        }
        if(!strcmp(rd->kind, "Namespace"))
            j_set(j_ensure_obj(neu, "status"), "phase", j_str("Terminating"));
        long long g = 0;
        if(pod_graceful(rd)){
            g = grace >= 0 ? grace : j_geti(j_get(neu, "spec"), "terminationGracePeriodSeconds", 30);
            j_set(j_ensure_obj(neu, "spec"), "terminationGracePeriodSeconds", j_int(g));
        }
        j_set(md, "deletionGracePeriodSeconds", j_int(g));
        if(prop && !strcmp(prop, "Orphan")){
            json *f = j_get(md, "finalizers");
            if(!j_is(f, J_ARR)){ f = j_arr(); j_set(md, "finalizers", f); }
            int have = 0;
            for(size_t i = 0; i < f->nitems; i++)
                if(!strcmp(j_str_of(f->items[i]) ?: "", "orphan")) have = 1;
            if(!have) j_push(f, j_str("orphan"));
        }
        j_del(md, "resourceVersion");
        store_update(key, j_clone(neu), o->mod);
        sobj *so = store_get(key);
        free(key);
        j_free(v);
        json *out = view_of(rd, so, version);
        j_free(neu);
        free(uid_copy);
        reply_obj(c, 200, out);
        return;
    }
    store_delete(key);
    free(key);
    if(!strcmp(rd->kind, "CustomResourceDefinition")) crd_unregister(ri->name);
    /* upstream returns the deleted object for Pods, a Success Status otherwise */
    if(pod_graceful(rd)){ free(uid_copy); reply_obj(c, 200, v); return; }
    j_free(v);
    STATUS_OMIT_CODE = 1;
    json *st = status_obj(200, NULL, NULL, rd->group, rd->resource, ri->name, NULL);
    json *d = j_get(st, "details");
    if(d) j_set(d, "uid", j_str(uid_copy));
    free(uid_copy);
    reply_obj(c, 200, st);
}

typedef struct { json *out; json *lt, *ft; } delctx;
static int delcoll_cb(sobj *o, void *ud){
    delctx *D = ud;
    if(!labels_match(D->lt, o->obj)) return 1;
    if(!fields_match(D->ft, o->obj)) return 1;
    j_push(D->out, j_str(o->key));
    return 1;
}

static void handle_deletecollection(hconn *c, hreq *r, reqinfo *ri, const char *version){
    resdef *rd = ri->rd;
    int b1 = 0, b2 = 0;
    json *lt = parse_label_selector(hreq_query(r, "labelSelector"), &b1);
    json *ft = parse_field_selector(hreq_query(r, "fieldSelector"), &b2);
    delctx D = { j_arr(), lt, ft };
    char pfx[512];
    if(rd->namespaced) snprintf(pfx, sizeof pfx, "/registry/%s/%s/", rd->prefix, ri->ns);
    else snprintf(pfx, sizeof pfx, "/registry/%s/", rd->prefix);
    store_each(pfx, delcoll_cb, &D);
    json *items = j_arr();
    for(size_t i = 0; i < D.out->nitems; i++){
        const char *k = j_str_of(D.out->items[i]);
        sobj *o = store_get(k);
        if(!o) continue;
        json *v = view_of(rd, o, version);
        j_del(v, "apiVersion");
        j_del(v, "kind");
        j_push(items, v);
        store_delete(k);
    }
    j_free(D.out); j_free(lt); j_free(ft);
    json *list = j_obj();
    char *lk = xasprintf("%sList", rd->kind);
    j_set(list, "kind", j_str(lk)); free(lk);
    set_av(list, rd->group, version);
    json *md = j_obj();
    char b[32]; snprintf(b, sizeof b, "%lld", store_rv());
    j_set(md, "resourceVersion", j_str(b));
    j_set(list, "metadata", md);
    j_set(list, "items", items);
    reply_obj(c, 200, list);
}

/* --------------------------------- watch ---------------------------------- */
static void handle_watch(hconn *c, hreq *r, reqinfo *ri, const char *version){
    resdef *rd = ri->rd;
    int b1 = 0, b2 = 0;
    json *lt = parse_label_selector(hreq_query(r, "labelSelector"), &b1);
    json *ft = parse_field_selector(hreq_query(r, "fieldSelector"), &b2);
    if(b1 || b2){
        j_free(lt); j_free(ft);
        reply_status(c, 400, "BadRequest", "unable to parse requirement", "", NULL, NULL, NULL);
        return;
    }
    const char *rvq = hreq_query(r, "resourceVersion");
    long long from = store_rv();
    int send_initial = 0;
    if(rvq && *rvq){
        char *end;
        long long v = strtoll(rvq, &end, 10);
        if(*end){
            j_free(lt); j_free(ft);
            reply_obj(c, 500, status_obj(500, NULL, "resourceVersion is not a number",
                                         "", NULL, NULL, NULL));
            return;
        }
        if(v == 0){ send_initial = 1; from = store_rv(); }
        else if(RELEASE_MINOR >= 33 && v < store_rv()){
            /* An explicit resourceVersion behind the server's current one.
             * Through 1.32 the server gives up and replays the current set as
             * ADDED; from 1.33 it refuses, delivering the 410 Expired as a
             * single ERROR event and ending the stream.
             *
             * Upstream decides this against the watch cache's starting point.
             * This store never compacts -- RING is 8192, `store_load` sets
             * `ring_first_rv` to the rv it started at (0 on a fresh etcd), and
             * a plan makes a few hundred writes -- so `watch_history_has` can
             * never answer "too old" and cannot serve as this gate. Testing
             * "behind the current rv" instead is an approximation of
             * upstream's window, and it is the only shape the
             * corpus exercises: of the 36 watch ops across all eighteen plans
             * that name a resourceVersion, the 18 non-zero ones are all
             * S8.watch-from-old-rv at rv=1. A resume from the current rv, and
             * rv=0, are both left alone. */
            j_free(lt); j_free(ft);
            h_stream_begin(c, "application/json");
            json *ev = j_obj();
            j_set(ev, "type", j_str("ERROR"));
            j_set(ev, "object", status_obj(410, "Expired",
                    "too old resource version", "", NULL, NULL, NULL));
            char *t = j_dump(ev);
            buf line = {0}; buf_str(&line, t); buf_str(&line, "\n");
            h_stream_write(c, line.p, line.n);
            buf_free(&line); free(t); j_free(ev);
            h_stream_end(c);
            return;
        }
        else if(!watch_history_has(v)){
            send_initial = 1; from = store_rv();
        }
        else from = v;
    }
    long long timeout = 300;
    const char *tq = hreq_query(r, "timeoutSeconds");
    if(tq) timeout = strtoll(tq, NULL, 10);
    int bookmarks = is_true(hreq_query(r, "allowWatchBookmarks"));
    char pfx[512];
    if(rd->namespaced && ri->ns && *ri->ns) snprintf(pfx, sizeof pfx, "/registry/%s/%s/", rd->prefix, ri->ns);
    else snprintf(pfx, sizeof pfx, "/registry/%s/", rd->prefix);
    size_t plen = strlen(pfx);
    h_stream_begin(c, "application/json");
    if(send_initial){
        listctx L = {0};
        L.items = j_arr(); L.lterms = lt; L.fterms = ft;
        store_each(pfx, list_cb, &L);
        qsort(L.items->items, L.items->nitems, sizeof(json*), cmp_items);
        for(size_t i = 0; i < L.items->nitems; i++){
            json *ev = j_obj();
            j_set(ev, "type", j_str("ADDED"));
            json *ob = convert_to(rd, L.items->items[i], version);
            j_set(ev, "object", ob);
            char *t = j_dump(ev);
            buf line = {0}; buf_str(&line, t); buf_str(&line, "\n");
            h_stream_write(c, line.p, line.n);
            buf_free(&line); free(t); j_free(ev);
        }
        j_free(L.items);
    }
    long long deadline = now_unix() + timeout;
    /* upstream's cacher emits a bookmark roughly once a minute */
    long long next_bookmark = now_unix() + 60;
    watcher *w = watch_start(from);
    while(now_unix() < deadline && h_conn_alive(c)){
        sevent *e = watch_next(w, 1000);
        if(!e){
            if(bookmarks && now_unix() >= next_bookmark){
                next_bookmark = now_unix() + 60;
                json *ev = j_obj();
                j_set(ev, "type", j_str("BOOKMARK"));
                json *ob = j_obj();
                set_av(ob, rd->group, version);
                j_set(ob, "kind", j_str(rd->kind));
                json *md = j_obj();
                char b[32]; snprintf(b, sizeof b, "%lld", store_rv());
                j_set(md, "resourceVersion", j_str(b));
                j_set(md, "creationTimestamp", j_null());
                j_set(ob, "metadata", md);
                j_set(ev, "object", ob);
                char *t = j_dump(ev);
                store_unlock();
                buf line = {0}; buf_str(&line, t); buf_str(&line, "\n");
                h_stream_write(c, line.p, line.n);
                buf_free(&line); free(t); j_free(ev);
                store_lock();
            }
            continue;
        }
        if(strncmp(e->key, pfx, plen)) continue;
        if(!labels_match(lt, e->obj) || !fields_match(ft, e->obj)) continue;
        json *ev = j_obj();
        j_set(ev, "type", j_str(e->type));
        json *ob = convert_to(rd, e->obj, version);
        set_rv(ob, e->rv);
        j_set(ev, "object", ob);
        char *t = j_dump(ev);
        store_unlock();
        buf line = {0}; buf_str(&line, t); buf_str(&line, "\n");
        h_stream_write(c, line.p, line.n);
        buf_free(&line); free(t); j_free(ev);
        store_lock();
    }
    watch_stop(w);
    h_stream_end(c);
    j_free(lt); j_free(ft);
}

/* -------------------------------- routing --------------------------------- */
static int authorize_locked(const ident *who, const reqinfo *ri, const char *nonres_path){
    store_lock();
    int ok = authorize(who, ri, nonres_path);
    store_unlock();
    return ok;
}

static void nonres(hconn *c, hreq *r, ident *who, const char *path, json *body, int code){
    reqinfo ri = {0};
    ri.verb = (char*)(strcmp(r->method, "GET") ? "post" : "get");
    if(!authorize_locked(who, &ri, path)){
        j_free(body);
        reply_status(c, 403, "Forbidden", "forbidden", "", NULL, NULL, NULL);
        return;
    }
    reply_obj(c, code, body);
}

static void handle(hconn *c, hreq *r, void *ud){
    ident who = {0};
    if(!authenticate(r, &who)){
        reply_obj(c, 401, status_obj(401, "Unauthorized", "Unauthorized", "", NULL, NULL, NULL));
        j_free(who.groups); free(who.name);
        return;
    }
    {
        const char *imp = hreq_header(r, "Impersonate-User");
        if(imp && *imp){
            free(who.name);
            who.name = xstrdup2(imp);
            j_free(who.groups);
            who.groups = j_arr();
            for(int i = 0; i < r->nhdr; i++)
                if(!strcasecmp(r->hdr[i].name, "Impersonate-Group"))
                    j_push(who.groups, j_str(r->hdr[i].value));
            j_push(who.groups, j_str("system:authenticated"));
        }
    }
    const char *path = r->path;
    if(!strcmp(path, "/healthz") || !strcmp(path, "/livez") || !strcmp(path, "/readyz")){
        reqinfo ri = {0}; ri.verb = (char*)"get";
        if(!authorize_locked(&who, &ri, path)){ reply_status(c, 403, "Forbidden", "forbidden", "", NULL, NULL, NULL); goto done; }
        h_reply(c, 200, "text/plain; charset=utf-8", "ok", 2);
        goto done;
    }
    if(!strcmp(path, "/version")){
        json *v = j_obj();
        j_set(v, "major", j_str("1"));
        char mn[8]; snprintf(mn, sizeof mn, "%d", RELEASE_MINOR);
        j_set(v, "minor", j_str(mn));
        char gv[32]; snprintf(gv, sizeof gv, "v1.%d.0", RELEASE_MINOR);
        j_set(v, "gitVersion", j_str(gv));
        j_set(v, "gitCommit", j_str(""));
        j_set(v, "gitTreeState", j_str(""));
        j_set(v, "buildDate", j_str("1970-01-01T00:00:00Z"));
        j_set(v, "goVersion", j_str(""));
        j_set(v, "compiler", j_str("gc"));
        j_set(v, "platform", j_str("linux/amd64"));
        nonres(c, r, &who, path, v, 200);
        goto done;
    }
    if(str_has_prefix(path, "/openapi/")){
        reqinfo ri = {0}; ri.verb = (char*)"get";
        if(!authorize_locked(&who, &ri, path)){ reply_status(c, 403, "Forbidden", "forbidden", "", NULL, NULL, NULL); goto done; }
        const char *blob = NULL;
        if(!strcmp(path, "/openapi/v2")) blob = embedded_openapi_v2();
        else if(!strcmp(path, "/openapi/v3")) blob = embedded_openapi_v3_root();
        else blob = embedded_openapi_v3(path + strlen("/openapi/v3"));
        if(!blob){ reply_status(c, 404, "NotFound", "not found", "", NULL, NULL, NULL); goto done; }
        h_reply(c, 200, "application/json", blob, strlen(blob));
        goto done;
    }
    {
        const char *aggv = agg_version_wanted(hreq_header(r, "Accept"));
        if(AGG_UNACCEPTABLE && (!strcmp(path, "/api") || !strcmp(path, "/apis"))){
            STATUS_OMIT_CODE = 0;
            reply_status(c, 406, "NotAcceptable",
                         "only the following media types are accepted: application/json, "
                         "application/yaml, application/vnd.kubernetes.protobuf",
                         "", NULL, NULL, NULL);
            goto done;
        }
        if(aggv && (!strcmp(path, "/api") || !strcmp(path, "/apis"))){
            store_lock();
            json *ad = aggregated_discovery(!strcmp(path, "/api"), aggv);
            store_unlock();
            reqinfo ri0 = {0}; ri0.verb = (char*)"get";
            if(!authorize_locked(&who, &ri0, path)){
                j_free(ad);
                reply_status(c, 403, "Forbidden", "forbidden", "", NULL, NULL, NULL);
                goto done;
            }
            char *t = j_dump(ad);
            j_free(ad);
            char ct[128];
            snprintf(ct, sizeof ct,
                     "application/json;g=apidiscovery.k8s.io;v=%s;as=APIGroupDiscoveryList", aggv);
            h_reply(c, 200, ct, t, strlen(t));
            free(t);
            goto done;
        }
    }
    if(!strcmp(path, "/api")){ nonres(c, r, &who, path, api_versions(), 200); goto done; }
    if(!strcmp(path, "/apis")){
        store_lock();
        json *gl = api_group_list();
        json *extra = crd_all_group_versions();
        store_unlock();
        json *groups = j_get(gl, "groups");
        for(size_t i = 0; extra && i < extra->nitems; i++) j_push(groups, j_clone(extra->items[i]));
        j_free(extra);
        nonres(c, r, &who, path, gl, 200);
        goto done;
    }
    char parts[12][256];
    int np = split_path(path, parts, 12);
    int core = np && !strcmp(parts[0], "api");
    int grouped = np && !strcmp(parts[0], "apis");
    if(!core && !grouped){
        reply_status(c, 404, "NotFound", "the server could not find the requested resource", "", NULL, NULL, NULL);
        goto done;
    }
    char group[192] = "", version[64] = "";
    int idx;
    if(core){
        if(np < 2){ reply_status(c, 404, "NotFound", "not found", "", NULL, NULL, NULL); goto done; }
        snprintf(version, sizeof version, "%s", parts[1]);
        idx = 2;
    } else {
        if(np < 3){
            if(np == 2){
                store_lock();
                json *g = api_group(parts[1]);
                if(!g){
                    json *e2 = crd_all_group_versions();
                    for(size_t i = 0; e2 && i < e2->nitems; i++)
                        if(!strcmp(j_gets(e2->items[i], "name") ?: "", parts[1])) g = j_clone(e2->items[i]);
                    j_free(e2);
                }
                store_unlock();
                if(!g){
                    h_reply(c, 404, "text/plain; charset=utf-8", "404 page not found\n", 19);
                    goto done;
                }
                j_set(g, "kind", j_str("APIGroup"));
                j_set(g, "apiVersion", j_str("v1"));
                nonres(c, r, &who, path, g, 200);
                goto done;
            }
            reply_status(c, 404, "NotFound", "not found", "", NULL, NULL, NULL); goto done;
        }
        if(!group_is_served(parts[1])){
            h_reply(c, 404, "text/plain; charset=utf-8", "404 page not found\n", 19);
            goto done;
        }
        snprintf(group, sizeof group, "%s", parts[1]);
        snprintf(version, sizeof version, "%s", parts[2]);
        idx = 3;
    }
    char gv[256];
    if(*group) snprintf(gv, sizeof gv, "%s/%s", group, version);
    else snprintf(gv, sizeof gv, "%s", version);
    if(np == idx){
        extern json *crd_resource_list(const char *gv);
        store_lock();
        json *l = api_resource_list(gv);
        if(!l) l = crd_resource_list(gv);
        store_unlock();
        if(!l){ reply_status(c, 404, "NotFound", "the server could not find the requested resource", "", NULL, NULL, NULL); goto done; }
        nonres(c, r, &who, path, l, 200);
        goto done;
    }
    reqinfo ri = {0};
    ri.group = group; ri.version = version;
    int i = idx;
    if(!strcmp(parts[i], "namespaces") && np > i + 2){
        resdef *nsrd = res_lookup(group, version, "namespaces");
        if(nsrd && np == i + 3 && has_sub(nsrd, parts[i+2])){
            ri.resource = parts[i]; ri.name = parts[i+1]; ri.sub = parts[i+2];
        } else {
            ri.ns = parts[i+1];
            ri.resource = parts[i+2];
            if(np > i + 3) ri.name = parts[i+3];
            if(np > i + 4) ri.sub = parts[i+4];
        }
    } else {
        ri.resource = parts[i];
        if(np > i + 1) ri.name = parts[i+1];
        if(np > i + 2) ri.sub = parts[i+2];
    }
    store_lock();
    resdef *rd = res_lookup(group, version, ri.resource);
    if(rd && !gv_enabled(gv)) rd = NULL;
    if(!rd) rd = crd_lookup(group, version, ri.resource);
    if(!rd || (ri.sub && !has_sub(rd, ri.sub))){
        store_unlock();
        reply_status(c, 404, "NotFound", "the server could not find the requested resource", "", NULL, NULL, NULL);
        goto done;
    }
    ri.rd = rd;
    VALIDATE_SUB = ri.sub ?: "";
    int watch = is_true(hreq_query(r, "watch"));
    ri.verb = (char*)verb_for(r->method, ri.name != NULL, watch);
    if(!authorize(&who, &ri, NULL)){
        json *st = status_obj(403, "Forbidden", "forbidden", rd->group, rd->resource, ri.name, NULL);
        store_unlock();
        reply_obj(c, 403, st);
        goto done;
    }
    /* Discovery says which verbs a resource serves; one it does not serve is a
     * method the endpoint does not allow, which is how `bindings` -- create and
     * nothing else -- answers a list. */
    if(!ri.sub && !serves_verb(rd->verbs, ri.verb)){
        store_unlock();
        reply_status(c, 405, "MethodNotAllowed", "method not allowed", "", NULL, NULL, NULL);
        goto done;
    }
    if(watch && !strcmp(r->method, "GET")) handle_watch(c, r, &ri, version);
    else if(!strcmp(r->method, "GET") && ri.name) handle_get(c, r, &ri, version);
    else if(!strcmp(r->method, "GET")) handle_list(c, r, &ri, version);
    else if(!strcmp(r->method, "POST")) handle_create(c, r, &ri, &who, version);
    else if(!strcmp(r->method, "PUT")) handle_update(c, r, &ri, &who, version);
    else if(!strcmp(r->method, "PATCH")) handle_patch(c, r, &ri, &who, version);
    else if(!strcmp(r->method, "DELETE") && ri.name) handle_delete(c, r, &ri, &who, version);
    else if(!strcmp(r->method, "DELETE")) handle_deletecollection(c, r, &ri, version);
    else reply_status(c, 405, "MethodNotAllowed", "method not allowed", "", NULL, NULL, NULL);
    store_unlock();
done:
    j_free(who.groups); free(who.name);
}

/* --------------------------------- main ----------------------------------- */
static const char *argval(int argc, char **argv, const char *flag, const char *dflt){
    size_t n = strlen(flag);
    for(int i = 1; i < argc; i++){
        if(!strncmp(argv[i], flag, n) && argv[i][n] == '=') return argv[i] + n + 1;
        if(!strcmp(argv[i], flag) && i + 1 < argc) return argv[i+1];
    }
    return dflt;
}

#include <execinfo.h>
static void crash_handler(int sig){
    void *bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "\n*** fatal signal %d ***\n", sig);
    backtrace_symbols_fd(bt, n, 2);
    _exit(128 + sig);
}

int main(int argc, char **argv){
    signal(SIGPIPE, SIG_IGN);
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS, crash_handler);
    CFG.bind_address = (char*)argval(argc, argv, "--bind-address", "0.0.0.0");
    CFG.secure_port = atoi(argval(argc, argv, "--secure-port", "6443"));
    CFG.cert = (char*)argval(argc, argv, "--tls-cert-file", NULL);
    CFG.key = (char*)argval(argc, argv, "--tls-private-key-file", NULL);
    CFG.client_ca = (char*)argval(argc, argv, "--client-ca-file", NULL);
    CFG.token_file = (char*)argval(argc, argv, "--token-auth-file", NULL);
    CFG.etcd_servers = (char*)argval(argc, argv, "--etcd-servers", "http://127.0.0.1:2379");
    CFG.authorization_mode = (char*)argval(argc, argv, "--authorization-mode", "RBAC");
    CFG.storage_media_type = (char*)argval(argc, argv, "--storage-media-type", "application/json");
    CFG.runtime_config = (char*)argval(argc, argv, "--runtime-config", "");
    CFG.feature_gates = (char*)argval(argc, argv, "--feature-gates", "");
    CFG.authn_config = (char*)argval(argc, argv, "--authentication-config", NULL);
    CFG.sa_issuer = (char*)argval(argc, argv, "--service-account-issuer", "");
    schema_init();
    if(CFG.token_file) token_file_load(CFG.token_file);
    if(etcd_init(&ETCD, CFG.etcd_servers) < 0){ fprintf(stderr, "bad --etcd-servers\n"); return 1; }
    store_init(&ETCD);
    for(int i = 0; i < 240; i++){
        if(etcd_revision(&ETCD) > 0) break;
        usleep(500000);
    }
    store_lock();
    store_load();
    bootstrap_rbac();
    /* re-register every CustomResourceDefinition that survived a restart */
    extern void crd_load_all(void);
    crd_load_all();
    store_unlock();
    fprintf(stderr, "kube-apiserver listening on %s:%d (release 1.%d)\n",
            CFG.bind_address, CFG.secure_port, RELEASE_MINOR);
    return http_serve(CFG.bind_address, CFG.secure_port, CFG.cert, CFG.key, handle, NULL);
}
