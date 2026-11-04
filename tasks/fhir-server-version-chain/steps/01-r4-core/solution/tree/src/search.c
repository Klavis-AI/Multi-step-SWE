#include "search.h"
#include "fpath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ---------------- date ranges ---------------- */

static long long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + (long long)doe - 719468LL;
}

static int dim(int y, int m) {
    static const int t[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return (m >= 1 && m <= 12) ? t[m-1] : 30;
}

/* Parse a FHIR date/dateTime/instant into [lo,hi) microseconds since epoch. */
static int parse_dt(const char *s, long long *lo, long long *hi) {
    if (!s || !*s) return 0;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    long long frac_us = 0;
    int prec = 0;        /* 1=y 2=m 3=d 4=h 5=min 6=sec 7=frac */
    int off_min = 0, have_off = 0;
    const char *p = s;
    if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]) ||
        !isdigit((unsigned char)p[2]) || !isdigit((unsigned char)p[3])) return 0;
    y = (p[0]-'0')*1000 + (p[1]-'0')*100 + (p[2]-'0')*10 + (p[3]-'0');
    p += 4; prec = 1;
    if (*p == '-' && isdigit((unsigned char)p[1])) {
        mo = (p[1]-'0')*10 + (p[2]-'0'); p += 3; prec = 2;
        if (*p == '-' && isdigit((unsigned char)p[1])) {
            d = (p[1]-'0')*10 + (p[2]-'0'); p += 3; prec = 3;
            if (*p == 'T') {
                p++;
                if (!isdigit((unsigned char)p[0])) return 0;
                h = (p[0]-'0')*10 + (p[1]-'0'); p += 2; prec = 4;
                if (*p == ':') { mi = (p[1]-'0')*10 + (p[2]-'0'); p += 3; prec = 5; }
                if (*p == ':') { se = (p[1]-'0')*10 + (p[2]-'0'); p += 3; prec = 6; }
                if (*p == '.') {
                    p++;
                    long long mul = 100000;
                    while (isdigit((unsigned char)*p)) { frac_us += (*p - '0') * mul; mul /= 10; p++; if (mul == 0) { while (isdigit((unsigned char)*p)) p++; break; } }
                    prec = 7;
                }
                if (*p == 'Z') { have_off = 1; off_min = 0; p++; }
                else if (*p == '+' || *p == '-') {
                    int sign = (*p == '-') ? -1 : 1;
                    int oh = (p[1]-'0')*10 + (p[2]-'0');
                    int om = (p[4]-'0')*10 + (p[5]-'0');
                    off_min = sign * (oh * 60 + om);
                    have_off = 1;
                    p += 6;
                }
            }
        }
    }
    if (mo == 0) mo = 1;
    if (d == 0) d = 1;
    long long base = days_from_civil(y, mo, d) * 86400000000LL
                   + (long long)h * 3600000000LL + (long long)mi * 60000000LL
                   + (long long)se * 1000000LL + frac_us;
    if (have_off) base -= (long long)off_min * 60000000LL;
    long long span;
    switch (prec) {
        case 1: span = (long long)(days_from_civil(y + 1, 1, 1) - days_from_civil(y, 1, 1)) * 86400000000LL; break;
        case 2: span = (long long)dim(y, mo) * 86400000000LL; break;
        case 3: span = 86400000000LL; break;
        case 4: span = 3600000000LL; break;
        case 5: span = 60000000LL; break;
        case 6: span = 1000000LL; break;
        default: span = 1; break;
    }
    if (prec <= 3) {
        /* date-only values have no timezone: treat as UTC, matching the common
           server behaviour the spec permits. */
    }
    *lo = base;
    *hi = base + span;
    return 1;
}

long long fhir_date_lo(const char *s) { long long a = 0, b = 0; parse_dt(s, &a, &b); return a; }
long long fhir_date_hi(const char *s) { long long a = 0, b = 0; parse_dt(s, &a, &b); return b; }

/* ---------------- helpers ---------------- */

