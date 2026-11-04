/* The FHIR terminology operations.
 *
 * Everything answered here comes out of the published `CodeSystem`, `ValueSet`
 * and `ConceptMap` resources the definitions layer loaded: the expansion of a
 * value set is what its `compose` selects from the code systems, a concept's
 * properties are the ones its `CodeSystem.concept` carries, and a translation
 * is what a `ConceptMap.group.element.target` states.  Nothing is invented.
 */
#include "term.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* case-insensitive substring, for the `filter` parameter of $expand */
static int str_icontains(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nl) return 1;
    }
    return 0;
}

static jval *param(arena *a, const char *name) {
    jval *p = j_obj(a);
    j_set(a, p, "name", j_str(a, name));
    return p;
}

static jval *params(arena *a) {
    jval *o = j_obj(a);
    j_set(a, o, "resourceType", j_str(a, "Parameters"));
    j_set(a, o, "parameter", j_arr(a));
    return o;
}

static void put_param(arena *a, jval *ps, const char *name, const char *key, jval *v) {
    jval *p = param(a, name);
    j_set(a, p, key, v);
    j_push(a, j_get(ps, "parameter"), p);
}

static jval *find_vs(fhirdefs *d, const char *url) {
    if (!url) return NULL;
    defs_load_igs(d);
    jval *v = smap_get(&d->valuesets, url);
    if (v) return v;
    const char *bar = strchr(url, '|');
    if (bar) {
        char buf[512];
        snprintf(buf, sizeof buf, "%.*s", (int)(bar - url), url);
        return smap_get(&d->valuesets, buf);
    }
    return NULL;
}

static jval *find_cs(fhirdefs *d, const char *url) {
    if (!url) return NULL;
    defs_load_igs(d);
    jval *v = smap_get(&d->codesystems, url);
    if (v) return v;
    const char *bar = strchr(url, '|');
    if (bar) {
        char buf[512];
        snprintf(buf, sizeof buf, "%.*s", (int)(bar - url), url);
        return smap_get(&d->codesystems, buf);
    }
    return NULL;
}

/* ---- walking a code system's concept tree ---- */

static jval *cs_find(jval *concepts, const char *code) {
    for (int i = 0; i < j_len(concepts); i++) {
        jval *c = j_at(concepts, i);
        if (str_eq(j_gets(c, "code"), code)) return c;
        jval *kid = cs_find(j_get(c, "concept"), code);
        if (kid) return kid;
    }
    return NULL;
}

static int cs_is_a(jval *concepts, const char *ancestor, const char *code, int inside) {
    for (int i = 0; i < j_len(concepts); i++) {
        jval *c = j_at(concepts, i);
        int here = inside || str_eq(j_gets(c, "code"), ancestor);
        if (here && str_eq(j_gets(c, "code"), code)) return 1;
        if (cs_is_a(j_get(c, "concept"), ancestor, code, here)) return 1;
    }
    return 0;
}

/* `codesystem.html`: a concept marked `notSelectable` (or `abstract`) groups
   other concepts and is not itself a code anyone may use, so it does not appear
   in an expansion. */
static int not_selectable(jval *c) {
    jval *props = j_get(c, "property");
    for (int i = 0; i < j_len(props); i++) {
        jval *p = j_at(props, i);
        const char *code = j_gets(p, "code");
        if (!code) continue;
        if (!str_eq(code, "notSelectable") && !str_eq(code, "abstract")) continue;
        jval *v = j_get(p, "valueBoolean");
        if (v && j_is(v, J_BOOL) && v->u.b) return 1;
    }
    return 0;
}

