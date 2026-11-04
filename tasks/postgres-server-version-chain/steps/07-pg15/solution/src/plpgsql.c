/* plpgsql.c - a small PL/pgSQL interpreter.
 *
 * The subset implemented here is the core of the language: a block with
 * optional declarations, assignment, IF/ELSIF/ELSE, the three loop forms,
 * RAISE, RETURN, PERFORM, transaction control inside procedures, and any
 * SQL statement run through the engine's normal path.
 *
 * Values reach SQL the simple way: a reference to a PL/pgSQL variable is
 * replaced by a literal of that variable's value before the statement is
 * handed to the SQL parser.  That keeps the interpreter independent of the
 * executor and is enough for every construct the language offers here.
 */
#include "engine.h"
#include "stmt.h"
#include "lex.h"

/* ------------------------------------------------------------- variables */
typedef struct PlVar {
    char  *name;
    Oid    type;
    Datum  val;
    bool   isrow;              /* NEW / OLD: a whole row of rowtab */
    Table *rowtab;
    Datum *rowvals;
    int    mode;               /* 0 IN, 1 OUT, 2 INOUT */
} PlVar;

/* ------------------------------------------------------------- statements */
typedef enum {
    P_BLOCK = 1, P_ASSIGN, P_IF, P_LOOP, P_FORRANGE, P_WHILE, P_RAISE,
    P_RETURN, P_SQL, P_PERFORM, P_COMMIT, P_ROLLBACK, P_NOP, P_EXIT, P_CONTINUE
} PKind;

typedef struct PStmt PStmt;
typedef struct PDecl { char *name; char *type; char *def; } PDecl;

struct PStmt {
    PKind  kind;
    int    line;
    char  *target;             /* assignment target / loop variable */
    char  *expr;               /* an expression or a whole SQL statement */
    char **conds; PStmt ***arms; int *narm; int nconds;
    PStmt **elsearm; int nelse;
    char  *lo, *hi; bool reverse;
    PStmt **body; int nbody;
    int    level;              /* RAISE: 0 notice 1 warning 2 exception 3 info */
    char  *fmt; char **args; int nargs;
    char  *use_errcode, *use_message, *use_detail, *use_hint;
    char **into; int ninto; bool into_strict;
    PDecl *decls; int ndecls;
    PStmt **stmts; int nstmts;
    char  *when;
};

/* ------------------------------------------------------------- context */
typedef struct PlCtx {
    Session *s;
    PlVar   *vars; int nvars;
    bool     is_proc;          /* transaction control is allowed */
    const char *fname;
    Datum    ret; bool returned;
    int      loopsig;          /* 0 none, 1 EXIT, 2 CONTINUE */
} PlCtx;

/* =========================================================== the parser */
typedef struct { Lexer L; int i; Session *s; } PP;

static Tok *cur(PP *p) { return &p->L.toks[p->i]; }
static bool at_kw(PP *p, const char *kw)
{
    Tok *t = cur(p);
    return t->kind == T_IDENT && !t->quoted && t->sval && !strcmp(t->sval, kw);
}
static bool accept_kw2(PP *p, const char *kw) { if (at_kw(p, kw)) { p->i++; return true; } return false; }
static void expect_kw2(PP *p, const char *kw)
{
    if (!accept_kw2(p, kw))
        ereport(p->s, "42601", "syntax error at or near \"%s\"",
                cur(p)->sval ? cur(p)->sval : "end of body");
}
static int line_of(PP *p, int tok)
{
    int line = 1;
    const char *q = p->L.src, *e = p->L.toks[tok].start;
    while (q < e) { if (*q == '\n') line++; q++; }
    return line;
}
static char *src_range(PP *p, int a, int b)
{
    if (b <= a) return xstrdup("");
    return xstrndup(p->L.toks[a].start, (size_t)(p->L.toks[b-1].end - p->L.toks[a].start));
}
static void spush2(char ***arr, int *n, char *v)
{
    *arr = xrealloc(*arr, (size_t)(*n + 1) * sizeof(char *));
    (*arr)[(*n)++] = v;
}
static void stpush(PStmt ***arr, int *n, PStmt *v)
{
    *arr = xrealloc(*arr, (size_t)(*n + 1) * sizeof(PStmt *));
    (*arr)[(*n)++] = v;
}

static PStmt *pl_parse_stmt(PP *p);
static void pl_parse_body(PP *p, PStmt ***out, int *nout, const char *end1, const char *end2);

/* The index of the next top-level semicolon. */
static int find_semi(PP *p)
{
    int depth = 0, i = p->i;
    while (i < p->L.ntok && p->L.toks[i].kind != T_EOF) {
        if (p->L.toks[i].kind == T_LPAREN) depth++;
        else if (p->L.toks[i].kind == T_RPAREN) depth--;
        else if (p->L.toks[i].kind == T_SEMI && depth <= 0) return i;
        i++;
    }
    return i;
}

