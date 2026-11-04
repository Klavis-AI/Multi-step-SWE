#define _GNU_SOURCE
#include "api.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- RFC 6901 pointers ---------- */
static char *unescape_token(const char *t, size_t n){
    char *o = malloc(n+1); size_t k = 0;
    for(size_t i = 0; i < n; i++){
        if(t[i] == '~' && i+1 < n){
            if(t[i+1] == '0'){ o[k++] = '~'; i++; continue; }
            if(t[i+1] == '1'){ o[k++] = '/'; i++; continue; }
        }
        o[k++] = t[i];
    }
    o[k] = 0; return o;
}

/* Walk to the parent container of `path`; *last receives the final token. */
static json *walk_parent(json *root, const char *path, char **last){
    if(path[0] != '/') return NULL;
    json *cur = root;
    const char *p = path + 1;
    for(;;){
        const char *slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        char *tok = unescape_token(p, n);
        if(!slash){ *last = tok; return cur; }
        if(cur->t == J_OBJ){
            json *nx = j_get(cur, tok);
            free(tok);
            if(!nx) return NULL;
            cur = nx;
        } else if(cur->t == J_ARR){
            char *end; long idx = strtol(tok, &end, 10);
            free(tok);
            if(*end) return NULL;
            cur = j_at(cur, (size_t)idx);
            if(!cur) return NULL;
        } else { free(tok); return NULL; }
        p = slash + 1;
    }
}

static int ptr_get(json *root, const char *path, json **out){
    if(!*path){ *out = root; return 1; }
    char *last = NULL;
    json *par = walk_parent(root, path, &last);
    if(!par){ free(last); return 0; }
    int ok = 0;
    if(par->t == J_OBJ){ json *v = j_get(par, last); if(v){ *out = v; ok = 1; } }
    else if(par->t == J_ARR){
        char *end; long idx = strtol(last, &end, 10);
        if(!*end){ json *v = j_at(par, (size_t)idx); if(v){ *out = v; ok = 1; } }
    }
    free(last);
    return ok;
}

json *apply_json_patch(const json *orig, const json *patch, char **err){
    if(!j_is(patch, J_ARR)){ *err = xstrdup2("the body of the request was in an unknown format"); return NULL; }
    json *doc = j_clone(orig);
    for(size_t i = 0; i < patch->nitems; i++){
        json *op = patch->items[i];
        const char *o = j_gets(op, "op");
        const char *path = j_gets(op, "path");
        if(!o || !path){ *err = xstrdup2("missing op or path"); j_free(doc); return NULL; }
        json *val = j_get(op, "value");
        if(!strcmp(o, "test")){
            json *cur = NULL;
            if(!ptr_get(doc, path, &cur) || !j_equal(cur, val)){
                *err = xasprintf("testing value %s failed", path);
                j_free(doc); return NULL;
            }
            continue;
        }
        if(!strcmp(o, "remove") || !strcmp(o, "move") || !strcmp(o, "copy") ||
           !strcmp(o, "add") || !strcmp(o, "replace")){
            json *from_val = NULL;
            if(!strcmp(o, "move") || !strcmp(o, "copy")){
                const char *from = j_gets(op, "from");
                json *fv = NULL;
                if(!from || !ptr_get(doc, from, &fv)){
                    *err = xasprintf("remove operation does not apply: doc is missing path: %s",
                                     from ? from : "");
                    j_free(doc); return NULL;
                }
                from_val = j_clone(fv);
                if(!strcmp(o, "move")){
                    char *flast = NULL;
                    json *fpar = walk_parent(doc, from, &flast);
                    if(fpar && fpar->t == J_OBJ) j_del(fpar, flast);
                    else if(fpar && fpar->t == J_ARR){ char *e2; long ix = strtol(flast,&e2,10); if(!*e2) j_remove_at(fpar,(size_t)ix); }
                    free(flast);
                }
            }
            char *last = NULL;
            json *par = walk_parent(doc, path, &last);
            if(!par){
                free(last); j_free(from_val);
                *err = xasprintf("add operation does not apply: doc is missing path: %s", path);
                j_free(doc); return NULL;
            }
            json *v = from_val ? from_val : (val ? j_clone(val) : j_null());
            if(par->t == J_OBJ){
                if(!strcmp(o, "remove")){
                    if(!j_get(par, last)){
                        free(last); j_free(v);
                        *err = xasprintf("remove operation does not apply: doc is missing path: %s", path);
                        j_free(doc); return NULL;
                    }
                    j_del(par, last); j_free(v);
                } else if(!strcmp(o, "replace")){
                    if(!j_get(par, last)){
                        free(last); j_free(v);
                        *err = xasprintf("replace operation does not apply: doc is missing key: %s", path);
                        j_free(doc); return NULL;
                    }
                    j_set(par, last, v);
                } else j_set(par, last, v);
            } else if(par->t == J_ARR){
                if(!strcmp(last, "-")){ j_push(par, v); }
                else {
                    char *end; long idx = strtol(last, &end, 10);
                    if(*end || idx < 0 || (size_t)idx > par->nitems){
                        free(last); j_free(v);
                        *err = xasprintf("add operation does not apply: doc is missing path: %s", path);
                        j_free(doc); return NULL;
                    }
                    if(!strcmp(o, "remove")){ j_remove_at(par, (size_t)idx); j_free(v); }
                    else if(!strcmp(o, "replace")){
                        if((size_t)idx >= par->nitems){ free(last); j_free(v);
                            *err = xstrdup2("replace out of range"); j_free(doc); return NULL; }
                        j_free(par->items[idx]); par->items[idx] = v;
                    } else {
                        j_push(par, j_null());
                        for(size_t k = par->nitems - 1; k > (size_t)idx; k--) par->items[k] = par->items[k-1];
                        par->items[idx] = v;
                    }
                }
            } else { free(last); j_free(v); *err = xstrdup2("bad path"); j_free(doc); return NULL; }
            free(last);
            continue;
        }
        *err = xasprintf("Unexpected kind: %s", o);
        j_free(doc); return NULL;
    }
    return doc;
}

