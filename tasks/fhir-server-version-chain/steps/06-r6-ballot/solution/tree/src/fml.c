#include "fml.h"
#include "fpath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------------- model ---------------- */

typedef struct {
    const char *url, *alias, *mode;
} fml_uses;

typedef struct {
    const char *context, *element, *variable, *where, *check;
} fml_source;

typedef struct {
    const char *context, *element, *variable, *transform;
    vec         params;          /* const char*, verbatim parameter text */
    const char *listmode;
    const char *share;           /* `share <name>`: reuse one created value */
} fml_target;

typedef struct fml_rule {
    vec         sources;         /* fml_source* */
    vec         targets;         /* fml_target* */
    vec         deps;            /* const char*, "group(a, b)" */
    vec         subrules;        /* fml_rule*   */
    const char *name;
} fml_rule;

typedef struct {
    const char *name, *extends, *typemode;
    vec         params;          /* const char* "mode:name:type" */
    vec         rules;           /* fml_rule* */
} fml_group;

struct fmlmap {
    const char *url, *name;
    vec         uses;            /* fml_uses*  */
    vec         imports;         /* const char* */
    vec         groups;          /* fml_group* */
    arena      *a;
};

const char *fml_url(fmlmap *m) { return m ? m->url : NULL; }
const char *fml_name(fmlmap *m) { return m ? m->name : NULL; }

/* ---------------- lexer ---------------- */

typedef struct {
    const char *p;
    arena      *a;
    char       *err;
    size_t      errlen;
    int         failed;
} fp_t;

static void fail(fp_t *P, const char *msg) {
    if (!P->failed && P->err && P->errlen) {
        snprintf(P->err, P->errlen, "%s near: %.40s", msg, P->p ? P->p : "");
    }
    P->failed = 1;
}

static void skip_ws(fp_t *P) {
    for (;;) {
        while (*P->p && isspace((unsigned char)*P->p)) P->p++;
        if (P->p[0] == '/' && P->p[1] == '/') { while (*P->p && *P->p != '\n') P->p++; continue; }
        if (P->p[0] == '/' && P->p[1] == '*') {
            P->p += 2;
            while (*P->p && !(P->p[0] == '*' && P->p[1] == '/')) P->p++;
            if (*P->p) P->p += 2;
            continue;
        }
        break;
    }
}

static int eat(fp_t *P, const char *lit) {
    skip_ws(P);
    size_t n = strlen(lit);
    if (strncmp(P->p, lit, n) != 0) return 0;
    /* a word literal must not be a prefix of a longer identifier */
    if (isalpha((unsigned char)lit[0]) && (isalnum((unsigned char)P->p[n]) || P->p[n] == '_')) return 0;
    P->p += n;
    return 1;
}

static const char *word(fp_t *P) {
    skip_ws(P);
    const char *st = P->p;
    while (isalnum((unsigned char)*P->p) || *P->p == '_' || *P->p == '-') P->p++;
    if (P->p == st) return NULL;
    return arena_strndup(P->a, st, (size_t)(P->p - st));
}

static const char *qstring(fp_t *P) {
    skip_ws(P);
    char q = *P->p;
    if (q != '"' && q != '\'') return NULL;
    P->p++;
    buf b; buf_init(&b);
    while (*P->p && *P->p != q) {
        if (*P->p == '\\' && P->p[1]) { P->p++; buf_putc(&b, *P->p == 'n' ? '\n' : *P->p == 't' ? '\t' : *P->p); P->p++; }
        else buf_putc(&b, *P->p++);
    }
    if (*P->p == q) P->p++;
    const char *out = arena_strndup(P->a, b.p ? b.p : "", b.n);
    buf_free(&b);
    return out;
}

/* Copy text up to one of `stops` at nesting depth 0, honouring quotes. */
static const char *until(fp_t *P, const char *stops) {
    skip_ws(P);
    const char *st = P->p;
    int depth = 0;
    while (*P->p) {
        char c = *P->p;
        if (c == '\'' || c == '"') {
            char q = c;
            const char *qs = P->p;
            P->p++;
            while (*P->p && *P->p != q) { if (*P->p == '\\' && P->p[1]) P->p++; P->p++; }
            if (*P->p) P->p++;
            if (q == '"' && depth == 0 && strchr(stops, '"')) {
                /* `mapping-language.html` puts the rule name last: a double
                   quoted string followed by the end of the rule is the name,
                   one followed by anything else is a value. */
                const char *la = P->p;
                while (*la && isspace((unsigned char)*la)) la++;
                if (!*la || *la == ';' || *la == '}') { P->p = qs; break; }
            }
            continue;
        }
        if (c == '(' || c == '[') depth++;
        else if (c == ')' || c == ']') { if (depth == 0 && strchr(stops, c)) break; depth--; }
        else if (depth == 0 && strchr(stops, c)) break;
        else if (depth == 0 && c == 'l' && !strncmp(P->p, "log", 3) &&
                 P->p != st && isspace((unsigned char)P->p[-1]) &&
                 (isspace((unsigned char)P->p[3]) || P->p[3] == '('))
            break;
        else if (depth == 0 && c == 't' && !strncmp(P->p, "then", 4) &&
                 (P->p == st || isspace((unsigned char)P->p[-1])) &&
                 (isspace((unsigned char)P->p[4]) || P->p[4] == '{'))
            break;            /* `where (...) then {` -- `then` starts the block */
        P->p++;
    }
    size_t n = (size_t)(P->p - st);
    while (n && isspace((unsigned char)st[n - 1])) n--;
    return arena_strndup(P->a, st, n);
}

/* ---------------- parser ---------------- */

