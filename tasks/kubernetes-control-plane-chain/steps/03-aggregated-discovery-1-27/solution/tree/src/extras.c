#define _GNU_SOURCE
#include "api.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern json *rbac_rules_review(const ident *who, const char *ns);
extern int rbac_covers(const ident *, const char *, json *);

/* ---------------------- status defaults that need the object ---------------- */
void prepare_status_defaults(resdef *rd, json *obj){
    const char *K = rd->kind;
    if(!strcmp(K, "Job")){
        json *ann = j_ensure_obj(j_ensure_obj(obj, "metadata"), "annotations");
        if(RELEASE_MINOR <= 27 && !j_get(ann, "batch.kubernetes.io/job-tracking"))
            j_set(ann, "batch.kubernetes.io/job-tracking", j_str(""));
        json *md0 = j_get(obj, "metadata");
        json *labels0 = j_ensure_obj(md0, "labels");
        const char *uid0 = j_gets(md0, "uid");
        const char *nm0 = j_gets(md0, "name");
        if(!j_get(labels0, "controller-uid")) j_set(labels0, "controller-uid", j_str(uid0 ?: ""));
        if(!j_get(labels0, "job-name")) j_set(labels0, "job-name", j_str(nm0 ?: ""));
        if(RELEASE_MINOR >= 27){
            if(!j_get(labels0, "batch.kubernetes.io/controller-uid"))
                j_set(labels0, "batch.kubernetes.io/controller-uid", j_str(uid0 ?: ""));
            if(!j_get(labels0, "batch.kubernetes.io/job-name"))
                j_set(labels0, "batch.kubernetes.io/job-name", j_str(nm0 ?: ""));
        }
        const char *uid = j_gets(j_get(obj, "metadata"), "uid");
        const char *name = j_gets(j_get(obj, "metadata"), "name");
        json *spec = j_get(obj, "spec");
        if(!spec) return;
        /* A Job with per-index backoff counts failures per index, so the overall
           limit defaults to the largest int32 rather than 6. */
        if(j_get(spec, "backoffLimitPerIndex")) j_set(spec, "backoffLimit", j_int(2147483647));
        /* per-index backoff is pruned until 1.29 -- but only after it has had its
           effect on the overall limit, exactly as upstream's order does */
        if(RELEASE_MINOR <= 28){
            j_del(spec, "backoffLimitPerIndex");
            j_del(spec, "maxFailedIndexes");
            j_del(spec, "successPolicy");
        }
        /* 1.29 defaults both of these on every Job */
        if(RELEASE_MINOR >= 29){
            if(!j_get(spec, "manualSelector")) j_set(spec, "manualSelector", j_bool(0));
            if(!j_get(spec, "podReplacementPolicy"))
                j_set(spec, "podReplacementPolicy", j_str("TerminatingOrFailed"));
        }
        if(!j_get(spec, "selector")){
            json *sel = j_obj();
            json *ml = j_obj();
            /* From 1.27 the generated selector matches only the prefixed label;
               the unprefixed pair stays on the labels for compatibility. */
            if(RELEASE_MINOR >= 27) j_set(ml, "batch.kubernetes.io/controller-uid", j_str(uid ?: ""));
            else j_set(ml, "controller-uid", j_str(uid ?: ""));
            j_set(sel, "matchLabels", ml);
            j_set(spec, "selector", sel);
        }
        json *tmpl = j_get(spec, "template");
        if(tmpl){
            json *md = j_ensure_obj(tmpl, "metadata");
            json *labels = j_get(md, "labels");
            if(!labels){ labels = j_obj(); j_set(md, "labels", labels); }
            if(!j_get(labels, "controller-uid")) j_set(labels, "controller-uid", j_str(uid ?: ""));
            if(!j_get(labels, "job-name")) j_set(labels, "job-name", j_str(name ?: ""));
            if(RELEASE_MINOR >= 27){
                if(!j_get(labels, "batch.kubernetes.io/controller-uid"))
                    j_set(labels, "batch.kubernetes.io/controller-uid", j_str(uid ?: ""));
                if(!j_get(labels, "batch.kubernetes.io/job-name"))
                    j_set(labels, "batch.kubernetes.io/job-name", j_str(name ?: ""));
            }
        }
    } else if(!strcmp(K, "CustomResourceDefinition")){
        json *st = j_obj();
        json *acc = j_obj();
        j_set(acc, "kind", j_str(""));
        j_set(acc, "plural", j_str(""));
        j_set(st, "acceptedNames", acc);
        j_set(st, "conditions", j_null());
        json *sv = j_arr();
        json *vers0 = j_path(obj, "spec.versions");
        for(size_t i = 0; j_is(vers0, J_ARR) && i < vers0->nitems; i++)
            if(j_getb(vers0->items[i], "storage", 0))
                j_push(sv, j_clone(j_get(vers0->items[i], "name")));
        j_set(st, "storedVersions", sv);
        j_set(obj, "status", st);
        return;
    } else if(0){
        json *st = j_ensure_obj(obj, "status");
        json *names = j_path(obj, "spec.names");
        json *acc = j_obj();
        const char *fields[] = {"plural", "singular", "kind", "listKind", NULL};
        for(int i = 0; fields[i]; i++){
            const char *v = names ? j_gets(names, fields[i]) : NULL;
            if(v) j_set(acc, fields[i], j_str(v));
            else if(i < 1) j_set(acc, fields[i], j_str(""));
        }
        if(!j_get(acc, "kind")) j_set(acc, "kind", j_str(""));
        json *sn = names ? j_get(names, "shortNames") : NULL;
        if(sn) j_set(acc, "shortNames", j_clone(sn));
        json *cat = names ? j_get(names, "categories") : NULL;
        if(cat) j_set(acc, "categories", j_clone(cat));
        j_set(st, "acceptedNames", acc);
        j_set(st, "conditions", j_null());
        json *sv = j_arr();
        json *vers = j_path(obj, "spec.versions");
        for(size_t i = 0; j_is(vers, J_ARR) && i < vers->nitems; i++)
            if(j_getb(vers->items[i], "storage", 0)) j_push(sv, j_clone(j_get(vers->items[i], "name")));
        j_set(st, "storedVersions", sv);
    } else if(!strcmp(K, "HorizontalPodAutoscaler")){
        json *st = j_ensure_obj(obj, "status");
        if(!j_get(st, "desiredReplicas")) j_set(st, "desiredReplicas", j_int(0));
        if(!j_get(st, "currentMetrics")) j_set(st, "currentMetrics", j_null());
    }
}