static void arg_list(PP *p, int endtok, char ***args, int *nargs)
{
    int depth = 0, start = p->i;
    for (int i = p->i; i < endtok; i++) {
        if (p->L.toks[i].kind == T_LPAREN) depth++;
        else if (p->L.toks[i].kind == T_RPAREN) depth--;
        else if (p->L.toks[i].kind == T_COMMA && depth == 0) {
            spush2(args, nargs, src_range(p, start, i));
            start = i + 1;
        }
    }
    if (endtok > start) spush2(args, nargs, src_range(p, start, endtok));
    p->i = endtok;
}

static PStmt *mkstmt(PKind k, int line)
{
    PStmt *st = xmalloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->kind = k; st->line = line;
    return st;
}

static PStmt *pl_parse_block(PP *p)
{
    PStmt *st = mkstmt(P_BLOCK, line_of(p, p->i));
    if (accept_kw2(p, "declare")) {
        while (!at_kw(p, "begin") && cur(p)->kind != T_EOF) {
            PDecl d; memset(&d, 0, sizeof d);
            if (cur(p)->kind != T_IDENT) { p->i++; continue; }
            d.name = xstrdup(cur(p)->sval);
            p->i++;
            accept_kw2(p, "constant");
            int tstart = p->i;
            int semi = find_semi(p);
            int assign = -1;
            for (int i = p->i; i < semi; i++)
                if (p->L.toks[i].kind == T_OP && p->L.toks[i].sval &&
                    (!strcmp(p->L.toks[i].sval, ":=") || !strcmp(p->L.toks[i].sval, "="))) {
                    assign = i; break;
                }
            if (assign >= 0) {
                d.type = src_range(p, tstart, assign);
                d.def  = src_range(p, assign + 1, semi);
            } else d.type = src_range(p, tstart, semi);
            p->i = semi < p->L.ntok ? semi + 1 : semi;
            st->decls = xrealloc(st->decls, (size_t)(st->ndecls + 1) * sizeof(PDecl));
            st->decls[st->ndecls++] = d;
        }
    }
    expect_kw2(p, "begin");
    pl_parse_body(p, &st->stmts, &st->nstmts, "end", NULL);
    expect_kw2(p, "end");
    if (cur(p)->kind == T_IDENT && !cur(p)->iskw) p->i++;   /* block label */
    if (cur(p)->kind == T_SEMI) p->i++;
    return st;
}

static void pl_parse_body(PP *p, PStmt ***out, int *nout, const char *end1, const char *end2)
{
    for (;;) {
        if (cur(p)->kind == T_EOF) break;
        if (at_kw(p, end1)) break;
        if (end2 && at_kw(p, end2)) break;
        if (cur(p)->kind == T_SEMI) { p->i++; continue; }
        PStmt *st = pl_parse_stmt(p);
        if (st) stpush(out, nout, st);
    }
}