static const char *strip_parens(arena *a, const char *e) {
    /* A whole expression wrapped in one pair of parentheses is the same
       expression; the parentheses are FML punctuation, not part of it. */
    if (!e) return e;
    while (e[0] == '(') {
        size_t n = strlen(e);
        if (n < 2 || e[n - 1] != ')') break;
        int depth = 0;
        size_t i = 0;
        for (; i < n; i++) {
            char c = e[i];
            if (c == '\'' || c == '"') {
                char q = c;
                i++;
                while (i < n && e[i] != q) { if (e[i] == '\\' && i + 1 < n) i++; i++; }
                continue;
            }
            if (c == '(') depth++;
            else if (c == ')') { depth--; if (!depth) break; }
        }
        if (i != n - 1) break;              /* the pair does not span the whole */
        e = arena_strndup(a, e + 1, n - 2);
        while (*e == ' ') e++;
    }
    return e;
}


static void parse_rule(fp_t *P, vec *out);

static fml_source *parse_source(fp_t *P) {
    fml_source *s = arena_calloc(P->a, sizeof(fml_source));
    skip_ws(P);
    const char *ctx = word(P);
    if (!ctx) { fail(P, "expected a source context"); return s; }
    s->context = ctx;
    buf path; buf_init(&path);
    while (*P->p == '.') {
        P->p++;
        const char *w = word(P);
        if (!w) break;
        if (path.n) buf_putc(&path, '.');
        buf_puts(&path, w);
    }
    if (path.n) s->element = arena_strndup(P->a, path.p, path.n);
    buf_free(&path);
    /* an optional type and cardinality are accepted and ignored */
    skip_ws(P);
    if (*P->p == ':') { P->p++; word(P); }
    skip_ws(P);
    if (isdigit((unsigned char)*P->p)) { while (isdigit((unsigned char)*P->p) || *P->p == '.' || *P->p == '*') P->p++; }
    for (;;) {
        skip_ws(P);
        if (eat(P, "as")) { s->variable = word(P); continue; }
        if (eat(P, "where")) { s->where = strip_parens(P->a, until(P, ",->;{\"")); continue; }
        if (eat(P, "check")) { s->check = strip_parens(P->a, until(P, ",->;{\"")); continue; }
        if (eat(P, "log")) { until(P, ",->;{\""); continue; }
        if (eat(P, "default")) { until(P, ",->;{\""); continue; }
        if (eat(P, "first") || eat(P, "last") || eat(P, "not_first") ||
            eat(P, "not_last") || eat(P, "only_one")) continue;
        break;
    }
    return s;
}

static fml_target *parse_target(fp_t *P) {
    fml_target *t = arena_calloc(P->a, sizeof(fml_target));
    vec_init(&t->params);
    skip_ws(P);
    t->context = word(P);
    buf path; buf_init(&path);
    while (*P->p == '.') {
        P->p++;
        const char *w = word(P);
        if (!w) break;
        if (path.n) buf_putc(&path, '.');
        buf_puts(&path, w);
    }
    if (path.n) t->element = arena_strndup(P->a, path.p, path.n);
    buf_free(&path);
    skip_ws(P);
    if (*P->p == '=') {
        P->p++;
        skip_ws(P);
        const char *save = P->p;
        const char *fn = word(P);
        skip_ws(P);
        if (fn && *P->p == '(') {
            P->p++;
            t->transform = fn;
            for (;;) {
                skip_ws(P);
                if (*P->p == ')') { P->p++; break; }
                const char *arg = until(P, ",)");
                if (arg && *arg) vec_push(&t->params, (void *)arg);
                skip_ws(P);
                if (*P->p == ',') { P->p++; continue; }
                if (*P->p == ')') { P->p++; break; }
                if (!*P->p) { fail(P, "unterminated transform arguments"); break; }
            }
        } else if (*P->p == '(') {
            /* `mapping-language.html`: a parenthesised right-hand side is a
               FHIRPath expression, which is the `evaluate` transform. */
            P->p = save;
            t->transform = "evaluate";
            const char *expr = strip_parens(P->a, until(P, ",;\""));
            vec_push(&t->params, (void *)expr);
        } else {
            P->p = save;
            t->transform = "copy";
            const char *expr = strip_parens(P->a, until(P, ",;\""));
            vec_push(&t->params, (void *)expr);
        }
    }
    for (;;) {
        skip_ws(P);
        if (eat(P, "as")) { t->variable = word(P); continue; }
        if (eat(P, "share")) {
            t->listmode = "share";
            skip_ws(P);
            if (isalpha((unsigned char)*P->p)) t->share = word(P);
            continue;
        }
        if (eat(P, "first") || eat(P, "last") || eat(P, "collate")) {
            t->listmode = "set";
            skip_ws(P);
            if (isalpha((unsigned char)*P->p)) word(P);
            continue;
        }
        break;
    }
    return t;
}

static void parse_rule(fp_t *P, vec *out) {
    fml_rule *r = arena_calloc(P->a, sizeof(fml_rule));
    vec_init(&r->sources); vec_init(&r->targets); vec_init(&r->deps); vec_init(&r->subrules);
    for (;;) {
        vec_push(&r->sources, parse_source(P));
        skip_ws(P);
        if (*P->p == ',') { P->p++; continue; }
        break;
    }
    skip_ws(P);
    if (P->p[0] == '-' && P->p[1] == '>') {
        P->p += 2;
        for (;;) {
            vec_push(&r->targets, parse_target(P));
            skip_ws(P);
            if (*P->p == ',') { P->p++; continue; }
            break;
        }
    }
    skip_ws(P);
    if (eat(P, "then")) {
        skip_ws(P);
        if (*P->p == '{') {
            P->p++;
            for (;;) {
                skip_ws(P);
                if (*P->p == '}') { P->p++; break; }
                if (!*P->p) { fail(P, "unterminated rule block"); break; }
                parse_rule(P, &r->subrules);
                if (P->failed) break;
            }
        } else {
            for (;;) {
                skip_ws(P);
                const char *inv = until(P, ",;\"");
                if (inv && *inv) vec_push(&r->deps, (void *)inv);
                skip_ws(P);
                if (*P->p == ',') { P->p++; continue; }
                break;
            }
        }
    }
    skip_ws(P);
    if (*P->p == '"') r->name = qstring(P);
    skip_ws(P);
    if (*P->p == ';') P->p++;
    vec_push(out, r);
}

