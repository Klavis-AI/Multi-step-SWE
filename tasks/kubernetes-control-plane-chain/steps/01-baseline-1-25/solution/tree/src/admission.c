#define _GNU_SOURCE
#include <time.h>
#include "api.h"
#include "cel.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static json *cause(const char *reason, const char *field){
    json *c = j_obj();
    if(reason) j_set(c, "reason", j_str(reason));
    if(field) j_set(c, "field", j_str(field));
    return c;
}

/* ---------------------------- PodSecurity (GA in 1.25) ---------------------- */
static const char *DEFAULT_CAPS[] = {
    "AUDIT_WRITE","CHOWN","DAC_OVERRIDE","FOWNER","FSETID","KILL","MKNOD",
    "NET_BIND_SERVICE","SETFCAP","SETGID","SETPCAP","SETUID","SYS_CHROOT", NULL
};
static int cap_is_default(const char *c){
    for(int i = 0; DEFAULT_CAPS[i]; i++) if(!strcmp(DEFAULT_CAPS[i], c)) return 1;
    return 0;
}

static json *all_containers(json *spec){
    json *out = j_arr();
    const char *keys[] = {"initContainers", "containers", "ephemeralContainers", NULL};
    for(int i = 0; keys[i]; i++){
        json *l = j_get(spec, keys[i]);
        for(size_t k = 0; j_is(l, J_ARR) && k < l->nitems; k++) j_push(out, j_clone(l->items[k]));
    }
    return out;
}

static int ps_baseline_violation(json *spec){
    if(j_getb(spec, "hostNetwork", 0) || j_getb(spec, "hostPID", 0) || j_getb(spec, "hostIPC", 0))
        return 1;
    json *vols = j_get(spec, "volumes");
    for(size_t i = 0; j_is(vols, J_ARR) && i < vols->nitems; i++)
        if(j_get(vols->items[i], "hostPath")) return 1;
    json *sc = j_get(spec, "securityContext");
    if(sc){
        json *sy = j_get(sc, "sysctls");
        if(j_is(sy, J_ARR) && sy->nitems) return 1;
        json *se = j_get(sc, "seLinuxOptions");
        if(se && (j_gets(se, "type") || j_gets(se, "user") || j_gets(se, "role"))) return 1;
        json *sp = j_get(sc, "seccompProfile");
        const char *t = sp ? j_gets(sp, "type") : NULL;
        if(t && !strcmp(t, "Unconfined")) return 1;
    }
    json *cs = all_containers(spec);
    int bad = 0;
    for(size_t i = 0; i < cs->nitems && !bad; i++){
        json *c = cs->items[i];
        json *csc = j_get(c, "securityContext");
        if(csc){
            if(j_getb(csc, "privileged", 0)) bad = 1;
            json *caps = j_get(csc, "capabilities");
            json *add = caps ? j_get(caps, "add") : NULL;
            for(size_t k = 0; j_is(add, J_ARR) && k < add->nitems; k++){
                const char *cap = j_str_of(add->items[k]);
                if(cap && !cap_is_default(cap)) bad = 1;
            }
            json *sp = j_get(csc, "seccompProfile");
            const char *t = sp ? j_gets(sp, "type") : NULL;
            if(t && !strcmp(t, "Unconfined")) bad = 1;
            json *pm = j_get(csc, "procMount");
            if(pm && j_str_of(pm) && strcmp(j_str_of(pm), "Default")) bad = 1;
        }
        json *ports = j_get(c, "ports");
        for(size_t k = 0; j_is(ports, J_ARR) && k < ports->nitems; k++)
            if(j_get(ports->items[k], "hostPort")) bad = 1;
    }
    j_free(cs);
    return bad;
}

