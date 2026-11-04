#include "validate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


void vres_init(vresult *r, arena *a) { r->a = a; r->v = NULL; r->n = r->cap = 0; r->errors = 0; }

void vres_add(vresult *r, const char *sev, const char *code, const char *expr, const char *text) {
    if (r->n == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 16;
        r->v = realloc(r->v, r->cap * sizeof(vissue));
        if (!r->v) { fprintf(stderr, "oom\n"); exit(70); }
    }
    r->v[r->n].severity = arena_strdup(r->a, sev);
    r->v[r->n].code = arena_strdup(r->a, code);
    r->v[r->n].expression = arena_strdup(r->a, expr ? expr : "");
    r->v[r->n].text = arena_strdup(r->a, text ? text : "");
    r->n++;
    if (str_eq(sev, "error") || str_eq(sev, "fatal")) r->errors++;
}

static int valid_id(const char *s) {
    if (!s || !*s || strlen(s) > 64) return 0;
    for (const char *p = s; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '.') return 0;
    return 1;
}

typedef struct {
    arena    *a;
    fhirdefs *d;
    vresult  *out;
    int       depth;
    int       profile;   /* walking a profile's snapshot, not the base's */
} vctx;

static void walk(vctx *c, jval *v, sdef *s, edef *parent, const char *path);

static const char *prim_json_kind(const char *type) {
    if (str_eq(type, "boolean")) return "boolean";
    if (str_eq(type, "integer") || str_eq(type, "unsignedInt") || str_eq(type, "positiveInt") ||
        str_eq(type, "decimal") || str_eq(type, "integer64")) return "number";
    return "string";
}

static int is_primitive(fhirdefs *d, const char *type) {
    sdef *s = defs_sd(d, type);
    return s && str_eq(s->kind, "primitive-type");
}

static void check_primitive(vctx *c, jval *v, const char *type, const char *path) {
    sdef *s = defs_sd(c->d, type);
    const char *want = prim_json_kind(type);
    const char *got = j_is(v, J_STR) ? "string" : j_is(v, J_NUM) ? "number" : j_is(v, J_BOOL) ? "boolean" :
                      j_is(v, J_OBJ) ? "object" : j_is(v, J_ARR) ? "array" : "null";
    if (str_eq(type, "integer64")) want = "string";
    if (!str_eq(want, got)) {
        vres_add(c->out, "error", "structure", path,
                 "the JSON type of this element does not match the primitive type it is declared as");
        return;
    }
    const char *text = j_is(v, J_STR) ? j_str_of(v) : j_is(v, J_NUM) ? j_num_of(v) : NULL;
    if (!text) return;
    if (str_eq(type, "string") || str_eq(type, "markdown") || str_eq(type, "xhtml")) {
        if (!*text) vres_add(c->out, "error", "value", path, "a primitive value may not be empty");
        return;
    }
    if (!*text) { vres_add(c->out, "error", "value", path, "a primitive value may not be empty"); return; }
    if (s && s->re && !rx_match(s->re, text))
        vres_add(c->out, "error", "value", path, "the value does not match the regex for its primitive type");
}

/* Find the element definition for a JSON member of `parent`. */
static edef *find_child(vctx *c, sdef *s, edef *parent, const char *key, const char **type_out) {
    (void)c;
    *type_out = NULL;
    for (edef *e = parent->first_child; e; e = e->next) {
        if (str_eq(e->tail, key)) {
            if (e->types.n == 1) *type_out = (const char *)e->types.v[0];
            return e;
        }
        const char *t = ed_choice_type(e, key);
        if (t) { *type_out = t; return e; }
    }
    (void)s;
    return NULL;
}

static sdef *anchor_for(vctx *c, sdef *owner, edef *e, const char *type, edef **parent_out) {
    sdef *sd = NULL;
    edef *node = NULL;
    if (!ed_children_of(c->d, owner, e, type, &sd, &node)) { *parent_out = NULL; return NULL; }
    *parent_out = node;
    return sd;
}

/* ----- what a profile adds over the base definitions ----- */

/* `elementdefinition.html`: a value matches a pattern when everything the
   pattern names is present and equal.  An object matches when each of its
   properties matches; an array matches when each of its entries has a match. */