static fml_group *parse_group(fp_t *P) {
    fml_group *g = arena_calloc(P->a, sizeof(fml_group));
    vec_init(&g->params); vec_init(&g->rules);
    g->name = word(P);
    skip_ws(P);
    if (*P->p != '(') { fail(P, "expected '(' after a group name"); return g; }
    P->p++;
    for (;;) {
        skip_ws(P);
        if (*P->p == ')') { P->p++; break; }
        const char *mode = word(P);
        const char *nm = word(P);
        const char *type = NULL;
        skip_ws(P);
        if (*P->p == ':') { P->p++; type = word(P); }
        vec_push(&g->params, (void *)arena_printf(P->a, "%s|%s|%s",
                 mode ? mode : "", nm ? nm : "", type ? type : ""));
        skip_ws(P);
        if (*P->p == ',') { P->p++; continue; }
        if (*P->p == ')') { P->p++; break; }
        if (!*P->p) { fail(P, "unterminated group parameters"); break; }
    }
    skip_ws(P);
    if (eat(P, "extends")) g->extends = word(P);
    skip_ws(P);
    if (P->p[0] == '<' && P->p[1] == '<') {
        P->p += 2;
        g->typemode = until(P, ">");
        if (g->typemode && !strcmp(g->typemode, "type+")) g->typemode = "type-and-types";
        while (*P->p == '>') P->p++;
    }
    skip_ws(P);
    if (*P->p != '{') { fail(P, "expected '{' to open a group"); return g; }
    P->p++;
    for (;;) {
        skip_ws(P);
        if (*P->p == '}') { P->p++; break; }
        if (!*P->p) { fail(P, "unterminated group"); break; }
        parse_rule(P, &g->rules);
        if (P->failed) break;
    }
    return g;
}

fmlmap *fml_parse(arena *a, const char *text, char *err, size_t errlen) {
    if (err && errlen) err[0] = 0;
    fmlmap *m = arena_calloc(a, sizeof(fmlmap));
    m->a = a;
    vec_init(&m->uses); vec_init(&m->imports); vec_init(&m->groups);
    fp_t P = { text, a, err, errlen, 0 };
    skip_ws(&P);
    /* metadata lines written as `/// name = "value"` */
    while (P.p[0] == '/' && P.p[1] == '/' && P.p[2] == '/') {
        while (*P.p && *P.p != '\n') P.p++;
        skip_ws(&P);
    }
    if (eat(&P, "map")) {
        m->url = qstring(&P);
        skip_ws(&P);
        if (*P.p == '=') { P.p++; m->name = qstring(&P); }
    }
    for (;;) {
        skip_ws(&P);
        if (eat(&P, "uses")) {
            fml_uses *u = arena_calloc(a, sizeof(fml_uses));
            u->url = qstring(&P);
            skip_ws(&P);
            if (eat(&P, "alias")) u->alias = word(&P);
            skip_ws(&P);
            if (eat(&P, "as")) u->mode = word(&P);
            vec_push(&m->uses, u);
            continue;
        }
        if (eat(&P, "imports")) { const char *u = qstring(&P); if (u) vec_push(&m->imports, (void *)u); continue; }
        if (eat(&P, "conceptmap")) {
            /* an embedded concept map is skipped whole */
            skip_ws(&P);
            qstring(&P);
            skip_ws(&P);
            if (*P.p == '{') {
                int depth = 0;
                do { if (*P.p == '{') depth++; else if (*P.p == '}') depth--; P.p++; } while (*P.p && depth);
            }
            continue;
        }
        break;
    }
    for (;;) {
        skip_ws(&P);
        if (!*P.p) break;
        if (!eat(&P, "group")) { fail(&P, "expected a group"); break; }
        vec_push(&m->groups, parse_group(&P));
        if (P.failed) break;
    }
    if (P.failed) return NULL;
    if (m->groups.n == 0) { if (err && errlen) snprintf(err, errlen, "the map declares no group"); return NULL; }
    return m;
}

/* ---------------- StructureMap resource form ---------------- */

static const char *sm_str(jval *o, const char *k) { return j_gets(o, k); }