static int ps_restricted_violation(json *spec){
    if(ps_baseline_violation(spec)) return 1;
    json *psc = j_get(spec, "securityContext");
    json *psp = psc ? j_get(psc, "seccompProfile") : NULL;
    const char *pst = psp ? j_gets(psp, "type") : NULL;
    int pod_seccomp_ok = pst && (!strcmp(pst, "RuntimeDefault") || !strcmp(pst, "Localhost"));
    int pod_nonroot = psc ? j_getb(psc, "runAsNonRoot", 0) : 0;
    json *vols = j_get(spec, "volumes");
    static const char *OKV[] = {"configMap","csi","downwardAPI","emptyDir","ephemeral",
                                "persistentVolumeClaim","projected","secret", NULL};
    for(size_t i = 0; j_is(vols, J_ARR) && i < vols->nitems; i++){
        json *v = vols->items[i];
        int ok = 0;
        for(size_t m = 0; m < v->nmem; m++){
            if(!strcmp(v->mem[m].key, "name")) continue;
            for(int k = 0; OKV[k]; k++) if(!strcmp(v->mem[m].key, OKV[k])) ok = 1;
        }
        if(!ok) return 1;
    }
    json *cs = all_containers(spec);
    int bad = 0;
    for(size_t i = 0; i < cs->nitems && !bad; i++){
        json *c = cs->items[i];
        json *csc = j_get(c, "securityContext");
        if(!csc){ bad = 1; break; }
        json *ape = j_get(csc, "allowPrivilegeEscalation");
        if(!ape || ape->t != J_BOOL || ape->b) bad = 1;
        json *caps = j_get(csc, "capabilities");
        json *drop = caps ? j_get(caps, "drop") : NULL;
        int dropall = 0;
        for(size_t k = 0; j_is(drop, J_ARR) && k < drop->nitems; k++){
            const char *d = j_str_of(drop->items[k]);
            if(d && !strcmp(d, "ALL")) dropall = 1;
        }
        if(!dropall) bad = 1;
        json *add = caps ? j_get(caps, "add") : NULL;
        for(size_t k = 0; j_is(add, J_ARR) && k < add->nitems; k++){
            const char *cap = j_str_of(add->items[k]);
            if(cap && strcmp(cap, "NET_BIND_SERVICE")) bad = 1;
        }
        json *nr = j_get(csc, "runAsNonRoot");
        if(!(pod_nonroot || (nr && nr->t == J_BOOL && nr->b))) bad = 1;
        json *ru = j_get(csc, "runAsUser");
        if(ru && ru->t == J_NUM && ru->n == 0) bad = 1;
        json *sp = j_get(csc, "seccompProfile");
        const char *t = sp ? j_gets(sp, "type") : NULL;
        int cseccomp = t && (!strcmp(t, "RuntimeDefault") || !strcmp(t, "Localhost"));
        if(!(pod_seccomp_ok || cseccomp)) bad = 1;
    }
    j_free(cs);
    return bad;
}

/* --------------------------------- plugins ---------------------------------- */
static json *ns_obj(const char *name){
    char key[512];
    snprintf(key, sizeof key, "/registry/namespaces/%s", name);
    sobj *o = store_get(key);
    return o ? o->obj : NULL;
}

static void add_toleration(json *spec, const char *keyname){
    json *tol = j_get(spec, "tolerations");
    if(!j_is(tol, J_ARR)){ tol = j_arr(); j_set(spec, "tolerations", tol); }
    for(size_t i = 0; i < tol->nitems; i++){
        const char *k = j_gets(tol->items[i], "key");
        const char *e = j_gets(tol->items[i], "effect");
        if(k && !strcmp(k, keyname) && (!e || !strcmp(e, "NoExecute"))) return;
        if(!k && j_gets(tol->items[i], "operator") &&
           !strcmp(j_gets(tol->items[i], "operator"), "Exists") && !e) return;
    }
    json *t = j_obj();
    j_set(t, "key", j_str(keyname));
    j_set(t, "operator", j_str("Exists"));
    j_set(t, "effect", j_str("NoExecute"));
    j_set(t, "tolerationSeconds", j_int(300));
    j_push(tol, t);
}