static int pattern_match(jval *pat, jval *got) {
    if (!pat) return 1;
    if (!got) return 0;
    if (j_is(pat, J_OBJ)) {
        if (!j_is(got, J_OBJ)) return 0;
        for (jmem *m = pat->u.obj.first; m; m = m->next)
            if (!pattern_match(m->val, j_get(got, m->key))) return 0;
        return 1;
    }
    if (j_is(pat, J_ARR)) {
        if (!j_is(got, J_ARR)) return 0;
        for (int i = 0; i < j_len(pat); i++) {
            int any = 0;
            for (int k = 0; k < j_len(got) && !any; k++)
                any = pattern_match(j_at(pat, i), j_at(got, k));
            if (!any) return 0;
        }
        return 1;
    }
    return j_equal(pat, got);
}

static const char *vs_url_of(edef *e) {
    return e && e->binding_strength && str_eq(e->binding_strength, "required")
           ? e->binding_vs : NULL;
}

/* every code a value set's expansion or composition admits, by system */
int vs_has_code(fhirdefs *d, const char *vsurl, const char *system, const char *code);

static void collect_codes(arena *a, jval *v, vec *out) {
    /* `Coding`, `CodeableConcept` or a bare code, as (system, code) pairs */
    if (!v) return;
    if (j_is(v, J_STR)) { vec_push(out, NULL); vec_push(out, (void *)j_str_of(v)); return; }
    if (j_is(v, J_OBJ)) {
        jval *cod = j_get(v, "coding");
        if (cod) {
            for (int i = 0; i < j_len(cod); i++) {
                jval *cc = j_at(cod, i);
                vec_push(out, (void *)j_gets(cc, "system"));
                vec_push(out, (void *)j_gets(cc, "code"));
            }
            return;
        }
        const char *cd = j_gets(v, "code");
        if (cd) { vec_push(out, (void *)j_gets(v, "system")); vec_push(out, (void *)cd); }
    }
    (void)a;
}

static void check_constraints(vctx *c, edef *e, jval *val, const char *path) {
    /* Fixed values, patterns and required bindings are what a PROFILE states.
       The base resource definitions are checked for structure, cardinality and
       the lexical form of primitives; whether a code is in a value set is what
       the terminology operations answer, and `$validate` reports it for the
       profile the caller asked about. */
    if (!e || !c->profile) return;
    if (e->fixed_val && !j_equal(e->fixed_val, val))
        vres_add(c->out, "error", "value", path,
                 "this element does not have the fixed value its profile states");
    if (e->pattern_val && !pattern_match(e->pattern_val, val))
        vres_add(c->out, "error", "value", path,
                 "this element does not match the pattern its profile states");
    const char *vs = vs_url_of(e);
    if (vs) {
        vec codes; vec_init(&codes);
        collect_codes(c->a, val, &codes);
        int any = codes.n == 0, ok = 0;
        for (size_t i = 0; i + 1 < codes.n; i += 2) {
            any = 1;
            if (vs_has_code(c->d, vs, (const char *)codes.v[i], (const char *)codes.v[i + 1]))
                ok = 1;
        }
        vec_free(&codes);
        if (any && !ok)
            vres_add(c->out, "error", "code-invalid", path,
                     arena_printf(c->a, "this code is not in the value set %s that its "
                                        "profile requires", vs));
    }
}

/* `profiling.html`: slicing splits an element's repeats into named slices, and a
   repeat belongs to the slice whose discriminators it matches. */
static int discriminator_matches(vctx *c, edef *slice, jval *item);

static void check_slicing(vctx *c, edef *e, jval *val, const char *path) {
    if (!e || !c->profile || !e->slicing || e->slices.n == 0) return;
    int n = j_is(val, J_ARR) ? j_len(val) : (val ? 1 : 0);
    const char *rules = j_gets(e->slicing, "rules");
    for (size_t si = 0; si < e->slices.n; si++) {
        edef *sl = e->slices.v[si];
        int hits = 0;
        for (int i = 0; i < n; i++) {
            jval *item = j_is(val, J_ARR) ? j_at(val, i) : val;
            if (discriminator_matches(c, sl, item)) hits++;
        }
        if (sl->min > hits)
            vres_add(c->out, "error", "required", path,
                     arena_printf(c->a, "the slice %s requires at least %d matching "
                                        "entries and %d match",
                                  sl->slice_name ? sl->slice_name : "?", sl->min, hits));
        if (sl->max && !str_eq(sl->max, "*") && hits > atoi(sl->max))
            vres_add(c->out, "error", "structure", path,
                     arena_printf(c->a, "the slice %s allows at most %s matching entries "
                                        "and %d match",
                                  sl->slice_name ? sl->slice_name : "?", sl->max, hits));
    }
    if (rules && str_eq(rules, "closed")) {
        for (int i = 0; i < n; i++) {
            jval *item = j_is(val, J_ARR) ? j_at(val, i) : val;
            int any = 0;
            for (size_t si = 0; si < e->slices.n && !any; si++)
                any = discriminator_matches(c, e->slices.v[si], item);
            if (!any)
                vres_add(c->out, "error", "structure", path,
                         "this entry matches no slice of a closed slicing");
        }
    }
}