fmlmap *fml_from_resource(arena *a, jval *sm, char *err, size_t errlen) {
    if (err && errlen) err[0] = 0;
    if (!str_eq(j_gets(sm, "resourceType"), "StructureMap")) {
        if (err && errlen) snprintf(err, errlen, "not a StructureMap resource");
        return NULL;
    }
    fmlmap *m = arena_calloc(a, sizeof(fmlmap));
    m->a = a;
    vec_init(&m->uses); vec_init(&m->imports); vec_init(&m->groups);
    m->url = sm_str(sm, "url");
    m->name = sm_str(sm, "name");
    jval *structs = j_get(sm, "structure");
    for (int i = 0; i < j_len(structs); i++) {
        jval *s = j_at(structs, i);
        fml_uses *u = arena_calloc(a, sizeof(fml_uses));
        u->url = sm_str(s, "url");
        u->alias = sm_str(s, "alias");
        u->mode = sm_str(s, "mode");
        vec_push(&m->uses, u);
    }
    jval *groups = j_get(sm, "group");
    for (int i = 0; i < j_len(groups); i++) {
        jval *g = j_at(groups, i);
        fml_group *fg = arena_calloc(a, sizeof(fml_group));
        vec_init(&fg->params); vec_init(&fg->rules);
        fg->name = sm_str(g, "name");
        fg->extends = sm_str(g, "extends");
        fg->typemode = sm_str(g, "typeMode");
        jval *ins = j_get(g, "input");
        for (int k = 0; k < j_len(ins); k++) {
            jval *in = j_at(ins, k);
            vec_push(&fg->params, (void *)arena_printf(a, "%s|%s|%s",
                     sm_str(in, "mode") ? sm_str(in, "mode") : "",
                     sm_str(in, "name") ? sm_str(in, "name") : "",
                     sm_str(in, "type") ? sm_str(in, "type") : ""));
        }
        jval *rules = j_get(g, "rule");
        /* rules nest through `rule.rule`, which maps onto subrules */
        for (int k = 0; k < j_len(rules); k++) {
            jval *r = j_at(rules, k);
            fml_rule *fr = arena_calloc(a, sizeof(fml_rule));
            vec_init(&fr->sources); vec_init(&fr->targets); vec_init(&fr->deps); vec_init(&fr->subrules);
            fr->name = sm_str(r, "name");
            jval *srcs = j_get(r, "source");
            for (int z = 0; z < j_len(srcs); z++) {
                jval *s = j_at(srcs, z);
                fml_source *fs = arena_calloc(a, sizeof(fml_source));
                fs->context = sm_str(s, "context");
                fs->element = sm_str(s, "element");
                fs->variable = sm_str(s, "variable");
                fs->where = sm_str(s, "condition");
                fs->check = sm_str(s, "check");
                vec_push(&fr->sources, fs);
            }
            jval *tgts = j_get(r, "target");
            for (int z = 0; z < j_len(tgts); z++) {
                jval *t = j_at(tgts, z);
                fml_target *ft = arena_calloc(a, sizeof(fml_target));
                vec_init(&ft->params);
                ft->context = sm_str(t, "context");
                ft->element = sm_str(t, "element");
                ft->variable = sm_str(t, "variable");
                ft->transform = sm_str(t, "transform");
                ft->listmode = sm_str(t, "listMode");
                ft->share = sm_str(t, "listRuleId");
                jval *ps = j_get(t, "parameter");
                for (int y = 0; y < j_len(ps); y++) {
                    jval *pv = j_at(ps, y);
                    const char *lit = NULL;
                    for (jmem *mm = pv->kind == J_OBJ ? pv->u.obj.first : NULL; mm; mm = mm->next) {
                        if (str_starts(mm->key, "value")) {
                            if (j_is(mm->val, J_STR)) {
                                if (str_eq(mm->key, "valueId"))
                                    lit = arena_strdup(a, j_str_of(mm->val));
                                else
                                    lit = arena_printf(a, "'%s'", j_str_of(mm->val));
                            } else if (j_is(mm->val, J_NUM)) lit = arena_strdup(a, j_num_of(mm->val));
                            else if (j_is(mm->val, J_BOOL)) lit = mm->val->u.b ? "true" : "false";
                        }
                    }
                    if (lit) vec_push(&ft->params, (void *)lit);
                }
                vec_push(&fr->targets, ft);
            }
            jval *deps = j_get(r, "dependent");
            for (int z = 0; z < j_len(deps); z++) {
                jval *dp = j_at(deps, z);
                buf b; buf_init(&b);
                buf_printf(&b, "%s(", sm_str(dp, "name") ? sm_str(dp, "name") : "");
                jval *vs = j_get(dp, "variable");
                for (int y = 0; y < j_len(vs); y++) {
                    if (y) buf_puts(&b, ", ");
                    buf_puts(&b, j_str_of(j_at(vs, y)));
                }
                buf_putc(&b, ')');
                vec_push(&fr->deps, (void *)arena_strndup(a, b.p, b.n));
                buf_free(&b);
            }
            vec_push(&fg->rules, fr);
        }
        vec_push(&m->groups, fg);
    }
    if (m->groups.n == 0) {
        if (err && errlen) snprintf(err, errlen, "the StructureMap declares no group");
        return NULL;
    }
    return m;
}

/* ---------------- engine ---------------- */

typedef struct fvar {
    const char  *name;
    jval        *val;
    const char  *type;
    sdef        *sd;      /* where val's children are defined */
    edef        *node;    /* val's element node inside sd     */
    struct fvar *next;
} fvar;

typedef struct {
    arena    *a;
    fhirdefs *d;
    fmlmap   *m;
    char     *err;
    size_t    errlen;
    int       depth;
    int       failed;
    struct fvar *shares;   /* `share <id>` values, one table per group call */
} fctx_t;

static fvar *var_push2(fctx_t *c, fvar *env, const char *name, jval *v, const char *type,
                       sdef *sd, edef *node) {
    fvar *f = arena_calloc(c->a, sizeof(fvar));
    f->name = name; f->val = v; f->type = type; f->sd = sd; f->node = node; f->next = env;
    if (!f->sd && type) {
        sdef *s = defs_sd(c->d, type);
        if (s) { f->sd = s; f->node = s->root; }
    }
    return f;
}

static fvar *var_push(fctx_t *c, fvar *env, const char *name, jval *v, const char *type) {
    return var_push2(c, env, name, v, type, NULL, NULL);
}

/* One-element input collection for FHIRPath, carrying the definition context. */
static fpcoll one_of(fvar *f) {
    static fpnode slot[8];
    static int turn = 0;
    fpcoll c;
    memset(&c, 0, sizeof c);
    int i = turn++ & 7;
    slot[i].v = f->val; slot[i].type = f->type; slot[i].sd = f->sd; slot[i].node = f->node;
    c.v = &slot[i]; c.n = 1; c.cap = 1;
    return c;
}

static fvar *var_find(fvar *env, const char *name) {
    for (fvar *f = env; f; f = f->next) if (str_eq(f->name, name)) return f;
    return NULL;
}

static void run_rules(fctx_t *c, vec *rules, fvar *env);

static const char *type_of(jval *v) {
    return j_is(v, J_OBJ) ? j_gets(v, "resourceType") : NULL;
}

