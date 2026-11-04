/* sub.c -- topic-based subscriptions.
 *
 * R4B and R5 say the same thing in two shapes.  R5's Subscription carries
 * `topic`, `filterBy`, `channelType`, `endpoint`, `content`; R4B's Subscription
 * is still R4's resource, and the subscriptions backport IG puts the topic in
 * `criteria` and everything else in extensions.  Every accessor below reads
 * both, so the matching, the notification and the two operations are written
 * once.
 */
#include "sub.h"
#include "search.h"
#include "http.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>

#define BACKPORT "http://hl7.org/fhir/uv/subscriptions-backport/StructureDefinition/"
#define CHANNEL_CS "http://terminology.hl7.org/CodeSystem/subscription-channel-type"

/* ---------------- per-subscription state ----------------
   The event counter and the log of what has already been sent.  Held in memory
   and keyed by base path and id: a notification's `eventNumber` is a property of
   the delivery stream, not of the stored resource. */

typedef struct {
    long long   number;
    char       *ts;
    char       *ftype, *fid;
    int         fver;
} subev;

typedef struct {
    char     *base, *id;
    long long count;
    vec       events;          /* subev* */
} substate;

static vec             g_states;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int             g_init;

static substate *state_for(const char *base, const char *id) {
    if (!g_init) { vec_init(&g_states); g_init = 1; }
    for (size_t i = 0; i < g_states.n; i++) {
        substate *s = g_states.v[i];
        if (str_eq(s->base, base) && str_eq(s->id, id)) return s;
    }
    substate *s = calloc(1, sizeof *s);
    s->base = str_dup(base);
    s->id = str_dup(id);
    vec_init(&s->events);
    vec_push(&g_states, s);
    return s;
}

/* ---------------- reading a Subscription, either shape ---------------- */

static int is_r5(fhirdefs *d) {
    return d && d->ver && strcmp(d->ver, "5.0.0") >= 0;
}

/* The extension array that decorates a primitive element: FHIR JSON carries it
   under `_<name>`. */
static jval *prim_ext(jval *obj, const char *name) {
    char key[64];
    snprintf(key, sizeof key, "_%s", name);
    jval *u = j_get(obj, key);
    return u ? j_get(u, "extension") : NULL;
}

static jval *ext_find(jval *exts, const char *url) {
    for (int i = 0; exts && i < j_len(exts); i++) {
        jval *e = j_at(exts, i);
        if (str_eq(j_gets(e, "url"), url)) return e;
    }
    return NULL;
}

static const char *ext_value_str(jval *exts, const char *url) {
    jval *e = ext_find(exts, url);
    if (!e) return NULL;
    for (size_t i = 0; i < (size_t)j_len(e); i++) (void)i;
    const char *keys[] = {"valueString", "valueCode", "valueUri", "valueCanonical", NULL};
    for (int k = 0; keys[k]; k++) {
        const char *v = j_gets(e, keys[k]);
        if (v) return v;
    }
    jval *c = j_get(e, "valueCoding");
    if (c) return j_gets(c, "code");
    return NULL;
}

static long long ext_value_int(jval *exts, const char *url, long long dflt) {
    jval *e = ext_find(exts, url);
    if (!e) return dflt;
    const char *keys[] = {"valueUnsignedInt", "valuePositiveInt", "valueInteger", NULL};
    for (int k = 0; keys[k]; k++) {
        jval *v = j_get(e, keys[k]);
        if (v && j_num_of(v)) return atoll(j_num_of(v));
    }
    return dflt;
}

static const char *sub_topic_of(fhirdefs *d, jval *s) {
    if (is_r5(d)) return j_gets(s, "topic");
    return j_gets(s, "criteria");            /* the backport puts it here */
}

static const char *sub_endpoint_of(fhirdefs *d, jval *s) {
    if (is_r5(d)) return j_gets(s, "endpoint");
    jval *ch = j_get(s, "channel");
    return ch ? j_gets(ch, "endpoint") : NULL;
}

static const char *sub_channel_of(fhirdefs *d, jval *s) {
    if (is_r5(d)) {
        jval *ct = j_get(s, "channelType");
        return ct ? j_gets(ct, "code") : NULL;
    }
    jval *ch = j_get(s, "channel");
    return ch ? j_gets(ch, "type") : NULL;
}

