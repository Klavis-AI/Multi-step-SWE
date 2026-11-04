#include <dirent.h>
#include "defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *p = malloc((size_t)n + 1);
    if (!p) { fclose(f); return NULL; }
    size_t got = fread(p, 1, (size_t)n, f);
    fclose(f);
    p[got] = 0;
    if (len) *len = got;
    return p;
}

static jval *load_json(arena *a, const char *dir, const char *file) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    size_t n = 0;
    char *txt = read_file(path, &n);
    if (!txt) return NULL;
    char err[256];
    jval *v = j_parse(a, txt, n, err, sizeof err);
    free(txt);
    return v;
}

static const char *last_seg(const char *path) {
    const char *d = strrchr(path, '.');
    return d ? d + 1 : path;
}

/* ----- element tree construction from a snapshot ----- */

static int path_depth(const char *p) { int n = 1; for (const char *c = p; *c; c++) if (*c == '.') n++; return n; }

static const char *ext_str(jval *owner, const char *url) {
    jval *exts = j_get(owner, "extension");
    for (int i = 0; i < j_len(exts); i++) {
        jval *e = j_at(exts, i);
        if (str_eq(j_gets(e, "url"), url)) {
            const char *s = j_gets(e, "valueUrl");
            if (!s) s = j_gets(e, "valueString");
            if (!s) s = j_gets(e, "valueCode");
            if (!s) s = j_gets(e, "valueUri");
            if (s) return s;
        }
    }
    return NULL;
}

static void fill_edef(arena *a, edef *e, jval *ed) {
    e->ed = ed;
    e->path = j_gets(ed, "path");
    e->id = j_gets(ed, "id");
    e->tail = last_seg(e->path);
    e->slice_name = j_gets(ed, "sliceName");
    jval *mn = j_get(ed, "min");
    e->min = mn && mn->kind == J_NUM ? atoi(j_num_of(mn)) : 0;
    e->max = j_gets(ed, "max");
    if (!e->max) e->max = "1";
    vec_init(&e->types); vec_init(&e->target_profiles); vec_init(&e->slices);
    jval *ts = j_get(ed, "type");
    for (int i = 0; i < j_len(ts); i++) {
        jval *t = j_at(ts, i);
        const char *code = j_gets(t, "code");
        if (!code) continue;
        /* R4 primitive types in the base spec use a URL in .code with an extension
           carrying the real name; normalise to the short name. */
        if (str_starts(code, "http://hl7.org/fhirpath/System.")) {
            const char *fh = ext_str(t, "http://hl7.org/fhir/StructureDefinition/structuredefinition-fhir-type");
            if (fh) code = fh;
        }
        vec_push(&e->types, (void *)arena_strdup(a, code));
        jval *tp = j_get(t, "targetProfile");
        for (int k = 0; k < j_len(tp); k++) {
            const char *u = j_str_of(j_at(tp, k));
            if (u) vec_push(&e->target_profiles, (void *)arena_strdup(a, u));
        }
        if (j_is(tp, J_STR)) vec_push(&e->target_profiles, (void *)arena_strdup(a, j_str_of(tp)));
    }
    e->content_ref = j_gets(ed, "contentReference");
    /* DSTU2 spells a recursive element `nameReference`, naming the `name` of the
       element it repeats (`Questionnaire.group.group` -> `group`), where STU3
       and later use `contentReference` with a path fragment. */
    if (!e->content_ref) {
        const char *nr = j_gets(ed, "nameReference");
        if (nr) e->content_ref = arena_printf(a, "@name:%s", nr);
    }
    e->elem_name = j_gets(ed, "name");
    jval *bind = j_get(ed, "binding");
    if (bind) {
        e->binding_strength = j_gets(bind, "strength");
        e->binding_vs = j_gets(bind, "valueSet");
        if (!e->binding_vs) {
            jval *vr = j_get(bind, "valueSetReference");        /* DSTU2/STU3 shape */
            if (vr) e->binding_vs = j_gets(vr, "reference");
            const char *vu = j_gets(bind, "valueSetUri");
            if (!e->binding_vs && vu) e->binding_vs = vu;
        }
    }
    for (jmem *m = ed->kind == J_OBJ ? ed->u.obj.first : NULL; m; m = m->next) {
        if (str_starts(m->key, "fixed") && strlen(m->key) > 5) { e->fixed_type = m->key + 5; e->fixed_val = m->val; }
        else if (str_starts(m->key, "pattern") && strlen(m->key) > 7) { e->pattern_type = m->key + 7; e->pattern_val = m->val; }
    }
    jval *ms = j_get(ed, "isModifier");
    e->is_modifier = ms && ms->kind == J_BOOL && ms->u.b;
    jval *su = j_get(ed, "isSummary");
    e->is_summary = su && su->kind == J_BOOL && su->u.b;
    e->slicing = j_get(ed, "slicing");
}