static char *norm_string(arena *a, const char *s) {
    /* case- and accent-insensitive comparison key (Latin-1 folding). */
    if (!s) return arena_strdup(a, "");
    buf b; buf_init(&b);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned cp = *p;
        if (cp < 0x80) { buf_putc(&b, (char)tolower((int)cp)); continue; }
        if ((cp & 0xE0) == 0xC0 && p[1]) {
            unsigned c = ((cp & 0x1F) << 6) | (p[1] & 0x3F);
            p++;
            static const char *folds =
                "AAAAAA" "C" "EEEE" "IIII" "D" "N" "OOOOO" "O" "UUUU" "Y" "s";
            (void)folds;
            if (c >= 0xC0 && c <= 0xC5) c = 'a';
            else if (c == 0xC7) c = 'c';
            else if (c >= 0xC8 && c <= 0xCB) c = 'e';
            else if (c >= 0xCC && c <= 0xCF) c = 'i';
            else if (c == 0xD1) c = 'n';
            else if ((c >= 0xD2 && c <= 0xD6) || c == 0xD8) c = 'o';
            else if (c >= 0xD9 && c <= 0xDC) c = 'u';
            else if (c == 0xDD) c = 'y';
            else if (c >= 0xE0 && c <= 0xE5) c = 'a';
            else if (c == 0xE7) c = 'c';
            else if (c >= 0xE8 && c <= 0xEB) c = 'e';
            else if (c >= 0xEC && c <= 0xEF) c = 'i';
            else if (c == 0xF1) c = 'n';
            else if ((c >= 0xF2 && c <= 0xF6) || c == 0xF8) c = 'o';
            else if (c >= 0xF9 && c <= 0xFC) c = 'u';
            else if (c == 0xFD || c == 0xFF) c = 'y';
            if (c < 0x80) buf_putc(&b, (char)tolower((int)c));
            continue;
        }
        buf_putc(&b, (char)cp);
    }
    char *out = arena_strndup(a, b.p ? b.p : "", b.n);
    buf_free(&b);
    return out;
}

static const char *prim_text(jval *v) {
    if (j_is(v, J_STR)) return j_str_of(v);
    if (j_is(v, J_NUM)) return j_num_of(v);
    if (j_is(v, J_BOOL)) return v->u.b ? "true" : "false";
    return NULL;
}

/* ---------------- query parsing ---------------- */

static const char *known_prefix(const char *v, const char **rest) {
    static const char *pf[] = {"eq","ne","gt","lt","ge","le","sa","eb","ap",NULL};
    for (int i = 0; pf[i]; i++)
        if (strncmp(v, pf[i], 2) == 0 && (isdigit((unsigned char)v[2]) || v[2] == '-' || v[2] == '+')) {
            *rest = v + 2;
            return pf[i];
        }
    *rest = v;
    return NULL;
}

static void split_bars(arena *a, sval *sv) {
    const char *s = sv->raw;
    const char *b1 = strchr(s, '|');
    if (!b1) { sv->v1 = s; return; }
    sv->has_bar = 1;
    sv->v1 = arena_strndup(a, s, (size_t)(b1 - s));
    const char *b2 = strchr(b1 + 1, '|');
    if (!b2) { sv->v2 = arena_strdup(a, b1 + 1); return; }
    sv->v2 = arena_strndup(a, b1 + 1, (size_t)(b2 - b1 - 1));
    sv->v3 = arena_strdup(a, b2 + 1);
}

static void add_values(arena *a, sparamq *p, char *value) {
    size_t cap = 4;
    p->vals = arena_calloc(a, cap * sizeof(sval));
    p->nval = 0;
    char *s = value;
    while (s) {
        char *comma = s;
        for (;;) {
            comma = strchr(comma, ',');
            if (!comma) break;
            if (comma > s && comma[-1] == '\\') { comma++; continue; }
            break;
        }
        if (comma) *comma = 0;
        if (p->nval == cap) {
            sval *nv = arena_calloc(a, cap * 2 * sizeof(sval));
            memcpy(nv, p->vals, cap * sizeof(sval));
            p->vals = nv; cap *= 2;
        }
        sval *sv = &p->vals[p->nval++];
        memset(sv, 0, sizeof *sv);
        const char *rest = NULL;
        sv->prefix = known_prefix(s, &rest);
        sv->raw = arena_strdup(a, rest);
        split_bars(a, sv);
        s = comma ? comma + 1 : NULL;
    }
}

