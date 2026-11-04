/* bulk.c -- Bulk Data export and $everything.
 *
 * `async.html` defines one pattern for every long-running request: 202 with a
 * Content-Location, a status endpoint that answers 202 while it runs and 200
 * with a manifest when it is done, and DELETE to cancel.  The manifest is not a
 * FHIR resource, and the files it points at are newline-delimited JSON -- one
 * resource per line, one file per type, no empty files.
 *
 * The compartment both halves need is the published CompartmentDefinition, not
 * a guess: `compartmentdefinition-patient` names, for each resource type, the
 * search parameters whose target is the patient.
 */
#include "bulk.h"
#include "search.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char  *name;        /* "Patient.ndjson" */
    char  *type;
    buf    data;
} bfile;

typedef struct {
    char     *id;
    char     *request;
    char     *tstamp;
    int       done, failed, cancelled;
    int       progress;      /* 0..100 */
    vec       files;         /* bfile* */
    char     *error;
} bjob;

static vec             g_jobs;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int             g_init;
static long long       g_next = 1;

static bjob *job_get(const char *id) {
    for (size_t i = 0; i < g_jobs.n; i++) {
        bjob *j = g_jobs.v[i];
        if (str_eq(j->id, id)) return j;
    }
    return NULL;
}

/* ---------------- the patient compartment ---------------- */

/* -> the search parameter codes that put `type` in a patient's compartment. */
static jval *compartment_def(fhirdefs *d) {
    return smap_get(&d->compartments, "Patient");
}

static int in_patient_compartment(arena *a, fhirdefs *d, store *st, jval *res,
                                  const char *patient_id) {
    const char *rt = j_gets(res, "resourceType");
    if (!rt) return 0;
    if (str_eq(rt, "Patient")) return str_eq(j_gets(res, "id"), patient_id);
    jval *cd = compartment_def(d);
    jval *rs = cd ? j_get(cd, "resource") : NULL;
    for (int i = 0; i < j_len(rs); i++) {
        jval *r = j_at(rs, i);
        if (!str_eq(j_gets(r, "code"), rt)) continue;
        jval *params = j_get(r, "param");
        for (int k = 0; k < j_len(params); k++) {
            const char *code = j_str_of(j_at(params, k));
            if (!code) continue;
            char qs[256];
            snprintf(qs, sizeof qs, "%s=Patient/%s", code, patient_id);
            squery q;
            memset(&q, 0, sizeof q);
            if (sq_parse(a, d, rt, qs, &q) != 0 || q.bad) continue;
            if (sq_match(a, d, st, res, &q)) return 1;
        }
        return 0;
    }
    return 0;
}

/* ---------------- the export ---------------- */

static bfile *file_for(arena *a, bjob *j, const char *type) {
    (void)a;
    for (size_t i = 0; i < j->files.n; i++) {
        bfile *f = j->files.v[i];
        if (str_eq(f->type, type)) return f;
    }
    bfile *f = calloc(1, sizeof *f);
    f->type = str_dup(type);
    f->name = malloc(strlen(type) + 8);
    sprintf(f->name, "%s.ndjson", type);
    buf_init(&f->data);
    vec_push(&j->files, f);
    return f;
}

static int type_wanted(vec *types, const char *rt) {
    if (!types->n) return 1;
    for (size_t i = 0; i < types->n; i++)
        if (str_eq((const char *)types->v[i], rt)) return 1;
    return 0;
}