/* ---------------------------- review endpoints ----------------------------- */
json *review_answer(const char *resource, json *body, const ident *who, const char *ns){
    json *out = j_obj();
    json *spec = j_get(body, "spec");
    /* a SelfSubjectReview carries no spec at all */
    if(!spec && strcmp(resource, "selfsubjectreviews")) return NULL;
    if(spec) j_set(out, "spec", j_clone(spec));
    if(!strcmp(resource, "localsubjectaccessreviews")){
        json *md = j_obj();
        j_set(md, "namespace", j_str(ns ?: ""));
        j_set(out, "metadata", md);
    }
    json *st = j_obj();
    if(!strcmp(resource, "selfsubjectrulesreviews")){
        json *sp = j_get(out, "spec");
        const char *n = sp ? j_gets(sp, "namespace") : NULL;
        char *nc = xstrdup2(n ?: ns ?: "");
        j_free(st);
        st = rbac_rules_review(who, nc);
        free(nc);
        j_set(out, "spec", j_obj());
        j_set(out, "status", st);
        return out;
    }
    if(!strcmp(resource, "tokenreviews")){
        j_set(st, "user", j_obj());
        j_set(out, "status", st);
        return out;
    }
    if(!strcmp(resource, "selfsubjectreviews")){
        json *u = j_obj();
        j_set(u, "username", j_str(who->name ?: ""));
        j_set(u, "uid", j_str(who->uid ?: ""));
        j_set(u, "groups", j_clone(who->groups));
        j_set(st, "userInfo", u);
        j_set(out, "status", st);
        j_del(out, "spec");           /* the review carries no spec back */
        return out;
    }
    /* access reviews */
    ident subject = { (char*)(who->name), who->groups };
    ident tmp;
    int owns = 0;
    if(!strcmp(resource, "subjectaccessreviews") || !strcmp(resource, "localsubjectaccessreviews")){
        tmp.name = (char*)(j_gets(spec, "user") ?: "");
        tmp.groups = j_get(spec, "groups") ? j_clone(j_get(spec, "groups")) : j_arr();
        j_push(tmp.groups, j_str("system:authenticated"));
        subject = tmp;
        owns = 1;
    }
    reqinfo ri = {0};
    json *ra = j_get(spec, "resourceAttributes");
    json *na = j_get(spec, "nonResourceAttributes");
    int allowed;
    if(na){
        ri.verb = (char*)(j_gets(na, "verb") ?: "get");
        allowed = authorize(&subject, &ri, j_gets(na, "path") ?: "/");
    } else if(ra){
        ri.verb = (char*)(j_gets(ra, "verb") ?: "get");
        ri.group = (char*)(j_gets(ra, "group") ?: "");
        ri.resource = (char*)(j_gets(ra, "resource") ?: "");
        ri.sub = (char*)j_gets(ra, "subresource");
        ri.name = (char*)j_gets(ra, "name");
        ri.ns = (char*)(j_gets(ra, "namespace") ?: "");
        allowed = authorize(&subject, &ri, NULL);
    } else allowed = 0;
    if(owns) j_free(subject.groups);
    j_set(st, "allowed", j_bool(allowed));
    if(!allowed) j_set(st, "denied", j_bool(0)), j_del(st, "denied");
    j_set(out, "status", st);
    return out;
}

