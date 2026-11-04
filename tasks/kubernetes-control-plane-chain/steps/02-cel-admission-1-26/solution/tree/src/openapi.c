#define _GNU_SOURCE
#include "api.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* The OpenAPI v3 documents are derived from the v2 document the release
 * publishes: same schemas, the v3 layout. */
static json *V2;
static json *V3ROOT;
static json *V3DOCS;      /* path -> serialized document */
static pthread_mutex_t omu = PTHREAD_MUTEX_INITIALIZER;

static void ensure_v2(void){
    if(!V2) V2 = j_parse0(embedded_openapi_v2());
}

static const char *gv_of(const char *p){
    static char buf[256];
    if(str_has_prefix(p, "/api/")){ snprintf(buf, sizeof buf, "%s", p + 5); }
    else if(str_has_prefix(p, "/apis/")) snprintf(buf, sizeof buf, "%s", p + 6);
    else return NULL;
    char *slash = buf;
    int seg = str_has_prefix(p, "/apis/") ? 2 : 1;
    int count = 0;
    for(char *q = buf; *q; q++){
        if(*q == '/'){ count++; if(count == seg){ *q = 0; break; } }
    }
    (void)slash;
    return buf;
}


/* The groups this release's published OpenAPI names, which is exactly the set
   the server registers a web service for. */
int upstream_serves_group(const char *g){
    static json *GROUPS;
    pthread_mutex_lock(&omu);
    if(!GROUPS){
        GROUPS = j_obj();
        json *l = j_parse0(embedded_served_groups());
        for(size_t i = 0; j_is(l, J_ARR) && i < l->nitems; i++){
            const char *g = j_str_of(l->items[i]);
            if(g && !j_get(GROUPS, g)) j_set(GROUPS, g, j_bool(1));
        }
        j_free(l);
    }
    int r = j_get(GROUPS, g) != NULL;
    pthread_mutex_unlock(&omu);
    return r;
}

const char *embedded_openapi_v3_root(void){
    pthread_mutex_lock(&omu);
    if(!V3ROOT){
        V3ROOT = j_obj();
        json *paths = j_obj();
        const char *buckets[] = {"groupVersions", "optionalGroupVersions", NULL};
        json *seen = j_obj();
        for(int b = 0; buckets[b]; b++){
            json *m = j_get(SURFACE, buckets[b]);
            for(size_t i = 0; m && i < m->nmem; i++){
                const char *gv = m->mem[i].key;
                if(!gv_enabled(gv)) continue;
                char key[256];
                if(!strcmp(gv, "v1")) snprintf(key, sizeof key, "api/v1");
                else snprintf(key, sizeof key, "apis/%s", gv);
                json *e = j_obj();
                char url[512];
                snprintf(url, sizeof url, "/openapi/v3/%s?hash=0", key);
                j_set(e, "serverRelativeURL", j_str(url));
                j_set(paths, key, e);
                if(strcmp(gv, "v1")){
                    char g[192];
                    const char *slash = strchr(gv, '/');
                    size_t n = (size_t)(slash - gv);
                    memcpy(g, gv, n); g[n] = 0;
                    char gk[256]; snprintf(gk, sizeof gk, "apis/%s", g);
                    if(!j_get(seen, gk)){
                        json *ge = j_obj();
                        char gu[512];
                        snprintf(gu, sizeof gu, "/openapi/v3/%s?hash=0", gk);
                        j_set(ge, "serverRelativeURL", j_str(gu));
                        j_set(paths, gk, ge);
                        j_set(seen, gk, j_bool(1));
                    }
                }
            }
        }
        for(const char **p = (const char*[]){"api", "apis", NULL}; *p; p++){
            json *e = j_obj();
            char u[256]; snprintf(u, sizeof u, "/openapi/v3/%s?hash=0", *p);
            j_set(e, "serverRelativeURL", j_str(u));
            j_set(paths, *p, e);
        }
        j_free(seen);
        j_set(V3ROOT, "paths", paths);
    }
    static char *text;
    if(!text) text = j_dump(V3ROOT);
    pthread_mutex_unlock(&omu);
    return text;
}