static PStmt *pl_parse_stmt(PP *p)
{
    int line = line_of(p, p->i);

    if (at_kw(p, "begin") || at_kw(p, "declare")) return pl_parse_block(p);

    if (accept_kw2(p, "if")) {
        PStmt *st = mkstmt(P_IF, line);
        for (;;) {
            int c0 = p->i;
            while (cur(p)->kind != T_EOF && !at_kw(p, "then")) p->i++;
            spush2(&st->conds, &st->nconds, src_range(p, c0, p->i));
            expect_kw2(p, "then");
            st->arms = xrealloc(st->arms, (size_t)st->nconds * sizeof(PStmt **));
            st->narm = xrealloc(st->narm, (size_t)st->nconds * sizeof(int));
            st->arms[st->nconds-1] = NULL; st->narm[st->nconds-1] = 0;
            for (;;) {
                if (cur(p)->kind == T_EOF) break;
                if (at_kw(p, "elsif") || at_kw(p, "elseif") || at_kw(p, "else") ||
                    at_kw(p, "end")) break;
                if (cur(p)->kind == T_SEMI) { p->i++; continue; }
                PStmt *b = pl_parse_stmt(p);
                if (b) stpush(&st->arms[st->nconds-1], &st->narm[st->nconds-1], b);
            }
            if (accept_kw2(p, "elsif") || accept_kw2(p, "elseif")) continue;
            break;
        }
        if (accept_kw2(p, "else"))
            pl_parse_body(p, &st->elsearm, &st->nelse, "end", NULL);
        expect_kw2(p, "end");
        expect_kw2(p, "if");
        if (cur(p)->kind == T_SEMI) p->i++;
        return st;
    }

    if (accept_kw2(p, "loop")) {
        PStmt *st = mkstmt(P_LOOP, line);
        pl_parse_body(p, &st->body, &st->nbody, "end", NULL);
        expect_kw2(p, "end"); expect_kw2(p, "loop");
        if (cur(p)->kind == T_SEMI) p->i++;
        return st;
    }

    if (accept_kw2(p, "while")) {
        PStmt *st = mkstmt(P_WHILE, line);
        int c0 = p->i;
        while (cur(p)->kind != T_EOF && !at_kw(p, "loop")) p->i++;
        st->expr = src_range(p, c0, p->i);
        expect_kw2(p, "loop");
        pl_parse_body(p, &st->body, &st->nbody, "end", NULL);
        expect_kw2(p, "end"); expect_kw2(p, "loop");
        if (cur(p)->kind == T_SEMI) p->i++;
        return st;
    }

    if (accept_kw2(p, "for")) {
        PStmt *st = mkstmt(P_FORRANGE, line);
        st->target = xstrdup(cur(p)->sval ? cur(p)->sval : "i");
        p->i++;
        expect_kw2(p, "in");
        st->reverse = accept_kw2(p, "reverse");
        int a0 = p->i;
        /* the ".." separator: the SQL lexer may fold the second dot into the
         * upper bound when it is a bare number, so handle both shapes */
        while (cur(p)->kind != T_EOF && cur(p)->kind != T_DOT) p->i++;
        st->lo = src_range(p, a0, p->i);
        p->i++;
        const char *hbegin = NULL;
        if (cur(p)->kind == T_DOT) p->i++;
        else if (cur(p)->kind == T_NUMBER && cur(p)->start[0] == '.') hbegin = cur(p)->start + 1;
        int b0 = p->i;
        while (cur(p)->kind != T_EOF && !at_kw(p, "loop") && !at_kw(p, "by")) p->i++;
        if (hbegin && p->i > b0)
            st->hi = xstrndup(hbegin, (size_t)(p->L.toks[p->i-1].end - hbegin));
        else st->hi = src_range(p, b0, p->i);
        if (accept_kw2(p, "by")) { while (cur(p)->kind != T_EOF && !at_kw(p, "loop")) p->i++; }
        expect_kw2(p, "loop");
        pl_parse_body(p, &st->body, &st->nbody, "end", NULL);
        expect_kw2(p, "end"); expect_kw2(p, "loop");
        if (cur(p)->kind == T_SEMI) p->i++;
        return st;
    }

    if (accept_kw2(p, "raise")) {
        PStmt *st = mkstmt(P_RAISE, line);
        if (accept_kw2(p, "notice")) st->level = 0;
        else if (accept_kw2(p, "warning")) st->level = 1;
        else if (accept_kw2(p, "exception")) st->level = 2;
        else if (accept_kw2(p, "info") || accept_kw2(p, "log") || accept_kw2(p, "debug"))
            st->level = 3;
        int semi = find_semi(p);
        int usetok = -1;
        for (int i = p->i; i < semi; i++)
            if (p->L.toks[i].kind == T_IDENT && p->L.toks[i].sval &&
                !strcmp(p->L.toks[i].sval, "using")) { usetok = i; break; }
        int msgend = (usetok >= 0) ? usetok : semi;
        if (cur(p)->kind == T_STRING && p->i < msgend) {
            st->fmt = xstrdup(cur(p)->sval);
            p->i++;
            if (cur(p)->kind == T_COMMA && p->i < msgend) {
                p->i++;
                arg_list(p, msgend, &st->args, &st->nargs);
            }
        } else if (p->i < msgend) st->fmt = src_range(p, p->i, msgend);
        p->i = msgend;
        if (usetok >= 0) {
            p->i = usetok + 1;
            while (p->i < semi) {
                if (cur(p)->kind == T_IDENT && cur(p)->sval) {
                    char *key = xstrdup(cur(p)->sval);
                    p->i++;
                    if (cur(p)->kind == T_OP && cur(p)->sval && !strcmp(cur(p)->sval, "=")) p->i++;
                    int v0 = p->i;
                    while (p->i < semi && cur(p)->kind != T_COMMA) p->i++;
                    char *val = src_range(p, v0, p->i);
                    if (!strcmp(key, "errcode")) st->use_errcode = val;
                    else if (!strcmp(key, "message")) st->use_message = val;
                    else if (!strcmp(key, "detail")) st->use_detail = val;
                    else if (!strcmp(key, "hint")) st->use_hint = val;
                    else free(val);
                    free(key);
                }
                if (cur(p)->kind == T_COMMA) p->i++; else break;
            }
        }
        p->i = semi < p->L.ntok ? semi + 1 : semi;
        return st;
    }

    if (accept_kw2(p, "return")) {
        PStmt *st = mkstmt(P_RETURN, line);
        int semi = find_semi(p);
        if (semi > p->i) st->expr = src_range(p, p->i, semi);
        p->i = semi < p->L.ntok ? semi + 1 : semi;
        return st;
    }

    if (accept_kw2(p, "perform")) {
        PStmt *st = mkstmt(P_PERFORM, line);
        int semi = find_semi(p);
        st->expr = src_range(p, p->i, semi);
        p->i = semi < p->L.ntok ? semi + 1 : semi;
        return st;
    }

    if (at_kw(p, "commit") || at_kw(p, "rollback")) {
        PStmt *st = mkstmt(at_kw(p, "commit") ? P_COMMIT : P_ROLLBACK, line);
        int semi = find_semi(p);
        p->i = semi < p->L.ntok ? semi + 1 : semi;
        return st;
    }

    if (at_kw(p, "exit") || at_kw(p, "continue")) {
        PStmt *st = mkstmt(at_kw(p, "exit") ? P_EXIT : P_CONTINUE, line);
        p->i++;
        int semi = find_semi(p);
        for (int i = p->i; i < semi; i++)
            if (p->L.toks[i].kind == T_IDENT && p->L.toks[i].sval &&
                !strcmp(p->L.toks[i].sval, "when")) { st->when = src_range(p, i + 1, semi); break; }
        p->i = semi < p->L.ntok ? semi + 1 : semi;
        return st;
    }

    if (at_kw(p, "null") && p->L.toks[p->i+1].kind == T_SEMI) {
        p->i += 2;
        return mkstmt(P_NOP, line);
    }

    if (cur(p)->kind == T_IDENT) {
        int j = p->i + 1;
        if (p->L.toks[j].kind == T_DOT) j += 2;
        if (p->L.toks[j].kind == T_OP && p->L.toks[j].sval && !strcmp(p->L.toks[j].sval, ":=")) {
            PStmt *st = mkstmt(P_ASSIGN, line);
            st->target = src_range(p, p->i, j);
            p->i = j + 1;
            int semi = find_semi(p);
            st->expr = src_range(p, p->i, semi);
            p->i = semi < p->L.ntok ? semi + 1 : semi;
            return st;
        }
    }

    {   /* anything else is a SQL statement */
        PStmt *st = mkstmt(P_SQL, line);
        int semi = find_semi(p);
        int intotok = -1;
        if (at_kw(p, "select")) {
            int depth = 0;
            for (int i = p->i; i < semi; i++) {
                if (p->L.toks[i].kind == T_LPAREN) depth++;
                else if (p->L.toks[i].kind == T_RPAREN) depth--;
                else if (depth == 0 && p->L.toks[i].kind == T_IDENT && p->L.toks[i].sval &&
                         !strcmp(p->L.toks[i].sval, "into")) { intotok = i; break; }
            }
        }
        if (intotok >= 0) {
            int k = intotok + 1;
            if (p->L.toks[k].kind == T_IDENT && p->L.toks[k].sval &&
                !strcmp(p->L.toks[k].sval, "strict")) { st->into_strict = true; k++; }
            /* the target list is "name [, name]*": stop at the first name that
             * is not preceded by a comma, or the rest of the query is eaten */
            int vend = k;
            for (;;) {
                if (vend >= semi || p->L.toks[vend].kind != T_IDENT) break;
                Buf nb; buf_init(&nb);
                buf_adds(&nb, p->L.toks[vend].sval ? p->L.toks[vend].sval : "");
                vend++;
                while (vend + 1 < semi && p->L.toks[vend].kind == T_DOT &&
                       p->L.toks[vend+1].kind == T_IDENT) {
                    buf_addc(&nb, '.');
                    buf_adds(&nb, p->L.toks[vend+1].sval ? p->L.toks[vend+1].sval : "");
                    vend += 2;
                }
                spush2(&st->into, &st->ninto, buf_steal(&nb));
                if (vend < semi && p->L.toks[vend].kind == T_COMMA) { vend++; continue; }
                break;
            }
            Buf b; buf_init(&b);
            char *head = src_range(p, p->i, intotok);
            char *tail = src_range(p, vend, semi);
            buf_adds(&b, head); buf_addc(&b, ' '); buf_adds(&b, tail);
            st->expr = buf_steal(&b);
            free(head); free(tail);
        } else st->expr = src_range(p, p->i, semi);
        p->i = semi < p->L.ntok ? semi + 1 : semi;
        if (!st->expr || !*st->expr) st->kind = P_NOP;
        return st;
    }
}

