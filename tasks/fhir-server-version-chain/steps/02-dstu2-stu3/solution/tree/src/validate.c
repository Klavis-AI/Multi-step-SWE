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
                walk_value(c, item, s, e, type, arena_printf(c->a, "%s.%s[%d]", path, key, i));
            }
        } else {
            if (repeats) {
                vres_add(c->out, "error", "structure", arena_printf(c->a, "%s.%s", path, key),
                         "this element repeats, so it must be an array");
                continue;
            }
            walk_value(c, m->val, s, e, type, arena_printf(c->a, "%s.%s", path, key));
        }
    }
}

void validate_resource(arena *a, fhirdefs *d, jval *res, const char *profile, vresult *out) {
    (void)profile;
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
    vctx c = { a, d, out, 0 };
    walk(&c, res, s, s->root, rt);
}
