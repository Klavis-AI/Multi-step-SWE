/* rest_ops.inc -- routing and the individual REST interactions (included by rest.c). */

static void do_history(rctx *c, const char *type, const char *id, const char *qs) {
    arena *a = c->a;
    vec recs; vec_init(&recs);
    if (type && id) { rec *r = store_get(c->b->st, type, id); if (r) vec_push(&recs, r); }
    else store_scan(c->b->st, type, &recs);
    if (type && id && recs.n == 0) { send_error(c, 404, "not-found", "resource not found"); return; }

    long count = 50, offset = 0;
    const char *since = NULL, *at = NULL;
    char *q = arena_strdup(a, qs ? qs : "");
    for (char *t = strtok(q, "&"); t; t = strtok(NULL, "&")) {
        char *eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = arena_strdup(a, eq + 1);
        url_decode(v);
        if (str_eq(t, "_count")) count = strtol(v, NULL, 10);
        else if (str_eq(t, "_offset")) offset = strtol(v, NULL, 10);
        else if (str_eq(t, "_since")) since = v;
        else if (str_eq(t, "_at")) at = v;
    }
    (void)at;

    vec vers; vec_init(&vers);
    for (size_t i = 0; i < recs.n; i++) {
        rec *r = recs.v[i];
        for (size_t k = 0; k < r->vers.n; k++) {
            rver *v = r->vers.v[k];
            if (since && strcmp(v->last_updated, since) < 0) continue;
            vec_push(&vers, v);
            /* remember the owning record on the version for entry building */
        }
    }
    /* newest first: sort by lastUpdated desc then version desc */
    for (size_t i = 1; i < vers.n; i++) {
        rver *e = vers.v[i];
        size_t j2 = i;
        while (j2 > 0) {
            rver *p = vers.v[j2-1];
            int cmp = strcmp(p->last_updated, e->last_updated);
            if (cmp == 0) cmp = p->vid - e->vid;
            if (cmp >= 0) break;
            vers.v[j2] = vers.v[j2-1];
            j2--;
        }
        vers.v[j2] = e;
    }
    jval *bundle = bundle_new(a, "history");
    j_set(a, bundle, "id", j_str(a, "history"));
    j_set(a, bundle, "total", j_int(a, (long long)vers.n));
    jval *entries = j_arr(a);
    for (long i = offset; i < (long)vers.n && i < offset + count; i++) {
        rver *v = vers.v[i];
        /* find the owning record */
        rec *owner = NULL;
        for (size_t k = 0; k < recs.n && !owner; k++) {
            rec *r = recs.v[k];
            for (size_t z = 0; z < r->vers.n; z++) if (r->vers.v[z] == v) { owner = r; break; }
        }
        if (!owner) continue;
        jval *e = j_obj(a);
        j_set(a, e, "fullUrl", j_str(a, arena_printf(a, "%s/%s/%s/%s",
                 c->srv->base_url, c->b->base_path, owner->type, owner->id)));
        if (v->res) j_set(a, e, "resource", v->res);
        jval *req = j_obj(a);
        j_set(a, req, "method", j_str(a, v->deleted ? "DELETE" : (v->vid == 1 ? "POST" : "PUT")));
        j_set(a, req, "url", j_str(a, v->vid == 1 && !v->deleted
                 ? arena_printf(a, "%s", owner->type)
                 : arena_printf(a, "%s/%s", owner->type, owner->id)));
        j_set(a, e, "request", req);
        jval *rsp = j_obj(a);
        j_set(a, rsp, "status", j_str(a, v->deleted ? "204 No Content" : (v->vid == 1 ? "201 Created" : "200 OK")));
        j_set(a, rsp, "etag", j_str(a, arena_printf(a, "W/\"%d\"", v->vid)));
        j_set(a, rsp, "lastModified", j_str(a, v->last_updated));
        j_set(a, e, "response", rsp);
        j_push(a, entries, e);
    }
    j_set(a, bundle, "entry", entries);
    buf self; buf_init(&self);
    buf_printf(&self, "%s/%s", c->srv->base_url, c->b->base_path);
    if (type) buf_printf(&self, "/%s", type);
    if (id) buf_printf(&self, "/%s", id);
    buf_puts(&self, "/_history");
    if (qs && *qs) buf_printf(&self, "?%s", qs);
    bundle_link(a, bundle, "self", self.p);
    buf_free(&self);
    vec_free(&vers); vec_free(&recs);
    send_resource(c, 200, bundle);
}

