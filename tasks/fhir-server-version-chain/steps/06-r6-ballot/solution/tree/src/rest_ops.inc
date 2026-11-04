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
    char *sp1 = NULL;
    for (char *t = strtok_r(q, "&", &sp1); t; t = strtok_r(NULL, "&", &sp1)) {
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
        /* `bdl-4` allows `entry.response` only on `batch-response` and
           `transaction-response` in DSTU2 and STU3; R4 added `history` to that
           list and made it required there.  A history bundle that carries a
           response is invalid at the earlier two. */
        if (!str_eq(c->b->defs->ver, "1.0.2") && !str_eq(c->b->defs->ver, "3.0.2")) {
            jval *rsp = j_obj(a);
            j_set(a, rsp, "status", j_str(a, v->deleted ? "204 No Content" : (v->vid == 1 ? "201 Created" : "200 OK")));
            j_set(a, rsp, "etag", j_str(a, arena_printf(a, "W/\"%d\"", v->vid)));
            j_set(a, rsp, "lastModified", j_str(a, v->last_updated));
            j_set(a, e, "response", rsp);
        }
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
    rver *v = put_notify(c, type, id, copy);
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
    rver *v = put_notify(c, type, id, copy);
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
            rver *v = put_notify(c, type, r->id, copy);
        respond_written(c, 200, type, r->id, v, 0);
        return;
    }
    vec_free(&hits);
    if (!check_res_type(c, res, type)) return;
    char *id = bid ? str_dup(bid) : store_new_id(c->b->st);
    jval *copy = j_clone(c->a, res);
    j_set(c->a, copy, "id", j_str(c->a, id));
    rver *v = put_notify(c, type, id, copy);
    respond_written(c, 201, type, id, v, 1);
    free(id);
}

static void do_delete(rctx *c, const char *type, const char *id) {
    rec *r = store_get(c->b->st, type, id);
    if (!r || !r->cur) { c->res->status = 204; return; }
    if (r->cur->deleted) { c->res->status = 204; return; }
    rver *v = delete_notify(c, type, id);
    hresp_hdrf(c->res, "ETag", "W/\"%d\"", v->vid);
    c->res->status = 204;
}