/* pick the v2 definitions whose group-version-kind belongs to `gv` plus deps */
static void collect_refs(json *node, json *defs, json *out);

static void add_def(const char *name, json *defs, json *out){
    if(j_get(out, name)) return;
    json *d = j_get(defs, name);
    if(!d) return;
    j_set(out, name, j_clone(d));
    collect_refs(d, defs, out);
}
static void collect_refs(json *node, json *defs, json *out){
    if(!node) return;
    if(node->t == J_OBJ){
        for(size_t i = 0; i < node->nmem; i++){
            if(!strcmp(node->mem[i].key, "$ref")){
                const char *r = j_str_of(node->mem[i].val);
                if(r && str_has_prefix(r, "#/definitions/")) add_def(r + 14, defs, out);
            } else collect_refs(node->mem[i].val, defs, out);
        }
    } else if(node->t == J_ARR){
        for(size_t i = 0; i < node->nitems; i++) collect_refs(node->items[i], defs, out);
    }
}

static void rewrite_refs(json *node){
    if(!node) return;
    if(node->t == J_OBJ){
        for(size_t i = 0; i < node->nmem; i++){
            if(!strcmp(node->mem[i].key, "$ref")){
                const char *r = j_str_of(node->mem[i].val);
                if(r && str_has_prefix(r, "#/definitions/")){
                    char *n = xasprintf("#/components/schemas/%s", r + 14);
                    j_set(node, "$ref", j_str(n));
                    free(n);
                }
            } else rewrite_refs(node->mem[i].val);
        }
    } else if(node->t == J_ARR){
        for(size_t i = 0; i < node->nitems; i++) rewrite_refs(node->items[i]);
    }
}

const char *embedded_openapi_v3(const char *path){
    if(!path || !*path) return NULL;
    pthread_mutex_lock(&omu);
    ensure_v2();
    if(!V3DOCS) V3DOCS = j_obj();
    json *cached = j_get(V3DOCS, path);
    if(cached){ const char *t = j_str_of(cached); pthread_mutex_unlock(&omu); return t; }
    const char *gv = gv_of(path);
    json *defs = j_get(V2, "definitions");
    json *schemas = j_obj();
    if(gv && defs){
        char group[192] = "", version[64] = "";
        const char *slash = strchr(gv, '/');
        if(slash){
            size_t n = (size_t)(slash - gv);
            memcpy(group, gv, n); group[n] = 0;
            snprintf(version, sizeof version, "%s", slash + 1);
        } else snprintf(version, sizeof version, "%s", gv);
        for(size_t i = 0; i < defs->nmem; i++){
            json *gvk = j_get(defs->mem[i].val, "x-kubernetes-group-version-kind");
            for(size_t k = 0; j_is(gvk, J_ARR) && k < gvk->nitems; k++){
                json *e = gvk->items[k];
                if(!strcmp(j_gets(e, "group") ?: "", group) &&
                   !strcmp(j_gets(e, "version") ?: "", version))
                    add_def(defs->mem[i].key, defs, schemas);
            }
        }
    }
    rewrite_refs(schemas);
    json *doc = j_obj();
    j_set(doc, "openapi", j_str("3.0.0"));
    json *info = j_obj();
    j_set(info, "title", j_str("Kubernetes"));
    char v[32]; snprintf(v, sizeof v, "v1.%d.0", RELEASE_MINOR);
    j_set(info, "version", j_str(v));
    j_set(doc, "info", info);
    json *comp = j_obj();
    j_set(comp, "schemas", schemas);
    j_set(doc, "components", comp);
    j_set(doc, "paths", j_obj());
    char *text = j_dump(doc);
    j_free(doc);
    j_set(V3DOCS, path, j_str(text));
    free(text);
    const char *t = j_str_of(j_get(V3DOCS, path));
    pthread_mutex_unlock(&omu);
    return t;
}