/* ======================================================= value plumbing */
static PlVar *pl_find(PlCtx *c, const char *name)
{
    for (int i = c->nvars - 1; i >= 0; i--)
        if (!strcasecmp(c->vars[i].name, name)) return &c->vars[i];
    return NULL;
}

static void pl_literal(Session *s, Datum d, Buf *b)
{
    if (d.isnull) { buf_adds(b, "NULL"); return; }
    switch (d.type) {
    case BOOLOID: buf_adds(b, d.v.i ? "true" : "false"); return;
    case INT2OID: case INT4OID: case INT8OID: case OIDOID:
    case FLOAT4OID: case FLOAT8OID: case NUMERICOID: {
        char *t = out_func(s, d);
        if (*t == '-') buf_addc(b, '(');
        buf_adds(b, t);
        if (*t == '-') buf_addc(b, ')');
        free(t);
        return;
    }
    default: break;
    }
    char *t = out_func(s, d);
    buf_addc(b, '\'');
    for (char *q = t; *q; q++) { if (*q == '\'') buf_addc(b, '\''); buf_addc(b, *q); }
    buf_addc(b, '\'');
    if (d.type && d.type != UNKNOWNOID) {
        char *ty = format_type_str(d.type, -1);
        buf_printf(b, "::%s", ty);
        free(ty);
    }
    free(t);
}