static const char *sub_content_of(fhirdefs *d, jval *s) {
    if (is_r5(d)) {
        const char *c = j_gets(s, "content");
        return c ? c : "empty";
    }
    jval *ch = j_get(s, "channel");
    const char *c = ch ? ext_value_str(prim_ext(ch, "payload"), BACKPORT "backport-payload-content")
                       : NULL;
    return c ? c : (ch && j_gets(ch, "payload") ? "full-resource" : "empty");
}

static const char *sub_ctype_of(fhirdefs *d, jval *s) {
    const char *c = is_r5(d) ? j_gets(s, "contentType")
                             : (j_get(s, "channel") ? j_gets(j_get(s, "channel"), "payload")
                                                    : NULL);
    return c ? c : "application/fhir+json";
}

static long long sub_maxcount_of(fhirdefs *d, jval *s) {
    if (is_r5(d)) {
        jval *v = j_get(s, "maxCount");
        return v && j_num_of(v) ? atoll(j_num_of(v)) : 0;
    }
    jval *ch = j_get(s, "channel");
    return ch ? ext_value_int(j_get(ch, "extension"), BACKPORT "backport-max-count", 0) : 0;
}

/* Each filter as a `<param>=<value>` query fragment for its resource type.
   `out_type` receives the type the filter applies to, when the shape names it. */
static void sub_filters_of(arena *a, fhirdefs *d, jval *s, vec *out /* char* */) {
    if (is_r5(d)) {
        jval *fb = j_get(s, "filterBy");
        for (int i = 0; fb && i < j_len(fb); i++) {
            jval *f = j_at(fb, i);
            const char *p = j_gets(f, "filterParameter");
            const char *v = j_gets(f, "value");
            const char *cmp = j_gets(f, "comparator");
            const char *mod = j_gets(f, "modifier");
            if (!p || !v) continue;
            vec_push(out, arena_printf(a, "%s%s%s=%s%s", p,
                                       mod ? ":" : "", mod ? mod : "",
                                       cmp ? cmp : "", v));
        }
        return;
    }
    jval *exts = prim_ext(s, "criteria");
    for (int i = 0; exts && i < j_len(exts); i++) {
        jval *e = j_at(exts, i);
        if (!str_eq(j_gets(e, "url"), BACKPORT "backport-filter-criteria")) continue;
        const char *v = j_gets(e, "valueString");
        if (!v) continue;
        const char *q = strchr(v, '?');
        vec_push(out, arena_strdup(a, q ? q + 1 : v));   /* "Encounter?patient=x" */
    }
}

/* ---------------- the topic ---------------- */

static jval *topic_by_url(fhirbase *b, const char *url) {
    if (!url) return NULL;
    vec all;
    vec_init(&all);
    store_scan(b->st, "SubscriptionTopic", &all);
    jval *found = NULL;
    for (size_t i = 0; i < all.n && !found; i++) {
        rec *r = all.v[i];
        if (!r->cur || r->cur->deleted || !r->cur->res) continue;
        const char *u = j_gets(r->cur->res, "url");
        if (u && (str_eq(u, url) || (strncmp(url, u, strlen(u)) == 0 && url[strlen(u)] == '|')))
            found = r->cur->res;
    }
    vec_free(&all);
    return found;
}

static const char *type_tail(const char *s) {
    if (!s) return NULL;
    const char *sl = strrchr(s, '/');
    return sl ? sl + 1 : s;
}

static int match_query(arena *a, fhirbase *b, const char *type, const char *qs, jval *res) {
    if (!qs || !*qs || !res) return 1;
    squery q;
    memset(&q, 0, sizeof q);
    if (sq_parse(a, b->defs, type, qs, &q) != 0 || q.bad) return 0;
    return sq_match(a, b->defs, b->st, res, &q);
}

