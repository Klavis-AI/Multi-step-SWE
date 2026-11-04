#define _GNU_SOURCE
#include <arpa/inet.h>
#include "api.h"
#include "cel.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =============================================================================
 * Resource registry.  The served surface, including every discovery attribute,
 * is loaded from the release's API-surface document; this table adds the two
 * things discovery does not carry: the etcd key prefix and the version an
 * object is canonicalised to before it is stored.
 * ========================================================================== */
static const struct { const char *group, *resource, *prefix, *canon; } STORAGE[] = {
    { "",  "pods",                     "pods",                   "v1" },
    { "",  "configmaps",               "configmaps",             "v1" },
    { "",  "secrets",                  "secrets",                "v1" },
    { "",  "serviceaccounts",          "serviceaccounts",        "v1" },
    { "",  "services",                 "services/specs",         "v1" },
    { "",  "endpoints",                "services/endpoints",     "v1" },
    { "",  "namespaces",               "namespaces",             "v1" },
    { "",  "nodes",                    "minions",                "v1" },
    { "",  "events",                   "events",                 "v1" },
    { "",  "persistentvolumeclaims",   "persistentvolumeclaims", "v1" },
    { "",  "bindings",                 "pods",                   "v1" },
    { "apps", "deployments",           "deployments",            "v1" },
    { "apps", "replicasets",           "replicasets",            "v1" },
    { "batch", "jobs",                 "jobs",                   "v1" },
    { "rbac.authorization.k8s.io", "roles",               "roles",               "v1" },
    { "rbac.authorization.k8s.io", "rolebindings",        "rolebindings",        "v1" },
    { "rbac.authorization.k8s.io", "clusterroles",        "clusterroles",        "v1" },
    { "rbac.authorization.k8s.io", "clusterrolebindings", "clusterrolebindings", "v1" },
    { "autoscaling", "horizontalpodautoscalers", "horizontalpodautoscalers", "v2" },
    { "flowcontrol.apiserver.k8s.io", "flowschemas", "flowschemas", "*newest" },
    { "flowcontrol.apiserver.k8s.io", "prioritylevelconfigurations",
      "prioritylevelconfigurations", "*newest" },
    { "apiextensions.k8s.io", "customresourcedefinitions",
      "apiextensions.k8s.io/customresourcedefinitions", "v1" },
    { "discovery.k8s.io", "endpointslices", "endpointslices", "v1" },
    { "coordination.k8s.io", "leases", "leases", "v1" },
    { "networking.k8s.io", "networkpolicies", "networkpolicies", "v1" },
    { "scheduling.k8s.io", "priorityclasses", "priorityclasses", "v1" },
    { "storage.k8s.io", "storageclasses", "storageclasses", "v1" },
    { "storage.k8s.io", "csistoragecapacities", "csistoragecapacities", "v1beta1" },
    { "storage.k8s.io", "volumeattributesclasses", "volumeattributesclasses", "*newest" },
    { "admissionregistration.k8s.io", "validatingwebhookconfigurations",
      "validatingwebhookconfigurations", "v1" },
    { "admissionregistration.k8s.io", "validatingadmissionpolicies",
      "validatingadmissionpolicies", "*newest" },
    { "admissionregistration.k8s.io", "validatingadmissionpolicybindings",
      "validatingadmissionpolicybindings", "*newest" },
    { "networking.k8s.io", "servicecidrs", "servicecidrs", "*newest" },
    { "networking.k8s.io", "ipaddresses", "ipaddresses", "*newest" },
    { "coordination.k8s.io", "leasecandidates", "leasecandidates", "*newest" },
    { NULL, NULL, NULL, NULL }
};

/* Resources that exist only as a request/response pair (never stored). */
static const char *EPHEMERAL[] = {
    "selfsubjectaccessreviews", "subjectaccessreviews", "localsubjectaccessreviews",
    "selfsubjectrulesreviews", "tokenreviews", "selfsubjectreviews", NULL
};
int res_is_ephemeral(const char *r){
    for(int i = 0; EPHEMERAL[i]; i++) if(!strcmp(EPHEMERAL[i], r)) return 1;
    return 0;
}

static resdef *RES; static int NRES;

static const char *storage_for(const char *g, const char *r, const char **canon){
    for(int i = 0; STORAGE[i].resource; i++)
        if(!strcmp(STORAGE[i].group, g) && !strcmp(STORAGE[i].resource, r)){
            *canon = STORAGE[i].canon;
            return STORAGE[i].prefix;
        }
    *canon = NULL;
    return NULL;
}

static int rank(const char *v){
    int maj = 0, min = 0, kind = 3;
    if(sscanf(v, "v%dalpha%d", &maj, &min) == 2) kind = 1;
    else if(sscanf(v, "v%dbeta%d", &maj, &min) == 2) kind = 2;
    else if(sscanf(v, "v%d", &maj) == 1){ kind = 3; min = 0; }
    return kind * 100000 + maj * 1000 + (kind == 3 ? 0 : (999 - min));
}

void schema_init(void){
    SURFACE = j_parse0(embedded_surface());
    if(!SURFACE){ fprintf(stderr, "surface blob is not valid JSON\n"); exit(1); }
    const char *buckets[] = {"groupVersions", "optionalGroupVersions", NULL};
    for(int b = 0; buckets[b]; b++){
        json *m = j_get(SURFACE, buckets[b]);
        for(size_t i = 0; m && i < m->nmem; i++){
            const char *gv = m->mem[i].key;
            char group[192] = "", version[64];
            const char *slash = strchr(gv, '/');
            if(slash){
                size_t n = (size_t)(slash - gv);
                memcpy(group, gv, n); group[n] = 0;
                snprintf(version, sizeof version, "%s", slash + 1);
            } else snprintf(version, sizeof version, "%s", gv);
            json *list = m->mem[i].val;
            for(size_t k = 0; j_is(list, J_ARR) && k < list->nitems; k++){
                json *e = list->items[k];
                const char *name = j_gets(e, "name");
                if(!name || strchr(name, '/')) continue;   /* subresources handled below */
                RES = realloc(RES, (size_t)(NRES + 1) * sizeof(resdef));
                resdef *rd = &RES[NRES++];
                memset(rd, 0, sizeof *rd);
                rd->group = xstrdup2(group);
                rd->version = xstrdup2(version);
                rd->resource = xstrdup2(name);
                rd->kind = xstrdup2(j_gets(e, "kind") ?: "");
                rd->singular = xstrdup2(j_gets(e, "singularName") ?: "");
                rd->namespaced = j_getb(e, "namespaced", 0);
                rd->verbs = j_clone(j_get(e, "verbs"));
                rd->shortnames = j_clone(j_get(e, "shortNames"));
                rd->categories = j_clone(j_get(e, "categories"));
                rd->subresources = j_arr();
                rd->optional = (b == 1);
                const char *canon = NULL;
                const char *pfx = storage_for(group, name, &canon);
                rd->prefix = xstrdup2(pfx ?: name);
                if(canon && !strcmp(canon, "*newest")){
                    /* the newest served version of this group wins */
                    const char *best = version;
                    for(int b2 = 0; buckets[b2]; b2++){
                        json *m2 = j_get(SURFACE, buckets[b2]);
                        for(size_t z = 0; m2 && z < m2->nmem; z++){
                            const char *gv2 = m2->mem[z].key;
                            const char *s2 = strchr(gv2, '/');
                            if(!s2 || strncmp(gv2, group, strlen(group)) || s2 != gv2 + strlen(group))
                                continue;
                            if(rank(s2 + 1) > rank(best)) best = s2 + 1;
                        }
                    }
                    rd->canon = xstrdup2(best);
                } else rd->canon = xstrdup2(canon ?: version);
            }
            /* attach subresources */
            for(size_t k = 0; j_is(list, J_ARR) && k < list->nitems; k++){
                const char *name = j_gets(list->items[k], "name");
                const char *slash2 = name ? strchr(name, '/') : NULL;
                if(!slash2) continue;
                char parent[128];
                size_t n = (size_t)(slash2 - name);
                memcpy(parent, name, n); parent[n] = 0;
                resdef *rd = res_lookup(group, version, parent);
                if(rd) j_push(rd->subresources, j_str(slash2 + 1));
            }
        }
    }
}

