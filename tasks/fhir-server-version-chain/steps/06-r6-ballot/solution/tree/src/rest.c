#define _GNU_SOURCE
#include "server.h"
#include "search.h"
#include "sub.h"
#include "bulk.h"
#include "smart.h"
#include "convert.h"
#include "validate.h"
#include "term.h"
#include "fpath.h"
#include "fxml.h"
#include "fml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    server    *srv;
    fhirbase  *b;
    arena     *a;
    hreq      *req;
    hresp     *res;
    int        xml;          /* respond in XML */
    const char *prefer;      /* return=minimal|representation|OperationOutcome */
    vec        events;       /* subevent*: what this request wrote, for the
                                subscription dispatch that runs after it */
    smart_token tok;         /* the bearer token, when one was sent */
    const char *scope_patient;  /* a patient/ scope confines the request to this */
    fhirdefs   *render_as_defs; /* the release the fhirVersion parameter asked for */
    const char *render_as_mm;   /* ... as its major.minor, for Content-Type */
} rctx;

/* ---------------- content negotiation ---------------- */

static int wants_xml(const char *accept, const char *fmt) {
    if (fmt) {
        if (strstr(fmt, "xml")) return 1;
        if (strstr(fmt, "json")) return 0;
    }
    if (accept) {
        const char *x = strstr(accept, "xml");
        const char *j = strstr(accept, "json");
        if (x && !j) return 1;
        if (x && j) return x < j;
    }
    return 0;
}

static const char *ct_for(int xml, fhirdefs *d) {
    if (str_eq(d->code, "DSTU2")) return xml ? "application/xml+fhir; charset=UTF-8" : "application/json+fhir; charset=UTF-8";
    return xml ? "application/fhir+xml; charset=UTF-8" : "application/fhir+json; charset=UTF-8";
}

/* The `fhirVersion` parameter of the highest-weighted acceptable media type.
   `versions.html`: the parameter carries major.minor, and it is part of what
   makes a media type acceptable, so a value naming no release this server holds
   makes the whole Accept unacceptable rather than being ignored. */
static const char *version_param(arena *a, const char *hdr, int *saw_any) {
    if (saw_any) *saw_any = 0;
    if (!hdr) return NULL;
    char *h = arena_strdup(a, hdr);
    char *sp = NULL;
    double best_q = -1.0;
    const char *best = NULL;
    for (char *t = strtok_r(h, ",", &sp); t; t = strtok_r(NULL, ",", &sp)) {
        while (*t == ' ') t++;
        double q = 1.0;
        const char *fv = NULL;
        char *semi = strchr(t, ';');
        while (semi) {
            char *p = semi + 1;
            while (*p == ' ') p++;
            char *nxt = strchr(p, ';');
            if (nxt) *nxt = 0;
            if (strncmp(p, "q=", 2) == 0) q = atof(p + 2);
            else if (strncmp(p, "fhirVersion=", 12) == 0) {
                char *v = p + 12;
                while (*v == '"' || *v == ' ') v++;
                char *e = v + strlen(v);
                while (e > v && (e[-1] == '"' || e[-1] == ' ')) *--e = 0;
                fv = v;
                if (saw_any) *saw_any = 1;
            }
            semi = nxt;
        }
        if (q > best_q) { best_q = q; best = fv; }
    }
    return best;
}

/* json.html: "an array is not present if it has no values", and an element with
   neither a value nor children is not legal either. */
static void prune_empty(jval *v) {
    if (!v) return;
    if (j_is(v, J_ARR)) {
        for (int i = 0; i < j_len(v); i++) prune_empty(j_at(v, i));
        return;
    }
    if (!j_is(v, J_OBJ)) return;
    for (jmem *m = v->u.obj.first; m; m = m->next) prune_empty(m->val);
    for (;;) {
        const char *drop = NULL;
        for (jmem *m = v->u.obj.first; m; m = m->next) {
            if (j_is(m->val, J_ARR) && j_len(m->val) == 0) { drop = m->key; break; }
            if (j_is(m->val, J_OBJ) && j_len(m->val) == 0) { drop = m->key; break; }
        }
        if (!drop) break;
        j_del(v, drop);
    }
}