/* Follow a discriminator path inside one repeat.  The paths profiles use are
   dotted element names, optionally ending in `.resolve()` or `$this`. */
static jval *disc_value(jval *item, const char *dpath) {
    if (!dpath || !*dpath || str_eq(dpath, "$this")) return item;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", dpath);
    jval *cur = item;
    for (char *t = strtok(buf, "."); t && cur; t = strtok(NULL, ".")) {
        if (str_eq(t, "$this") || str_eq(t, "resolve()")) continue;
        if (!j_is(cur, J_OBJ)) return NULL;
        jval *nxt = j_get(cur, t);
        if (!nxt) {
            /* a choice element named by its base name */
            size_t tl = strlen(t);
            nxt = NULL;
            for (jmem *m = cur->u.obj.first; m && !nxt; m = m->next)
                if (!strncmp(m->key, t, tl) && isupper((unsigned char)m->key[tl])) nxt = m->val;
        }
        cur = nxt;
        if (j_is(cur, J_ARR) && j_len(cur) > 0) cur = j_at(cur, 0);
    }
    return cur;
}

static int discriminator_matches(vctx *c, edef *slice, jval *item) {
    edef *host = slice->parent;
    jval *slicing = NULL;
    for (edef *e = host ? host->first_child : NULL; e; e = e->next)
        if (e->slicing && str_eq(e->tail, slice->tail)) { slicing = e->slicing; break; }
    if (!slicing) slicing = slice->parent ? NULL : NULL;
    jval *discs = j_get(slicing, "discriminator");
    if (!discs || j_len(discs) == 0) return 0;
    for (int i = 0; i < j_len(discs); i++) {
        jval *dd = j_at(discs, i);
        const char *dtype = j_gets(dd, "type");
        const char *dpath = j_gets(dd, "path");
        jval *got = disc_value(item, dpath);
        edef *target = dpath && *dpath && !str_eq(dpath, "$this")
                       ? sd_path_rel(slice, dpath) : slice;
        if (str_eq(dtype, "exists")) {
            int want = target && target->min > 0;
            if ((got != NULL) != want) return 0;
            continue;
        }
        if (str_eq(dtype, "value") || str_eq(dtype, "pattern")) {
            if (!target) return 0;
            jval *want = target->fixed_val ? target->fixed_val : target->pattern_val;
            if (!want) return 0;
            if (!pattern_match(want, got)) return 0;
            continue;
        }
        if (str_eq(dtype, "type")) {
            const char *t = got ? j_gets(got, "resourceType") : NULL;
            if (!t || !target || target->types.n == 0) return 0;
            int ok = 0;
            for (size_t z = 0; z < target->types.n && !ok; z++)
                ok = str_eq((const char *)target->types.v[z], t);
            if (!ok) return 0;
            continue;
        }
        if (str_eq(dtype, "profile")) {
            /* the target has to claim the profile the slice names */
            if (!target || target->target_profiles.n == 0) return 0;
            jval *meta = got ? j_get(got, "meta") : NULL;
            jval *prof = j_get(meta, "profile");
            int ok = 0;
            for (int z = 0; z < j_len(prof) && !ok; z++)
                for (size_t y = 0; y < target->target_profiles.n && !ok; y++)
                    ok = str_eq(j_str_of(j_at(prof, z)),
                                (const char *)target->target_profiles.v[y]);
            if (!ok) return 0;
            continue;
        }
        return 0;
    }
    (void)c;
    return 1;
}