/* Does this topic fire for this change? */
static int topic_fires(arena *a, fhirbase *b, jval *topic, const subevent *ev) {
    jval *trigs = j_get(topic, "resourceTrigger");
    for (int i = 0; trigs && i < j_len(trigs); i++) {
        jval *t = j_at(trigs, i);
        const char *want = type_tail(j_gets(t, "resource"));
        if (want && !str_eq(want, ev->type)) continue;
        jval *ints = j_get(t, "supportedInteraction");
        int ok = (ints == NULL || j_len(ints) == 0);
        for (int k = 0; ints && k < j_len(ints); k++)
            if (str_eq(j_str_of(j_at(ints, k)), ev->interaction)) ok = 1;
        if (!ok) continue;
        jval *qc = j_get(t, "queryCriteria");
        if (!qc) return 1;
        const char *cur = j_gets(qc, "current");
        const char *prev = j_gets(qc, "previous");
        jval *rb = j_get(qc, "requireBoth");
        int both = rb && j_is(rb, J_BOOL) && rb->u.b;
        int mcur = cur ? (ev->after && match_query(a, b, ev->type, cur, ev->after)) : -1;
        int mprev = prev ? (ev->before && match_query(a, b, ev->type, prev, ev->before)) : -1;
        if (str_eq(ev->interaction, "create") && prev && !ev->before) {
            jval *rfc = j_get(qc, "resultForCreate");
            mprev = str_eq(j_str_of(rfc), "test-passes") ? 1 : 0;
        }
        if (str_eq(ev->interaction, "delete") && cur && !ev->after) {
            jval *rfd = j_get(qc, "resultForDelete");
            mcur = str_eq(j_str_of(rfd), "test-passes") ? 1 : 0;
        }
        if (mcur < 0 && mprev < 0) return 1;
        if (both) { if (mcur > 0 && mprev > 0) return 1; }
        else      { if (mcur > 0 || mprev > 0) return 1; }
    }
    return 0;
}

/* ---------------- building a notification ---------------- */

static const char *now_instant(char *buf, size_t n) {
    return store_now(buf, n);
}

static jval *status_resource(arena *a, fhirbase *b, const char *kind, jval *sub,
                             const char *sub_id, substate *st, vec *evs /* subev* */,
                             const char *error) {
    jval *ss = j_obj(a);
    j_set(a, ss, "resourceType", j_str(a, "SubscriptionStatus"));
    j_set(a, ss, "status", j_str(a, j_gets(sub, "status") ? j_gets(sub, "status") : "active"));
    j_set(a, ss, "type", j_str(a, kind));
    j_set(a, ss, "eventsSinceSubscriptionStart", j_int(a, st->count));
    if (evs && evs->n) {
        jval *arr = j_arr(a);
        for (size_t i = 0; i < evs->n; i++) {
            subev *e = evs->v[i];
            jval *n = j_obj(a);
            j_set(a, n, "eventNumber", j_int(a, e->number));
            if (e->ts) j_set(a, n, "timestamp", j_str(a, e->ts));
            if (e->ftype && e->fid) {
                jval *f = j_obj(a);
                j_set(a, f, "reference", j_str(a, arena_printf(a, "%s/%s", e->ftype, e->fid)));
                j_set(a, n, "focus", f);
            }
            j_push(a, arr, n);
        }
        j_set(a, ss, "notificationEvent", arr);
    }
    jval *sr = j_obj(a);
    j_set(a, sr, "reference", j_str(a, arena_printf(a, "Subscription/%s", sub_id)));
    j_set(a, ss, "subscription", sr);
    const char *topic = sub_topic_of(b->defs, sub);
    if (topic) j_set(a, ss, "topic", j_str(a, topic));
    if (error) {
        jval *arr = j_arr(a), *cc = j_obj(a), *cod = j_arr(a), *c = j_obj(a);
        j_set(a, c, "system", j_str(a, "http://terminology.hl7.org/CodeSystem/subscription-error"));
        j_set(a, c, "code", j_str(a, "no-response"));
        j_push(a, cod, c);
        j_set(a, cc, "coding", cod);
        j_set(a, cc, "text", j_str(a, error));
        j_push(a, arr, cc);
        j_set(a, ss, "error", arr);
    }
    return ss;
}

static jval *notification_bundle(arena *a, server *srv, fhirbase *b, jval *sub,
                                 const char *sub_id, const char *kind, substate *st,
                                 vec *evs, const char *content, const char *error) {
    char ts[64];
    jval *bn = j_obj(a);
    j_set(a, bn, "resourceType", j_str(a, "Bundle"));
    j_set(a, bn, "type", j_str(a, is_r5(b->defs) ? "subscription-notification" : "history"));
    j_set(a, bn, "timestamp", j_str(a, now_instant(ts, sizeof ts)));
    jval *entries = j_arr(a);
    jval *e0 = j_obj(a);
    j_set(a, e0, "resource", status_resource(a, b, kind, sub, sub_id, st, evs, error));
    j_push(a, entries, e0);
    if (content && !str_eq(content, "empty") && evs) {
        for (size_t i = 0; i < evs->n; i++) {
            subev *e = evs->v[i];
            if (!e->ftype || !e->fid) continue;
            jval *en = j_obj(a);
            j_set(a, en, "fullUrl", j_str(a, arena_printf(a, "%s/%s/%s/%s", srv->base_url,
                                                          b->base_path, e->ftype, e->fid)));
            if (str_eq(content, "full-resource")) {
                rec *r = store_get(b->st, e->ftype, e->fid);
                if (r && r->cur && r->cur->res && !r->cur->deleted)
                    j_set(a, en, "resource", j_clone(a, r->cur->res));
            }
            jval *rq = j_obj(a);
            j_set(a, rq, "method", j_str(a, "GET"));
            j_set(a, rq, "url", j_str(a, arena_printf(a, "%s/%s", e->ftype, e->fid)));
            j_set(a, en, "request", rq);
            jval *rs = j_obj(a);
            j_set(a, rs, "status", j_str(a, "200"));
            j_set(a, en, "response", rs);
            j_push(a, entries, en);
        }
    }
    j_set(a, bn, "entry", entries);
    return bn;
}