static void send_resource(rctx *c, int status, jval *res) {
    fhirdefs *d = c->b->defs;
    if (c->render_as_defs && c->render_as_defs != d) {
        int dropped = 0;
        jval *conv = render_as(c->a, c->render_as_defs, res, &dropped);
        if (conv) { res = conv; d = c->render_as_defs; }
    }
    prune_empty(res);
    c->res->status = status;
    if (c->render_as_mm && c->render_as_defs)
        hresp_hdrf(c->res, "Content-Type", "%s; fhirVersion=%s",
                   c->xml ? (str_eq(d->code, "DSTU2") ? "application/xml+fhir"
                                                      : "application/fhir+xml")
                          : (str_eq(d->code, "DSTU2") ? "application/json+fhir"
                                                      : "application/fhir+json"),
                   c->render_as_mm);
    else
        hresp_hdr(c->res, "Content-Type", ct_for(c->xml, d));
    if (c->xml) fhir_to_xml(c->a, d, res, &c->res->body);
    else j_write(&c->res->body, res, 1);
}

static jval *outcome(arena *a, const char *sev, const char *code, const char *diag, const char *expr) {
    jval *oo = j_obj(a);
    j_set(a, oo, "resourceType", j_str(a, "OperationOutcome"));
    jval *issues = j_arr(a);
    jval *i = j_obj(a);
    j_set(a, i, "severity", j_str(a, sev));
    j_set(a, i, "code", j_str(a, code));
    if (diag) { jval *det = j_obj(a); j_set(a, det, "text", j_str(a, diag)); j_set(a, i, "details", det); }
    if (diag) j_set(a, i, "diagnostics", j_str(a, diag));
    if (expr && *expr) { jval *e = j_arr(a); j_push(a, e, j_str(a, expr)); j_set(a, i, "expression", e); }
    j_push(a, issues, i);
    j_set(a, oo, "issue", issues);
    return oo;
}

static void send_error(rctx *c, int status, const char *code, const char *diag) {
    send_resource(c, status, outcome(c->a, status >= 500 ? "fatal" : "error", code, diag, NULL));
}

/* ---------------- path handling ---------------- */

typedef struct {
    char *seg[8];
    int   n;
} psplit;

static psplit split_path(arena *a, const char *path) {
    psplit p; memset(&p, 0, sizeof p);
    const char *s = path;
    while (*s == '/') s++;
    while (*s && p.n < 8) {
        const char *e = strchr(s, '/');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len) p.seg[p.n++] = arena_strndup(a, s, len);
        if (!e) break;
        s = e + 1;
    }
    return p;
}

fhirbase *server_base(server *s, const char *seg) {
    for (size_t i = 0; i < s->nbase; i++)
        if (str_eq(s->bases[i]->base_path, seg)) return s->bases[i];
    return NULL;
}

/* ---------------- bundle helpers ---------------- */

static void set_etag(rctx *c, rver *v) {
    hresp_hdrf(c->res, "ETag", "W/\"%d\"", v->vid);
    hresp_hdr(c->res, "Last-Modified", v->last_updated);
}

static jval *bundle_new(arena *a, const char *type) {
    jval *b = j_obj(a);
    j_set(a, b, "resourceType", j_str(a, "Bundle"));
    j_set(a, b, "type", j_str(a, type));
    return b;
}

static void bundle_link(arena *a, jval *bundle, const char *rel, const char *url) {
    jval *links = j_get(bundle, "link");
    if (!links) { links = j_arr(a); j_set(a, bundle, "link", links); }
    jval *l = j_obj(a);
    j_set(a, l, "relation", j_str(a, rel));
    j_set(a, l, "url", j_str(a, url));
    j_push(a, links, l);
}

static jval *subset_summary(arena *a, fhirdefs *d, jval *res, const char *summary, vec *elements);

static jval *entry_for(arena *a, const char *full_url, jval *res, const char *mode) {
    jval *e = j_obj(a);
    if (full_url) j_set(a, e, "fullUrl", j_str(a, full_url));
    if (res) j_set(a, e, "resource", res);
    if (mode) {
        jval *s = j_obj(a);
        j_set(a, s, "mode", j_str(a, mode));
        j_set(a, e, "search", s);
    }
    return e;
}