/* DSTU2 defines `code`, `id`, `markdown`, `oid`, `uuid`, `SimpleQuantity` and
   others as constraints on another type, so their snapshot paths are rooted at
   the *base* type.  Re-root them on the definition's own name. */
static const char *reroot(arena *a, const char *path, const char *from, const char *to) {
    if (!path || !from || !to || str_eq(from, to)) return path;
    size_t fl = strlen(from);
    if (strncmp(path, from, fl) != 0) return path;
    if (path[fl] != 0 && path[fl] != '.') return path;
    return arena_printf(a, "%s%s", to, path + fl);
}

/* An XSD regex is anchored by definition, and `^` and `$` are ordinary
   characters in that flavour.  The R6 ballot's primitive types nevertheless
   write their patterns with both -- `^[^\\s]+( [^\\s]+)*$` for `code` where R5
   writes `[^\\s]+( [^\\s]+)*` -- so a compiler that takes them literally rejects
   every code in the release.  The anchors are dropped; the match is anchored
   anyway. */
static const char *rx_unanchor(arena *a, const char *re) {
    if (!re || !*re) return re;
    size_t n = strlen(re);
    size_t start = (re[0] == '^') ? 1 : 0;
    size_t end = (n > start && re[n - 1] == '$') ? n - 1 : n;
    if (start == 0 && end == n) return re;
    return arena_strndup(a, re + start, end - start);
}

static void sd_build(arena *a, sdef *s, jval *snapshot, const char *root_from) {
    smap_init(&s->by_path, 512);
    int n = j_len(snapshot);
    if (n == 0) return;
    edef **stack = calloc((size_t)n + 4, sizeof(edef *));
    edef *root = NULL;
    for (int i = 0; i < n; i++) {
        jval *ed = j_at(snapshot, i);
        const char *path = j_gets(ed, "path");
        if (!path) continue;
        if (root_from && s->type) path = reroot(a, path, root_from, s->type);
        edef *e = arena_calloc(a, sizeof(edef));
        fill_edef(a, e, ed);
        e->path = path;
        e->tail = last_seg(path);
        int d = path_depth(path);
        if (d == 1) { root = e; stack[1] = e; continue; }
        edef *parent = stack[d - 1];
        if (!parent) continue;
        e->parent = parent;
        if (e->slice_name) {
            /* attach to the slicing root with the same path when there is one */
            edef *host = NULL;
            for (edef *c = parent->first_child; c; c = c->next)
                if (str_eq(c->path, path) && !c->slice_name) host = c;
            if (host) { vec_push(&host->slices, e); stack[d] = e; continue; }
        }
        if (parent->last_child) parent->last_child->next = e; else parent->first_child = e;
        parent->last_child = e;
        stack[d] = e;
        if (!smap_get(&s->by_path, path)) smap_put(&s->by_path, e->path, e);
    }
    free(stack);
    s->root = root;
    if (root) smap_put(&s->by_path, root->path, root);
}