/* ------------------------- ValidatingAdmissionPolicy ------------------------ */
static int vap_matches(json *mc, const reqinfo *ri, const char *verb){
    json *rules = mc ? j_get(mc, "resourceRules") : NULL;
    if(!j_is(rules, J_ARR) || !rules->nitems) return 0;
    for(size_t i = 0; i < rules->nitems; i++){
        json *r = rules->items[i];
        json *g = j_get(r, "apiGroups"), *v = j_get(r, "apiVersions");
        json *res = j_get(r, "resources"), *ops = j_get(r, "operations");
        int gm = 0, vm = 0, rm = 0, om = 0;
        for(size_t k = 0; j_is(g, J_ARR) && k < g->nitems; k++){
            const char *x = j_str_of(g->items[k]);
            if(x && (!strcmp(x, "*") || !strcmp(x, ri->group ?: ""))) gm = 1;
        }
        for(size_t k = 0; j_is(v, J_ARR) && k < v->nitems; k++){
            const char *x = j_str_of(v->items[k]);
            if(x && (!strcmp(x, "*") || !strcmp(x, ri->version ?: ""))) vm = 1;
        }
        for(size_t k = 0; j_is(res, J_ARR) && k < res->nitems; k++){
            const char *x = j_str_of(res->items[k]);
            if(x && (!strcmp(x, "*") || !strcmp(x, ri->resource ?: ""))) rm = 1;
        }
        for(size_t k = 0; j_is(ops, J_ARR) && k < ops->nitems; k++){
            const char *x = j_str_of(ops->items[k]);
            if(!x) continue;
            if(!strcmp(x, "*")) om = 1;
            else if(!strcmp(x, "CREATE") && !strcmp(verb, "create")) om = 1;
            else if(!strcmp(x, "UPDATE") && (!strcmp(verb, "update") || !strcmp(verb, "patch"))) om = 1;
            else if(!strcmp(x, "DELETE") && !strcmp(verb, "delete")) om = 1;
        }
        if(gm && vm && rm && om) return 1;
    }
    return 0;
}

static int selector_on_ns(json *sel, json *nsobj){
    if(!sel || !j_is(sel, J_OBJ) || (!j_get(sel, "matchLabels") && !j_get(sel, "matchExpressions")))
        return 1;
    json *ml = j_get(sel, "matchLabels");
    json *labels = nsobj ? j_get(j_get(nsobj, "metadata"), "labels") : NULL;
    for(size_t i = 0; j_is(ml, J_OBJ) && i < ml->nmem; i++){
        const char *have = labels ? j_gets(labels, ml->mem[i].key) : NULL;
        if(!have || strcmp(have, j_str_of(ml->mem[i].val) ?: "")) return 0;
    }
    return 1;
}

typedef struct { const reqinfo *ri; json *obj, *old; const ident *who; int denied;
                 char *why; } vapctx;

static int vap_bindings_cb(sobj *o, void *ud);

