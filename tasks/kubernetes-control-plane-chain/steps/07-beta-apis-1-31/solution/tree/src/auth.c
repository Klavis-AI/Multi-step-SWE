#define _GNU_SOURCE
#include "api.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------ authentication ------------------------------ */
typedef struct { char *token, *user, *uid; json *groups; } tokrec;
static tokrec *TOKS; static int NTOK;

static char *csv_field(char **p){
    char *s = *p;
    while(*s == ' ') s++;
    char *out;
    if(*s == '"'){
        s++;
        char *e = strchr(s, '"');
        if(!e) e = s + strlen(s);
        out = malloc((size_t)(e - s) + 1);
        memcpy(out, s, (size_t)(e - s)); out[e - s] = 0;
        s = *e ? e + 1 : e;
        if(*s == ',') s++;
    } else {
        char *e = strchr(s, ',');
        if(!e) e = s + strlen(s);
        out = malloc((size_t)(e - s) + 1);
        memcpy(out, s, (size_t)(e - s)); out[e - s] = 0;
        s = *e ? e + 1 : e;
    }
    *p = s;
    return out;
}

void token_file_load(const char *path){
    FILE *f = fopen(path, "r");
    if(!f) return;
    char line[4096];
    while(fgets(line, sizeof line, f)){
        char *nl = strpbrk(line, "\r\n"); if(nl) *nl = 0;
        if(!line[0] || line[0] == '#') continue;
        char *p = line;
        char *tok = csv_field(&p), *user = csv_field(&p), *uid = csv_field(&p);
        char *grp = *p ? csv_field(&p) : xstrdup2("");
        TOKS = realloc(TOKS, (size_t)(NTOK + 1) * sizeof(tokrec));
        TOKS[NTOK].token = tok; TOKS[NTOK].user = user; TOKS[NTOK].uid = uid;
        json *g = j_arr();
        if(grp[0]){
            int n = 0; char **parts = str_split(grp, ',', &n);
            for(int i = 0; i < n; i++){
                char *q = parts[i];
                while(*q == ' ') q++;
                if(*q) j_push(g, j_str(q));
            }
            str_split_free(parts, n);
        }
        TOKS[NTOK].groups = g;
        NTOK++;
        free(grp);
    }
    fclose(f);
}

int authenticate(hreq *r, ident *who){
    const char *a = hreq_header(r, "Authorization");
    who->name = NULL; who->groups = j_arr(); who->uid = NULL;
    if(!a || !str_has_prefix(a, "Bearer ")){
        who->name = xstrdup2("system:anonymous");
        j_push(who->groups, j_str("system:unauthenticated"));
        return 1;
    }
    const char *tok = a + 7;
    while(*tok == ' ') tok++;
    if(!*tok){
        who->name = xstrdup2("system:anonymous");
        j_push(who->groups, j_str("system:unauthenticated"));
        return 1;
    }
    for(int i = 0; i < NTOK; i++){
        if(!strcmp(TOKS[i].token, tok)){
            who->name = xstrdup2(TOKS[i].user);
            who->uid = xstrdup2(TOKS[i].uid ?: "");
            for(size_t k = 0; k < TOKS[i].groups->nitems; k++)
                j_push(who->groups, j_clone(TOKS[i].groups->items[k]));
            j_push(who->groups, j_str("system:authenticated"));
            return 1;
        }
    }
    return 0;   /* 401 */
}

/* --------------------------------- RBAC ------------------------------------- */
static int has_group(const ident *who, const char *g){
    for(size_t i = 0; i < who->groups->nitems; i++){
        const char *v = j_str_of(who->groups->items[i]);
        if(v && !strcmp(v, g)) return 1;
    }
    return 0;
}

static int list_has(json *arr, const char *v){
    if(!j_is(arr, J_ARR)) return 0;
    for(size_t i = 0; i < arr->nitems; i++){
        const char *s = j_str_of(arr->items[i]);
        if(s && (!strcmp(s, v) || !strcmp(s, "*"))) return 1;
    }
    return 0;
}
static int list_has_exact(json *arr, const char *v){
    if(!j_is(arr, J_ARR)) return 0;
    for(size_t i = 0; i < arr->nitems; i++){
        const char *s = j_str_of(arr->items[i]);
        if(s && !strcmp(s, v)) return 1;
    }
    return 0;
}
static int url_match(const char *pat, const char *path){
    size_t n = strlen(pat);
    if(n && pat[n-1] == '*') return strncmp(pat, path, n-1) == 0;
    return strcmp(pat, path) == 0;
}