int sq_parse(arena *a, fhirdefs *d, const char *type, const char *qs, squery *out) {
    memset(out, 0, sizeof *out);
    out->count = -1;
    out->offset = 0;
    vec_init(&out->includes); vec_init(&out->revincludes); vec_init(&out->elements); vec_init(&out->types);
    size_t cap = 16;
    out->params = arena_calloc(a, cap * sizeof(sparamq));
    char *q = arena_strdup(a, qs ? qs : "");
    char *tok = q;
    while (tok && *tok) {
        char *amp = strchr(tok, '&');
        if (amp) *amp = 0;
        char *eq = strchr(tok, '=');
        char *name = tok, *value = eq ? eq + 1 : (char *)"";
        if (eq) *eq = 0;
        url_decode(name);
        value = arena_strdup(a, value);
        url_decode(value);
        if (*name) {
            if (str_eq(name, "_count")) out->count = strtol(value, NULL, 10);
            else if (str_eq(name, "_offset")) out->offset = strtol(value, NULL, 10);
            else if (str_eq(name, "_sort")) out->sort = arena_strdup(a, value);
            else if (str_eq(name, "_include")) vec_push(&out->includes, arena_strdup(a, value));
            else if (str_starts(name, "_include:")) vec_push(&out->includes, arena_printf(a, "%s%s", value, str_ends(name, ":iterate") ? "\x01iterate" : ""));
            else if (str_eq(name, "_revinclude")) vec_push(&out->revincludes, arena_strdup(a, value));
            else if (str_starts(name, "_revinclude:")) vec_push(&out->revincludes, arena_printf(a, "%s%s", value, str_ends(name, ":iterate") ? "\x01iterate" : ""));
            else if (str_eq(name, "_summary")) out->summary = arena_strdup(a, value);
            else if (str_eq(name, "_total")) out->total_mode = arena_strdup(a, value);
            else if (str_eq(name, "_format")) out->format = arena_strdup(a, value);
            else if (str_eq(name, "_pretty")) { /* display only */ }
            else if (str_eq(name, "_elements")) {
                char *v = arena_strdup(a, value);
                for (char *s2 = strtok(v, ","); s2; s2 = strtok(NULL, ",")) vec_push(&out->elements, arena_strdup(a, s2));
            } else if (str_eq(name, "_type")) {
                char *v = arena_strdup(a, value);
                for (char *s2 = strtok(v, ","); s2; s2 = strtok(NULL, ",")) vec_push(&out->types, arena_strdup(a, s2));
            } else {
                if (out->nparam == cap) {
                    sparamq *np = arena_calloc(a, cap * 2 * sizeof(sparamq));
                    memcpy(np, out->params, cap * sizeof(sparamq));
                    out->params = np; cap *= 2;
                }
                sparamq *p = &out->params[out->nparam];
                memset(p, 0, sizeof *p);
                char *nm = arena_strdup(a, name);
                if (str_starts(nm, "_has:")) {
                    char *r = nm + 5;
                    char *c1 = strchr(r, ':');
                    if (c1) { *c1 = 0; char *c2 = strchr(c1 + 1, ':');
                        if (c2) { *c2 = 0; p->has_type = r; p->has_ref = c1 + 1; p->has_code = c2 + 1; } }
                    if (!p->has_type) { out->bad = 1; out->bad_detail = arena_strdup(a, name); }
                    p->name = nm;
                } else {
                    char *colon = strchr(nm, ':');
                    char *dot = strchr(nm, '.');
                    if (dot && (!colon || dot < colon)) { *dot = 0; p->chain = dot + 1; }
                    if (colon) {
                        *colon = 0;
                        char *mod = colon + 1;
                        char *dot2 = strchr(mod, '.');
                        if (dot2) { *dot2 = 0; p->chain = dot2 + 1; p->chain_type = mod; }
                        else p->modifier = mod;
                    }
                    p->name = nm;
                    if (type && !str_starts(nm, "_")) {
                        p->sp = defs_sp(d, type, nm);
                        if (!p->sp) { out->bad = 1; out->bad_detail = arena_strdup(a, nm); }
                    } else if (type) {
                        p->sp = defs_sp(d, "Resource", nm);
                        if (!p->sp && !str_eq(nm, "_id") && !str_eq(nm, "_lastUpdated") &&
                            !str_eq(nm, "_tag") && !str_eq(nm, "_profile") && !str_eq(nm, "_security") &&
                            !str_eq(nm, "_source") && !str_eq(nm, "_text") && !str_eq(nm, "_content") &&
                            !str_eq(nm, "_language") && !str_eq(nm, "_query") && !str_eq(nm, "_filter")) {
                            out->bad = 1; out->bad_detail = arena_strdup(a, nm);
                        }
                    }
                    if (p->chain_type && p->sp && p->sp->type && str_eq(p->sp->type, "reference")) {
                        /* modifier was actually a type, keep it */
                    } else if (p->chain_type && p->sp && !str_eq(p->sp->type, "reference")) {
                        out->bad = 1; out->bad_detail = arena_strdup(a, nm);
                    }
                }
                add_values(a, p, value);
                out->nparam++;
            }
        }
        tok = amp ? amp + 1 : NULL;
    }
    return out->bad ? -1 : 0;
}

/* ---------------- index value extraction ---------------- */

static void push_str(arena *a, vec *out, const char *s) { if (s) vec_push(out, (void *)arena_strdup(a, s)); }