static int check_res_type(rctx *c, jval *res, const char *type) {
    const char *rt = j_gets(res, "resourceType");
    if (!rt) { send_error(c, 400, "structure", "resourceType is missing"); return 0; }
    if (!defs_resource(c->b->defs, rt)) { send_error(c, 400, "not-supported", "unknown resource type"); return 0; }
    if (type && !str_eq(rt, type)) {
        send_error(c, 400, "processing", "the resource type in the body does not match the request URL");
        return 0;
    }
    return 1;
}

static void do_create(rctx *c, const char *type, jval *res) {
    arena *a = c->a;
    if (!check_res_type(c, res, type)) return;
    const char *ine = hreq_hdr(c->req, "If-None-Exist");
    if (ine && *ine) {
        vec hits; vec_init(&hits);
        if (find_matching(c, type, ine, &hits) != 0) { vec_free(&hits); send_error(c, 400, "processing", "unknown search parameter in If-None-Exist"); return; }
        if (hits.n == 1) {
            rec *r = hits.v[0];
            vec_free(&hits);
            set_etag(c, r->cur);
            hresp_hdrf(c->res, "Location", "%s/%s/%s/%s/_history/%d",
                       c->srv->base_url, c->b->base_path, r->type, r->id, r->cur->vid);
            c->res->status = 200;
            if (!c->prefer || !strstr(c->prefer, "minimal")) send_resource(c, 200, r->cur->res);
            return;
        }
        if (hits.n > 1) { vec_free(&hits); send_error(c, 412, "processing", "the If-None-Exist criteria matched more than one resource"); return; }
        vec_free(&hits);
    }
    char *id = store_new_id(c->b->st);
    jval *copy = j_clone(a, res);
    j_set(a, copy, "id", j_str(a, id));
    rver *v = store_put(c->b->st, type, id, copy);
    respond_written(c, 201, type, id, v, 1);
    free(id);
}

static void do_update(rctx *c, const char *type, const char *id, jval *res) {
    arena *a = c->a;
    if (!check_res_type(c, res, type)) return;
    const char *bid = j_gets(res, "id");
    if (!bid) { send_error(c, 400, "processing", "the resource in the body has no id"); return; }
    if (!str_eq(bid, id)) { send_error(c, 400, "processing", "the id in the body does not match the id in the URL"); return; }
    rec *r = store_get(c->b->st, type, id);
    const char *im = hreq_hdr(c->req, "If-Match");
    if (im && *im) {
        int want = 0;
        const char *q1 = strchr(im, '"');
        if (q1) want = atoi(q1 + 1);
        if (!r || !r->cur || r->cur->deleted || r->cur->vid != want) {
            send_error(c, 409, "conflict", "the version in If-Match does not match the current version");
            return;
        }
    }
    int existed = r && r->cur && !r->cur->deleted;
    jval *copy = j_clone(a, res);
    rver *v = store_put(c->b->st, type, id, copy);
    respond_written(c, existed ? 200 : 201, type, id, v, !existed);
}

static void do_conditional_update(rctx *c, const char *type, const char *qs, jval *res) {
    vec hits; vec_init(&hits);
    if (find_matching(c, type, qs, &hits) != 0) { vec_free(&hits); send_error(c, 400, "processing", "unknown search parameter"); return; }
    const char *bid = j_gets(res, "id");
    if (hits.n > 1) { vec_free(&hits); send_error(c, 412, "processing", "the search criteria matched more than one resource"); return; }
    if (hits.n == 1) {
        rec *r = hits.v[0];
        vec_free(&hits);
        if (bid && !str_eq(bid, r->id)) { send_error(c, 400, "processing", "the id in the body does not match the resource found by the search criteria"); return; }
        jval *copy = j_clone(c->a, res);
        j_set(c->a, copy, "id", j_str(c->a, r->id));
        if (!check_res_type(c, copy, type)) return;
            rver *v = store_put(c->b->st, type, r->id, copy);
        respond_written(c, 200, type, r->id, v, 0);
        return;
    }
    vec_free(&hits);
    if (!check_res_type(c, res, type)) return;
    char *id = bid ? str_dup(bid) : store_new_id(c->b->st);
    jval *copy = j_clone(c->a, res);
    j_set(c->a, copy, "id", j_str(c->a, id));
    rver *v = store_put(c->b->st, type, id, copy);
    respond_written(c, 201, type, id, v, 1);
    free(id);
}