/* ---------------- search ---------------- */


typedef struct { const char *key; rec *r; } sortent;

static void add_subsetted_tag(arena *a, jval *res) {
    jval *meta = j_get(res, "meta");
    if (!meta) { meta = j_obj(a); j_set(a, meta, "x", j_null(a)); j_del(meta, "x"); j_set(a, res, "meta", meta); }
    jval *tags = j_get(meta, "tag");
    if (!tags) { tags = j_arr(a); j_set(a, meta, "tag", tags); }
    jval *t = j_obj(a);
    j_set(a, t, "system", j_str(a, "http://terminology.hl7.org/CodeSystem/v3-ObservationValue"));
    j_set(a, t, "code", j_str(a, "SUBSETTED"));
    j_push(a, tags, t);
}

static jval *subset_summary(arena *a, fhirdefs *d, jval *res, const char *summary, vec *elements) {
    if ((!summary || str_eq(summary, "false")) && (!elements || elements->n == 0)) return res;
    const char *rt = j_gets(res, "resourceType");
    sdef *sd = defs_resource(d, rt);
    jval *out = j_obj(a);
    j_set(a, out, "resourceType", j_str(a, rt));
    if (j_get(res, "id")) j_set(a, out, "id", j_get(res, "id"));
    if (j_get(res, "meta")) j_set(a, out, "meta", j_clone(a, j_get(res, "meta")));
    if (summary && str_eq(summary, "text")) {
        if (j_get(res, "text")) j_set(a, out, "text", j_get(res, "text"));
        add_subsetted_tag(a, out);
        return out;
    }
    for (jmem *m = res->u.obj.first; m; m = m->next) {
        if (str_eq(m->key, "resourceType") || str_eq(m->key, "id") || str_eq(m->key, "meta")) continue;
        int keep = 0;
        if (elements && elements->n) {
            for (size_t i = 0; i < elements->n; i++)
                if (str_eq((const char *)elements->v[i], m->key)) keep = 1;
        } else if (summary && str_eq(summary, "data")) {
            keep = !str_eq(m->key, "text");
        } else if (summary && str_eq(summary, "true")) {
            edef *e = NULL;
            const char *t = NULL;
            if (sd && sd->root) e = ed_child(d, sd, sd->root, m->key, &t);
            keep = e && (e->is_summary || e->min > 0);
        }
        if (keep) {
            j_set(a, out, m->key, m->val);
            if (elements && elements->n) {
                /* an explicitly requested element keeps its primitive extensions */
                char shadow[256];
                snprintf(shadow, sizeof shadow, "_%s", m->key);
                jval *sh = j_get(res, shadow);
                if (sh) j_set(a, out, shadow, sh);
            }
        }
    }
    /* mandatory elements are always kept */
    if (sd && sd->root)
        for (edef *e = sd->root->first_child; e; e = e->next)
            if (e->min > 0 && j_get(res, e->tail) && !j_get(out, e->tail))
                j_set(a, out, e->tail, j_get(res, e->tail));
    add_subsetted_tag(a, out);
    return out;
}

static void collect_includes(rctx *c, squery *q, vec *matched, jval *entries, smap *seen);