static int vap_policy_cb(sobj *o, void *ud){
    vapctx *V = ud;
    if(V->denied) return 0;
    json *pol = o->obj;
    json *spec = j_get(pol, "spec");
    if(!vap_matches(j_get(spec, "matchConstraints"), V->ri, V->ri->verb)) return 1;
    /* is any binding pointing at this policy, and does it select our namespace? */
    const char *pname = j_gets(j_get(pol, "metadata"), "name");
    json *nsobj = NULL;
    if(V->ri->ns && *V->ri->ns){
        char key[512];
        snprintf(key, sizeof key, "/registry/namespaces/%s", V->ri->ns);
        sobj *n = store_get(key);
        nsobj = n ? n->obj : NULL;
    }
    struct { const char *pname; json *nsobj; int bound; } B = { pname, nsobj, 0 };
    store_each("/registry/validatingadmissionpolicybindings/",
               vap_bindings_cb, &B);
    if(!B.bound) return 1;
    json *act = j_obj();
    j_set(act, "object", j_clone(V->obj));
    j_set(act, "oldObject", V->old ? j_clone(V->old) : j_null());
    json *req = j_obj();
    j_set(req, "namespace", j_str(V->ri->ns ?: ""));
    j_set(req, "name", j_str(V->ri->name ?: ""));
    j_set(req, "operation", j_str(!strcmp(V->ri->verb, "create") ? "CREATE" : "UPDATE"));
    j_set(act, "request", req);
    j_set(act, "params", j_null());
    if(nsobj) j_set(act, "namespaceObject", j_clone(nsobj));
    /* variables, evaluated in order */
    json *vars = j_get(spec, "variables");
    json *vmap = j_obj();
    for(size_t i = 0; j_is(vars, J_ARR) && i < vars->nitems; i++){
        const char *nm = j_gets(vars->items[i], "name");
        const char *ex = j_gets(vars->items[i], "expression");
        if(!nm || !ex) continue;
        j_set(act, "variables", j_clone(vmap));
        char *e = NULL;
        json *v = cel_eval(ex, act, &e);
        free(e);
        if(v) j_set(vmap, nm, v);
    }
    j_set(act, "variables", vmap);
    /* matchConditions gate the validations */
    json *mcs = j_get(spec, "matchConditions");
    int apply = 1;
    for(size_t i = 0; j_is(mcs, J_ARR) && i < mcs->nitems && apply; i++){
        const char *ex = j_gets(mcs->items[i], "expression");
        char *e = NULL;
        json *v = ex ? cel_eval(ex, act, &e) : NULL;
        if(!v || v->t != J_BOOL || !v->b) apply = 0;
        j_free(v); free(e);
    }
    if(apply){
        json *vals = j_get(spec, "validations");
        for(size_t i = 0; j_is(vals, J_ARR) && i < vals->nitems; i++){
            const char *ex = j_gets(vals->items[i], "expression");
            char *e = NULL;
            json *v = ex ? cel_eval(ex, act, &e) : NULL;
            int ok = v && v->t == J_BOOL && v->b;
            if(!v){
                const char *fp = j_gets(spec, "failurePolicy");
                ok = fp && !strcmp(fp, "Ignore");
            }
            j_free(v); free(e);
            if(!ok){
                V->denied = 1;
                V->why = xasprintf("ValidatingAdmissionPolicy '%s' denied request",
                                   pname ?: "");
                break;
            }
        }
    }
    j_free(act);
    return !V->denied;
}

static int vap_bindings_cb(sobj *o, void *ud){
    struct { const char *pname; json *nsobj; int bound; } *B = ud;
    json *spec = j_get(o->obj, "spec");
    const char *pn = spec ? j_gets(spec, "policyName") : NULL;
    if(!pn || !B->pname || strcmp(pn, B->pname)) return 1;
    json *va = spec ? j_get(spec, "validationActions") : NULL;
    int deny = 0;
    for(size_t i = 0; j_is(va, J_ARR) && i < va->nitems; i++)
        if(!strcmp(j_str_of(va->items[i]) ?: "", "Deny")) deny = 1;
    if(!deny) return 1;
    json *mr = spec ? j_get(spec, "matchResources") : NULL;
    if(!selector_on_ns(mr ? j_get(mr, "namespaceSelector") : NULL, B->nsobj)) return 1;
    B->bound = 1;
    return 0;
}