static void sd_init(arena *a, sdef *s, jval *sd) {
    s->sd = sd;
    s->url = j_gets(sd, "url");
    s->id = j_gets(sd, "id");
    s->name = j_gets(sd, "name");
    s->type = j_gets(sd, "type");
    if (!s->type) s->type = j_gets(sd, "id");
    s->kind = j_gets(sd, "kind");
    s->derivation = j_gets(sd, "derivation");
    s->base = j_gets(sd, "baseDefinition");
    if (!s->base) { jval *b = j_get(sd, "base"); if (b && b->kind == J_STR) s->base = b->u.str; }
    jval *ab = j_get(sd, "abstract");
    s->abstract_ = ab && ab->kind == J_BOOL && ab->u.b;
    jval *snap = j_get(j_get(sd, "snapshot"), "element");
    if (!snap) snap = j_get(j_get(sd, "differential"), "element");
    const char *root_from = NULL;
    jval *first = j_at(snap, 0);
    if (first) {
        const char *rp = j_gets(first, "path");
        if (rp && s->type && !str_eq(rp, s->type)) root_from = rp;
    }
    sd_build(a, s, snap, root_from);
    /* DSTU2 calls every datatype `datatype`; a primitive is the one whose
       `<Type>.value` element carries the regex / json-type extensions. */
    char vp[256];
    snprintf(vp, sizeof vp, "%s.value", s->type ? s->type : "");
    edef *ve = smap_get(&s->by_path, vp);
    int prim = 0;
    if (ve) {
        jval *ts = j_get(ve->ed, "type");
        for (int i = 0; i < j_len(ts); i++) {
            jval *t = j_at(ts, i);
            jval *exts = j_get(t, "extension");
            for (int k = 0; k < j_len(exts); k++) {
                jval *x = j_at(exts, k);
                const char *u = j_gets(x, "url");
                if (str_ends(u ? u : "", "/regex") || str_ends(u ? u : "", "structuredefinition-regex")) {
                    const char *r = j_gets(x, "valueString");
                    if (r) { s->regex = rx_unanchor(a, arena_strdup(a, r));
                                      s->regex = rx_unanchor(a, s->regex);
                s->re = rx_compile(a, s->regex); prim = 1; }
                }
            }
            jval *codeext = j_get(t, "_code");
            if (codeext) prim = 1;
        }
    }
    /* DSTU2 publishes no regex on its primitives: `instant.value` carries only
       `structuredefinition-xml-type`, naming the XML Schema type whose lexical
       rule applies.  The lexical form of these types is the same in every FHIR
       release, so where a release supplies no regex the published one for that
       primitive is used. */
    if (prim && !s->regex && s->type) {
        static const struct { const char *type, *re; } BUILTIN[] = {
            {"instant", "([0-9]([0-9]([0-9][1-9]|[1-9]0)|[1-9]00)|[1-9]000)-(0[1-9]|1[0-2])"
                        "-(0[1-9]|[1-2][0-9]|3[0-1])T([01][0-9]|2[0-3]):[0-5][0-9]:([0-5][0-9]|60)"
                        "(\\.[0-9]+)?(Z|(\\+|-)((0[0-9]|1[0-3]):[0-5][0-9]|14:00))"},
            {"date", "([0-9]([0-9]([0-9][1-9]|[1-9]0)|[1-9]00)|[1-9]000)"
                     "(-(0[1-9]|1[0-2])(-(0[1-9]|[1-2][0-9]|3[0-1]))?)?"},
            {"dateTime", "([0-9]([0-9]([0-9][1-9]|[1-9]0)|[1-9]00)|[1-9]000)"
                         "(-(0[1-9]|1[0-2])(-(0[1-9]|[1-2][0-9]|3[0-1])"
                         "(T([01][0-9]|2[0-3]):[0-5][0-9]:([0-5][0-9]|60)(\\.[0-9]+)?"
                         "(Z|(\\+|-)((0[0-9]|1[0-3]):[0-5][0-9]|14:00)))?)?)?"},
            {"time", "([01][0-9]|2[0-3]):[0-5][0-9]:([0-5][0-9]|60)(\\.[0-9]+)?"},
            {"base64Binary", "(\\s*([0-9a-zA-Z\\+/=]){4}\\s*)+"},
            {"oid", "urn:oid:(0|[1-9][0-9]*)(\\.(0|[1-9][0-9]*))*"},
            {"uuid", "urn:uuid:[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}"
                     "-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}"},
            {"decimal", "-?([0]|([1-9][0-9]*))(\\.[0-9]+)?([eE][+-]?[0-9]+)?"},
            {"integer", "-?([0]|([1-9][0-9]*))"},
            {"unsignedInt", "[0]|([1-9][0-9]*)"},
            {"positiveInt", "[1-9][0-9]*"},
            {NULL, NULL}};
        for (int i = 0; BUILTIN[i].type; i++)
            if (str_eq(s->type, BUILTIN[i].type)) {
                s->regex = BUILTIN[i].re;
                s->regex = rx_unanchor(a, s->regex);
                s->re = rx_compile(a, s->regex);
                break;
            }
    }
    if (prim && !str_eq(s->kind, "resource")) s->kind = "primitive-type";
    else if (str_eq(s->kind, "datatype")) s->kind = "complex-type";
}