static void do_search(rctx *c, const char *type, const char *qs) {
    arena *a = c->a;
    fhirbase *b = c->b;
    squery q;
    if (sq_parse(a, b->defs, type, qs, &q) != 0) {
        send_resource(c, 400, outcome(a, "error", "processing",
            arena_printf(a, "unknown search parameter '%s'", q.bad_detail ? q.bad_detail : "?"), NULL));
        return;
    }
    vec all; vec_init(&all);
    if (type) store_scan(b->st, type, &all);
    else if (q.types.n) { for (size_t i = 0; i < q.types.n; i++) store_scan(b->st, (const char *)q.types.v[i], &all); }
    else store_scan(b->st, NULL, &all);

    vec matched; vec_init(&matched);
    for (size_t i = 0; i < all.n; i++) {
        rec *r = all.v[i];
        if (!r->cur || r->cur->deleted) continue;
        if (sq_match(a, b->defs, b->st, r->cur->res, &q)) vec_push(&matched, r);
    }
    vec_free(&all);

    /* sort */
    if (q.sort) {
        char *spec = arena_strdup(a, q.sort);
        vec keys; vec_init(&keys);
        for (char *t = strtok(spec, ","); t; t = strtok(NULL, ",")) vec_push(&keys, t);
        for (size_t pass = keys.n; pass-- > 0; ) {
            const char *k = (const char *)keys.v[pass];
            int desc = 0;
            if (*k == '-') { desc = 1; k++; }
            sortent *ents = arena_calloc(a, (matched.n ? matched.n : 1) * sizeof(sortent));
            for (size_t i = 0; i < matched.n; i++) {
                rec *r = matched.v[i];
                ents[i].key = sq_sortkey(a, b->defs, r->cur->res, k);
                ents[i].r = r;
            }
            /* stable insertion sort keeps earlier keys as tie-breakers */
            for (size_t i = 1; i < matched.n; i++) {
                sortent e = ents[i];
                size_t j2 = i;
                while (j2 > 0) {
                    int cmp = strcmp(ents[j2-1].key, e.key);
                    if (desc) cmp = -cmp;
                    if (cmp <= 0) break;
                    ents[j2] = ents[j2-1];
                    j2--;
                }
                ents[j2] = e;
            }
            for (size_t i = 0; i < matched.n; i++) matched.v[i] = ents[i].r;
        }
        vec_free(&keys);
    } else {
        /* deterministic default ordering by type then id */
        for (size_t i = 1; i < matched.n; i++) {
            rec *e = matched.v[i];
            size_t j2 = i;
            while (j2 > 0) {
                rec *p = matched.v[j2-1];
                int cmp = strcmp(p->type, e->type);
                if (cmp == 0) cmp = strcmp(p->id, e->id);
                if (cmp <= 0) break;
                matched.v[j2] = matched.v[j2-1];
                j2--;
            }
            matched.v[j2] = e;
        }
    }

    long total = (long)matched.n;
    long count = q.count >= 0 ? q.count : 50;
    if (count > 1000) count = 1000;
    long offset = q.offset > 0 ? q.offset : 0;

    jval *bundle = bundle_new(a, "searchset");
    j_set(a, bundle, "id", j_str(a, "search"));
    if (!q.total_mode || !str_eq(q.total_mode, "none")) j_set(a, bundle, "total", j_int(a, total));

    buf self; buf_init(&self);
    buf_printf(&self, "%s/%s", c->srv->base_url, b->base_path);
    if (type) buf_printf(&self, "/%s", type);
    if (qs && *qs) buf_printf(&self, "?%s", qs);
    bundle_link(a, bundle, "self", self.p ? self.p : "");
    buf_free(&self);

    jval *entries = j_arr(a);
    if (!(q.summary && str_eq(q.summary, "count")) && count > 0) {
        smap seen; smap_init(&seen, 256);
        vec page; vec_init(&page);
        for (long i = offset; i < total && i < offset + count; i++) {
            rec *r = matched.v[i];
            vec_push(&page, r);
            char *key = arena_printf(a, "%s/%s", r->type, r->id);
            smap_put(&seen, key, r);
            jval *res = subset_summary(a, b->defs, r->cur->res, q.summary, &q.elements);
            j_push(a, entries, entry_for(a,
                arena_printf(a, "%s/%s/%s/%s", c->srv->base_url, b->base_path, r->type, r->id),
                res, "match"));
        }
        collect_includes(c, &q, &page, entries, &seen);
        vec_free(&page);
        smap_free(&seen);
    }
    j_set(a, bundle, "entry", entries);

    /* paging links */
    if (count > 0 && offset + count < total) {
        buf nx; buf_init(&nx);
        buf_printf(&nx, "%s/%s", c->srv->base_url, b->base_path);
        if (type) buf_printf(&nx, "/%s", type);
        buf_putc(&nx, '?');
        /* strip any previous _offset/_count */
        char *copy = arena_strdup(a, qs ? qs : "");
        int first = 1;
        for (char *t = strtok(copy, "&"); t; t = strtok(NULL, "&")) {
            if (str_starts(t, "_offset=") || str_starts(t, "_count=")) continue;
            if (!first) buf_putc(&nx, '&');
            buf_puts(&nx, t);
            first = 0;
        }
        if (!first) buf_putc(&nx, '&');
        buf_printf(&nx, "_count=%ld&_offset=%ld", count, offset + count);
        bundle_link(a, bundle, "next", nx.p);
        buf_free(&nx);
    }
    vec_free(&matched);
    send_resource(c, 200, bundle);
}