static void check_cardinality(vctx *c, jval *obj, edef *parent, const char *path) {
    for (edef *e = parent->first_child; e; e = e->next) {
        if (str_eq(e->tail, "id") || str_eq(e->tail, "extension") || str_eq(e->tail, "modifierExtension")) {
            if (e->min == 0) continue;
        }
        int present = 0, count = 0;
        size_t bl = strlen(e->tail);
        if (bl > 3 && strcmp(e->tail + bl - 3, "[x]") == 0) {
            for (jmem *m = obj->u.obj.first; m; m = m->next) {
                if (ed_choice_type(e, m->key)) { present = 1; count = j_is(m->val, J_ARR) ? j_len(m->val) : 1; }
            }
        } else {
            jval *m = j_get(obj, e->tail);
            if (m) { present = 1; count = j_is(m, J_ARR) ? j_len(m) : 1; }
        }
        if (e->min > 0 && (!present || count < e->min))
            vres_add(c->out, "error", "required", arena_printf(c->a, "%s.%s", path, e->tail),
                     "this element is required by its StructureDefinition but is missing");
        if (present && e->max && !str_eq(e->max, "*")) {
            int mx = atoi(e->max);
            if (mx == 0)
                vres_add(c->out, "error", "structure", arena_printf(c->a, "%s.%s", path, e->tail),
                         "this element is prohibited (max = 0)");
            else if (count > mx)
                vres_add(c->out, "error", "structure", arena_printf(c->a, "%s.%s", path, e->tail),
                         "this element occurs more often than its maximum cardinality allows");
        }
    }
}

static void check_fixed(vctx *c, jval *v, edef *e, const char *path) {
    if (e->fixed_val && !j_equal(e->fixed_val, v))
        vres_add(c->out, "error", "value", path, "the value does not match the fixed value in the profile");
    if (e->pattern_val) {
        /* pattern: every property present in the pattern must be present and equal */
        if (j_is(e->pattern_val, J_OBJ) && j_is(v, J_OBJ)) {
            for (jmem *m = e->pattern_val->u.obj.first; m; m = m->next) {
                jval *got = j_get(v, m->key);
                int ok = 0;
                if (got && j_is(got, J_ARR) && j_is(m->val, J_ARR)) {
                    ok = 1;
                    for (int i = 0; i < j_len(m->val); i++) {
                        int found = 0;
                        for (int k = 0; k < j_len(got); k++) if (j_equal(j_at(m->val, i), j_at(got, k))) found = 1;
                        if (!found) ok = 0;
                    }
                } else if (got && j_is(got, J_ARR)) {
                    for (int k = 0; k < j_len(got); k++) if (j_equal(m->val, j_at(got, k))) ok = 1;
                } else ok = got && j_equal(m->val, got);
                if (!ok) {
                    vres_add(c->out, "error", "value", path, "the value does not match the pattern in the profile");
                    break;
                }
            }
        } else if (!j_equal(e->pattern_val, v)) {
            vres_add(c->out, "error", "value", path, "the value does not match the pattern in the profile");
        }
    }
}

static void walk_value(vctx *c, jval *v, sdef *owner, edef *e, const char *type, const char *path) {
    if (c->depth > 60) return;
    check_fixed(c, v, e, path);
    if (type && is_primitive(c->d, type)) { check_primitive(c, v, type, path); return; }
    if (type && str_eq(type, "Resource")) {
        const char *rt = j_is(v, J_OBJ) ? j_gets(v, "resourceType") : NULL;
        if (!rt) { vres_add(c->out, "error", "structure", path, "a contained resource must carry resourceType"); return; }
        sdef *rs = defs_resource(c->d, rt);
        if (!rs) { vres_add(c->out, "error", "structure", path, "unknown resource type"); return; }
        c->depth++;
        walk(c, v, rs, rs->root, path);
        c->depth--;
        return;
    }
    if (!j_is(v, J_OBJ)) {
        vres_add(c->out, "error", "structure", path, "expected an object for a complex element");
        return;
    }
    edef *anchor = NULL;
    sdef *asd = anchor_for(c, owner, e, type, &anchor);
    if (!anchor) return;
    c->depth++;
    walk(c, v, asd ? asd : owner, anchor, path);
    c->depth--;
}