/* `uses ... alias X as target` gives the type a bare `create()` produces. */
static const char *alias_type(fmlmap *m, const char *alias) {
    for (size_t i = 0; i < m->uses.n; i++) {
        fml_uses *u = m->uses.v[i];
        if (str_eq(u->alias, alias)) {
            const char *slash = u->url ? strrchr(u->url, '/') : NULL;
            return slash ? slash + 1 : u->url;
        }
    }
    return alias;
}

static jval *eval_expr(fctx_t *c, fvar *env, const char *expr) {
    /* a string literal */
    if (!expr) return NULL;
    while (*expr == ' ') expr++;
    size_t n = strlen(expr);
    if (n >= 2 && expr[0] == '\'' && expr[n - 1] == '\'')
        return j_str(c->a, arena_strndup(c->a, expr + 1, n - 2));
    if (str_eq(expr, "true") || str_eq(expr, "false")) return j_bool(c->a, str_eq(expr, "true"));
    /* a bare variable */
    fvar *f = var_find(env, expr);
    if (f) return f->val;
    /* otherwise FHIRPath, rooted at the first path segment's variable */
    const char *dot = strchr(expr, '.');
    const char *head = dot ? arena_strndup(c->a, expr, (size_t)(dot - expr)) : expr;
    fvar *base = var_find(env, head);
    if (!base) {
        /* try the whole expression against every variable in turn */
        for (fvar *x = env; x; x = x->next) {
            if (!j_is(x->val, J_OBJ)) continue;
            fpcoll r = fp_eval(c->a, c->d, x->val, expr);
            jval *out = r.n ? r.v[0].v : NULL;
            fp_free(&r);
            if (out) return out;
        }
        return NULL;
    }
    if (!dot) return base->val;
    fpcoll start = one_of(base);
    fpcoll r = fp_eval_in(c->a, c->d, &start, dot + 1);
    jval *out = r.n ? r.v[0].v : NULL;
    fp_free(&r);
    return out;
}

static int truthy_where(fctx_t *c, fvar *item, const char *where) {
    if (!where || !*where) return 1;
    fpcoll start = one_of(item);
    fpcoll r = fp_eval_in(c->a, c->d, &start, where);
    int ok = 0;
    for (size_t i = 0; i < r.n; i++) {
        jval *v = r.v[i].v;
        if (j_is(v, J_BOOL)) { ok = v->u.b; break; }
        ok = 1;
    }
    fp_free(&r);
    return ok;
}

static void set_member(fctx_t *c, jval *obj, const char *elem, jval *val,
                       const char *owner_type, const char *val_type) {
    if (!j_is(obj, J_OBJ) || !elem) return;
    sdef *s = owner_type ? defs_sd(c->d, owner_type) : NULL;
    if (!s) {
        const char *rt = type_of(obj);
        if (rt) s = defs_sd(c->d, rt);
    }
    int repeats = 0;
    const char *ctype = NULL;
    if (s && s->root) {
        edef *e = ed_child(c->d, s, s->root, elem, &ctype);
        if (e) {
            repeats = e->max && (str_eq(e->max, "*") || atoi(e->max) > 1);
            /* a choice element is written with its concrete type suffix */
            size_t tl = strlen(e->tail);
            if (tl > 3 && strcmp(e->tail + tl - 3, "[x]") == 0) {
                const char *vt = val_type;
                if (!vt) vt = type_of(val);
                if (!vt && j_is(val, J_STR)) vt = "String";
                if (!vt && j_is(val, J_BOOL)) vt = "Boolean";
                if (vt) {
                    char *nm = arena_printf(c->a, "%.*s%c%s", (int)(tl - 3), e->tail,
                                            toupper((unsigned char)vt[0]), vt + 1);
                    elem = nm;
                }
            }
        }
    }
    if (repeats) {
        jval *cur = j_get(obj, elem);
        if (!cur || !j_is(cur, J_ARR)) { cur = j_arr(c->a); j_set(c->a, obj, elem, cur); }
        j_push(c->a, cur, val);
    } else {
        j_set(c->a, obj, elem, val);
    }
}

static const char *member_type(fctx_t *c, const char *owner_type, const char *elem) {
    sdef *s = owner_type ? defs_sd(c->d, owner_type) : NULL;
    if (!s || !s->root) return NULL;
    const char *ctype = NULL;
    edef *e = ed_child(c->d, s, s->root, elem, &ctype);
    if (!e) return NULL;
    if (!ctype && e->types.n >= 1) ctype = (const char *)e->types.v[0];
    return ctype;
}

