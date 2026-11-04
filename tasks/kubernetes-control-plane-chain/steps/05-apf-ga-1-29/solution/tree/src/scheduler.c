#define _GNU_SOURCE
#include "client.h"
#include "util.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static kclient C;

static const char *mname(json *o){ return j_gets(j_get(o, "metadata"), "name"); }
static const char *mns(json *o){ return j_gets(j_get(o, "metadata"), "namespace"); }

static double qty(const char *q){
    if(!q) return 0;
    char *end;
    double v = strtod(q, &end);
    static const struct { const char *s; double m; } SUF[] = {
        {"", 1}, {"m", 0.001}, {"k", 1e3}, {"M", 1e6}, {"G", 1e9}, {"T", 1e12},
        {"Ki", 1024.0}, {"Mi", 1048576.0}, {"Gi", 1073741824.0}, {"Ti", 1099511627776.0},
        {NULL, 0}
    };
    for(int i = 0; SUF[i].s; i++) if(!strcmp(end, SUF[i].s)) return v * SUF[i].m;
    return v;
}

static double pod_request(json *pod, const char *res){
    double sum = 0, init = 0;
    json *spec = j_get(pod, "spec");
    json *cs = j_get(spec, "containers");
    for(size_t i = 0; j_is(cs, J_ARR) && i < cs->nitems; i++){
        json *r = j_path(cs->items[i], "resources.requests");
        if(r) sum += qty(j_gets(r, res));
    }
    json *ic = j_get(spec, "initContainers");
    for(size_t i = 0; j_is(ic, J_ARR) && i < ic->nitems; i++){
        json *c = ic->items[i];
        json *r = j_path(c, "resources.requests");
        double v = r ? qty(j_gets(r, res)) : 0;
        const char *rp = j_gets(c, "restartPolicy");
        if(rp && !strcmp(rp, "Always")) sum += v;      /* a sidecar runs alongside */
        else if(v > init) init = v;
    }
    return sum > init ? sum : init;
}

static int label_match(json *labels, json *req){
    for(size_t i = 0; j_is(req, J_OBJ) && i < req->nmem; i++){
        const char *v = labels ? j_gets(labels, req->mem[i].key) : NULL;
        if(!v || strcmp(v, j_str_of(req->mem[i].val) ?: "")) return 0;
    }
    return 1;
}

static int expr_match(json *labels, json *expr){
    const char *key = j_gets(expr, "key");
    const char *op = j_gets(expr, "operator");
    json *vals = j_get(expr, "values");
    const char *have = labels ? j_gets(labels, key) : NULL;
    int in = 0;
    for(size_t i = 0; have && j_is(vals, J_ARR) && i < vals->nitems; i++)
        if(!strcmp(have, j_str_of(vals->items[i]) ?: "")) in = 1;
    if(!op) return 0;
    if(!strcmp(op, "In")) return in;
    if(!strcmp(op, "NotIn")) return !in;
    if(!strcmp(op, "Exists")) return have != NULL;
    if(!strcmp(op, "DoesNotExist")) return have == NULL;
    if(!strcmp(op, "Gt") || !strcmp(op, "Lt")){
        if(!have || !j_is(vals, J_ARR) || !vals->nitems) return 0;
        long long a = strtoll(have, NULL, 10);
        long long b = strtoll(j_str_of(vals->items[0]) ?: "0", NULL, 10);
        return !strcmp(op, "Gt") ? a > b : a < b;
    }
    return 0;
}

static int affinity_ok(json *pod, json *node){
    json *terms = j_path(pod, "spec.affinity.nodeAffinity."
                              "requiredDuringSchedulingIgnoredDuringExecution.nodeSelectorTerms");
    if(!j_is(terms, J_ARR) || !terms->nitems) return 1;
    json *labels = j_get(j_get(node, "metadata"), "labels");
    for(size_t i = 0; i < terms->nitems; i++){
        json *t = terms->items[i];
        int ok = 1;
        json *me = j_get(t, "matchExpressions");
        for(size_t k = 0; j_is(me, J_ARR) && k < me->nitems; k++)
            if(!expr_match(labels, me->items[k])) ok = 0;
        json *mf = j_get(t, "matchFields");
        for(size_t k = 0; j_is(mf, J_ARR) && k < mf->nitems; k++){
            json *fake = j_obj();
            j_set(fake, "metadata.name", j_str(mname(node)));
            if(!expr_match(fake, mf->items[k])) ok = 0;
            j_free(fake);
        }
        if(ok) return 1;
    }
    return 0;
}