static void walk(vctx *c, jval *obj, sdef *s, edef *parent, const char *path) {
    if (!j_is(obj, J_OBJ) || !parent) return;
    check_cardinality(c, obj, parent, path);
    for (jmem *m = obj->u.obj.first; m; m = m->next) {
        const char *key = m->key;
        if (str_eq(key, "resourceType")) continue;
        /* DSTU2 `json.html` defines `fhir_comments` as a JSON-only property
           carrying the comments the XML form keeps as XML comments.  It is
           legal wherever an element may appear in 1.0.2 and was dropped from
           STU3 onwards. */
        if (str_eq(key, "fhir_comments") && c->d && str_eq(c->d->ver, "1.0.2")) continue;
        if (key[0] == '_') {
            const char *base = key + 1;
            const char *t2 = NULL;
            edef *pe = find_child(c, s, parent, base, &t2);
            if (!pe)
                vres_add(c->out, "error", "structure", arena_printf(c->a, "%s.%s", path, key),
                         "this element is not defined in the StructureDefinition");
            continue;
        }
        const char *type = NULL;
        edef *e = find_child(c, s, parent, key, &type);
        if (!e) {
            vres_add(c->out, "error", "structure", arena_printf(c->a, "%s.%s", path, key),
                     "this element is not defined in the StructureDefinition");
            continue;
        }
        if (j_is(m->val, J_NULL)) {
            vres_add(c->out, "error", "structure", arena_printf(c->a, "%s.%s", path, key),
                     "null is not a legal value in the FHIR JSON representation");
            continue;
        }
        /* What a profile adds over the base definitions applies to the element
           as a whole -- its slices -- and to each value in turn. */
        check_slicing(c, e, m->val, arena_printf(c->a, "%s.%s", path, key));
        int repeats = e->max && (str_eq(e->max, "*") || atoi(e->max) > 1);
        if (j_is(m->val, J_ARR)) {
            if (!repeats) {
                vres_add(c->out, "error", "structure", arena_printf(c->a, "%s.%s", path, key),
                         "this element does not repeat, so it may not be an array");
                continue;
            }
            if (j_len(m->val) == 0)
                vres_add(c->out, "error", "structure", arena_printf(c->a, "%s.%s", path, key),
                         "an array in the FHIR JSON representation may not be empty");
            for (int i = 0; i < j_len(m->val); i++) {
                jval *item = j_at(m->val, i);
                if (j_is(item, J_NULL)) continue;    /* primitive-extension padding */
                check_constraints(c, e, item, arena_printf(c->a, "%s.%s[%d]", path, key, i));
                walk_value(c, item, s, e, type, arena_printf(c->a, "%s.%s[%d]", path, key, i));
            }
        } else {
            if (repeats) {
                vres_add(c->out, "error", "structure", arena_printf(c->a, "%s.%s", path, key),
                         "this element repeats, so it must be an array");
                continue;
            }
            check_constraints(c, e, m->val, arena_printf(c->a, "%s.%s", path, key));
            walk_value(c, m->val, s, e, type, arena_printf(c->a, "%s.%s", path, key));
        }
    }
}

/* `validation.html`: an instance is validated against the base resource and
   against every profile it is asked to conform to -- the `profile` parameter of
   `$validate` and the profiles its own `meta.profile` claims.  A profile's
   snapshot has the same element paths as the base with its own constraints, so
   the same walk over the profile's definitions applies every narrowing it
   states. */
static void walk_profile(arena *a, fhirdefs *d, jval *res, const char *canonical,
                         const char *rt, vresult *out, int required) {
    if (!canonical || !*canonical) return;
    sdef *p = defs_profile(d, canonical);
    if (!p) {
        if (required)
            vres_add(out, "error", "not-supported", rt,
                     arena_printf(a, "this server does not hold the profile %s", canonical));
        return;
    }
    if (p->type && rt && !str_eq(p->type, rt)) {
        vres_add(out, "error", "invalid", rt,
                 arena_printf(a, "the profile %s constrains %s, not %s", canonical, p->type, rt));
        return;
    }
    if (!p->root) return;
    vctx c = { a, d, out, 0, 1 };
    walk(&c, res, p, p->root, rt);
}

void validate_resource(arena *a, fhirdefs *d, jval *res, const char *profile, vresult *out) {
    if (!j_is(res, J_OBJ)) {
        vres_add(out, "error", "structure", "", "the body is not a JSON object");
        return;
    }
    const char *rt = j_gets(res, "resourceType");
    if (!rt) {
        vres_add(out, "error", "structure", "", "resourceType is missing");
        return;
    }
    sdef *s = defs_resource(d, rt);
    if (!s) {
        vres_add(out, "error", "not-supported", rt, "this resource type is not defined in this FHIR version");
        return;
    }
    const char *id = j_gets(res, "id");
    if (id && !valid_id(id))
        vres_add(out, "error", "value", arena_printf(a, "%s.id", rt), "the logical id does not match the id format");
    vctx c = { a, d, out, 0, 0 };
    walk(&c, res, s, s->root, rt);
    if (profile && *profile)
        walk_profile(a, d, res, profile, rt, out, 1);
    jval *claimed = j_get(j_get(res, "meta"), "profile");
    for (int i = 0; i < j_len(claimed); i++) {
        const char *u = j_str_of(j_at(claimed, i));
        if (!u || (profile && str_eq(u, profile))) continue;
        /* a claimed profile this server does not hold is not an error: the
           instance may conform to a guide this deployment has not installed */
        walk_profile(a, d, res, u, rt, out, 0);
    }
}