static void cs_collect(arena *a, jval *concepts, const char *system, vec *out) {
    for (int i = 0; i < j_len(concepts); i++) {
        jval *c = j_at(concepts, i);
        /* A whole-code-system include takes the concepts as they are; it is the
           filtered includes whose expansions the publisher reports without the
           grouper concepts. */
        jval *e = j_obj(a);
        j_set(a, e, "system", j_str(a, system));
        if (j_gets(c, "code")) j_set(a, e, "code", j_str(a, j_gets(c, "code")));
        if (j_gets(c, "display")) j_set(a, e, "display", j_str(a, j_gets(c, "display")));
        vec_push(out, e);
        cs_collect(a, j_get(c, "concept"), system, out);
    }
}

static void cs_collect_under(arena *a, jval *concepts, const char *system,
                            const char *ancestor, int inside, int self, vec *out) {
    for (int i = 0; i < j_len(concepts); i++) {
        jval *c = j_at(concepts, i);
        int is_root = str_eq(j_gets(c, "code"), ancestor);
        int here = inside || is_root;
        if (here && (self || !is_root) && !not_selectable(c)) {
            jval *e = j_obj(a);
            j_set(a, e, "system", j_str(a, system));
            if (j_gets(c, "code")) j_set(a, e, "code", j_str(a, j_gets(c, "code")));
            if (j_gets(c, "display")) j_set(a, e, "display", j_str(a, j_gets(c, "display")));
            vec_push(out, e);
        }
        cs_collect_under(a, j_get(c, "concept"), system, ancestor, here, self, out);
    }
}

/* ---- expansion ---- */

static int contains_pair(vec *v, const char *system, const char *code) {
    for (size_t i = 0; i < v->n; i++) {
        jval *e = v->v[i];
        if (str_eq(j_gets(e, "system"), system) && str_eq(j_gets(e, "code"), code)) return 1;
    }
    return 0;
}

static void expand_compose(arena *a, fhirdefs *d, jval *vs, vec *out, int depth);

static void expand_include(arena *a, fhirdefs *d, jval *inc, vec *out, int depth) {
    const char *system = j_gets(inc, "system");
    jval *concept = j_get(inc, "concept");
    jval *filter = j_get(inc, "filter");
    jval *vsrefs = j_get(inc, "valueSet");
    for (int i = 0; i < j_len(vsrefs); i++) {
        jval *sub = find_vs(d, j_str_of(j_at(vsrefs, i)));
        if (sub && depth < 8) expand_compose(a, d, sub, out, depth + 1);
    }
    if (!system) return;
    jval *cs = find_cs(d, system);
    if (j_len(concept)) {
        for (int i = 0; i < j_len(concept); i++) {
            jval *c = j_at(concept, i);
            jval *e = j_obj(a);
            j_set(a, e, "system", j_str(a, system));
            j_set(a, e, "code", j_str(a, j_gets(c, "code")));
            const char *disp = j_gets(c, "display");
            if (!disp && cs) {
                jval *k = cs_find(j_get(cs, "concept"), j_gets(c, "code"));
                disp = k ? j_gets(k, "display") : NULL;
            }
            if (disp) j_set(a, e, "display", j_str(a, disp));
            if (!contains_pair(out, system, j_gets(c, "code"))) vec_push(out, e);
        }
        return;
    }
    if (j_len(filter)) {
        if (!cs) return;
        for (int i = 0; i < j_len(filter); i++) {
            jval *f = j_at(filter, i);
            const char *op = j_gets(f, "op");
            const char *val = j_gets(f, "value");
            vec tmp; vec_init(&tmp);
            if (str_eq(op, "is-a"))
                cs_collect_under(a, j_get(cs, "concept"), system, val, 0, 1, &tmp);
            else if (str_eq(op, "descendent-of"))
                cs_collect_under(a, j_get(cs, "concept"), system, val, 0, 0, &tmp);
            else if (str_eq(op, "="))
                cs_collect_under(a, j_get(cs, "concept"), system, val, 0, 1, &tmp);
            else if (str_eq(op, "is-not-a")) {
                /* everything the code system has, less the subtree under `value` */
                vec all; vec_init(&all);
                cs_collect(a, j_get(cs, "concept"), system, &all);
                vec under; vec_init(&under);
                cs_collect_under(a, j_get(cs, "concept"), system, val, 0, 1, &under);
                for (size_t z = 0; z < all.n; z++) {
                    jval *e = all.v[z];
                    int excluded = 0;
                    for (size_t y = 0; y < under.n && !excluded; y++)
                        excluded = str_eq(j_gets((jval *)under.v[y], "code"), j_gets(e, "code"));
                    if (!excluded) vec_push(&tmp, e);
                }
                vec_free(&all); vec_free(&under);
            } else if (str_eq(op, "in") || str_eq(op, "not-in")) {
                vec all; vec_init(&all);
                cs_collect(a, j_get(cs, "concept"), system, &all);
                int want_in = str_eq(op, "in");
                for (size_t z = 0; z < all.n; z++) {
                    jval *e = all.v[z];
                    const char *cd = j_gets(e, "code");
                    int listed = 0;
                    const char *q = val;
                    while (q && *q && !listed) {
                        const char *comma = strchr(q, ',');
                        size_t len = comma ? (size_t)(comma - q) : strlen(q);
                        if (cd && strlen(cd) == len && !strncmp(cd, q, len)) listed = 1;
                        q = comma ? comma + 1 : NULL;
                    }
                    if (listed == want_in) vec_push(&tmp, e);
                }
                vec_free(&all);
            }
            for (size_t z = 0; z < tmp.n; z++) {
                jval *e = tmp.v[z];
                if (!contains_pair(out, system, j_gets(e, "code"))) vec_push(out, e);
            }
            vec_free(&tmp);
        }
        return;
    }
    if (cs) {
        vec tmp; vec_init(&tmp);
        cs_collect(a, j_get(cs, "concept"), system, &tmp);
        for (size_t z = 0; z < tmp.n; z++) {
            jval *e = tmp.v[z];
            if (!contains_pair(out, system, j_gets(e, "code"))) vec_push(out, e);
        }
        vec_free(&tmp);
    }
}