/* ----- package loading ----- */

/* DSTU2 defines search parameters with XPath:
   `f:Patient/f:telecom[system/@value='phone']` selects `Patient.telecom`. */
static const char *xpath_to_path(arena *a, const char *xp) {
    buf out; buf_init(&out);
    const char *p = xp;
    while (*p) {
        const char *bar = strchr(p, '|');
        size_t len = bar ? (size_t)(bar - p) : strlen(p);
        char *term = arena_strndup(a, p, len);
        /* drop predicates and the f: namespace prefix, turn / into . */
        buf t; buf_init(&t);
        int depth = 0, ok = 1;
        for (char *q = term; *q; q++) {
            if (*q == '[') { depth++; continue; }
            if (*q == ']') { if (depth) depth--; continue; }
            if (depth) continue;
            if (*q == 'f' && q[1] == ':') { q++; continue; }
            if (*q == '/') { buf_putc(&t, '.'); continue; }
            if (*q == ' ' || *q == '\n' || *q == '\t') continue;
            if (!isalnum((unsigned char)*q) && *q != '.' && *q != '_') { ok = 0; break; }
            buf_putc(&t, *q);
        }
        if (ok && t.n) {
            if (out.n) buf_puts(&out, " | ");
            buf_put(&out, t.p, t.n);
        }
        buf_free(&t);
        if (!bar) break;
        p = bar + 1;
    }
    const char *res = arena_strndup(a, out.p ? out.p : "", out.n);
    buf_free(&out);
    return res;
}

static int sp_score(const sparam *p, const char *base) {
    int s = 0;
    const char *slash = p->url ? strrchr(p->url, '/') : NULL;
    const char *name = slash ? slash + 1 : p->url;
    if (name && base && p->code) {
        size_t bl = strlen(base), cl = strlen(p->code);
        if (strlen(name) == bl + 1 + cl && strncmp(name, base, bl) == 0 &&
            name[bl] == '-' && strcmp(name + bl + 1, p->code) == 0) s += 4;
    }
    if (p->expression && *p->expression) s += 2;
    return s;
}

static int sp_better(const sparam *a, const sparam *b, const char *base) {
    return sp_score(a, base) > sp_score(b, base);
}