static int deliver(arena *a, jval *bundle, const char *endpoint, const char *ctype) {
    buf out;
    buf_init(&out);
    j_write(&out, bundle, 0);
    int st = http_post_url(endpoint, ctype, out.p, out.n, 10);
    buf_free(&out);
    (void)a;
    return st;
}

/* ---------------- the public entry points ---------------- */

void sub_record(arena *a, vec *events, const char *interaction,
                const char *type, const char *id, jval *before, jval *after) {
    subevent *e = arena_calloc(a, sizeof *e);
    e->interaction = interaction;
    e->type = arena_strdup(a, type);
    e->id = arena_strdup(a, id);
    e->before = before;
    e->after = after;
    vec_push(events, e);
}

static void set_status(fhirbase *b, const char *id, const char *status, const char *err) {
    rec *r = store_get(b->st, "Subscription", id);
    if (!r || !r->cur || !r->cur->res) return;
    arena tmp;
    arena_init(&tmp);
    jval *copy = j_clone(&tmp, r->cur->res);
    j_set(&tmp, copy, "status", j_str(&tmp, status));
    if (err && !is_r5(b->defs)) j_set(&tmp, copy, "error", j_str(&tmp, err));
    store_put(b->st, "Subscription", id, copy);
    arena_free(&tmp);
}

void sub_handshake(server *srv, fhirbase *b, const char *id) {
    rec *r = store_get(b->st, "Subscription", id);
    if (!r || !r->cur || !r->cur->res || r->cur->deleted) return;
    jval *sub = r->cur->res;
    if (!str_eq(j_gets(sub, "status"), "requested")) return;
    const char *ch = sub_channel_of(b->defs, sub);
    if (ch && !str_eq(ch, "rest-hook")) return;          /* nothing to shake hands with */
    const char *ep = sub_endpoint_of(b->defs, sub);
    arena a;
    arena_init(&a);
    pthread_mutex_lock(&g_mu);
    substate *st = state_for(b->base_path, id);
    pthread_mutex_unlock(&g_mu);
    jval *bn = notification_bundle(&a, srv, b, sub, id, "handshake", st, NULL,
                                   "empty", NULL);
    int code = ep ? deliver(&a, bn, ep, sub_ctype_of(b->defs, sub)) : 0;
    arena_free(&a);
    if (code >= 200 && code < 300) set_status(b, id, "active", NULL);
    else set_status(b, id, "error", "the notification endpoint did not accept the handshake");
}