static void expand_compose(arena *a, fhirdefs *d, jval *vs, vec *out, int depth) {
    /* A publisher-supplied expansion is the answer where there is one. */
    jval *exp = j_get(vs, "expansion");
    jval *contains = j_get(exp, "contains");
    if (j_len(contains)) {
        for (int i = 0; i < j_len(contains); i++) {
            jval *e = j_at(contains, i);
            if (!contains_pair(out, j_gets(e, "system"), j_gets(e, "code"))) vec_push(out, e);
        }
        return;
    }
    jval *comp = j_get(vs, "compose");
    jval *inc = j_get(comp, "include");
    for (int i = 0; i < j_len(inc); i++) expand_include(a, d, j_at(inc, i), out, depth);
    jval *exc = j_get(comp, "exclude");
    for (int i = 0; i < j_len(exc); i++) {
        vec drop; vec_init(&drop);
        expand_include(a, d, j_at(exc, i), &drop, depth);
        for (size_t z = 0; z < drop.n; z++) {
            jval *e = drop.v[z];
            for (size_t y = 0; y < out->n; y++) {
                jval *k = out->v[y];
                if (str_eq(j_gets(k, "system"), j_gets(e, "system")) &&
                    str_eq(j_gets(k, "code"), j_gets(e, "code"))) {
                    memmove(&out->v[y], &out->v[y + 1], (out->n - y - 1) * sizeof(void *));
                    out->n--;
                    break;
                }
            }
        }
        vec_free(&drop);
    }
    /* DSTU2 keeps the code system inside the value set */
    jval *own = j_get(vs, "codeSystem");
    if (own && out->n == 0) {
        const char *system = j_gets(own, "system");
        vec tmp; vec_init(&tmp);
        cs_collect(a, j_get(own, "concept"), system, &tmp);
        for (size_t z = 0; z < tmp.n; z++) vec_push(out, tmp.v[z]);
        vec_free(&tmp);
    }
}

