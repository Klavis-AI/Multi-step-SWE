/* convert.c -- rendering a resource against another release's definitions.
 *
 * `versions.html` gives three ways a FHIR version is determined; the second is
 * the `fhirVersion` parameter on the media type.  When it names a release other
 * than the base's own, the answer has to be that release's representation of
 * the resource -- which is decided by that release's StructureDefinitions and
 * nothing else: an element the target release does not define is not part of
 * its representation, and a resource type it does not define has no
 * representation at all.
 */
#include "convert.h"
#include <string.h>

static jval *walk_obj(arena *a, fhirdefs *to, sdef *s, edef *parent, jval *obj,
                      int *dropped, int depth);

static jval *walk_val(arena *a, fhirdefs *to, sdef *owner, edef *e,
                      const char *type, jval *v, int *dropped, int depth) {
    if (depth > 40 || !j_is(v, J_OBJ)) return j_clone(a, v);
    edef *anchor = NULL;
    sdef *asd = NULL;
    if (!ed_children_of(to, owner, e, type, &asd, &anchor) || !anchor)
        return j_clone(a, v);
    return walk_obj(a, to, asd ? asd : owner, anchor, v, dropped, depth + 1);
}

static jval *walk_obj(arena *a, fhirdefs *to, sdef *s, edef *parent, jval *obj,
                      int *dropped, int depth) {
    jval *out = j_obj(a);
    for (jmem *m = obj->u.obj.first; m; m = m->next) {
        const char *key = m->key;
        if (str_eq(key, "resourceType") || key[0] == '_') {
            j_set(a, out, key, j_clone(a, m->val));
            continue;
        }
        const char *type = NULL;
        edef *e = NULL;
        for (edef *k = parent ? parent->first_child : NULL; k && !e; k = k->next) {
            if (str_eq(k->tail, key)) {
                e = k;
                if (k->types.n == 1) type = (const char *)k->types.v[0];
            } else {
                const char *t = ed_choice_type(k, key);
                if (t) { e = k; type = t; }
            }
        }
        if (!e) { (*dropped)++; continue; }
        if (j_is(m->val, J_ARR)) {
            jval *arr = j_arr(a);
            for (int i = 0; i < j_len(m->val); i++)
                j_push(a, arr, walk_val(a, to, s, e, type, j_at(m->val, i),
                                        dropped, depth));
            j_set(a, out, key, arr);
        } else {
            j_set(a, out, key, walk_val(a, to, s, e, type, m->val, dropped, depth));
        }
    }
    return out;
}

jval *render_as(arena *a, fhirdefs *to, jval *res, int *dropped) {
    int d = 0;
    if (!dropped) dropped = &d;
    const char *rt = j_is(res, J_OBJ) ? j_gets(res, "resourceType") : NULL;
    if (!rt) return NULL;
    if (str_eq(rt, "Bundle") || str_eq(rt, "OperationOutcome") ||
        str_eq(rt, "Parameters")) {
        /* the envelope types are the same shape in every release this server
           serves; their contents are rendered where they are reached */
    }
    sdef *s = defs_resource(to, rt);
    if (!s) return NULL;
    return walk_obj(a, to, s, s->root, res, dropped, 0);
}

/* "4.0" -> baseR4.  The parameter carries major.minor, never the patch. */
const char *version_param_base(const char *mm) {
    if (!mm) return NULL;
    if (str_eq(mm, "1.0")) return "baseDstu2";
    if (str_eq(mm, "3.0")) return "baseDstu3";
    if (str_eq(mm, "4.0")) return "baseR4";
    if (str_eq(mm, "4.3")) return "baseR4B";
    if (str_eq(mm, "5.0")) return "baseR5";
    if (str_eq(mm, "6.0")) return "baseR6";
    return NULL;
}