static void token_values(arena *a, jval *v, const char *type, vec *out) {
    if (!v) return;
    if (j_is(v, J_STR) || j_is(v, J_BOOL) || j_is(v, J_NUM)) { push_str(a, out, prim_text(v)); return; }
    if (!j_is(v, J_OBJ)) return;
    if (str_eq(type, "CodeableConcept") || j_get(v, "coding")) {
        jval *cs = j_get(v, "coding");
        for (int i = 0; i < j_len(cs); i++) {
            jval *c = j_at(cs, i);
            const char *sys = j_gets(c, "system"), *code = j_gets(c, "code");
            if (code) push_str(a, out, arena_printf(a, "%s|%s", sys ? sys : "", code));
        }
        return;
    }
    if (j_get(v, "code") || str_eq(type, "Coding")) {
        const char *sys = j_gets(v, "system"), *code = j_gets(v, "code");
        if (code) push_str(a, out, arena_printf(a, "%s|%s", sys ? sys : "", code));
        return;
    }
    if (j_get(v, "value") && (j_get(v, "system") || str_eq(type, "Identifier") || str_eq(type, "ContactPoint"))) {
        const char *sys = j_gets(v, "system"), *val = j_gets(v, "value");
        if (val) push_str(a, out, arena_printf(a, "%s|%s", sys ? sys : "", val));
        return;
    }
}

static void string_values(arena *a, jval *v, vec *out) {
    if (!v) return;
    if (j_is(v, J_STR)) { push_str(a, out, j_str_of(v)); return; }
    if (!j_is(v, J_OBJ)) return;
    static const char *fields[] = {"text","family","given","prefix","suffix",
                                   "line","city","district","state","postalCode","country",NULL};
    int any = 0;
    for (int i = 0; fields[i]; i++) {
        jval *f = j_get(v, fields[i]);
        if (!f) continue;
        any = 1;
        if (j_is(f, J_ARR)) { for (int k = 0; k < j_len(f); k++) push_str(a, out, j_str_of(j_at(f, k))); }
        else push_str(a, out, j_str_of(f));
    }
    if (!any) {
        for (jmem *m = v->u.obj.first; m; m = m->next)
            if (j_is(m->val, J_STR)) push_str(a, out, j_str_of(m->val));
    }
}

static void ref_values(arena *a, jval *v, vec *out) {
    if (!v) return;
    if (j_is(v, J_STR)) { push_str(a, out, j_str_of(v)); return; }
    if (!j_is(v, J_OBJ)) return;
    const char *r = j_gets(v, "reference");
    if (r) push_str(a, out, r);
    const char *u = j_gets(v, "url");        /* canonical */
    if (u) push_str(a, out, u);
}

static void values_from(arena *a, const fpcoll *c, const char *sptype, vec *out, const vec *targets);

void sq_values(arena *a, fhirdefs *d, jval *res, sparam *sp, vec *out) {
    if (!sp || !sp->expression) return;
    fpcoll c = fp_eval(a, d, res, sp->expression);
    values_from(a, &c, sp->type, out, &sp->targets);
    fp_free(&c);
}

static void values_from(arena *a, const fpcoll *cc, const char *sptype, vec *out, const vec *targets) {
    const fpcoll c = *cc;
    const char *tp = sptype;
    struct { const char *type; } sp_ = { tp };
    (void)sp_;
    for (size_t i = 0; i < c.n; i++) {
        jval *v = c.v[i].v;
        const char *ty = c.v[i].type;
        if (str_eq(tp, "reference") && targets && targets->n) {
            /* a reference parameter matches only references to the resource
               types its SearchParameter declares as targets */
            vec tmp; vec_init(&tmp);
            ref_values(a, v, &tmp);
            for (size_t k = 0; k < tmp.n; k++) {
                const char *r = (const char *)tmp.v[k];
                const char *slash = strrchr(r, '/');
                if (!slash) { vec_push(out, (void *)r); continue; }
                char *pre = arena_strndup(a, r, (size_t)(slash - r));
                const char *ts = strrchr(pre, '/');
                const char *rtype = ts ? ts + 1 : pre;
                int ok = 0;
                for (size_t z = 0; z < targets->n; z++)
                    if (str_eq((const char *)targets->v[z], rtype)) ok = 1;
                if (ok) vec_push(out, (void *)r);
            }
            vec_free(&tmp);
            continue;
        }
        if (str_eq(tp, "token")) token_values(a, v, ty, out);
        else if (str_eq(tp, "string")) string_values(a, v, out);
        else if (str_eq(tp, "reference")) ref_values(a, v, out);
        else if (str_eq(tp, "uri")) push_str(a, out, prim_text(v));
        else if (str_eq(tp, "number")) push_str(a, out, prim_text(v));
        else if (str_eq(tp, "date")) {
            /* A date search parameter only applies to the date-valued types;
               a `scheduledString` under `scheduled[x]` is not one of them. */
            int datish = ty == NULL || str_eq(ty, "date") || str_eq(ty, "dateTime") ||
                         str_eq(ty, "instant") || str_eq(ty, "Period") || str_eq(ty, "Timing");
            if (!datish) continue;
            if (j_is(v, J_STR)) push_str(a, out, j_str_of(v));
            else if (j_is(v, J_OBJ)) {
                const char *st = j_gets(v, "start"), *en = j_gets(v, "end");
                if (st || en) push_str(a, out, arena_printf(a, "%s\x1f%s", st ? st : "", en ? en : ""));
                else {
                    jval *ev = j_get(v, "event");
                    for (int k = 0; k < j_len(ev); k++) push_str(a, out, j_str_of(j_at(ev, k)));
                    jval *rep = j_get(v, "repeat");
                    jval *bp = j_get(rep, "boundsPeriod");
                    if (bp) {
                        const char *bs = j_gets(bp, "start"), *be = j_gets(bp, "end");
                        if (bs || be) push_str(a, out, arena_printf(a, "%s\x1f%s", bs ? bs : "", be ? be : ""));
                    }
                }
            }
        } else if (str_eq(tp, "quantity")) {
            if (j_is(v, J_OBJ)) {
                const char *val = NULL;
                jval *vv = j_get(v, "value");
                if (vv) val = prim_text(vv);
                const char *sys = j_gets(v, "system");
                const char *code = j_gets(v, "code");
                if (!code) code = j_gets(v, "unit");
                if (val) push_str(a, out, arena_printf(a, "%s|%s|%s", val, sys ? sys : "", code ? code : ""));
            } else push_str(a, out, prim_text(v));
        } else {
            push_str(a, out, prim_text(v));
        }
    }
}