jval *term_expand(arena *a, fhirdefs *d, const char *url, const char *filter,
                  int count, int offset, int *status, const char **msg) {
    jval *vs = find_vs(d, url);
    if (!vs) { *status = 404; *msg = "this server does not hold that value set"; return NULL; }
    vec all; vec_init(&all);
    expand_compose(a, d, vs, &all, 0);
    jval *out = j_obj(a);
    j_set(a, out, "resourceType", j_str(a, "ValueSet"));
    if (j_gets(vs, "url")) j_set(a, out, "url", j_str(a, j_gets(vs, "url")));
    if (j_gets(vs, "version")) j_set(a, out, "version", j_str(a, j_gets(vs, "version")));
    if (j_gets(vs, "name")) j_set(a, out, "name", j_str(a, j_gets(vs, "name")));
    j_set(a, out, "status", j_str(a, j_gets(vs, "status") ? j_gets(vs, "status") : "active"));
    jval *exp = j_obj(a);
    j_set(a, exp, "identifier", j_str(a, arena_printf(a, "urn:uuid:00000000-0000-4000-8000-%012zu", all.n)));
    j_set(a, exp, "timestamp", j_str(a, "2024-01-01T00:00:00Z"));
    jval *contains = j_arr(a);
    size_t kept = 0, skipped = 0;
    for (size_t i = 0; i < all.n; i++) {
        jval *e = all.v[i];
        if (filter && *filter) {
            const char *disp = j_gets(e, "display");
            const char *code = j_gets(e, "code");
            if (!str_icontains(disp, filter) && !str_icontains(code, filter)) continue;
        }
        if (offset > 0 && (int)skipped < offset) { skipped++; continue; }
        if (count >= 0 && (int)kept >= count) break;
        j_push(a, contains, e);
        kept++;
    }
    j_set(a, exp, "total", j_num(a, arena_printf(a, "%zu", all.n)));
    if (offset > 0) j_set(a, exp, "offset", j_num(a, arena_printf(a, "%d", offset)));
    j_set(a, exp, "contains", contains);
    j_set(a, out, "expansion", exp);
    vec_free(&all);
    *status = 200;
    return out;
}

int vs_has_code(fhirdefs *d, const char *vsurl, const char *system, const char *code) {
    if (!code) return 1;
    jval *vs = find_vs(d, vsurl);
    if (!vs) return 1;                 /* a value set this server does not hold binds nothing */
    arena tmp; arena_init(&tmp);
    vec all; vec_init(&all);
    expand_compose(&tmp, d, vs, &all, 0);
    int found = 0;
    for (size_t i = 0; i < all.n && !found; i++) {
        jval *e = all.v[i];
        if (!str_eq(j_gets(e, "code"), code)) continue;
        if (!system || str_eq(j_gets(e, "system"), system)) found = 1;
    }
    if (!all.n) found = 1;             /* an expansion this server cannot compute binds nothing */
    vec_free(&all);
    arena_free(&tmp);
    return found;
}

