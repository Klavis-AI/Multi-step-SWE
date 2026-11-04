#define _GNU_SOURCE
#include "client.h"
#include "util.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#ifndef KCP_RELEASE
#define KCP_RELEASE 25
#endif
static const int RELEASE_MINOR = KCP_RELEASE;
#define FLOWCONTROL_GV (KCP_RELEASE <= 25 ? "v1beta1" : (KCP_RELEASE <= 28 ? "v1beta3" : "v1"))

static kclient C;

static const char *mname(json *o){ return j_gets(j_get(o, "metadata"), "name"); }
static const char *mns(json *o){ return j_gets(j_get(o, "metadata"), "namespace"); }
static const char *muid(json *o){ return j_gets(j_get(o, "metadata"), "uid"); }

static json *list(const char *path){
    int st = 0;
    json *l = kc_get(&C, path, &st);
    if(st != 200){ j_free(l); return NULL; }
    return l;
}

static unsigned fnv(const char *s){
    unsigned h = 2166136261u;
    for(; *s; s++){ h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}
static void template_hash(json *tmpl, char *out, size_t n){
    char *t = j_dump_sorted(tmpl);
    unsigned h = fnv(t);
    free(t);
    static const char SAFE[] = "bcdfghjklmnpqrstvwxz2456789";
    size_t k = 0;
    for(; k < 10 && k + 1 < n; k++){ out[k] = SAFE[h % 27]; h = h / 27 + h * 7; }
    out[k] = 0;
}

static int selector_matches(json *sel, json *labels){
    json *ml = sel ? j_get(sel, "matchLabels") : NULL;
    if(!j_is(ml, J_OBJ) || !ml->nmem) return 0;
    for(size_t i = 0; i < ml->nmem; i++){
        const char *v = labels ? j_gets(labels, ml->mem[i].key) : NULL;
        if(!v || strcmp(v, j_str_of(ml->mem[i].val) ?: "")) return 0;
    }
    return 1;
}

static json *owner_ref(const char *api, const char *kind, const char *name,
                       const char *uid, int controller){
    json *o = j_obj();
    j_set(o, "apiVersion", j_str(api));
    j_set(o, "kind", j_str(kind));
    j_set(o, "name", j_str(name));
    j_set(o, "uid", j_str(uid ?: ""));
    j_set(o, "controller", j_bool(controller));
    j_set(o, "blockOwnerDeletion", j_bool(1));
    return o;
}

static int owned_by(json *obj, const char *uid){
    json *refs = j_get(j_get(obj, "metadata"), "ownerReferences");
    for(size_t i = 0; j_is(refs, J_ARR) && i < refs->nitems; i++)
        if(!strcmp(j_gets(refs->items[i], "uid") ?: "", uid ?: "")) return 1;
    return 0;
}

static void patch_status(const char *path, json *status){
    json *b = j_obj();
    j_set(b, "status", status);
    int st = 0;
    j_free(kc_send(&C, "PATCH", path, b, "application/merge-patch+json", &st));
}

/* ------------------------------ serviceaccount ------------------------------ */
static void sync_serviceaccounts(void){
    json *nss = list("/api/v1/namespaces");
    json *items = nss ? j_get(nss, "items") : NULL;
    for(size_t i = 0; j_is(items, J_ARR) && i < items->nitems; i++){
        json *ns = items->items[i];
        const char *phase = j_gets(j_get(ns, "status"), "phase");
        if(phase && strcmp(phase, "Active")) continue;
        char p[512];
        snprintf(p, sizeof p, "/api/v1/namespaces/%s/serviceaccounts/default", mname(ns));
        int st = 0;
        json *sa = kc_get(&C, p, &st);
        j_free(sa);
        if(st == 200) continue;
        json *o = j_obj();
        j_set(o, "apiVersion", j_str("v1"));
        j_set(o, "kind", j_str("ServiceAccount"));
        json *md = j_obj();
        j_set(md, "name", j_str("default"));
        j_set(md, "namespace", j_str(mname(ns)));
        j_set(o, "metadata", md);
        snprintf(p, sizeof p, "/api/v1/namespaces/%s/serviceaccounts", mname(ns));
        j_free(kc_send(&C, "POST", p, o, NULL, &st));
    }
    j_free(nss);
}

/* -------------------------------- namespace -------------------------------- */
static const char *NS_RESOURCES[][2] = {
    { "/api/v1/namespaces/%s/pods", "pods" },
    { "/apis/apps/v1/namespaces/%s/deployments", "deployments" },
    { "/apis/apps/v1/namespaces/%s/replicasets", "replicasets" },
    { "/apis/batch/v1/namespaces/%s/jobs", "jobs" },
    { "/api/v1/namespaces/%s/configmaps", "configmaps" },
    { "/api/v1/namespaces/%s/secrets", "secrets" },
    { "/api/v1/namespaces/%s/services", "services" },
    { "/api/v1/namespaces/%s/endpoints", "endpoints" },
    { "/api/v1/namespaces/%s/serviceaccounts", "serviceaccounts" },
    { "/api/v1/namespaces/%s/events", "events" },
    { "/api/v1/namespaces/%s/persistentvolumeclaims", "pvc" },
    { "/apis/discovery.k8s.io/v1/namespaces/%s/endpointslices", "endpointslices" },
    { "/apis/coordination.k8s.io/v1/namespaces/%s/leases", "leases" },
    { "/apis/networking.k8s.io/v1/namespaces/%s/networkpolicies", "netpol" },
    { "/apis/autoscaling/v2/namespaces/%s/horizontalpodautoscalers", "hpa" },
    { "/apis/rbac.authorization.k8s.io/v1/namespaces/%s/roles", "roles" },
    { "/apis/rbac.authorization.k8s.io/v1/namespaces/%s/rolebindings", "rolebindings" },
    { NULL, NULL }
};

static void sync_namespaces(void){
    json *nss = list("/api/v1/namespaces");
    json *items = nss ? j_get(nss, "items") : NULL;
    for(size_t i = 0; j_is(items, J_ARR) && i < items->nitems; i++){
        json *ns = items->items[i];
        json *md = j_get(ns, "metadata");
        if(!j_gets(md, "deletionTimestamp")) continue;
        const char *name = mname(ns);
        const char *phase = j_gets(j_get(ns, "status"), "phase");
        if(!phase || strcmp(phase, "Terminating")){
            char p[512];
            snprintf(p, sizeof p, "/api/v1/namespaces/%s/status", name);
            json *st = j_obj();
            j_set(st, "phase", j_str("Terminating"));
            patch_status(p, st);
        }
        int remaining = 0;
        for(int k = 0; NS_RESOURCES[k][0]; k++){
            char p[512];
            snprintf(p, sizeof p, NS_RESOURCES[k][0], name);
            json *l = list(p);
            json *it = l ? j_get(l, "items") : NULL;
            for(size_t z = 0; j_is(it, J_ARR) && z < it->nitems; z++){
                char one[640];
                snprintf(one, sizeof one, "%s/%s?gracePeriodSeconds=0", p, mname(it->items[z]));
                int st = 0;
                j_free(kc_send(&C, "DELETE", one, NULL, NULL, &st));
                remaining++;
            }
            j_free(l);
        }
        json *fin = j_get(j_get(ns, "spec"), "finalizers");
        int has_kube = 0, others = 0;
        for(size_t z = 0; j_is(fin, J_ARR) && z < fin->nitems; z++){
            const char *f = j_str_of(fin->items[z]);
            if(f && !strcmp(f, "kubernetes")) has_kube = 1; else others++;
        }
        if(!remaining && has_kube){
            json *body = j_obj();
            j_set(body, "apiVersion", j_str("v1"));
            j_set(body, "kind", j_str("Namespace"));
            json *m2 = j_obj();
            j_set(m2, "name", j_str(name));
            j_set(body, "metadata", m2);
            json *sp = j_obj();
            json *keep = j_arr();
            for(size_t z = 0; j_is(fin, J_ARR) && z < fin->nitems; z++){
                const char *f = j_str_of(fin->items[z]);
                if(f && strcmp(f, "kubernetes")) j_push(keep, j_str(f));
            }
            j_set(sp, "finalizers", keep);
            j_set(body, "spec", sp);
            char p[512];
            snprintf(p, sizeof p, "/api/v1/namespaces/%s/finalize", name);
            int st = 0;
            j_free(kc_send(&C, "PUT", p, body, NULL, &st));
            if(!others){
                snprintf(p, sizeof p, "/api/v1/namespaces/%s", name);
                j_free(kc_send(&C, "DELETE", p, NULL, NULL, &st));
            }
        }
    }
    j_free(nss);
}

/* ------------------------------- replicaset -------------------------------- */
static void make_pod_from_template2(json *tmpl, const char *ns, const char *gen,
                                    json *owner, const char *path, const char *finalizer);
static void make_pod_from_template(json *tmpl, const char *ns, const char *gen,
                                   json *owner, const char *path){
    make_pod_from_template2(tmpl, ns, gen, owner, path, NULL);
}
static void make_pod_from_template2(json *tmpl, const char *ns, const char *gen,
                                    json *owner, const char *path, const char *finalizer){
    json *pod = j_obj();
    j_set(pod, "apiVersion", j_str("v1"));
    j_set(pod, "kind", j_str("Pod"));
    json *md = j_clone(j_get(tmpl, "metadata"));
    if(!md || md->t != J_OBJ){ j_free(md); md = j_obj(); }
    if(finalizer){
        json *f = j_arr();
        j_push(f, j_str(finalizer));
        j_set(md, "finalizers", f);
    }
    j_set(md, "generateName", j_str(gen));
    j_set(md, "namespace", j_str(ns));
    json *refs = j_arr();
    j_push(refs, j_clone(owner));
    j_set(md, "ownerReferences", refs);
    j_set(pod, "metadata", md);
    j_set(pod, "spec", j_clone(j_get(tmpl, "spec")));
    int st = 0;
    j_free(kc_send(&C, "POST", path, pod, NULL, &st));
}

static void sync_replicasets(void){
    json *l = list("/apis/apps/v1/replicasets");
    json *items = l ? j_get(l, "items") : NULL;
    for(size_t i = 0; j_is(items, J_ARR) && i < items->nitems; i++){
        json *rs = items->items[i];
        if(j_gets(j_get(rs, "metadata"), "deletionTimestamp")) continue;
        const char *ns = mns(rs), *name = mname(rs), *uid = muid(rs);
        json *spec = j_get(rs, "spec");
        long long want = j_geti(spec, "replicas", 0);
        json *sel = j_get(spec, "selector");
        char p[512];
        snprintf(p, sizeof p, "/api/v1/namespaces/%s/pods", ns);
        json *pl = list(p);
        json *pods = pl ? j_get(pl, "items") : NULL;
        json *mine = j_arr();
        for(size_t k = 0; j_is(pods, J_ARR) && k < pods->nitems; k++){
            json *pod = pods->items[k];
            if(j_gets(j_get(pod, "metadata"), "deletionTimestamp")) continue;
            json *labels = j_get(j_get(pod, "metadata"), "labels");
            if(!selector_matches(sel, labels)) continue;
            json *refs = j_get(j_get(pod, "metadata"), "ownerReferences");
            int has_other = 0, ours = owned_by(pod, uid);
            for(size_t z = 0; j_is(refs, J_ARR) && z < refs->nitems; z++)
                if(j_getb(refs->items[z], "controller", 0) &&
                   strcmp(j_gets(refs->items[z], "uid") ?: "", uid ?: "")) has_other = 1;
            if(has_other) continue;
            if(!ours){
                /* adopt the orphan */
                json *patch = j_obj();
                json *m = j_obj();
                json *r2 = j_arr();
                j_push(r2, owner_ref("apps/v1", "ReplicaSet", name, uid, 1));
                j_set(m, "ownerReferences", r2);
                j_set(patch, "metadata", m);
                char one[640];
                snprintf(one, sizeof one, "%s/%s", p, mname(pod));
                int st = 0;
                j_free(kc_send(&C, "PATCH", one, patch, "application/merge-patch+json", &st));
            }
            j_push(mine, j_clone(pod));
        }
        long long have = (long long)mine->nitems;
        if(have < want){
            json *ow = owner_ref("apps/v1", "ReplicaSet", name, uid, 1);
            char gen[320];
            snprintf(gen, sizeof gen, "%s-", name);
            for(long long k = have; k < want; k++)
                make_pod_from_template(j_get(spec, "template"), ns, gen, ow, p);
            j_free(ow);
        } else if(have > want){
            for(long long k = want; k < have; k++){
                char one[640];
                snprintf(one, sizeof one, "%s/%s", p, mname(mine->items[k]));
                int st = 0;
                j_free(kc_send(&C, "DELETE", one, NULL, NULL, &st));
            }
        }
        json *status = j_get(rs, "status");
        long long cur = have < want ? have : want;
        if(j_geti(status, "replicas", -1) != cur ||
           j_geti(status, "observedGeneration", -1) != j_geti(j_get(rs, "metadata"), "generation", 0)){
            char sp[640];
            snprintf(sp, sizeof sp, "/apis/apps/v1/namespaces/%s/replicasets/%s/status", ns, name);
            json *st = j_obj();
            j_set(st, "replicas", j_int(cur));
            j_set(st, "fullyLabeledReplicas", j_int(cur));
            j_set(st, "observedGeneration", j_int(j_geti(j_get(rs, "metadata"), "generation", 0)));
            patch_status(sp, st);
        }
        j_free(mine);
        j_free(pl);
    }
    j_free(l);
}

/* ------------------------------- deployment -------------------------------- */
static void sync_deployments(void){
    json *l = list("/apis/apps/v1/deployments");
    json *items = l ? j_get(l, "items") : NULL;
    for(size_t i = 0; j_is(items, J_ARR) && i < items->nitems; i++){
        json *dep = items->items[i];
        if(j_gets(j_get(dep, "metadata"), "deletionTimestamp")) continue;
        const char *ns = mns(dep), *name = mname(dep), *uid = muid(dep);
        json *spec = j_get(dep, "spec");
        json *tmpl = j_get(spec, "template");
        char hash[16];
        template_hash(tmpl, hash, sizeof hash);
        char rsname[320];
        snprintf(rsname, sizeof rsname, "%s-%s", name, hash);
        char p[512];
        snprintf(p, sizeof p, "/apis/apps/v1/namespaces/%s/replicasets", ns);
        json *rl = list(p);
        json *rss = rl ? j_get(rl, "items") : NULL;
        json *mine = NULL;
        long long other = 0;
        for(size_t k = 0; j_is(rss, J_ARR) && k < rss->nitems; k++){
            if(!owned_by(rss->items[k], uid)) continue;
            if(!strcmp(mname(rss->items[k]), rsname)) mine = rss->items[k];
            else other++;
        }
        long long want = j_geti(spec, "replicas", 1);
        if(!mine){
            json *rs = j_obj();
            j_set(rs, "apiVersion", j_str("apps/v1"));
            j_set(rs, "kind", j_str("ReplicaSet"));
            json *md = j_obj();
            j_set(md, "name", j_str(rsname));
            j_set(md, "namespace", j_str(ns));
            json *labels = j_clone(j_get(j_get(tmpl, "metadata"), "labels"));
            if(!labels) labels = j_obj();
            j_set(labels, "pod-template-hash", j_str(hash));
            j_set(md, "labels", labels);
            json *ann = j_obj();
            j_set(ann, "deployment.kubernetes.io/revision", j_str("1"));
            {
                char b2[32];
                snprintf(b2, sizeof b2, "%lld", want);
                j_set(ann, "deployment.kubernetes.io/desired-replicas", j_str(b2));
                long long surge = (want * 25 + 99) / 100;     /* maxSurge 25%, rounded up */
                json *ru = j_path(spec, "strategy.rollingUpdate");
                const char *ms = ru ? j_gets(ru, "maxSurge") : NULL;
                if(ms){
                    char *end;
                    long v = strtol(ms, &end, 10);
                    surge = (*end == '%') ? (want * v + 99) / 100 : v;
                }
                snprintf(b2, sizeof b2, "%lld", want + surge);
                j_set(ann, "deployment.kubernetes.io/max-replicas", j_str(b2));
            }
            j_set(md, "annotations", ann);
            json *refs = j_arr();
            j_push(refs, owner_ref("apps/v1", "Deployment", name, uid, 1));
            j_set(md, "ownerReferences", refs);
            j_set(rs, "metadata", md);
            json *rspec = j_obj();
            j_set(rspec, "replicas", j_int(want));
            json *sel = j_clone(j_get(spec, "selector"));
            json *ml = j_ensure_obj(sel, "matchLabels");
            j_set(ml, "pod-template-hash", j_str(hash));
            j_set(rspec, "selector", sel);
            json *t2 = j_clone(tmpl);
            json *tl = j_ensure_obj(j_ensure_obj(t2, "metadata"), "labels");
            j_set(tl, "pod-template-hash", j_str(hash));
            j_set(rspec, "template", t2);
            j_set(rs, "spec", rspec);
            int st = 0;
            j_free(kc_send(&C, "POST", p, rs, NULL, &st));
        } else if(j_geti(j_get(mine, "spec"), "replicas", -1) != want){
            char one[640];
            snprintf(one, sizeof one, "%s/%s", p, rsname);
            json *patch = j_obj();
            json *sp = j_obj();
            j_set(sp, "replicas", j_int(want));
            j_set(patch, "spec", sp);
            int st = 0;
            j_free(kc_send(&C, "PATCH", one, patch, "application/merge-patch+json", &st));
        }
        /* scale every superseded ReplicaSet to zero */
        for(size_t k = 0; j_is(rss, J_ARR) && k < rss->nitems; k++){
            if(!owned_by(rss->items[k], uid)) continue;
            if(!strcmp(mname(rss->items[k]), rsname)) continue;
            if(j_geti(j_get(rss->items[k], "spec"), "replicas", 0) == 0) continue;
            char one[640];
            snprintf(one, sizeof one, "%s/%s", p, mname(rss->items[k]));
            json *patch = j_obj();
            json *sp = j_obj();
            j_set(sp, "replicas", j_int(0));
            j_set(patch, "spec", sp);
            int st = 0;
            j_free(kc_send(&C, "PATCH", one, patch, "application/merge-patch+json", &st));
        }
        {
            json *dann = j_get(j_get(dep, "metadata"), "annotations");
            if(!dann || !j_gets(dann, "deployment.kubernetes.io/revision")){
                json *patch = j_obj();
                json *m = j_obj();
                json *a2 = j_obj();
                j_set(a2, "deployment.kubernetes.io/revision", j_str("1"));
                j_set(m, "annotations", a2);
                j_set(patch, "metadata", m);
                char one[640];
                snprintf(one, sizeof one, "/apis/apps/v1/namespaces/%s/deployments/%s", ns, name);
                int st2 = 0;
                j_free(kc_send(&C, "PATCH", one, patch, "application/merge-patch+json", &st2));
            }
        }
        long long ready = mine ? j_geti(j_get(mine, "status"), "replicas", 0) : 0;
        json *dst = j_get(dep, "status");
        if(j_geti(dst, "replicas", -1) != ready ||
           j_geti(dst, "observedGeneration", -1) != j_geti(j_get(dep, "metadata"), "generation", 0)){
            char sp[640];
            snprintf(sp, sizeof sp, "/apis/apps/v1/namespaces/%s/deployments/%s/status", ns, name);
            json *st = j_obj();
            j_set(st, "replicas", j_int(ready));
            j_set(st, "updatedReplicas", j_int(ready));
            j_set(st, "unavailableReplicas", j_int(ready));
            j_set(st, "observedGeneration", j_int(j_geti(j_get(dep, "metadata"), "generation", 0)));
            char *ts = now_rfc3339();
            json *conds = j_arr();
            json *c1 = j_obj();
            j_set(c1, "type", j_str("Available"));
            j_set(c1, "status", j_str("False"));
            j_set(c1, "reason", j_str("MinimumReplicasUnavailable"));
            j_set(c1, "message", j_str("Deployment does not have minimum availability."));
            j_set(c1, "lastUpdateTime", j_str(ts));
            j_set(c1, "lastTransitionTime", j_str(ts));
            j_push(conds, c1);
            json *c2 = j_obj();
            j_set(c2, "type", j_str("Progressing"));
            j_set(c2, "status", j_str("True"));
            j_set(c2, "reason", j_str("ReplicaSetUpdated"));
            {
                char *m = xasprintf("ReplicaSet \"%s\" is progressing.", rsname);
                j_set(c2, "message", j_str(m));
                free(m);
            }
            j_set(c2, "lastUpdateTime", j_str(ts));
            j_set(c2, "lastTransitionTime", j_str(ts));
            j_push(conds, c2);
            free(ts);
            j_set(st, "conditions", conds);
            patch_status(sp, st);
        }
        j_free(rl);
    }
    j_free(l);
}

/* ----------------------------------- job ----------------------------------- */
static void sync_jobs(void){
    json *l = list("/apis/batch/v1/jobs");
    json *items = l ? j_get(l, "items") : NULL;
    for(size_t i = 0; j_is(items, J_ARR) && i < items->nitems; i++){
        json *job = items->items[i];
        if(j_gets(j_get(job, "metadata"), "deletionTimestamp")) continue;
        const char *ns = mns(job), *name = mname(job), *uid = muid(job);
        json *spec = j_get(job, "spec");
        if(j_getb(spec, "suspend", 0)) continue;
        long long completions = j_geti(spec, "completions", 1);
        long long parallelism = j_geti(spec, "parallelism", 1);
        char p[512];
        snprintf(p, sizeof p, "/api/v1/namespaces/%s/pods", ns);
        json *pl = list(p);
        json *pods = pl ? j_get(pl, "items") : NULL;
        long long active = 0, succeeded = 0, failed = 0;
        for(size_t k = 0; j_is(pods, J_ARR) && k < pods->nitems; k++){
            json *pod = pods->items[k];
            if(!owned_by(pod, uid)) continue;
            const char *ph = j_gets(j_get(pod, "status"), "phase");
            if(ph && !strcmp(ph, "Succeeded")) succeeded++;
            else if(ph && !strcmp(ph, "Failed")) failed++;
            else if(!j_gets(j_get(pod, "metadata"), "deletionTimestamp")) active++;
        }
        long long done = succeeded;
        long long need = completions - done - active;
        if(need > parallelism - active) need = parallelism - active;
        if(need > 0){
            json *ow = owner_ref("batch/v1", "Job", name, uid, 1);
            char gen[320];
            snprintf(gen, sizeof gen, "%s-", name);
            for(long long k = 0; k < need; k++)
                make_pod_from_template2(j_get(spec, "template"), ns, gen, ow, p,
                                        "batch.kubernetes.io/job-tracking");
            j_free(ow);
            active += need;
        }
        json *st = j_obj();
        j_set(st, "ready", j_int(0));
        /* 1.29 added the count of pods on their way out */
        if(RELEASE_MINOR >= 29) j_set(st, "terminating", j_int(0));
        if(!j_get(j_get(job, "status"), "uncountedTerminatedPods"))
            j_set(st, "uncountedTerminatedPods", j_obj());
        if(!j_gets(j_get(job, "status"), "startTime")){
            char *ts = now_rfc3339();
            j_set(st, "startTime", j_str(ts));
            free(ts);
        }
        if(active) j_set(st, "active", j_int(active));
        if(succeeded) j_set(st, "succeeded", j_int(succeeded));
        if(failed) j_set(st, "failed", j_int(failed));
        if(succeeded >= completions){
            json *conds = j_arr();
            char *ts = now_rfc3339();
            /* 1.31 records the success criteria separately from completion, and
               gives both conditions a reason and a message. */
            if(RELEASE_MINOR >= 31){
                json *sc = j_obj();
                j_set(sc, "type", j_str("SuccessCriteriaMet"));
                j_set(sc, "status", j_str("True"));
                j_set(sc, "reason", j_str("CompletionsReached"));
                j_set(sc, "message", j_str("Reached expected number of succeeded pods"));
                j_set(sc, "lastProbeTime", j_str(ts));
                j_set(sc, "lastTransitionTime", j_str(ts));
                j_push(conds, sc);
            }
            json *cc = j_obj();
            j_set(cc, "type", j_str("Complete"));
            j_set(cc, "status", j_str("True"));
            if(RELEASE_MINOR >= 31){
                j_set(cc, "reason", j_str("CompletionsReached"));
                j_set(cc, "message", j_str("Reached expected number of succeeded pods"));
            }
            j_set(cc, "lastProbeTime", j_str(ts));
            j_set(cc, "lastTransitionTime", j_str(ts));
            free(ts);
            j_push(conds, cc);
            j_set(st, "conditions", conds);
            j_set(st, "active", j_null());
        }
        json *cur = j_get(job, "status");
        json *cmp = j_clone(cur ? cur : j_obj());
        int changed = 0;
        for(size_t z = 0; z < st->nmem; z++){
            json *have = j_get(cmp, st->mem[z].key);
            if(st->mem[z].val->t == J_NULL){ if(have) changed = 1; continue; }
            if(!have || !j_equal(have, st->mem[z].val)) changed = 1;
        }
        j_free(cmp);
        if(changed){
            char sp[640];
            snprintf(sp, sizeof sp, "/apis/batch/v1/namespaces/%s/jobs/%s/status", ns, name);
            patch_status(sp, st);
        } else j_free(st);
        j_free(pl);
    }
    j_free(l);
}

/* --------------------------- garbage collection ---------------------------- */
static const char *GC_RESOURCES[][2] = {
    { "/api/v1/pods", "/api/v1/namespaces/%s/pods/%s" },
    { "/apis/apps/v1/replicasets", "/apis/apps/v1/namespaces/%s/replicasets/%s" },
    { "/apis/batch/v1/jobs", "/apis/batch/v1/namespaces/%s/jobs/%s" },
    { "/apis/discovery.k8s.io/v1/endpointslices",
      "/apis/discovery.k8s.io/v1/namespaces/%s/endpointslices/%s" },
    { NULL, NULL }
};

static int owner_exists(json *ref, const char *ns){
    const char *kind = j_gets(ref, "kind");
    const char *api = j_gets(ref, "apiVersion");
    const char *name = j_gets(ref, "name");
    const char *uid = j_gets(ref, "uid");
    if(!kind || !name) return 1;
    char p[640];
    if(!strcmp(kind, "ReplicaSet")) snprintf(p, sizeof p, "/apis/apps/v1/namespaces/%s/replicasets/%s", ns, name);
    else if(!strcmp(kind, "Deployment")) snprintf(p, sizeof p, "/apis/apps/v1/namespaces/%s/deployments/%s", ns, name);
    else if(!strcmp(kind, "Job")) snprintf(p, sizeof p, "/apis/batch/v1/namespaces/%s/jobs/%s", ns, name);
    else if(!strcmp(kind, "Service")) snprintf(p, sizeof p, "/api/v1/namespaces/%s/services/%s", ns, name);
    else if(!strcmp(kind, "Pod")) snprintf(p, sizeof p, "/api/v1/namespaces/%s/pods/%s", ns, name);
    else return 1;
    (void)api;
    int st = 0;
    json *o = kc_get(&C, p, &st);
    int ok = (st == 200) && (!uid || !strcmp(j_gets(j_get(o, "metadata"), "uid") ?: "", uid));
    j_free(o);
    return ok;
}

static void sync_gc(void){
    /* dependents whose owner is gone */
    for(int i = 0; GC_RESOURCES[i][0]; i++){
        json *l = list(GC_RESOURCES[i][0]);
        json *items = l ? j_get(l, "items") : NULL;
        for(size_t k = 0; j_is(items, J_ARR) && k < items->nitems; k++){
            json *o = items->items[k];
            json *refs = j_get(j_get(o, "metadata"), "ownerReferences");
            if(!j_is(refs, J_ARR) || !refs->nitems) continue;
            int gone = 0;
            for(size_t z = 0; z < refs->nitems; z++)
                if(!owner_exists(refs->items[z], mns(o))) gone = 1;
            if(!gone) continue;
            char p[768];
            snprintf(p, sizeof p, GC_RESOURCES[i][1], mns(o), mname(o));
            strcat(p, "?gracePeriodSeconds=0");
            int st = 0;
            j_free(kc_send(&C, "DELETE", p, NULL, NULL, &st));
        }
        j_free(l);
    }
    /* objects held by the orphan finalizer: strip ownerReferences, then release */
    const char *owners[][2] = {
        { "/apis/apps/v1/deployments", "/apis/apps/v1/namespaces/%s/deployments/%s" },
        { "/apis/batch/v1/jobs", "/apis/batch/v1/namespaces/%s/jobs/%s" },
        { NULL, NULL }
    };
    for(int i = 0; owners[i][0]; i++){
        json *l = list(owners[i][0]);
        json *items = l ? j_get(l, "items") : NULL;
        for(size_t k = 0; j_is(items, J_ARR) && k < items->nitems; k++){
            json *o = items->items[k];
            json *md = j_get(o, "metadata");
            if(!j_gets(md, "deletionTimestamp")) continue;
            json *fin = j_get(md, "finalizers");
            int orphan = 0;
            for(size_t z = 0; j_is(fin, J_ARR) && z < fin->nitems; z++)
                if(!strcmp(j_str_of(fin->items[z]) ?: "", "orphan")) orphan = 1;
            if(!orphan) continue;
            const char *uid = muid(o);
            for(int r = 0; GC_RESOURCES[r][0]; r++){
                json *dl = list(GC_RESOURCES[r][0]);
                json *di = dl ? j_get(dl, "items") : NULL;
                for(size_t z = 0; j_is(di, J_ARR) && z < di->nitems; z++){
                    if(!owned_by(di->items[z], uid)) continue;
                    json *patch = j_obj();
                    json *m = j_obj();
                    j_set(m, "ownerReferences", j_null());
                    j_set(patch, "metadata", m);
                    char p[768];
                    snprintf(p, sizeof p, GC_RESOURCES[r][1], mns(di->items[z]), mname(di->items[z]));
                    int st = 0;
                    j_free(kc_send(&C, "PATCH", p, patch, "application/merge-patch+json", &st));
                }
                j_free(dl);
            }
            json *patch = j_obj();
            json *m = j_obj();
            j_set(m, "finalizers", j_arr());
            j_set(patch, "metadata", m);
            char p[768];
            snprintf(p, sizeof p, owners[i][1], mns(o), mname(o));
            int st = 0;
            j_free(kc_send(&C, "PATCH", p, patch, "application/merge-patch+json", &st));
        }
        j_free(l);
    }
}

/* --------------------------- endpoints / slices ---------------------------- */
static int pod_ready(json *pod){
    json *conds = j_get(j_get(pod, "status"), "conditions");
    for(size_t i = 0; j_is(conds, J_ARR) && i < conds->nitems; i++)
        if(!strcmp(j_gets(conds->items[i], "type") ?: "", "Ready"))
            return !strcmp(j_gets(conds->items[i], "status") ?: "", "True");
    return 0;
}

static long long resolve_target(json *pod, json *sp){
    json *tp = j_get(sp, "targetPort");
    if(tp && tp->t == J_NUM) return (long long)tp->n;
    const char *nm = tp ? j_str_of(tp) : NULL;
    if(!nm) return j_geti(sp, "port", 0);
    json *cs = j_get(j_get(pod, "spec"), "containers");
    for(size_t i = 0; j_is(cs, J_ARR) && i < cs->nitems; i++){
        json *ports = j_get(cs->items[i], "ports");
        for(size_t k = 0; j_is(ports, J_ARR) && k < ports->nitems; k++)
            if(!strcmp(j_gets(ports->items[k], "name") ?: "", nm))
                return j_geti(ports->items[k], "containerPort", 0);
    }
    return 0;
}

static void sync_endpoints(void){
    json *l = list("/api/v1/services");
    json *items = l ? j_get(l, "items") : NULL;
    for(size_t i = 0; j_is(items, J_ARR) && i < items->nitems; i++){
        json *svc = items->items[i];
        json *sel = j_get(j_get(svc, "spec"), "selector");
        if(!j_is(sel, J_OBJ) || !sel->nmem) continue;
        const char *ns = mns(svc), *name = mname(svc);
        char p[512];
        snprintf(p, sizeof p, "/api/v1/namespaces/%s/pods", ns);
        json *pl = list(p);
        json *pods = pl ? j_get(pl, "items") : NULL;
        json *ready = j_arr(), *notready = j_arr();
        json *fake = j_obj();
        j_set(fake, "matchLabels", j_clone(sel));
        for(size_t k = 0; j_is(pods, J_ARR) && k < pods->nitems; k++){
            json *pod = pods->items[k];
            if(!selector_matches(fake, j_get(j_get(pod, "metadata"), "labels"))) continue;
            const char *ip = j_gets(j_get(pod, "status"), "podIP");
            if(!ip || !*ip) continue;
            if(j_gets(j_get(pod, "metadata"), "deletionTimestamp")) continue;
            j_push(pod_ready(pod) ? ready : notready, j_clone(pod));
        }
        j_free(fake);
        json *ports = j_get(j_get(svc, "spec"), "ports");
        json *subsets = j_arr();
        if(ready->nitems || notready->nitems){
            json *ss = j_obj();
            json *addrs = j_arr(), *naddrs = j_arr(), *sports = j_arr();
            json *lists[2] = { ready, notready };
            json *outs[2] = { addrs, naddrs };
            for(int z = 0; z < 2; z++)
                for(size_t k = 0; k < lists[z]->nitems; k++){
                    json *pod = lists[z]->items[k];
                    json *a = j_obj();
                    j_set(a, "ip", j_clone(j_get(j_get(pod, "status"), "podIP")));
                    const char *nn = j_gets(j_get(pod, "spec"), "nodeName");
                    if(nn) j_set(a, "nodeName", j_str(nn));
                    json *tr = j_obj();
                    j_set(tr, "kind", j_str("Pod"));
                    j_set(tr, "namespace", j_str(ns));
                    j_set(tr, "name", j_str(mname(pod)));
                    j_set(tr, "uid", j_str(muid(pod) ?: ""));
                    j_set(a, "targetRef", tr);
                    j_push(outs[z], a);
                }
            for(size_t k = 0; j_is(ports, J_ARR) && k < ports->nitems; k++){
                json *sp = ports->items[k];
                json *e = j_obj();
                const char *pn = j_gets(sp, "name");
                if(pn) j_set(e, "name", j_str(pn));
                json *ref = ready->nitems ? ready->items[0] : notready->items[0];
                j_set(e, "port", j_int(resolve_target(ref, sp)));
                j_set(e, "protocol", j_str(j_gets(sp, "protocol") ?: "TCP"));
                j_push(sports, e);
            }
            if(addrs->nitems) j_set(ss, "addresses", addrs); else j_free(addrs);
            if(naddrs->nitems) j_set(ss, "notReadyAddresses", naddrs); else j_free(naddrs);
            j_set(ss, "ports", sports);
            j_push(subsets, ss);
        }
        char ep[640];
        snprintf(ep, sizeof ep, "/api/v1/namespaces/%s/endpoints/%s", ns, name);
        int st = 0;
        json *cur = kc_get(&C, ep, &st);
        json *body = j_obj();
        j_set(body, "apiVersion", j_str("v1"));
        j_set(body, "kind", j_str("Endpoints"));
        json *md = j_obj();
        j_set(md, "name", j_str(name));
        j_set(md, "namespace", j_str(ns));
        json *labels = j_obj();
        j_set(labels, "service.kubernetes.io/headless", j_str(""));
        j_del(labels, "service.kubernetes.io/headless");
        j_set(md, "labels", labels);
        j_set(body, "metadata", md);
        if(subsets->nitems) j_set(body, "subsets", subsets); else j_free(subsets);
        if(st == 200){
            json *have = j_get(cur, "subsets");
            json *want = j_get(body, "subsets");
            if(j_equal(have, want)){ j_free(body); j_free(cur); goto slices; }
            snprintf(ep, sizeof ep, "/api/v1/namespaces/%s/endpoints/%s", ns, name);
            j_free(kc_send(&C, "PUT", ep, body, NULL, &st));
        } else {
            snprintf(ep, sizeof ep, "/api/v1/namespaces/%s/endpoints", ns);
            j_free(kc_send(&C, "POST", ep, body, NULL, &st));
        }
        j_free(cur);
    slices: ;
        /* the matching EndpointSlice */
        char slp[640];
        snprintf(slp, sizeof slp, "/apis/discovery.k8s.io/v1/namespaces/%s/endpointslices"
                 "?labelSelector=kubernetes.io%%2Fservice-name%%3D%s", ns, name);
        json *sl = list(slp);
        json *sit = sl ? j_get(sl, "items") : NULL;
        json *eps = j_arr();
        json *lists2[2] = { ready, notready };
        for(int z = 0; z < 2; z++)
            for(size_t k = 0; k < lists2[z]->nitems; k++){
                json *pod = lists2[z]->items[k];
                json *e = j_obj();
                json *a = j_arr();
                j_push(a, j_clone(j_get(j_get(pod, "status"), "podIP")));
                j_set(e, "addresses", a);
                json *cond = j_obj();
                j_set(cond, "ready", j_bool(z == 0));
                j_set(cond, "serving", j_bool(z == 0));
                j_set(cond, "terminating", j_bool(0));
                j_set(e, "conditions", cond);
                json *tr = j_obj();
                j_set(tr, "kind", j_str("Pod"));
                j_set(tr, "namespace", j_str(ns));
                j_set(tr, "name", j_str(mname(pod)));
                j_set(tr, "uid", j_str(muid(pod) ?: ""));
                j_set(e, "targetRef", tr);
                const char *nn = j_gets(j_get(pod, "spec"), "nodeName");
                if(nn) j_set(e, "nodeName", j_str(nn));
                j_push(eps, e);
            }
        json *sports = j_arr();
        for(size_t k = 0; j_is(ports, J_ARR) && k < ports->nitems; k++){
            json *sp = ports->items[k];
            json *e = j_obj();
            const char *pn = j_gets(sp, "name");
            j_set(e, "name", pn ? j_str(pn) : j_str(""));
            json *ref = ready->nitems ? ready->items[0] : (notready->nitems ? notready->items[0] : NULL);
            j_set(e, "port", j_int(ref ? resolve_target(ref, sp) : j_geti(sp, "port", 0)));
            j_set(e, "protocol", j_str(j_gets(sp, "protocol") ?: "TCP"));
            j_push(sports, e);
        }
        json *slice = j_obj();
        j_set(slice, "apiVersion", j_str("discovery.k8s.io/v1"));
        j_set(slice, "kind", j_str("EndpointSlice"));
        json *smd = j_obj();
        char sname[400];
        snprintf(sname, sizeof sname, "%s-kcp01", name);
        j_set(smd, "name", j_str(sname));
        j_set(smd, "namespace", j_str(ns));
        json *slabels = j_obj();
        j_set(slabels, "endpointslice.kubernetes.io/managed-by", j_str("endpointslice-controller.k8s.io"));
        j_set(slabels, "kubernetes.io/service-name", j_str(name));
        j_set(smd, "labels", slabels);
        json *srefs = j_arr();
        json *sref = owner_ref("v1", "Service", name, muid(svc), 1);
        j_del(sref, "controller");
        j_set(sref, "controller", j_bool(1));
        j_push(srefs, sref);
        j_set(smd, "ownerReferences", srefs);
        j_set(slice, "metadata", smd);
        j_set(slice, "addressType", j_str("IPv4"));
        j_set(slice, "endpoints", eps);
        j_set(slice, "ports", sports);
        if(sit && j_is(sit, J_ARR) && sit->nitems){
            json *ex = sit->items[0];
            if(!j_equal(j_get(ex, "endpoints"), j_get(slice, "endpoints")) ||
               !j_equal(j_get(ex, "ports"), j_get(slice, "ports"))){
                char one[768];
                snprintf(one, sizeof one, "/apis/discovery.k8s.io/v1/namespaces/%s/endpointslices/%s",
                         ns, mname(ex));
                j_set(j_get(slice, "metadata"), "name", j_str(mname(ex)));
                j_free(kc_send(&C, "PUT", one, slice, NULL, &st));
            } else j_free(slice);
        } else if(eps->nitems){
            char one[768];
            snprintf(one, sizeof one, "/apis/discovery.k8s.io/v1/namespaces/%s/endpointslices", ns);
            j_free(kc_send(&C, "POST", one, slice, NULL, &st));
        } else j_free(slice);
        j_free(sl);
        j_free(ready); j_free(notready);
        j_free(pl);
    }
    j_free(l);
}

static void sync_crds(void){
    json *l = list("/apis/apiextensions.k8s.io/v1/customresourcedefinitions");
    json *items = l ? j_get(l, "items") : NULL;
    for(size_t i = 0; j_is(items, J_ARR) && i < items->nitems; i++){
        json *crd = items->items[i];
        json *st = j_get(crd, "status");
        json *acc = st ? j_get(st, "acceptedNames") : NULL;
        const char *k = acc ? j_gets(acc, "kind") : NULL;
        if(k && *k) continue;
        json *names = j_path(crd, "spec.names");
        json *nacc = j_obj();
        for(const char **f = (const char*[]){"plural", "singular", "kind", "listKind", NULL}; *f; f++)
            if(names && j_gets(names, *f)) j_set(nacc, *f, j_str(j_gets(names, *f)));
        json *sn = names ? j_get(names, "shortNames") : NULL;
        if(sn) j_set(nacc, "shortNames", j_clone(sn));
        json *cat = names ? j_get(names, "categories") : NULL;
        if(cat) j_set(nacc, "categories", j_clone(cat));
        char *ts = now_rfc3339();
        json *conds = j_arr();
        json *c1 = j_obj();
        j_set(c1, "type", j_str("NamesAccepted"));
        j_set(c1, "status", j_str("True"));
        j_set(c1, "reason", j_str("NoConflicts"));
        j_set(c1, "message", j_str("no conflicts found"));
        j_set(c1, "lastTransitionTime", j_str(ts));
        j_push(conds, c1);
        json *c2 = j_obj();
        j_set(c2, "type", j_str("Established"));
        j_set(c2, "status", j_str("True"));
        j_set(c2, "reason", j_str("InitialNamesAccepted"));
        j_set(c2, "message", j_str("the initial names have been accepted"));
        j_set(c2, "lastTransitionTime", j_str(ts));
        j_push(conds, c2);
        free(ts);
        json *stored = j_arr();
        json *vers = j_path(crd, "spec.versions");
        for(size_t z = 0; j_is(vers, J_ARR) && z < vers->nitems; z++)
            if(j_getb(vers->items[z], "storage", 0))
                j_push(stored, j_clone(j_get(vers->items[z], "name")));
        json *nst = j_obj();
        j_set(nst, "acceptedNames", nacc);
        j_set(nst, "conditions", conds);
        j_set(nst, "storedVersions", stored);
        char p[640];
        snprintf(p, sizeof p, "/apis/apiextensions.k8s.io/v1/customresourcedefinitions/%s/status",
                 mname(crd));
        patch_status(p, nst);
    }
    j_free(l);
}

static void sync_flowschemas(const char *gv){
    char base[256];
    snprintf(base, sizeof base, "/apis/flowcontrol.apiserver.k8s.io/%s/flowschemas", gv);
    json *l = list(base);
    json *items = l ? j_get(l, "items") : NULL;
    for(size_t i = 0; j_is(items, J_ARR) && i < items->nitems; i++){
        json *fs = items->items[i];
        json *st = j_get(fs, "status");
        if(st && j_is(j_get(st, "conditions"), J_ARR) && j_get(st, "conditions")->nitems) continue;
        const char *pln = j_gets(j_path(fs, "spec.priorityLevelConfiguration"), "name");
        char p[640];
        snprintf(p, sizeof p, "/apis/flowcontrol.apiserver.k8s.io/%s/prioritylevelconfigurations/%s",
                 gv, pln ?: "");
        int code = 0;
        json *plc = kc_get(&C, p, &code);
        j_free(plc);
        char *ts = now_rfc3339();
        json *conds = j_arr();
        json *c1 = j_obj();
        j_set(c1, "type", j_str("Dangling"));
        j_set(c1, "status", j_str(code == 200 ? "False" : "True"));
        j_set(c1, "reason", j_str(code == 200 ? "Found" : "NotFound"));
        char *m = code == 200
            ? xasprintf("This FlowSchema references the PriorityLevelConfiguration object named "
                        "\"%s\" and it exists", pln ?: "")
            : xasprintf("This FlowSchema references the PriorityLevelConfiguration object named "
                        "\"%s\" but there is no such object", pln ?: "");
        j_set(c1, "message", j_str(m));
        free(m);
        j_set(c1, "lastTransitionTime", j_str(ts));
        free(ts);
        j_push(conds, c1);
        json *nst = j_obj();
        j_set(nst, "conditions", conds);
        snprintf(p, sizeof p, "%s/%s/status", base, mname(fs));
        patch_status(p, nst);
    }
    j_free(l);
}

int main(int argc, char **argv){
    signal(SIGPIPE, SIG_IGN);
    const char *kubeconfig = NULL;
    for(int i = 1; i < argc; i++){
        if(str_has_prefix(argv[i], "--kubeconfig=")) kubeconfig = argv[i] + 13;
        else if(!strcmp(argv[i], "--kubeconfig") && i + 1 < argc) kubeconfig = argv[++i];
        else if(str_has_prefix(argv[i], "--cloud-provider=")){
            const char *v = argv[i] + 17;
            if(*v && RELEASE_MINOR >= 31){
                fprintf(stderr, "cloud provider %s is no longer available\n", v);
                return 1;
            }
        }
    }
    if(!kubeconfig){ fprintf(stderr, "--kubeconfig is required\n"); return 1; }
    if(kc_from_kubeconfig(&C, kubeconfig) < 0){ fprintf(stderr, "bad kubeconfig\n"); return 1; }
    fprintf(stderr, "kube-controller-manager started\n");
    for(;;){
        sync_serviceaccounts();
        sync_deployments();
        sync_replicasets();
        sync_jobs();
        sync_endpoints();
        sync_gc();
        sync_crds();
        sync_flowschemas(FLOWCONTROL_GV);
        sync_namespaces();
        usleep(400000);
    }
}