/* Replace every reference to a PL/pgSQL variable in src with its value. */
static char *pl_subst(PlCtx *c, const char *src)
{
    Lexer L; lex_init(&L, c->s, src);
    lex_all(&L);
    Buf out; buf_init(&out);
    const char *last = src;
    for (int i = 0; i < L.ntok && L.toks[i].kind != T_EOF; i++) {
        Tok *t = &L.toks[i];
        if (t->kind != T_IDENT || t->quoted || !t->sval) continue;
        if (i > 0 && L.toks[i-1].kind == T_DOT) continue;
        PlVar *v = pl_find(c, t->sval);
        if (!v) continue;
        if (v->isrow) {
            if (L.toks[i+1].kind != T_DOT || L.toks[i+2].kind != T_IDENT) continue;
            const char *fld = L.toks[i+2].sval;
            int ci = (v->rowtab && fld) ? col_index(v->rowtab, fld) : -1;
            buf_add(&out, last, (size_t)(t->start - last));
            if (!v->rowvals || ci < 0) buf_adds(&out, "NULL");
            else pl_literal(c->s, v->rowvals[ci], &out);
            last = L.toks[i+2].end;
            i += 2;
            continue;
        }
        buf_add(&out, last, (size_t)(t->start - last));
        pl_literal(c->s, v->val, &out);
        last = t->end;
    }
    buf_adds(&out, last);
    return buf_steal(&out);
}

static Datum pl_eval(PlCtx *c, const char *expr)
{
    char *sub = pl_subst(c, expr);
    Buf b; buf_init(&b);
    buf_printf(&b, "SELECT %s", sub);
    free(sub);
    char *sql = buf_steal(&b);
    QueryResult *q = exec_sql(c->s, sql);
    free(sql);
    if (!q || q->nrows == 0 || q->ncols == 0) return d_null(TEXTOID);
    return q->rows[0][0];
}

static bool pl_eval_bool(PlCtx *c, const char *expr)
{
    Datum d = pl_eval(c, expr);
    if (d.isnull) return false;
    if (d.type == BOOLOID) return d.v.i != 0;
    Datum b = coerce_to(c->s, d, BOOLOID, -1, 2);
    return !b.isnull && b.v.i != 0;
}

/* ============================================================= execution */
void xact_begin(Session *s, int isolation, int readonly);
void xact_commit(Session *s);
void xact_rollback(Session *s);
void catalog_undo_begin(void);
void catalog_undo_clear(void);
void catalog_undo_apply(void);

static void pl_exec_list(PlCtx *c, PStmt **list, int n);

static void pl_txn_end(PlCtx *c, bool commit, PStmt *st)
{
    Session *s = c->s;
    if (!c->is_proc || s->xact_state != 0) {
        err_context(s, "PL/pgSQL function %s line %d at %s",
                    c->fname, st->line, commit ? "COMMIT" : "ROLLBACK");
        ereport(s, "2D000", "invalid transaction termination");
    }
    if (commit) { catalog_undo_clear(); xact_commit(s); }
    else { catalog_undo_apply(); xact_rollback(s); }
    xact_begin(s, s->isolation, false);
    s->xact_state = 0;
    catalog_undo_begin();
}