/* Collect every PolicyRule that applies to `who` in `ns`. */
typedef struct { json *rules; } rulebag;

static json *role_rules(const char *kind, const char *name, const char *ns){
    char key[512];
    if(!strcmp(kind, "ClusterRole")) snprintf(key, sizeof key, "/registry/clusterroles/%s", name);
    else snprintf(key, sizeof key, "/registry/roles/%s/%s", ns, name);
    sobj *o = store_get(key);
    return o ? j_get(o->obj, "rules") : NULL;
}

static int subject_matches(json *sub, const ident *who, const char *binding_ns){
    const char *k = j_gets(sub, "kind");
    const char *n = j_gets(sub, "name");
    if(!k || !n) return 0;
    if(!strcmp(k, "User")) return who->name && !strcmp(who->name, n);
    if(!strcmp(k, "Group")) return has_group(who, n);
    if(!strcmp(k, "ServiceAccount")){
        const char *sns = j_gets(sub, "namespace");
        if(!sns) sns = binding_ns;
        char *want = xasprintf("system:serviceaccount:%s:%s", sns ? sns : "", n);
        int ok = who->name && !strcmp(who->name, want);
        free(want);
        return ok;
    }
    return 0;
}

static int collect_cb(sobj *o, void *ud){
    rulebag *bag = ud;
    ident *who = ((void**)bag)[1];
    const char *ns = ((void**)bag)[2];
    json *b = o->obj;
    json *subs = j_get(b, "subjects");
    const char *bns = j_gets(j_get(b, "metadata"), "namespace");
    int match = 0;
    for(size_t i = 0; subs && j_is(subs, J_ARR) && i < subs->nitems; i++)
        if(subject_matches(subs->items[i], who, bns)){ match = 1; break; }
    if(!match) return 1;
    json *ref = j_get(b, "roleRef");
    const char *rk = j_gets(ref, "kind"), *rn = j_gets(ref, "name");
    if(!rk || !rn) return 1;
    json *rules = role_rules(rk, rn, bns ? bns : ns);
    for(size_t i = 0; rules && j_is(rules, J_ARR) && i < rules->nitems; i++)
        j_push(((json**)bag)[0], j_clone(rules->items[i]));
    return 1;
}

json *rbac_rules_for(const ident *who, const char *ns){
    void *bag[3];
    json *rules = j_arr();
    bag[0] = rules; bag[1] = (void*)who; bag[2] = (void*)ns;
    store_each("/registry/clusterrolebindings/", collect_cb, bag);
    if(ns && *ns){
        char pfx[512];
        snprintf(pfx, sizeof pfx, "/registry/rolebindings/%s/", ns);
        store_each(pfx, collect_cb, bag);
    }
    return rules;
}

static int rule_allows(json *rule, const reqinfo *ri, const char *nonres){
    json *verbs = j_get(rule, "verbs");
    if(!list_has(verbs, ri->verb)) return 0;
    if(nonres){
        json *urls = j_get(rule, "nonResourceURLs");
        if(!j_is(urls, J_ARR)) return 0;
        for(size_t i = 0; i < urls->nitems; i++){
            const char *p = j_str_of(urls->items[i]);
            if(p && (url_match(p, nonres) || !strcmp(p, "*"))) return 1;
        }
        return 0;
    }
    json *groups = j_get(rule, "apiGroups");
    if(!list_has(groups, ri->group ? ri->group : "")) return 0;
    json *res = j_get(rule, "resources");
    char full[256];
    if(ri->sub) snprintf(full, sizeof full, "%s/%s", ri->resource, ri->sub);
    else snprintf(full, sizeof full, "%s", ri->resource);
    if(!list_has(res, full) && !(ri->sub && list_has(res, ri->resource))) return 0;
    json *names = j_get(rule, "resourceNames");
    if(j_is(names, J_ARR) && names->nitems){
        if(!ri->name || !list_has_exact(names, ri->name)) return 0;
    }
    return 1;
}

int authorize(const ident *who, const reqinfo *ri, const char *nonres){
    if(has_group(who, "system:masters")) return 1;
    json *rules = rbac_rules_for(who, ri->ns);
    int ok = 0;
    for(size_t i = 0; i < rules->nitems; i++)
        if(rule_allows(rules->items[i], ri, nonres)){ ok = 1; break; }
    j_free(rules);
    return ok;
}