/* ---------------- matching ---------------- */

static int num_cmp_range(const char *val, const char *want, const char *prefix) {
    /* implicit precision range around `want` for eq/ne/ap */
    char *e1 = NULL, *e2 = NULL;
    double v = strtod(val, &e1);
    double w = strtod(want, &e2);
    if (e1 == val || e2 == want) return 0;
    const char *dot = strchr(want, '.');
    int digits = dot ? (int)strlen(dot + 1) : 0;
    double half = 0.5 * pow(10.0, -digits);
    if (!prefix || str_eq(prefix, "eq")) return v >= w - half && v < w + half;
    if (str_eq(prefix, "ne")) return !(v >= w - half && v < w + half);
    if (str_eq(prefix, "gt")) return v > w;
    if (str_eq(prefix, "lt")) return v < w;
    if (str_eq(prefix, "ge")) return v >= w;
    if (str_eq(prefix, "le")) return v <= w;
    if (str_eq(prefix, "sa")) return v > w + half;
    if (str_eq(prefix, "eb")) return v < w - half;
    if (str_eq(prefix, "ap")) { double d = fabs(w) * 0.1; if (d < half) d = half; return fabs(v - w) <= d; }
    return 0;
}

static int date_match(const char *idx, const sval *sv) {
    long long tlo, thi;
    const char *sep = strchr(idx, '\x1f');
    if (sep) {
        char *s1 = str_ndup(idx, (size_t)(sep - idx));
        const char *s2 = sep + 1;
        tlo = *s1 ? fhir_date_lo(s1) : -4000000000000000000LL;
        thi = *s2 ? fhir_date_hi(s2) : 4000000000000000000LL;
        free(s1);
    } else {
        if (!parse_dt(idx, &tlo, &thi)) return 0;
    }
    long long slo, shi;
    if (!parse_dt(sv->raw, &slo, &shi)) return 0;
    const char *p = sv->prefix ? sv->prefix : "eq";
    int overlaps = tlo < shi && slo < thi;
    int contains = slo <= tlo && thi <= shi;
    if (str_eq(p, "eq")) return contains;
    if (str_eq(p, "ne")) return !contains;
    if (str_eq(p, "gt")) return thi > shi;
    if (str_eq(p, "lt")) return tlo < slo;
    if (str_eq(p, "ge")) return thi > shi || contains;
    if (str_eq(p, "le")) return tlo < slo || contains;
    if (str_eq(p, "sa")) return tlo >= shi;
    if (str_eq(p, "eb")) return thi <= slo;
    if (str_eq(p, "ap")) return overlaps || llabs((tlo + thi) / 2 - (slo + shi) / 2) < 86400000000LL * 30;
    return 0;
}