jval *term_validate_code(arena *a, fhirdefs *d, const char *vsurl, const char *system,
                         const char *code, const char *display, jval *coding,
                         jval *cc, int *status, const char **msg) {
    if (coding) { system = j_gets(coding, "system"); code = j_gets(coding, "code");
                  if (!display) display = j_gets(coding, "display"); }
    if (cc) {
        jval *cods = j_get(cc, "coding");
        if (j_len(cods)) { jval *c0 = j_at(cods, 0);
                           system = j_gets(c0, "system"); code = j_gets(c0, "code"); }
    }
    jval *vs = find_vs(d, vsurl);
    if (!vs) { *status = 404; *msg = "this server does not hold that value set"; return NULL; }
    if (!code) { *status = 400; *msg = "no code to validate"; return NULL; }
    vec all; vec_init(&all);
    expand_compose(a, d, vs, &all, 0);
    jval *hit = NULL;
    for (size_t i = 0; i < all.n && !hit; i++) {
        jval *e = all.v[i];
        if (str_eq(j_gets(e, "code"), code) && (!system || str_eq(j_gets(e, "system"), system)))
            hit = e;
    }
    jval *ps = params(a);
    put_param(a, ps, "result", "valueBoolean", j_bool(a, hit != NULL));
    if (hit && j_gets(hit, "display"))
        put_param(a, ps, "display", "valueString", j_str(a, j_gets(hit, "display")));
    if (hit && display && j_gets(hit, "display") && !str_eq(display, j_gets(hit, "display")))
        put_param(a, ps, "message", "valueString",
                  j_str(a, arena_printf(a, "the display '%s' is not the one this code system "
                                           "gives for %s", display, code)));
    if (!hit)
        put_param(a, ps, "message", "valueString",
                  j_str(a, arena_printf(a, "the code %s is not in the value set %s",
                                        code, vsurl ? vsurl : "")));
    if (system) put_param(a, ps, "system", "valueUri", j_str(a, system));
    put_param(a, ps, "code", "valueCode", j_str(a, code));
    vec_free(&all);
    *status = 200;
    return ps;
}

jval *term_lookup(arena *a, fhirdefs *d, const char *system, const char *code,
                  int *status, const char **msg) {
    jval *cs = find_cs(d, system);
    if (!cs) { *status = 404; *msg = "this server does not hold that code system"; return NULL; }
    jval *c = cs_find(j_get(cs, "concept"), code);
    if (!c) { *status = 404; *msg = "that code is not in this code system"; return NULL; }
    jval *ps = params(a);
    put_param(a, ps, "name", "valueString",
              j_str(a, j_gets(cs, "name") ? j_gets(cs, "name") : j_gets(cs, "url")));
    if (j_gets(cs, "version")) put_param(a, ps, "version", "valueString", j_str(a, j_gets(cs, "version")));
    if (j_gets(c, "display")) put_param(a, ps, "display", "valueString", j_str(a, j_gets(c, "display")));
    if (j_gets(c, "definition"))
        put_param(a, ps, "definition", "valueString", j_str(a, j_gets(c, "definition")));
    jval *des = j_get(c, "designation");
    for (int i = 0; i < j_len(des); i++) {
        jval *dd = j_at(des, i);
        jval *p = param(a, "designation");
        jval *parts = j_arr(a);
        if (j_gets(dd, "language")) {
            jval *q = param(a, "language");
            j_set(a, q, "valueCode", j_str(a, j_gets(dd, "language")));
            j_push(a, parts, q);
        }
        jval *q2 = param(a, "value");
        j_set(a, q2, "valueString", j_str(a, j_gets(dd, "value")));
        j_push(a, parts, q2);
        j_set(a, p, "part", parts);
        j_push(a, j_get(ps, "parameter"), p);
    }
    jval *props = j_get(c, "property");
    for (int i = 0; i < j_len(props); i++) {
        jval *pr = j_at(props, i);
        jval *p = param(a, "property");
        jval *parts = j_arr(a);
        jval *q = param(a, "code");
        j_set(a, q, "valueCode", j_str(a, j_gets(pr, "code")));
        j_push(a, parts, q);
        for (jmem *m = pr->u.obj.first; m; m = m->next) {
            if (strncmp(m->key, "value", 5)) continue;
            jval *q2 = param(a, "value");
            j_set(a, q2, m->key, m->val);
            j_push(a, parts, q2);
        }
        j_set(a, p, "part", parts);
        j_push(a, j_get(ps, "parameter"), p);
    }
    *status = 200;
    return ps;
}