/* ---------- RFC 7386 merge patch ---------- */
static void merge_into(json *dst, const json *patch){
    for(size_t i = 0; i < patch->nmem; i++){
        const char *k = patch->mem[i].key;
        json *v = patch->mem[i].val;
        if(v->t == J_NULL){ j_del(dst, k); continue; }
        json *cur = j_get(dst, k);
        if(v->t == J_OBJ && cur && cur->t == J_OBJ) merge_into(cur, v);
        else j_set(dst, k, j_clone(v));
    }
}
json *apply_merge_patch(const json *orig, const json *patch){
    if(!j_is(patch, J_OBJ)) return j_clone(patch);
    json *doc = j_clone(orig);
    merge_into(doc, patch);
    return doc;
}

/* ---------- strategic merge patch ---------- */
/* Upstream keys list merges off the Go struct tag patchMergeKey.  The table below
 * carries the same keys, addressed by the JSON path of the list. */
static const struct { const char *suffix; const char *key; } MERGE_KEYS[] = {
    { "spec.containers",                  "name" },
    { "spec.initContainers",              "name" },
    { "spec.ephemeralContainers",         "name" },
    { "spec.volumes",                     "name" },
    { "spec.imagePullSecrets",            "name" },
    { "spec.tolerations",                 NULL },       /* atomic */

    { "env",                              "name" },
    { "envFrom",                          NULL },
    { "volumeMounts",                     "mountPath" },
    { "volumeDevices",                    "devicePath" },
    { "spec.ports",                       "port" },
    { "ports",                            "containerPort" },   /* container ports */
    { "spec.template.spec.containers",    "name" },
    { "spec.template.spec.initContainers","name" },
    { "spec.template.spec.volumes",       "name" },
    { "metadata.finalizers",              "" },         /* merge set of scalars */
    { "metadata.ownerReferences",         "uid" },
    { "subsets",                          NULL },
    { "rules",                            NULL },
    { "status.conditions",                "type" },
    { NULL, NULL }
};

static const char *merge_key_for(const char *path){
    for(int i = 0; MERGE_KEYS[i].suffix; i++){
        size_t n = strlen(MERGE_KEYS[i].suffix);
        size_t p = strlen(path);
        if(p >= n && strcmp(path + p - n, MERGE_KEYS[i].suffix) == 0){
            const char *before = path + p - n;
            if(before == path || before[-1] == '.') return MERGE_KEYS[i].key;
        }
    }
    return NULL;
}