resdef *res_lookup(const char *group, const char *version, const char *resource){
    for(int i = 0; i < NRES; i++)
        if(!strcmp(RES[i].group, group) && !strcmp(RES[i].version, version) &&
           !strcmp(RES[i].resource, resource)) return &RES[i];
    return NULL;
}
resdef *res_by_kind(const char *group, const char *version, const char *kind){
    for(int i = 0; i < NRES; i++)
        if(!strcmp(RES[i].group, group) && !strcmp(RES[i].version, version) &&
           !strcmp(RES[i].kind, kind)) return &RES[i];
    return NULL;
}
int has_sub(resdef *rd, const char *sub){
    for(size_t i = 0; i < rd->subresources->nitems; i++)
        if(!strcmp(j_str_of(rd->subresources->items[i]), sub)) return 1;
    return 0;
}

/* =============================================================================
 * Defaulting.  Each kind carries a table of "path -> value"; a path segment of
 * [*] iterates an existing array.  Intermediate objects are created only before
 * the first [*]; after one, the path must already exist.
 * ========================================================================== */
typedef struct { const char *kind; const char *table; } kdefaults;

static const kdefaults DEFAULTS[] = {
{ "Pod", "{"
  "\"spec.restartPolicy\":\"Always\","
  "\"spec.dnsPolicy\":\"ClusterFirst\","
  "\"spec.terminationGracePeriodSeconds\":30,"
  "\"spec.securityContext\":{},"
  "\"spec.schedulerName\":\"default-scheduler\","
  "\"spec.enableServiceLinks\":true,"
  "\"spec.preemptionPolicy\":\"PreemptLowerPriority\","
  "\"spec.containers[*].resources\":{},"
  "\"spec.containers[*].terminationMessagePath\":\"/dev/termination-log\","
  "\"spec.containers[*].terminationMessagePolicy\":\"File\","
  "\"spec.containers[*].ports[*].protocol\":\"TCP\","
  "\"spec.initContainers[*].resources\":{},"
  "\"spec.initContainers[*].terminationMessagePath\":\"/dev/termination-log\","
  "\"spec.initContainers[*].terminationMessagePolicy\":\"File\","
  "\"spec.initContainers[*].ports[*].protocol\":\"TCP\","
  "\"spec.volumes[*].secret.defaultMode\":420,"
  "\"spec.volumes[*].configMap.defaultMode\":420,"
  "\"spec.volumes[*].projected.defaultMode\":420,"
  "\"spec.volumes[*].downwardAPI.defaultMode\":420,"
  "\"status.phase\":\"Pending\"}" },
{ "PodTemplateSpecHolder", "{}" },
{ "Deployment", "{"
  "\"spec.replicas\":1,"
  "\"spec.progressDeadlineSeconds\":600,"
  "\"spec.revisionHistoryLimit\":10,"
  "\"spec.strategy\":{\"type\":\"RollingUpdate\",\"rollingUpdate\":"
     "{\"maxSurge\":\"25%\",\"maxUnavailable\":\"25%\"}},"
  "\"status\":{}}" },
{ "ReplicaSet", "{\"spec.replicas\":1,\"status\":{\"replicas\":0}}" },
{ "Job", "{"
  "\"spec.parallelism\":1,"
  "\"spec.completions\":1,"
  "\"spec.backoffLimit\":6,"
  "\"spec.completionMode\":\"NonIndexed\","
  "\"spec.suspend\":false,"
  "\"status\":{}}" },
{ "Service", "{"
  "\"spec.type\":\"ClusterIP\","
  "\"spec.sessionAffinity\":\"None\","
  "\"spec.internalTrafficPolicy\":\"Cluster\","
  "\"spec.ipFamilies\":[\"IPv4\"],"
  "\"spec.ipFamilyPolicy\":\"SingleStack\","
  "\"spec.ports[*].protocol\":\"TCP\","
  "\"status\":{\"loadBalancer\":{}}}" },
{ "Endpoints", "{\"subsets[*].ports[*].protocol\":\"TCP\"}" },
{ "EndpointSlice", "{\"endpoints[*].conditions\":{},\"ports[*].protocol\":\"TCP\"}" },
{ "PersistentVolumeClaim", "{\"spec.volumeMode\":\"Filesystem\",\"status\":{\"phase\":\"Pending\"}}" },
{ "StorageClass", "{\"reclaimPolicy\":\"Delete\",\"volumeBindingMode\":\"Immediate\"}" },
{ "PriorityClass", "{\"preemptionPolicy\":\"PreemptLowerPriority\"}" },
{ "NetworkPolicy", "{\"status\":{}}" },
{ "ValidatingAdmissionPolicy", "{"
  "\"spec.matchConstraints.matchPolicy\":\"Equivalent\","
  "\"spec.matchConstraints.namespaceSelector\":{},"
  "\"spec.matchConstraints.objectSelector\":{},"
  "\"spec.matchConstraints.resourceRules[*].scope\":\"*\"}" },
{ "ValidatingAdmissionPolicyBinding", "{"
  "\"spec.matchResources.matchPolicy\":\"Equivalent\","
  "\"spec.matchResources.objectSelector\":{}}" },
{ "ServiceCIDR", "{\"status\":{}}" },
{ "FlowSchema", "{\"status\":{}}" },
{ "PriorityLevelConfiguration", "{\"status\":{}}" },
{ "Namespace", "{\"spec\":{\"finalizers\":[\"kubernetes\"]},\"status\":{\"phase\":\"Active\"}}" },
{ "Node", "{\"status\":{\"daemonEndpoints\":{\"kubeletEndpoint\":{\"Port\":0}},"
           "\"nodeInfo\":{\"architecture\":\"\",\"bootID\":\"\",\"containerRuntimeVersion\":\"\","
           "\"kernelVersion\":\"\",\"kubeProxyVersion\":\"\",\"kubeletVersion\":\"\","
           "\"machineID\":\"\",\"operatingSystem\":\"\",\"osImage\":\"\",\"systemUUID\":\"\"}}}" },
{ "ValidatingWebhookConfiguration", "{"
  "\"webhooks[*].failurePolicy\":\"Fail\","
  "\"webhooks[*].matchPolicy\":\"Equivalent\","
  "\"webhooks[*].namespaceSelector\":{},"
  "\"webhooks[*].objectSelector\":{},"
  "\"webhooks[*].timeoutSeconds\":10,"
  "\"webhooks[*].rules[*].scope\":\"*\"}" },
{ "CustomResourceDefinition", "{\"spec.conversion\":{\"strategy\":\"None\"}}" },
{ "HorizontalPodAutoscaler", "{\"spec.minReplicas\":1}" },
{ "Lease", "{}" },
{ "Event", "{}" },
{ NULL, NULL }
};

static json *DEFAULT_CACHE;

static json *defaults_for(const char *kind){
    if(!DEFAULT_CACHE){
        DEFAULT_CACHE = j_obj();
        for(int i = 0; DEFAULTS[i].kind; i++){
            json *t = j_parse0(DEFAULTS[i].table);
            if(t) j_set(DEFAULT_CACHE, DEFAULTS[i].kind, t);
        }
    }
    return j_get(DEFAULT_CACHE, kind);
}

static void apply_path(json *node, char **segs, int nseg, int idx, json *val, int past_star){
    if(idx == nseg - 1){
        if(node->t != J_OBJ) return;
        if(!j_get(node, segs[idx])) j_set(node, segs[idx], j_clone(val));
        return;
    }
    const char *s = segs[idx];
    if(!strcmp(s, "[*]")){
        if(node->t != J_ARR) return;
        for(size_t i = 0; i < node->nitems; i++)
            apply_path(node->items[i], segs, nseg, idx + 1, val, 1);
        return;
    }
    json *next = (node->t == J_OBJ) ? j_get(node, s) : NULL;
    int star_next = (idx + 1 < nseg && !strcmp(segs[idx+1], "[*]"));
    if(!next){
        if(past_star || star_next) return;
        if(node->t != J_OBJ) return;
        next = j_obj();
        j_set(node, s, next);
    }
    apply_path(next, segs, nseg, idx + 1, val, past_star);
}