static void do_conditional_delete(rctx *c, const char *type, const char *qs) {
    vec hits; vec_init(&hits);
    if (find_matching(c, type, qs, &hits) != 0) { vec_free(&hits); send_error(c, 400, "processing", "unknown search parameter"); return; }
    if (hits.n == 0) { vec_free(&hits); c->res->status = 204; return; }
    for (size_t i = 0; i < hits.n; i++) {
        rec *r = hits.v[i];
        delete_notify(c, r->type, r->id);
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
            rver *v = put_notify(c, type, nid, copy);
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
            rver *v = put_notify(c, type, id, copy);
            entry_response(a, oe, existed ? 200 : 201, arena_printf(a, "%s/%s/%s/%s/_history/%d",
                c->srv->base_url, c->b->base_path, type, id, v->vid), v);
            j_set(a, oe, "fullUrl", j_str(a, arena_printf(a, "%s/%s/%s/%s", c->srv->base_url, c->b->base_path, type, id)));
            if (!c->prefer || !strstr(c->prefer, "minimal")) j_set(a, oe, "resource", v->res);
            results[i] = oe;
        } else if (str_eq(method, "DELETE")) {
            if (id) delete_notify(c, type, id);
            else if (q) {
                vec hits; vec_init(&hits);
                find_matching(c, type, q, &hits);
                for (size_t z = 0; z < hits.n; z++) { rec *r = hits.v[z]; delete_notify(c, r->type, r->id); }
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

/* Read an operation parameter from the query string or from a `Parameters`
   body, which `operations.html` makes equivalent. */
static const char *op_param(rctx *c, const char *qs, jval *body, const char *name) {
    if (qs) {
        char *q = arena_strdup(c->a, qs);
        char *sp1 = NULL;
    for (char *t = strtok_r(q, "&", &sp1); t; t = strtok_r(NULL, "&", &sp1)) {
            char *eq = strchr(t, '=');
            if (!eq) continue;
            *eq = 0;
            char *k = arena_strdup(c->a, t), *v = arena_strdup(c->a, eq + 1);
            url_decode(k); url_decode(v);
            if (str_eq(k, name)) return v;
        }
    }
    if (body && str_eq(j_gets(body, "resourceType"), "Parameters")) {
        jval *pl = j_get(body, "parameter");
        for (int i = 0; i < j_len(pl); i++) {
            jval *p = j_at(pl, i);
            if (!str_eq(j_gets(p, "name"), name)) continue;
            for (jmem *m = p->u.obj.first; m; m = m->next)
                if (!strncmp(m->key, "value", 5) && j_is(m->val, J_STR)) return j_str_of(m->val);
        }
    }
    return NULL;
}

static jval *op_param_res(rctx *c, jval *body, const char *name) {
    if (!body || !str_eq(j_gets(body, "resourceType"), "Parameters")) return NULL;
    jval *pl = j_get(body, "parameter");
    for (int i = 0; i < j_len(pl); i++) {
        jval *p = j_at(pl, i);
        if (!str_eq(j_gets(p, "name"), name)) continue;
        for (jmem *m = p->u.obj.first; m; m = m->next)
            if (!strncmp(m->key, "value", 5) && j_is(m->val, J_OBJ)) return m->val;
        jval *r = j_get(p, "resource");
        if (r) return r;
    }
    (void)c;
    return NULL;
}

/* -> 1 when this request was an operation this function answered. */
static int do_terminology_op(rctx *c, const char *type, const char *id,
                             const char *op, const char *qs, const char *method) {
    arena *a = c->a;
    fhirdefs *d = c->b->defs;
    int known = (str_eq(type, "ValueSet") && (str_eq(op, "$expand") || str_eq(op, "$validate-code"))) ||
                (str_eq(type, "CodeSystem") && (str_eq(op, "$lookup") || str_eq(op, "$subsumes"))) ||
                (str_eq(type, "ConceptMap") && str_eq(op, "$translate"));
    if (!known) return 0;
    if (!str_eq(method, "GET") && !str_eq(method, "POST")) {
        send_error(c, 405, "not-supported", "method not allowed");
        return 1;
    }
    jval *body = NULL;
    if (str_eq(method, "POST")) {
        int bad = 0;
        body = parse_body(c, &bad);
        if (bad) { send_error(c, 400, "structure", "the request body could not be parsed"); return 1; }
    }
    /* the instance form names the resource by id; the type form by url */
    const char *url = op_param(c, qs, body, "url");
    if (!url) url = op_param(c, qs, body, "system");
    if (id) {
        rec *r = store_get(c->b->st, type, id);
        jval *held = r && r->cur && !r->cur->deleted ? r->cur->res : NULL;
        if (held && j_gets(held, "url")) url = j_gets(held, "url");
        else if (!held) { send_error(c, 404, "not-found", "no such resource"); return 1; }
    }
    int status = 200;
    const char *msg = NULL;
    jval *out = NULL;
    if (str_eq(op, "$expand")) {
        const char *f = op_param(c, qs, body, "filter");
        const char *cnt = op_param(c, qs, body, "count");
        const char *off = op_param(c, qs, body, "offset");
        out = term_expand(a, d, url, f, cnt ? atoi(cnt) : -1, off ? atoi(off) : 0, &status, &msg);
    } else if (str_eq(op, "$validate-code") && str_eq(type, "ValueSet")) {
        out = term_validate_code(a, d, url, op_param(c, qs, body, "system"),
                                 op_param(c, qs, body, "code"),
                                 op_param(c, qs, body, "display"),
                                 op_param_res(c, body, "coding"),
                                 op_param_res(c, body, "codeableConcept"),
                                 &status, &msg);
    } else if (str_eq(op, "$lookup")) {
        out = term_lookup(a, d, url, op_param(c, qs, body, "code"), &status, &msg);
    } else if (str_eq(op, "$subsumes")) {
        out = term_subsumes(a, d, url, op_param(c, qs, body, "codeA"),
                            op_param(c, qs, body, "codeB"), &status, &msg);
    } else if (str_eq(op, "$translate")) {
        out = term_translate(a, d, url, op_param(c, qs, body, "system"),
                             op_param(c, qs, body, "code"),
                             op_param(c, qs, body, "target"), &status, &msg);
    }
    if (!out) {
        send_error(c, status ? status : 400, status == 404 ? "not-found" : "invalid",
                   msg ? msg : "the operation could not be answered");
        return 1;
    }
    send_resource(c, 200, out);
    return 1;
}

/* ---- the two subscription operations ---- */

/* `name` out of a raw query string, URL-decoded into the arena. */
static char *qs_value(arena *a, const char *qs, const char *name) {
    if (!qs) return NULL;
    size_t nl = strlen(name);
    char *q = arena_strdup(a, qs);
    char *sp1 = NULL;
    for (char *t = strtok_r(q, "&", &sp1); t; t = strtok_r(NULL, "&", &sp1)) {
        if (strncmp(t, name, nl) == 0 && t[nl] == '=') {
            char *v = arena_strdup(a, t + nl + 1);
            url_decode(v);
            return v;
        }
    }
    return NULL;
}

static int do_subscription_op(rctx *c, const char *type, const char *id,
                              const char *op, const char *qs, const char *method) {
    if (!str_eq(type, "Subscription")) return 0;
    if (!str_eq(method, "GET") && !str_eq(method, "POST")) return 0;
    arena *a = c->a;
    if (str_eq(op, "$status")) {
        vec ids;
        vec_init(&ids);
        if (id) vec_push(&ids, (void *)id);
        char *want = qs_value(a, qs, "ids");
        char *sp = NULL;
        for (char *t = want ? strtok_r(want, ",", &sp) : NULL; t;
             t = strtok_r(NULL, ",", &sp))
            vec_push(&ids, t);
        jval *bn = sub_status(a, c->srv, c->b, &ids, qs_value(a, qs, "status"));
        vec_free(&ids);
        if (id && j_len(j_get(bn, "entry")) == 0) {
            send_error(c, 404, "not-found", "no such Subscription");
            return 1;
        }
        send_resource(c, 200, bn);
        return 1;
    }
    if (str_eq(op, "$events")) {
        if (!id) { send_error(c, 400, "processing", "$events is an instance operation"); return 1; }
        char *since = qs_value(a, qs, "eventsSinceNumber");
        char *until = qs_value(a, qs, "eventsUntilNumber");
        jval *bn = sub_events(a, c->srv, c->b, id,
                              since ? atoll(since) : -1, until ? atoll(until) : -1,
                              qs_value(a, qs, "content"));
        if (!bn) { send_error(c, 404, "not-found", "no such Subscription"); return 1; }
        send_resource(c, 200, bn);
        return 1;
    }
    return 0;
}

/* ---- Bulk Data, SMART and $everything ---- */

static void send_json(rctx *c, int status, jval *v) {
    c->res->status = status;
    hresp_hdr(c->res, "Content-Type", "application/json");
    j_write(&c->res->body, v, 0);
}

/* The status endpoint and the NDJSON files live at the server root rather than
   under a service base: the manifest is not a FHIR resource and neither are the
   files, so they are not version-specific. */
static int do_bulk_root(rctx *c, psplit *ps, const char *method) {
    if (ps->n < 2 || !str_eq(ps->seg[0], "bulk")) return 0;
    const char *jobid = ps->seg[1];
    if (ps->n == 3) {
        size_t len = 0;
        const char *data = bulk_file(jobid, ps->seg[2], &len);
        if (!data) { send_error(c, 404, "not-found", "no such export file"); return 1; }
        c->res->status = 200;
        hresp_hdr(c->res, "Content-Type", "application/fhir+ndjson");
        buf_put(&c->res->body, data, len);
        return 1;
    }
    if (str_eq(method, "DELETE")) {
        int rc = bulk_cancel(jobid);
        if (rc == 404) send_error(c, 404, "not-found", "no such export job");
        else c->res->status = 202;
        return 1;
    }
    if (!str_eq(method, "GET")) { send_error(c, 405, "not-supported", "method not allowed"); return 1; }
    jval *body = NULL;
    const char *progress = NULL;
    int rc = bulk_status(c->a, c->srv, jobid, &body, &progress);
    if (rc == 404) { send_error(c, 404, "not-found", "no such export job"); return 1; }
    if (rc == 500) { send_error(c, 500, "exception", "the export failed"); return 1; }
    if (rc == 202) {
        c->res->status = 202;
        if (progress) hresp_hdr(c->res, "X-Progress", progress);
        hresp_hdr(c->res, "Retry-After", "1");
        return 1;
    }
    send_json(c, 200, body);
    return 1;
}

static int do_export(rctx *c, psplit *ps, const char *qs, const char *method) {
    const char *level = NULL, *gid = NULL;
    if (ps->n == 2 && str_eq(ps->seg[1], "$export")) level = "system";
    else if (ps->n == 3 && str_eq(ps->seg[1], "Patient") && str_eq(ps->seg[2], "$export"))
        level = "patient";
    else if (ps->n == 4 && str_eq(ps->seg[1], "Group") && str_eq(ps->seg[3], "$export")) {
        level = "group";
        gid = ps->seg[2];
    }
    if (!level) return 0;
    if (!str_eq(method, "GET") && !str_eq(method, "POST")) {
        send_error(c, 405, "not-supported", "method not allowed");
        return 1;
    }
    /* `_outputFormat` is the only parameter with a closed value set */
    char *q = arena_strdup(c->a, qs ? qs : "");
    char *sp1 = NULL;
    for (char *t = strtok_r(q, "&", &sp1); t; t = strtok_r(NULL, "&", &sp1)) {
        char *eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = arena_strdup(c->a, eq + 1);
        url_decode(v);
        if (str_eq(t, "_outputFormat")) {
            if (!str_eq(v, "application/fhir+ndjson") && !str_eq(v, "application/ndjson") &&
                !str_eq(v, "ndjson")) {
                send_error(c, 400, "value", "_outputFormat must be ndjson");
                return 1;
            }
        } else if (str_eq(t, "_type")) {
            char *sp2 = NULL;
            for (char *x = strtok_r(v, ",", &sp2); x; x = strtok_r(NULL, ",", &sp2)) {
                if (!defs_is_resource(c->b->defs, x)) {
                    send_error(c, 400, "value",
                               arena_printf(c->a, "%s is not a resource type in this "
                                                  "FHIR version", x));
                    return 1;
                }
            }
        }
    }
    const char *pref = hreq_hdr(c->req, "Prefer");
    if (!pref || !strstr(pref, "respond-async")) {
        send_error(c, 400, "required",
                   "$export is asynchronous: send Prefer: respond-async");
        return 1;
    }
    char requrl[1024];
    snprintf(requrl, sizeof requrl, "%s%s%s%s", c->srv->base_url, c->req->path,
             qs && *qs ? "?" : "", qs ? qs : "");
    const char *jobid = bulk_start(c->a, c->srv, c->b, level, gid, qs, requrl);
    c->res->status = 202;
    hresp_hdrf(c->res, "Content-Location", "%s/bulk/%s", c->srv->base_url, jobid);
    return 1;
}

/* The interaction letter a request asks for, for the SMART scope check. */
static char smart_op(const char *method, int instance_level) {
    if (str_eq(method, "GET")) return instance_level ? 'r' : 's';
    if (str_eq(method, "POST")) return 'c';
    if (str_eq(method, "PUT")) return 'u';
    if (str_eq(method, "DELETE")) return 'd';
    return 'r';
}

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
        char *sp1 = NULL;
    for (char *t = strtok_r(q, "&", &sp1); t; t = strtok_r(NULL, "&", &sp1)) {
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

/* ---------------- StructureMap/$transform ---------------- */

static jval *find_structuremap(rctx *c, const char *canonical) {
    vec all; vec_init(&all);
    store_scan(c->b->st, "StructureMap", &all);
    jval *found = NULL;
    for (size_t i = 0; i < all.n && !found; i++) {
        rec *r = all.v[i];
        if (!r->cur || r->cur->deleted) continue;
        if (str_eq(j_gets(r->cur->res, "url"), canonical)) found = r->cur->res;
        else if (str_eq(r->id, canonical)) found = r->cur->res;
    }
    vec_free(&all);
    return found;
}

static void do_transform(rctx *c, jval *body, const char *qs) {
    arena *a = c->a;
    const char *source = NULL;
    if (qs) {
        char *q = arena_strdup(a, qs);
        char *sp1 = NULL;
    for (char *t = strtok_r(q, "&", &sp1); t; t = strtok_r(NULL, "&", &sp1)) {
            char *eq = strchr(t, '=');
            if (!eq) continue;
            *eq = 0;
            char *v = arena_strdup(a, eq + 1);
            url_decode(v);
            if (str_eq(t, "source")) source = v;
        }
    }
    if (!source) { send_error(c, 400, "required", "the $transform operation needs a source parameter naming the map"); return; }
    jval *sm = find_structuremap(c, source);
    if (!sm) { send_error(c, 404, "not-found", "no StructureMap with that canonical url"); return; }
    char err[256] = {0};
    fmlmap *m = fml_from_resource(a, sm, err, sizeof err);
    if (!m) { send_error(c, 422, "processing", err[0] ? err : "the StructureMap could not be read"); return; }
    jval *content = body;
    if (body && str_eq(j_gets(body, "resourceType"), "Parameters")) {
        jval *ps = j_get(body, "parameter");
        for (int i = 0; i < j_len(ps); i++) {
            jval *p = j_at(ps, i);
            if (str_eq(j_gets(p, "name"), "content")) content = j_get(p, "resource");
        }
    }
    if (!content) { send_error(c, 400, "required", "no content to transform"); return; }
    jval *out = fml_transform(a, c->b->defs, m, content, err, sizeof err);
    if (!out) { send_error(c, 422, "processing", err[0] ? err : "the transform produced nothing"); return; }
    send_resource(c, 200, out);
}

/* ---------------- dispatcher ---------------- */

void fhir_handle(void *ctx, hreq *req, hresp *res) {
    server *s = ctx;
    arena a; arena_init(&a);
    rctx c; memset(&c, 0, sizeof c);
    c.srv = s; c.a = &a; c.req = req; c.res = res;
    vec_init(&c.events);

    psplit ps = split_path(&a, req->path);
    if (ps.n == 0) {
        res->status = 404;
        hresp_hdr(res, "Content-Type", "application/fhir+json");
        buf_puts(&res->body, "{\"resourceType\":\"OperationOutcome\",\"issue\":[{\"severity\":\"error\",\"code\":\"not-found\",\"details\":{\"text\":\"no service base in the request path\"}}]}");
        arena_free(&a);
        return;
    }
    if (str_eq(ps.seg[0], "bulk")) {
        vec_init(&c.events);
        c.b = s->bases[0];
        if (do_bulk_root(&c, &ps, req->method)) { arena_free(&a); return; }
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
    smart_verify(&a, hreq_hdr(req, "Authorization"), s->base_url, &c.tok);

    /* `versions.html`: the `fhirVersion` parameter on the media type is one of
       the three ways a FHIR version is determined.  On Accept it selects the
       release the answer is rendered in; on Content-Type it says which release
       the body is in.  A value naming no release this server holds makes the
       media type unacceptable rather than being ignored. */
    {
        int saw = 0;
        const char *want = version_param(&a, hreq_hdr(req, "Accept"), &saw);
        if (saw && !want) {
            res->status = 406;
            hresp_hdr(res, "Content-Type", "application/fhir+json");
            buf_puts(&res->body, "{\"resourceType\":\"OperationOutcome\",\"issue\":[{\"severity\":\"error\",\"code\":\"not-supported\",\"details\":{\"text\":\"the fhirVersion media type parameter is empty\"}}]}");
            arena_free(&a);
            return;
        }
        if (want) {
            const char *bp = version_param_base(want);
            fhirbase *target = bp ? server_base(s, bp) : NULL;
            if (!target) {
                res->status = 406;
                hresp_hdr(res, "Content-Type", "application/fhir+json");
                buf_puts(&res->body, "{\"resourceType\":\"OperationOutcome\",\"issue\":[{\"severity\":\"error\",\"code\":\"not-supported\",\"details\":{\"text\":\"this server does not hold that FHIR version\"}}]}");
                arena_free(&a);
                return;
            }
            c.render_as_defs = target->defs;
            c.render_as_mm = arena_strdup(&a, want);
        }
        int saw_ct = 0;
        const char *body_v = version_param(&a, hreq_hdr(req, "Content-Type"), &saw_ct);
        if (saw_ct && (!body_v || !version_param_base(body_v) ||
                       !server_base(s, version_param_base(body_v)))) {
            res->status = 415;
            hresp_hdr(res, "Content-Type", "application/fhir+json");
            buf_puts(&res->body, "{\"resourceType\":\"OperationOutcome\",\"issue\":[{\"severity\":\"error\",\"code\":\"not-supported\",\"details\":{\"text\":\"this server does not hold the FHIR version the body claims\"}}]}");
            arena_free(&a);
            return;
        }
    }

    /* query-string _format has to be known before anything is written */
    const char *fmt = NULL;
    {
        char *q = arena_strdup(&a, req->query);
        char *sp1 = NULL;
    for (char *t = strtok_r(q, "&", &sp1); t; t = strtok_r(NULL, "&", &sp1)) {
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
    /* The SMART discovery document is not a FHIR resource and does not live
       under a resource type, so it is answered before the type is resolved. */
    if (ps.n == 3 && str_eq(type, ".well-known") &&
        str_eq(ps.seg[2], "smart-configuration") && str_eq(method, "GET")) {
        send_json(&c, 200, smart_configuration(&a, c.srv->base_url));
        goto out;
    }
    if (do_export(&c, &ps, qs, method)) goto out;
    if (!defs_resource(b->defs, type)) {
        send_error(&c, 404, "not-found", "unknown resource type");
        goto out;
    }
    /* SMART: the server is open by default and enforces only what a token asks
       it to, so a request with no Authorization header reaches here unchanged. */
    if (c.tok.present) {
        if (!c.tok.ok) {
            hresp_hdr(c.res, "WWW-Authenticate", "Bearer");
            send_error(&c, 401, "security",
                       c.tok.reason ? c.tok.reason : "the bearer token did not verify");
            goto out;
        }
        const char *compartment = NULL;
        if (!smart_allows(&c.tok, type, smart_op(method, ps.n >= 3), &compartment)) {
            send_error(&c, 403, "forbidden",
                       arena_printf(&a, "the granted scopes do not permit this "
                                        "interaction on %s", type));
            goto out;
        }
        c.scope_patient = compartment;
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
    if (ps.n == 4 && str_eq(ps.seg[3], "$everything") &&
        (str_eq(type, "Patient") || str_eq(type, "Encounter"))) {
        if (!str_eq(method, "GET") && !str_eq(method, "POST")) {
            send_error(&c, 405, "not-supported", "method not allowed");
            goto out;
        }
        jval *bn = everything(&a, c.srv, b, type, ps.seg[2], qs);
        if (!bn) { send_error(&c, 404, "not-found", "no such resource"); goto out; }
        send_resource(&c, 200, bn);
        goto out;
    }
    if (ps.n == 3 && str_eq(ps.seg[2], "$everything")) {
        send_error(&c, 400, "processing",
                   "$everything is an instance operation");
        goto out;
    }
    if (ps.n == 3 && ps.seg[2][0] == '$' &&
        do_subscription_op(&c, type, NULL, ps.seg[2], qs, method)) goto out;
    if (ps.n == 4 && ps.seg[3][0] == '$' &&
        do_subscription_op(&c, type, ps.seg[2], ps.seg[3], qs, method)) goto out;

    /* ---- the terminology operations (terminology-service.html) ---- */
    if (ps.n >= 3 && ps.seg[2][0] == '$' && do_terminology_op(&c, type, ps.n == 4 ? NULL : NULL,
                                                             ps.seg[2], qs, method)) goto out;
    if (ps.n == 4 && ps.seg[3][0] == '$' && do_terminology_op(&c, type, ps.seg[2],
                                                             ps.seg[3], qs, method)) goto out;
    if (ps.n == 3 && str_eq(type, "StructureMap") && str_eq(ps.seg[2], "$transform") && str_eq(method, "POST")) {
        int bad = 0;
        jval *body = parse_body(&c, &bad);
        if (bad) { send_error(&c, 400, "structure", "the request body could not be parsed"); goto out; }
        do_transform(&c, body, qs);
        goto out;
    }
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
    /* The response is complete; now tell anyone who subscribed.  Dispatch runs
       inside the store lock so that a notification can never describe a state
       the next request has already changed, and a Subscription that was just
       created gets its handshake first so that it is `active` in time to see
       the writes of the request that follows. */
    for (size_t i = 0; i < c.events.n; i++) {
        subevent *ev = c.events.v[i];
        if (str_eq(ev->type, "Subscription") && !str_eq(ev->interaction, "delete"))
            sub_handshake(s, b, ev->id);
    }
    sub_dispatch(s, b, &c.events);
    vec_free(&c.events);
    store_unlock(b->st);
    arena_free(&a);
}