static void pl_raise(PlCtx *c, PStmt *st)
{
    Session *s = c->s;
    Buf b; buf_init(&b);
    int ai = 0;
    if (st->fmt) {
        for (const char *q = st->fmt; *q; q++) {
            if (*q == '%' && q[1] == '%') { buf_addc(&b, '%'); q++; continue; }
            if (*q == '%') {
                if (ai < st->nargs) {
                    Datum d = pl_eval(c, st->args[ai++]);
                    if (d.isnull) buf_adds(&b, "<NULL>");
                    else { char *t = out_func(s, d); buf_adds(&b, t); free(t); }
                }
                continue;
            }
            buf_addc(&b, *q);
        }
    }
    char *msg = buf_steal(&b);
    if (st->use_message) {
        Datum d = pl_eval(c, st->use_message);
        if (!d.isnull) { free(msg); msg = out_func(s, d); }
    }
    char code[8] = "P0001";
    if (st->use_errcode) {
        Datum d = pl_eval(c, st->use_errcode);
        if (!d.isnull) { char *t = out_func(s, d); snprintf(code, sizeof code, "%s", t); free(t); }
    }
    if (st->level == 2) {
        if (st->use_detail) { Datum d = pl_eval(c, st->use_detail);
            if (!d.isnull) { char *t = out_func(s, d); err_detail(s, "%s", t); free(t); } }
        if (st->use_hint) { Datum d = pl_eval(c, st->use_hint);
            if (!d.isnull) { char *t = out_func(s, d); err_hint(s, "%s", t); free(t); } }
        ereport(s, code, "%s", msg);
    }
    if (st->level == 1) warning(s, "01000", "%s", msg);
    else notice(s, "00000", "%s", msg);
    free(msg);
}

static void pl_assign(PlCtx *c, const char *target, Datum v)
{
    const char *dot = strchr(target, '.');
    if (dot) {
        char *base = xstrndup(target, (size_t)(dot - target));
        PlVar *pv = pl_find(c, base);
        free(base);
        if (pv && pv->isrow && pv->rowtab && pv->rowvals) {
            int ci = col_index(pv->rowtab, dot + 1);
            if (ci >= 0)
                pv->rowvals[ci] = coerce_to(c->s, v, pv->rowtab->cols[ci].type,
                                            pv->rowtab->cols[ci].typmod, 1);
        }
        return;
    }
    PlVar *pv = pl_find(c, target);
    if (!pv) ereport(c->s, "42703", "\"%s\" is not a known variable", target);
    pv->val = pv->type ? coerce_to(c->s, v, pv->type, -1, 1) : v;
}

static void pl_exec_one(PlCtx *c, PStmt *st)
{
    Session *s = c->s;
    check_cancel(s);
    switch (st->kind) {
    case P_NOP: return;
    case P_BLOCK: {
        int save = c->nvars;
        for (int i = 0; i < st->ndecls; i++) {
            Oid ty = type_by_name(st->decls[i].type);
            c->vars = xrealloc(c->vars, (size_t)(c->nvars + 1) * sizeof(PlVar));
            PlVar *v = &c->vars[c->nvars++];
            memset(v, 0, sizeof *v);
            v->name = st->decls[i].name;
            v->type = ty;
            v->val = d_null(ty ? ty : TEXTOID);
        }
        for (int i = 0; i < st->ndecls; i++)
            if (st->decls[i].def)
                pl_assign(c, st->decls[i].name, pl_eval(c, st->decls[i].def));
        pl_exec_list(c, st->stmts, st->nstmts);
        if (!c->returned && !c->loopsig) c->nvars = save;
        return;
    }
    case P_ASSIGN: pl_assign(c, st->target, pl_eval(c, st->expr)); return;
    case P_IF:
        for (int i = 0; i < st->nconds; i++)
            if (pl_eval_bool(c, st->conds[i])) { pl_exec_list(c, st->arms[i], st->narm[i]); return; }
        pl_exec_list(c, st->elsearm, st->nelse);
        return;
    case P_LOOP:
        for (;;) {
            pl_exec_list(c, st->body, st->nbody);
            if (c->returned) return;
            if (c->loopsig == 1) { c->loopsig = 0; break; }
            if (c->loopsig == 2) c->loopsig = 0;
        }
        return;
    case P_WHILE:
        while (pl_eval_bool(c, st->expr)) {
            pl_exec_list(c, st->body, st->nbody);
            if (c->returned) return;
            if (c->loopsig == 1) { c->loopsig = 0; break; }
            if (c->loopsig == 2) c->loopsig = 0;
        }
        return;
    case P_FORRANGE: {
        Datum lo = pl_eval(c, st->lo), hi = pl_eval(c, st->hi);
        int64 a = lo.isnull ? 0 : coerce_to(s, lo, INT8OID, -1, 1).v.i;
        int64 b = hi.isnull ? 0 : coerce_to(s, hi, INT8OID, -1, 1).v.i;
        int save = c->nvars;
        if (!pl_find(c, st->target)) {
            c->vars = xrealloc(c->vars, (size_t)(c->nvars + 1) * sizeof(PlVar));
            PlVar *nv = &c->vars[c->nvars++];
            memset(nv, 0, sizeof *nv);
            nv->name = st->target; nv->type = INT4OID;
        }
        int64 from = a, to = b, step = st->reverse ? -1 : 1;
        for (int64 k = from; st->reverse ? k >= to : k <= to; k += step) {
            PlVar *v = pl_find(c, st->target);
            v->val = d_int(v->type ? v->type : INT4OID, k);
            pl_exec_list(c, st->body, st->nbody);
            if (c->returned) return;
            if (c->loopsig == 1) { c->loopsig = 0; break; }
            if (c->loopsig == 2) c->loopsig = 0;
        }
        if (c->nvars > save) c->nvars = save;
        return;
    }
    case P_EXIT:
        if (st->when && !pl_eval_bool(c, st->when)) return;
        c->loopsig = 1; return;
    case P_CONTINUE:
        if (st->when && !pl_eval_bool(c, st->when)) return;
        c->loopsig = 2; return;
    case P_RAISE: pl_raise(c, st); return;
    case P_RETURN: {
        if (st->expr && *st->expr) {
            /* RETURN NEW / RETURN OLD hands back the trigger row itself */
            char *t = st->expr;
            while (*t == ' ') t++;
            PlVar *rv = pl_find(c, t);
            if (rv && rv->isrow) {
                c->ret = rv->rowvals ? d_int(RECORDOID, 1) : d_null(RECORDOID);
                c->returned = true;
                return;
            }
            c->ret = pl_eval(c, st->expr);
        } else c->ret = d_null(VOIDOID);
        c->returned = true;
        return;
    }
    case P_COMMIT:   pl_txn_end(c, true, st); return;
    case P_ROLLBACK: pl_txn_end(c, false, st); return;
    case P_PERFORM: {
        char *sub = pl_subst(c, st->expr);
        Buf b; buf_init(&b);
        buf_printf(&b, "SELECT %s", sub);
        free(sub);
        char *sql = buf_steal(&b);
        exec_sql(s, sql);
        free(sql);
        return;
    }
    case P_SQL: {
        char *sql = pl_subst(c, st->expr);
        QueryResult *q = exec_sql(s, sql);
        free(sql);
        if (st->ninto && q && q->ncols)
            for (int i = 0; i < st->ninto; i++) {
                Datum v = (q->nrows > 0 && i < q->ncols) ? q->rows[0][i] : d_null(TEXTOID);
                pl_assign(c, st->into[i], v);
            }
        return;
    }
    }
}