static void split_dpath(const char *path, char segs[32][64], int *n){
    *n = 0;
    const char *p = path;
    while(*p && *n < 32){
        if(!strncmp(p, "[*]", 3)){ snprintf(segs[(*n)++], 64, "[*]"); p += 3; if(*p=='.') p++; continue; }
        const char *q = p;
        while(*q && *q != '.' && strncmp(q, "[*]", 3)) q++;
        size_t len = (size_t)(q - p);
        if(len >= 64) len = 63;
        memcpy(segs[*n], p, len); segs[*n][len] = 0; (*n)++;
        p = q;
        if(*p == '.') p++;
    }
}

/* image reference without an explicit tag, or tagged :latest, pulls Always */
static const char *pull_policy_for(const char *image){
    if(!image) return "Always";
    const char *slash = strrchr(image, '/');
    const char *name = slash ? slash + 1 : image;
    const char *colon = strrchr(name, ':');
    if(!colon) return "Always";
    return strcmp(colon + 1, "latest") ? "IfNotPresent" : "Always";
}

static void default_containers(json *spec){
    const char *keys[] = {"initContainers", "containers", "ephemeralContainers", NULL};
    for(int k = 0; keys[k]; k++){
        json *l = j_get(spec, keys[k]);
        for(size_t i = 0; j_is(l, J_ARR) && i < l->nitems; i++){
            json *c = l->items[i];
            if(!j_get(c, "imagePullPolicy"))
                j_set(c, "imagePullPolicy", j_str(pull_policy_for(j_gets(c, "image"))));
        }
    }
}

static int qty_millis(const char *q, long long *out){
    if(!q) return 0;
    char *end;
    double v = strtod(q, &end);
    if(end == q) return 0;
    if(!strcmp(end, "m")) *out = (long long)(v);
    else if(!*end) *out = (long long)(v * 1000);
    else return 0;
    return 1;
}

static const char *qos_of(json *spec){
    if(!spec) return NULL;
    int has_req = 0, has_lim = 0, guaranteed = 1;
    const char *keys[] = {"initContainers", "containers", NULL};
    for(int k = 0; keys[k]; k++){
        json *l = j_get(spec, keys[k]);
        for(size_t i = 0; j_is(l, J_ARR) && i < l->nitems; i++){
            json *r = j_get(l->items[i], "resources");
            json *req = r ? j_get(r, "requests") : NULL;
            json *lim = r ? j_get(r, "limits") : NULL;
            if(j_is(req, J_OBJ) && req->nmem) has_req = 1;
            if(j_is(lim, J_OBJ) && lim->nmem) has_lim = 1;
            if(!j_is(lim, J_OBJ) || !lim->nmem) guaranteed = 0;
            else if(j_is(req, J_OBJ)){
                for(size_t m = 0; m < req->nmem; m++){
                    const char *lv = j_gets(lim, req->mem[m].key);
                    const char *rv = j_str_of(req->mem[m].val);
                    if(!lv || !rv || strcmp(lv, rv)) guaranteed = 0;
                }
            }
        }
    }
    return (!has_req && !has_lim) ? "BestEffort" : (guaranteed ? "Guaranteed" : "Burstable");
}

static void set_qos(json *obj){
    json *spec = j_get(obj, "spec");
    if(!spec) return;
    int has_req = 0, has_lim = 0, guaranteed = 1;
    const char *keys[] = {"initContainers", "containers", NULL};
    for(int k = 0; keys[k]; k++){
        json *l = j_get(spec, keys[k]);
        for(size_t i = 0; j_is(l, J_ARR) && i < l->nitems; i++){
            json *r = j_get(l->items[i], "resources");
            json *req = r ? j_get(r, "requests") : NULL;
            json *lim = r ? j_get(r, "limits") : NULL;
            if(j_is(req, J_OBJ) && req->nmem) has_req = 1;
            if(j_is(lim, J_OBJ) && lim->nmem) has_lim = 1;
            if(!j_is(lim, J_OBJ) || !lim->nmem) guaranteed = 0;
            else if(j_is(req, J_OBJ)){
                for(size_t m = 0; m < req->nmem; m++){
                    const char *lv = j_gets(lim, req->mem[m].key);
                    const char *rv = j_str_of(req->mem[m].val);
                    if(!lv || !rv || strcmp(lv, rv)) guaranteed = 0;
                }
            }
        }
    }
    const char *q = (!has_req && !has_lim) ? "BestEffort" : (guaranteed ? "Guaranteed" : "Burstable");
    j_set(j_ensure_obj(obj, "status"), "qosClass", j_str(q));
}