static void include_pass(rctx *c, vec *spec, int rev, vec *sources, jval *entries, smap *seen, vec *added) {
    arena *a = c->a;
    fhirbase *b = c->b;
    for (size_t i = 0; i < spec->n; i++) {
        char *s = arena_strdup(a, (const char *)spec->v[i]);
        char *it = strchr(s, '\x01');
        if (it) *it = 0;
        char *p1 = strchr(s, ':');
        /* `_include=*` is the wildcard for every reference parameter of the
           result type (`search.html`). */
        if (!p1 && str_eq(s, "*")) { s = arena_strdup(a, "*:*"); p1 = strchr(s, ':'); }
        if (!p1) continue;
        *p1 = 0;
        char *code = p1 + 1;
        char *p2 = strchr(code, ':');
        char *target = NULL;
        if (p2) { *p2 = 0; target = p2 + 1; }
        const char *stype = s;
        if (!rev) {
            for (size_t k = 0; k < sources->n; k++) {
                rec *r = sources->v[k];
                if (!str_eq(r->type, stype) && !str_eq(stype, "*")) continue;
                vec codes; vec_init(&codes);
                if (str_eq(code, "*")) {
                    for (size_t z = 0; z < b->defs->sparams.n; z++) {
                        sparam *sp = b->defs->sparams.v[z];
                        if (!str_eq(sp->type, "reference")) continue;
                        for (size_t y = 0; y < sp->bases.n; y++)
                            if (str_eq((const char *)sp->bases.v[y], r->type)) vec_push(&codes, sp);
                    }
                } else {
                    sparam *sp = defs_sp(b->defs, stype, code);
                    if (sp) vec_push(&codes, sp);
                }
                for (size_t z = 0; z < codes.n; z++) {
                    vec refs; vec_init(&refs);
                    sq_values(a, b->defs, r->cur->res, codes.v[z], &refs);
                    for (size_t y = 0; y < refs.n; y++) {
                        const char *ref = (const char *)refs.v[y];
                        if (!ref || *ref == '#') continue;
                        const char *slash = strrchr(ref, '/');
                        if (!slash) continue;
                        char *id = arena_strdup(a, slash + 1);
                        char *pre = arena_strndup(a, ref, (size_t)(slash - ref));
                        const char *ts = strrchr(pre, '/');
                        const char *rtype = ts ? ts + 1 : pre;
                        if (target && !str_eq(target, rtype)) continue;
                        rec *tr = store_get(b->st, rtype, id);
                        if (!tr || !tr->cur || tr->cur->deleted) continue;
                        char *key = arena_printf(a, "%s/%s", rtype, id);
                        if (smap_get(seen, key)) continue;
                        smap_put(seen, key, tr);
                        j_push(a, entries, entry_for(a,
                            arena_printf(a, "%s/%s/%s/%s", c->srv->base_url, b->base_path, rtype, id),
                            tr->cur->res, "include"));
                        if (added) vec_push(added, tr);
                    }
                    vec_free(&refs);
                }
                vec_free(&codes);
            }
        } else {
            vec cand; vec_init(&cand);
            store_scan(b->st, str_eq(stype, "*") ? NULL : stype, &cand);
            for (size_t k = 0; k < cand.n; k++) {
                rec *r = cand.v[k];
                if (!r->cur || r->cur->deleted) continue;
                sparam *sp = defs_sp(b->defs, r->type, code);
                if (!sp) continue;
                vec refs; vec_init(&refs);
                sq_values(a, b->defs, r->cur->res, sp, &refs);
                int hit = 0;
                for (size_t y = 0; y < refs.n && !hit; y++) {
                    const char *ref = (const char *)refs.v[y];
                    for (size_t z = 0; z < sources->n; z++) {
                        rec *src = sources->v[z];
                        char *want = arena_printf(a, "%s/%s", src->type, src->id);
                        size_t rl = strlen(ref), wl = strlen(want);
                        if (rl >= wl && str_eq(ref + rl - wl, want) && (rl == wl || ref[rl-wl-1] == '/')) { hit = 1; break; }
                    }
                }
                vec_free(&refs);
                if (!hit) continue;
                char *key = arena_printf(a, "%s/%s", r->type, r->id);
                if (smap_get(seen, key)) continue;
                smap_put(seen, key, r);
                j_push(a, entries, entry_for(a,
                    arena_printf(a, "%s/%s/%s/%s", c->srv->base_url, b->base_path, r->type, r->id),
                    r->cur->res, "include"));
                if (added) vec_push(added, r);
            }
            vec_free(&cand);
        }
    }
}