static void apply_target(fctx_t *c, fvar **env, fml_target *t) {
    fvar *ctx = t->context ? var_find(*env, t->context) : NULL;
    if (!ctx) return;
    jval *owner = ctx->val;
    const char *owner_type = ctx->type ? ctx->type : type_of(owner);
    jval *value = NULL;
    const char *vtype = NULL;

    /* `share <id>` makes every rule that names the same id write into one
       created value instead of creating a new one each time. */
    if (t->share && t->element) {
        char key[256];
        snprintf(key, sizeof key, "#share:%s", t->share);
        fvar *sh = var_find(c->shares, key);
        if (sh && sh->val) {
            if (t->variable) *env = var_push2(c, *env, t->variable, sh->val, sh->type, sh->sd, sh->node);
            return;
        }
    }

    const char *tf = t->transform;
    if (!t->element) {
        /* `-> tgt as x`: bind another name to the same object */
        if (t->variable) *env = var_push(c, *env, t->variable, owner, owner_type);
        return;
    }
    if (!tf) {
        /* create an empty child object of the element's own type */
        const char *mt = member_type(c, owner_type, t->element);
        value = j_obj(c->a);
        vtype = mt;
    } else if (str_eq(tf, "create")) {
        const char *type = t->params.n ? (const char *)t->params.v[0] : NULL;
        if (type && *type == '\'') type = arena_strndup(c->a, type + 1, strlen(type) - 2);
        if (!type) type = member_type(c, owner_type, t->element);
        value = j_obj(c->a);
        if (type && defs_resource(c->d, type)) j_set(c->a, value, "resourceType", j_str(c->a, type));
        vtype = type;
    } else if (str_eq(tf, "copy") || str_eq(tf, "evaluate")) {
        const char *expr = t->params.n ? (const char *)t->params.v[t->params.n - 1] : NULL;
        if (expr && *expr == '(' && expr[strlen(expr) - 1] == ')')
            expr = arena_strndup(c->a, expr + 1, strlen(expr) - 2);
        value = eval_expr(c, *env, expr);
        if (value && j_is(value, J_OBJ) && !type_of(value)) {
            /* a complex value copied as-is keeps its shape */
        }
    } else if (str_eq(tf, "truncate")) {
        jval *v = t->params.n ? eval_expr(c, *env, (const char *)t->params.v[0]) : NULL;
        long lim = t->params.n > 1 ? strtol((const char *)t->params.v[1], NULL, 10) : 0;
        const char *s = v && j_is(v, J_STR) ? j_str_of(v) : NULL;
        if (s) value = j_str(c->a, arena_strndup(c->a, s, (size_t)lim < strlen(s) ? (size_t)lim : strlen(s)));
    } else if (str_eq(tf, "append")) {
        buf b; buf_init(&b);
        for (size_t i = 0; i < t->params.n; i++) {
            jval *v = eval_expr(c, *env, (const char *)t->params.v[i]);
            if (v && j_is(v, J_STR)) buf_puts(&b, j_str_of(v));
            else if (v && j_is(v, J_NUM)) buf_puts(&b, j_num_of(v));
        }
        value = j_str(c->a, arena_strndup(c->a, b.p ? b.p : "", b.n));
        buf_free(&b);
    } else if (str_eq(tf, "reference")) {
        jval *v = t->params.n ? eval_expr(c, *env, (const char *)t->params.v[0]) : NULL;
        const char *rt = v ? type_of(v) : NULL;
        const char *id = v ? j_gets(v, "id") : NULL;
        if (rt) value = j_str(c->a, id ? arena_printf(c->a, "%s/%s", rt, id) : rt);
    } else if (str_eq(tf, "uuid")) {
        value = j_str(c->a, "urn:uuid:00000000-0000-4000-8000-000000000000");
    } else if (str_eq(tf, "cast")) {
        value = t->params.n ? eval_expr(c, *env, (const char *)t->params.v[0]) : NULL;
    } else if (str_eq(tf, "c") || str_eq(tf, "cc")) {
        jval *coding = j_obj(c->a);
        if (t->params.n > 0) {
            jval *sys = eval_expr(c, *env, (const char *)t->params.v[0]);
            if (sys) j_set(c->a, coding, "system", sys);
        }
        if (t->params.n > 1) {
            jval *code = eval_expr(c, *env, (const char *)t->params.v[1]);
            if (code) j_set(c->a, coding, "code", code);
        }
        if (t->params.n > 2) {
            jval *disp = eval_expr(c, *env, (const char *)t->params.v[2]);
            if (disp) j_set(c->a, coding, "display", disp);
        }
        if (str_eq(tf, "cc")) {
            jval *ccv = j_obj(c->a);
            jval *arr = j_arr(c->a);
            j_push(c->a, arr, coding);
            j_set(c->a, ccv, "coding", arr);
            value = ccv;
            vtype = "CodeableConcept";
        } else { value = coding; vtype = "Coding"; }
    } else if (str_eq(tf, "qty")) {
        value = t->params.n ? eval_expr(c, *env, (const char *)t->params.v[0]) : NULL;
    } else if (str_eq(tf, "id")) {
        value = t->params.n > 1 ? eval_expr(c, *env, (const char *)t->params.v[1]) : NULL;
    } else {
        /* an unknown transform yields nothing rather than failing the map */
        value = NULL;
    }
    if (!value) return;
    if (!vtype) vtype = member_type(c, owner_type, t->element);
    set_member(c, owner, t->element, value, owner_type, vtype);
    if (t->variable) *env = var_push(c, *env, t->variable, value, vtype);
    if (t->share) {
        char key[256];
        snprintf(key, sizeof key, "#share:%s", t->share);
        c->shares = var_push(c, c->shares, arena_strdup(c->a, key), value, vtype);
    }
}

static fml_group *find_group(fmlmap *m, const char *name) {
    for (size_t i = 0; i < m->groups.n; i++) {
        fml_group *g = m->groups.v[i];
        if (str_eq(g->name, name)) return g;
    }
    return NULL;
}

static void run_group(fctx_t *c, fml_group *g, vec *args) {
    if (!g || c->depth > 40) return;
    c->depth++;
    fvar *saved_shares = c->shares;
    c->shares = NULL;
    fvar *env = NULL;
    for (size_t i = 0; i < g->params.n && i < args->n; i++) {
        char *spec = arena_strdup(c->a, (const char *)g->params.v[i]);
        char *p1 = strchr(spec, '|');
        char *p2 = p1 ? strchr(p1 + 1, '|') : NULL;
        if (p1) *p1 = 0;
        if (p2) *p2 = 0;
        const char *nm = p1 ? p1 + 1 : spec;
        const char *ty = p2 ? p2 + 1 : NULL;
        fvar *src = args->v[i];
        env = var_push2(c, env, nm, src->val, (ty && *ty) ? ty : src->type, src->sd, src->node);
    }
    if (g->extends) {
        fml_group *base = find_group(c->m, g->extends);
        if (base) run_group(c, base, args);
    }
    run_rules(c, &g->rules, env);
    c->shares = saved_shares;
    c->depth--;
}

