#include <ctype.h>
#include "server.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static jval *code_obj(arena *a, const char *code) { jval *o = j_obj(a); j_set(a, o, "code", j_str(a, code)); return o; }

jval *capability_statement(arena *a, server *s, fhirbase *b) {
    fhirdefs *d = b->defs;
    int dstu2 = str_eq(d->code, "DSTU2");
    jval *cs = j_obj(a);
    j_set(a, cs, "resourceType", j_str(a, dstu2 ? "Conformance" : "CapabilityStatement"));
    j_set(a, cs, "id", j_str(a, arena_printf(a, "%s-capability", b->base_path)));
    j_set(a, cs, "url", j_str(a, arena_printf(a, "%s/%s/metadata", s->base_url, b->base_path)));
    j_set(a, cs, "status", j_str(a, "active"));
    j_set(a, cs, "date", j_str(a, "2026-01-01T00:00:00+00:00"));
    jval *sw = j_obj(a);
    j_set(a, sw, "name", j_str(a, "blaze"));
    j_set(a, sw, "version", j_str(a, "1.0.0"));
    j_set(a, cs, "software", sw);
    jval *impl = j_obj(a);
    j_set(a, impl, "description", j_str(a, "FHIR server"));
    j_set(a, impl, "url", j_str(a, arena_printf(a, "%s/%s", s->base_url, b->base_path)));
    j_set(a, cs, "implementation", impl);
    j_set(a, cs, "kind", j_str(a, "instance"));
    j_set(a, cs, "fhirVersion", j_str(a, d->ver));
    /* `acceptUnknown` is required in DSTU2 and STU3 (min 1) and was removed in
       R4. */
    if (dstu2 || str_eq(d->code, "STU3")) {
        j_set(a, cs, "acceptUnknown", j_str(a, "both"));
    }
    jval *fmt = j_arr(a);
    j_push(a, fmt, j_str(a, "application/fhir+json"));
    j_push(a, fmt, j_str(a, "application/fhir+xml"));
    if (dstu2) {
        fmt = j_arr(a);
        j_push(a, fmt, j_str(a, "application/json+fhir"));
        j_push(a, fmt, j_str(a, "application/xml+fhir"));
    }
    j_set(a, cs, "format", fmt);

    jval *rest = j_arr(a);
    jval *r = j_obj(a);
    j_set(a, r, "mode", j_str(a, "server"));
    jval *resources = j_arr(a);
    for (size_t i = 0; i < d->resources.n; i++) {
        sdef *sd = d->resources.v[i];
        if (str_eq(sd->type, "Bundle")) { /* still served, keep it listed */ }
        jval *e = j_obj(a);
        j_set(a, e, "type", j_str(a, sd->type));
        /* `rest.resource.profile` is a Reference until R4 makes it a canonical. */
        if (dstu2 || str_eq(d->code, "STU3")) {
            jval *pr = j_obj(a); j_set(a, pr, "reference", j_str(a, sd->url));
            j_set(a, e, "profile", pr);
        } else {
            j_set(a, e, "profile", j_str(a, sd->url));
        }
        jval *ints = j_arr(a);
        static const char *codes[] = {"read","vread","update","delete","history-instance","history-type","create","search-type",NULL};
        for (int k = 0; codes[k]; k++) j_push(a, ints, code_obj(a, codes[k]));
        j_set(a, e, "interaction", ints);
        j_set(a, e, "versioning", j_str(a, "versioned"));
        j_set(a, e, "readHistory", j_bool(a, 1));
        j_set(a, e, "updateCreate", j_bool(a, 1));
        j_set(a, e, "conditionalCreate", j_bool(a, 1));
        j_set(a, e, "conditionalUpdate", j_bool(a, 1));
        j_set(a, e, "conditionalDelete", j_str(a, "multiple"));
        jval *sps = j_arr(a);
        smap seen_names;
        smap_init(&seen_names, 256);
        for (size_t k = 0; k < d->sparams.n; k++) {
            sparam *sp = d->sparams.v[k];
            int mine = 0;
            for (size_t z = 0; z < sp->bases.n; z++) if (str_eq((const char *)sp->bases.v[z], sd->type)) mine = 1;
            if (!mine || !sp->code || !sp->type) continue;
            /* The core packages ship example parameters whose `base` and
               `expression` disagree -- `example-extension` is declared on
               Patient and evaluates over DocumentReference -- and a parameter
               that cannot be evaluated against this type is not supported. */
            if (sp->expression && isupper((unsigned char)sp->expression[0])) {
                size_t tl = strlen(sd->type);
                if (strncmp(sp->expression, sd->type, tl) ||
                    (sp->expression[tl] && sp->expression[tl] != '.' &&
                     sp->expression[tl] != '(' && sp->expression[tl] != ' ')) {
                    int elsewhere = 0;
                    /* a union expression may name this type in a later term */
                    for (const char *q = sp->expression; (q = strstr(q, sd->type)); q += tl) {
                        if (q == sp->expression || q[-1] == '|' || q[-1] == ' ' ||
                            q[-1] == '(') { elsewhere = 1; break; }
                    }
                    if (!elsewhere) continue;
                }
            }
            /* cpb-12: search parameter names are unique within a resource */
            if (smap_get(&seen_names, sp->code)) continue;
            smap_put(&seen_names, sp->code, sp);
            jval *spj = j_obj(a);
            j_set(a, spj, "name", j_str(a, sp->code));
            if (!dstu2) j_set(a, spj, "definition", j_str(a, sp->url));
            else j_set(a, spj, "definition", j_str(a, sp->url));
            j_set(a, spj, "type", j_str(a, sp->type));
            j_push(a, sps, spj);
        }
        /* `capabilitystatement.html`: the parameters every resource supports are
           listed on each resource, not only once at the server level. */
        static const struct { const char *name, *type; } COMMON[] = {
            {"_id", "token"}, {"_lastUpdated", "date"}, {"_tag", "token"},
            {"_profile", "uri"}, {"_security", "token"}, {"_source", "uri"},
            {"_has", "string"}, {"_filter", "string"},
            {NULL, NULL}};
        /* `_text` and `_content` search the narrative and the whole resource as
           text; this server has no full-text index and does not claim them. */
        for (int ci = 0; COMMON[ci].name; ci++) {
            if (smap_get(&seen_names, COMMON[ci].name)) continue;
            if (dstu2 && (str_eq(COMMON[ci].name, "_source") ||
                          str_eq(COMMON[ci].name, "_has"))) continue;
            smap_put(&seen_names, COMMON[ci].name, (void *)COMMON);
            jval *spj = j_obj(a);
            j_set(a, spj, "name", j_str(a, COMMON[ci].name));
            j_set(a, spj, "type", j_str(a, COMMON[ci].type));
            j_push(a, sps, spj);
        }
        smap_free(&seen_names);
        j_set(a, e, "searchParam", sps);
        j_push(a, resources, e);
    }
    j_set(a, r, "resource", resources);
    jval *ints = j_arr(a);
    j_push(a, ints, code_obj(a, "transaction"));
    j_push(a, ints, code_obj(a, "batch"));
    j_push(a, ints, code_obj(a, "search-system"));
    j_push(a, ints, code_obj(a, "history-system"));
    j_set(a, r, "interaction", ints);
    j_push(a, rest, r);
    j_set(a, cs, "rest", rest);
    return cs;
}
