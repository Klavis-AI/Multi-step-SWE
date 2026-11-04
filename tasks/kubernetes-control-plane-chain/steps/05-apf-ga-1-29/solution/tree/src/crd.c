#define _GNU_SOURCE
#include "api.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dynamic resources contributed by CustomResourceDefinitions. */
typedef struct {
    resdef rd;
    json *schema;        /* openAPIV3Schema for this version */
    char *scale_spec, *scale_status;
    int has_status;
    char *crd_name;
} crdres;

static crdres *CRDS; static int NCRD;

const char *crd_scale_spec_path(resdef *rd){
    for(int i = 0; i < NCRD; i++) if(&CRDS[i].rd == rd) return CRDS[i].scale_spec;
    return NULL;
}
const char *crd_scale_status_path(resdef *rd){
    for(int i = 0; i < NCRD; i++) if(&CRDS[i].rd == rd) return CRDS[i].scale_status;
    return NULL;
}

static void drop_for(const char *crd_name){
    for(int i = 0; i < NCRD; ){
        if(!strcmp(CRDS[i].crd_name, crd_name)){
            free(CRDS[i].crd_name);
            memmove(&CRDS[i], &CRDS[i+1], (size_t)(NCRD - i - 1) * sizeof(crdres));
            NCRD--;
        } else i++;
    }
}

void crd_unregister(const char *name){ drop_for(name); }

void crd_register(json *crd){
    const char *name = j_gets(j_get(crd, "metadata"), "name");
    if(!name) return;
    drop_for(name);
    json *spec = j_get(crd, "spec");
    const char *group = j_gets(spec, "group");
    json *names = j_get(spec, "names");
    const char *scope = j_gets(spec, "scope");
    json *vers = j_get(spec, "versions");
    for(size_t i = 0; j_is(vers, J_ARR) && i < vers->nitems; i++){
        json *v = vers->items[i];
        if(!j_getb(v, "served", 0)) continue;
        CRDS = realloc(CRDS, (size_t)(NCRD + 1) * sizeof(crdres));
        crdres *cr = &CRDS[NCRD++];
        memset(cr, 0, sizeof *cr);
        cr->crd_name = xstrdup2(name);
        cr->rd.group = xstrdup2(group ?: "");
        cr->rd.version = xstrdup2(j_gets(v, "name") ?: "");
        cr->rd.resource = xstrdup2(j_gets(names, "plural") ?: "");
        cr->rd.kind = xstrdup2(j_gets(names, "kind") ?: "");
        cr->rd.singular = xstrdup2(j_gets(names, "singular") ?: "");
        cr->rd.namespaced = scope && !strcmp(scope, "Namespaced");
        cr->rd.prefix = xasprintf("%s/%s", group ?: "", j_gets(names, "plural") ?: "");
        cr->rd.canon = xstrdup2(cr->rd.version);
        cr->rd.optional = 2;
        cr->rd.subresources = j_arr();
        cr->rd.shortnames = j_clone(j_get(names, "shortNames"));
        cr->rd.categories = j_clone(j_get(names, "categories"));
        cr->rd.verbs = j_parse0("[\"delete\",\"deletecollection\",\"get\",\"list\",\"patch\","
                                "\"create\",\"update\",\"watch\"]");
        json *sub = j_get(v, "subresources");
        if(sub && j_get(sub, "status")){ j_push(cr->rd.subresources, j_str("status")); cr->has_status = 1; }
        if(sub && j_get(sub, "scale")){
            j_push(cr->rd.subresources, j_str("scale"));
            json *sc = j_get(sub, "scale");
            cr->scale_spec = xstrdup2(j_gets(sc, "specReplicasPath") ?: ".spec.replicas");
            cr->scale_status = xstrdup2(j_gets(sc, "statusReplicasPath") ?: ".status.replicas");
        }
        json *sch = j_get(v, "schema");
        cr->schema = sch ? j_clone(j_get(sch, "openAPIV3Schema")) : NULL;
    }
}

resdef *crd_lookup(const char *group, const char *version, const char *resource){
    for(int i = 0; i < NCRD; i++)
        if(!strcmp(CRDS[i].rd.group, group) && !strcmp(CRDS[i].rd.version, version) &&
           !strcmp(CRDS[i].rd.resource, resource)) return &CRDS[i].rd;
    return NULL;
}