static void pl_exec_list(PlCtx *c, PStmt **list, int n)
{
    for (int i = 0; i < n; i++) {
        pl_exec_one(c, list[i]);
        if (c->returned || c->loopsig) return;
    }
}

/* ================================================================= entry */
static void pl_add_var(PlCtx *c, const char *name, Oid type, Datum val)
{
    c->vars = xrealloc(c->vars, (size_t)(c->nvars + 1) * sizeof(PlVar));
    PlVar *v = &c->vars[c->nvars++];
    memset(v, 0, sizeof *v);
    v->name = xstrdup(name); v->type = type; v->val = val;
}

static void pl_add_row(PlCtx *c, const char *name, Table *t, Datum *vals)
{
    c->vars = xrealloc(c->vars, (size_t)(c->nvars + 1) * sizeof(PlVar));
    PlVar *v = &c->vars[c->nvars++];
    memset(v, 0, sizeof *v);
    v->name = xstrdup(name); v->isrow = true; v->rowtab = t; v->rowvals = vals;
}

/* PostgreSQL names the routine with its argument list in the CONTEXT line. */
static char *pl_signature(Session *s, SqlRoutine *p)
{
    extern char *routine_sig(Session *s, const Oid *types, int n, bool internal);
    if (!p) return xstrdup("inline_code_block");
    char *a = routine_sig(s, p->argtypes, p->nargs, false);
    Buf b; buf_init(&b);
    buf_printf(&b, "%s(%s)", p->name, a);
    free(a);
    return buf_steal(&b);
}

/* A procedure may not RETURN a value; PostgreSQL rejects it at CREATE time. */
static int pl_find_return_expr(PStmt **list, int n, int *line);

static int pl_scan_return(PStmt *st, int *line)
{
    if (!st) return 0;
    if (st->kind == P_RETURN && st->expr && *st->expr) { *line = st->line; return 1; }
    if (st->kind == P_BLOCK) return pl_find_return_expr(st->stmts, st->nstmts, line);
    if (st->kind == P_IF) {
        for (int i = 0; i < st->nconds; i++)
            if (pl_find_return_expr(st->arms[i], st->narm[i], line)) return 1;
        return pl_find_return_expr(st->elsearm, st->nelse, line);
    }
    return pl_find_return_expr(st->body, st->nbody, line);
}