static void do_delete(rctx *c, const char *type, const char *id) {
    rec *r = store_get(c->b->st, type, id);
    if (!r || !r->cur) { c->res->status = 204; return; }
    if (r->cur->deleted) { c->res->status = 204; return; }
    rver *v = store_delete(c->b->st, type, id);
    hresp_hdrf(c->res, "ETag", "W/\"%d\"", v->vid);
    c->res->status = 204;
}

static void do_conditional_delete(rctx *c, const char *type, const char *qs) {
    vec hits; vec_init(&hits);
    if (find_matching(c, type, qs, &hits) != 0) { vec_free(&hits); send_error(c, 400, "processing", "unknown search parameter"); return; }
    if (hits.n == 0) { vec_free(&hits); c->res->status = 204; return; }
    for (size_t i = 0; i < hits.n; i++) {
        rec *r = hits.v[i];
        store_delete(c->b->st, r->type, r->id);
    }
    vec_free(&hits);
    c->res->status = 204;
}

static void do_read(rctx *c, const char *type, const char *id, const char *vid) {
    rec *r = store_get(c->b->st, type, id);
    if (!r || r->vers.n == 0) { send_error(c, 404, "not-found", "resource not found"); return; }
    rver *v = r->cur;
    if (vid) {
        v = store_version(r, atoi(vid));
        if (!v) { send_error(c, 404, "not-found", "version not found"); return; }
    }
    if (v->deleted) {
        c->res->status = 410;
        hresp_hdrf(c->res, "ETag", "W/\"%d\"", v->vid);
        hresp_hdr(c->res, "Content-Type", ct_for(c->xml, c->b->defs));
        jval *oo = outcome(c->a, "error", "deleted", "resource was deleted", NULL);
        if (c->xml) fhir_to_xml(c->a, c->b->defs, oo, &c->res->body); else j_write(&c->res->body, oo, 1);
        return;
    }
    const char *inm = hreq_hdr(c->req, "If-None-Match");
    if (inm) {
        char want[64];
        snprintf(want, sizeof want, "W/\"%d\"", v->vid);
        if (strstr(inm, want)) { set_etag(c, v); c->res->status = 304; return; }
    }
    const char *ims = hreq_hdr(c->req, "If-Modified-Since");
    (void)ims;
    set_etag(c, v);
    send_resource(c, 200, v->res);
}

/* ---------------- transaction / batch ---------------- */

typedef struct { const char *from; const char *to; } idmap;

static void rewrite_refs(arena *a, jval *v, idmap *map, size_t n) {
    if (!v) return;
    if (j_is(v, J_ARR)) { for (int i = 0; i < j_len(v); i++) rewrite_refs(a, j_at(v, i), map, n); return; }
    if (!j_is(v, J_OBJ)) return;
    for (jmem *m = v->u.obj.first; m; m = m->next) {
        if ((str_eq(m->key, "reference") || str_eq(m->key, "url")) && j_is(m->val, J_STR)) {
            const char *s = j_str_of(m->val);
            for (size_t i = 0; i < n; i++)
                if (str_eq(s, map[i].from)) { m->val = j_str(a, map[i].to); break; }
        } else rewrite_refs(a, m->val, map, n);
    }
}

static void entry_response(arena *a, jval *e, int status, const char *loc, rver *v) {
    jval *rsp = j_obj(a);
    j_set(a, rsp, "status", j_str(a, arena_printf(a, "%d %s", status, http_reason(status))));
    if (loc) j_set(a, rsp, "location", j_str(a, loc));
    if (v) {
        j_set(a, rsp, "etag", j_str(a, arena_printf(a, "W/\"%d\"", v->vid)));
        j_set(a, rsp, "lastModified", j_str(a, v->last_updated));
    }
    j_set(a, e, "response", rsp);
}