json *crd_resource_list(const char *gv){
    json *out = NULL;
    for(int i = 0; i < NCRD; i++){
        char mine[256];
        if(*CRDS[i].rd.group) snprintf(mine, sizeof mine, "%s/%s", CRDS[i].rd.group, CRDS[i].rd.version);
        else snprintf(mine, sizeof mine, "%s", CRDS[i].rd.version);
        if(strcmp(mine, gv)) continue;
        if(!out){
            out = j_obj();
            j_set(out, "kind", j_str("APIResourceList"));
            j_set(out, "apiVersion", j_str("v1"));
            j_set(out, "groupVersion", j_str(gv));
            j_set(out, "resources", j_arr());
        }
        json *res = j_get(out, "resources");
        json *e = j_obj();
        j_set(e, "name", j_str(CRDS[i].rd.resource));
        j_set(e, "singularName", j_str(CRDS[i].rd.singular));
        j_set(e, "namespaced", j_bool(CRDS[i].rd.namespaced));
        j_set(e, "kind", j_str(CRDS[i].rd.kind));
        j_set(e, "verbs", j_clone(CRDS[i].rd.verbs));
        if(CRDS[i].rd.shortnames) j_set(e, "shortNames", j_clone(CRDS[i].rd.shortnames));
        if(CRDS[i].rd.categories) j_set(e, "categories", j_clone(CRDS[i].rd.categories));
        j_push(res, e);
        for(size_t k = 0; k < CRDS[i].rd.subresources->nitems; k++){
            json *se = j_obj();
            char *nm = xasprintf("%s/%s", CRDS[i].rd.resource,
                                 j_str_of(CRDS[i].rd.subresources->items[k]));
            j_set(se, "name", j_str(nm)); free(nm);
            j_set(se, "singularName", j_str(""));
            j_set(se, "namespaced", j_bool(CRDS[i].rd.namespaced));
            const char *sub = j_str_of(CRDS[i].rd.subresources->items[k]);
            j_set(se, "kind", j_str(!strcmp(sub, "scale") ? "Scale" : CRDS[i].rd.kind));
            if(!strcmp(sub, "scale")) j_set(se, "group", j_str("autoscaling")),
                                      j_set(se, "version", j_str("v1"));
            j_set(se, "verbs", j_parse0("[\"get\",\"patch\",\"update\"]"));
            j_push(res, se);
        }
    }
    return out;
}

json *crd_all_group_versions(void){
    json *out = j_arr();
    for(int i = 0; i < NCRD; i++){
        json *found = NULL;
        for(size_t k = 0; k < out->nitems; k++)
            if(!strcmp(j_gets(out->items[k], "name") ?: "", CRDS[i].rd.group)) found = out->items[k];
        if(!found){
            found = j_obj();
            j_set(found, "name", j_str(CRDS[i].rd.group));
            j_set(found, "versions", j_arr());
            j_push(out, found);
        }
        json *vl = j_get(found, "versions");
        int have = 0;
        for(size_t k = 0; k < vl->nitems; k++)
            if(!strcmp(j_gets(vl->items[k], "version") ?: "", CRDS[i].rd.version)) have = 1;
        if(have) continue;
        json *e = j_obj();
        char *gv = xasprintf("%s/%s", CRDS[i].rd.group, CRDS[i].rd.version);
        j_set(e, "groupVersion", j_str(gv));
        j_set(e, "version", j_str(CRDS[i].rd.version));
        j_push(vl, e);
        json *pref = j_obj();
        j_set(pref, "groupVersion", j_str(gv));
        j_set(pref, "version", j_str(CRDS[i].rd.version));
        j_set(found, "preferredVersion", pref);
        free(gv);
    }
    return out;
}

static int load_cb(sobj *o, void *ud){ crd_register(o->obj); return 1; }
void crd_load_all(void){ store_each("/registry/apiextensions.k8s.io/customresourcedefinitions/", load_cb, NULL); }

/* ------------------------- structural schema handling ---------------------- */

/* -----------------------------------------------------------------------------
 * Structural schema rules, reported the way upstream reports them: against the
 * legacy `spec.validation.openAPIV3Schema` path, because that is the internal
 * representation its validator walks.
 *   - every property must say what type it is;
 *   - inside a logical junctor (anyOf, oneOf, allOf, not) a type is forbidden;
 *   - a property a junctor mentions must also exist beside it.
 * -------------------------------------------------------------------------- */
#define LEGACY "spec.validation.openAPIV3Schema"