/* Does `who` already hold every permission in `rules`?  Used for the RBAC
 * escalation check on Role/ClusterRole writes. */
int rbac_covers(const ident *who, const char *ns, json *rules){
    if(has_group(who, "system:masters")) return 1;
    json *own = rbac_rules_for(who, ns);
    int ok = 1;
    for(size_t i = 0; rules && j_is(rules, J_ARR) && i < rules->nitems && ok; i++){
        json *want = rules->items[i];
        json *verbs = j_get(want, "verbs");
        json *groups = j_get(want, "apiGroups");
        json *res = j_get(want, "resources");
        json *urls = j_get(want, "nonResourceURLs");
        for(size_t v = 0; verbs && v < verbs->nitems && ok; v++){
            const char *verb = j_str_of(verbs->items[v]);
            if(j_is(urls, J_ARR) && urls->nitems){
                for(size_t u = 0; u < urls->nitems && ok; u++){
                    reqinfo ri = {0};
                    ri.verb = (char*)verb;
                    int found = 0;
                    for(size_t k = 0; k < own->nitems; k++)
                        if(rule_allows(own->items[k], &ri, j_str_of(urls->items[u]))){ found = 1; break; }
                    if(!found) ok = 0;
                }
                continue;
            }
            for(size_t g = 0; groups && g < groups->nitems && ok; g++){
                for(size_t rr = 0; res && rr < res->nitems && ok; rr++){
                    reqinfo ri = {0};
                    ri.verb = (char*)verb;
                    ri.group = (char*)j_str_of(groups->items[g]);
                    char *rname = xstrdup2(j_str_of(res->items[rr]) ?: "");
                    char *slash = strchr(rname, '/');
                    if(slash){ *slash = 0; ri.sub = slash + 1; }
                    ri.resource = rname;
                    ri.ns = (char*)ns;
                    int found = 0;
                    for(size_t k = 0; k < own->nitems; k++)
                        if(rule_allows(own->items[k], &ri, NULL)){ found = 1; break; }
                    free(rname);
                    if(!found) ok = 0;
                }
            }
        }
    }
    j_free(own);
    return ok;
}

/* SelfSubjectRulesReview: the rules the user holds in one namespace. */
json *rbac_rules_review(const ident *who, const char *ns){
    json *out = j_obj();
    json *rres = j_arr(), *rnon = j_arr();
    json *rules = has_group(who, "system:masters") ? NULL : rbac_rules_for(who, ns);
    if(!rules){
        json *r = j_obj();
        j_set(r, "verbs", j_arr()); j_push(j_get(r, "verbs"), j_str("*"));
        j_set(r, "apiGroups", j_arr()); j_push(j_get(r, "apiGroups"), j_str("*"));
        j_set(r, "resources", j_arr()); j_push(j_get(r, "resources"), j_str("*"));
        j_push(rres, r);
        json *n = j_obj();
        j_set(n, "verbs", j_arr()); j_push(j_get(n, "verbs"), j_str("*"));
        j_set(n, "nonResourceURLs", j_arr()); j_push(j_get(n, "nonResourceURLs"), j_str("*"));
        j_push(rnon, n);
    } else {
        for(size_t i = 0; i < rules->nitems; i++){
            json *r = rules->items[i];
            json *urls = j_get(r, "nonResourceURLs");
            if(j_is(urls, J_ARR) && urls->nitems){
                json *n = j_obj();
                j_set(n, "verbs", j_clone(j_get(r, "verbs")));
                j_set(n, "nonResourceURLs", j_clone(urls));
                j_push(rnon, n);
            }
            json *res = j_get(r, "resources");
            if(j_is(res, J_ARR) && res->nitems){
                json *n = j_obj();
                j_set(n, "verbs", j_clone(j_get(r, "verbs")));
                j_set(n, "apiGroups", j_clone(j_get(r, "apiGroups")));
                j_set(n, "resources", j_clone(res));
                if(j_get(r, "resourceNames")) j_set(n, "resourceNames", j_clone(j_get(r, "resourceNames")));
                j_push(rres, n);
            }
        }
        j_free(rules);
    }
    j_set(out, "resourceRules", rres);
    j_set(out, "nonResourceRules", rnon);
    j_set(out, "incomplete", j_bool(0));
    return out;
}
