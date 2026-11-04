#include "fpath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void coll_init(fpcoll *c) { c->v = NULL; c->n = c->cap = 0; }

static void coll_pushn(fpcoll *c, jval *v, const char *type, sdef *sd, edef *node) {
    if (!v) return;
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 8;
        c->v = realloc(c->v, c->cap * sizeof(fpnode));
        if (!c->v) { fprintf(stderr, "oom\n"); exit(70); }
    }
    c->v[c->n].v = v;
    c->v[c->n].type = type;
    c->v[c->n].sd = sd;
    c->v[c->n].node = node;
    c->n++;
}

#define coll_push(c, v, t) coll_pushn((c), (v), (t), NULL, NULL)
#define coll_copy(c, n)    coll_pushn((c), (n)->v, (n)->type, (n)->sd, (n)->node)

void fp_free(fpcoll *c) { free(c->v); c->v = NULL; c->n = c->cap = 0; }

/* ---------------- lexer ---------------- */

typedef enum { T_END, T_NAME, T_STR, T_NUM, T_DOT, T_LP, T_RP, T_LB, T_RB, T_PIPE,
               T_EQ, T_NE, T_COMMA, T_OTHER } ttype;

typedef struct {
    const char *s;
    ttype       t;
    const char *text;   /* arena copy for NAME/STR */
    long        num;
    arena      *a;
} lex;

static void lx_next(lex *L) {
    const char *p = L->s;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (!*p) { L->t = T_END; L->s = p; return; }
    if (*p == '\'') {
        const char *st = ++p;
        buf b; buf_init(&b);
        while (*p && *p != '\'') {
            if (*p == '\\' && p[1]) { p++; buf_putc(&b, *p == 'n' ? '\n' : *p == 't' ? '\t' : *p); p++; }
            else buf_putc(&b, *p++);
        }
        if (*p == '\'') p++;
        L->text = arena_strndup(L->a, b.p ? b.p : "", b.n);
        buf_free(&b);
        (void)st;
        L->t = T_STR; L->s = p; return;
    }
    if (isdigit((unsigned char)*p)) {
        char *end;
        L->num = strtol(p, &end, 10);
        L->s = end; L->t = T_NUM; return;
    }
    if (isalpha((unsigned char)*p) || *p == '_' || *p == '%') {
        const char *st = p;
        while (isalnum((unsigned char)*p) || *p == '_' || *p == '%') p++;
        L->text = arena_strndup(L->a, st, (size_t)(p - st));
        L->t = T_NAME; L->s = p; return;
    }
    switch (*p) {
        case '.': L->t = T_DOT; L->s = p + 1; return;
        case '(': L->t = T_LP; L->s = p + 1; return;
        case ')': L->t = T_RP; L->s = p + 1; return;
        case '[': L->t = T_LB; L->s = p + 1; return;
        case ']': L->t = T_RB; L->s = p + 1; return;
        case '|': L->t = T_PIPE; L->s = p + 1; return;
        case ',': L->t = T_COMMA; L->s = p + 1; return;
        case '=': L->t = T_EQ; L->s = p + 1; return;
        case '!': if (p[1] == '=') { L->t = T_NE; L->s = p + 2; return; } break;
        default: break;
    }
    L->t = T_OTHER; L->s = p + 1;
}

/* ---------------- evaluation ---------------- */

typedef struct {
    arena    *a;
    fhirdefs *d;
    jval     *root;
} fctx;

static fpcoll eval_union(fctx *c, lex *L, const fpcoll *in);

static const char *type_of_json(jval *v) {
    if (j_is(v, J_OBJ)) {
        const char *rt = j_gets(v, "resourceType");
        if (rt) return rt;
    }
    return NULL;
}

static sdef *sd_of(fctx *c, const char *type) { return type ? defs_sd(c->d, type) : NULL; }