static void structural_props(json *sch, const char *base, json *cs){
    json *props = j_get(sch, "properties");
    for(size_t i = 0; j_is(props, J_OBJ) && i < props->nmem; i++){
        json *ps = props->mem[i].val;
        char here[320];
        snprintf(here, sizeof here, "%s.properties[%s]", base, props->mem[i].key);
        if(!j_gets(ps, "type")){
            char f[384];
            snprintf(f, sizeof f, "%s.type", here);
            add_cause2(cs, "FieldValueRequired", f);
        }
        structural_props(ps, here, cs);
    }
    json *items = j_get(sch, "items");
    if(items){
        char here[320];
        snprintf(here, sizeof here, "%s.items", base);
        structural_props(items, here, cs);
    }
}

static void structural_junctors(json *sch, const char *base, json *cs){
    const char *names[] = {"allOf", "anyOf", "oneOf", NULL};
    json *props = j_get(sch, "properties");
    for(int k = 0; names[k]; k++){
        json *arr = j_get(sch, names[k]);
        for(size_t i = 0; j_is(arr, J_ARR) && i < arr->nitems; i++){
            json *br = arr->items[i];
            json *bp = j_get(br, "properties");
            for(size_t m = 0; j_is(bp, J_OBJ) && m < bp->nmem; m++){
                if(j_gets(bp->mem[m].val, "type")){
                    char f[384];
                    snprintf(f, sizeof f, "%s.%s[%zu].properties[%s].type",
                             base, names[k], i, bp->mem[m].key);
                    add_cause2(cs, "FieldValueForbidden", f);
                }
                if(!j_get(props, bp->mem[m].key)){
                    char f[384];
                    snprintf(f, sizeof f, "%s.properties[%s]", base, bp->mem[m].key);
                    add_cause2(cs, "FieldValueRequired", f);
                }
            }
        }
    }
}

static int cause_cmp(const void *a, const void *b){
    const json *x = *(json *const *)a, *y = *(json *const *)b;
    return strcmp(j_gets((json*)x, "field") ?: "", j_gets((json*)y, "field") ?: "");
}

static void structural_causes(json *root, json *cs){
    size_t before = cs->nitems;
    structural_props(root, LEGACY, cs);
    structural_junctors(root, LEGACY, cs);
    if(cs->nitems > before + 1)
        qsort(cs->items + before, cs->nitems - before, sizeof(json*), cause_cmp);
}


static void prune(json *obj, json *schema);

static void prune_props(json *obj, json *schema){
    json *props = j_get(schema, "properties");
    json *addl = j_get(schema, "additionalProperties");
    if(!props && !addl) return;
    for(size_t i = 0; i < obj->nmem; ){
        json *ps = props ? j_get(props, obj->mem[i].key) : NULL;
        if(!ps && !(addl && addl->t == J_OBJ)){
            j_del(obj, obj->mem[i].key);
            continue;
        }
        prune(obj->mem[i].val, ps ? ps : addl);
        i++;
    }
}

static void prune(json *obj, json *schema){
    if(!obj || !schema) return;
    const char *t = j_gets(schema, "type");
    if(obj->t == J_OBJ && (!t || !strcmp(t, "object"))){
        if(j_getb(schema, "x-kubernetes-preserve-unknown-fields", 0)) return;
        prune_props(obj, schema);
    } else if(obj->t == J_ARR){
        json *items = j_get(schema, "items");
        for(size_t i = 0; i < obj->nitems; i++) prune(obj->items[i], items);
    }
}

static void defaults_rec(json *obj, json *schema){
    if(!obj || !schema) return;
    if(obj->t == J_OBJ){
        json *props = j_get(schema, "properties");
        for(size_t i = 0; j_is(props, J_OBJ) && i < props->nmem; i++){
            json *ps = props->mem[i].val;
            json *dv = j_get(ps, "default");
            json *cur = j_get(obj, props->mem[i].key);
            if(!cur && dv){ j_set(obj, props->mem[i].key, j_clone(dv)); cur = j_get(obj, props->mem[i].key); }
            if(cur) defaults_rec(cur, ps);
        }
    } else if(obj->t == J_ARR){
        json *items = j_get(schema, "items");
        for(size_t i = 0; i < obj->nitems; i++) defaults_rec(obj->items[i], items);
    }
}

static json *schema_of(resdef *rd){
    for(int i = 0; i < NCRD; i++) if(&CRDS[i].rd == rd) return CRDS[i].schema;
    return NULL;
}
static int status_sub_of(resdef *rd){
    for(int i = 0; i < NCRD; i++) if(&CRDS[i].rd == rd) return CRDS[i].has_status;
    return 0;
}