static int tolerates(json *pod, json *taint){
    const char *tk = j_gets(taint, "key");
    const char *tv = j_gets(taint, "value");
    const char *te = j_gets(taint, "effect");
    if(te && !strcmp(te, "PreferNoSchedule")) return 1;
    json *tols = j_path(pod, "spec.tolerations");
    for(size_t i = 0; j_is(tols, J_ARR) && i < tols->nitems; i++){
        json *t = tols->items[i];
        const char *k = j_gets(t, "key");
        const char *op = j_gets(t, "operator") ?: "Equal";
        const char *v = j_gets(t, "value");
        const char *e = j_gets(t, "effect");
        if(k && tk && strcmp(k, tk)) continue;
        if(!k && strcmp(op, "Exists")) continue;
        if(e && te && strcmp(e, te)) continue;
        if(!strcmp(op, "Exists")) return 1;
        if(v && tv && !strcmp(v, tv)) return 1;
        if(!v && !tv) return 1;
    }
    return 0;
}

static int fits(json *pod, json *node, json *pods){
    if(j_getb(j_get(node, "spec"), "unschedulable", 0)) return 0;
    json *labels = j_get(j_get(node, "metadata"), "labels");
    json *sel = j_path(pod, "spec.nodeSelector");
    if(sel && !label_match(labels, sel)) return 0;
    if(!affinity_ok(pod, node)) return 0;
    json *taints = j_get(j_get(node, "spec"), "taints");
    for(size_t i = 0; j_is(taints, J_ARR) && i < taints->nitems; i++)
        if(!tolerates(pod, taints->items[i])) return 0;
    json *alloc = j_get(j_get(node, "status"), "allocatable");
    const char *res[] = {"cpu", "memory", NULL};
    for(int r = 0; res[r]; r++){
        double cap = alloc ? qty(j_gets(alloc, res[r])) : 0;
        double used = 0;
        for(size_t i = 0; j_is(pods, J_ARR) && i < pods->nitems; i++){
            json *p = pods->items[i];
            if(strcmp(j_gets(j_get(p, "spec"), "nodeName") ?: "", mname(node))) continue;
            used += pod_request(p, res[r]);
        }
        if(used + pod_request(pod, res[r]) > cap + 1e-9) return 0;
    }
    double maxpods = alloc ? qty(j_gets(alloc, "pods")) : 110;
    long long n = 0;
    for(size_t i = 0; j_is(pods, J_ARR) && i < pods->nitems; i++)
        if(!strcmp(j_gets(j_get(pods->items[i], "spec"), "nodeName") ?: "", mname(node))) n++;
    if(n + 1 > maxpods) return 0;
    return 1;
}

static void mark_unschedulable(json *pod){
    json *conds = j_get(j_get(pod, "status"), "conditions");
    for(size_t i = 0; j_is(conds, J_ARR) && i < conds->nitems; i++)
        if(!strcmp(j_gets(conds->items[i], "type") ?: "", "PodScheduled") &&
           !strcmp(j_gets(conds->items[i], "reason") ?: "", "Unschedulable")) return;
    json *st = j_obj();
    json *cl = j_arr();
    json *c = j_obj();
    j_set(c, "type", j_str("PodScheduled"));
    j_set(c, "status", j_str("False"));
    j_set(c, "reason", j_str("Unschedulable"));
    j_set(c, "message", j_str("0/0 nodes are available"));
    char *ts = now_rfc3339();
    j_set(c, "lastTransitionTime", j_str(ts));
    j_set(c, "lastProbeTime", j_null());
    free(ts);
    j_push(cl, c);
    j_set(st, "conditions", cl);
    json *body = j_obj();
    j_set(body, "status", st);
    char p[640];
    snprintf(p, sizeof p, "/api/v1/namespaces/%s/pods/%s/status", mns(pod), mname(pod));
    int code = 0;
    j_free(kc_send(&C, "PATCH", p, body, "application/merge-patch+json", &code));
}