/* Navigate one step by element name, tracking where children are defined. */
static fpcoll step_name(fctx *c, const fpcoll *in, const char *name) {
    fpcoll out; coll_init(&out);
    for (size_t i = 0; i < in->n; i++) {
        jval *v = in->v[i].v;
        if (!j_is(v, J_OBJ)) {
            /* a primitive addressed as `x.value` is itself */
            if (str_eq(name, "value")) coll_pushn(&out, v, in->v[i].type, NULL, NULL);
            continue;
        }
        sdef *psd = in->v[i].sd;
        edef *pnode = in->v[i].node;
        if (!pnode) {
            const char *ty = in->v[i].type ? in->v[i].type : type_of_json(v);
            sdef *s = sd_of(c, ty);
            if (s) { psd = s; pnode = s->root; }
        }
        const char *ctype = NULL;
        edef *ce = pnode ? ed_child(c->d, psd, pnode, name, &ctype) : NULL;
        jval *m = j_get(v, name);
        if (!m && ce) {
            for (jmem *mm = v->u.obj.first; mm; mm = mm->next) {
                const char *t = ed_choice_type(ce, mm->key);
                if (t) { m = mm->val; ctype = t; break; }
            }
        }
        if (!m && str_eq(name, "value")) { coll_pushn(&out, v, in->v[i].type, NULL, NULL); continue; }
        if (!m) continue;
        if (!ctype && ce && ce->types.n >= 1) ctype = (const char *)ce->types.v[0];
        sdef *csd = NULL; edef *cnode = NULL;
        if (ce) ed_children_of(c->d, psd, ce, ctype, &csd, &cnode);
        if (j_is(m, J_ARR)) {
            for (int k = 0; k < j_len(m); k++) {
                jval *item = j_at(m, k);
                if (j_is(item, J_NULL)) continue;
                sdef *isd = csd; edef *inode = cnode;
                const char *ity = ctype;
                if (ity && (str_eq(ity, "Resource") || defs_resource(c->d, ity))) {
                    const char *rt = type_of_json(item);
                    sdef *rs = rt ? defs_resource(c->d, rt) : NULL;
                    if (rs) { isd = rs; inode = rs->root; ity = rt; }
                }
                coll_pushn(&out, item, ity, isd, inode);
            }
        } else {
            sdef *isd = csd; edef *inode = cnode;
            const char *ity = ctype;
            if (ity && (str_eq(ity, "Resource") || defs_resource(c->d, ity))) {
                const char *rt = type_of_json(m);
                sdef *rs = rt ? defs_resource(c->d, rt) : NULL;
                if (rs) { isd = rs; inode = rs->root; ity = rt; }
            }
            coll_pushn(&out, m, ity, isd, inode);
        }
    }
    return out;
}

/* `fhirpath.html` maps each FHIR primitive onto a FHIRPath system type, and a
   definition may name either one: STU3's `Goal-start-date` is written
   `Goal.start.as(Date)` over an element whose type is `date`. */
static const char *system_type_of(const char *t) {
    static const struct { const char *fhir, *sys; } map[] = {
        {"boolean", "Boolean"}, {"integer", "Integer"}, {"positiveInt", "Integer"},
        {"unsignedInt", "Integer"}, {"decimal", "Decimal"}, {"date", "Date"},
        {"dateTime", "DateTime"}, {"instant", "DateTime"}, {"time", "Time"},
        {"string", "String"}, {"code", "String"}, {"id", "String"},
        {"markdown", "String"}, {"uri", "String"}, {"url", "String"},
        {"canonical", "String"}, {"oid", "String"}, {"uuid", "String"},
        {"base64Binary", "String"}, {"Quantity", "Quantity"}, {NULL, NULL}};
    for (int i = 0; map[i].fhir; i++)
        if (str_eq(t, map[i].fhir)) return map[i].sys;
    return NULL;
}

static int is_type(fctx *c, const fpnode *n, const char *type) {
    const char *t = n->type ? n->type : type_of_json(n->v);
    if (!t) return 0;
    if (str_eq(t, type)) return 1;
    if (!strncmp(type, "System.", 7) && str_eq(system_type_of(t), type + 7)) return 1;
    if (str_eq(system_type_of(t), type)) return 1;
    /* walk baseDefinition chain */
    sdef *s = sd_of(c, t);
    int guard = 0;
    while (s && guard++ < 16) {
        if (str_eq(s->type, type)) return 1;
        if (!s->base) break;
        s = defs_sd(c->d, s->base);
    }
    return 0;
}