static void run_rule(fctx_t *c, fml_rule *r, fvar *env) {
    if (r->sources.n == 0) return;
    fml_source *s0 = r->sources.v[0];
    fvar *base = var_find(env, s0->context);
    if (!base) return;

    /* the values this rule iterates over */
    vec items; vec_init(&items);
    vec types; vec_init(&types);
    vec sds; vec_init(&sds);
    vec nodes; vec_init(&nodes);
    if (!s0->element) {
        vec_push(&items, base->val);
        vec_push(&types, (void *)(base->type ? base->type : type_of(base->val)));
        vec_push(&sds, base->sd);
        vec_push(&nodes, base->node);
    } else {
        fpcoll start = one_of(base);
        fpcoll got = fp_eval_in(c->a, c->d, &start, s0->element);
        for (size_t i = 0; i < got.n; i++) {
            vec_push(&items, got.v[i].v);
            vec_push(&types, (void *)got.v[i].type);
            vec_push(&sds, got.v[i].sd);
            vec_push(&nodes, got.v[i].node);
        }
        fp_free(&got);
    }

    for (size_t i = 0; i < items.n; i++) {
        jval *item = items.v[i];
        const char *itype = (const char *)types.v[i];
        fvar probe = { "$this", item, itype, sds.v[i], nodes.v[i], NULL };
        if (!truthy_where(c, &probe, s0->where)) continue;
        fvar *local = env;
        if (s0->variable)
            local = var_push2(c, local, s0->variable, item, itype, sds.v[i], nodes.v[i]);
        for (size_t t = 0; t < r->targets.n; t++) apply_target(c, &local, r->targets.v[t]);
        for (size_t k = 0; k < r->deps.n; k++) {
            const char *inv = (const char *)r->deps.v[k];
            char *copy = arena_strdup(c->a, inv);
            char *lp = strchr(copy, '(');
            if (!lp) continue;
            *lp = 0;
            char *rp = strrchr(lp + 1, ')');
            if (rp) *rp = 0;
            vec args; vec_init(&args);
            for (char *tok = strtok(lp + 1, ","); tok; tok = strtok(NULL, ",")) {
                while (*tok == ' ') tok++;
                char *end = tok + strlen(tok);
                while (end > tok && end[-1] == ' ') *--end = 0;
                fvar *v = var_find(local, tok);
                if (!v) { v = arena_calloc(c->a, sizeof(fvar)); v->name = tok; v->val = NULL; }
                vec_push(&args, v);
            }
            char *nm = copy;
            while (*nm == ' ') nm++;
            run_group(c, find_group(c->m, nm), &args);
            vec_free(&args);
        }
        if (r->subrules.n) run_rules(c, &r->subrules, local);
    }
    vec_free(&items); vec_free(&types); vec_free(&sds); vec_free(&nodes);
}

static void run_rules(fctx_t *c, vec *rules, fvar *env) {
    for (size_t i = 0; i < rules->n; i++) run_rule(c, rules->v[i], env);
}

jval *fml_transform(arena *a, fhirdefs *d, fmlmap *m, jval *src, char *err, size_t errlen) {
    if (err && errlen) err[0] = 0;
    if (!m || m->groups.n == 0) { if (err && errlen) snprintf(err, errlen, "no map"); return NULL; }
    fctx_t c = { a, d, m, err, errlen, 0, 0, NULL };
    fml_group *g = m->groups.v[0];
    /* the first group's second parameter is the target; create it from the
       type the group declares, or from the `as target` structure. */
    const char *ttype = NULL;
    if (g->params.n >= 2) {
        char *spec = arena_strdup(a, (const char *)g->params.v[1]);
        char *p1 = strchr(spec, '|');
        char *p2 = p1 ? strchr(p1 + 1, '|') : NULL;
        if (p2 && p2[1]) ttype = p2 + 1;
    }
    if (!ttype) {
        for (size_t i = 0; i < m->uses.n; i++) {
            fml_uses *u = m->uses.v[i];
            if (str_eq(u->mode, "target")) ttype = alias_type(m, u->alias);
        }
    }
    if (!ttype) { if (err && errlen) snprintf(err, errlen, "the map does not name a target type"); return NULL; }
    ttype = alias_type(m, ttype);
    jval *tgt = j_obj(a);
    if (defs_resource(d, ttype)) j_set(a, tgt, "resourceType", j_str(a, ttype));
    sdef *ssd = defs_sd(d, type_of(src));
    sdef *tsd = defs_sd(d, ttype);
    fvar sv = { NULL, src, type_of(src), ssd, ssd ? ssd->root : NULL, NULL };
    fvar tv = { NULL, tgt, ttype, tsd, tsd ? tsd->root : NULL, NULL };
    vec args; vec_init(&args);
    vec_push(&args, &sv);
    vec_push(&args, &tv);
    run_group(&c, g, &args);
    vec_free(&args);
    return tgt;
}

/* ---------------- FML text -> StructureMap resource ---------------- */

static jval *param_value(arena *a, const char *p) {
    jval *o = j_obj(a);
    size_t n = p ? strlen(p) : 0;
    if (n >= 2 && ((p[0] == '\'' && p[n - 1] == '\'') || (p[0] == '"' && p[n - 1] == '"')))
        j_set(a, o, "valueString", j_str(a, arena_strndup(a, p + 1, n - 2)));
    else if (p && (isdigit((unsigned char)p[0]) || (p[0] == '-' && isdigit((unsigned char)p[1]))))
        j_set(a, o, "valueInteger", j_num(a, p));
    else
        j_set(a, o, "valueId", j_str(a, p ? p : ""));
    return o;
}