static int unresolved_urn(jval *v) {
    if (!v) return 0;
    if (j_is(v, J_ARR)) {
        for (int i = 0; i < j_len(v); i++) if (unresolved_urn(j_at(v, i))) return 1;
        return 0;
    }
    if (!j_is(v, J_OBJ)) return 0;
    for (jmem *m = v->u.obj.first; m; m = m->next) {
        if (str_eq(m->key, "reference") && j_is(m->val, J_STR) &&
            str_starts(j_str_of(m->val), "urn:uuid:")) return 1;
        if (unresolved_urn(m->val)) return 1;
    }
    return 0;
}

static void do_transaction(rctx *c, jval *bundle) {
    arena *a = c->a;
    int failed_urn = 0;
    const char *btype = j_gets(bundle, "type");
    int is_tx = str_eq(btype, "transaction");
    if (!is_tx && !str_eq(btype, "batch")) {
        send_error(c, 400, "processing", "Bundle.type must be 'transaction' or 'batch' for this interaction");
        return;
    }
    jval *entries = j_get(bundle, "entry");
    int n = j_len(entries);

    /* first pass: allocate ids for POST entries and build the urn:uuid map */
    idmap *map = arena_calloc(a, ((size_t)n + 1) * sizeof(idmap));
    size_t nmap = 0;
    char **newids = arena_calloc(a, ((size_t)n + 1) * sizeof(char *));
    if (is_tx) {
        for (int i = 0; i < n; i++) {
            jval *e = j_at(entries, i);
            jval *req = j_get(e, "request");
            const char *method = j_gets(req, "method");
            const char *full = j_gets(e, "fullUrl");
            if (!str_eq(method, "POST")) continue;
            jval *res = j_get(e, "resource");
            const char *rt = res ? j_gets(res, "resourceType") : NULL;
            if (!rt) continue;
            char *id = store_new_id(c->b->st);
            newids[i] = arena_strdup(a, id);
            free(id);
            if (full && str_starts(full, "urn:uuid:")) {
                map[nmap].from = full;
                map[nmap].to = arena_printf(a, "%s/%s", rt, newids[i]);
                nmap++;
            }
        }
        for (int i = 0; i < n; i++) rewrite_refs(a, j_get(j_at(entries, i), "resource"), map, nmap);
        /* http.html: a urn:uuid: reference inside a transaction must resolve to
           an entry of the same bundle. */
        for (int i = 0; i < n && !failed_urn; i++) {
            if (unresolved_urn(j_get(j_at(entries, i), "resource"))) failed_urn = 1;
        }
    }
    if (failed_urn) {
        send_resource(c, 400, outcome(a, "error", "processing",
            "a urn:uuid: reference in the transaction does not resolve to an entry of the bundle", NULL));
        return;
    }

    jval *out = bundle_new(a, is_tx ? "transaction-response" : "batch-response");
    j_set(a, out, "id", j_str(a, "txresp"));
    jval *oents = j_arr(a);

    /* order: DELETE, POST, PUT, GET (transactions only) */
    int order[4] = {0, 1, 2, 3};
    (void)order;
    int *seq = arena_calloc(a, ((size_t)n + 1) * sizeof(int));
    int sn = 0;
    if (is_tx) {
        const char *phases[] = {"DELETE", "POST", "PUT", "PATCH", "GET", "HEAD", NULL};
        for (int p = 0; phases[p]; p++)
            for (int i = 0; i < n; i++) {
                const char *m = j_gets(j_get(j_at(entries, i), "request"), "method");
                if (str_eq(m, phases[p])) seq[sn++] = i;
            }
        for (int i = 0; i < n; i++) {
            const char *m = j_gets(j_get(j_at(entries, i), "request"), "method");
            int found = 0;
            for (int p = 0; phases[p]; p++) if (str_eq(m, phases[p])) found = 1;
            if (!found) seq[sn++] = i;
        }
    } else {
        for (int i = 0; i < n; i++) seq[sn++] = i;
    }

    jval **results = arena_calloc(a, ((size_t)n + 1) * sizeof(jval *));
    int failed = 0, fail_status = 0;
    const char *fail_msg = NULL;

    for (int k = 0; k < sn && !failed; k++) {
        int i = seq[k];
        jval *e = j_at(entries, i);
        jval *req = j_get(e, "request");
        const char *method = j_gets(req, "method");
        const char *url = j_gets(req, "url");
        jval *res = j_get(e, "resource");
        jval *oe = j_obj(a);
        if (!method || !url) {
            if (is_tx) { failed = 1; fail_status = 400; fail_msg = "Bundle.entry.request is missing method or url"; break; }
            entry_response(a, oe, 400, NULL, NULL);
            j_set(a, oe, "outcome", outcome(a, "error", "processing", "Bundle.entry.request is missing method or url", NULL));
            results[i] = oe;
            continue;
        }
        char *u = arena_strdup(a, url);
        char *qmark = strchr(u, '?');
        char *q = NULL;
        if (qmark) { *qmark = 0; q = qmark + 1; }
        psplit ps = split_path(a, u);
        const char *type = ps.n > 0 ? ps.seg[0] : NULL;
        const char *id = ps.n > 1 ? ps.seg[1] : NULL;

        if (str_eq(method, "POST")) {
            if (!res || !defs_resource(c->b->defs, type)) {
                if (is_tx) { failed = 1; fail_status = 400; fail_msg = "unknown resource type in a transaction entry"; break; }
                entry_response(a, oe, 404, NULL, NULL);
                results[i] = oe; continue;
            }
            const char *ine = j_gets(req, "ifNoneExist");
            if (ine) {
                vec hits; vec_init(&hits);
                find_matching(c, type, ine, &hits);
                if (hits.n == 1) {
                    rec *r = hits.v[0];
                    entry_response(a, oe, 200, arena_printf(a, "%s/%s/%s/%s/_history/%d",
                        c->srv->base_url, c->b->base_path, r->type, r->id, r->cur->vid), r->cur);
                    vec_free(&hits);
                    results[i] = oe;
                    continue;
                }
                if (hits.n > 1) {
                    vec_free(&hits);
                    if (is_tx) { failed = 1; fail_status = 412; fail_msg = "ifNoneExist matched more than one resource"; break; }
                    entry_response(a, oe, 412, NULL, NULL);
                    results[i] = oe; continue;
                }
                vec_free(&hits);
            }
            const char *nid = newids[i];
            char *tmp = NULL;
            if (!nid) { tmp = store_new_id(c->b->st); nid = arena_strdup(a, tmp); free(tmp); }
            jval *copy = j_clone(a, res);
            j_set(a, copy, "id", j_str(a, nid));
            rver *v = store_put(c->b->st, type, nid, copy);
            entry_response(a, oe, 201, arena_printf(a, "%s/%s/%s/%s/_history/%d",
                c->srv->base_url, c->b->base_path, type, nid, v->vid), v);
            j_set(a, oe, "fullUrl", j_str(a, arena_printf(a, "%s/%s/%s/%s", c->srv->base_url, c->b->base_path, type, nid)));
            if (!c->prefer || !strstr(c->prefer, "minimal")) j_set(a, oe, "resource", v->res);
            results[i] = oe;
        } else if (str_eq(method, "PUT")) {
            if (!res || !defs_resource(c->b->defs, type)) {
                if (is_tx) { failed = 1; fail_status = 400; fail_msg = "unknown resource type in a transaction entry"; break; }
                entry_response(a, oe, 404, NULL, NULL); results[i] = oe; continue;
            }
            if (!id && q) {
                vec hits; vec_init(&hits);
                find_matching(c, type, q, &hits);
                if (hits.n > 1) {
                    vec_free(&hits);
                    if (is_tx) { failed = 1; fail_status = 412; fail_msg = "conditional update matched more than one resource"; break; }
                    entry_response(a, oe, 412, NULL, NULL); results[i] = oe; continue;
                }
                if (hits.n == 1) id = ((rec *)hits.v[0])->id;
                vec_free(&hits);
            }
            if (!id) {
                const char *bid = j_gets(res, "id");
                char *tmp = NULL;
                if (bid) id = bid;
                else { tmp = store_new_id(c->b->st); id = arena_strdup(a, tmp); free(tmp); }
            } else {
                const char *bid = j_gets(res, "id");
                if (bid && !str_eq(bid, id)) {
                    if (is_tx) { failed = 1; fail_status = 400; fail_msg = "the id in the body does not match the id in the URL"; break; }
                    entry_response(a, oe, 400, NULL, NULL);
                    j_set(a, oe, "outcome", outcome(a, "error", "processing",
                          "the id in the body does not match the id in the URL", NULL));
                    results[i] = oe; continue;
                }
            }
            rec *ex = store_get(c->b->st, type, id);
            int existed = ex && ex->cur && !ex->cur->deleted;
            jval *copy = j_clone(a, res);
            j_set(a, copy, "id", j_str(a, id));
            rver *v = store_put(c->b->st, type, id, copy);
            entry_response(a, oe, existed ? 200 : 201, arena_printf(a, "%s/%s/%s/%s/_history/%d",
                c->srv->base_url, c->b->base_path, type, id, v->vid), v);
            j_set(a, oe, "fullUrl", j_str(a, arena_printf(a, "%s/%s/%s/%s", c->srv->base_url, c->b->base_path, type, id)));
            if (!c->prefer || !strstr(c->prefer, "minimal")) j_set(a, oe, "resource", v->res);
            results[i] = oe;
        } else if (str_eq(method, "DELETE")) {
            if (id) store_delete(c->b->st, type, id);
            else if (q) {
                vec hits; vec_init(&hits);
                find_matching(c, type, q, &hits);
                for (size_t z = 0; z < hits.n; z++) { rec *r = hits.v[z]; store_delete(c->b->st, r->type, r->id); }
                vec_free(&hits);
            }
            entry_response(a, oe, 204, NULL, NULL);
            results[i] = oe;
        } else if (str_eq(method, "GET") || str_eq(method, "HEAD")) {
            if (!id && q && defs_resource(c->b->defs, type)) {
                /* a search inside the bundle: answer it with a nested searchset */
                hresp sub; hresp_init(&sub);
                hresp *saved = c->res;
                c->res = &sub;
                do_search(c, type, q);
                c->res = saved;
                entry_response(a, oe, sub.status, NULL, NULL);
                char err2[128];
                jval *sb = j_parse(a, sub.body.p ? sub.body.p : "", sub.body.n, err2, sizeof err2);
                if (sb) j_set(a, oe, "resource", sb);
                hresp_free(&sub);
                results[i] = oe;
                continue;
            }
            if (id && defs_resource(c->b->defs, type)) {
                rec *r = store_get(c->b->st, type, id);
                if (r && r->cur && !r->cur->deleted) {
                    entry_response(a, oe, 200, NULL, r->cur);
                    j_set(a, oe, "fullUrl", j_str(a, arena_printf(a, "%s/%s/%s/%s", c->srv->base_url, c->b->base_path, type, id)));
                    j_set(a, oe, "resource", r->cur->res);
                } else {
                    if (is_tx) { failed = 1; fail_status = 404; fail_msg = "a resource read by the transaction does not exist"; break; }
                    entry_response(a, oe, 404, NULL, NULL);
                    j_set(a, oe, "outcome", outcome(a, "error", "not-found", "resource not found", NULL));
                }
            } else {
                entry_response(a, oe, 404, NULL, NULL);
                j_set(a, oe, "outcome", outcome(a, "error", "not-found", "not found", NULL));
            }
            results[i] = oe;
        } else {
            if (is_tx) { failed = 1; fail_status = 400; fail_msg = "unsupported method in a transaction entry"; break; }
            entry_response(a, oe, 405, NULL, NULL);
            results[i] = oe;
        }
    }

    if (failed) {
        send_resource(c, fail_status, outcome(a, "error", "processing", fail_msg, NULL));
        return;
    }
    for (int i = 0; i < n; i++) if (results[i]) j_push(a, oents, results[i]);
    j_set(a, out, "entry", oents);
    send_resource(c, 200, out);
}