static int token_match(arena *a, const char *idx, const sval *sv, const char *mod) {
    const char *bar = strchr(idx, '|');
    const char *isys = bar ? arena_strndup(a, idx, (size_t)(bar - idx)) : "";
    const char *icode = bar ? bar + 1 : idx;
    if (str_eq(mod, "text")) {
        char *n = norm_string(a, icode);
        char *w = norm_string(a, sv->raw);
        return strstr(n, w) != NULL;
    }
    if (sv->has_bar) {
        const char *wsys = sv->v1 ? sv->v1 : "";
        const char *wcode = sv->v2 ? sv->v2 : "";
        if (*wsys && !str_eq(wsys, isys)) return 0;
        if (!*wsys && sv->v1 && sv->v1[0] == 0 && bar && *isys) return 0;  /* |code : no system */
        if (*wcode && !str_eq(wcode, icode)) return 0;
        if (!*wcode && !*wsys) return 0;
        return 1;
    }
    return str_eq(icode, sv->raw);
}

static int string_match(arena *a, const char *idx, const sval *sv, const char *mod) {
    if (str_eq(mod, "exact")) return str_eq(idx, sv->raw);
    char *n = norm_string(a, idx);
    char *w = norm_string(a, sv->raw);
    if (str_eq(mod, "contains")) return strstr(n, w) != NULL;
    return strncmp(n, w, strlen(w)) == 0;
}

static int uri_match(const char *idx, const sval *sv, const char *mod) {
    if (str_eq(mod, "below")) return str_starts(idx, sv->raw);
    if (str_eq(mod, "above")) return str_starts(sv->raw, idx);
    return str_eq(idx, sv->raw);
}

static int ref_match(const char *idx, const sval *sv) {
    const char *want = sv->raw;
    if (str_eq(idx, want)) return 1;
    /* trailing "Type/id" comparison */
    const char *islash = strrchr(idx, '/');
    const char *wslash = strrchr(want, '/');
    if (!wslash) return islash && str_eq(islash + 1, want);
    /* compare the last two segments */
    const char *ip = idx, *wp = want;
    size_t il = strlen(idx), wl = strlen(want);
    if (il >= wl && str_eq(idx + il - wl, want) && (il == wl || idx[il - wl - 1] == '/')) return 1;
    (void)ip; (void)wp; (void)islash;
    return 0;
}

static int quantity_match(arena *a, const char *idx, const sval *sv) {
    char *copy = arena_strdup(a, idx);
    char *b1 = strchr(copy, '|');
    if (!b1) return 0;
    *b1 = 0;
    char *b2 = strchr(b1 + 1, '|');
    const char *isys = b1 + 1, *icode = "";
    if (b2) { *b2 = 0; icode = b2 + 1; }
    const char *wnum = sv->v1 ? sv->v1 : sv->raw;
    const char *wsys = sv->v2, *wcode = sv->v3;
    if (!sv->v3 && sv->v2) { wcode = sv->v2; wsys = NULL; }
    if (wsys && *wsys && !str_eq(wsys, isys)) return 0;
    if (wcode && *wcode && !str_eq(wcode, icode)) return 0;
    return num_cmp_range(copy, wnum, sv->prefix);
}

static int has_param(arena *a, fhirdefs *d, store *st, jval *res, const sparamq *p);
static int token_match(arena *a, const char *idx, const sval *sv, const char *mod);
static int string_match(arena *a, const char *idx, const sval *sv, const char *mod);
static int date_match(const char *idx, const sval *sv);
static int quantity_match(arena *a, const char *idx, const sval *sv);
static int uri_match(const char *idx, const sval *sv, const char *mod);
static int ref_match(const char *idx, const sval *sv);
static int num_cmp_range(const char *val, const char *want, const char *prefix);

/* A composite parameter names several component parameters whose expressions are
   relative to the composite's own expression; all components must match inside
   the same item. */