static int has_iterate(vec *spec) {
    for (size_t i = 0; i < spec->n; i++) if (strchr((const char *)spec->v[i], '\x01')) return 1;
    return 0;
}

static void collect_includes(rctx *c, squery *q, vec *matched, jval *entries, smap *seen) {
    vec added; vec_init(&added);
    include_pass(c, &q->includes, 0, matched, entries, seen, &added);
    include_pass(c, &q->revincludes, 1, matched, entries, seen, &added);
    int rounds = (has_iterate(&q->includes) || has_iterate(&q->revincludes)) ? 4 : 0;
    while (rounds-- > 0 && added.n) {
        vec next; vec_init(&next);
        include_pass(c, &q->includes, 0, &added, entries, seen, &next);
        include_pass(c, &q->revincludes, 1, &added, entries, seen, &next);
        vec_free(&added);
        added = next;
    }
    vec_free(&added);
}

/* ---------------- parse request body ---------------- */

static jval *parse_body(rctx *c, int *bad) {
    *bad = 0;
    const char *ct = hreq_hdr(c->req, "Content-Type");
    if (!c->req->body || c->req->blen == 0) { *bad = 1; return NULL; }
    if (ct && strstr(ct, "fhir-mapping")) {
        /* FHIR Mapping Language source: mapping-language.html */
        char err[256] = {0};
        fmlmap *m = fml_parse(c->a, arena_strndup(c->a, c->req->body, c->req->blen), err, sizeof err);
        if (!m) { *bad = 1; return NULL; }
        return fml_to_resource(c->a, m);
    }
    if (ct && strstr(ct, "xml")) {
        char err[256] = {0};
        jval *v = fhir_from_xml(c->a, c->b->defs, c->req->body, c->req->blen, err, sizeof err);
        if (!v) *bad = 1;
        return v;
    }
    char err[256] = {0};
    jval *v = j_parse(c->a, c->req->body, c->req->blen, err, sizeof err);
    if (!v) *bad = 1;
    return v;
}

/* ---------------- interactions ---------------- */

static int find_matching(rctx *c, const char *type, const char *qs, vec *out) {
    squery q;
    if (sq_parse(c->a, c->b->defs, type, qs, &q) != 0) return -1;
    vec all; vec_init(&all);
    store_scan(c->b->st, type, &all);
    for (size_t i = 0; i < all.n; i++) {
        rec *r = all.v[i];
        if (!r->cur || r->cur->deleted) continue;
        if (sq_match(c->a, c->b->defs, c->b->st, r->cur->res, &q)) vec_push(out, r);
    }
    vec_free(&all);
    return 0;
}

/* Every write goes through these so that the subscription dispatch sees the
   resource before and after, once per resource, and only for writes that
   actually happened -- a transaction that writes five resources notifies five
   times, and a rejected write notifies not at all. */
static rver *put_notify(rctx *c, const char *type, const char *id, jval *res) {
    rec *old = store_get(c->b->st, type, id);
    jval *before = (old && old->cur && !old->cur->deleted && old->cur->res)
                   ? j_clone(c->a, old->cur->res) : NULL;
    rver *v = store_put(c->b->st, type, id, res);
    sub_record(c->a, &c->events, before ? "update" : "create", type, id, before,
               v && v->res ? v->res : res);
    return v;
}