void apply_defaults(resdef *rd, json *obj, const char *version){
    json *tbl = defaults_for(rd->kind);
    if(tbl){
        for(size_t i = 0; i < tbl->nmem; i++){
            char segs[32][64]; int n = 0;
            split_dpath(tbl->mem[i].key, segs, &n);
            char *ptrs[32];
            for(int k = 0; k < n; k++) ptrs[k] = segs[k];
            apply_path(obj, ptrs, n, 0, tbl->mem[i].val, 0);
        }
    }
    if(!strcmp(rd->kind, "Pod")){
        json *spec = j_get(obj, "spec");
        if(spec) default_containers(spec);
        json *ec = spec ? j_get(spec, "ephemeralContainers") : NULL;
        for(size_t i = 0; j_is(ec, J_ARR) && i < ec->nitems; i++){
            json *c = ec->items[i];
            if(!j_get(c, "resources")) j_set(c, "resources", j_obj());
            if(!j_get(c, "terminationMessagePath"))
                j_set(c, "terminationMessagePath", j_str("/dev/termination-log"));
            if(!j_get(c, "terminationMessagePolicy"))
                j_set(c, "terminationMessagePolicy", j_str("File"));
        }
        /* A volume left with no source at all -- because the only one it named was
           a field this release's schema does not know, and the decoder dropped it
           -- takes the default source. */
        if(spec){
            json *vs = j_get(spec, "volumes");
            for(size_t i = 0; j_is(vs, J_ARR) && i < vs->nitems; i++){
                json *v = vs->items[i];
                int sources = 0;
                for(size_t k = 0; k < v->nmem; k++)
                    if(strcmp(v->mem[k].key, "name")) sources++;
                if(!sources) j_set(v, "emptyDir", j_obj());
            }
        }
        /* An image volume source is not served until the feature is on: before
           1.31 the field is simply unknown, so the volume is left without a
           source and defaults to emptyDir; from 1.31 the whole volume is
           dropped, and so is any mount that referred to it. */
        if(spec){
            json *vols = j_get(spec, "volumes");
            for(size_t i = 0; j_is(vols, J_ARR) && i < vols->nitems; i++){
                if(!j_get(vols->items[i], "image")) continue;
                if(RELEASE_MINOR < 31) continue;   /* pruned already; defaulted below */
                const char *vn = j_gets(vols->items[i], "name");
                static const char *LISTS2[] = {"containers", "initContainers", NULL};
                for(int L = 0; vn && LISTS2[L]; L++){
                    json *cl = j_get(spec, LISTS2[L]);
                    for(size_t k = 0; j_is(cl, J_ARR) && k < cl->nitems; k++){
                        json *mts = j_get(cl->items[k], "volumeMounts");
                        for(size_t z = 0; j_is(mts, J_ARR) && z < mts->nitems; z++)
                            if(!strcmp(j_gets(mts->items[z], "name") ?: "", vn)){
                                j_remove_at(mts, z); z--;
                            }
                        if(j_is(mts, J_ARR) && !mts->nitems) j_del(cl->items[k], "volumeMounts");
                    }
                }
                j_remove_at(vols, i); i--;
            }
            if(j_is(vols, J_ARR) && !vols->nitems) j_del(spec, "volumes");
        }
        /* the sidecar field is pruned at 1.28 and honoured from 1.29 */
        if(RELEASE_MINOR <= 28 && spec){
            json *ic = j_get(spec, "initContainers");
            for(size_t i = 0; j_is(ic, J_ARR) && i < ic->nitems; i++)
                j_del(ic->items[i], "restartPolicy");
        }
        /* user namespaces are off at 1.30-1.32, so the field is dropped there */
        if(RELEASE_MINOR >= 30 && RELEASE_MINOR <= 32 && spec) j_del(spec, "hostUsers");
        /* 1.30 made the AppArmor profile a field and keeps the deprecated
           annotation in step with it. */
        if(RELEASE_MINOR >= 30 && spec){
            static const char *LISTS[] = {"containers", "initContainers", NULL};
            for(int L = 0; LISTS[L]; L++){
                json *l = j_get(spec, LISTS[L]);
                for(size_t i = 0; j_is(l, J_ARR) && i < l->nitems; i++){
                    const char *cn = j_gets(l->items[i], "name");
                    const char *ty = j_gets(j_path(l->items[i], "securityContext.appArmorProfile"), "type");
                    if(!cn || !ty) continue;
                    const char *val = !strcmp(ty, "RuntimeDefault") ? "runtime/default"
                                    : !strcmp(ty, "Unconfined") ? "unconfined" : NULL;
                    char ann[320];
                    if(!val){
                        const char *lp = j_gets(j_path(l->items[i], "securityContext.appArmorProfile"),
                                                "localhostProfile");
                        if(!lp) continue;
                        snprintf(ann, sizeof ann, "localhost/%s", lp);
                        val = ann;
                    }
                    char key[320];
                    snprintf(key, sizeof key, "container.apparmor.security.beta.kubernetes.io/%s", cn);
                    json *md = j_ensure_obj(obj, "metadata");
                    json *a = j_ensure_obj(md, "annotations");
                    if(!j_get(a, key)) j_set(a, key, j_str(val));
                }
            }
        }
        set_qos(obj);
        json *conds = j_path(obj, "status.conditions");
        for(size_t i = 0; j_is(conds, J_ARR) && i < conds->nitems; i++){
            if(!j_get(conds->items[i], "lastProbeTime"))
                j_set(conds->items[i], "lastProbeTime", j_null());
            if(!j_get(conds->items[i], "lastTransitionTime")){
                char *ts = now_rfc3339();
                j_set(conds->items[i], "lastTransitionTime", j_str(ts));
                free(ts);
            }
        }
    } else if(!strcmp(rd->kind, "Deployment") || !strcmp(rd->kind, "ReplicaSet") ||
              !strcmp(rd->kind, "Job")){
        json *ts = j_path(obj, "spec.template.spec");
        if(ts){
            default_containers(ts);
            if(!j_get(ts, "restartPolicy") && strcmp(rd->kind, "Job"))
                j_set(ts, "restartPolicy", j_str("Always"));
            if(!j_get(ts, "dnsPolicy")) j_set(ts, "dnsPolicy", j_str("ClusterFirst"));
            if(!j_get(ts, "schedulerName")) j_set(ts, "schedulerName", j_str("default-scheduler"));
            if(!j_get(ts, "securityContext")) j_set(ts, "securityContext", j_obj());
            if(!j_get(ts, "terminationGracePeriodSeconds"))
                j_set(ts, "terminationGracePeriodSeconds", j_int(30));
            const char *ck[] = {"containers", "initContainers", NULL};
            for(int k = 0; ck[k]; k++){
                json *l = j_get(ts, ck[k]);
                for(size_t i = 0; j_is(l, J_ARR) && i < l->nitems; i++){
                    json *c = l->items[i];
                    if(!j_get(c, "resources")) j_set(c, "resources", j_obj());
                    if(!j_get(c, "terminationMessagePath"))
                        j_set(c, "terminationMessagePath", j_str("/dev/termination-log"));
                    if(!j_get(c, "terminationMessagePolicy"))
                        j_set(c, "terminationMessagePolicy", j_str("File"));
                    json *ports = j_get(c, "ports");
                    for(size_t z = 0; j_is(ports, J_ARR) && z < ports->nitems; z++)
                        if(!j_get(ports->items[z], "protocol"))
                            j_set(ports->items[z], "protocol", j_str("TCP"));
                }
            }
        }
    } else if(!strcmp(rd->kind, "Service")){
        json *spec2 = j_get(obj, "spec");
        const char *ty = spec2 ? j_gets(spec2, "type") : NULL;
        if(spec2 && (!ty || strcmp(ty, "ExternalName"))){
            const char *cip = j_gets(spec2, "clusterIP");
            if(!cip){
                static int next_ip = 10;
                char b2[32];
                snprintf(b2, sizeof b2, "10.0.0.%d", (next_ip++ % 240) + 10);
                j_set(spec2, "clusterIP", j_str(b2));
            }
            cip = j_gets(spec2, "clusterIP");
            if(!j_get(spec2, "clusterIPs") && cip){
                json *l = j_arr();
                j_push(l, j_str(cip));
                j_set(spec2, "clusterIPs", l);
            }
        }
        if(spec2 && ty && !strcmp(ty, "NodePort")){
            json *pl = j_get(spec2, "ports");
            static int next_np = 30000;
            for(size_t i = 0; j_is(pl, J_ARR) && i < pl->nitems; i++)
                if(!j_get(pl->items[i], "nodePort"))
                    j_set(pl->items[i], "nodePort", j_int(next_np++));
        }
        json *ports = j_path(obj, "spec.ports");
        for(size_t i = 0; j_is(ports, J_ARR) && i < ports->nitems; i++){
            json *p = ports->items[i];
            if(!j_get(p, "targetPort")) j_set(p, "targetPort", j_clone(j_get(p, "port")));
        }
        json *sp2 = j_get(obj, "spec");
        const char *svctype = sp2 ? j_gets(sp2, "type") : NULL;
        if(svctype && (!strcmp(svctype, "NodePort") || !strcmp(svctype, "LoadBalancer")) &&
           !j_get(sp2, "externalTrafficPolicy"))
            j_set(sp2, "externalTrafficPolicy", j_str("Cluster"));
    } else if(!strcmp(rd->kind, "ValidatingAdmissionPolicy")){
        if(RELEASE_MINOR >= 27 && !j_get(obj, "status")) j_set(obj, "status", j_obj());
        if(RELEASE_MINOR == 28 && j_get(obj, "spec") && !j_get(j_get(obj, "spec"), "variables"))
            j_set(j_get(obj, "spec"), "variables", j_null());
    } else if(!strcmp(rd->kind, "ValidatingAdmissionPolicyBinding")){
        /* spec.validationActions only exists from 1.27 */
        if(RELEASE_MINOR <= 26 && j_get(obj, "spec")) j_del(j_get(obj, "spec"), "validationActions");
    } else if(!strcmp(rd->kind, "NetworkPolicy")){
        /* NetworkPolicyStatus was withdrawn in 1.28: the field is gone. */
        if(RELEASE_MINOR >= 28) j_del(obj, "status");
        json *spec = j_get(obj, "spec");
        if(spec && !j_get(spec, "policyTypes")){
            json *t = j_arr();
            if(j_get(spec, "egress") && !j_get(spec, "ingress")) j_push(t, j_str("Egress"));
            else {
                j_push(t, j_str("Ingress"));
                if(j_get(spec, "egress")) j_push(t, j_str("Egress"));
            }
            j_set(spec, "policyTypes", t);
        }
    } else if(!strcmp(rd->kind, "IPAddress")){
        /* an empty group in a parentRef means the core group, and is not stored */
        json *pr = j_path(obj, "spec.parentRef");
        if(pr){
            const char *g = j_gets(pr, "group");
            if(g && !*g) j_del(pr, "group");
        }
    } else if(!strcmp(rd->kind, "Node")){
        json *conds = j_path(obj, "status.conditions");
        for(size_t i = 0; j_is(conds, J_ARR) && i < conds->nitems; i++)
            if(!j_get(conds->items[i], "lastHeartbeatTime"))
                j_set(conds->items[i], "lastHeartbeatTime", j_null());
    } else if(!strcmp(rd->kind, "Namespace")){
        json *md = j_ensure_obj(obj, "metadata");
        json *labels = j_ensure_obj(md, "labels");
        const char *nm = j_gets(md, "name");
        if(nm && !j_get(labels, "kubernetes.io/metadata.name"))
            j_set(labels, "kubernetes.io/metadata.name", j_str(nm));
    } else if(!strcmp(rd->kind, "EndpointSlice")){
        if(!j_get(obj, "ports")) j_set(obj, "ports", j_null());
    } else if(!strcmp(rd->kind, "Secret")){
        json *sd = j_get(obj, "stringData");
        if(j_is(sd, J_OBJ)){
            json *d = j_ensure_obj(obj, "data");
            for(size_t i = 0; i < sd->nmem; i++){
                const char *v = j_str_of(sd->mem[i].val);
                if(!v) continue;
                char *b = b64_encode((const unsigned char*)v, strlen(v));
                j_set(d, sd->mem[i].key, j_str(b));
                free(b);
            }
            j_del(obj, "stringData");
        }
        if(!j_get(obj, "type")) j_set(obj, "type", j_str("Opaque"));
    } else if(!strcmp(rd->kind, "Event")){
        if(*rd->group == 0){
            if(!j_get(obj, "eventTime")) j_set(obj, "eventTime", j_null());
            if(!j_get(obj, "reportingComponent")) j_set(obj, "reportingComponent", j_str(""));
            if(!j_get(obj, "reportingInstance")) j_set(obj, "reportingInstance", j_str(""));
        }
    } else if(!strcmp(rd->kind, "HorizontalPodAutoscaler")){
        if(!strcmp(version, "v1")){
            json *spec = j_get(obj, "spec");
            if(spec && !j_get(spec, "targetCPUUtilizationPercentage") && !j_get(spec, "metrics"))
                j_set(spec, "targetCPUUtilizationPercentage", j_int(80));
            json *st = j_ensure_obj(obj, "status");
            if(!j_get(st, "currentReplicas")) j_set(st, "currentReplicas", j_int(0));
        }
    }
}