void sub_dispatch(server *srv, fhirbase *b, vec *events) {
    if (!events || !events->n) return;
    vec subs;
    vec_init(&subs);
    store_scan(b->st, "Subscription", &subs);
    for (size_t i = 0; i < subs.n; i++) {
        rec *r = subs.v[i];
        if (!r->cur || r->cur->deleted || !r->cur->res) continue;
        jval *sub = r->cur->res;
        if (!str_eq(j_gets(sub, "status"), "active")) continue;
        const char *ch = sub_channel_of(b->defs, sub);
        if (ch && !str_eq(ch, "rest-hook")) continue;
        arena a;
        arena_init(&a);
        jval *topic = topic_by_url(b, sub_topic_of(b->defs, sub));
        if (!topic) { arena_free(&a); continue; }
        vec filters;
        vec_init(&filters);
        sub_filters_of(&a, b->defs, sub, &filters);

        vec fired;
        vec_init(&fired);
        for (size_t k = 0; k < events->n; k++) {
            subevent *ev = events->v[k];
            if (str_eq(ev->type, "Subscription") || str_eq(ev->type, "SubscriptionTopic"))
                continue;                       /* the machinery does not notify on itself */
            if (!topic_fires(&a, b, topic, ev)) continue;
            int pass = 1;
            for (size_t f = 0; f < filters.n && pass; f++) {
                const char *q = filters.v[f];
                const char *body = strchr(q, '?');
                const char *type = ev->type;
                if (body) {
                    size_t tl = (size_t)(body - q);
                    char *t = arena_strndup(&a, q, tl);
                    if (!str_eq(t, ev->type)) continue;   /* a filter for another type */
                    type = t;
                    q = body + 1;
                }
                pass = match_query(&a, b, type, q, ev->after ? ev->after : ev->before);
            }
            if (!pass) continue;
            vec_push(&fired, ev);
        }
        if (fired.n) {
            char ts[64];
            now_instant(ts, sizeof ts);
            pthread_mutex_lock(&g_mu);
            substate *st = state_for(b->base_path, r->id);
            vec batch;
            vec_init(&batch);
            for (size_t k = 0; k < fired.n; k++) {
                subevent *ev = fired.v[k];
                subev *e = calloc(1, sizeof *e);
                e->number = ++st->count;
                e->ts = str_dup(ts);
                e->ftype = str_dup(ev->type);
                e->fid = str_dup(ev->id);
                vec_push(&st->events, e);
                vec_push(&batch, e);
            }
            long long cap = sub_maxcount_of(b->defs, sub);
            pthread_mutex_unlock(&g_mu);
            (void)cap;
            jval *bn = notification_bundle(&a, srv, b, sub, r->id, "event-notification",
                                           st, &batch, sub_content_of(b->defs, sub), NULL);
            const char *ep = sub_endpoint_of(b->defs, sub);
            if (ep) deliver(&a, bn, ep, sub_ctype_of(b->defs, sub));
            vec_free(&batch);
        }
        vec_free(&fired);
        vec_free(&filters);
        arena_free(&a);
    }
    vec_free(&subs);
}

jval *sub_status(arena *a, server *srv, fhirbase *b, vec *ids, const char *want_status) {
    (void)srv;
    jval *bn = j_obj(a);
    j_set(a, bn, "resourceType", j_str(a, "Bundle"));
    j_set(a, bn, "type", j_str(a, "searchset"));
    jval *entries = j_arr(a);
    vec subs;
    vec_init(&subs);
    store_scan(b->st, "Subscription", &subs);
    int n = 0;
    for (size_t i = 0; i < subs.n; i++) {
        rec *r = subs.v[i];
        if (!r->cur || r->cur->deleted || !r->cur->res) continue;
        if (ids && ids->n) {
            int want = 0;
            for (size_t k = 0; k < ids->n; k++) if (str_eq(ids->v[k], r->id)) want = 1;
            if (!want) continue;
        }
        if (want_status && !str_eq(j_gets(r->cur->res, "status"), want_status)) continue;
        pthread_mutex_lock(&g_mu);
        substate *st = state_for(b->base_path, r->id);
        pthread_mutex_unlock(&g_mu);
        jval *e = j_obj(a);
        j_set(a, e, "resource", status_resource(a, b, "query-status", r->cur->res,
                                                r->id, st, NULL, NULL));
        j_push(a, entries, e);
        n++;
    }
    vec_free(&subs);
    j_set(a, bn, "total", j_int(a, n));
    j_set(a, bn, "entry", entries);
    return bn;
}

jval *sub_events(arena *a, server *srv, fhirbase *b, const char *id,
                 long long since, long long until, const char *content) {
    rec *r = store_get(b->st, "Subscription", id);
    if (!r || !r->cur || r->cur->deleted || !r->cur->res) return NULL;
    pthread_mutex_lock(&g_mu);
    substate *st = state_for(b->base_path, id);
    vec batch;
    vec_init(&batch);
    for (size_t i = 0; i < st->events.n; i++) {
        subev *e = st->events.v[i];
        if (since >= 0 && e->number < since) continue;
        if (until >= 0 && e->number > until) continue;
        vec_push(&batch, e);
    }
    pthread_mutex_unlock(&g_mu);
    if (!content) content = sub_content_of(b->defs, r->cur->res);
    jval *bn = notification_bundle(a, srv, b, r->cur->res, id, "query-event",
                                   st, &batch, content, NULL);
    vec_free(&batch);
    return bn;
}