static const char *ref_target_type(jval *v) {
    const char *r = NULL;
    if (j_is(v, J_OBJ)) r = j_gets(v, "reference");
    else if (j_is(v, J_STR)) r = j_str_of(v);
    if (!r) return NULL;
    const char *hash = strchr(r, '#');
    (void)hash;
    /* "Patient/123", "http://h/base/Patient/123", "Patient/123/_history/2" */
    const char *segs[8]; int n = 0;
    const char *p = r;
    while (*p && n < 8) {
        const char *q = strchr(p, '/');
        if (!q) { segs[n++] = p; break; }
        segs[n++] = p;
        p = q + 1;
    }
    /* find the last segment that looks like a resource type (capitalised) */
    static char buf[128];
    for (int i = n - 1; i >= 0; i--) {
        const char *s = segs[i];
        if (!isupper((unsigned char)s[0])) continue;
        size_t len = 0;
        while (s[len] && s[len] != '/') len++;
        if (len == 0 || len >= sizeof buf) continue;
        int ok = 1;
        for (size_t k = 0; k < len; k++) if (!isalpha((unsigned char)s[k])) { ok = 0; break; }
        if (!ok) continue;
        memcpy(buf, s, len); buf[len] = 0;
        return buf;
    }
    return NULL;
}

static int coll_truthy(const fpcoll *c) {
    if (c->n == 0) return 0;
    for (size_t i = 0; i < c->n; i++) {
        jval *v = c->v[i].v;
        if (j_is(v, J_BOOL) && !v->u.b) return 0;
        if (j_is(v, J_STR) && str_eq(j_str_of(v), "false")) return 0;
    }
    return 1;
}

static const char *prim_str(jval *v) {
    if (j_is(v, J_STR)) return j_str_of(v);
    if (j_is(v, J_NUM)) return j_num_of(v);
    if (j_is(v, J_BOOL)) return v->u.b ? "true" : "false";
    return NULL;
}

/* Parse and evaluate a postfix chain starting from `in`. */
static fpcoll eval_postfix(fctx *c, lex *L, fpcoll cur);
static int apply_function(fctx *c, lex *L, const char *name, fpcoll *cur);

static fpcoll eval_primary(fctx *c, lex *L, const fpcoll *in) {
    fpcoll cur; coll_init(&cur);
    if (L->t == T_LP) {
        lx_next(L);
        fpcoll inner = eval_union(c, L, in);
        while (L->t == T_COMMA) {          /* a collection literal: ('a', 'b') */
            lx_next(L);
            fpcoll more = eval_union(c, L, in);
            for (size_t i = 0; i < more.n; i++) coll_copy(&inner, &more.v[i]);
            fp_free(&more);
        }
        if (L->t == T_RP) lx_next(L);
        return eval_postfix(c, L, inner);
    }
    if (L->t == T_STR) {
        coll_push(&cur, j_str(c->a, L->text), "string");
        lx_next(L);
        return eval_postfix(c, L, cur);
    }
    if (L->t == T_NUM) {
        coll_push(&cur, j_int(c->a, L->num), "integer");
        lx_next(L);
        return eval_postfix(c, L, cur);
    }
    if (L->t != T_NAME) { lx_next(L); return cur; }

    const char *name = L->text;
    lx_next(L);
    if (L->t == T_LP && !isupper((unsigned char)name[0])) {
        /* a function applied to the input collection, e.g. the `resolve()` that
           opens `where(resolve() is Patient)` */
        lx_next(L);
        for (size_t i = 0; i < in->n; i++) coll_copy(&cur, &in->v[i]);
        if (!apply_function(c, L, name, &cur)) {
            fp_free(&cur);
            coll_init(&cur);
        }
        return eval_postfix(c, L, cur);
    }
    if (str_eq(name, "true") || str_eq(name, "false")) {
        coll_push(&cur, j_bool(c->a, str_eq(name, "true")), "boolean");
        return eval_postfix(c, L, cur);
    }
    /* A leading capitalised name is either the resource type anchor or a type
       literal; both behave the same here. */
    if (isupper((unsigned char)name[0])) {
        for (size_t i = 0; i < in->n; i++) {
            const char *t = in->v[i].type ? in->v[i].type : type_of_json(in->v[i].v);
            if (str_eq(t, name)) coll_copy(&cur, &in->v[i]);
        }
        if (cur.n == 0) {
            /* not the anchor: treat as a plain element name (e.g. an extension) */
            fpcoll byname = step_name(c, in, name);
            return eval_postfix(c, L, byname);
        }
        return eval_postfix(c, L, cur);
    }
    fpcoll step = step_name(c, in, name);
    return eval_postfix(c, L, step);
}