const char *bulk_start(arena *a, server *srv, fhirbase *b, const char *level,
                       const char *gid, const char *qs, const char *request_url) {
    (void)srv;
    pthread_mutex_lock(&g_mu);
    if (!g_init) { vec_init(&g_jobs); g_init = 1; }
    bjob *j = calloc(1, sizeof *j);
    char idbuf[32];
    snprintf(idbuf, sizeof idbuf, "export-%lld", g_next++);
    j->id = str_dup(idbuf);
    j->request = str_dup(request_url ? request_url : "");
    char ts[64];
    j->tstamp = str_dup(store_now(ts, sizeof ts));
    vec_init(&j->files);
    vec_push(&g_jobs, j);
    pthread_mutex_unlock(&g_mu);

    /* parameters */
    vec types, filters;
    vec_init(&types);
    vec_init(&filters);
    const char *since = NULL;
    char *q = arena_strdup(a, qs ? qs : "");
    char *sp1 = NULL;
    for (char *t = strtok_r(q, "&", &sp1); t; t = strtok_r(NULL, "&", &sp1)) {
        char *eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = arena_strdup(a, eq + 1);
        url_decode(v);
        if (str_eq(t, "_type")) {
            char *sp2 = NULL;
            for (char *x = strtok_r(v, ",", &sp2); x; x = strtok_r(NULL, ",", &sp2))
                vec_push(&types, x);
        } else if (str_eq(t, "_since")) {
            since = v;
        } else if (str_eq(t, "_typeFilter")) {
            vec_push(&filters, v);
        }
    }

    /* the members of the group, when there is one */
    smap members;
    smap_init(&members, 64);
    int group_level = str_eq(level, "group");
    if (group_level && gid) {
        rec *g = store_get(b->st, "Group", gid);
        jval *gr = g && g->cur && !g->cur->deleted ? g->cur->res : NULL;
        jval *mem = gr ? j_get(gr, "member") : NULL;
        for (int i = 0; i < j_len(mem); i++) {
            jval *e = j_get(j_at(mem, i), "entity");
            const char *ref = e ? j_gets(e, "reference") : NULL;
            if (ref && strncmp(ref, "Patient/", 8) == 0)
                smap_put(&members, arena_strdup(a, ref + 8), (void *)1);
        }
    }

    vec all;
    vec_init(&all);
    store_scan(b->st, NULL, &all);
    long long lo = since ? fhir_date_lo(since) : 0;
    for (size_t i = 0; i < all.n; i++) {
        rec *r = all.v[i];
        if (!r->cur || r->cur->deleted || !r->cur->res) continue;
        const char *rt = r->type;
        if (!type_wanted(&types, rt)) continue;
        if (since) {
            jval *meta = j_get(r->cur->res, "meta");
            const char *lu = meta ? j_gets(meta, "lastUpdated") : NULL;
            if (!lu || fhir_date_lo(lu) < lo) continue;
        }
        if (!str_eq(level, "system")) {
            int keep = 0;
            if (str_eq(rt, "Patient")) {
                keep = group_level ? smap_get(&members, r->id) != NULL : 1;
            } else if (group_level) {
                for (size_t k = 0; k < all.n && !keep; k++) {
                    rec *p = all.v[k];
                    if (!str_eq(p->type, "Patient") || !smap_get(&members, p->id)) continue;
                    keep = in_patient_compartment(a, b->defs, b->st, r->cur->res, p->id);
                }
            } else {
                for (size_t k = 0; k < all.n && !keep; k++) {
                    rec *p = all.v[k];
                    if (!str_eq(p->type, "Patient")) continue;
                    keep = in_patient_compartment(a, b->defs, b->st, r->cur->res, p->id);
                }
            }
            if (!keep) continue;
        }
        int pass = 1;
        for (size_t f = 0; f < filters.n && pass; f++) {
            const char *spec = filters.v[f];
            const char *qm = strchr(spec, '?');
            if (!qm) continue;
            char ftype[64];
            size_t tl = (size_t)(qm - spec);
            if (tl >= sizeof ftype) continue;
            memcpy(ftype, spec, tl);
            ftype[tl] = 0;
            if (!str_eq(ftype, rt)) continue;      /* a filter for another type */
            squery sq;
            memset(&sq, 0, sizeof sq);
            pass = (sq_parse(a, b->defs, rt, qm + 1, &sq) == 0 && !sq.bad &&
                    sq_match(a, b->defs, b->st, r->cur->res, &sq));
        }
        if (!pass) continue;
        bfile *bf = file_for(a, j, rt);
        buf tmp;
        buf_init(&tmp);
        j_write(&tmp, r->cur->res, 0);
        buf_put(&bf->data, tmp.p, tmp.n);
        buf_putc(&bf->data, '\n');
        buf_free(&tmp);
    }
    vec_free(&all);
    pthread_mutex_lock(&g_mu);
    j->progress = 100;
    j->done = 1;
    pthread_mutex_unlock(&g_mu);
    return j->id;
}

int bulk_status(arena *a, server *srv, const char *jobid, jval **body,
                const char **progress) {
    *body = NULL;
    if (progress) *progress = NULL;
    pthread_mutex_lock(&g_mu);
    bjob *j = g_init ? job_get(jobid) : NULL;
    if (!j || j->cancelled) { pthread_mutex_unlock(&g_mu); return 404; }
    if (j->failed) {
        pthread_mutex_unlock(&g_mu);
        return 500;
    }
    if (!j->done) {
        if (progress) *progress = "in progress";
        pthread_mutex_unlock(&g_mu);
        return 202;
    }
    jval *m = j_obj(a);
    j_set(a, m, "transactionTime", j_str(a, j->tstamp));
    j_set(a, m, "request", j_str(a, j->request));
    j_set(a, m, "requiresAccessToken", j_bool(a, 0));
    jval *out = j_arr(a);
    for (size_t i = 0; i < j->files.n; i++) {
        bfile *f = j->files.v[i];
        if (!f->data.n) continue;                  /* no entry for an empty file */
        jval *e = j_obj(a);
        j_set(a, e, "type", j_str(a, f->type));
        j_set(a, e, "url", j_str(a, arena_printf(a, "%s/bulk/%s/%s",
                                                 srv->base_url, j->id, f->name)));
        j_set(a, e, "count", j_int(a, 0));
        long long n = 0;
        for (size_t k = 0; k < f->data.n; k++) if (f->data.p[k] == '\n') n++;
        j_set(a, e, "count", j_int(a, n));
        j_push(a, out, e);
    }
    j_set(a, m, "output", out);
    j_set(a, m, "deleted", j_arr(a));
    j_set(a, m, "error", j_arr(a));
    pthread_mutex_unlock(&g_mu);
    *body = m;
    return 200;
}