static jval *rule_resource(arena *a, fml_rule *r, int idx) {
    jval *jr = j_obj(a);
    const char *nm = r->name;
    if (!nm) {
        /* `mapping-language.html` makes the rule name optional; a map rendered
           into a StructureMap still carries one, taken from what the rule
           reads.  `value[x]` loses its marker. */
        const fml_source *s0 = r->sources.n ? r->sources.v[0] : NULL;
        const char *base = s0 ? (s0->element ? s0->element : s0->context) : NULL;
        if (base && *base) {
            char *t = arena_strdup(a, base);
            char *br = strchr(t, '[');
            if (br) *br = 0;
            for (char *q = t; *q; q++)
                if (*q == '.') *q = '_';
            nm = t;
        }
    }
    j_set(a, jr, "name", j_str(a, nm ? nm : arena_printf(a, "rule%d", idx)));
    jval *srcs = j_arr(a);
    for (size_t i = 0; i < r->sources.n; i++) {
        fml_source *s = r->sources.v[i];
        jval *o = j_obj(a);
        if (s->context) j_set(a, o, "context", j_str(a, s->context));
        if (s->element) j_set(a, o, "element", j_str(a, s->element));
        if (s->variable) j_set(a, o, "variable", j_str(a, s->variable));
        if (s->where) j_set(a, o, "condition", j_str(a, s->where));
        if (s->check) j_set(a, o, "check", j_str(a, s->check));
        j_push(a, srcs, o);
    }
    j_set(a, jr, "source", srcs);
    if (r->targets.n) {
        jval *tgts = j_arr(a);
        for (size_t i = 0; i < r->targets.n; i++) {
            fml_target *t = r->targets.v[i];
            jval *o = j_obj(a);
            if (t->context) j_set(a, o, "context", j_str(a, t->context));
            if (t->element) j_set(a, o, "element", j_str(a, t->element));
            if (t->variable) j_set(a, o, "variable", j_str(a, t->variable));
            if (t->transform) j_set(a, o, "transform", j_str(a, t->transform));
            if (t->listmode) j_set(a, o, "listMode", j_str(a, t->listmode));
            if (t->share) j_set(a, o, "listRuleId", j_str(a, t->share));
            if (t->params.n) {
                jval *ps = j_arr(a);
                for (size_t k = 0; k < t->params.n; k++)
                    j_push(a, ps, param_value(a, (const char *)t->params.v[k]));
                j_set(a, o, "parameter", ps);
            }
            j_push(a, tgts, o);
        }
        j_set(a, jr, "target", tgts);
    }
    if (r->subrules.n) {
        jval *subs = j_arr(a);
        for (size_t i = 0; i < r->subrules.n; i++)
            j_push(a, subs, rule_resource(a, r->subrules.v[i], (int)i));
        j_set(a, jr, "rule", subs);
    }
    if (r->deps.n) {
        jval *deps = j_arr(a);
        for (size_t i = 0; i < r->deps.n; i++) {
            char *copy = arena_strdup(a, (const char *)r->deps.v[i]);
            char *lp = strchr(copy, '(');
            if (!lp) continue;
            *lp = 0;
            char *rp = strrchr(lp + 1, ')');
            if (rp) *rp = 0;
            jval *o = j_obj(a);
            char *nm = copy;
            while (*nm == ' ') nm++;
            j_set(a, o, "name", j_str(a, nm));
            jval *vs = j_arr(a);
            for (char *tok = strtok(lp + 1, ","); tok; tok = strtok(NULL, ",")) {
                while (*tok == ' ') tok++;
                char *end = tok + strlen(tok);
                while (end > tok && end[-1] == ' ') *--end = 0;
                j_push(a, vs, j_str(a, tok));
            }
            j_set(a, o, "variable", vs);
            j_push(a, deps, o);
        }
        j_set(a, jr, "dependent", deps);
    }
    return jr;
}

jval *fml_to_resource(arena *a, fmlmap *m) {
    jval *sm = j_obj(a);
    j_set(a, sm, "resourceType", j_str(a, "StructureMap"));
    if (m->url) j_set(a, sm, "url", j_str(a, m->url));
    if (m->name) j_set(a, sm, "name", j_str(a, m->name));
    j_set(a, sm, "status", j_str(a, "active"));
    if (m->uses.n) {
        jval *sts = j_arr(a);
        for (size_t i = 0; i < m->uses.n; i++) {
            fml_uses *u = m->uses.v[i];
            jval *o = j_obj(a);
            if (u->url) j_set(a, o, "url", j_str(a, u->url));
            if (u->mode) j_set(a, o, "mode", j_str(a, u->mode));
            if (u->alias) j_set(a, o, "alias", j_str(a, u->alias));
            j_push(a, sts, o);
        }
        j_set(a, sm, "structure", sts);
    }
    if (m->imports.n) {
        jval *im = j_arr(a);
        for (size_t i = 0; i < m->imports.n; i++) j_push(a, im, j_str(a, (const char *)m->imports.v[i]));
        j_set(a, sm, "import", im);
    }
    jval *gs = j_arr(a);
    for (size_t i = 0; i < m->groups.n; i++) {
        fml_group *g = m->groups.v[i];
        jval *o = j_obj(a);
        j_set(a, o, "name", j_str(a, g->name ? g->name : "group"));
        if (g->extends) j_set(a, o, "extends", j_str(a, g->extends));
        j_set(a, o, "typeMode", j_str(a, g->typemode ? g->typemode : "none"));
        jval *ins = j_arr(a);
        for (size_t k = 0; k < g->params.n; k++) {
            char *spec = arena_strdup(a, (const char *)g->params.v[k]);
            char *p1 = strchr(spec, '|');
            char *p2 = p1 ? strchr(p1 + 1, '|') : NULL;
            if (p1) *p1 = 0;
            if (p2) *p2 = 0;
            jval *in = j_obj(a);
            j_set(a, in, "name", j_str(a, p1 ? p1 + 1 : spec));
            if (p2 && p2[1]) j_set(a, in, "type", j_str(a, p2 + 1));
            j_set(a, in, "mode", j_str(a, spec));
            j_push(a, ins, in);
        }
        j_set(a, o, "input", ins);
        jval *rs = j_arr(a);
        for (size_t k = 0; k < g->rules.n; k++) j_push(a, rs, rule_resource(a, g->rules.v[k], (int)k));
        j_set(a, o, "rule", rs);
        j_push(a, gs, o);
    }
    j_set(a, sm, "group", gs);
    return sm;
}