static void smp_obj(json *dst, const json *patch, const char *path);

static void smp_list(json **dstp, const json *patch, const char *path, const char *mk){
    json *dst = *dstp;
    if(!mk){ j_free(dst); *dstp = j_clone(patch); return; }
    if(!*mk){            /* set of scalars */
        json *out = j_arr();
        for(size_t i = 0; i < patch->nitems; i++) j_push(out, j_clone(patch->items[i]));
        for(size_t i = 0; dst && i < dst->nitems; i++){
            int found = 0;
            for(size_t k = 0; k < out->nitems; k++) if(j_equal(out->items[k], dst->items[i])) found = 1;
            if(!found) j_push(out, j_clone(dst->items[i]));
        }
        j_free(dst); *dstp = out; return;
    }
    if(!dst || dst->t != J_ARR){ j_free(dst); dst = j_arr(); }
    /* The merged list is ordered the way upstream normalises it: the entries the
       patch mentions, in the patch's order, then the entries only the stored
       object had, in theirs. */
    json *seen = j_arr();
    for(size_t i = 0; i < patch->nitems; i++){
        json *pe = patch->items[i];
        if(pe->t != J_OBJ){ continue; }
        json *pv = j_get(pe, mk);
        const char *directive = j_gets(pe, "$patch");
        int matched = 0;
        for(size_t k = 0; k < dst->nitems; k++){
            json *de = dst->items[k];
            json *dv = j_get(de, mk);
            if(pv && dv && j_equal(pv, dv)){
                matched = 1;
                if(directive && !strcmp(directive, "delete")){ j_remove_at(dst, k); }
                else {
                    char sub[256]; snprintf(sub, sizeof sub, "%s[]", path);
                    smp_obj(de, pe, sub);
                    j_push(seen, j_clone(pv));
                }
                break;
            }
        }
        if(!matched && !(directive && !strcmp(directive, "delete"))){
            json *cp = j_clone(pe);
            j_del(cp, "$patch");
            j_push(dst, cp);
            if(pv) j_push(seen, j_clone(pv));
        }
    }
    if(seen->nitems){
        json *out = j_arr();
        for(size_t i = 0; i < seen->nitems; i++)
            for(size_t k = 0; k < dst->nitems; k++)
                if(j_equal(j_get(dst->items[k], mk), seen->items[i])){
                    j_push(out, j_clone(dst->items[k])); break;
                }
        for(size_t k = 0; k < dst->nitems; k++){
            int in_patch = 0;
            for(size_t i = 0; i < seen->nitems; i++)
                if(j_equal(j_get(dst->items[k], mk), seen->items[i])) in_patch = 1;
            if(!in_patch) j_push(out, j_clone(dst->items[k]));
        }
        j_free(dst); dst = out;
    }
    j_free(seen);
    *dstp = dst;
}

static void smp_obj(json *dst, const json *patch, const char *path){
    for(size_t i = 0; i < patch->nmem; i++){
        const char *k = patch->mem[i].key;
        json *pv = patch->mem[i].val;
        if(!strcmp(k, "$patch")) continue;
        if(str_has_prefix(k, "$setElementOrder/")) continue;
        if(pv->t == J_NULL){ j_del(dst, k); continue; }
        char sub[256];
        snprintf(sub, sizeof sub, "%s%s%s", path, *path ? "." : "", k);
        json *dv = j_get(dst, k);
        if(pv->t == J_OBJ && dv && dv->t == J_OBJ){ smp_obj(dv, pv, sub); continue; }
        if(pv->t == J_ARR){
            const char *mk = merge_key_for(sub);
            json *cur = dv ? j_clone(dv) : NULL;
            smp_list(&cur, pv, sub, mk);
            j_set(dst, k, cur);
            continue;
        }
        j_set(dst, k, j_clone(pv));
    }
}

json *apply_strategic_patch(resdef *rd, const json *orig, const json *patch){
    if(!j_is(patch, J_OBJ)) return j_clone(patch);
    json *doc = j_clone(orig);
    smp_obj(doc, patch, "");
    return doc;
}