static void bind(json *pod, const char *node){
    json *b = j_obj();
    j_set(b, "apiVersion", j_str("v1"));
    j_set(b, "kind", j_str("Binding"));
    json *md = j_obj();
    j_set(md, "name", j_str(mname(pod)));
    j_set(md, "namespace", j_str(mns(pod)));
    j_set(b, "metadata", md);
    json *t = j_obj();
    j_set(t, "apiVersion", j_str("v1"));
    j_set(t, "kind", j_str("Node"));
    j_set(t, "name", j_str(node));
    j_set(b, "target", t);
    char p[640];
    snprintf(p, sizeof p, "/api/v1/namespaces/%s/pods/%s/binding", mns(pod), mname(pod));
    int code = 0;
    j_free(kc_send(&C, "POST", p, b, NULL, &code));
}

int main(int argc, char **argv){
    signal(SIGPIPE, SIG_IGN);
    const char *kubeconfig = NULL;
    for(int i = 1; i < argc; i++){
        if(str_has_prefix(argv[i], "--kubeconfig=")) kubeconfig = argv[i] + 13;
        else if(!strcmp(argv[i], "--kubeconfig") && i + 1 < argc) kubeconfig = argv[++i];
    }
    if(!kubeconfig){ fprintf(stderr, "--kubeconfig is required\n"); return 1; }
    if(kc_from_kubeconfig(&C, kubeconfig) < 0){ fprintf(stderr, "bad kubeconfig\n"); return 1; }
    fprintf(stderr, "kube-scheduler started\n");
    for(;;){
        int st = 0;
        json *nl = kc_get(&C, "/api/v1/nodes", &st);
        json *pl = kc_get(&C, "/api/v1/pods", &st);
        json *nodes = nl ? j_get(nl, "items") : NULL;
        json *pods = pl ? j_get(pl, "items") : NULL;
        for(size_t i = 0; j_is(pods, J_ARR) && i < pods->nitems; i++){
            json *pod = pods->items[i];
            json *spec = j_get(pod, "spec");
            const char *nn = j_gets(spec, "nodeName");
            if(nn && *nn) continue;
            if(j_gets(j_get(pod, "metadata"), "deletionTimestamp")) continue;
            const char *sn = j_gets(spec, "schedulerName");
            if(sn && strcmp(sn, "default-scheduler")) continue;
            const char *phase = j_gets(j_get(pod, "status"), "phase");
            if(phase && (!strcmp(phase, "Succeeded") || !strcmp(phase, "Failed"))) continue;
            /* first fit, nodes considered in name order */
            const char *chosen = NULL;
            for(size_t k = 0; j_is(nodes, J_ARR) && k < nodes->nitems; k++)
                for(size_t z = k + 1; z < nodes->nitems; z++)
                    if(strcmp(mname(nodes->items[z]), mname(nodes->items[k])) < 0){
                        json *t = nodes->items[k];
                        nodes->items[k] = nodes->items[z];
                        nodes->items[z] = t;
                    }
            for(size_t k = 0; j_is(nodes, J_ARR) && k < nodes->nitems; k++){
                if(!fits(pod, nodes->items[k], pods)) continue;
                chosen = mname(nodes->items[k]);
                break;
            }
            if(!chosen)
                fprintf(stderr, "unschedulable %s/%s\n", mns(pod), mname(pod));
            if(chosen){
                fprintf(stderr, "bind %s/%s -> %s\n", mns(pod), mname(pod), chosen);
                bind(pod, chosen);
                j_set(j_get(pod, "spec"), "nodeName", j_str(chosen));
            } else mark_unschedulable(pod);
        }
        j_free(nl); j_free(pl);
        usleep(400000);
    }
}