/* Apply `name(...)` to `cur`.  L is positioned just after the '('; on return it
   sits after the matching ')'.  Returns 0 for an unknown function. */
static int apply_function(fctx *c, lex *L, const char *name, fpcoll *curp) {
    fpcoll cur = *curp;
    if (str_eq(name, "where")) {
        fpcoll out; coll_init(&out);
        const char *save = L->s;
        ttype savet = L->t; const char *savetext = L->text; long savenum = L->num;
        for (size_t i = 0; i < cur.n; i++) {
            lex sub = *L;
            sub.s = save; sub.t = savet; sub.text = savetext; sub.num = savenum;
            fpcoll one; coll_init(&one);
            coll_copy(&one, &cur.v[i]);
            fpcoll r = eval_union(c, &sub, &one);
            if (coll_truthy(&r)) coll_copy(&out, &cur.v[i]);
            fp_free(&r); fp_free(&one);
            *L = sub;
        }
        if (cur.n == 0) {
            fpcoll empty; coll_init(&empty);
            fpcoll r = eval_union(c, L, &empty);
            fp_free(&r); fp_free(&empty);
        }
        if (L->t == T_RP) lx_next(L);
        fp_free(&cur);
        *curp = out;
        return 1;
    }
    if (str_eq(name, "resolve")) {
        if (L->t == T_RP) lx_next(L);
        fpcoll out; coll_init(&out);
        for (size_t i = 0; i < cur.n; i++) {
            const char *t = ref_target_type(cur.v[i].v);
            if (t) coll_pushn(&out, cur.v[i].v, arena_strdup(c->a, t), NULL, NULL);
        }
        fp_free(&cur);
        *curp = out;
        return 1;
    }
    if (str_eq(name, "extension") || str_eq(name, "hasExtension")) {
        const char *url = (L->t == T_STR) ? L->text : NULL;
        if (L->t == T_STR) lx_next(L);
        if (L->t == T_RP) lx_next(L);
        fpcoll exts = step_name(c, &cur, "extension");
        fpcoll out; coll_init(&out);
        for (size_t i = 0; i < exts.n; i++)
            if (!url || str_eq(j_gets(exts.v[i].v, "url"), url)) coll_copy(&out, &exts.v[i]);
        fp_free(&exts);
        if (str_eq(name, "hasExtension")) {
            int had = out.n > 0;
            fp_free(&out); coll_init(&out);
            coll_push(&out, j_bool(c->a, had), "boolean");
        }
        fp_free(&cur);
        *curp = out;
        return 1;
    }
    if (str_eq(name, "as") || str_eq(name, "is") || str_eq(name, "ofType")) {
        const char *type = (L->t == T_NAME) ? L->text : "";
        int isop = str_eq(name, "is");
        if (L->t == T_NAME) lx_next(L);
        if (L->t == T_RP) lx_next(L);
        fpcoll out; coll_init(&out);
        if (isop) {
            int any = cur.n > 0;
            for (size_t i = 0; i < cur.n; i++) if (!is_type(c, &cur.v[i], type)) any = 0;
            coll_push(&out, j_bool(c->a, any), "boolean");
        } else {
            for (size_t i = 0; i < cur.n; i++) if (is_type(c, &cur.v[i], type)) coll_copy(&out, &cur.v[i]);
        }
        fp_free(&cur);
        *curp = out;
        return 1;
    }
    if (str_eq(name, "conformsTo")) {
        const char *url = (L->t == T_STR) ? L->text : NULL;
        if (L->t == T_STR) lx_next(L);
        if (L->t == T_RP) lx_next(L);
        int ok = 0;
        if (url) {
            const char *slash = strrchr(url, '/');
            const char *want = slash ? slash + 1 : url;
            for (size_t i = 0; i < cur.n; i++) if (is_type(c, &cur.v[i], want)) ok = 1;
        }
        fp_free(&cur); coll_init(&cur);
        coll_push(&cur, j_bool(c->a, ok), "boolean");
        *curp = cur;
        return 1;
    }
    if (str_eq(name, "exists") || str_eq(name, "empty")) {
        if (L->t == T_RP) lx_next(L);
        int e = cur.n > 0;
        fp_free(&cur); coll_init(&cur);
        coll_push(&cur, j_bool(c->a, str_eq(name, "exists") ? e : !e), "boolean");
        *curp = cur;
        return 1;
    }
    if (str_eq(name, "first") || str_eq(name, "last")) {
        if (L->t == T_RP) lx_next(L);
        fpcoll out; coll_init(&out);
        if (cur.n) {
            size_t i = str_eq(name, "first") ? 0 : cur.n - 1;
            coll_copy(&out, &cur.v[i]);
        }
        fp_free(&cur);
        *curp = out;
        return 1;
    }
    if (str_eq(name, "toString") || str_eq(name, "lower") || str_eq(name, "upper")) {
        if (L->t == T_RP) lx_next(L);
        *curp = cur;
        return 1;
    }
    if (str_eq(name, "not")) {
        if (L->t == T_RP) lx_next(L);
        int t = coll_truthy(&cur);
        fp_free(&cur); coll_init(&cur);
        coll_push(&cur, j_bool(c->a, !t), "boolean");
        *curp = cur;
        return 1;
    }
    /* unknown function: skip to the matching ')' and yield nothing */
    int depth = 1;
    while (depth > 0 && L->t != T_END) {
        if (L->t == T_LP) depth++;
        else if (L->t == T_RP) depth--;
        if (depth > 0) lx_next(L);
    }
    if (L->t == T_RP) lx_next(L);
    fp_free(&cur);
    coll_init(curp);
    return 0;
}