/* ---------------------------- bootstrap RBAC -------------------------------- */
static void put_if_absent(const char *key, const char *text){
    if(store_get(key)) return;
    json *o = j_parse0(text);
    if(!o) return;
    char *ts = now_rfc3339();
    char *uid = new_uid();
    json *md = j_ensure_obj(o, "metadata");
    j_set(md, "creationTimestamp", j_str(ts));
    j_set(md, "uid", j_str(uid));
    free(ts); free(uid);
    store_create(key, o);
}

void bootstrap_rbac(void){
    put_if_absent("/registry/clusterroles/cluster-admin",
      "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"ClusterRole\","
      "\"metadata\":{\"name\":\"cluster-admin\"},"
      "\"rules\":[{\"apiGroups\":[\"*\"],\"resources\":[\"*\"],\"verbs\":[\"*\"]},"
      "{\"nonResourceURLs\":[\"*\"],\"verbs\":[\"*\"]}]}");
    put_if_absent("/registry/clusterrolebindings/cluster-admin",
      "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"ClusterRoleBinding\","
      "\"metadata\":{\"name\":\"cluster-admin\"},"
      "\"roleRef\":{\"apiGroup\":\"rbac.authorization.k8s.io\",\"kind\":\"ClusterRole\",\"name\":\"cluster-admin\"},"
      "\"subjects\":[{\"apiGroup\":\"rbac.authorization.k8s.io\",\"kind\":\"Group\",\"name\":\"system:masters\"}]}");
    /* 1.27 gave every authenticated user the right to create a SelfSubjectReview. */
    put_if_absent("/registry/clusterroles/system:basic-user",
      RELEASE_MINOR >= 27
      ? "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"ClusterRole\","
        "\"metadata\":{\"name\":\"system:basic-user\"},"
        "\"rules\":[{\"apiGroups\":[\"authorization.k8s.io\"],"
        "\"resources\":[\"selfsubjectaccessreviews\",\"selfsubjectrulesreviews\"],\"verbs\":[\"create\"]},"
        "{\"apiGroups\":[\"authentication.k8s.io\"],"
        "\"resources\":[\"selfsubjectreviews\"],\"verbs\":[\"create\"]}]}"
      : "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"ClusterRole\","
        "\"metadata\":{\"name\":\"system:basic-user\"},"
        "\"rules\":[{\"apiGroups\":[\"authorization.k8s.io\"],"
        "\"resources\":[\"selfsubjectaccessreviews\",\"selfsubjectrulesreviews\"],\"verbs\":[\"create\"]}]}");
    put_if_absent("/registry/clusterrolebindings/system:basic-user",
      "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"ClusterRoleBinding\","
      "\"metadata\":{\"name\":\"system:basic-user\"},"
      "\"roleRef\":{\"apiGroup\":\"rbac.authorization.k8s.io\",\"kind\":\"ClusterRole\",\"name\":\"system:basic-user\"},"
      "\"subjects\":[{\"apiGroup\":\"rbac.authorization.k8s.io\",\"kind\":\"Group\",\"name\":\"system:authenticated\"}]}");
    put_if_absent("/registry/clusterroles/system:discovery",
      "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"ClusterRole\","
      "\"metadata\":{\"name\":\"system:discovery\"},"
      "\"rules\":[{\"nonResourceURLs\":[\"/api\",\"/api/*\",\"/apis\",\"/apis/*\",\"/healthz\","
      "\"/livez\",\"/openapi\",\"/openapi/*\",\"/readyz\",\"/version\",\"/version/\"],\"verbs\":[\"get\"]}]}");
    put_if_absent("/registry/clusterrolebindings/system:discovery",
      "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"ClusterRoleBinding\","
      "\"metadata\":{\"name\":\"system:discovery\"},"
      "\"roleRef\":{\"apiGroup\":\"rbac.authorization.k8s.io\",\"kind\":\"ClusterRole\",\"name\":\"system:discovery\"},"
      "\"subjects\":[{\"apiGroup\":\"rbac.authorization.k8s.io\",\"kind\":\"Group\",\"name\":\"system:authenticated\"}]}");
    put_if_absent("/registry/clusterroles/system:public-info-viewer",
      "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"ClusterRole\","
      "\"metadata\":{\"name\":\"system:public-info-viewer\"},"
      "\"rules\":[{\"nonResourceURLs\":[\"/healthz\",\"/livez\",\"/readyz\",\"/version\",\"/version/\"],"
      "\"verbs\":[\"get\"]}]}");
    put_if_absent("/registry/clusterrolebindings/system:public-info-viewer",
      "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"ClusterRoleBinding\","
      "\"metadata\":{\"name\":\"system:public-info-viewer\"},"
      "\"roleRef\":{\"apiGroup\":\"rbac.authorization.k8s.io\",\"kind\":\"ClusterRole\",\"name\":\"system:public-info-viewer\"},"
      "\"subjects\":[{\"apiGroup\":\"rbac.authorization.k8s.io\",\"kind\":\"Group\",\"name\":\"system:authenticated\"},"
      "{\"apiGroup\":\"rbac.authorization.k8s.io\",\"kind\":\"Group\",\"name\":\"system:unauthenticated\"}]}");
    /* the default namespaces every cluster starts with */
    put_if_absent("/registry/namespaces/default",
      "{\"apiVersion\":\"v1\",\"kind\":\"Namespace\",\"metadata\":{\"name\":\"default\","
      "\"labels\":{\"kubernetes.io/metadata.name\":\"default\"}},"
      "\"spec\":{\"finalizers\":[\"kubernetes\"]},\"status\":{\"phase\":\"Active\"}}");
    put_if_absent("/registry/namespaces/kube-system",
      "{\"apiVersion\":\"v1\",\"kind\":\"Namespace\",\"metadata\":{\"name\":\"kube-system\","
      "\"labels\":{\"kubernetes.io/metadata.name\":\"kube-system\"}},"
      "\"spec\":{\"finalizers\":[\"kubernetes\"]},\"status\":{\"phase\":\"Active\"}}");
    put_if_absent("/registry/namespaces/kube-public",
      "{\"apiVersion\":\"v1\",\"kind\":\"Namespace\",\"metadata\":{\"name\":\"kube-public\","
      "\"labels\":{\"kubernetes.io/metadata.name\":\"kube-public\"}},"
      "\"spec\":{\"finalizers\":[\"kubernetes\"]},\"status\":{\"phase\":\"Active\"}}");
    put_if_absent("/registry/namespaces/kube-node-lease",
      "{\"apiVersion\":\"v1\",\"kind\":\"Namespace\",\"metadata\":{\"name\":\"kube-node-lease\","
      "\"labels\":{\"kubernetes.io/metadata.name\":\"kube-node-lease\"}},"
      "\"spec\":{\"finalizers\":[\"kubernetes\"]},\"status\":{\"phase\":\"Active\"}}");
}