static int composite_match(arena *a, fhirdefs *d, jval *res, const sparamq *p) {
    sparam *sp = p->sp;
    if (!sp || sp->components.n == 0 || !sp->expression) return 0;
    fpcoll items = fp_eval(a, d, res, sp->expression);
    int hit = 0;
    for (size_t k = 0; k < p->nval && !hit; k++) {
        char *raw = arena_strdup(a, p->vals[k].raw);
        /* split on '$' into one part per component */
        char *parts[8];
        size_t np = 0;
        char *cur = raw;
        while (np < 8) {
            char *dollar = strchr(cur, '$');
            if (!dollar) { parts[np++] = cur; break; }
            *dollar = 0;
            parts[np++] = cur;
            cur = dollar + 1;
        }
        if (np != sp->components.n) continue;
        for (size_t i = 0; i < items.n && !hit; i++) {
            fpcoll one; memset(&one, 0, sizeof one);
            fpcoll tmp = fp_eval_in(a, d, &items, "");
            fp_free(&tmp);
            one.v = &items.v[i];
            one.n = 1; one.cap = 1;
            int all = 1;
            for (size_t ci = 0; ci < sp->components.n && all; ci++) {
                jval *comp = sp->components.v[ci];
                const char *cexpr = j_gets(comp, "expression");
                const char *cdef = j_gets(comp, "definition");
                sparam *cp = NULL;
                for (size_t z = 0; z < d->sparams.n && !cp; z++) {
                    sparam *q = d->sparams.v[z];
                    if (str_eq(q->url, cdef)) cp = q;
                }
                if (!cexpr || !cp) { all = 0; break; }
                fpcoll cv = fp_eval_in(a, d, &one, cexpr);
                vec vals; vec_init(&vals);
                values_from(a, &cv, cp->type, &vals, &cp->targets);
                fp_free(&cv);
                sparamq sub;
                memset(&sub, 0, sizeof sub);
                sub.name = cp->code;
                sub.sp = cp;
                sval sv;
                memset(&sv, 0, sizeof sv);
                const char *rest = NULL;
                sv.prefix = known_prefix(parts[ci], &rest);
                sv.raw = arena_strdup(a, rest);
                split_bars(a, &sv);
                int m = 0;
                for (size_t y = 0; y < vals.n && !m; y++) {
                    const char *idx = (const char *)vals.v[y];
                    if (str_eq(cp->type, "token")) m = token_match(a, idx, &sv, "");
                    else if (str_eq(cp->type, "string")) m = string_match(a, idx, &sv, "");
                    else if (str_eq(cp->type, "date")) m = date_match(idx, &sv);
                    else if (str_eq(cp->type, "quantity")) m = quantity_match(a, idx, &sv);
                    else if (str_eq(cp->type, "number")) m = num_cmp_range(idx, sv.raw, sv.prefix);
                    else if (str_eq(cp->type, "uri")) m = uri_match(idx, &sv, "");
                    else if (str_eq(cp->type, "reference")) m = ref_match(idx, &sv);
                    else m = str_eq(idx, sv.raw);
                }
                vec_free(&vals);
                if (!m) all = 0;
            }
            if (all) hit = 1;
        }
    }
    fp_free(&items);
    return hit;
}

static int match_one(arena *a, fhirdefs *d, store *st, jval *res, const sparamq *p) {
    const char *mod = p->modifier ? p->modifier : "";
    if (p->has_type) return has_param(a, d, st, res, p);
    if (p->sp && str_eq(p->sp->type, "composite") && !str_eq(mod, "missing"))
        return composite_match(a, d, res, p);

    vec vals; vec_init(&vals);
    const char *name = p->name;
    if (str_eq(name, "_id")) push_str(a, &vals, j_gets(res, "id"));
    else if (str_eq(name, "_lastUpdated")) push_str(a, &vals, j_gets(j_get(res, "meta"), "lastUpdated"));
    else if (str_eq(name, "_profile")) {
        jval *m = j_get(j_get(res, "meta"), "profile");
        for (int i = 0; i < j_len(m); i++) push_str(a, &vals, j_str_of(j_at(m, i)));
    } else if (str_eq(name, "_tag") || str_eq(name, "_security")) {
        jval *m = j_get(j_get(res, "meta"), str_eq(name, "_tag") ? "tag" : "security");
        for (int i = 0; i < j_len(m); i++) {
            jval *c = j_at(m, i);
            const char *sys = j_gets(c, "system"), *code = j_gets(c, "code");
            if (code) push_str(a, &vals, arena_printf(a, "%s|%s", sys ? sys : "", code));
        }
    } else if (str_eq(name, "_source")) push_str(a, &vals, j_gets(j_get(res, "meta"), "source"));
    else if (p->sp) sq_values(a, d, res, p->sp, &vals);

    const char *ptype = p->sp ? p->sp->type : (str_eq(name, "_lastUpdated") ? "date" :
                        (str_eq(name, "_profile") || str_eq(name, "_source")) ? "uri" : "token");

    if (str_eq(mod, "missing")) {
        int want_missing = str_eq(p->vals[0].raw, "true");
        int missing = vals.n == 0;
        vec_free(&vals);
        return want_missing == missing;
    }

    /* chained reference */
    if (p->chain && p->sp && str_eq(p->sp->type, "reference")) {
        int hit = 0;
        for (size_t i = 0; i < vals.n && !hit; i++) {
            const char *ref = (const char *)vals.v[i];
            const char *slash = strrchr(ref, '/');
            if (!slash) continue;
            char *id = arena_strdup(a, slash + 1);
            char *tmp = arena_strndup(a, ref, (size_t)(slash - ref));
            const char *tslash = strrchr(tmp, '/');
            const char *rtype = tslash ? tslash + 1 : tmp;
            if (p->chain_type && !str_eq(p->chain_type, rtype)) continue;
            rec *r = store_get(st, rtype, id);
            if (!r || !r->cur || r->cur->deleted) continue;
            squery sub;
            char qs[1024];
            buf qb; buf_init(&qb);
            buf_puts(&qb, p->chain);
            buf_putc(&qb, '=');
            for (size_t k = 0; k < p->nval; k++) { if (k) buf_putc(&qb, ','); buf_puts(&qb, p->vals[k].raw); }
            snprintf(qs, sizeof qs, "%s", qb.p ? qb.p : "");
            buf_free(&qb);
            if (sq_parse(a, d, rtype, qs, &sub) == 0 && sq_match(a, d, st, r->cur->res, &sub)) hit = 1;
        }
        vec_free(&vals);
        return hit;
    }

    int neg = str_eq(mod, "not");
    int any = 0;
    for (size_t k = 0; k < p->nval && !any; k++) {
        const sval *sv = &p->vals[k];
        for (size_t i = 0; i < vals.n; i++) {
            const char *idx = (const char *)vals.v[i];
            int m = 0;
            if (str_eq(ptype, "token")) m = token_match(a, idx, sv, mod);
            else if (str_eq(ptype, "string")) m = string_match(a, idx, sv, mod);
            else if (str_eq(ptype, "date")) m = date_match(idx, sv);
            else if (str_eq(ptype, "reference")) {
                if (str_eq(mod, "identifier")) m = 0;
                else m = ref_match(idx, sv);
            }
            else if (str_eq(ptype, "uri")) m = uri_match(idx, sv, mod);
            else if (str_eq(ptype, "number")) m = num_cmp_range(idx, sv->raw, sv->prefix);
            else if (str_eq(ptype, "quantity")) m = quantity_match(a, idx, sv);
            else m = str_eq(idx, sv->raw);
            if (m) { any = 1; break; }
        }
    }
    vec_free(&vals);
    return neg ? !any : any;
}