json *admit(const reqinfo *ri, json *obj, json *old, const ident *who, int *code,
            const char **reason, char **msg){
    *code = 0;
    const char *res = ri->resource;
    /* NamespaceLifecycle */
    if(ri->rd && ri->rd->namespaced && ri->ns && *ri->ns &&
       (!strcmp(ri->verb, "create") || !strcmp(ri->verb, "update") || !strcmp(ri->verb, "patch"))){
        json *n = ns_obj(ri->ns);
        if(!n){
            *code = 404; *reason = "NotFound";
            *msg = xasprintf("namespaces \"%s\" not found", ri->ns);
            ADMIT_DETAIL_KIND = "namespaces";
            ADMIT_DETAIL_NAME = xstrdup2(ri->ns);
            return NULL;
        }
        const char *phase = j_gets(j_get(n, "status"), "phase");
        if(!strcmp(ri->verb, "create") && phase && !strcmp(phase, "Terminating")){
            *code = 403; *reason = "Forbidden";
            *msg = xasprintf("unable to create new content in namespace %s because it is being terminated", ri->ns);
            json *cs = j_arr();
            json *cc = j_obj();
            j_set(cc, "reason", j_str("NamespaceTerminating"));
            j_set(cc, "field", j_str("metadata.namespace"));
            j_push(cs, cc);
            return cs;
        }
    }
    if(!obj) return NULL;
    json *spec = j_get(obj, "spec");

    /* Pod-specific plugins */
    if(!strcmp(res, "pods") && !ri->sub && spec){
        if(!strcmp(ri->verb, "create")){
            const char *san = j_gets(spec, "serviceAccountName");
            if(!san || !*san){ j_set(spec, "serviceAccountName", j_str("default")); san = "default"; }
            j_set(spec, "serviceAccount", j_str(san));
            char key[512];
            snprintf(key, sizeof key, "/registry/serviceaccounts/%s/%s", ri->ns, san);
            sobj *sa = store_get(key);
            /* Upstream's ServiceAccount plugin re-reads a missing "default"
               account rather than failing at once, because the controller
               creates it moments after the namespace appears. */
            if(!sa && !strcmp(san, "default")){
                for(int att = 0; att < 40 && !sa; att++){
                    store_unlock();
                    struct timespec ts = {0, 150L * 1000000L};
                    nanosleep(&ts, NULL);
                    store_lock();
                    sa = store_get(key);
                }
            }
            if(!sa){
                *code = 403; *reason = "Forbidden";
                *msg = xasprintf("error looking up service account %s/%s: serviceaccount \"%s\" not found",
                                 ri->ns, san, san);
                return NULL;
            }
            int automount = 1;
            json *am = j_get(sa->obj, "automountServiceAccountToken");
            if(am && am->t == J_BOOL) automount = am->b;
            json *pam = j_get(spec, "automountServiceAccountToken");
            if(pam && pam->t == J_BOOL) automount = pam->b;
            if(automount){
                char sfx[8]; rand_suffix(sfx, 5);
                char *vname = xasprintf("kube-api-access-%s", sfx);
                json *vols = j_get(spec, "volumes");
                if(!j_is(vols, J_ARR)){ vols = j_arr(); j_set(spec, "volumes", vols); }
                json *v = j_obj();
                j_set(v, "name", j_str(vname));
                json *proj = j_obj();
                json *sources = j_arr();
                json *s1 = j_obj();
                json *sat = j_obj();
                j_set(sat, "expirationSeconds", j_int(3607));
                j_set(sat, "path", j_str("token"));
                j_set(s1, "serviceAccountToken", sat);
                j_push(sources, s1);
                json *s2 = j_obj();
                json *cm = j_obj();
                j_set(cm, "name", j_str("kube-root-ca.crt"));
                json *cmitems = j_arr();
                json *it = j_obj();
                j_set(it, "key", j_str("ca.crt"));
                j_set(it, "path", j_str("ca.crt"));
                j_push(cmitems, it);
                j_set(cm, "items", cmitems);
                j_set(s2, "configMap", cm);
                j_push(sources, s2);
                json *s3 = j_obj();
                json *dw = j_obj();
                json *dwitems = j_arr();
                json *di = j_obj();
                j_set(di, "path", j_str("namespace"));
                json *fr = j_obj();
                j_set(fr, "apiVersion", j_str("v1"));
                j_set(fr, "fieldPath", j_str("metadata.namespace"));
                j_set(di, "fieldRef", fr);
                j_push(dwitems, di);
                j_set(dw, "items", dwitems);
                j_set(s3, "downwardAPI", dw);
                j_push(sources, s3);
                j_set(proj, "sources", sources);
                j_set(proj, "defaultMode", j_int(420));
                j_set(v, "projected", proj);
                j_push(vols, v);
                const char *ck[] = {"initContainers", "containers", "ephemeralContainers", NULL};
                for(int i = 0; ck[i]; i++){
                    json *l = j_get(spec, ck[i]);
                    for(size_t k = 0; j_is(l, J_ARR) && k < l->nitems; k++){
                        json *c = l->items[k];
                        json *vm = j_get(c, "volumeMounts");
                        if(!j_is(vm, J_ARR)){ vm = j_arr(); j_set(c, "volumeMounts", vm); }
                        json *m = j_obj();
                        j_set(m, "name", j_str(vname));
                        j_set(m, "readOnly", j_bool(1));
                        j_set(m, "mountPath", j_str("/var/run/secrets/kubernetes.io/serviceaccount"));
                        j_push(vm, m);
                    }
                }
                free(vname);
            }
            /* Priority */
            const char *pcn = j_gets(spec, "priorityClassName");
            long long prio = 0;
            if(pcn && *pcn){
                char pk[512];
                snprintf(pk, sizeof pk, "/registry/priorityclasses/%s", pcn);
                sobj *pc = store_get(pk);
                if(!pc){
                    *code = 403; *reason = "Forbidden";
                    *msg = xasprintf("pods \"%s\" is forbidden: no PriorityClass with name %s was found",
                                     meta_str(obj, "name") ?: "", pcn);
                    return NULL;
                }
                prio = j_geti(pc->obj, "value", 0);
            }
            if(!j_get(spec, "priority")) j_set(spec, "priority", j_int(prio));
            /* DefaultTolerationSeconds */
            add_toleration(spec, "node.kubernetes.io/not-ready");
            add_toleration(spec, "node.kubernetes.io/unreachable");
        }
        /* PodSecurity */
        if(!strcmp(ri->verb, "create") || !strcmp(ri->verb, "update")){
            json *n = ns_obj(ri->ns);
            const char *enforce = NULL;
            if(n){
                json *labels = j_get(j_get(n, "metadata"), "labels");
                if(labels) enforce = j_gets(labels, "pod-security.kubernetes.io/enforce");
            }
            if(enforce && (!strcmp(enforce, "baseline") || !strcmp(enforce, "restricted"))){
                int bad = !strcmp(enforce, "restricted") ? ps_restricted_violation(spec)
                                                         : ps_baseline_violation(spec);
                if(bad){
                    *code = 403; *reason = "Forbidden";
                    *msg = xasprintf("pods \"%s\" is forbidden: violates PodSecurity \"%s:latest\"",
                                     meta_str(obj, "name") ?: "", enforce);
                    return NULL;
                }
            }
        }
    }
    /* StorageObjectInUseProtection */
    if(!strcmp(ri->verb, "create") && !ri->sub){
        const char *fin = NULL;
        if(!strcmp(res, "persistentvolumeclaims")) fin = "kubernetes.io/pvc-protection";
        else if(!strcmp(res, "persistentvolumes")) fin = "kubernetes.io/pv-protection";
        if(fin){
            json *md = meta(obj);
            json *f = j_get(md, "finalizers");
            if(!j_is(f, J_ARR)){ f = j_arr(); j_set(md, "finalizers", f); }
            int have = 0;
            for(size_t i = 0; i < f->nitems; i++){
                const char *s = j_str_of(f->items[i]);
                if(s && !strcmp(s, fin)) have = 1;
            }
            if(!have) j_push(f, j_str(fin));
        }
    }
    /* ValidatingAdmissionPolicy (CEL). The objects are served from 1.26, but a
       policy is only enforced once the feature is on by default, at 1.30. */
    if(RELEASE_MINOR >= 30 && obj &&
       (!strcmp(ri->verb, "create") || !strcmp(ri->verb, "update") || !strcmp(ri->verb, "patch"))){
        vapctx V = { ri, obj, old, who, 0, NULL };
        store_each("/registry/validatingadmissionpolicies/", vap_policy_cb, &V);
        if(V.denied){
            /* a policy denial is reported as an Invalid status carrying one
               cause per failed validation */
            *code = 422; *reason = "Invalid";
            *msg = V.why;
            json *cs = j_arr();
            j_push(cs, cause(NULL, NULL));
            return cs;
        }
    }
    /* RBAC escalation checks on Role / ClusterRole writes */
    if((!strcmp(res, "roles") || !strcmp(res, "clusterroles")) &&
       (!strcmp(ri->verb, "create") || !strcmp(ri->verb, "update"))){
        extern int rbac_covers(const ident *, const char *, json *);
        if(!rbac_covers(who, ri->ns, j_get(obj, "rules"))){
            reqinfo esc = *ri;
            esc.verb = (char*)"escalate";
            if(!authorize(who, &esc, NULL)){
                *code = 403; *reason = "Forbidden";
                *msg = xasprintf("user %s (groups=...) is attempting to grant RBAC permissions not currently held",
                                 who->name ?: "");
                return NULL;
            }
        }
    }
    return NULL;
}