static void add_sp(fhirdefs *d, jval *sp) {
    sparam *p = arena_calloc(&d->a, sizeof(sparam));
    p->sp = sp;
    p->url = j_gets(sp, "url");
    p->code = j_gets(sp, "code");
    p->type = j_gets(sp, "type");
    p->expression = j_gets(sp, "expression");
    if (!p->expression) {
        const char *xp = j_gets(sp, "xpath");
        if (xp) p->expression = xpath_to_path(&d->a, xp);
    }
    vec_init(&p->bases); vec_init(&p->targets); vec_init(&p->components);
    jval *base = j_get(sp, "base");
    for (int i = 0; i < j_len(base); i++) {
        const char *b = j_str_of(j_at(base, i));
        if (b) vec_push(&p->bases, (void *)b);
    }
    if (j_is(base, J_STR)) vec_push(&p->bases, (void *)j_str_of(base));
    jval *tgt = j_get(sp, "target");
    for (int i = 0; i < j_len(tgt); i++) {
        const char *t = j_str_of(j_at(tgt, i));
        if (t) vec_push(&p->targets, (void *)t);
    }
    jval *comp = j_get(sp, "component");
    for (int i = 0; i < j_len(comp); i++) vec_push(&p->components, j_at(comp, i));
    vec_push(&d->sparams, p);
    for (size_t i = 0; i < p->bases.n; i++) {
        const char *base = (const char *)p->bases.v[i];
        const char *key = arena_printf(&d->a, "%s|%s", base, p->code ? p->code : "");
        /* A package may publish several search parameters with the same code for
           one resource: the resource's own definition, a shared multi-resource
           one, and -- in the core packages -- example definitions.  Prefer the one
           whose canonical is named `<Type>-<code>`, then one that has a path. */
        sparam *prev = smap_get(&d->sp_by_key, key);
        if (prev && !sp_better(p, prev, base)) continue;
        smap_put(&d->sp_by_key, key, p);
    }
}

fhirdefs *defs_load(const char *pkgdir, const char *ver, const char *code, const char *base_path) {
    fhirdefs *d = calloc(1, sizeof(fhirdefs));
    arena_init(&d->a);
    d->ver = arena_strdup(&d->a, ver);
    d->code = arena_strdup(&d->a, code);
    d->base_path = arena_strdup(&d->a, base_path);
    d->dir = arena_strdup(&d->a, pkgdir);
    smap_init(&d->sd_by_url, 2048);
    smap_init(&d->sd_by_name, 2048);
    smap_init(&d->res_by_name, 512);
    smap_init(&d->sp_by_key, 4096);
    smap_init(&d->compartments, 32);
    smap_init(&d->opdefs, 256);
    smap_init(&d->valuesets, 4096);
    smap_init(&d->codesystems, 4096);
    smap_init(&d->conceptmaps, 256);
    smap_init(&d->profiles, 1024);
    vec_init(&d->profile_list);
    vec_init(&d->resources); vec_init(&d->sparams); vec_init(&d->all_types);

    jval *idx = load_json(&d->a, pkgdir, ".index.json");
    if (!idx) { fprintf(stderr, "defs: cannot read %s/.index.json\n", pkgdir); free(d); return NULL; }
    jval *files = j_get(idx, "files");
    for (int i = 0; i < j_len(files); i++) {
        jval *f = j_at(files, i);
        const char *rt = j_gets(f, "resourceType");
        const char *fn = j_gets(f, "filename");
        if (!rt || !fn) continue;
        if (str_eq(rt, "StructureDefinition")) {
            jval *sd = load_json(&d->a, pkgdir, fn);
            if (!sd) continue;
            const char *kind = j_gets(sd, "kind");
            const char *deriv = j_gets(sd, "derivation");
            if (!kind) continue;
            if (str_eq(kind, "logical")) continue;
            if (str_eq(deriv, "constraint")) continue;          /* R4+ profiles */
            /* DSTU2 marks both resource profiles and several *datatypes*
               (`code`, `id`, `oid`, `SimpleQuantity`, ...) with constrainedType;
               only the resource profiles are profiles. */
            const char *ctype = j_gets(sd, "constrainedType");
            if (ctype && !str_eq(kind, "datatype")) continue;
            sdef *s = arena_calloc(&d->a, sizeof(sdef));
            sd_init(&d->a, s, sd);
            if (s->url) smap_put(&d->sd_by_url, s->url, s);
            if (s->type) smap_put(&d->sd_by_name, s->type, s);
            if (s->id && !smap_get(&d->sd_by_name, s->id)) smap_put(&d->sd_by_name, s->id, s);
            if (str_eq(kind, "resource") && !s->abstract_) {
                vec_push(&d->resources, s);
                smap_put(&d->res_by_name, s->type, s);
                vec_push(&d->all_types, (void *)s->type);
            }
        } else if (str_eq(rt, "SearchParameter")) {
            jval *sp = load_json(&d->a, pkgdir, fn);
            if (sp) add_sp(d, sp);
        } else if (str_eq(rt, "CompartmentDefinition")) {
            jval *cd = load_json(&d->a, pkgdir, fn);
            const char *nm = cd ? j_gets(cd, "code") : NULL;
            if (nm) smap_put(&d->compartments, arena_strdup(&d->a, nm), cd);
        } else if (str_eq(rt, "OperationDefinition")) {
            jval *od = load_json(&d->a, pkgdir, fn);
            const char *u = od ? j_gets(od, "url") : NULL;
            if (u) smap_put(&d->opdefs, u, od);
        }
    }
    return d;
}