static int has_param(arena *a, fhirdefs *d, store *st, jval *res, const sparamq *p) {
    const char *rt = j_gets(res, "resourceType");
    const char *id = j_gets(res, "id");
    if (!rt || !id) return 0;
    vec all; vec_init(&all);
    store_scan(st, p->has_type, &all);
    char target[512];
    snprintf(target, sizeof target, "%s/%s", rt, id);
    int hit = 0;
    for (size_t i = 0; i < all.n && !hit; i++) {
        rec *r = all.v[i];
        if (!r->cur || r->cur->deleted) continue;
        sparam *refsp = defs_sp(d, p->has_type, p->has_ref);
        if (!refsp) continue;
        vec rv; vec_init(&rv);
        sq_values(a, d, r->cur->res, refsp, &rv);
        int points = 0;
        for (size_t k = 0; k < rv.n; k++) {
            const char *ref = (const char *)rv.v[k];
            size_t rl = strlen(ref), tl = strlen(target);
            if (rl >= tl && str_eq(ref + rl - tl, target) && (rl == tl || ref[rl - tl - 1] == '/')) { points = 1; break; }
        }
        vec_free(&rv);
        if (!points) continue;
        buf qb; buf_init(&qb);
        buf_puts(&qb, p->has_code); buf_putc(&qb, '=');
        for (size_t k = 0; k < p->nval; k++) { if (k) buf_putc(&qb, ','); buf_puts(&qb, p->vals[k].raw); }
        squery sub;
        if (sq_parse(a, d, p->has_type, qb.p ? qb.p : "", &sub) == 0 && sq_match(a, d, st, r->cur->res, &sub)) hit = 1;
        buf_free(&qb);
    }
    vec_free(&all);
    return hit;
}

int sq_match(arena *a, fhirdefs *d, store *st, jval *res, const squery *q) {
    for (size_t i = 0; i < q->nparam; i++)
        if (!match_one(a, d, st, res, &q->params[i])) return 0;
    return 1;
}

char *sq_sortkey(arena *a, fhirdefs *d, jval *res, const char *code) {
    const char *rt = j_gets(res, "resourceType");
    vec vals; vec_init(&vals);
    if (str_eq(code, "_id")) push_str(a, &vals, j_gets(res, "id"));
    else if (str_eq(code, "_lastUpdated")) push_str(a, &vals, j_gets(j_get(res, "meta"), "lastUpdated"));
    else {
        sparam *sp = defs_sp(d, rt, code);
        if (sp) sq_values(a, d, res, sp, &vals);
    }
    const char *best = NULL;
    for (size_t i = 0; i < vals.n; i++) {
        const char *s = (const char *)vals.v[i];
        if (!best || strcmp(s, best) < 0) best = s;
    }
    char *out = arena_strdup(a, best ? best : "\xff\xff");
    vec_free(&vals);
    return out;
}