/* ---------------- $validate ---------------- */

static void do_validate_op(rctx *c, const char *type, jval *body, const char *qs) {
    arena *a = c->a;
    jval *res = body;
    const char *profile = NULL;
    if (body && str_eq(j_gets(body, "resourceType"), "Parameters")) {
        jval *ps = j_get(body, "parameter");
        for (int i = 0; i < j_len(ps); i++) {
            jval *p = j_at(ps, i);
            const char *nm = j_gets(p, "name");
            if (str_eq(nm, "resource")) res = j_get(p, "resource");
            else if (str_eq(nm, "profile")) {
                profile = j_gets(p, "valueUri");
                if (!profile) profile = j_gets(p, "valueCanonical");
            }
        }
    }
    if (qs) {
        char *q = arena_strdup(a, qs);
        for (char *t = strtok(q, "&"); t; t = strtok(NULL, "&")) {
            char *eq = strchr(t, '=');
            if (!eq) continue;
            *eq = 0;
            char *v = arena_strdup(a, eq + 1);
            url_decode(v);
            if (str_eq(t, "profile")) profile = v;
        }
    }
    if (!res) { send_error(c, 400, "structure", "no resource to validate"); return; }
    if (type && !str_eq(j_gets(res, "resourceType"), type)) {
        send_resource(c, 200, outcome(a, "error", "invalid",
            "the resource type does not match the type in the request URL", NULL));
        return;
    }
    jval *oo = validation_outcome(a, c->b->defs, res, profile);
    int errs = 0;
    jval *issues = j_get(oo, "issue");
    for (int i = 0; i < j_len(issues); i++) {
        const char *sev = j_gets(j_at(issues, i), "severity");
        if (str_eq(sev, "error") || str_eq(sev, "fatal")) errs = 1;
    }
    send_resource(c, errs ? 422 : 200, oo);
}