/* Load the terminology resources on demand: they dominate the package file count. */
void defs_load_terminology(fhirdefs *d) {
    if (d->valuesets.n || d->codesystems.n) return;
    jval *idx = load_json(&d->a, d->dir, ".index.json");
    jval *files = j_get(idx, "files");
    for (int i = 0; i < j_len(files); i++) {
        jval *f = j_at(files, i);
        const char *rt = j_gets(f, "resourceType");
        const char *fn = j_gets(f, "filename");
        if (!rt || !fn) continue;
        if (str_eq(rt, "ValueSet")) {
            jval *v = load_json(&d->a, d->dir, fn);
            const char *u = v ? j_gets(v, "url") : NULL;
            if (u) smap_put(&d->valuesets, u, v);
        } else if (str_eq(rt, "CodeSystem")) {
            jval *v = load_json(&d->a, d->dir, fn);
            const char *u = v ? j_gets(v, "url") : NULL;
            if (u) smap_put(&d->codesystems, u, v);
        }
    }
}

/* ----- implementation guides ----- */

/* Every package in the FHIR package cache whose `fhirVersions` covers this
   release is loaded: its profiles (StructureDefinitions whose derivation is
   `constraint`), value sets, code systems and concept maps.  Which guides are
   installed is the deployment's business, not a flag -- the same way the core
   definitions are found. */
static void load_ig_dir(fhirdefs *d, const char *pkgdir) {
    jval *idx = load_json(&d->a, pkgdir, ".index.json");
    if (!idx) return;
    jval *files = j_get(idx, "files");
    for (int i = 0; i < j_len(files); i++) {
        jval *f = j_at(files, i);
        const char *rt = j_gets(f, "resourceType");
        const char *fn = j_gets(f, "filename");
        if (!rt || !fn) continue;
        if (str_eq(rt, "StructureDefinition")) {
            jval *sd = load_json(&d->a, pkgdir, fn);
            if (!sd) continue;
            const char *deriv = j_gets(sd, "derivation");
            const char *kind = j_gets(sd, "kind");
            if (!str_eq(deriv, "constraint") || !kind) continue;
            if (str_eq(kind, "logical")) continue;
            sdef *s = arena_calloc(&d->a, sizeof(sdef));
            sd_init(&d->a, s, sd);
            if (!s->url) continue;
            smap_put(&d->profiles, s->url, s);
            vec_push(&d->profile_list, s);
        } else if (str_eq(rt, "ValueSet")) {
            jval *v = load_json(&d->a, pkgdir, fn);
            const char *u = v ? j_gets(v, "url") : NULL;
            if (u && !smap_get(&d->valuesets, u)) smap_put(&d->valuesets, u, v);
        } else if (str_eq(rt, "CodeSystem")) {
            jval *v = load_json(&d->a, pkgdir, fn);
            const char *u = v ? j_gets(v, "url") : NULL;
            if (u && !smap_get(&d->codesystems, u)) smap_put(&d->codesystems, u, v);
        } else if (str_eq(rt, "ConceptMap")) {
            jval *v = load_json(&d->a, pkgdir, fn);
            const char *u = v ? j_gets(v, "url") : NULL;
            if (u) smap_put(&d->conceptmaps, u, v);
        }
    }
}