jval *term_subsumes(arena *a, fhirdefs *d, const char *system, const char *ca,
                    const char *cb, int *status, const char **msg) {
    jval *cs = find_cs(d, system);
    if (!cs) { *status = 404; *msg = "this server does not hold that code system"; return NULL; }
    jval *concepts = j_get(cs, "concept");
    if (!cs_find(concepts, ca) || !cs_find(concepts, cb)) {
        *status = 400; *msg = "one of the codes is not in this code system"; return NULL;
    }
    const char *res;
    if (str_eq(ca, cb)) res = "equivalent";
    else if (cs_is_a(concepts, ca, cb, 0)) res = "subsumes";
    else if (cs_is_a(concepts, cb, ca, 0)) res = "subsumed-by";
    else res = "not-subsumed";
    jval *ps = params(a);
    put_param(a, ps, "outcome", "valueCode", j_str(a, res));
    *status = 200;
    return ps;
}

jval *term_translate(arena *a, fhirdefs *d, const char *cmurl, const char *system,
                     const char *code, const char *target, int *status, const char **msg) {
    defs_load_igs(d);
    jval *cm = cmurl ? smap_get(&d->conceptmaps, cmurl) : NULL;
    jval *ps = params(a);
    int matched = 0;
    /* without a named map, every map the server holds is searched, which is what
       `conceptmap-operations.html` describes for the no-`url` form */
    jval *maps[512];
    size_t nmaps = 0;
    if (cm) maps[nmaps++] = cm;
    else
        for (size_t z = 0; z < d->conceptmaps.cap && nmaps < 512; z++)
            if (d->conceptmaps.keys[z]) maps[nmaps++] = (jval *)d->conceptmaps.vals[z];
    if (cmurl && !cm) { *status = 404; *msg = "this server does not hold that concept map"; return NULL; }
    for (size_t mi = 0; mi < nmaps; mi++) {
        jval *groups = j_get(maps[mi], "group");
        for (int g = 0; g < j_len(groups); g++) {
            jval *grp = j_at(groups, g);
            const char *src = j_gets(grp, "source");
            const char *tgt = j_gets(grp, "target");
            if (system && src && !str_eq(src, system)) continue;
            if (target && tgt && !str_eq(tgt, target)) continue;
            jval *els = j_get(grp, "element");
            for (int i = 0; i < j_len(els); i++) {
                jval *el = j_at(els, i);
                if (!str_eq(j_gets(el, "code"), code)) continue;
                jval *tg = j_get(el, "target");
                for (int k = 0; k < j_len(tg); k++) {
                    jval *t = j_at(tg, k);
                    jval *p = param(a, "match");
                    jval *parts = j_arr(a);
                    const char *eq = j_gets(t, "equivalence");
                    if (!eq) eq = j_gets(t, "relationship");
                    if (eq) {
                        jval *q = param(a, "equivalence");
                        j_set(a, q, "valueCode", j_str(a, eq));
                        j_push(a, parts, q);
                    }
                    jval *q2 = param(a, "concept");
                    jval *cod = j_obj(a);
                    if (tgt) j_set(a, cod, "system", j_str(a, tgt));
                    if (j_gets(t, "code")) j_set(a, cod, "code", j_str(a, j_gets(t, "code")));
                    if (j_gets(t, "display")) j_set(a, cod, "display", j_str(a, j_gets(t, "display")));
                    j_set(a, q2, "valueCoding", cod);
                    j_push(a, parts, q2);
                    jval *q3 = param(a, "source");
                    j_set(a, q3, "valueUri", j_str(a, j_gets(maps[mi], "url")));
                    j_push(a, parts, q3);
                    j_set(a, p, "part", parts);
                    j_push(a, j_get(ps, "parameter"), p);
                    matched = 1;
                }
            }
        }
    }
    jval *r = param(a, "result");
    j_set(a, r, "valueBoolean", j_bool(a, matched));
    jval *arr = j_get(ps, "parameter");
    /* `result` comes first */
    jval *reordered = j_arr(a);
    j_push(a, reordered, r);
    for (int i = 0; i < j_len(arr); i++) j_push(a, reordered, j_at(arr, i));
    j_set(a, ps, "parameter", reordered);
    *status = 200;
    (void)msg;
    return ps;
}