int bulk_cancel(const char *jobid) {
    pthread_mutex_lock(&g_mu);
    bjob *j = g_init ? job_get(jobid) : NULL;
    int rc = 404;
    if (j && !j->cancelled) { j->cancelled = 1; rc = 202; }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

const char *bulk_file(const char *jobid, const char *name, size_t *len) {
    pthread_mutex_lock(&g_mu);
    bjob *j = g_init ? job_get(jobid) : NULL;
    const char *p = NULL;
    if (j && !j->cancelled && j->done) {
        for (size_t i = 0; i < j->files.n; i++) {
            bfile *f = j->files.v[i];
            if (str_eq(f->name, name)) { p = f->data.p; *len = f->data.n; break; }
        }
    }
    pthread_mutex_unlock(&g_mu);
    return p;
}

/* ---------------- $everything ---------------- */

jval *everything(arena *a, server *srv, fhirbase *b, const char *type,
                 const char *id, const char *qs) {
    rec *anchor = store_get(b->st, type, id);
    if (!anchor || !anchor->cur || anchor->cur->deleted) return NULL;
    vec types;
    vec_init(&types);
    const char *since = NULL;
    char *q = arena_strdup(a, qs ? qs : "");
    char *sp1 = NULL;
    for (char *t = strtok_r(q, "&", &sp1); t; t = strtok_r(NULL, "&", &sp1)) {
        char *eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = arena_strdup(a, eq + 1);
        url_decode(v);
        if (str_eq(t, "_type")) {
            char *sp2 = NULL;
            for (char *x = strtok_r(v, ",", &sp2); x; x = strtok_r(NULL, ",", &sp2))
                vec_push(&types, x);
        } else if (str_eq(t, "_since")) {
            since = v;
        }
    }
    jval *bundle = j_obj(a);
    j_set(a, bundle, "resourceType", j_str(a, "Bundle"));
    j_set(a, bundle, "type", j_str(a, "searchset"));
    jval *entries = j_arr(a);
    long long lo = since ? fhir_date_lo(since) : 0;
    vec all;
    vec_init(&all);
    store_scan(b->st, NULL, &all);
    int n = 0;
    const char *pid = str_eq(type, "Patient") ? id : NULL;
    if (!pid) {
        jval *subj = j_get(anchor->cur->res, "subject");
        const char *ref = subj ? j_gets(subj, "reference") : NULL;
        if (ref && strncmp(ref, "Patient/", 8) == 0) pid = ref + 8;
    }
    for (size_t i = 0; i < all.n; i++) {
        rec *r = all.v[i];
        if (!r->cur || r->cur->deleted || !r->cur->res) continue;
        if (types.n && !type_wanted(&types, r->type)) continue;
        if (since) {
            jval *meta = j_get(r->cur->res, "meta");
            const char *lu = meta ? j_gets(meta, "lastUpdated") : NULL;
            if (!lu || fhir_date_lo(lu) < lo) continue;
        }
        int keep = 0;
        if (r == anchor) keep = 1;
        else if (pid) keep = in_patient_compartment(a, b->defs, b->st, r->cur->res, pid);
        if (!keep) continue;
        jval *e = j_obj(a);
        j_set(a, e, "fullUrl", j_str(a, arena_printf(a, "%s/%s/%s/%s", srv->base_url,
                                                     b->base_path, r->type, r->id)));
        j_set(a, e, "resource", j_clone(a, r->cur->res));
        jval *s = j_obj(a);
        j_set(a, s, "mode", j_str(a, r == anchor ? "match" : "include"));
        j_set(a, e, "search", s);
        j_push(a, entries, e);
        n++;
    }
    vec_free(&all);
    j_set(a, bundle, "total", j_int(a, n));
    j_set(a, bundle, "entry", entries);
    return bundle;
}