static int pkg_covers_version(fhirdefs *d, const char *pkgdir) {
    jval *pj = load_json(&d->a, pkgdir, "package.json");
    if (!pj) return 0;
    jval *fv = j_get(pj, "fhirVersions");
    if (!fv) fv = j_get(pj, "fhir-version-list");
    char major[8];
    snprintf(major, sizeof major, "%.*s", (int)(strchr(d->ver, '.') - d->ver), d->ver);
    for (int i = 0; i < j_len(fv); i++) {
        const char *v = j_str_of(j_at(fv, i));
        if (!v) continue;
        if (str_eq(v, d->ver)) return 1;
        /* `4.0.1` is covered by a package declaring `4.0` or `4.0.1` */
        if (!strncmp(v, d->ver, strlen(v)) && strlen(v) >= 3) return 1;
        if (!strncmp(d->ver, v, strlen(d->ver))) return 1;
        (void)major;
    }
    return 0;
}

void defs_load_igs(fhirdefs *d) {
    if (d->igs_loaded) return;
    d->igs_loaded = 1;
    defs_load_terminology(d);
    const char *cache = getenv("FHIR_PACKAGE_CACHE");
    char root[1024];
    if (cache && *cache) snprintf(root, sizeof root, "%s", cache);
    else snprintf(root, sizeof root, "%s/.fhir/packages", getenv("HOME") ? getenv("HOME") : "/root");
    DIR *dir = opendir(root);
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (e->d_name[0] == '.' || !strchr(e->d_name, '#')) continue;
        char pkgdir[2400];
        snprintf(pkgdir, sizeof pkgdir, "%s/%s/package", root, e->d_name);
        if (!strcmp(pkgdir, d->dir)) continue;               /* the core package */
        if (strstr(e->d_name, ".examples#")) continue;        /* instances, not definitions */
        if (!pkg_covers_version(d, pkgdir)) continue;
        load_ig_dir(d, pkgdir);
    }
    closedir(dir);
    fprintf(stderr, "%s: %zu profiles, %zu value sets, %zu code systems, %zu concept maps\n",
            d->code, d->profile_list.n, d->valuesets.n, d->codesystems.n, d->conceptmaps.n);
}

/* Resolve a dotted element path relative to an element (a slice), for the
   discriminators a profile's slicing names. */
edef *sd_path_rel(edef *base, const char *dotted) {
    if (!base || !dotted) return base;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", dotted);
    edef *cur = base;
    for (char *t = strtok(buf, "."); t && cur; t = strtok(NULL, ".")) {
        if (str_eq(t, "$this") || str_eq(t, "resolve()")) continue;
        edef *found = NULL;
        size_t tl = strlen(t);
        for (edef *k = cur->first_child; k && !found; k = k->next) {
            if (str_eq(k->tail, t)) found = k;
            else {
                size_t kl = strlen(k->tail);
                if (kl > 3 && !strcmp(k->tail + kl - 3, "[x]") &&
                    kl - 3 == tl && !strncmp(k->tail, t, tl)) found = k;
            }
        }
        cur = found;
    }
    return cur;
}

sdef *defs_profile(fhirdefs *d, const char *canonical) {
    if (!canonical) return NULL;
    defs_load_igs(d);
    char *c = arena_strdup(&d->a, canonical);
    char *bar = strchr(c, '|');
    if (bar) *bar = 0;
    sdef *s = smap_get(&d->profiles, c);
    if (s) return s;
    /* a canonical that names a base resource definition is not a profile but is
       a legal argument to $validate */
    return smap_get(&d->sd_by_url, c);
}