static fpcoll eval_postfix(fctx *c, lex *L, fpcoll cur) {
    for (;;) {
        if (L->t == T_LB) {
            lx_next(L);
            long idx = (L->t == T_NUM) ? L->num : 0;
            lx_next(L);
            if (L->t == T_RB) lx_next(L);
            fpcoll out; coll_init(&out);
            if (idx >= 0 && (size_t)idx < cur.n) coll_copy(&out, &cur.v[idx]);
            fp_free(&cur);
            cur = out;
            continue;
        }
        if (L->t == T_NAME && (str_eq(L->text, "as") || str_eq(L->text, "is"))) {
            int isop = str_eq(L->text, "is");
            lx_next(L);
            const char *type = (L->t == T_NAME) ? L->text : "";
            lx_next(L);
            fpcoll out; coll_init(&out);
            if (isop) {
                int any = cur.n > 0;
                for (size_t i = 0; i < cur.n; i++) if (!is_type(c, &cur.v[i], type)) any = 0;
                coll_push(&out, j_bool(c->a, any), "boolean");
            } else {
                for (size_t i = 0; i < cur.n; i++) if (is_type(c, &cur.v[i], type)) coll_copy(&out, &cur.v[i]);
            }
            fp_free(&cur);
            cur = out;
            continue;
        }
        if (L->t != T_DOT) break;
        lx_next(L);
        if (L->t != T_NAME) break;
        const char *name = L->text;
        lx_next(L);
        if (L->t == T_LP) {
            lx_next(L);
            apply_function(c, L, name, &cur);
            continue;
        }
        fpcoll step = step_name(c, &cur, name);
        fp_free(&cur);
        cur = step;
    }
    return cur;
}