/* ---- strict field validation ------------------------------------------------
 * Walk the object against the release's own OpenAPI schema for its kind and
 * report the first field the schema does not declare. */
static json *schema_for_gvk(const char *group, const char *version, const char *kind){
    pthread_mutex_lock(&omu);
    ensure_v2();
    json *defs = j_get(V2, "definitions");
    json *found = NULL;
    for(size_t i = 0; defs && i < defs->nmem; i++){
        json *gvk = j_get(defs->mem[i].val, "x-kubernetes-group-version-kind");
        for(size_t k = 0; j_is(gvk, J_ARR) && k < gvk->nitems; k++){
            json *e = gvk->items[k];
            if(!strcmp(j_gets(e, "group") ?: "", group) &&
               !strcmp(j_gets(e, "version") ?: "", version) &&
               !strcmp(j_gets(e, "kind") ?: "", kind)) found = defs->mem[i].val;
        }
    }
    pthread_mutex_unlock(&omu);
    return found;
}

static json *deref(json *sch){
    if(!sch) return NULL;
    const char *r = j_gets(sch, "$ref");
    if(!r || !str_has_prefix(r, "#/definitions/")) return sch;
    json *defs = j_get(V2, "definitions");
    return j_get(defs, r + 14);
}

static int walk_strict(json *obj, json *sch, const char *path, char **what){
    sch = deref(sch);
    if(!sch || !obj) return 0;
    if(obj->t == J_OBJ){
        json *props = j_get(sch, "properties");
        if(!props) return 0;
        for(size_t i = 0; i < obj->nmem; i++){
            json *ps = j_get(props, obj->mem[i].key);
            if(!ps){
                if(j_get(sch, "additionalProperties")) continue;
                *what = xasprintf("%s%s%s", path, *path ? "." : "", obj->mem[i].key);
                return 1;
            }
            char sub[256];
            snprintf(sub, sizeof sub, "%s%s%s", path, *path ? "." : "", obj->mem[i].key);
            if(walk_strict(obj->mem[i].val, ps, sub, what)) return 1;
        }
    } else if(obj->t == J_ARR){
        json *items = j_get(sch, "items");
        for(size_t i = 0; i < obj->nitems; i++){
            char sub[256];
            snprintf(sub, sizeof sub, "%s[%zu]", path, i);
            if(walk_strict(obj->items[i], items, sub, what)) return 1;
        }
    }
    return 0;
}

int schema_unknown_fields(resdef *rd, const char *version, json *obj, char **what){
    if(rd->optional == 2) return 0;            /* custom resources prune instead */
    json *sch = schema_for_gvk(rd->group, version, rd->kind);
    if(!sch) return 0;
    pthread_mutex_lock(&omu);
    int bad = walk_strict(obj, sch, "", what);
    pthread_mutex_unlock(&omu);
    return bad;
}

/* The default decoder drops what the schema does not describe, rather than
   refusing it; only fieldValidation=Strict turns that into an error. */
static void walk_prune(json *obj, json *sch){
    sch = deref(sch);
    if(!sch || !obj) return;
    if(obj->t == J_OBJ){
        json *props = j_get(sch, "properties");
        if(!props) return;
        for(size_t i = 0; i < obj->nmem; ){
            json *ps = j_get(props, obj->mem[i].key);
            if(!ps && !j_get(sch, "additionalProperties")){
                j_del(obj, obj->mem[i].key);
                continue;                      /* the member list shifted down */
            }
            if(ps) walk_prune(obj->mem[i].val, ps);
            i++;
        }
    } else if(obj->t == J_ARR){
        json *items = j_get(sch, "items");
        for(size_t i = 0; i < obj->nitems; i++) walk_prune(obj->items[i], items);
    }
}

void schema_prune_unknown(resdef *rd, const char *version, json *obj){
    if(rd->optional == 2) return;              /* custom resources prune by schema */
    json *sch = schema_for_gvk(rd->group, version, rd->kind);
    if(!sch) return;
    pthread_mutex_lock(&omu);
    walk_prune(obj, sch);
    pthread_mutex_unlock(&omu);
}