sdef *defs_sd(fhirdefs *d, const char *n) {
    if (!n) return NULL;
    sdef *s = smap_get(&d->sd_by_url, n);
    if (s) return s;
    s = smap_get(&d->sd_by_name, n);
    if (s) return s;
    const char *slash = strrchr(n, '/');
    if (slash) return smap_get(&d->sd_by_name, slash + 1);
    return NULL;
}

sdef *defs_resource(fhirdefs *d, const char *t) { return t ? smap_get(&d->res_by_name, t) : NULL; }
int defs_is_resource(fhirdefs *d, const char *t) { return defs_resource(d, t) != NULL; }
edef *sd_path(sdef *s, const char *path) { return s ? smap_get(&s->by_path, path) : NULL; }
sparam *defs_sp(fhirdefs *d, const char *type, const char *code) {
    if (!type || !code) return NULL;
    char key[512];
    snprintf(key, sizeof key, "%s|%s", type, code);
    return smap_get(&d->sp_by_key, key);
}

const char *ed_choice_type(edef *e, const char *json_name) {
    if (!e || !e->tail) return NULL;
    size_t bl = strlen(e->tail);
    if (bl < 3 || strcmp(e->tail + bl - 3, "[x]") != 0) return NULL;
    size_t stem = bl - 3;
    if (strncmp(json_name, e->tail, stem) != 0) return NULL;
    const char *suffix = json_name + stem;
    if (!*suffix) return NULL;
    for (size_t i = 0; i < e->types.n; i++) {
        const char *t = (const char *)e->types.v[i];
        if (strlen(t) != strlen(suffix)) continue;
        if (toupper((unsigned char)t[0]) != (unsigned char)suffix[0]) continue;
        if (strcmp(t + 1, suffix + 1) != 0) continue;
        return t;
    }
    return NULL;
}

edef *ed_child(fhirdefs *d, sdef *s, edef *parent, const char *json_name, const char **out_type) {
    (void)d;
    if (out_type) *out_type = NULL;
    if (!parent) return NULL;
    size_t nl = strlen(json_name);
    for (edef *c = parent->first_child; c; c = c->next) {
        if (str_eq(c->tail, json_name)) {
            if (out_type && c->types.n == 1) *out_type = (const char *)c->types.v[0];
            return c;
        }
        const char *ct = ed_choice_type(c, json_name);
        if (ct) { if (out_type) *out_type = ct; return c; }
        /* A choice element addressed by its base name, the way FHIRPath and
           SearchParameter.expression address it: `Observation.value`. */
        size_t tl = strlen(c->tail);
        if (tl == nl + 3 && strcmp(c->tail + tl - 3, "[x]") == 0 &&
            strncmp(c->tail, json_name, nl) == 0)
            return c;
    }
    (void)s;
    return NULL;
}

/* Find the element a DSTU2 `nameReference` names, by walking the snapshot. */
static edef *ed_by_name(edef *node, const char *want) {
    if (!node) return NULL;
    for (edef *c = node->first_child; c; c = c->next) {
        if (c->elem_name && str_eq(c->elem_name, want)) return c;
        edef *r = ed_by_name(c, want);
        if (r) return r;
    }
    return NULL;
}

int ed_children_of(fhirdefs *d, sdef *owner, edef *e, const char *type_code,
                   sdef **out_sd, edef **out_node) {
    *out_sd = NULL; *out_node = NULL;
    if (e && e->content_ref) {
        const char *ref = e->content_ref;
        edef *t = NULL;
        if (!strncmp(ref, "@name:", 6)) {
            t = ed_by_name(owner ? owner->root : NULL, ref + 6);
        } else {
            const char *hash = strrchr(ref, '#');
            if (hash) ref = hash + 1;
            t = sd_path(owner, ref);
        }
        if (!t) return 0;
        *out_sd = owner; *out_node = t;
        return 1;
    }
    if (e && e->first_child) { *out_sd = owner; *out_node = e; return 1; }
    if (!type_code) return 0;
    sdef *ts = defs_sd(d, type_code);
    if (!ts || !ts->root) return 0;
    *out_sd = ts; *out_node = ts->root;
    return 1;
}