void crd_default(resdef *rd, json *obj){
    json *s = schema_of(rd);
    if(!s) return;
    /* apiVersion, kind and metadata are never pruned by a structural schema */
    json *av = j_get(obj, "apiVersion"), *kd = j_get(obj, "kind"), *md = j_get(obj, "metadata");
    av = av ? j_clone(av) : NULL; kd = kd ? j_clone(kd) : NULL; md = md ? j_clone(md) : NULL;
    prune(obj, s);
    if(av) j_set(obj, "apiVersion", av);
    if(kd) j_set(obj, "kind", kd);
    if(md) j_set(obj, "metadata", md);
    defaults_rec(obj, s);
    if(status_sub_of(rd)) j_del(obj, "status");
}

void add_cause2(json *cs, const char *reason, const char *field){
    json *c = j_obj();
    j_set(c, "reason", j_str(reason));
    j_set(c, "field", j_str(field));
    j_push(cs, c);
}

static void validate_rec(json *obj, json *schema, const char *path, json *cs){
    if(!schema) return;
    const char *t = j_gets(schema, "type");
    if(t && obj){
        int ok = 1;
        if(!strcmp(t, "object")) ok = obj->t == J_OBJ;
        else if(!strcmp(t, "array")) ok = obj->t == J_ARR;
        else if(!strcmp(t, "string")) ok = obj->t == J_STR;
        else if(!strcmp(t, "integer")) ok = obj->t == J_NUM && obj->is_int;
        else if(!strcmp(t, "number")) ok = obj->t == J_NUM;
        else if(!strcmp(t, "boolean")) ok = obj->t == J_BOOL;
        if(!ok){
            char dotted[260];
            snprintf(dotted, sizeof dotted, ".%s", path);
            add_cause2(cs, "FieldValueInvalid", dotted);
            add_cause2(cs, "FieldValueTypeInvalid", path);
            return;
        }
    }
    if(!obj) return;
    json *en = j_get(schema, "enum");
    if(j_is(en, J_ARR)){
        int found = 0;
        for(size_t i = 0; i < en->nitems; i++) if(j_equal(en->items[i], obj)) found = 1;
        if(!found) add_cause2(cs, "FieldValueNotSupported", path);
    }
    if(obj->t == J_NUM){
        json *mn = j_get(schema, "minimum"), *mx = j_get(schema, "maximum");
        if(mn && mn->t == J_NUM && obj->n < mn->n) add_cause2(cs, "FieldValueInvalid", path);
        if(mx && mx->t == J_NUM && obj->n > mx->n) add_cause2(cs, "FieldValueInvalid", path);
    }
    if(obj->t == J_OBJ){
        json *req = j_get(schema, "required");
        for(size_t i = 0; j_is(req, J_ARR) && i < req->nitems; i++){
            const char *k = j_str_of(req->items[i]);
            if(k && !j_get(obj, k)){
                char p2[256]; snprintf(p2, sizeof p2, "%s%s%s", path, *path ? "." : "", k);
                add_cause2(cs, "FieldValueRequired", p2);
            }
        }
        json *props = j_get(schema, "properties");
        for(size_t i = 0; j_is(props, J_OBJ) && i < props->nmem; i++){
            json *cur = j_get(obj, props->mem[i].key);
            if(!cur) continue;
            char p2[256]; snprintf(p2, sizeof p2, "%s%s%s", path, *path ? "." : "", props->mem[i].key);
            validate_rec(cur, props->mem[i].val, p2, cs);
        }
    } else if(obj->t == J_ARR){
        json *items = j_get(schema, "items");
        for(size_t i = 0; i < obj->nitems; i++){
            char p2[256]; snprintf(p2, sizeof p2, "%s[%zu]", path, i);
            validate_rec(obj->items[i], items, p2, cs);
        }
    }
}

void add_cause2(json *cs, const char *reason, const char *field);