/* ---------------- dispatcher ---------------- */

void fhir_handle(void *ctx, hreq *req, hresp *res) {
    server *s = ctx;
    arena a; arena_init(&a);
    rctx c; memset(&c, 0, sizeof c);
    c.srv = s; c.a = &a; c.req = req; c.res = res;

    psplit ps = split_path(&a, req->path);
    if (ps.n == 0) {
        res->status = 404;
        hresp_hdr(res, "Content-Type", "application/fhir+json");
        buf_puts(&res->body, "{\"resourceType\":\"OperationOutcome\",\"issue\":[{\"severity\":\"error\",\"code\":\"not-found\",\"details\":{\"text\":\"no service base in the request path\"}}]}");
        arena_free(&a);
        return;
    }
    fhirbase *b = server_base(s, ps.seg[0]);
    if (!b) {
        res->status = 404;
        hresp_hdr(res, "Content-Type", "application/fhir+json");
        buf_puts(&res->body, "{\"resourceType\":\"OperationOutcome\",\"issue\":[{\"severity\":\"error\",\"code\":\"not-found\",\"details\":{\"text\":\"unknown service base\"}}]}");
        arena_free(&a);
        return;
    }
    c.b = b;

    /* query-string _format has to be known before anything is written */
    const char *fmt = NULL;
    {
        char *q = arena_strdup(&a, req->query);
        for (char *t = strtok(q, "&"); t; t = strtok(NULL, "&")) {
            char *eq = strchr(t, '=');
            if (!eq) continue;
            *eq = 0;
            if (str_eq(t, "_format")) { char *v = arena_strdup(&a, eq + 1); url_decode(v); fmt = v; }
        }
    }
    c.xml = wants_xml(hreq_hdr(req, "Accept"), fmt);
    c.prefer = hreq_hdr(req, "Prefer");

    const char *method = req->method;
    const char *qs = req->query;
    store_lock(b->st);

    if (ps.n == 2 && str_eq(ps.seg[1], "metadata") && str_eq(method, "GET")) {
        send_resource(&c, 200, capability_statement(&a, s, b));
        goto out;
    }
    if (ps.n == 1) {
        if (str_eq(method, "POST")) {
            int bad = 0;
            jval *body = parse_body(&c, &bad);
            if (bad) { send_error(&c, 400, "structure", "the request body could not be parsed"); goto out; }
            do_transaction(&c, body);
            goto out;
        }
        if (str_eq(method, "GET")) { do_search(&c, NULL, qs); goto out; }
        send_error(&c, 405, "not-supported", "method not allowed on the service base");
        goto out;
    }
    const char *type = ps.seg[1];
    if (str_eq(type, "_history")) { do_history(&c, NULL, NULL, qs); goto out; }
    if (!defs_resource(b->defs, type)) {
        send_error(&c, 404, "not-found", "unknown resource type");
        goto out;
    }
    if (ps.n == 2) {
        if (str_eq(method, "GET")) { do_search(&c, type, qs); goto out; }
        if (str_eq(method, "POST")) {
            int bad = 0;
            jval *body = parse_body(&c, &bad);
            if (bad) { send_error(&c, 400, "structure", "the request body could not be parsed"); goto out; }
            do_create(&c, type, body);
            goto out;
        }
        if (str_eq(method, "PUT")) {
            if (!qs || !*qs) { send_error(&c, 400, "processing", "a conditional update needs search criteria"); goto out; }
            int bad = 0;
            jval *body = parse_body(&c, &bad);
            if (bad) { send_error(&c, 400, "structure", "the request body could not be parsed"); goto out; }
            do_conditional_update(&c, type, qs, body);
            goto out;
        }
        if (str_eq(method, "DELETE")) {
            if (!qs || !*qs) { send_error(&c, 400, "processing", "a conditional delete needs search criteria"); goto out; }
            do_conditional_delete(&c, type, qs);
            goto out;
        }
        send_error(&c, 405, "not-supported", "method not allowed");
        goto out;
    }
    if (ps.n == 3 && str_eq(ps.seg[2], "_search") && str_eq(method, "POST")) {
        const char *ct = hreq_hdr(req, "Content-Type");
        buf q; buf_init(&q);
        if (qs && *qs) buf_puts(&q, qs);
        if (ct && strstr(ct, "application/x-www-form-urlencoded") && req->blen) {
            if (q.n) buf_putc(&q, '&');
            buf_put(&q, req->body, req->blen);
        }
        do_search(&c, type, q.p ? q.p : "");
        buf_free(&q);
        goto out;
    }
    if (ps.n == 3 && str_eq(ps.seg[2], "_history")) { do_history(&c, type, NULL, qs); goto out; }
    if (ps.n == 3 && str_eq(ps.seg[2], "$validate") && str_eq(method, "POST")) {
        int bad = 0;
        jval *body = parse_body(&c, &bad);
        if (bad) { send_error(&c, 400, "structure", "the request body could not be parsed"); goto out; }
        do_validate_op(&c, type, body, qs);
        goto out;
    }
    const char *id = ps.seg[2];
    if (ps.n == 3) {
        if (str_eq(method, "GET")) { do_read(&c, type, id, NULL); goto out; }
        if (str_eq(method, "PUT")) {
            int bad = 0;
            jval *body = parse_body(&c, &bad);
            if (bad) { send_error(&c, 400, "structure", "the request body could not be parsed"); goto out; }
            do_update(&c, type, id, body);
            goto out;
        }
        if (str_eq(method, "DELETE")) { do_delete(&c, type, id); goto out; }
        send_error(&c, 405, "not-supported", "method not allowed");
        goto out;
    }
    if (ps.n == 4 && str_eq(ps.seg[3], "_history")) { do_history(&c, type, id, qs); goto out; }
    if (ps.n == 4 && str_eq(ps.seg[3], "$validate") && str_eq(method, "POST")) {
        int bad = 0;
        jval *body = parse_body(&c, &bad);
        if (bad) { send_error(&c, 400, "structure", "the request body could not be parsed"); goto out; }
        do_validate_op(&c, type, body, qs);
        goto out;
    }
    if (ps.n == 5 && str_eq(ps.seg[3], "_history") && str_eq(method, "GET")) {
        do_read(&c, type, id, ps.seg[4]);
        goto out;
    }
    send_error(&c, 404, "not-found", "unknown path");
out:
    store_unlock(b->st);
    arena_free(&a);
}