static int pl_find_return_expr(PStmt **list, int n, int *line)
{
    for (int i = 0; i < n; i++) if (pl_scan_return(list[i], line)) return 1;
    return 0;
}

/* Returns the offset of a value-returning RETURN inside body, or -1. */
int plpgsql_bad_proc_return(Session *s, const char *body)
{
    PP pp; memset(&pp, 0, sizeof pp);
    pp.s = s;
    lex_init(&pp.L, s, body);
    lex_all(&pp.L);
    for (int i = 0; i < pp.L.ntok; i++) {
        Tok *t = &pp.L.toks[i];
        if (t->kind != T_IDENT || !t->sval || strcmp(t->sval, "return")) continue;
        Tok *nx = &pp.L.toks[i+1];
        if (nx->kind == T_SEMI || nx->kind == T_EOF) continue;
        if (nx->kind == T_IDENT && nx->sval &&
            (!strcmp(nx->sval, "next") || !strcmp(nx->sval, "query"))) continue;
        /* PostgreSQL points at the expression, not at RETURN */
        return (int)(nx->start - body);
    }
    return -1;
}

Datum plpgsql_run(Session *s, SqlRoutine *p, Datum *args, int nargs,
                  PlTrigCtx *trig, Datum **outvals, int *noutvals)
{
    PlCtx c; memset(&c, 0, sizeof c);
    c.s = s;
    c.is_proc = (p && p->prokind == 'p');
    c.fname = p ? pl_signature(s, p) : "inline_code_block";
    c.ret = d_null(p ? p->rettype : VOIDOID);

    if (p)
        for (int i = 0; i < p->nargs; i++) {
            char nm[64];
            if (p->argnames && p->argnames[i] && *p->argnames[i])
                snprintf(nm, sizeof nm, "%s", p->argnames[i]);
            else snprintf(nm, sizeof nm, "$%d", i + 1);
            Datum v = (i < nargs) ? args[i] : d_null(p->argtypes[i]);
            pl_add_var(&c, nm, p->argtypes[i], v);
            c.vars[c.nvars-1].mode = (p->argmodes && (p->argmodes[i] == 'o' ||
                                                      p->argmodes[i] == 'b')) ? 1 : 0;
        }
    if (trig) {
        pl_add_row(&c, "new", trig->table, trig->newrow);
        pl_add_row(&c, "old", trig->table, trig->oldrow);
        pl_add_var(&c, "tg_op", TEXTOID, d_text(TEXTOID, trig->op));
        pl_add_var(&c, "tg_name", NAMEOID, d_text(NAMEOID, trig->name));
        pl_add_var(&c, "tg_when", TEXTOID, d_text(TEXTOID, trig->when));
        pl_add_var(&c, "tg_level", TEXTOID, d_text(TEXTOID, trig->level));
        pl_add_var(&c, "tg_table_name", NAMEOID,
                   d_text(NAMEOID, trig->table ? trig->table->name : ""));
        pl_add_var(&c, "tg_relname", NAMEOID,
                   d_text(NAMEOID, trig->table ? trig->table->name : ""));
        pl_add_var(&c, "tg_table_schema", NAMEOID,
                   d_text(NAMEOID, trig->table ? trig->table->schema : "public"));
        pl_add_var(&c, "tg_nargs", INT4OID, d_int(INT4OID, 0));
    }

    PP pp; memset(&pp, 0, sizeof pp);
    pp.s = s;
    lex_init(&pp.L, s, p ? p->body : "");
    lex_all(&pp.L);
    PStmt *blk = pl_parse_block(&pp);
    pl_exec_one(&c, blk);          /* the outer block may DECLARE variables */

    if (outvals && noutvals) {
        int n = 0;
        for (int i = 0; p && i < p->nargs; i++) if (c.vars[i].mode) n++;
        if (n) {
            Datum *o = xmalloc((size_t)n * sizeof(Datum));
            int k = 0;
            for (int i = 0; i < p->nargs; i++) if (c.vars[i].mode) o[k++] = c.vars[i].val;
            *outvals = o; *noutvals = n;
        } else { *outvals = NULL; *noutvals = 0; }
    }
    return c.ret;
}

void plpgsql_do(Session *s, const char *body)
{
    PlCtx c; memset(&c, 0, sizeof c);
    c.s = s;
    c.is_proc = true;                 /* a DO block may control transactions */
    c.fname = "inline_code_block";

    PP pp; memset(&pp, 0, sizeof pp);
    pp.s = s;
    lex_init(&pp.L, s, body);
    lex_all(&pp.L);
    PStmt *blk = pl_parse_block(&pp);
    pl_exec_one(&c, blk);          /* the outer block may DECLARE variables */
}