static int coll_eq(const fpcoll *x, const fpcoll *y) {
    if (x->n == 0 || y->n == 0) return 0;
    for (size_t i = 0; i < x->n; i++) {
        const char *a = prim_str(x->v[i].v);
        for (size_t k = 0; k < y->n; k++) {
            const char *b = prim_str(y->v[k].v);
            if (a && b && strcmp(a, b) == 0) return 1;
            if (!a && !b && j_equal(x->v[i].v, y->v[k].v)) return 1;
        }
    }
    return 0;
}

static fpcoll eval_cmp(fctx *c, lex *L, const fpcoll *in) {
    fpcoll left = eval_primary(c, L, in);
    while (L->t == T_EQ || L->t == T_NE) {
        int neg = L->t == T_NE;
        lx_next(L);
        fpcoll right = eval_primary(c, L, in);
        int eq = coll_eq(&left, &right);
        fp_free(&left); fp_free(&right);
        coll_init(&left);
        coll_push(&left, j_bool(c->a, neg ? !eq : eq), "boolean");
    }
    return left;
}

static fpcoll eval_membership(fctx *c, lex *L, const fpcoll *in) {
    fpcoll left = eval_cmp(c, L, in);
    while (L->t == T_NAME && (str_eq(L->text, "in") || str_eq(L->text, "contains"))) {
        int rev = str_eq(L->text, "contains");
        lx_next(L);
        fpcoll right = eval_cmp(c, L, in);
        int found = rev ? coll_eq(&right, &left) : coll_eq(&left, &right);
        fp_free(&left); fp_free(&right);
        coll_init(&left);
        coll_push(&left, j_bool(c->a, found), "boolean");
    }
    return left;
}

static fpcoll eval_and(fctx *c, lex *L, const fpcoll *in) {
    fpcoll left = eval_membership(c, L, in);
    while (L->t == T_NAME && str_eq(L->text, "and")) {
        lx_next(L);
        fpcoll right = eval_membership(c, L, in);
        int v = coll_truthy(&left) && coll_truthy(&right);
        fp_free(&left); fp_free(&right);
        coll_init(&left);
        coll_push(&left, j_bool(c->a, v), "boolean");
    }
    return left;
}

static fpcoll eval_or(fctx *c, lex *L, const fpcoll *in) {
    fpcoll left = eval_and(c, L, in);
    while (L->t == T_NAME && (str_eq(L->text, "or") || str_eq(L->text, "xor"))) {
        lx_next(L);
        fpcoll right = eval_and(c, L, in);
        int v = coll_truthy(&left) || coll_truthy(&right);
        fp_free(&left); fp_free(&right);
        coll_init(&left);
        coll_push(&left, j_bool(c->a, v), "boolean");
    }
    return left;
}

static fpcoll eval_union(fctx *c, lex *L, const fpcoll *in) {
    fpcoll acc = eval_or(c, L, in);
    while (L->t == T_PIPE) {
        lx_next(L);
        fpcoll r = eval_or(c, L, in);
        for (size_t i = 0; i < r.n; i++) coll_copy(&acc, &r.v[i]);
        fp_free(&r);
    }
    return acc;
}

fpcoll fp_eval_in(arena *a, fhirdefs *d, const fpcoll *start, const char *expr) {
    fctx c = { a, d, start->n ? start->v[0].v : NULL };
    lex L; memset(&L, 0, sizeof L);
    L.a = a; L.s = expr ? expr : "";
    lx_next(&L);
    return eval_union(&c, &L, start);
}

fpcoll fp_eval(arena *a, fhirdefs *d, jval *root, const char *expr) {
    fctx c = { a, d, root };
    lex L; memset(&L, 0, sizeof L);
    L.a = a; L.s = expr ? expr : "";
    lx_next(&L);
    fpcoll in; coll_init(&in);
    const char *rt = type_of_json(root);
    sdef *rs = rt ? defs_resource(d, rt) : NULL;
    coll_pushn(&in, root, rt, rs, rs ? rs->root : NULL);
    fpcoll out = eval_union(&c, &L, &in);
    fp_free(&in);
    return out;
}