/* =============================================================================
 * Validation.  Causes carry the API-conventions reason plus the field path;
 * the human-readable message is not part of the contract.
 * ========================================================================== */
/* the human half of a cause: a client renders "<field>: <message>" */
static const char *cause_message(const char *reason){
    if(!strcmp(reason, "FieldValueRequired"))     return "Required value";
    if(!strcmp(reason, "FieldValueDuplicate"))    return "Duplicate value";
    if(!strcmp(reason, "FieldValueNotSupported")) return "Unsupported value";
    if(!strcmp(reason, "FieldValueForbidden"))    return "Forbidden";
    return "Invalid value";
}

static void add_cause(json *cs, const char *reason, const char *field){
    json *c = j_obj();
    j_set(c, "reason", j_str(reason));
    j_set(c, "message", j_str(cause_message(reason)));
    j_set(c, "field", j_str(field));
    j_push(cs, c);
}
#define CAUSE(r, ...) do { char _f[256]; snprintf(_f, sizeof _f, __VA_ARGS__); \
                           add_cause(cs, r, _f); } while(0)

static int is_dns1123_sub(const char *s){
    size_t n = s ? strlen(s) : 0;
    if(!n || n > 253) return 0;
    for(size_t i = 0; i < n; i++){
        char c = s[i];
        if(!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '-' || c == '.')) return 0;
    }
    if(!isalnum((unsigned char)s[0]) || !isalnum((unsigned char)s[n-1])) return 0;
    return 1;
}
static int is_dns1123_label(const char *s){
    size_t n = s ? strlen(s) : 0;
    if(!n || n > 63) return 0;
    for(size_t i = 0; i < n; i++){
        char c = s[i];
        if(!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '-')) return 0;
    }
    return isalnum((unsigned char)s[0]) && isalnum((unsigned char)s[n-1]);
}
static int is_qualified_name(const char *s){
    if(!s || !*s) return 0;
    const char *slash = strchr(s, '/');
    const char *name = slash ? slash + 1 : s;
    if(slash){
        char pre[256];
        size_t n = (size_t)(slash - s);
        if(n == 0 || n >= sizeof pre) return 0;
        memcpy(pre, s, n); pre[n] = 0;
        if(!is_dns1123_sub(pre)) return 0;
    }
    size_t n = strlen(name);
    if(!n || n > 63) return 0;
    for(size_t i = 0; i < n; i++){
        char c = name[i];
        if(!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return 0;
    }
    return isalnum((unsigned char)name[0]) && isalnum((unsigned char)name[n-1]);
}
static int is_label_value(const char *s){
    if(!s) return 0;
    if(!*s) return 1;
    if(strlen(s) > 63) return 0;
    for(const char *p = s; *p; p++)
        if(!(isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '.')) return 0;
    return isalnum((unsigned char)s[0]) && isalnum((unsigned char)s[strlen(s)-1]);
}
static int is_cfgmap_key(const char *s){
    if(!s || !*s) return 0;
    if(!strcmp(s, ".") || !strcmp(s, "..")) return 0;
    for(const char *p = s; *p; p++)
        if(!(isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '.')) return 0;
    return 1;
}
static int parse_quantity(const char *q, double *out){
    if(!q || !*q) return 0;
    char *end;
    double v = strtod(q, &end);
    if(end == q) return 0;
    static const struct { const char *s; double m; } SUF[] = {
        {"", 1}, {"m", 0.001}, {"k", 1e3}, {"M", 1e6}, {"G", 1e9}, {"T", 1e12},
        {"P", 1e15}, {"E", 1e18}, {"Ki", 1024.0}, {"Mi", 1048576.0},
        {"Gi", 1073741824.0}, {"Ti", 1099511627776.0}, {"Pi", 1.125899906842624e15},
        {"Ei", 1.152921504606847e18}, {NULL, 0}
    };
    for(int i = 0; SUF[i].s; i++)
        if(!strcmp(end, SUF[i].s)){ *out = v * SUF[i].m; return 1; }
    return 0;
}
static int in_list(const char *v, const char **l){
    for(int i = 0; l[i]; i++) if(v && !strcmp(v, l[i])) return 1;
    return 0;
}

static void validate_pod_spec(json *spec, const char *base, json *cs){
    json *cts = j_get(spec, "containers");
    if(!j_is(cts, J_ARR) || !cts->nitems) CAUSE("FieldValueRequired", "%scontainers", base);
    json *seen = j_obj();
    const char *lists[] = {"containers", "initContainers", NULL};
    for(int L = 0; lists[L]; L++){
        json *l = j_get(spec, lists[L]);
        for(size_t i = 0; j_is(l, J_ARR) && i < l->nitems; i++){
            json *c = l->items[i];
            const char *nm = j_gets(c, "name");
            if(!nm || !*nm) CAUSE("FieldValueRequired", "%s%s[%zu].name", base, lists[L], i);
            else if(!is_dns1123_label(nm)) CAUSE("FieldValueInvalid", "%s%s[%zu].name", base, lists[L], i);
            else if(j_get(seen, nm)) CAUSE("FieldValueDuplicate", "%s%s[%zu].name", base, lists[L], i);
            else j_set(seen, nm, j_bool(1));
            const char *img = j_gets(c, "image");
            if(!img || !*img) CAUSE("FieldValueRequired", "%s%s[%zu].image", base, lists[L], i);
            json *r = j_get(c, "resources");
            json *req = r ? j_get(r, "requests") : NULL;
            json *lim = r ? j_get(r, "limits") : NULL;
            double rv, lv;
            for(size_t m = 0; j_is(req, J_OBJ) && m < req->nmem; m++){
                const char *s = j_str_of(req->mem[m].val);
                if(!parse_quantity(s, &rv))
                    CAUSE("FieldValueInvalid", "%s%s[%zu].resources.requests[%s]", base, lists[L], i, req->mem[m].key);
                else {
                    const char *ls = lim ? j_gets(lim, req->mem[m].key) : NULL;
                    if(ls && parse_quantity(ls, &lv) && rv > lv)
                        CAUSE("FieldValueInvalid", "%s%s[%zu].resources.requests", base, lists[L], i);
                }
            }
            for(size_t m = 0; j_is(lim, J_OBJ) && m < lim->nmem; m++)
                if(!parse_quantity(j_str_of(lim->mem[m].val), &lv))
                    CAUSE("FieldValueInvalid", "%s%s[%zu].resources.limits[%s]", base, lists[L], i, lim->mem[m].key);
            json *ports = j_get(c, "ports");
            for(size_t z = 0; j_is(ports, J_ARR) && z < ports->nitems; z++){
                long long p = j_geti(ports->items[z], "containerPort", 0);
                if(p < 1 || p > 65535)
                    CAUSE("FieldValueInvalid", "%s%s[%zu].ports[%zu].containerPort", base, lists[L], i, z);
            }
        }
    }
    j_free(seen);
    static const char *RP[] = {"Always", "OnFailure", "Never", NULL};
    const char *rp = j_gets(spec, "restartPolicy");
    if(rp && !in_list(rp, RP)) CAUSE("FieldValueNotSupported", "%srestartPolicy", base);
    static const char *DP[] = {"ClusterFirst", "ClusterFirstWithHostNet", "Default", "None", NULL};
    const char *dp = j_gets(spec, "dnsPolicy");
    if(dp && !in_list(dp, DP)) CAUSE("FieldValueNotSupported", "%sdnsPolicy", base);
}

static int selector_matches_labels(json *sel, json *labels){
    json *ml = sel ? j_get(sel, "matchLabels") : NULL;
    if(!j_is(ml, J_OBJ)) return 1;
    for(size_t i = 0; i < ml->nmem; i++){
        const char *v = labels ? j_gets(labels, ml->mem[i].key) : NULL;
        if(!v || strcmp(v, j_str_of(ml->mem[i].val) ?: "")) return 0;
    }
    return 1;
}

json *validate_obj(resdef *rd, json *obj, json *old, const char *version){
    json *cs = j_arr();
    json *md = j_get(obj, "metadata");
    const char *name = md ? j_gets(md, "name") : NULL;
    if(!name || !*name) CAUSE("FieldValueRequired", "metadata.name");
    else if(!is_dns1123_sub(name)) CAUSE("FieldValueInvalid", "metadata.name");
    json *labels = md ? j_get(md, "labels") : NULL;
    for(size_t i = 0; j_is(labels, J_OBJ) && i < labels->nmem; i++){
        if(!is_qualified_name(labels->mem[i].key))
            CAUSE("FieldValueInvalid", "metadata.labels");
        else if(!is_label_value(j_str_of(labels->mem[i].val)))
            CAUSE("FieldValueInvalid", "metadata.labels");
    }
    if(old){
        json *omd = j_get(old, "metadata");
        json *ofin = omd ? j_get(omd, "finalizers") : NULL;
        if(omd && j_gets(omd, "deletionTimestamp")){
            json *nfin = md ? j_get(md, "finalizers") : NULL;
            for(size_t i = 0; j_is(nfin, J_ARR) && i < nfin->nitems; i++){
                int found = 0;
                for(size_t k = 0; j_is(ofin, J_ARR) && k < ofin->nitems; k++)
                    if(j_equal(nfin->items[i], ofin->items[k])) found = 1;
                if(!found){
                    CAUSE("FieldValueForbidden", "metadata.finalizers");
                    CAUSE("FieldValueForbidden", "metadata.finalizers");
                }
            }
        }
    }
    json *spec = j_get(obj, "spec");
    const char *K = rd->kind;
    if(!strcmp(K, "Pod")){
        if(spec) validate_pod_spec(spec, "spec.", cs);
        /* an init container may only restart Always: that is what makes it a sidecar */
        if(RELEASE_MINOR >= 29 && spec){
            json *ic = j_get(spec, "initContainers");
            for(size_t i = 0; j_is(ic, J_ARR) && i < ic->nitems; i++){
                const char *rp = j_gets(ic->items[i], "restartPolicy");
                if(rp && strcmp(rp, "Always")){
                    char f[64];
                    snprintf(f, sizeof f, "spec.initContainers[%zu].restartPolicy", i);
                    CAUSE("FieldValueNotSupported", f);
                }
            }
        }
        if(old){
            /* a Pod's spec is immutable except for the fields upstream allows */
            json *a = j_clone(j_get(old, "spec"));
            json *b = j_clone(spec);
            const char *lists[] = {"containers", "initContainers", "ephemeralContainers", NULL};
            for(int L = 0; L < 3; L++){
                for(int side = 0; side < 2; side++){
                    json *l = j_get(side ? b : a, lists[L]);
                    for(size_t i = 0; j_is(l, J_ARR) && i < l->nitems; i++)
                        j_set(l->items[i], "image", j_str(""));
                }
            }
            if(!strcmp(VALIDATE_SUB, "resize")){
                /* a container with no resources has nothing to resize */
                json *cl = j_path(old, "spec.containers");
                int sized = 0;
                for(size_t i = 0; j_is(cl, J_ARR) && i < cl->nitems; i++){
                    json *rs = j_get(cl->items[i], "resources");
                    if(rs && j_is(rs, J_OBJ) && rs->nmem) sized = 1;
                }
                if(!sized) CAUSE("FieldValueInvalid", "spec");
            }
            if(!strcmp(VALIDATE_SUB, "resize")){
                /* the resize subresource is how container resources change */
                for(int L = 0; L < 3; L++)
                    for(int side = 0; side < 2; side++){
                        json *l = j_get(side ? b : a, lists[L]);
                        for(size_t i = 0; j_is(l, J_ARR) && i < l->nitems; i++)
                            j_del(l->items[i], "resources");
                    }
            }
            for(int side = 0; side < 2; side++){
                json *x = side ? b : a;
                j_del(x, "activeDeadlineSeconds");
                j_del(x, "tolerations");
                j_del(x, "terminationGracePeriodSeconds");
                j_del(x, "ephemeralContainers");
            }
            /* 1.29 through 1.31 report the QoS class separately, on metadata,
             * before the spec-immutability cause; from 1.32 the class is fixed
             * at admission and carried forward instead, so the cause is gone. */
            if(RELEASE_MINOR >= 29 && RELEASE_MINOR <= 31 && old){
                const char *qa = qos_of(j_get(old, "spec"));
                const char *qb = qos_of(j_get(obj, "spec"));
                if(qa && qb && strcmp(qa, qb)) CAUSE("FieldValueInvalid", "metadata");
            }
            if(!j_equal(a, b)){
                char *ta = j_dump_sorted(a), *tb = j_dump_sorted(b);
                fprintf(stderr, "pod spec immutable: old=%.900s\n              new=%.900s\n", ta, tb);
                free(ta); free(tb);
                CAUSE("FieldValueForbidden", "spec");
            }
            j_free(a); j_free(b);
        }
    } else if(!strcmp(K, "Deployment") || !strcmp(K, "ReplicaSet")){
        json *sel = spec ? j_get(spec, "selector") : NULL;
        if(!sel) CAUSE("FieldValueRequired", "spec.selector");
        json *tmpl = spec ? j_get(spec, "template") : NULL;
        json *tl = tmpl ? j_get(j_get(tmpl, "metadata"), "labels") : NULL;
        if(!selector_matches_labels(sel, tl) || !sel)
            CAUSE("FieldValueInvalid", "spec.template.metadata.labels");
        json *rep = spec ? j_get(spec, "replicas") : NULL;
        if(rep && rep->t == J_NUM && rep->n < 0) CAUSE("FieldValueInvalid", "spec.replicas");
        json *ts = tmpl ? j_get(tmpl, "spec") : NULL;
        if(ts) validate_pod_spec(ts, "spec.template.spec.", cs);
    } else if(!strcmp(K, "Job")){
        json *ts = spec ? j_path(spec, "template.spec") : NULL;
        if(ts){
            const char *rp = j_gets(ts, "restartPolicy");
            if(!rp || (strcmp(rp, "Never") && strcmp(rp, "OnFailure")))
                CAUSE("FieldValueRequired", "spec.template.spec.restartPolicy");
            validate_pod_spec(ts, "spec.template.spec.", cs);
        }
        json *cm = spec ? j_get(spec, "completionMode") : NULL;
        static const char *CM[] = {"NonIndexed", "Indexed", NULL};
        if(cm && !in_list(j_str_of(cm), CM)) CAUSE("FieldValueNotSupported", "spec.completionMode");
        int indexed = cm && j_str_of(cm) && !strcmp(j_str_of(cm), "Indexed");
        /* Both per-index backoff (1.28) and a success policy (1.31) only make
           sense for an indexed Job. */
        if(RELEASE_MINOR >= 29 && spec && j_get(spec, "backoffLimitPerIndex") && !indexed)
            CAUSE("FieldValueInvalid", "spec.backoffLimitPerIndex");
        if(spec && j_get(spec, "successPolicy") && !indexed)
            CAUSE("FieldValueInvalid", "spec.successPolicy");
    } else if(!strcmp(K, "LeaseCandidate")){
        if(RELEASE_MINOR >= 32 && (!spec || !j_get(spec, "strategy")))
            CAUSE("FieldValueRequired", "spec.strategy");
    } else if(!strcmp(K, "VolumeAttributesClass")){
        const char *dn = j_gets(obj, "driverName");
        if(!dn || !*dn) CAUSE("FieldValueRequired", "driverName");
    } else if(!strcmp(K, "IPAddress")){
        /* the object's name is the address it represents */
        const char *nm = j_gets(j_get(obj, "metadata"), "name");
        struct in_addr a4; struct in6_addr a6;
        if(!nm || (!inet_pton(AF_INET, nm, &a4) && !inet_pton(AF_INET6, nm, &a6)))
            CAUSE("FieldValueInvalid", "metadata.name");
    } else if(!strcmp(K, "ValidatingAdmissionPolicy")){
        json *vs = spec ? j_get(spec, "validations") : NULL;
        for(size_t i = 0; j_is(vs, J_ARR) && i < vs->nitems; i++){
            const char *ex = j_gets(vs->items[i], "expression");
            char *e = ex ? cel_check(ex) : NULL;
            if(!ex || e){
                char f[64];
                snprintf(f, sizeof f, "spec.validations[%zu].expression", i);
                CAUSE("FieldValueInvalid", f);
            }
            free(e);
        }
    } else if(!strcmp(K, "Service")){
        json *ports = spec ? j_get(spec, "ports") : NULL;
        const char *type = spec ? j_gets(spec, "type") : NULL;
        if((!j_is(ports, J_ARR) || !ports->nitems) && (!type || strcmp(type, "ExternalName")))
            CAUSE("FieldValueRequired", "spec.ports");
        int multi = j_is(ports, J_ARR) && ports->nitems > 1;
        for(size_t i = 0; j_is(ports, J_ARR) && i < ports->nitems; i++){
            json *p = ports->items[i];
            long long pn = j_geti(p, "port", 0);
            if(pn < 1 || pn > 65535){
                CAUSE("FieldValueInvalid", "spec.ports[%zu].port", i);
                json *tp = j_get(p, "targetPort");
                if(tp && tp->t == J_NUM && (tp->n < 1 || tp->n > 65535))
                    CAUSE("FieldValueInvalid", "spec.ports[%zu].targetPort", i);
            }
            const char *nm = j_gets(p, "name");
            if(multi && (!nm || !*nm)) CAUSE("FieldValueRequired", "spec.ports[%zu].name", i);
            for(size_t k = 0; nm && *nm && k < i; k++){
                const char *pn = j_gets(ports->items[k], "name");
                if(pn && !strcmp(pn, nm)) CAUSE("FieldValueDuplicate", "spec.ports[%zu].name", i);
            }
        }
    } else if(!strcmp(K, "ConfigMap")){
        json *data = j_get(obj, "data");
        for(size_t i = 0; j_is(data, J_OBJ) && i < data->nmem; i++)
            if(!is_cfgmap_key(data->mem[i].key))
                CAUSE("FieldValueInvalid", "data[%s]", data->mem[i].key);
        json *bd = j_get(obj, "binaryData");
        for(size_t i = 0; j_is(bd, J_OBJ) && i < bd->nmem; i++)
            if(!is_cfgmap_key(bd->mem[i].key))
                CAUSE("FieldValueInvalid", "binaryData[%s]", bd->mem[i].key);
    } else if(!strcmp(K, "Secret")){
        json *data = j_get(obj, "data");
        for(size_t i = 0; j_is(data, J_OBJ) && i < data->nmem; i++){
            size_t n = 0;
            unsigned char *d = b64_decode(j_str_of(data->mem[i].val) ?: "", &n);
            if(!d) CAUSE("FieldValueInvalid", "data[%s]", data->mem[i].key);
            free(d);
        }
        const char *t = j_gets(obj, "type");
        if(t && !strcmp(t, "kubernetes.io/dockercfg") && (!data || !j_get(data, ".dockercfg")))
            CAUSE("FieldValueRequired", "data[.dockercfg]");
        if(t && !strcmp(t, "kubernetes.io/dockerconfigjson") && (!data || !j_get(data, ".dockerconfigjson")))
            CAUSE("FieldValueRequired", "data[.dockerconfigjson]");
        if(t && !strcmp(t, "kubernetes.io/service-account-token")){
            json *ann = md ? j_get(md, "annotations") : NULL;
            if(!ann || !j_gets(ann, "kubernetes.io/service-account.name"))
                CAUSE("FieldValueRequired", "metadata.annotations[kubernetes.io/service-account.name]");
        }
    } else if(!strcmp(K, "PersistentVolumeClaim")){
        static const char *AM[] = {"ReadWriteOnce", "ReadOnlyMany", "ReadWriteMany",
                                   "ReadWriteOncePod", NULL};
        json *am = spec ? j_get(spec, "accessModes") : NULL;
        if(!j_is(am, J_ARR) || !am->nitems) CAUSE("FieldValueRequired", "spec.accessModes");
        for(size_t i = 0; j_is(am, J_ARR) && i < am->nitems; i++)
            if(!in_list(j_str_of(am->items[i]), AM)) CAUSE("FieldValueNotSupported", "spec.accessModes");
        json *req = spec ? j_path(spec, "resources.requests") : NULL;
        if(!req || !j_get(req, "storage")) CAUSE("FieldValueRequired", "spec.resources[storage]");
    } else if(!strcmp(K, "Role") || !strcmp(K, "ClusterRole")){
        json *rules = j_get(obj, "rules");
        for(size_t i = 0; j_is(rules, J_ARR) && i < rules->nitems; i++){
            json *v = j_get(rules->items[i], "verbs");
            if(!j_is(v, J_ARR) || !v->nitems) CAUSE("FieldValueRequired", "rules[%zu].verbs", i);
        }
    } else if(!strcmp(K, "RoleBinding") || !strcmp(K, "ClusterRoleBinding")){
        static const char *SK[] = {"User", "Group", "ServiceAccount", NULL};
        json *subs = j_get(obj, "subjects");
        for(size_t i = 0; j_is(subs, J_ARR) && i < subs->nitems; i++)
            if(!in_list(j_gets(subs->items[i], "kind"), SK))
                CAUSE("FieldValueNotSupported", "subjects[%zu].kind", i);
    } else if(!strcmp(K, "PriorityClass")){
        long long v = j_geti(obj, "value", 0);
        if(j_getb(obj, "globalDefault", 0) && v > 1000000000LL) CAUSE("FieldValueForbidden", "value");
        else if(v > 1000000000LL) CAUSE("FieldValueForbidden", "value");
    } else if(!strcmp(K, "PriorityLevelConfiguration")){
        const char *t = spec ? j_gets(spec, "type") : NULL;
        if(!t) CAUSE("FieldValueRequired", "spec.type");
        else if(!strcmp(t, "Limited") && !j_get(spec, "limited"))
            CAUSE("FieldValueRequired", "spec.limited");
        else if(!strcmp(t, "Exempt") && j_get(spec, "limited"))
            CAUSE("FieldValueForbidden", "spec.limited");
    } else if(!strcmp(K, "Lease")){
        json *d = spec ? j_get(spec, "leaseDurationSeconds") : NULL;
        if(d && d->t == J_NUM && d->n <= 0) CAUSE("FieldValueInvalid", "spec.leaseDurationSeconds");
    } else if(!strcmp(K, "EndpointSlice")){
        static const char *AT[] = {"IPv4", "IPv6", "FQDN", NULL};
        const char *at = j_gets(obj, "addressType");
        if(!at || !in_list(at, AT)) CAUSE("FieldValueNotSupported", "addressType");
    } else if(!strcmp(K, "NetworkPolicy")){
        const char *dirs[] = {"ingress", "egress", NULL};
        for(int d = 0; dirs[d]; d++){
            json *l = spec ? j_get(spec, dirs[d]) : NULL;
            for(size_t i = 0; j_is(l, J_ARR) && i < l->nitems; i++){
                json *peers = j_get(l->items[i], d ? "to" : "from");
                for(size_t k = 0; j_is(peers, J_ARR) && k < peers->nitems; k++){
                    json *ib = j_get(peers->items[k], "ipBlock");
                    const char *cidr = ib ? j_gets(ib, "cidr") : NULL;
                    if(cidr){
                        unsigned a, b, c, dd; int bits;
                        if(sscanf(cidr, "%u.%u.%u.%u/%d", &a, &b, &c, &dd, &bits) != 5 ||
                           a > 255 || b > 255 || c > 255 || dd > 255 || bits < 0 || bits > 32)
                            CAUSE("FieldValueInvalid", "spec.%s[%zu].%s[%zu].ipBlock.cidr",
                                  dirs[d], i, d ? "to" : "from", k);
                    }
                }
            }
        }
    }
    if(!cs->nitems){ j_free(cs); return NULL; }
    return cs;
}

/* =============================================================================
 * Conversion between served versions of one resource.
 * ========================================================================== */
void set_av(json *o, const char *group, const char *version){
    if(*group){ char *s = xasprintf("%s/%s", group, version); j_set(o, "apiVersion", j_str(s)); free(s); }
    else j_set(o, "apiVersion", j_str(version));
}

static json *hpa_to(json *o, const char *to){
    json *out = j_clone(o);
    json *spec = j_get(out, "spec");
    if(!spec) return out;
    if(!strcmp(to, "v1")){
        json *metrics = j_get(spec, "metrics");
        long long target = -1;
        for(size_t i = 0; j_is(metrics, J_ARR) && i < metrics->nitems; i++){
            json *m = metrics->items[i];
            json *r = j_get(m, "resource");
            if(r && !strcmp(j_gets(r, "name") ?: "", "cpu")){
                json *t = j_get(r, "target");
                if(t && j_get(t, "averageUtilization"))
                    target = j_geti(t, "averageUtilization", 0);
            }
        }
        j_del(spec, "metrics");
        j_del(spec, "behavior");
        if(target >= 0) j_set(spec, "targetCPUUtilizationPercentage", j_int(target));
        json *st = j_get(out, "status");
        if(st){
            j_del(st, "currentMetrics");
            j_del(st, "conditions");
            if(!j_get(st, "currentReplicas")) j_set(st, "currentReplicas", j_int(0));
            if(!j_get(st, "desiredReplicas")) j_set(st, "desiredReplicas", j_int(0));
        }
    } else {
        long long t = -1;
        json *tc = j_get(spec, "targetCPUUtilizationPercentage");
        if(tc && tc->t == J_NUM) t = (long long)tc->n;
        j_del(spec, "targetCPUUtilizationPercentage");
        if(t >= 0 && !j_get(spec, "metrics")){
            json *ms = j_arr();
            json *m = j_obj();
            j_set(m, "type", j_str("Resource"));
            json *r = j_obj();
            j_set(r, "name", j_str("cpu"));
            json *tg = j_obj();
            j_set(tg, "type", j_str("Utilization"));
            j_set(tg, "averageUtilization", j_int(t));
            j_set(r, "target", tg);
            j_set(m, "resource", r);
            j_push(ms, m);
            j_set(spec, "metrics", ms);
        }
        json *st = j_get(out, "status");
        if(st){
            int beta = !strcmp(to, "v2beta1") || !strcmp(to, "v2beta2");
            if(beta){
                if(!j_get(st, "conditions")) j_set(st, "conditions", j_null());
                if(!j_get(st, "currentReplicas")) j_set(st, "currentReplicas", j_int(0));
            } else {
                /* the v2 status carries neither of these */
                j_del(st, "currentReplicas");
                j_del(st, "conditions");
            }
            if(!j_get(st, "currentMetrics")) j_set(st, "currentMetrics", j_null());
            if(!j_get(st, "desiredReplicas")) j_set(st, "desiredReplicas", j_int(0));
        }
    }
    return out;
}

static json *flow_to(json *o, const char *to){
    json *out = j_clone(o);
    json *lim = j_path(out, "spec.limited");
    if(lim){
        json *acs = j_get(lim, "assuredConcurrencyShares");
        json *ncs = j_get(lim, "nominalConcurrencyShares");
        if(!strcmp(to, "v1beta1") || !strcmp(to, "v1beta2")){
            if(ncs && !acs){ j_set(lim, "assuredConcurrencyShares", j_clone(ncs)); j_del(lim, "nominalConcurrencyShares"); }
            /* only the oldest beta view drops the borrowing fields */
            if(!strcmp(to, "v1beta1")){
                j_del(lim, "borrowingLimitPercent");
                j_del(lim, "lendablePercent");
            } else if(RELEASE_MINOR >= 26 && !j_get(lim, "lendablePercent")) {
                j_set(lim, "lendablePercent", j_int(0));   /* the field arrives at 1.26 */
            }
        } else {
            if(acs && !ncs){ j_set(lim, "nominalConcurrencyShares", j_clone(acs)); j_del(lim, "assuredConcurrencyShares"); }
            if(RELEASE_MINOR >= 26 && !j_get(lim, "lendablePercent"))
                j_set(lim, "lendablePercent", j_int(0));
        }
    }
    return out;
}

json *convert_to(resdef *rd, const json *obj, const char *version){
    json *out;
    if(!strcmp(rd->kind, "HorizontalPodAutoscaler")) out = hpa_to((json*)obj, version);
    else if(!strcmp(rd->kind, "FlowSchema") || !strcmp(rd->kind, "PriorityLevelConfiguration"))
        out = flow_to((json*)obj, version);
    else out = j_clone(obj);
    set_av(out, rd->group, version);
    j_set(out, "kind", j_str(rd->kind));
    return out;
}

json *convert_from(resdef *rd, const json *obj, const char *version){
    json *out;
    if(!strcmp(rd->kind, "HorizontalPodAutoscaler")) out = hpa_to((json*)obj, rd->canon);
    else if(!strcmp(rd->kind, "FlowSchema") || !strcmp(rd->kind, "PriorityLevelConfiguration"))
        out = flow_to((json*)obj, rd->canon);
    else out = j_clone(obj);
    set_av(out, rd->group, rd->canon);
    j_set(out, "kind", j_str(rd->kind));
    return out;
}

/* -----------------------------------------------------------------------------
 * Values whose own unmarshaller rejects them: a resource.Quantity that does not
 * parse, or a Secret value that is not base64. Upstream never reaches validation
 * for these, so they are a 400 rather than a 422.
 * -------------------------------------------------------------------------- */
static int bad_quantities(json *spec, const char *const *lists){
    for(int L = 0; lists[L]; L++){
        json *cl = j_get(spec, lists[L]);
        for(size_t i = 0; j_is(cl, J_ARR) && i < cl->nitems; i++){
            json *rs = j_get(cl->items[i], "resources");
            const char *buckets[] = {"requests", "limits", NULL};
            for(int b = 0; buckets[b]; b++){
                json *m = rs ? j_get(rs, buckets[b]) : NULL;
                double v;
                for(size_t k = 0; j_is(m, J_OBJ) && k < m->nmem; k++)
                    if(!parse_quantity(j_str_of(m->mem[k].val) ?: "", &v)) return 1;
            }
        }
    }
    return 0;
}

int decode_level_error(resdef *rd, json *obj){
    static const char *LISTS[] = {"containers", "initContainers", "ephemeralContainers", NULL};
    if(!strcmp(rd->kind, "Pod")){
        json *spec = j_get(obj, "spec");
        if(spec && bad_quantities(spec, LISTS)) return 1;
    } else if(!strcmp(rd->kind, "Deployment") || !strcmp(rd->kind, "ReplicaSet") ||
              !strcmp(rd->kind, "Job")){
        json *ts = j_path(obj, "spec.template.spec");
        if(ts && bad_quantities(ts, LISTS)) return 1;
    } else if(!strcmp(rd->kind, "Secret")){
        json *data = j_get(obj, "data");
        for(size_t i = 0; j_is(data, J_OBJ) && i < data->nmem; i++){
            size_t n = 0;
            unsigned char *d = b64_decode(j_str_of(data->mem[i].val) ?: "", &n);
            if(!d) return 1;
            free(d);
        }
    }
    return 0;
}