/* Validation of a CustomResourceDefinition object itself. */
json *crd_object_validate(json *crd){
    json *cs = j_arr();
    json *spec = j_get(crd, "spec");
    const char *group = spec ? j_gets(spec, "group") : NULL;
    json *names = spec ? j_get(spec, "names") : NULL;
    const char *plural = names ? j_gets(names, "plural") : NULL;
    const char *name = j_gets(j_get(crd, "metadata"), "name");
    if(group && plural && name){
        char want[512];
        snprintf(want, sizeof want, "%s.%s", plural, group);
        if(strcmp(name, want)) add_cause2(cs, "FieldValueInvalid", "metadata.name");
    }
    json *vers = spec ? j_get(spec, "versions") : NULL;
    int storage = 0;
    for(size_t i = 0; j_is(vers, J_ARR) && i < vers->nitems; i++){
        if(j_getb(vers->items[i], "storage", 0)) storage++;
        json *sch = j_get(vers->items[i], "schema");
        json *root = sch ? j_get(sch, "openAPIV3Schema") : NULL;
        char field[128];
        snprintf(field, sizeof field, "spec.versions[%zu].schema.openAPIV3Schema", i);
        if(!root){ add_cause2(cs, "FieldValueRequired", field); continue; }
        const char *t = j_gets(root, "type");
        if(!t || strcmp(t, "object")){ add_cause2(cs, "FieldValueForbidden", field); continue; }
        structural_causes(root, cs);
    }
    if(storage != 1 && !cs->nitems){
        add_cause2(cs, "FieldValueInvalid", "spec.versions");
        add_cause2(cs, "FieldValueInvalid", "status.storedVersions");
    }
    if(!cs->nitems){ j_free(cs); return NULL; }
    return cs;
}

int crd_validate(resdef *rd, json *obj, json **causes){
    json *cs = j_arr();
    const char *name = j_gets(j_get(obj, "metadata"), "name");
    if(!name || !*name) add_cause2(cs, "FieldValueRequired", "metadata.name");
    json *s = schema_of(rd);
    if(s) validate_rec(obj, s, "", cs);

    if(!cs->nitems){ j_free(cs); *causes = NULL; return 1; }
    *causes = cs;
    return 0;
}

/* One aggregated-discovery group entry for a CustomResourceDefinition group. */
json *crd_agg_group(const char *group, const char *aggv){
    json *versions = j_arr();
    for(int i = 0; i < NCRD; i++){
        if(strcmp(CRDS[i].rd.group, group)) continue;
        json *ve = j_obj();
        j_set(ve, "version", j_str(CRDS[i].rd.version));
        json *res = j_arr();
        json *r = j_obj();
        j_set(r, "resource", j_str(CRDS[i].rd.resource));
        json *rk = j_obj();
        j_set(rk, "group", j_str(group));
        j_set(rk, "version", j_str(CRDS[i].rd.version));
        j_set(rk, "kind", j_str(CRDS[i].rd.kind));
        j_set(r, "responseKind", rk);
        j_set(r, "scope", j_str(CRDS[i].rd.namespaced ? "Namespaced" : "Cluster"));
        j_set(r, "singularResource", j_str(CRDS[i].rd.singular));
        j_set(r, "verbs", j_clone(CRDS[i].rd.verbs));
        if(CRDS[i].rd.shortnames) j_set(r, "shortNames", j_clone(CRDS[i].rd.shortnames));
        if(CRDS[i].rd.categories) j_set(r, "categories", j_clone(CRDS[i].rd.categories));
        json *subs = j_arr();
        for(size_t z = 0; z < CRDS[i].rd.subresources->nitems; z++){
            const char *sn = j_str_of(CRDS[i].rd.subresources->items[z]);
            json *se = j_obj();
            j_set(se, "subresource", j_str(sn));
            json *srk = j_obj();
            if(!strcmp(sn, "scale")){
                j_set(srk, "group", j_str("autoscaling"));
                j_set(srk, "version", j_str("v1"));
                j_set(srk, "kind", j_str("Scale"));
            } else {
                j_set(srk, "group", j_str(group));
                j_set(srk, "version", j_str(CRDS[i].rd.version));
                j_set(srk, "kind", j_str(CRDS[i].rd.kind));
            }
            j_set(se, "responseKind", srk);
            j_set(se, "verbs", j_parse0("[\"get\",\"patch\",\"update\"]"));
            j_push(subs, se);
        }
        if(subs->nitems) j_set(r, "subresources", subs); else j_free(subs);
        j_push(res, r);
        j_set(ve, "resources", res);
        j_set(ve, "freshness", j_str("Current"));
        j_push(versions, ve);
    }
    if(!versions->nitems){ j_free(versions); return NULL; }
    json *g = j_obj();
    json *md = j_obj();
    j_set(md, "name", j_str(group));
    j_set(md, "creationTimestamp", j_null());
    j_set(g, "metadata", md);
    j_set(g, "versions", versions);
    return g;
}