/* ------------------------------ Table output -------------------------------- */
static json *col(const char *name, const char *type, const char *desc, int prio){
    json *c = j_obj();
    j_set(c, "name", j_str(name));
    j_set(c, "type", j_str(type));
    j_set(c, "format", j_str(""));
    j_set(c, "description", j_str(desc ?: ""));
    j_set(c, "priority", j_int(prio));
    return c;
}

static json *age_cell(json *o){
    return j_str("<age>");
}

json *table_for(const char *kind, json *list){
    json *t = j_obj();
    j_set(t, "kind", j_str("Table"));
    j_set(t, "apiVersion", j_str("meta.k8s.io/v1"));
    j_set(t, "metadata", j_clone(j_get(list, "metadata")));
    json *cols = j_arr();
    json *rows = j_arr();
    int is_cm = !strcmp(kind, "ConfigMap");
    j_push(cols, col("Name", "string", "Name must be unique within a namespace.", 0));
    if(is_cm) j_push(cols, col("Data", "string", "", 0));
    j_push(cols, col("Age", "string", "", 0));
    json *items = j_get(list, "items");
    for(size_t i = 0; j_is(items, J_ARR) && i < items->nitems; i++){
        json *o = items->items[i];
        json *row = j_obj();
        json *cells = j_arr();
        j_push(cells, j_str(j_gets(j_get(o, "metadata"), "name") ?: ""));
        if(is_cm){
            json *d = j_get(o, "data");
            json *bd = j_get(o, "binaryData");
            long long n = (j_is(d, J_OBJ) ? (long long)d->nmem : 0) +
                          (j_is(bd, J_OBJ) ? (long long)bd->nmem : 0);
            j_push(cells, j_int(n));
        }
        j_push(cells, age_cell(o));
        j_set(row, "cells", cells);
        json *ro = j_obj();
        j_set(ro, "kind", j_str("PartialObjectMetadata"));
        j_set(ro, "apiVersion", j_str("meta.k8s.io/v1"));
        j_set(ro, "metadata", j_clone(j_get(o, "metadata")));
        j_set(row, "object", ro);
        j_push(rows, row);
    }
    j_set(t, "columnDefinitions", cols);
    j_set(t, "rows", rows);
    return t;
}