static rver *delete_notify(rctx *c, const char *type, const char *id) {
    rec *old = store_get(c->b->st, type, id);
    jval *before = (old && old->cur && !old->cur->deleted && old->cur->res)
                   ? j_clone(c->a, old->cur->res) : NULL;
    rver *v = store_delete(c->b->st, type, id);
    if (before) sub_record(c->a, &c->events, "delete", type, id, before, NULL);
    return v;
}

static void respond_written(rctx *c, int status, const char *type, const char *id, rver *v, int created) {
    set_etag(c, v);
    if (created)
        hresp_hdrf(c->res, "Location", "%s/%s/%s/%s/_history/%d",
                   c->srv->base_url, c->b->base_path, type, id, v->vid);
    const char *pref = c->prefer ? c->prefer : "";
    if (strstr(pref, "minimal")) {
        c->res->status = status;
        return;
    }
    if (strstr(pref, "OperationOutcome")) {
        send_resource(c, status, outcome(c->a, "information", "informational", "resource written", NULL));
        return;
    }
    send_resource(c, status, v->res);
}

__attribute__((unused)) static int validate_or_fail(rctx *c, jval *res) {
    vresult vr; vres_init(&vr, c->a);
    validate_resource(c->a, c->b->defs, res, NULL, &vr);
    if (!vr.errors) { free(vr.v); return 0; }
    jval *oo = j_obj(c->a);
    j_set(c->a, oo, "resourceType", j_str(c->a, "OperationOutcome"));
    jval *issues = j_arr(c->a);
    for (size_t i = 0; i < vr.n; i++) {
        jval *is = j_obj(c->a);
        j_set(c->a, is, "severity", j_str(c->a, vr.v[i].severity));
        j_set(c->a, is, "code", j_str(c->a, vr.v[i].code));
        jval *det = j_obj(c->a);
        j_set(c->a, det, "text", j_str(c->a, vr.v[i].text));
        j_set(c->a, is, "details", det);
        j_set(c->a, is, "diagnostics", j_str(c->a, vr.v[i].text));
        if (vr.v[i].expression && *vr.v[i].expression) {
            jval *e = j_arr(c->a);
            j_push(c->a, e, j_str(c->a, vr.v[i].expression));
            j_set(c->a, is, "expression", e);
        }
        j_push(c->a, issues, is);
    }
    j_set(c->a, oo, "issue", issues);
    free(vr.v);
    send_resource(c, 422, oo);
    return 1;
}

static jval *validation_outcome(arena *a, fhirdefs *d, jval *res, const char *profile) {
    vresult vr; vres_init(&vr, a);
    validate_resource(a, d, res, profile, &vr);
    jval *oo = j_obj(a);
    j_set(a, oo, "resourceType", j_str(a, "OperationOutcome"));
    jval *issues = j_arr(a);
    for (size_t i = 0; i < vr.n; i++) {
        jval *is = j_obj(a);
        j_set(a, is, "severity", j_str(a, vr.v[i].severity));
        j_set(a, is, "code", j_str(a, vr.v[i].code));
        jval *det = j_obj(a);
        j_set(a, det, "text", j_str(a, vr.v[i].text));
        j_set(a, is, "details", det);
        j_set(a, is, "diagnostics", j_str(a, vr.v[i].text));
        if (vr.v[i].expression && *vr.v[i].expression) {
            jval *e = j_arr(a);
            j_push(a, e, j_str(a, vr.v[i].expression));
            j_set(a, is, "expression", e);
        }
        j_push(a, issues, is);
    }
    if (vr.n == 0) {
        jval *is = j_obj(a);
        j_set(a, is, "severity", j_str(a, "information"));
        j_set(a, is, "code", j_str(a, "informational"));
        jval *det = j_obj(a);
        j_set(a, det, "text", j_str(a, "All OK"));
        j_set(a, is, "details", det);
        j_push(a, issues, is);
    }
    j_set(a, oo, "issue", issues);
    free(vr.v);
    return oo;
}

#include "rest_ops.inc"
