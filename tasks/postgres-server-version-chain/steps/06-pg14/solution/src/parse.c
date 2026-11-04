/* parse.c - recursive-descent SQL parser producing the trees in stmt.h.
 * Operator precedence follows the table in the PostgreSQL manual's
 * "Operator Precedence" section. */
#include "engine.h"
#include "lex.h"
#include "stmt.h"

typedef struct {
    Session *s;
    Lexer    L;
    int      i;
    const char *src;
    /* RETURNING WITH (OLD AS x, NEW AS y): the names the row versions go by */
    char    *ret_oldalias, *ret_newalias;
    bool     ret_aliases;
    char    *ret_relname;
} P;

#define CUR(p)   ((p)->L.toks[(p)->i])
#define PEEK(p,k) ((p)->L.toks[(p)->i + (k) < (p)->L.ntok ? (p)->i + (k) : (p)->L.ntok - 1])

static void perr(P *p, const char *fmt, ...) __attribute__((format(printf,2,3), noreturn));
static void perr(P *p, const char *fmt, ...)
{
    char msg[512];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    ereport_pos(p->s, "42601", CUR(p).pos, "%s", msg);
}

static void syntax_error(P *p) __attribute__((noreturn));
static void syntax_error(P *p)
{
    Tok t = CUR(p);
    if (t.kind == T_EOF)
        ereport_pos(p->s, "42601", t.pos, "syntax error at end of input");
    size_t n = (size_t)(t.end - t.start);
    char buf[128];
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, t.start, n); buf[n] = '\0';
    ereport_pos(p->s, "42601", t.pos, "syntax error at or near \"%s\"", buf);
}

static bool is_kw(P *p, int k, const char *kw)
{
    Tok t = PEEK(p, k);
    return t.kind == T_IDENT && !t.quoted && t.sval && !strcmp(t.sval, kw);
}
static bool at(P *p, const char *kw)  { return is_kw(p, 0, kw); }
static bool at2(P *p, const char *a, const char *b) { return is_kw(p,0,a) && is_kw(p,1,b); }
static bool accept_kw(P *p, const char *kw) { if (at(p, kw)) { p->i++; return true; } return false; }
static void expect_kw(P *p, const char *kw) { if (!accept_kw(p, kw)) syntax_error(p); }
static bool at_kind(P *p, TokKind k) { return CUR(p).kind == k; }
static bool accept_kind(P *p, TokKind k) { if (CUR(p).kind == k) { p->i++; return true; } return false; }
static void expect_kind(P *p, TokKind k) { if (!accept_kind(p, k)) syntax_error(p); }
static bool at_op(P *p, const char *o) { return CUR(p).kind == T_OP && !strcmp(CUR(p).sval, o); }
static bool accept_op(P *p, const char *o) { if (at_op(p, o)) { p->i++; return true; } return false; }

/* An identifier usable as a name: a non-reserved key word counts. */
static char *ident(P *p)
{
    Tok t = CUR(p);
    if (t.kind != T_IDENT) syntax_error(p);
    if (!t.quoted && t.iskw && kw_is_reserved(t.sval)) syntax_error(p);
    p->i++;
    return xstrdup(t.sval);
}
static char *ident_any(P *p)      /* also accepts reserved words (column labels) */
{
    Tok t = CUR(p);
    if (t.kind != T_IDENT) syntax_error(p);
    p->i++;
    return xstrdup(t.sval);
}
static bool at_ident(P *p)
{
    Tok t = CUR(p);
    return t.kind == T_IDENT && (t.quoted || !t.iskw || !kw_is_reserved(t.sval));
}

/* schema-qualified name */
static void qname(P *p, char **schema, char **name)
{
    char *a = ident(p);
    if (at_kind(p, T_DOT)) {
        p->i++;
        char *b = ident_any(p);
        if (at_kind(p, T_DOT)) { p->i++; free(a); a = b; b = ident_any(p); }
        *schema = a; *name = b;
    } else { *schema = NULL; *name = a; }
}

static Expr *newx(ExprTag tag, int pos)
{
    Expr *e = xmalloc(sizeof *e);
    memset(e, 0, sizeof *e);
    e->tag = tag; e->pos = pos;
    return e;
}
static void xpush(Expr ***arr, int *n, Expr *e)
{
    *arr = xrealloc(*arr, (size_t)(*n + 1) * sizeof(Expr *));
    (*arr)[(*n)++] = e;
}
static void spush(char ***arr, int *n, char *s)
{
    *arr = xrealloc(*arr, (size_t)(*n + 1) * sizeof(char *));
    (*arr)[(*n)++] = s;
}

static Expr *parse_expr(P *p);
static Expr *parse_a_expr(P *p, int prec);
static Node *parse_select_stmt(P *p, CteDef **ctes, int nctes);
static Node *parse_stmt(P *p);
static RangeTbl *parse_from_item(P *p);

/* ------------------------------------------------------------ type names */
static char *type_name_text(P *p, Oid *oid, int32 *typmod)
{
    Buf b; buf_init(&b);
    *typmod = -1;
    bool seen = false;
    /* multi-word built-ins */
    static const char *two[][2] = {
        {"double","precision"},{"character","varying"},{"bit","varying"},
        {"time","with"},{"time","without"},{"timestamp","with"},{"timestamp","without"}, {NULL,NULL}};
    if (accept_kw(p, "setof")) { /* ignore for our purposes */ }
    if (at(p, "double") && is_kw(p, 1, "precision")) { p->i += 2; buf_adds(&b, "double precision"); seen = true; }
    else if (at(p, "character") && is_kw(p, 1, "varying")) { p->i += 2; buf_adds(&b, "varchar"); seen = true; }
    else if (at(p, "bit") && is_kw(p, 1, "varying")) { p->i += 2; buf_adds(&b, "varbit"); seen = true; }
    else if ((at(p, "time") || at(p, "timestamp")) &&
             (is_kw(p, 1, "with") || is_kw(p, 1, "without"))) {
        bool ts = at(p, "timestamp");
        bool with = is_kw(p, 1, "with");
        p->i += 2;
        expect_kw(p, "time"); expect_kw(p, "zone");
        buf_adds(&b, ts ? (with ? "timestamptz" : "timestamp") : (with ? "timetz" : "time"));
        seen = true;
    }
    else if (at(p, "national") && is_kw(p, 1, "character")) { p->i += 2; buf_adds(&b, "bpchar"); seen = true; }
    (void)two;
    if (!seen) {
        char *sch = NULL, *nm = NULL;
        qname(p, &sch, &nm);
        if (sch && !strcmp(sch, "pg_catalog")) { free(sch); sch = NULL; }
        if (sch) { buf_adds(&b, sch); buf_addc(&b, '.'); }
        buf_adds(&b, nm);
        free(sch); free(nm);
    }
    /* typmod */
    if (at_kind(p, T_LPAREN)) {
        const char *nm = b.data;
        if (!strcmp(nm,"numeric") || !strcmp(nm,"decimal") || !strcmp(nm,"varchar") ||
            !strcmp(nm,"char") || !strcmp(nm,"bpchar") || !strcmp(nm,"character") ||
            !strcmp(nm,"bit") || !strcmp(nm,"varbit") || !strcmp(nm,"time") ||
            !strcmp(nm,"timetz") || !strcmp(nm,"timestamp") || !strcmp(nm,"timestamptz") ||
            !strcmp(nm,"interval") || !strcmp(nm,"float")) {
            p->i++;
            int a = -1, bb = -1;
            if (CUR(p).kind == T_NUMBER) { a = atoi(CUR(p).sval); p->i++; }
            else syntax_error(p);
            if (accept_kind(p, T_COMMA)) {
                if (CUR(p).kind == T_NUMBER) { bb = atoi(CUR(p).sval); p->i++; }
                else syntax_error(p);
            }
            expect_kind(p, T_RPAREN);
            if (!strcmp(nm,"numeric") || !strcmp(nm,"decimal"))
                *typmod = ((a << 16) | (bb < 0 ? 0 : bb)) + 4;
            else if (!strcmp(nm,"float"))
                { buf_reset(&b); buf_adds(&b, a <= 24 ? "float4" : "float8"); }
            else *typmod = a + 4;
        }
    }
    /* array suffix */
    int arrdim = 0;
    while (at_kind(p, T_LBRACK)) {
        p->i++;
        if (CUR(p).kind == T_NUMBER) p->i++;
        expect_kind(p, T_RBRACK);
        arrdim++;
    }
    if (accept_kw(p, "array")) {
        if (at_kind(p, T_LBRACK)) { p->i++; if (CUR(p).kind == T_NUMBER) p->i++; expect_kind(p, T_RBRACK); }
        arrdim++;
    }
    char *tn = buf_steal(&b);
    if (arrdim) {
        Buf c; buf_init(&c); buf_adds(&c, "_"); buf_adds(&c, tn); free(tn); tn = buf_steal(&c);
    }
    if (oid) *oid = type_by_name(tn);
    return tn;
}

/* ------------------------------------------------------------ expressions */

static SortItem *parse_sortlist(P *p, int *n)
{
    SortItem *list = NULL; int cnt = 0;
    for (;;) {
        SortItem it; memset(&it, 0, sizeof it);
        it.e = parse_expr(p);
        if (accept_kw(p, "asc")) it.dir = 0;
        else if (accept_kw(p, "desc")) it.dir = 1;
        else if (accept_kw(p, "using")) { if (CUR(p).kind == T_OP) { it.dir = (!strcmp(CUR(p).sval, ">")) ? 1 : 0; p->i++; } else syntax_error(p); }
        if (accept_kw(p, "nulls")) {
            if (accept_kw(p, "first")) it.nulls = 1;
            else if (accept_kw(p, "last")) it.nulls = 2;
            else syntax_error(p);
        }
        list = xrealloc(list, (size_t)(cnt + 1) * sizeof *list);
        list[cnt++] = it;
        if (!accept_kind(p, T_COMMA)) break;
    }
    *n = cnt;
    return list;
}

static WindowDef *parse_window_spec(P *p)
{
    WindowDef *w = xmalloc(sizeof *w);
    memset(w, 0, sizeof *w);
    w->frame_start = w->frame_end = -1;
    w->pos = CUR(p).pos;
    expect_kind(p, T_LPAREN);
    if (at_ident(p) && !at(p, "partition") && !at(p, "order") && !at(p, "rows") &&
        !at(p, "range") && !at(p, "groups") && !at_kind(p, T_RPAREN))
        w->refname = ident(p);
    if (accept_kw(p, "partition")) {
        expect_kw(p, "by");
        do xpush(&w->partition, &w->npartition, parse_expr(p)); while (accept_kind(p, T_COMMA));
    }
    if (accept_kw(p, "order")) { expect_kw(p, "by"); w->order = parse_sortlist(p, &w->norder); }
    if (at(p, "rows") || at(p, "range") || at(p, "groups")) {
        w->frame_mode = at(p, "rows") ? 1 : at(p, "groups") ? 2 : 0;
        p->i++;
        bool between = accept_kw(p, "between");
        for (int side = 0; side < (between ? 2 : 1); side++) {
            int kind; Expr *off = NULL;
            if (accept_kw(p, "unbounded")) {
                if (accept_kw(p, "preceding")) kind = 0;
                else { expect_kw(p, "following"); kind = 4; }
            } else if (accept_kw(p, "current")) { expect_kw(p, "row"); kind = 2; }
            else {
                off = parse_a_expr(p, 5);
                if (side == 0) w->start_kw_pos = CUR(p).pos;
                if (accept_kw(p, "preceding")) kind = 1;
                else { expect_kw(p, "following"); kind = 3; }
            }
            if (side == 0) { w->frame_start = kind; w->start_off = off; }
            else { w->frame_end = kind; w->end_off = off; }
            if (between && side == 0) expect_kw(p, "and");
        }
        if (!between) { w->frame_end = 2; }
        if (accept_kw(p, "exclude")) {
            if (accept_kw(p, "current")) { expect_kw(p, "row"); w->frame_excl = 1; }
            else if (accept_kw(p, "group")) w->frame_excl = 2;
            else if (accept_kw(p, "ties")) w->frame_excl = 3;
            else { expect_kw(p, "no"); expect_kw(p, "others"); w->frame_excl = 0; }
        }
    }
    expect_kind(p, T_RPAREN);
    return w;
}

static Expr *make_func(P *p, int pos, const char *name, Expr *a1, Expr *a2)
{
    Expr *e = newx(X_FUNC, pos);
    e->name = xstrdup(name);
    if (a1) xpush(&e->args, &e->nargs, a1);
    if (a2) xpush(&e->args, &e->nargs, a2);
    return e;
}

static bool is_agg_name(const char *n)
{
    static const char *a[] = {"count","sum","avg","min","max","array_agg","string_agg",
        "json_agg","jsonb_agg","json_object_agg","jsonb_object_agg","bool_and","bool_or",
        "every","bit_and","bit_or","bit_xor","stddev","stddev_pop","stddev_samp","variance",
        "var_pop","var_samp","range_agg","range_intersect_agg","any_value","xmlagg",
        "json_arrayagg","json_objectagg",
        "corr","covar_pop","covar_samp","regr_slope","percentile_cont","percentile_disc",
        "mode","rank","dense_rank","percent_rank","cume_dist", NULL};
    for (int i = 0; a[i]; i++) if (!strcmp(a[i], n)) return true;
    return false;
}
static bool is_window_only(const char *n)
{
    static const char *a[] = {"row_number","rank","dense_rank","percent_rank","cume_dist",
        "ntile","lag","lead","first_value","last_value","nth_value", NULL};
    for (int i = 0; a[i]; i++) if (!strcmp(a[i], n)) return true;
    return false;
}

/* PostgreSQL matches a named argument (name => value) to the parameter of
 * that name and fills the gaps with defaults.  The builtins here that are
 * called that way are the jsonb_path_* family and make_interval().  `def`
 * says what a skipped parameter takes: 0 means it is required, 'j' the empty
 * jsonpath vars object, 'b' false and 'z' zero.  `strict` is set for the
 * functions that reject a parameter name they do not have. */
static const struct {
    const char *fn; bool prefix; bool strict; int n; const char *nm[8]; char def[8];
} named_funcs[] = {
    { "jsonb_path_",   true,  false, 4,
      {"target","path","vars","silent"}, {0,0,'j','b'} },
    { "make_interval", false, true,  7,
      {"years","months","weeks","days","hours","mins","secs"},
      {'z','z','z','z','z','z','z'} },
    { NULL, false, false, 0, {NULL}, {0} }
};

static void named_args_to_positions(P *p, Expr *e, char **names, int n)
{
    bool any = false;
    for (int i = 0; i < n; i++) if (names[i]) any = true;
    if (!any || !e->name) return;

    int f = -1;
    for (int i = 0; named_funcs[i].fn && f < 0; i++)
        if (named_funcs[i].prefix
                ? !strncmp(e->name, named_funcs[i].fn, strlen(named_funcs[i].fn))
                : !strcmp(e->name, named_funcs[i].fn))
            f = i;
    if (f < 0) return;
    int np = named_funcs[f].n;

    Expr *slot[8];
    for (int k = 0; k < 8; k++) slot[k] = NULL;
    int npos = 0;
    for (int i = 0; i < n && i < e->nargs; i++) {
        if (!names[i]) { if (npos < np) slot[npos] = e->args[i]; npos++; continue; }
        int at = -1;
        for (int k = 0; k < np; k++)
            if (named_funcs[f].nm[k] && !strcmp(names[i], named_funcs[f].nm[k])) { at = k; break; }
        if (at < 0) {
            if (named_funcs[f].strict)
                perr(p, "function %s(...) does not exist", e->name);
            continue;
        }
        slot[at] = e->args[i];
    }
    int last = -1;
    for (int k = 0; k < np; k++) if (slot[k]) last = k;
    if (last < 0) return;
    for (int k = 0; k <= last; k++) {
        if (slot[k]) continue;
        Expr *c = newx(X_CONST, e->pos);
        switch (named_funcs[f].def[k]) {
        case 'j': c->strval = xstrdup("{}"); c->val = d_text(UNKNOWNOID, "{}"); break;
        case 'b': c->val = d_bool(false); break;
        case 'z': c->strval = xstrdup("0"); c->val = d_int(INT4OID, 0); break;
        default:  return;                  /* a required parameter is missing */
        }
        slot[k] = c;
    }
    e->args = xrealloc(e->args, (size_t)(last + 1) * sizeof(Expr *));
    for (int k = 0; k <= last; k++) e->args[k] = slot[k];
    e->nargs = last + 1;
}

static Expr *parse_func_call(P *p, char *schema, char *name, int pos)
{
    Expr *e = newx(X_FUNC, pos);
    e->nspname = schema;
    e->name = name;
    expect_kind(p, T_LPAREN);
    if (accept_op(p, "*")) {
        e->agg_star = true;
        expect_kind(p, T_RPAREN);
    } else if (at_kind(p, T_RPAREN)) {
        p->i++;
    } else {
        if (accept_kw(p, "distinct")) e->agg_distinct = true;
        else accept_kw(p, "all");
        /* special argument grammars */
        if (!strcmp(name, "extract")) {
            Expr *f = newx(X_CONST, CUR(p).pos);
            f->strval = at_ident(p) ? ident_any(p) : (CUR(p).kind == T_STRING ? xstrdup(CUR(p).sval) : NULL);
            if (!f->strval) syntax_error(p);
            if (CUR(p).kind == T_STRING && !f->strval) p->i++;
            f->val = d_text(TEXTOID, f->strval);
            xpush(&e->args, &e->nargs, f);
            expect_kw(p, "from");
            xpush(&e->args, &e->nargs, parse_expr(p));
#if PGVC_VN >= 130
        } else if (!strcmp(name, "normalize") && !schema) {
            /* NORMALIZE(expr [, NFC|NFD|NFKC|NFKD]) is spelled in the grammar,
             * so the form is a keyword and nothing else is accepted there. */
            xpush(&e->args, &e->nargs, parse_expr(p));
            if (accept_kind(p, T_COMMA)) {
                int form;
                if (at(p,"nfc")) form = 0;
                else if (at(p,"nfd")) form = 1;
                else if (at(p,"nfkc")) form = 2;
                else if (at(p,"nfkd")) form = 3;
                else syntax_error(p);
                p->i++;
                Expr *f = newx(X_CONST, CUR(p).pos);
                f->val = d_int(INT4OID, form);
                { char tb[4]; snprintf(tb, sizeof tb, "%d", form); f->strval = xstrdup(tb); }
                xpush(&e->args, &e->nargs, f);
            }
            e->grammar_func = true;
#endif
        } else if (!strcmp(name, "substring") || !strcmp(name, "substr")) {
            xpush(&e->args, &e->nargs, parse_expr(p));
            if (accept_kw(p, "from")) {
                xpush(&e->args, &e->nargs, parse_expr(p));
                if (accept_kw(p, "for")) xpush(&e->args, &e->nargs, parse_expr(p));
            } else if (accept_kw(p, "for")) {
                Expr *one = newx(X_CONST, pos); one->strval = xstrdup("1"); one->val = d_int(INT4OID, 1);
                xpush(&e->args, &e->nargs, one);
                xpush(&e->args, &e->nargs, parse_expr(p));
            } else while (accept_kind(p, T_COMMA)) xpush(&e->args, &e->nargs, parse_expr(p));
        } else if (!strcmp(name, "overlay")) {
            xpush(&e->args, &e->nargs, parse_expr(p));
            expect_kw(p, "placing"); xpush(&e->args, &e->nargs, parse_expr(p));
            expect_kw(p, "from");    xpush(&e->args, &e->nargs, parse_expr(p));
            if (accept_kw(p, "for")) xpush(&e->args, &e->nargs, parse_expr(p));
        } else if (!strcmp(name, "position")) {
            Expr *a = parse_a_expr(p, 6);
            expect_kw(p, "in");
            Expr *b = parse_expr(p);
            xpush(&e->args, &e->nargs, b); xpush(&e->args, &e->nargs, a);
        } else if (!strcmp(name, "trim") || !strcmp(name, "btrim")) {
            int side = 0;
            if (accept_kw(p, "leading")) side = 1;
            else if (accept_kw(p, "trailing")) side = 2;
            else if (accept_kw(p, "both")) side = 0;
            Expr *chars = NULL, *str = NULL;
            if (!at(p, "from")) chars = parse_expr(p);
            if (accept_kw(p, "from")) { str = parse_expr(p); }
            else if (accept_kind(p, T_COMMA)) { str = chars; chars = parse_expr(p); Expr *t = str; str = t; }
            if (!str) { str = chars; chars = NULL; }
            free(e->name);
            e->name = xstrdup(side == 1 ? "ltrim" : side == 2 ? "rtrim" : "btrim");
            xpush(&e->args, &e->nargs, str);
            if (chars) xpush(&e->args, &e->nargs, chars);
        } else if (!strcmp(name, "cast")) {
            Expr *a = parse_expr(p);
            expect_kw(p, "as");
            Oid t; int32 tm;
            char *tn = type_name_text(p, &t, &tm);
            Expr *c = newx(X_CAST, pos);
            c->args = NULL; xpush(&c->args, &c->nargs, a);
            c->casttype = t; c->casttypmod = tm; c->casttypename = tn;
            expect_kind(p, T_RPAREN);
            return c;
        } else {
            char **anames = NULL;
            int nnames = 0;
            for (;;) {
                char *an = NULL;
                if (at_ident(p) && PEEK(p,1).kind == T_OP && PEEK(p,1).sval &&
                    (!strcmp(PEEK(p,1).sval, ":=") || !strcmp(PEEK(p,1).sval, "=>"))) {
                    an = lower_ident(CUR(p).sval);
                    p->i += 2;   /* named argument */
                }
                if (accept_kw(p, "variadic")) { /* accepted, treated as ordinary */ }
                xpush(&e->args, &e->nargs, parse_expr(p));
                spush(&anames, &nnames, an);
                if (!accept_kind(p, T_COMMA)) break;
            }
            named_args_to_positions(p, e, anames, nnames);
            for (int i = 0; i < nnames; i++) free(anames[i]);
            free(anames);
        }
        if (accept_kw(p, "order")) { expect_kw(p, "by"); e->agg_order = parse_sortlist(p, &e->agg_norder); }
        expect_kind(p, T_RPAREN);
#if PGVC_VN >= 170
        /* named arguments were mapped to positions above by
         * named_args_to_positions(), which covers the jsonb_path_* family. */
        /* a jsonpath literal is parsed when the query is, not when it runs */
        if (!strncmp(e->name, "jsonb_path_", 11) && e->nargs > 1 &&
            e->args[1]->tag == X_CONST && e->args[1]->strval) {
            char *rest = jsonpath_leftover(p->s, e->args[1]->strval);
            if (rest) ereport_pos(p->s, "42601", e->args[1]->pos,
                                  "syntax error at or near \"%s\" of jsonpath input", rest);
        }
#endif
    }
    if (at(p, "within") && is_kw(p, 1, "group")) {
        p->i += 2; expect_kind(p, T_LPAREN);
        expect_kw(p, "order"); expect_kw(p, "by");
        e->agg_order = parse_sortlist(p, &e->agg_norder);
        expect_kind(p, T_RPAREN);
    }
    if (at(p, "filter")) {
        p->i++; expect_kind(p, T_LPAREN); expect_kw(p, "where");
        e->filter = parse_expr(p);
        expect_kind(p, T_RPAREN);
    }
    if (accept_kw(p, "over")) {
        if (at_kind(p, T_LPAREN)) e->over = parse_window_spec(p);
        else { e->over = xmalloc(sizeof(WindowDef)); memset(e->over, 0, sizeof(WindowDef));
               e->over->frame_start = e->over->frame_end = -1; e->over->refname = ident(p); }
        e->tag = X_WINDOW;
    } else if (is_agg_name(e->name) && !is_window_only(e->name) &&
               !(!strcmp(e->name, "any_value") && (e->agg_star || e->nargs != 1)))
        e->tag = X_AGG;
    else if (is_window_only(e->name))
        e->tag = X_AGG;                 /* rejected later unless OVER present */
    return e;
}

/* ARRAY[...] and the bare [...] sub-arrays PostgreSQL allows inside it */
static Expr *parse_array_literal(P *p, int pos)
{
    expect_kind(p, T_LBRACK);
    Expr *e = newx(X_ARRAY, pos);
    if (!at_kind(p, T_RBRACK)) {
        do {
            if (at_kind(p, T_LBRACK)) xpush(&e->args, &e->nargs, parse_array_literal(p, CUR(p).pos));
            else xpush(&e->args, &e->nargs, parse_expr(p));
        } while (accept_kind(p, T_COMMA));
    }
    expect_kind(p, T_RBRACK);
    return e;
}

static Expr *parse_case(P *p)
{
    int pos = CUR(p).pos;
    expect_kw(p, "case");
    Expr *e = newx(X_CASE, pos);
    if (!at(p, "when")) e->case_arg = parse_expr(p);
    while (accept_kw(p, "when")) {
        Expr *c = parse_expr(p);
        expect_kw(p, "then");
        Expr *r = parse_expr(p);
        e->when_cond = xrealloc(e->when_cond, (size_t)(e->nwhen + 1) * sizeof(Expr *));
        e->when_res  = xrealloc(e->when_res,  (size_t)(e->nwhen + 1) * sizeof(Expr *));
        e->when_cond[e->nwhen] = c; e->when_res[e->nwhen] = r; e->nwhen++;
    }
    if (accept_kw(p, "else")) e->case_else = parse_expr(p);
    expect_kw(p, "end");
    return e;
}

/* RETURNING <type> on a SQL/JSON constructor. */
static void parse_json_returning(P *p, Expr *e)
{
    int tpos = CUR(p).pos;
    Oid t; int32 tm;
    char *tn = type_name_text(p, &t, &tm);
    if (!t) ereport_pos(p->s, "42704", tpos, "type \"%s\" does not exist", tn);
    e->casttype = t; e->casttypmod = tm; e->casttypename = tn;
    if (accept_kw(p, "format")) { accept_kw(p, "json");
                                  if (accept_kw(p, "encoding")) p->i++; }
}

/* { NULL | ABSENT } ON NULL, at most once. */
static void parse_json_onnull(P *p, Expr *e)
{
    if (at(p, "absent") && is_kw(p, 1, "on") && is_kw(p, 2, "null")) {
        p->i += 3; e->json_absent_on_null = true;
    } else if (at(p, "null") && is_kw(p, 1, "on") && is_kw(p, 2, "null")) {
        p->i += 3; e->json_absent_on_null = false;
    }
}

/* { WITH | WITHOUT } UNIQUE [ KEYS ] */
static void parse_json_unique(P *p, Expr *e)
{
    if (at(p, "with") && is_kw(p, 1, "unique")) {
        p->i += 2; accept_kw(p, "keys"); e->isjson_unique = true;
    } else if (at(p, "without") && is_kw(p, 1, "unique")) {
        p->i += 2; accept_kw(p, "keys"); e->isjson_unique = false;
    }
}

#if PGVC_VN >= 170
/* { ERROR | NULL | TRUE | FALSE | UNKNOWN | EMPTY [ARRAY|OBJECT] | DEFAULT expr } */
static int parse_json_behav(P *p, Expr **dflt)
{
    *dflt = NULL;
    if (accept_kw(p, "error")) return 1;
    if (accept_kw(p, "null")) return 0;
    if (accept_kw(p, "true")) return 5;
    if (accept_kw(p, "false")) return 6;
    if (accept_kw(p, "unknown")) return 7;
    if (accept_kw(p, "empty")) {
        if (accept_kw(p, "object")) return 4;
        accept_kw(p, "array");
        return 3;
    }
    if (accept_kw(p, "default")) { *dflt = parse_a_expr(p, 3); return 2; }
    return -1;
}

/* The tail shared by JSON_EXISTS, JSON_VALUE, JSON_QUERY and JSON_TABLE. */
static void parse_json_query_opts(P *p, Expr *e, bool allow_wrapper, bool allow_ret)
{
    JsonBehav *bh = xmalloc(sizeof *bh);
    memset(bh, 0, sizeof *bh);
    bh->on_empty = -1; bh->on_error = -1;
    e->json_behav = bh;
    if (accept_kw(p, "passing")) {
        do {
            Expr *v = parse_a_expr(p, 3);
            expect_kw(p, "as");
            char *nm = ident_any(p);
            bh->passing = xrealloc(bh->passing, (size_t)(bh->npassing + 1) * sizeof(Expr *));
            bh->passnames = xrealloc(bh->passnames, (size_t)(bh->npassing + 1) * sizeof(char *));
            bh->passing[bh->npassing] = v;
            bh->passnames[bh->npassing] = nm;
            bh->npassing++;
        } while (accept_kind(p, T_COMMA));
    }
    if (allow_ret && accept_kw(p, "returning")) parse_json_returning(p, e);
    if (allow_wrapper) {
        if (accept_kw(p, "without")) { accept_kw(p, "array"); expect_kw(p, "wrapper"); bh->wrapper = 0; }
        else if (at(p, "with") && (is_kw(p,1,"wrapper") || is_kw(p,1,"array") ||
                                   is_kw(p,1,"conditional") || is_kw(p,1,"unconditional"))) {
            p->i++;
            if (accept_kw(p, "conditional")) bh->wrapper = 2;
            else { accept_kw(p, "unconditional"); bh->wrapper = 1; }
            accept_kw(p, "array");
            expect_kw(p, "wrapper");
        }
        if (accept_kw(p, "keep")) { expect_kw(p, "quotes"); bh->quotes = 0;
                                    if (accept_kw(p,"on")) { expect_kw(p,"scalar"); expect_kw(p,"string"); } }
        else if (accept_kw(p, "omit")) { expect_kw(p, "quotes"); bh->quotes = 1;
                                         if (accept_kw(p,"on")) { expect_kw(p,"scalar"); expect_kw(p,"string"); } }
    }
    for (int k = 0; k < 2; k++) {
        int save = p->i;
        Expr *d = NULL;
        int b = parse_json_behav(p, &d);
        if (b < 0) break;
        if (!accept_kw(p, "on")) { p->i = save; break; }
        if (accept_kw(p, "empty")) { bh->on_empty = b; bh->empty_default = d; }
        else if (accept_kw(p, "error")) { bh->on_error = b; bh->error_default = d; }
        else syntax_error(p);
    }
}

/* JSON_EXISTS / JSON_VALUE / JSON_QUERY, added in PostgreSQL 17. */
static Expr *parse_json_query_fn(P *p)
{
    int pos = CUR(p).pos;
    char *nm = ident_any(p);
    Expr *e = newx(X_JSONCTOR, pos);
    e->name = nm;
    e->json_ctor = !strcmp(nm, "json_query") ? 7 : !strcmp(nm, "json_value") ? 8 : 9;
    expect_kind(p, T_LPAREN);
    xpush(&e->args, &e->nargs, parse_a_expr(p, 3));
    if (accept_kw(p, "format")) { accept_kw(p, "json"); }
    expect_kind(p, T_COMMA);
    int ppos = CUR(p).pos;
    bool plit = (CUR(p).kind == T_STRING);
    char *plitv = plit ? xstrdup(CUR(p).sval) : NULL;
    xpush(&e->args, &e->nargs, parse_a_expr(p, 3));
    if (plitv) {
        char *rest = jsonpath_leftover(p->s, plitv);
        if (rest) ereport_pos(p->s, "42601", ppos,
                              "syntax error at or near \"%s\" of jsonpath input", rest);
        free(plitv);
    }
    parse_json_query_opts(p, e, e->json_ctor == 7, e->json_ctor != 9);
    expect_kind(p, T_RPAREN);
    return e;
}
#endif

/* The SQL/JSON constructors PostgreSQL 16 added:  JSON_ARRAY, JSON_OBJECT,
 * JSON_ARRAYAGG and JSON_OBJECTAGG (plus JSON, JSON_SCALAR, JSON_SERIALIZE).
 * JSON_OBJECT with a plain argument list is the older json_object() function
 * instead, which is what makes JSON_OBJECT('a') a malformed array literal. */
static Expr *parse_json_ctor(P *p)
{
    int pos = CUR(p).pos;
    char *nm = ident_any(p);
    Expr *e = newx(X_JSONCTOR, pos);
    e->name = nm;
    if (!strcmp(nm, "json_array")) e->json_ctor = 0;
    else if (!strcmp(nm, "json_object")) e->json_ctor = 1;
    else if (!strcmp(nm, "json_arrayagg")) e->json_ctor = 2;
    else if (!strcmp(nm, "json_objectagg")) e->json_ctor = 3;
    else if (!strcmp(nm, "json")) e->json_ctor = 4;
    else if (!strcmp(nm, "json_scalar")) e->json_ctor = 5;
    else e->json_ctor = 6;
    int kind = e->json_ctor;
    bool isobj = (kind == 1 || kind == 3);
    e->json_absent_on_null = !isobj;
    expect_kind(p, T_LPAREN);
    if (!at_kind(p, T_RPAREN)) {
        if (kind == 0 && (at(p, "select") || at(p, "with") || at(p, "values") ||
                          at(p, "table"))) {
            e->subquery = parse_select_stmt(p, NULL, 0);
        } else if (kind == 1) {
            Expr *first = parse_a_expr(p, 3);
            if (at_kind(p, T_COLON) || at(p, "value")) {
                p->i++;
                xpush(&e->args, &e->nargs, first);
                xpush(&e->args, &e->nargs, parse_a_expr(p, 3));
                while (accept_kind(p, T_COMMA)) {
                    Expr *k = parse_a_expr(p, 3);
                    if (!accept_kind(p, T_COLON) && !accept_kw(p, "value")) syntax_error(p);
                    xpush(&e->args, &e->nargs, k);
                    xpush(&e->args, &e->nargs, parse_a_expr(p, 3));
                }
            } else if (at_kind(p, T_COMMA) || at_kind(p, T_RPAREN)) {
                /* the ordinary json_object(text[]) / json_object(text[],text[]),
                 * whose arguments PostgreSQL coerces at the literal's position */
                Expr *f = newx(X_FUNC, pos);
                f->name = xstrdup("json_object");
                for (;;) {
                    Expr *cast = newx(X_CAST, first->pos);
                    xpush(&cast->args, &cast->nargs, first);
                    cast->casttype = TEXTARRAYOID; cast->casttypmod = -1;
                    cast->casttypename = xstrdup("text[]");
                    xpush(&f->args, &f->nargs, cast);
                    if (!accept_kind(p, T_COMMA)) break;
                    first = parse_expr(p);
                }
                expect_kind(p, T_RPAREN);
                return f;
            } else syntax_error(p);
        } else if (kind == 3) {
            Expr *k = parse_a_expr(p, 3);
            if (!accept_kind(p, T_COLON) && !accept_kw(p, "value")) syntax_error(p);
            xpush(&e->args, &e->nargs, k);
            xpush(&e->args, &e->nargs, parse_a_expr(p, 3));
        } else if (kind == 2) {
            xpush(&e->args, &e->nargs, parse_a_expr(p, 3));
            if (accept_kw(p, "order")) {
                expect_kw(p, "by");
                e->agg_order = parse_sortlist(p, &e->agg_norder);
            }
        } else {
            for (;;) {
                if ((at(p, "absent") || at(p, "null")) && is_kw(p, 1, "on") &&
                    is_kw(p, 2, "null")) break;
                xpush(&e->args, &e->nargs, parse_a_expr(p, 3));
                if (kind >= 4) break;
                if (!accept_kind(p, T_COMMA)) break;
            }
            if (kind >= 4 && accept_kw(p, "format")) { accept_kw(p, "json");
                                                        if (accept_kw(p, "encoding")) p->i++; }
        }
        if (kind <= 3) parse_json_onnull(p, e);
        if (isobj || kind == 4) parse_json_unique(p, e);
        if (kind != 4 && kind != 5 && accept_kw(p, "returning")) parse_json_returning(p, e);
    }
    expect_kind(p, T_RPAREN);
    /* the two aggregate forms travel through the aggregate machinery */
    if (kind == 2 || kind == 3) e->tag = X_AGG;
    return e;
}

static Expr *parse_primary(P *p)
{
    Tok t = CUR(p);
    int pos = t.pos;

    if (at(p, "xmltable") && PEEK(p,1).kind == T_LPAREN)
        ereport_pos(p->s, "42601", PEEK(p,1).pos, "syntax error at or near \"(\"");
    if (at(p, "xmlparse") && PEEK(p,1).kind == T_LPAREN) {
        /* XMLPARSE(DOCUMENT|CONTENT expr) is a cast of the text to xml */
        p->i += 2;
        accept_kw(p, "document"); accept_kw(p, "content");
        Expr *inner = parse_expr(p);
        accept_kw(p, "preserve"); accept_kw(p, "strip"); accept_kw(p, "whitespace");
        expect_kind(p, T_RPAREN);
        Expr *e = newx(X_CAST, pos);
        xpush(&e->args, &e->nargs, inner);
        e->casttype = XMLOID; e->casttypmod = -1;
        e->casttypename = xstrdup("xml");
        return e;
    }
    if (t.kind == T_PARAM) { p->i++; Expr *e = newx(X_PARAM, pos); e->paramno = t.ival; return e; }
    if (t.kind == T_NUMBER) {
        p->i++;
        Expr *e = newx(X_CONST, pos);
        e->strval = xstrdup(t.sval);
        if (t.isint) {
            errno = 0;
            long long v = strtoll(t.sval, NULL, 10);
            if (errno == 0 && v >= -2147483648LL && v <= 2147483647LL) e->val = d_int(INT4OID, v);
            else if (errno == 0) e->val = d_int(INT8OID, v);
            else { bool ok; e->val = d_num(num_from_str(t.sval, &ok)); }
        } else {
            bool ok; Numeric *n = num_from_str(t.sval, &ok);
            e->val = ok ? d_num(n) : d_null(NUMERICOID);
        }
        return e;
    }
    if (t.kind == T_STRING) {
        p->i++;
        Expr *e = newx(X_CONST, pos);
        e->strval = xstrndup(t.sval, (size_t)(t.slen ? t.slen : (int)strlen(t.sval)));
        e->val = d_textn(UNKNOWNOID, t.sval, t.slen ? t.slen : (int)strlen(t.sval));
        if (t.bitstr) e->is_kind = 7;
        return e;
    }
    if (t.kind == T_LPAREN) {
        p->i++;
        if (at(p, "select") || at(p, "with") || at(p, "values") || at(p, "table")) {
            Node *q = parse_select_stmt(p, NULL, 0);
            expect_kind(p, T_RPAREN);
            Expr *e = newx(X_SUBLINK, pos);
            e->subquery = q; e->sublink_kind = 0;
            return e;
        }
        Expr *first = parse_expr(p);
        if (at_kind(p, T_COMMA)) {
            Expr *e = newx(X_ROW, pos);
            xpush(&e->args, &e->nargs, first);
            while (accept_kind(p, T_COMMA)) xpush(&e->args, &e->nargs, parse_expr(p));
            expect_kind(p, T_RPAREN);
            return e;
        }
        expect_kind(p, T_RPAREN);
        return first;
    }
    if (t.kind == T_OP && (!strcmp(t.sval, "-") || !strcmp(t.sval, "+"))) {
        p->i++;
        Expr *a = parse_a_expr(p, 5);
        if (!strcmp(t.sval, "+")) return a;
        if (a->tag == X_CONST && a->strval && (a->val.type == INT4OID || a->val.type == INT8OID ||
                                               a->val.type == NUMERICOID)) {
            Buf b; buf_init(&b); buf_addc(&b, '-'); buf_adds(&b, a->strval);
            Expr *e = newx(X_CONST, pos);
            e->strval = buf_steal(&b);
            if (a->val.type == INT4OID) {
                long long v = -(long long)a->val.v.i;
                e->val = (v >= -2147483648LL) ? d_int(INT4OID, v) : d_int(INT8OID, v);
            } else if (a->val.type == INT8OID) e->val = d_int(INT8OID, -a->val.v.i);
            else { bool ok; e->val = d_num(num_from_str(e->strval, &ok)); }
            return e;
        }
        return make_func(p, pos, "-", a, NULL);
    }
    if (t.kind == T_OP && !strcmp(t.sval, "@")) { p->i++; return make_func(p, pos, "@", parse_a_expr(p, 5), NULL); }
    if (t.kind == T_OP && !strcmp(t.sval, "|/")) { p->i++; return make_func(p, pos, "sqrt", parse_a_expr(p, 5), NULL); }
    if (t.kind == T_OP && !strcmp(t.sval, "~")) { p->i++; return make_func(p, pos, "~num", parse_a_expr(p, 5), NULL); }
    if (t.kind == T_OP && !strcmp(t.sval, "!!")) { p->i++; return make_func(p, pos, "!!", parse_a_expr(p, 5), NULL); }

    if (t.kind == T_IDENT) {
        if (at(p, "case")) return parse_case(p);
        if (at(p, "exists") && PEEK(p,1).kind == T_LPAREN) {
            p->i += 2;
            Node *q = parse_select_stmt(p, NULL, 0);
            expect_kind(p, T_RPAREN);
            Expr *e = newx(X_EXISTS, pos); e->subquery = q; e->sublink_kind = 1;
            return e;
        }
        if (at(p, "array") && PEEK(p,1).kind == T_LBRACK) {
            p->i += 1;
            return parse_array_literal(p, pos);
        }
        if (at(p, "array") && PEEK(p,1).kind == T_LPAREN) {
            p->i += 2;
            Node *q = parse_select_stmt(p, NULL, 0);
            expect_kind(p, T_RPAREN);
            Expr *e = newx(X_SUBLINK, pos); e->subquery = q; e->sublink_kind = 5;
            return e;
        }
        if (at(p, "row") && PEEK(p,1).kind == T_LPAREN) {
            p->i += 2;
            Expr *e = newx(X_ROW, pos);
            if (!at_kind(p, T_RPAREN))
                do xpush(&e->args, &e->nargs, parse_expr(p)); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
            return e;
        }
        if (at(p, "not")) { p->i++; Expr *e = newx(X_NOT, pos); xpush(&e->args, &e->nargs, parse_a_expr(p, 10)); return e; }
        if (at(p, "null")) { p->i++; Expr *e = newx(X_CONST, pos); e->isnull_const = true; e->val = d_null(UNKNOWNOID); return e; }
        if (at(p, "true") || at(p, "false")) { bool b = at(p, "true"); p->i++; Expr *e = newx(X_CONST, pos); e->val = d_bool(b); return e; }
        if (at(p, "default") && !PEEK(p,1).iskw) { /* handled by callers */ }
        {
            /* PostgreSQL 18 OLD./NEW. in RETURNING, under their own names or
             * the ones a RETURNING WITH clause gave them */
            /* each row version answers to its alias, or to its own name when
             * the RETURNING WITH clause did not rename it */
            int which = 0;
            const char *on = p->ret_oldalias ? p->ret_oldalias : "old";
            const char *nn = p->ret_newalias ? p->ret_newalias : "new";
            if (at(p, on)) which = 1;
            else if (at(p, nn)) which = 2;
            if (which && PEEK(p,1).kind == T_DOT && !t.quoted) {
                Expr *e = newx(X_OLDNEW, pos);
                e->oldnew = which;
                p->i += 2;
                if (accept_op(p, "*")) { e->tag = X_STAR; e->colname = NULL; }
                else e->colname = ident_any(p);
                return e;
            }
        }
        if (at(p, "json_array") || at(p, "json_object") || at(p, "json_arrayagg") ||
            at(p, "json_objectagg")
#if PGVC_VN >= 170
            || at(p, "json_scalar") || at(p, "json_serialize") || at(p, "json")
#endif
            ) {
            if (PEEK(p,1).kind == T_LPAREN) return parse_json_ctor(p);
        }
#if PGVC_VN >= 170
        if (at(p, "json_query") || at(p, "json_value") || at(p, "json_exists")) {
            if (PEEK(p,1).kind == T_LPAREN) return parse_json_query_fn(p);
        }
#endif
        if (at(p, "interval") && PEEK(p,1).kind == T_STRING) {
            p->i++;
            Tok st = CUR(p); p->i++;
            Expr *c = newx(X_CONST, pos);
            c->strval = xstrdup(st.sval);
            c->val = d_text(UNKNOWNOID, st.sval);
            /* optional field qualifier: INTERVAL 'x' DAY TO SECOND */
            while (at_ident(p) && (at(p,"year")||at(p,"month")||at(p,"day")||at(p,"hour")||
                                   at(p,"minute")||at(p,"second")||at(p,"to"))) p->i++;
            Expr *e = newx(X_CAST, pos);
            xpush(&e->args, &e->nargs, c);
            e->casttype = INTERVALOID; e->casttypmod = -1; e->casttypename = xstrdup("interval");
            return e;
        }
        /* typed literal:  TYPE 'string'  */
        if (!t.quoted && PEEK(p,1).kind == T_STRING) {
            Oid ot = type_by_name(t.sval);
            if (ot) {
                int save = p->i;
                Oid tt; int32 tm;
                char *tn = type_name_text(p, &tt, &tm);
                if (CUR(p).kind == T_STRING && tt) {
                    Tok st = CUR(p); p->i++;
                    Expr *c = newx(X_CONST, pos);
                    c->strval = xstrdup(st.sval); c->val = d_text(UNKNOWNOID, st.sval);
                    Expr *e = newx(X_CAST, pos);
                    xpush(&e->args, &e->nargs, c);
                    e->casttype = tt; e->casttypmod = tm; e->casttypename = tn;
                    return e;
                }
                p->i = save; free(tn);
            }
        }
        if (at(p, "current_date") || at(p, "current_timestamp") || at(p, "current_time") ||
            at(p, "localtime") || at(p, "localtimestamp") || at(p, "current_user") ||
            at(p, "session_user") || at(p, "user") || at(p, "current_catalog") ||
#if PGVC_VN >= 160
            at(p, "system_user") ||
#endif
            at(p, "current_schema") || at(p, "current_role")) {
            char *nm = ident_any(p);
            Expr *e = newx(X_FUNC, pos); e->name = nm;
            if (at_kind(p, T_LPAREN)) {
                p->i++;
                if (!at_kind(p, T_RPAREN))
                    do xpush(&e->args, &e->nargs, parse_expr(p)); while (accept_kind(p, T_COMMA));
                expect_kind(p, T_RPAREN);
            }
            return e;
        }
        if (at(p, "grouping") && PEEK(p,1).kind == T_LPAREN) {
            p->i += 2;
            Expr *e = newx(X_GROUPING, pos);
            do xpush(&e->args, &e->nargs, parse_expr(p)); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
            return e;
        }
        /* qualified name / function call / column reference */
        {
            char *parts[4] = {0,0,0,0}; int np = 0;
            parts[np++] = ident_any(p);
            while (at_kind(p, T_DOT) && np < 4) {
                p->i++;
                if (accept_op(p, "*")) {
                    Expr *e = newx(X_STAR, pos);
                    if (np >= 2) { e->qual2 = parts[0]; e->qual = parts[1]; }
                    else e->qual = parts[0];
                    return e;
                }
                parts[np++] = ident_any(p);
            }
            if (at_kind(p, T_LPAREN)) {
                char *sch = np > 1 ? parts[np-2] : NULL;
                char *nm  = parts[np-1];
                return parse_func_call(p, sch ? xstrdup(sch) : NULL, nm, pos);
            }
            Expr *e = newx(X_COLREF, pos);
            if (np == 1) e->colname = parts[0];
            else if (np == 2) { e->qual = parts[0]; e->colname = parts[1]; }
            else { e->qual2 = parts[np-3]; e->qual = parts[np-2]; e->colname = parts[np-1]; }
            return e;
        }
    }
    syntax_error(p);
}

/* postfix: [] . :: */
static Expr *parse_postfix2(P *p, Expr *e, int startpos)
{
    for (;;) {
        if (at_kind(p, T_LBRACK) && e->tag == X_ARRAY && !e->alias) syntax_error(p);
        if (at_kind(p, T_LBRACK)) {
            int pos = CUR(p).pos;
            p->i++;
            Expr *sub = newx(X_SUBSCRIPT, pos);
            sub->basepos = startpos;
            xpush(&sub->args, &sub->nargs, e);
            Expr *lo = NULL;
            if (!at_kind(p, T_COLON)) lo = parse_expr(p);
            if (accept_kind(p, T_COLON)) {
                Expr *hi = at_kind(p, T_RBRACK) ? NULL : parse_expr(p);
                sub->is_kind = 1;                 /* slice */
                xpush(&sub->args, &sub->nargs, lo ? lo : newx(X_CONST, pos));
                xpush(&sub->args, &sub->nargs, hi ? hi : newx(X_CONST, pos));
            } else xpush(&sub->args, &sub->nargs, lo);
            expect_kind(p, T_RBRACK);
            e = sub;
            continue;
        }
        if (at_kind(p, T_DOT) && PEEK(p,1).kind == T_IDENT &&
            (e->tag == X_ROW || e->tag == X_FUNC || e->tag == X_SUBLINK)) {
            int pos = CUR(p).pos;
            p->i++;
            char *fld = ident_any(p);
            Expr *f = newx(X_FUNC, pos);
            f->name = xstrdup("__field__");
            xpush(&f->args, &f->nargs, e);
            f->colname = fld;
            e = f;
            continue;
        }
        if (at(p, "at") && (is_kw(p, 1, "time")
#if PGVC_VN >= 170
                            || is_kw(p, 1, "local")
#endif
                            )) {
            int pos = CUR(p).pos;
            p->i++;
            Expr *f = newx(X_FUNC, pos);
            f->name = xstrdup("timezone");
            if (accept_kw(p, "local")) {
                xpush(&f->args, &f->nargs, e);
            } else {
                expect_kw(p, "time"); expect_kw(p, "zone");
                int zp = CUR(p).pos;
                Expr *z = parse_postfix2(p, parse_primary(p), zp);
                xpush(&f->args, &f->nargs, z);
                xpush(&f->args, &f->nargs, e);
            }
            e = f;
            continue;
        }
        if (at_kind(p, T_CAST)) {
            int pos = CUR(p).pos;
            p->i++;
            Oid t; int32 tm;
            char *tn = type_name_text(p, &t, &tm);
            /* B'1010'::int4 and X'ff'::int4 read the literal in its own base */
            if (e->tag == X_CONST && e->is_kind == 7 &&
                (t == INT2OID || t == INT4OID || t == INT8OID)) {
                int64 v = 0;
                for (const char *q = e->strval; *q; q++) v = v * 2 + (*q == '1' ? 1 : 0);
                Expr *n2 = newx(X_CONST, e->pos);
                n2->strval = e->strval;
                n2->val = d_int(t, v);
                n2->alias = xstrdup(type_name(t));
                e = n2;
                free(tn);
                continue;
            }
            Expr *c = newx(X_CAST, pos);
            xpush(&c->args, &c->nargs, e);
            c->casttype = t; c->casttypmod = tm; c->casttypename = tn;
            e = c;
            continue;
        }
        break;
    }
    return e;
}

/* precedence levels (higher binds tighter):
 * 1 OR, 2 AND, 3 NOT, 4 IS/comparison/BETWEEN/IN/LIKE, 5 other ops/+-, 6 * / %, 7 ^, 8 unary, 9 postfix */
static Expr *parse_a_expr(P *p, int prec)
{
    Expr *left;
    if (prec <= 3 && at(p, "not")) {
        int pos = CUR(p).pos; p->i++;
        Expr *e = newx(X_NOT, pos);
        xpush(&e->args, &e->nargs, parse_a_expr(p, 3));
        left = e;
    } else {
        { int sp = CUR(p).pos; left = parse_postfix2(p, parse_primary(p), sp); }
        /* exponent binds tighter than * / */
        while (at_op(p, "^")) {
            int pos = CUR(p).pos; p->i++;
            int sp2 = CUR(p).pos;
            Expr *r = parse_postfix2(p, parse_primary(p), sp2);
            left = make_func(p, pos, "^", left, r);
        }
#if PGVC_VN < 140
        /* postfix factorial (removed in PostgreSQL 14) */
        while (at_op(p, "!") && prec <= 6) {
            int pos = CUR(p).pos; p->i++;
            left = make_func(p, pos, "!post", left, NULL);
        }
#else
        /* at 14 "!" is an ordinary infix operator with no definition, so a
         * trailing one is a syntax error at whatever follows it */
        if (at_op(p, "!") && prec <= 6) {
            int pos = CUR(p).pos; p->i++;
            Expr *r = parse_a_expr(p, 7);
            left = make_func(p, pos, "!", left, r);
        }
#endif
        /* multiplicative */
        while (prec <= 6 && CUR(p).kind == T_OP &&
               (!strcmp(CUR(p).sval, "*") || !strcmp(CUR(p).sval, "/") || !strcmp(CUR(p).sval, "%"))) {
            char *op = xstrdup(CUR(p).sval); int pos = CUR(p).pos; p->i++;
            Expr *r = parse_a_expr(p, 7);
            left = make_func(p, pos, op, left, r); free(op);
        }
        /* additive and other operators */
        while (prec <= 5 && CUR(p).kind == T_OP) {
            const char *o = CUR(p).sval;
            if (!strcmp(o,"=")||!strcmp(o,"<")||!strcmp(o,">")||!strcmp(o,"<=")||
                !strcmp(o,">=")||!strcmp(o,"<>")||!strcmp(o,"!=")) break;
            if (!strcmp(o,"!")) break;
            char *op = xstrdup(o); int pos = CUR(p).pos; p->i++;
            if ((at(p,"any") || at(p,"all") || at(p,"some")) && PEEK(p,1).kind == T_LPAREN) {
                int kind = at(p,"all") ? 4 : 3;
                p->i += 2;
                Expr *e = newx(X_ANYALL, pos);
                e->name = op; e->sublink_kind = kind;
                e->testexpr = left;
                if (at(p,"select") || at(p,"with")) { e->subquery = parse_select_stmt(p, NULL, 0); }
                else xpush(&e->args, &e->nargs, parse_expr(p));
                expect_kind(p, T_RPAREN);
                left = e;
                continue;
            }
            Expr *r = parse_a_expr(p, 6);
            left = make_func(p, pos, op, left, r); free(op);
        }
    }
    if (prec > 4) return left;

    /* comparison / IS / BETWEEN / IN / LIKE -- non-associative chain */
    for (;;) {
        int pos = CUR(p).pos;
        if (CUR(p).kind == T_OP) {
            const char *o = CUR(p).sval;
            if (!strcmp(o,"=")||!strcmp(o,"<")||!strcmp(o,">")||!strcmp(o,"<=")||
                !strcmp(o,">=")||!strcmp(o,"<>")||!strcmp(o,"!=")) {
                char *op = xstrdup(!strcmp(o,"!=") ? "<>" : o); p->i++;
                if ((at(p,"any") || at(p,"all") || at(p,"some")) && PEEK(p,1).kind == T_LPAREN) {
                    int kind = at(p,"all") ? 4 : 3;
                    p->i += 2;
                    Expr *e = newx(X_ANYALL, pos);
                    e->name = op; e->sublink_kind = kind; e->testexpr = left;
                    if (at(p,"select") || at(p,"with")) e->subquery = parse_select_stmt(p, NULL, 0);
                    else xpush(&e->args, &e->nargs, parse_expr(p));
                    expect_kind(p, T_RPAREN);
                    left = e; continue;
                }
                Expr *r = parse_a_expr(p, 5);
                left = make_func(p, pos, op, left, r); free(op);
                continue;
            }
            break;
        }
        if (at(p, "is")) {
            p->i++;
            Expr *e = newx(X_IS, pos);
            xpush(&e->args, &e->nargs, left);
            bool neg = accept_kw(p, "not");
            if (accept_kw(p, "null")) e->is_kind = neg ? 1 : 0;
            else if (accept_kw(p, "true")) e->is_kind = neg ? 3 : 2;
            else if (accept_kw(p, "false")) e->is_kind = neg ? 5 : 4;
            else if (accept_kw(p, "unknown")) e->is_kind = neg ? 7 : 6;
            else if (accept_kw(p, "distinct")) {
                expect_kw(p, "from");
                e->is_kind = neg ? 9 : 8;
                xpush(&e->args, &e->nargs, parse_a_expr(p, 5));
            }
#if PGVC_VN >= 160
            /* The IS JSON predicate is spelled in the grammar only from
             * PostgreSQL 16 on; earlier releases see a stray keyword. */
            else if (accept_kw(p, "json")) {
                e->tag = X_ISJSON; e->isjson_not = neg; e->isjson_kind = 0;
                if (accept_kw(p, "array")) e->isjson_kind = 1;
                else if (accept_kw(p, "object")) e->isjson_kind = 2;
                else if (accept_kw(p, "scalar")) e->isjson_kind = 3;
                else accept_kw(p, "value");
                if (accept_kw(p, "with")) { expect_kw(p, "unique"); accept_kw(p, "keys"); e->isjson_unique = true; }
                else if (accept_kw(p, "without")) { expect_kw(p, "unique"); accept_kw(p, "keys"); }
            }
#endif
            else if (accept_kw(p, "nan")) { e->tag = X_FUNC; e->name = xstrdup(neg ? "is_not_nan" : "is_nan"); }
#if PGVC_VN >= 130
            else if (at(p,"nfc") || at(p,"nfd") || at(p,"nfkc") || at(p,"nfkd")) {
                int form = at(p,"nfc") ? 0 : at(p,"nfd") ? 1 : at(p,"nfkc") ? 2 : 3;
                p->i++;
                expect_kw(p, "normalized");
                e->tag = X_FUNC; e->name = xstrdup("is_normalized");
                Expr *f = newx(X_CONST, pos);
                f->val = d_int(INT4OID, form);
                { char tb[4]; snprintf(tb, sizeof tb, "%d", form); f->strval = xstrdup(tb); }
                xpush(&e->args, &e->nargs, f);
                if (neg) { Expr *nn = newx(X_NOT, pos); xpush(&nn->args, &nn->nargs, e); e = nn; }
            }
#endif
            else if (accept_kw(p, "normalized")) {
                e->tag = X_FUNC; e->name = xstrdup("is_normalized");
                if (neg) { Expr *nn = newx(X_NOT, pos); xpush(&nn->args, &nn->nargs, e); e = nn; }
            }
            else syntax_error(p);
            left = e; continue;
        }
        if (at(p, "isnull")) { p->i++; Expr *e = newx(X_IS, pos); xpush(&e->args,&e->nargs,left); e->is_kind = 0; left = e; continue; }
        if (at(p, "notnull")) { p->i++; Expr *e = newx(X_IS, pos); xpush(&e->args,&e->nargs,left); e->is_kind = 1; left = e; continue; }
        bool neg = false;
        int save = p->i;
        if (at(p, "not") && (is_kw(p,1,"between") || is_kw(p,1,"in") || is_kw(p,1,"like") ||
                             is_kw(p,1,"ilike") || is_kw(p,1,"similar"))) { p->i++; neg = true; }
        if (at(p, "between")) {
            p->i++;
            Expr *e = newx(X_BETWEEN, pos);
            e->between_not = neg;
            if (accept_kw(p, "symmetric")) e->between_sym = true;
            else accept_kw(p, "asymmetric");
            xpush(&e->args, &e->nargs, left);
            xpush(&e->args, &e->nargs, parse_a_expr(p, 5));
            expect_kw(p, "and");
            xpush(&e->args, &e->nargs, parse_a_expr(p, 5));
            left = e; continue;
        }
        if (at(p, "in")) {
            p->i++;
            expect_kind(p, T_LPAREN);
            Expr *e = newx(X_IN, pos);
            e->between_not = neg;
            e->testexpr = left;
            if (at(p,"select") || at(p,"with") || at(p,"values")) e->subquery = parse_select_stmt(p, NULL, 0);
            else do xpush(&e->args, &e->nargs, parse_expr(p)); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
            left = e; continue;
        }
        if (at(p, "like") || at(p, "ilike") || at(p, "similar")) {
            bool ilike = at(p, "ilike"), sim = at(p, "similar");
            p->i++;
            if (sim) expect_kw(p, "to");
            Expr *e = newx(X_LIKE, pos);
            e->like_not = neg; e->like_ilike = ilike; e->like_similar = sim;
            xpush(&e->args, &e->nargs, left);
            xpush(&e->args, &e->nargs, parse_a_expr(p, 5));
            if (accept_kw(p, "escape")) e->like_escape = parse_a_expr(p, 5);
            left = e; continue;
        }
        if (neg) { p->i = save; }
        break;
    }
    return left;
}

static Expr *parse_expr(P *p)
{
    Expr *e = parse_a_expr(p, 2);
    while (at(p, "and")) {
        int pos = CUR(p).pos; p->i++;
        Expr *n = newx(X_AND, pos);
        xpush(&n->args, &n->nargs, e);
        xpush(&n->args, &n->nargs, parse_a_expr(p, 2));
        e = n;
    }
    while (at(p, "or")) {
        int pos = CUR(p).pos; p->i++;
        Expr *r = parse_a_expr(p, 2);
        while (at(p, "and")) {
            int p2 = CUR(p).pos; p->i++;
            Expr *n = newx(X_AND, p2);
            xpush(&n->args, &n->nargs, r);
            xpush(&n->args, &n->nargs, parse_a_expr(p, 2));
            r = n;
        }
        Expr *n = newx(X_OR, pos);
        xpush(&n->args, &n->nargs, e);
        xpush(&n->args, &n->nargs, r);
        e = n;
    }
    return e;
}

Expr *parse_expr_string(Session *s, const char *text)
{
    P p; memset(&p, 0, sizeof p);
    p.s = s; p.src = text;
    lex_init(&p.L, s, text);
    lex_all(&p.L);
    p.i = 0;
    return parse_expr(&p);
}

/* ------------------------------------------------------------- FROM items */
/* Does this expression name a column?  Such a function in FROM has to be
 * treated as LATERAL. */
static bool expr_has_colref(Expr *e)
{
    if (!e) return false;
    if (e->tag == X_COLREF || e->tag == X_STAR) return true;
    for (int i = 0; i < e->nargs; i++) if (expr_has_colref(e->args[i])) return true;
    if (expr_has_colref(e->case_arg) || expr_has_colref(e->case_else)) return true;
    for (int i = 0; i < e->nwhen; i++)
        if (expr_has_colref(e->when_cond[i]) || expr_has_colref(e->when_res[i])) return true;
    if (expr_has_colref(e->testexpr) || expr_has_colref(e->filter)) return true;
    if (expr_has_colref(e->like_escape)) return true;
    return false;
}

static RangeTbl *newrt(int kind)
{
    RangeTbl *r = xmalloc(sizeof *r);
    memset(r, 0, sizeof *r);
    r->kind = kind;
    return r;
}

static void parse_alias(P *p, RangeTbl *r)
{
    bool as = accept_kw(p, "as");
    if (as || (at_ident(p) && !at(p,"on") && !at(p,"using") && !at(p,"where") &&
               !at(p,"group") && !at(p,"order") && !at(p,"limit") && !at(p,"offset") &&
               !at(p,"having") && !at(p,"union") && !at(p,"intersect") && !at(p,"except") &&
               !at(p,"join") && !at(p,"inner") && !at(p,"left") && !at(p,"right") &&
               !at(p,"full") && !at(p,"cross") && !at(p,"natural") && !at(p,"window") &&
               !at(p,"for") && !at(p,"fetch") && !at(p,"returning") && !at(p,"set") &&
               !at(p,"when") && !at(p,"lateral") && !at(p,"with") && !at(p,"tablesample"))) {
        r->alias = ident_any(p);
        if (at_kind(p, T_LPAREN)) {
            p->i++;
            do spush(&r->colaliases, &r->ncolaliases, ident_any(p)); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
    }
}

#if PGVC_VN >= 170
static JsonTableCol *parse_jt_column(P *p, JsonTableCol ***all, int *nall);

/* COLUMNS ( ... ) of JSON_TABLE or of a NESTED PATH */
static void parse_jt_columns(P *p, JsonTableCol ***cols, int *ncols,
                             JsonTableCol ***all, int *nall)
{
    expect_kw(p, "columns");
    expect_kind(p, T_LPAREN);
    do {
        JsonTableCol *c = parse_jt_column(p, all, nall);
        *cols = xrealloc(*cols, (size_t)(*ncols + 1) * sizeof(JsonTableCol *));
        (*cols)[(*ncols)++] = c;
    } while (accept_kind(p, T_COMMA));
    expect_kind(p, T_RPAREN);
}

static JsonTableCol *parse_jt_column(P *p, JsonTableCol ***all, int *nall)
{
    JsonTableCol *c = xmalloc(sizeof *c);
    memset(c, 0, sizeof *c);
    c->on_empty = -1; c->on_error = -1; c->type = TEXTOID; c->typmod = -1;
    if (at(p, "nested")) {
        p->i++;
        accept_kw(p, "path");
        c->kind = 3;
        c->pathpos = CUR(p).pos;
        if (CUR(p).kind != T_STRING) syntax_error(p);
        c->path = xstrdup(CUR(p).sval); p->i++;
        { char *rest = jsonpath_leftover(p->s, c->path);
          if (rest) ereport_pos(p->s, "42601", c->pathpos,
                                "syntax error at or near \"%s\" of jsonpath input", rest); }
        if (accept_kw(p, "as")) c->name = ident_any(p);
        parse_jt_columns(p, &c->cols, &c->ncols, all, nall);
        return c;
    }
    c->namepos = CUR(p).pos;
    c->name = ident_any(p);
    /* the name must be unique across the whole JSON_TABLE */
    for (int i = 0; i < *nall; i++)
        if ((*all)[i]->name && !strcmp((*all)[i]->name, c->name))
            ereport_pos(p->s, "42701", c->namepos,
                        "duplicate JSON_TABLE column or path name: %s", c->name);
    *all = xrealloc(*all, (size_t)(*nall + 1) * sizeof(JsonTableCol *));
    (*all)[(*nall)++] = c;
    if (at(p, "for") && is_kw(p, 1, "ordinality")) {
        p->i += 2;
        c->kind = 1; c->type = INT4OID;
        return c;
    }
    { Oid t; int32 tm;
      c->typname = type_name_text(p, &t, &tm);
      if (!t) ereport_pos(p->s, "42704", c->namepos, "type \"%s\" does not exist", c->typname);
      c->type = t; c->typmod = tm; }
    if (accept_kw(p, "format")) { accept_kw(p, "json"); c->format_json = true;
                                  if (accept_kw(p, "encoding")) p->i++; }
    if (accept_kw(p, "exists")) {
        c->kind = 2;
        if (accept_kw(p, "path")) { c->pathpos = CUR(p).pos;
                                    if (CUR(p).kind != T_STRING) syntax_error(p);
                                    c->path = xstrdup(CUR(p).sval); p->i++; }
    } else if (accept_kw(p, "path")) {
        c->pathpos = CUR(p).pos;
        if (CUR(p).kind != T_STRING) syntax_error(p);
        c->path = xstrdup(CUR(p).sval); p->i++;
    }
    if (!c->path) {
        Buf b; buf_init(&b);
        buf_printf(&b, "$.\"%s\"", c->name);
        c->path = buf_steal(&b);
    } else {
        char *rest = jsonpath_leftover(p->s, c->path);
        if (rest) ereport_pos(p->s, "42601", c->pathpos,
                              "syntax error at or near \"%s\" of jsonpath input", rest);
    }
    /* wrapper / quotes / ON EMPTY / ON ERROR */
    {
        Expr *tmp = newx(X_JSONCTOR, CUR(p).pos);
        parse_json_query_opts(p, tmp, c->kind == 0, false);
        JsonBehav *bh = tmp->json_behav;
        c->wrapper = bh->wrapper; c->quotes = bh->quotes;
        c->on_empty = bh->on_empty; c->on_error = bh->on_error;
        c->empty_default = bh->empty_default; c->error_default = bh->error_default;
    }
    return c;
}

static RangeTbl *parse_json_table(P *p)
{
    int fnpos = CUR(p).pos;
    (void)fnpos;
    p->i++;                              /* json_table */
    expect_kind(p, T_LPAREN);
    JsonTableDef *jt = xmalloc(sizeof *jt);
    memset(jt, 0, sizeof *jt);
    jt->ctx = parse_a_expr(p, 3);
    if (accept_kw(p, "format")) accept_kw(p, "json");
    expect_kind(p, T_COMMA);
    jt->pathpos = CUR(p).pos;
    { bool plit = (CUR(p).kind == T_STRING);
      char *plitv = plit ? xstrdup(CUR(p).sval) : NULL;
      jt->path = parse_a_expr(p, 3);
      if (plitv) {
          char *rest = jsonpath_leftover(p->s, plitv);
          if (rest) ereport_pos(p->s, "42601", jt->pathpos,
                                "syntax error at or near \"%s\" of jsonpath input", rest);
          free(plitv);
      } }
    if (accept_kw(p, "as")) free(ident_any(p));
    if (accept_kw(p, "passing")) {
        do {
            Expr *v = parse_a_expr(p, 3);
            expect_kw(p, "as");
            char *nm = ident_any(p);
            jt->passing = xrealloc(jt->passing, (size_t)(jt->npassing + 1) * sizeof(Expr *));
            jt->passnames = xrealloc(jt->passnames, (size_t)(jt->npassing + 1) * sizeof(char *));
            jt->passing[jt->npassing] = v;
            jt->passnames[jt->npassing] = nm;
            jt->npassing++;
        } while (accept_kind(p, T_COMMA));
    }
    JsonTableCol **all = NULL; int nall = 0;
    parse_jt_columns(p, &jt->cols, &jt->ncols, &all, &nall);
    free(all);
    if (accept_kw(p, "error")) { expect_kw(p, "on"); expect_kw(p, "error"); jt->on_error = 1; }
    else if (accept_kw(p, "empty")) { accept_kw(p, "array"); expect_kw(p, "on"); expect_kw(p, "error");
                                       jt->on_error = 0; }
    expect_kind(p, T_RPAREN);
    RangeTbl *r = newrt(7);
    r->jtable = jt;
    parse_alias(p, r);
    return r;
}
#endif

static RangeTbl *parse_from_atom(P *p)
{
    RangeTbl *r;
    bool lateral = accept_kw(p, "lateral");
    if (at_kind(p, T_LPAREN)) {
        int save = p->i;
        p->i++;
        if (at(p, "select") || at(p, "with") || at(p, "values") || at(p, "table")) {
            Node *q = parse_select_stmt(p, NULL, 0);
            expect_kind(p, T_RPAREN);
            r = newrt(1); r->subquery = q; r->lateral = lateral;
            parse_alias(p, r);
            return r;
        }
        p->i = save;
        p->i++;
        r = parse_from_item(p);
        expect_kind(p, T_RPAREN);
        parse_alias(p, r);
        return r;
    }
    if (at(p, "values")) {
        Node *q = parse_select_stmt(p, NULL, 0);
        r = newrt(1); r->subquery = q;
        parse_alias(p, r);
        return r;
    }
    if (at(p, "xmltable") && PEEK(p,1).kind == T_LPAREN) {
        /* xmltable(row_expr PASSING doc COLUMNS name type PATH expr [, ...]) */
        p->i++;
        expect_kind(p, T_LPAREN);
        XmlTableDef *xt = xmalloc(sizeof *xt);
        memset(xt, 0, sizeof *xt);
        accept_kw(p, "xmlnamespaces");
        xt->rowexpr = parse_expr(p);
        expect_kw(p, "passing");
        if (accept_kw(p, "by")) { accept_kw(p, "value"); accept_kw(p, "ref"); }
        xt->docexpr = parse_expr(p);
        expect_kw(p, "columns");
        do {
            XmlTableCol col; memset(&col, 0, sizeof col);
            col.name = ident_any(p);
            if (accept_kw(p, "for")) { expect_kw(p, "ordinality"); col.forordinality = true;
                                       col.type = INT4OID; col.typmod = -1; }
            else {
                Oid t; int32 tm;
                free(type_name_text(p, &t, &tm));
                col.type = t; col.typmod = tm;
                for (;;) {
                    if (accept_kw(p, "path")) { col.pathexpr = parse_expr(p); }
                    else if (accept_kw(p, "default")) { col.defaultexpr = parse_expr(p); }
                    else if (at(p,"not") && is_kw(p,1,"null")) { p->i += 2; col.notnull = true; }
                    else if (at(p,"null") && !is_kw(p,1,"null")) { p->i++; col.notnull = false; }
                    else break;
                }
                if (!col.pathexpr) col.path = xstrdup(col.name);
            }
            xt->cols = xrealloc(xt->cols, (size_t)(xt->ncols + 1) * sizeof(XmlTableCol));
            xt->cols[xt->ncols++] = col;
        } while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
        r = newrt(6); r->xmlt = xt; r->lateral = lateral;
        parse_alias(p, r);
        return r;
    }
#if PGVC_VN >= 170
    if (at(p, "json_table") && PEEK(p,1).kind == T_LPAREN) {
        r = parse_json_table(p);
        r->lateral = true;
        return r;
    }
#endif
    /* relation or set-returning function */
    {
        int save = p->i;
        char *sch = NULL, *nm = NULL;
        bool only = accept_kw(p, "only");
        if (only && at_kind(p, T_LPAREN)) p->i++;
        qname(p, &sch, &nm);
        if (at_kind(p, T_LPAREN)) {
            p->i = save;
            int sp3 = CUR(p).pos;
            Expr *f = parse_postfix2(p, parse_primary(p), sp3);
            /* a function in FROM is implicitly LATERAL, so it may name a
             * column of an earlier FROM item */
            r = newrt(3); r->funcexpr = f;
            r->lateral = lateral || expr_has_colref(f);
            if (accept_kw(p, "with")) { expect_kw(p, "ordinality"); r->ordinality = true; }
            parse_alias(p, r);
            free(sch); free(nm);
            return r;
        }
        r = newrt(0); r->pos = p->L.toks[save].pos; r->schema = sch; r->relname = nm; r->only = only;
        if (only) accept_kind(p, T_RPAREN);
        if (accept_op(p, "*")) { }
        parse_alias(p, r);
        if (accept_kw(p, "tablesample")) { free(ident_any(p)); expect_kind(p, T_LPAREN);
            while (!at_kind(p, T_RPAREN)) p->i++; expect_kind(p, T_RPAREN);
            if (accept_kw(p, "repeatable")) { expect_kind(p, T_LPAREN); while (!at_kind(p, T_RPAREN)) p->i++; expect_kind(p, T_RPAREN); } }
        return r;
    }
}

static RangeTbl *parse_from_item(P *p)
{
    RangeTbl *left = parse_from_atom(p);
    for (;;) {
        bool natural = false;
        int jt = -1;
        int save = p->i;
        if (accept_kw(p, "natural")) natural = true;
        if (accept_kw(p, "cross")) { expect_kw(p, "join"); jt = 4; }
        else if (accept_kw(p, "inner")) { expect_kw(p, "join"); jt = 0; }
        else if (accept_kw(p, "left"))  { accept_kw(p, "outer"); expect_kw(p, "join"); jt = 1; }
        else if (accept_kw(p, "right")) { accept_kw(p, "outer"); expect_kw(p, "join"); jt = 2; }
        else if (accept_kw(p, "full"))  { accept_kw(p, "outer"); expect_kw(p, "join"); jt = 3; }
        else if (accept_kw(p, "join"))  { jt = 0; }
        else { p->i = save; break; }
        RangeTbl *right = parse_from_atom(p);
        RangeTbl *j = newrt(2);
        j->jointype = jt; j->left = left; j->right = right; j->natural = natural;
        if (!natural && jt != 4) {
            if (accept_kw(p, "on")) j->joinqual = parse_expr(p);
            else if (accept_kw(p, "using")) {
                expect_kind(p, T_LPAREN);
                do spush(&j->usingcols, &j->nusing, ident_any(p)); while (accept_kind(p, T_COMMA));
                expect_kind(p, T_RPAREN);
                if (accept_kw(p, "as")) j->alias = ident_any(p);
            }
        }
        left = j;
    }
    return left;
}

/* ------------------------------------------------------------------ CTEs */
static CteDef **parse_with(P *p, int *nctes, bool *recursive)
{
    CteDef **list = NULL; int n = 0;
    expect_kw(p, "with");
    *recursive = accept_kw(p, "recursive");
    do {
        CteDef *c = xmalloc(sizeof *c);
        memset(c, 0, sizeof *c);
        c->recursive = *recursive;
        c->name_pos = CUR(p).pos;
        c->name = ident(p);
        if (at_kind(p, T_LPAREN)) {
            p->i++;
            do spush(&c->cols, &c->ncols, ident_any(p)); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
        expect_kw(p, "as");
        if (accept_kw(p, "not")) { expect_kw(p, "materialized"); c->materialized = -1; }
        else if (accept_kw(p, "materialized")) c->materialized = 1;
        expect_kind(p, T_LPAREN);
#if PGVC_VN < 170
        /* PostgreSQL 17 gained MERGE ... RETURNING, and with it MERGE in WITH */
        if (at(p, "merge"))
            ereport_pos(p->s, "0A000", c->name_pos, "MERGE not supported in WITH query");
#endif
        c->query = parse_stmt(p);
        expect_kind(p, T_RPAREN);
        if (at(p, "search")) {
            c->search_pos = CUR(p).pos;
            expect_kw(p, "search");
            if (accept_kw(p, "breadth")) c->search_kind = 1;
            else { expect_kw(p, "depth"); c->search_kind = 0; }
            expect_kw(p, "first"); expect_kw(p, "by");
            c->search_by = parse_sortlist(p, &c->nsearch);
            expect_kw(p, "set");
            c->search_col = newx(X_COLREF, CUR(p).pos);
            c->search_col->colname = ident_any(p);
        }
        if (at(p, "cycle")) {
            c->cycle_pos = CUR(p).pos;
            expect_kw(p, "cycle");
            do xpush(&c->cycle_cols, &c->ncycle, parse_expr(p)); while (accept_kind(p, T_COMMA));
            expect_kw(p, "set");
            c->cycle_col = ident_any(p);
            c->has_cycle = true;
            if (accept_kw(p, "to")) {
                c->cycle_mark_e = parse_expr(p);
                expect_kw(p, "default");
                c->cycle_default_e = parse_expr(p);
            }
            expect_kw(p, "using");
            c->cycle_path = ident_any(p);
        }
        list = xrealloc(list, (size_t)(n + 1) * sizeof(CteDef *));
        list[n++] = c;
    } while (accept_kind(p, T_COMMA));
    *nctes = n;
    return list;
}

/* ---------------------------------------------------------------- SELECT */
static SelectStmt *newsel(void)
{
    SelectStmt *s = xmalloc(sizeof *s);
    memset(s, 0, sizeof *s);
    return s;
}

static SelectStmt *parse_simple_select(P *p);

static SelectStmt *parse_select_body(P *p)
{
    SelectStmt *s = parse_simple_select(p);
    for (;;) {
        int op;
        if (accept_kw(p, "union")) op = 1;
        else if (accept_kw(p, "intersect")) op = 2;
        else if (accept_kw(p, "except")) op = 3;
        else break;
        bool all = accept_kw(p, "all");
        if (!all) accept_kw(p, "distinct");
        SelectStmt *r = parse_simple_select(p);
        SelectStmt *n = newsel();
        n->setop = op; n->setop_all = all; n->larg = s; n->rarg = r;
        s = n;
    }
    /* trailing ORDER BY / LIMIT apply to the whole set operation */
    if (accept_kw(p, "order")) { expect_kw(p, "by"); s->orderby = parse_sortlist(p, &s->norderby); }
    for (;;) {
        if (accept_kw(p, "limit")) {
            if (accept_kw(p, "all")) s->limit = NULL; else s->limit = parse_expr(p);
            if (accept_kind(p, T_COMMA)) { s->offset = s->limit; s->limit = parse_expr(p); }
            continue;
        }
        if (accept_kw(p, "offset")) {
            s->offset = parse_expr(p);
            if (at(p,"row") || at(p,"rows")) p->i++;
            continue;
        }
        if (accept_kw(p, "fetch")) {
            int cpos = CUR(p).pos;
            if (!accept_kw(p, "first")) expect_kw(p, "next");
            bool have_count = false;
            if (!at(p,"row") && !at(p,"rows")) {
                cpos = CUR(p).pos;
                s->limit = parse_expr(p);
                have_count = true;
            }
            /* row_or_rows is not optional in the grammar */
            if (at(p,"row") || at(p,"rows")) p->i++; else syntax_error(p);
            if (at(p, "with")) { s->with_ties_pos = CUR(p).pos; p->i++;
                                 expect_kw(p, "ties"); s->with_ties = true; }
            else expect_kw(p, "only");
            if (!have_count) {
                Expr *one = newx(X_CONST, cpos);
                one->strval = xstrdup("1");
                one->val = d_int(INT4OID, 1);
                s->limit = one;
            } else if (s->with_ties && s->limit->tag == X_CONST && s->limit->isnull_const) {
                ereport(p->s, "2201W",
                        "row count cannot be null in FETCH FIRST ... WITH TIES clause");
            }
            s->fetch_first = true;
            continue;
        }
        break;
    }
    if (s->with_ties && !s->norderby)
        /* PostgreSQL 18 started pointing the caret at the WITH keyword */
        ereport_pos(p->s, "42601", PGVC_VN >= 180 ? s->with_ties_pos : 0,
                    "WITH TIES cannot be specified without ORDER BY clause");
    if (at(p, "for")) {
        p->i++;
        if (accept_kw(p, "update")) s->lock_kind = 1;
        else if (accept_kw(p, "share")) s->lock_kind = 2;
        else if (accept_kw(p, "no")) { expect_kw(p, "key"); expect_kw(p, "update"); s->lock_kind = 3; }
        else if (accept_kw(p, "key")) { expect_kw(p, "share"); s->lock_kind = 4; }
        else syntax_error(p);
        if (accept_kw(p, "of")) do { char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); free(b); } while (accept_kind(p, T_COMMA));
        if (accept_kw(p, "nowait")) s->lock_nowait = true;
        else if (accept_kw(p, "skip")) { expect_kw(p, "locked"); s->lock_skip = true; }
    }
    return s;
}

static SelectStmt *parse_simple_select(P *p)
{
    if (at_kind(p, T_LPAREN)) {
        p->i++;
        SelectStmt *s = parse_select_body(p);
        expect_kind(p, T_RPAREN);
        return s;
    }
    if (accept_kw(p, "values")) {
        SelectStmt *s = newsel();
        do {
            expect_kind(p, T_LPAREN);
            Expr **row = NULL; int nr = 0;
            do {
                if (at(p, "default") && (PEEK(p,1).kind == T_COMMA || PEEK(p,1).kind == T_RPAREN)) {
                    p->i++;
                    Expr *e = newx(X_CONST, CUR(p).pos); e->strval = xstrdup("DEFAULT");
                    e->isnull_const = true; e->alias = xstrdup("__default__");
                    xpush(&row, &nr, e);
                } else xpush(&row, &nr, parse_expr(p));
            } while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
            if (s->nvalcols && nr != s->nvalcols)
                ereport(p->s, "42601", "VALUES lists must all be the same length");
            s->nvalcols = nr;
            s->values = xrealloc(s->values, (size_t)(s->nvalrows + 1) * sizeof(Expr **));
            s->values[s->nvalrows++] = row;
        } while (accept_kind(p, T_COMMA));
        return s;
    }
    if (accept_kw(p, "table")) {
        SelectStmt *s = newsel();
        Expr *star = newx(X_STAR, CUR(p).pos);
        xpush(&s->target, &s->ntarget, star);
        RangeTbl *r = newrt(0);
        qname(p, &r->schema, &r->relname);
        s->from = xrealloc(s->from, sizeof(RangeTbl *));
        s->from[s->nfrom++] = r;
        return s;
    }
    expect_kw(p, "select");
    SelectStmt *s = newsel();
    if (accept_kw(p, "distinct")) {
        s->distinct = true;
        if (accept_kw(p, "on")) {
            expect_kind(p, T_LPAREN);
            do xpush(&s->distinct_on, &s->ndistinct_on, parse_expr(p)); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
    } else accept_kw(p, "all");

    if (!at(p, "from") && !at(p, "into")) {
        do {
            Expr *e;
            if (at_op(p, "*") && (PEEK(p,1).kind == T_COMMA || is_kw(p,1,"from") ||
                                  PEEK(p,1).kind == T_EOF || PEEK(p,1).kind == T_SEMI ||
                                  PEEK(p,1).kind == T_RPAREN || is_kw(p,1,"into"))) {
                p->i++;
                e = newx(X_STAR, CUR(p).pos);
            } else {
                e = parse_expr(p);
                if (accept_kw(p, "as")) e->alias = ident_any(p);
                else if (at_ident(p) && !CUR(p).iskw) e->alias = ident_any(p);
                else if (at_ident(p) && CUR(p).iskw && !kw_is_reserved(CUR(p).sval) &&
                         !at(p,"from") && !at(p,"into")) e->alias = ident_any(p);
            }
            xpush(&s->target, &s->ntarget, e);
        } while (accept_kind(p, T_COMMA));
    }
    if (accept_kw(p, "into")) {    /* SELECT INTO: parsed, executed as CREATE TABLE AS */
        accept_kw(p, "temporary"); accept_kw(p, "temp"); accept_kw(p, "unlogged");
        accept_kw(p, "table");
        char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); free(b);
    }
    if (accept_kw(p, "from")) {
        do {
            s->from = xrealloc(s->from, (size_t)(s->nfrom + 1) * sizeof(RangeTbl *));
            s->from[s->nfrom++] = parse_from_item(p);
        } while (accept_kind(p, T_COMMA));
    }
    if (accept_kw(p, "where")) s->where = parse_expr(p);
    if (accept_kw(p, "group")) {
        expect_kw(p, "by");
        /* GROUP BY [ ALL | DISTINCT ] ... (PostgreSQL 14) */
        if (!accept_kw(p, "all")) {
            if (at(p, "distinct")) {
#if PGVC_VN < 140
                syntax_error(p);
#else
                p->i++;
                s->group_distinct = true;
#endif
            }
        }
        do {
            if (at(p, "distinct") || at(p, "all")) syntax_error(p);
            if (at(p, "rollup") || at(p, "cube") || at(p, "grouping")) {
                char *k = ident_any(p);
                if (!strcmp(k, "grouping")) expect_kw(p, "sets");
                expect_kind(p, T_LPAREN);
                Expr *g = newx(X_LIST, CUR(p).pos);
                g->name = k;
                if (!at_kind(p, T_RPAREN))
                    do {
                        if (at_kind(p, T_LPAREN)) {
                            p->i++;
                            Expr *sub = newx(X_LIST, CUR(p).pos);
                            sub->name = xstrdup("()");
                            if (!at_kind(p, T_RPAREN))
                                do xpush(&sub->args, &sub->nargs, parse_expr(p)); while (accept_kind(p, T_COMMA));
                            expect_kind(p, T_RPAREN);
                            xpush(&g->args, &g->nargs, sub);
                        } else xpush(&g->args, &g->nargs, parse_expr(p));
                    } while (accept_kind(p, T_COMMA));
                expect_kind(p, T_RPAREN);
                xpush(&s->groupby, &s->ngroupby, g);
            } else xpush(&s->groupby, &s->ngroupby, parse_expr(p));
        } while (accept_kind(p, T_COMMA));
    }
    if (accept_kw(p, "having")) s->having = parse_expr(p);
    if (accept_kw(p, "window")) {
        do {
            char *nm = ident(p);
            expect_kw(p, "as");
            WindowDef *w = parse_window_spec(p);
            w->name = nm;
            s->windows = xrealloc(s->windows, (size_t)(s->nwindows + 1) * sizeof(WindowDef *));
            s->windows[s->nwindows++] = w;
        } while (accept_kind(p, T_COMMA));
    }
    return s;
}

static Node *mknode(NodeTag tag, void *x)
{
    Node *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->tag = tag; n->x = x;
    return n;
}

static Node *parse_select_stmt(P *p, CteDef **ctes, int nctes)
{
    bool rec = false;
    if (!ctes && at(p, "with")) ctes = parse_with(p, &nctes, &rec);
    SelectStmt *s = parse_select_body(p);
    SelectStmt *root = s;
    while (root->setop) root = root->larg;
    s->ctes = ctes; s->nctes = nctes; s->recursive = rec;
    Node *n = mknode(N_SELECT, NULL);
    n->select = s;
    return n;
}

/* ---------------------------------------------------------------- INSERT */
static Expr **parse_returning(P *p, int *n)
{
    Expr **list = NULL; *n = 0;
    p->ret_aliases = false; p->ret_oldalias = NULL; p->ret_newalias = NULL;
#if PGVC_VN >= 180
    if (at(p, "with") && PEEK(p,1).kind == T_LPAREN) {
        p->i += 2;
        p->ret_aliases = true;
        if (at_kind(p, T_RPAREN)) syntax_error(p);
        do {
            int kpos = CUR(p).pos;
            int which;
            if (accept_kw(p, "old")) which = 1;
            else if (accept_kw(p, "new")) which = 2;
            else syntax_error(p);
            expect_kw(p, "as");
            char *nm = ident_any(p);
            if (which == 1 && p->ret_oldalias)
                ereport_pos(p->s, "42601", kpos, "OLD cannot be specified multiple times");
            if (which == 2 && p->ret_newalias)
                ereport_pos(p->s, "42601", kpos, "NEW cannot be specified multiple times");
            if (p->ret_relname && !strcmp(nm, p->ret_relname))
                ereport_pos(p->s, "42712", kpos,
                            "table name \"%s\" specified more than once", nm);
            if (which == 1) p->ret_oldalias = nm; else p->ret_newalias = nm;
        } while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
    }
#endif
    do {
        Expr *e;
        if (at_op(p, "*")) { p->i++; e = newx(X_STAR, CUR(p).pos); }
        else {
            e = parse_expr(p);
            if (accept_kw(p, "as")) e->alias = ident_any(p);
            else if (at_ident(p) && !CUR(p).iskw) e->alias = ident_any(p);
        }
        xpush(&list, n, e);
    } while (accept_kind(p, T_COMMA));
    p->ret_aliases = false; p->ret_oldalias = NULL; p->ret_newalias = NULL;
    return list;
}

static Node *parse_insert(P *p, CteDef **ctes, int nctes)
{
    expect_kw(p, "insert"); expect_kw(p, "into");
    InsertStmt *st = xmalloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->pos = CUR(p).pos;
    st->ctes = ctes; st->nctes = nctes;
    qname(p, &st->schema, &st->name);
    if (accept_kw(p, "as")) { free(ident_any(p)); }
    if (at_kind(p, T_LPAREN)) {
        p->i++;
        do spush(&st->cols, &st->ncols, ident_any(p)); while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
    }
    if (accept_kw(p, "overriding")) {
        if (accept_kw(p, "system")) st->overriding = 1;
        else { expect_kw(p, "user"); st->overriding = 2; }
        expect_kw(p, "value");
    }
    if (accept_kw(p, "default")) { expect_kw(p, "values"); st->default_values = true; }
    else st->query = parse_select_stmt(p, NULL, 0);
    if (accept_kw(p, "on")) {
        expect_kw(p, "conflict");
        if (at_kind(p, T_LPAREN)) {
            p->i++;
            do spush(&st->conf_cols, &st->nconf_cols, ident_any(p)); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
            if (accept_kw(p, "where")) st->conf_where = parse_expr(p);
        } else if (accept_kw(p, "on")) { expect_kw(p, "constraint"); st->conf_constraint = ident(p); }
        expect_kw(p, "do");
        if (accept_kw(p, "nothing")) st->onconflict = 1;
        else {
            expect_kw(p, "update"); expect_kw(p, "set");
            st->onconflict = 2;
            do {
                spush(&st->set_cols, &st->nset_cols, ident_any(p));
                if (!accept_op(p, "=")) syntax_error(p);
                st->set_vals = xrealloc(st->set_vals, (size_t)st->nset_cols * sizeof(Expr *));
                st->set_vals[st->nset_cols - 1] = parse_expr(p);
            } while (accept_kind(p, T_COMMA));
            if (accept_kw(p, "where")) st->set_where = parse_expr(p);
        }
    }
    if (accept_kw(p, "returning")) {
        p->ret_relname = st->name;
        st->returning = parse_returning(p, &st->nret);
        p->ret_relname = NULL;
    }
    return mknode(N_INSERT, st);
}

static Node *parse_update(P *p, CteDef **ctes, int nctes)
{
    expect_kw(p, "update");
    UpdateStmt *st = xmalloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->ctes = ctes; st->nctes = nctes;
    st->only = accept_kw(p, "only");
    qname(p, &st->schema, &st->name);
    accept_op(p, "*");
    if (accept_kw(p, "as")) st->alias = ident_any(p);
    else if (at_ident(p) && !at(p,"set")) st->alias = ident_any(p);
    expect_kw(p, "set");
    do {
        if (at_kind(p, T_LPAREN)) {      /* (a,b) = (x,y) */
            p->i++;
            char **cols = NULL; int nc = 0;
            do spush(&cols, &nc, ident_any(p)); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
            if (!accept_op(p, "=")) syntax_error(p);
            expect_kind(p, T_LPAREN);
            for (int k = 0; k < nc; k++) {
                spush(&st->cols, &st->ncols, cols[k]);
                st->vals = xrealloc(st->vals, (size_t)st->ncols * sizeof(Expr *));
                st->vals[st->ncols - 1] = parse_expr(p);
                if (k < nc - 1) expect_kind(p, T_COMMA);
            }
            expect_kind(p, T_RPAREN);
            free(cols);
        } else {
            char *c = ident_any(p);
            Expr *target = NULL;
            while (at_kind(p, T_LBRACK)) {   /* a[1] = ... */
                p->i++;
                Expr *idx = parse_expr(p);
                expect_kind(p, T_RBRACK);
                Expr *sub = newx(X_SUBSCRIPT, CUR(p).pos);
                Expr *base = target ? target : ({ Expr *b = newx(X_COLREF, CUR(p).pos); b->colname = xstrdup(c); b; });
                xpush(&sub->args, &sub->nargs, base);
                xpush(&sub->args, &sub->nargs, idx);
                target = sub;
            }
            if (!accept_op(p, "=")) syntax_error(p);
            spush(&st->cols, &st->ncols, c);
            st->vals = xrealloc(st->vals, (size_t)st->ncols * sizeof(Expr *));
            Expr *v;
            if (at(p, "default") && (PEEK(p,1).kind == T_COMMA || PEEK(p,1).kind == T_SEMI ||
                                     PEEK(p,1).kind == T_EOF || is_kw(p,1,"where") ||
                                     is_kw(p,1,"from") || is_kw(p,1,"returning"))) {
                v = newx(X_CONST, CUR(p).pos);
                v->alias = xstrdup("__default__"); v->isnull_const = true;
                p->i++;
            } else v = parse_expr(p);
            if (target) { /* rewrite as assignment into a subscript */
                Expr *asg = newx(X_SUBSCRIPT, v->pos);
                asg->is_kind = 2;
                xpush(&asg->args, &asg->nargs, target);
                xpush(&asg->args, &asg->nargs, v);
                v = asg;
            }
            st->vals[st->ncols - 1] = v;
        }
    } while (accept_kind(p, T_COMMA));
    if (accept_kw(p, "from")) {
        do {
            st->from = xrealloc(st->from, (size_t)(st->nfrom + 1) * sizeof(RangeTbl *));
            st->from[st->nfrom++] = parse_from_item(p);
        } while (accept_kind(p, T_COMMA));
    }
    if (accept_kw(p, "where")) st->where = parse_expr(p);
    if (accept_kw(p, "returning")) {
        p->ret_relname = st->alias ? st->alias : st->name;
        st->returning = parse_returning(p, &st->nret);
        p->ret_relname = NULL;
    }
    return mknode(N_UPDATE, st);
}

static Node *parse_delete(P *p, CteDef **ctes, int nctes)
{
    expect_kw(p, "delete"); expect_kw(p, "from");
    DeleteStmt *st = xmalloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->ctes = ctes; st->nctes = nctes;
    st->only = accept_kw(p, "only");
    qname(p, &st->schema, &st->name);
    accept_op(p, "*");
    if (accept_kw(p, "as")) st->alias = ident_any(p);
    else if (at_ident(p) && !at(p,"using") && !at(p,"where") && !at(p,"returning")) st->alias = ident_any(p);
    if (accept_kw(p, "using")) {
        do {
            st->using_ = xrealloc(st->using_, (size_t)(st->nusing + 1) * sizeof(RangeTbl *));
            st->using_[st->nusing++] = parse_from_item(p);
        } while (accept_kind(p, T_COMMA));
    }
    if (accept_kw(p, "where")) st->where = parse_expr(p);
    if (accept_kw(p, "returning")) {
        p->ret_relname = st->alias ? st->alias : st->name;
        st->returning = parse_returning(p, &st->nret);
        p->ret_relname = NULL;
    }
    return mknode(N_DELETE, st);
}

static Node *parse_merge(P *p, CteDef **ctes, int nctes)
{
    expect_kw(p, "merge"); expect_kw(p, "into");
    MergeStmt *st = xmalloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->ctes = ctes; st->nctes = nctes;
    accept_kw(p, "only");
    st->relpos = CUR(p).pos;
    qname(p, &st->schema, &st->name);
    if (accept_kw(p, "as")) st->alias = ident_any(p);
    else if (at_ident(p) && !at(p,"using")) st->alias = ident_any(p);
    expect_kw(p, "using");
    st->source = parse_from_atom(p);
    expect_kw(p, "on");
    st->on = parse_expr(p);
    if (!at(p, "when")) syntax_error(p);        /* at least one WHEN clause */
    while (at(p, "when")) {
        p->i++;
        MergeWhen *w = xmalloc(sizeof *w);
        memset(w, 0, sizeof *w);
        if (accept_kw(p, "not")) {
            expect_kw(p, "matched");
            w->kind = 1;
            if (accept_kw(p, "by")) {
                if (accept_kw(p, "source")) w->kind = 2;
                else { expect_kw(p, "target"); w->kind = 1; }
            }
        } else { expect_kw(p, "matched"); w->kind = 0; }
        if (accept_kw(p, "and")) w->cond = parse_expr(p);
        expect_kw(p, "then");
        /* only INSERT is allowed on the NOT MATCHED side, and only UPDATE and
         * DELETE on the MATCHED side */
        if (w->kind == 1 && (at(p, "update") || at(p, "delete"))) syntax_error(p);
        if (w->kind != 1 && at(p, "insert")) syntax_error(p);
        if (accept_kw(p, "do")) { expect_kw(p, "nothing"); w->action = 3; }
        else if (accept_kw(p, "delete")) w->action = 1;
        else if (accept_kw(p, "update")) {
            expect_kw(p, "set");
            w->action = 0;
            do {
                int cp = CUR(p).pos;
                spush(&w->cols, &w->ncols, ident_any(p));
                w->colpos = xrealloc(w->colpos, (size_t)w->ncols * sizeof(int));
                w->colpos[w->ncols - 1] = cp;
                if (!accept_op(p, "=")) syntax_error(p);
                w->vals = xrealloc(w->vals, (size_t)w->ncols * sizeof(Expr *));
                if (at(p, "default") && (PEEK(p,1).kind == T_COMMA || PEEK(p,1).kind == T_SEMI ||
                                         PEEK(p,1).kind == T_EOF || is_kw(p,1,"when") ||
                                         is_kw(p,1,"returning"))) {
                    Expr *e = newx(X_CONST, CUR(p).pos);
                    e->alias = xstrdup("__default__"); e->isnull_const = true;
                    p->i++;
                    w->vals[w->ncols - 1] = e;
                } else w->vals[w->ncols - 1] = parse_expr(p);
            } while (accept_kind(p, T_COMMA));
        } else {
            /* only WHEN NOT MATCHED [BY TARGET] can insert */
            if (w->kind != 1) syntax_error(p);
            expect_kw(p, "insert");
            w->action = 2;
            if (at_kind(p, T_LPAREN)) {
                p->i++;
                do spush(&w->cols, &w->ncols, ident_any(p)); while (accept_kind(p, T_COMMA));
                expect_kind(p, T_RPAREN);
            }
            if (accept_kw(p, "overriding")) {
                if (accept_kw(p, "system")) w->overriding = 1; else { expect_kw(p, "user"); w->overriding = 2; }
                expect_kw(p, "value");
            }
            if (accept_kw(p, "default")) { expect_kw(p, "values"); w->ins_default_values = true; }
            else {
                expect_kw(p, "values");
                expect_kind(p, T_LPAREN);
                int nv = 0;
                do {
                    w->vals = xrealloc(w->vals, (size_t)(nv + 1) * sizeof(Expr *));
                    if (at(p, "default") && (PEEK(p,1).kind == T_COMMA || PEEK(p,1).kind == T_RPAREN)) {
                        p->i++;
                        Expr *e = newx(X_CONST, CUR(p).pos); e->alias = xstrdup("__default__"); e->isnull_const = true;
                        w->vals[nv++] = e;
                    } else w->vals[nv++] = parse_expr(p);
                } while (accept_kind(p, T_COMMA));
                expect_kind(p, T_RPAREN);
                if (w->ncols && w->ncols != nv)
                    ereport(p->s, "42601", "INSERT has more target columns than expressions");
                if (!w->ncols) w->ncols = -nv;     /* negative marks "all columns" */
            }
        }
        st->whens = xrealloc(st->whens, (size_t)(st->nwhens + 1) * sizeof(MergeWhen *));
        st->whens[st->nwhens++] = w;
    }
#if PGVC_VN < 170
    if (at(p, "returning")) syntax_error(p);    /* MERGE gained RETURNING at 17 */
#endif
    if (accept_kw(p, "returning")) {
        p->ret_relname = st->alias ? st->alias : st->name;
        st->returning = parse_returning(p, &st->nret);
        p->ret_relname = NULL;
    }
    /* an unconditional WHEN clause makes every later one of its kind dead */
    for (int i = 0; i < st->nwhens; i++) {
        if (st->whens[i]->cond) continue;
        for (int k = i + 1; k < st->nwhens; k++)
            if (st->whens[k]->kind == st->whens[i]->kind)
                ereport(p->s, "42601", "unreachable WHEN clause specified after "
                                       "unconditional WHEN clause");
    }
    return mknode(N_MERGE, st);
}

/* -------------------------------------------------------------------- DDL */
static char *expr_src(P *p, int from_tok, int to_tok)
{
    const char *a = p->L.toks[from_tok].start;
    const char *b = p->L.toks[to_tok > from_tok ? to_tok - 1 : from_tok].end;
    return xstrndup(a, (size_t)(b - a));
}

static char *parse_expr_src(P *p, Expr **out)
{
    int start = p->i;
    Expr *e = parse_expr(p);
    if (out) *out = e;
    return expr_src(p, start, p->i);
}

/* START WITH / INCREMENT BY / MINVALUE / MAXVALUE / CACHE / CYCLE after an
 * identity or sequence clause. */
static int64 seq_number(P *p)
{
    bool neg = false;
    if (accept_op(p, "-")) neg = true;
    else accept_op(p, "+");
    if (CUR(p).kind != T_NUMBER) syntax_error(p);
    int64 v = strtoll(CUR(p).sval, NULL, 10);
    p->i++;
    return neg ? -v : v;
}

static void parse_identity_seqopts(P *p, ColDef *c)
{
    if (!at_kind(p, T_LPAREN)) return;
    p->i++;
    while (!at_kind(p, T_RPAREN) && !at_kind(p, T_EOF)) {
        if (accept_kw(p, "start")) { accept_kw(p, "with");
            c->seq_start = seq_number(p); c->has_seq_start = true; }
        else if (accept_kw(p, "increment")) { accept_kw(p, "by");
            c->seq_inc = seq_number(p); c->has_seq_inc = true; }
        else if (accept_kw(p, "minvalue")) { c->seq_min = seq_number(p); c->has_seq_min = true; }
        else if (accept_kw(p, "maxvalue")) { c->seq_max = seq_number(p); c->has_seq_max = true; }
        else if (accept_kw(p, "cache")) { c->seq_cache = seq_number(p); }
        else if (accept_kw(p, "cycle")) { c->seq_cycle = true; }
        else if (accept_kw(p, "no")) { accept_kw(p, "cycle"); accept_kw(p, "minvalue");
                                       accept_kw(p, "maxvalue"); }
        else p->i++;
        accept_kind(p, T_COMMA);
    }
    expect_kind(p, T_RPAREN);
}

static void parse_col_constraints(P *p, ColDef *c)
{
    for (;;) {
        char *cname = NULL;
        if (accept_kw(p, "constraint")) cname = ident(p);
#if PGVC_VN >= 180
        if (at(p, "not") && is_kw(p, 1, "enforced"))
            ereport_pos(p->s, "42601", CUR(p).pos, "misplaced NOT ENFORCED clause");
        if (at(p, "enforced"))
            ereport_pos(p->s, "42601", CUR(p).pos, "misplaced ENFORCED clause");
#endif
        if (accept_kw(p, "not")) { expect_kw(p, "null"); c->notnull = true; }
        else if (accept_kw(p, "null")) { c->isnull_spec = true; }
        else if (at(p, "primary") && (c->cons_pos = CUR(p).pos, accept_kw(p, "primary")))
            { expect_kw(p, "key"); c->primary = true; c->notnull = true; }
        else if (at(p, "unique") && (c->cons_pos = CUR(p).pos, accept_kw(p, "unique"))) {
            c->unique = true;
            if (accept_kw(p, "nulls")) {
                c->nulls_nd = accept_kw(p, "not");
                expect_kw(p, "distinct");
            }
        }
        else if (accept_kw(p, "check")) {
            expect_kind(p, T_LPAREN);
            int st = p->i;
            parse_expr(p);
            c->checksrc = expr_src(p, st, p->i);
            expect_kind(p, T_RPAREN);
            if (accept_kw(p, "no")) { expect_kw(p, "inherit"); }
        }
#if PGVC_VN >= 140
        else if (at(p, "compression")) { p->i++; c->compression = ident_any(p); }
#endif
        else if (at(p, "default") && (c->default_pos = CUR(p).pos, accept_kw(p, "default")))
            c->defexpr = parse_expr_src(p, NULL);
        else if (at(p, "generated") && (c->identity_pos = CUR(p).pos, accept_kw(p, "generated"))) {
            if (accept_kw(p, "always")) {
                if (accept_kw(p, "as")) {
                    if (accept_kw(p, "identity")) {
                        c->identity = 'a';
                        parse_identity_seqopts(p, c);
                    } else {
                        expect_kind(p, T_LPAREN);
                        int st = p->i;
                        c->genexpr_pos = CUR(p).pos;
                        c->genexpr_e = parse_expr(p);
                        c->genexpr = expr_src(p, st, p->i);
                        expect_kind(p, T_RPAREN);
                        if (accept_kw(p, "stored")) c->generated = 's';
                        else if (at(p, "virtual")) { c->virtual_pos = CUR(p).pos; p->i++;
                                                     c->generated = 'v'; }
                        else c->generated = 'd';    /* unspecified: version-dependent */
                        c->generated_pos = CUR(p).pos;
                    }
                } else syntax_error(p);
            } else {
                expect_kw(p, "by"); expect_kw(p, "default"); expect_kw(p, "as"); expect_kw(p, "identity");
                c->identity = 'd';
                parse_identity_seqopts(p, c);
            }
        }
        else if (at(p, "references") && (c->cons_pos = CUR(p).pos, accept_kw(p, "references"))) {
            char *sch=NULL,*nm=NULL; qname(p,&sch,&nm); free(sch);
            c->fk_table = nm;
            if (at_kind(p, T_LPAREN)) {
                p->i++;
                do spush(&c->fk_cols, &c->nfk_cols, ident_any(p)); while (accept_kind(p, T_COMMA));
                expect_kind(p, T_RPAREN);
            }
            if (accept_kw(p, "match")) { accept_kw(p,"full"); accept_kw(p,"partial"); accept_kw(p,"simple"); }
            for (;;) {
                if (accept_kw(p, "on")) {
                    bool del = accept_kw(p, "delete"); if (!del) expect_kw(p, "update");
                    char act = 'a';
                    if (accept_kw(p, "cascade")) act = 'c';
                    else if (accept_kw(p, "restrict")) act = 'r';
                    else if (accept_kw(p, "set")) { if (accept_kw(p,"null")) act='n'; else { expect_kw(p,"default"); act='d'; } }
                    else { expect_kw(p, "no"); expect_kw(p, "action"); act = 'a'; }
                    if (del) c->fk_del = act; else c->fk_upd = act;
                    continue;
                }
                break;
            }
        }
        else if (accept_kw(p, "collate")) { char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); c->collation = b; }
        else if (accept_kw(p, "deferrable")) { accept_kw(p,"initially"); accept_kw(p,"deferred"); accept_kw(p,"immediate"); }
        else if (accept_kw(p, "initially")) { accept_kw(p,"deferred"); accept_kw(p,"immediate"); }
        else if (accept_kw(p, "not")) { expect_kw(p, "deferrable"); }
        else { free(cname); break; }
        free(cname);
    }
}

static TblCons *parse_table_constraint(P *p)
{
    TblCons *c = xmalloc(sizeof *c);
    memset(c, 0, sizeof *c);
    c->pos = CUR(p).pos;
    if (accept_kw(p, "constraint")) c->name = ident(p);
    if (accept_kw(p, "primary")) { expect_kw(p, "key"); c->contype = 'p'; }
    else if (accept_kw(p, "unique")) { c->contype = 'u';
        if (accept_kw(p, "nulls")) {
            c->nulls_nd = accept_kw(p, "not");
            expect_kw(p, "distinct");
        } }
    else if (accept_kw(p, "check")) {
        c->contype = 'c';
        expect_kind(p, T_LPAREN);
        int st = p->i;
        parse_expr(p);
        c->checksrc = expr_src(p, st, p->i);
        expect_kind(p, T_RPAREN);
        if (accept_kw(p, "no")) expect_kw(p, "inherit");
#if PGVC_VN >= 180
        if (accept_kw(p, "not")) {
            if (accept_kw(p, "enforced")) c->not_enforced = true;
            else { expect_kw(p, "valid"); c->not_valid = true; }
        } else if (accept_kw(p, "enforced")) { }
#endif
        return c;
    }
#if PGVC_VN >= 180
    else if (at(p, "not") && is_kw(p, 1, "null")) {
        /* PostgreSQL 18 spells a named not-null constraint out in full */
        p->i += 2;
        c->contype = 'n';
        spush(&c->cols, &c->ncols, ident_any(p));
        if (accept_kw(p, "no")) { expect_kw(p, "inherit"); c->noinherit = true; }
        if (accept_kw(p, "not")) { expect_kw(p, "valid"); c->not_valid = true; }
        return c;
    }
#endif
    else if (accept_kw(p, "foreign")) { expect_kw(p, "key"); c->contype = 'f'; }
    else if (accept_kw(p, "exclude")) {
        c->contype = 'x';
        if (accept_kw(p, "using")) free(ident_any(p));
        expect_kind(p, T_LPAREN);
        int d = 1; p->i++;
        while (d) { if (at_kind(p,T_LPAREN)) d++; else if (at_kind(p,T_RPAREN)) d--; if (d) p->i++; }
        expect_kind(p, T_RPAREN);
        return c;
    }
    else syntax_error(p);

    expect_kind(p, T_LPAREN);
    do {
        int cpos = CUR(p).pos;
#if PGVC_VN >= 180
        if (c->contype == 'f' && at(p, "period") && PEEK(p,1).kind != T_COMMA &&
            PEEK(p,1).kind != T_RPAREN) { p->i++; c->fk_period = true; cpos = CUR(p).pos; }
#endif
        char *cn = ident_any(p);
        if (accept_kw(p, "without")) {
            expect_kw(p, "overlaps"); c->without_overlaps = true;
            /* WITHOUT OVERLAPS may only mark the last key column */
            if (!at_kind(p, T_RPAREN)) syntax_error(p);
        }
        spush(&c->cols, &c->ncols, cn);
        c->colpos = xrealloc(c->colpos, (size_t)c->ncols * sizeof(int));
        c->colpos[c->ncols-1] = cpos;
    } while (accept_kind(p, T_COMMA));
    expect_kind(p, T_RPAREN);
    if (c->contype != 'f' && accept_kw(p, "include")) {
        expect_kind(p, T_LPAREN);
        do spush(&c->incl, &c->nincl, ident_any(p)); while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
    }
    if (c->contype != 'f' && at(p, "with") && PEEK(p,1).kind == T_LPAREN) {
        /* index storage parameters */
        p->i += 2;
        do {
            char *k = lower_ident(ident_any(p));
            char *v = NULL;
            if (accept_op(p, "=")) { v = xstrdup(CUR(p).sval ? CUR(p).sval : ""); p->i++; }
            if (!strcmp(k, "fillfactor") && v) {
                int fv = atoi(v);
                if (fv < 10 || fv > 100) {
                    err_detail(p->s, "Valid values are between \"10\" and \"100\".");
                    ereport(p->s, "22023", "value %s out of bounds for option \"fillfactor\"", v);
                }
            }
            free(k); free(v);
        } while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
    }
    if (c->contype == 'f') {
        expect_kw(p, "references");
        char *sch=NULL,*nm=NULL; qname(p,&sch,&nm); free(sch);
        c->fk_table = nm;
        if (at_kind(p, T_LPAREN)) {
            p->i++;
            do {
                if (at(p, "period") && PEEK(p,1).kind != T_COMMA &&
                    PEEK(p,1).kind != T_RPAREN) { p->i++; c->fk_period = true; }
                char *fc = ident_any(p);
                spush(&c->fk_cols, &c->nfk_cols, fc);
            } while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
        if (accept_kw(p, "match")) { accept_kw(p,"full"); accept_kw(p,"partial"); accept_kw(p,"simple"); }
        for (;;) {
            if (accept_kw(p, "on")) {
                bool del = accept_kw(p, "delete"); if (!del) expect_kw(p, "update");
                char act = 'a';
                if (accept_kw(p, "cascade")) act = 'c';
                else if (accept_kw(p, "restrict")) act = 'r';
                else if (accept_kw(p, "set")) { if (accept_kw(p,"null")) act='n'; else { expect_kw(p,"default"); act='d'; }
                    if (at_kind(p, T_LPAREN)) { p->i++; while (!at_kind(p,T_RPAREN)) p->i++; p->i++; } }
                else { expect_kw(p, "no"); expect_kw(p, "action"); }
                if (del) c->fk_del = act; else c->fk_upd = act;
                continue;
            }
            break;
        }
    }
#if PGVC_VN >= 180
    if (accept_kw(p, "not")) {
        if (accept_kw(p, "enforced")) c->not_enforced = true;
        else { expect_kw(p, "valid"); c->not_valid = true; }
    } else accept_kw(p, "enforced");
#endif
    accept_kw(p, "deferrable"); accept_kw(p, "initially");
    accept_kw(p, "deferred"); accept_kw(p, "immediate");
    return c;
}

static void parse_partbound(P *p, CreateTableStmt *st)
{
    int start = p->i;
    if (at(p, "default")) {
        st->bound_pos = CUR(p).pos;
        p->i++;
        st->bound_default = true;
        st->bound_text = expr_src(p, start, p->i);
        return;
    }
    expect_kw(p, "for"); expect_kw(p, "values");
    st->bound_pos = CUR(p).pos;
    if (accept_kw(p, "from")) {
        expect_kind(p, T_LPAREN);
        Expr **row = NULL; int n = 0;
        do {
            if (accept_kw(p, "minvalue")) { Expr *e = newx(X_CONST, CUR(p).pos); e->alias = xstrdup("MINVALUE"); xpush(&row,&n,e); }
            else if (accept_kw(p, "maxvalue")) { Expr *e = newx(X_CONST, CUR(p).pos); e->alias = xstrdup("MAXVALUE"); xpush(&row,&n,e); }
            else xpush(&row, &n, parse_expr(p));
        } while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
        st->bound_from = xmalloc(sizeof(Expr **)); st->bound_from[0] = row; st->nbound_from = n;
        expect_kw(p, "to");
        expect_kind(p, T_LPAREN);
        Expr **row2 = NULL; int n2 = 0;
        do {
            if (accept_kw(p, "minvalue")) { Expr *e = newx(X_CONST, CUR(p).pos); e->alias = xstrdup("MINVALUE"); xpush(&row2,&n2,e); }
            else if (accept_kw(p, "maxvalue")) { Expr *e = newx(X_CONST, CUR(p).pos); e->alias = xstrdup("MAXVALUE"); xpush(&row2,&n2,e); }
            else xpush(&row2, &n2, parse_expr(p));
        } while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
        st->bound_to = xmalloc(sizeof(Expr **)); st->bound_to[0] = row2; st->nbound_to = n2;
    } else if (accept_kw(p, "in")) {
        expect_kind(p, T_LPAREN);
        do xpush(&st->bound_in, &st->nbound_in, parse_expr(p)); while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
    } else if (accept_kw(p, "with")) {
        expect_kind(p, T_LPAREN);
        do {
            char *k = ident_any(p);
            if (CUR(p).kind == T_NUMBER) {
                int v = atoi(CUR(p).sval); p->i++;
                if (!strcmp(k, "modulus")) { st->bound_modulus = v; st->has_modulus = true; } else st->bound_remainder = v;
            } else syntax_error(p);
            free(k);
        } while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
    } else syntax_error(p);
    st->bound_text = expr_src(p, start, p->i);
}

static Node *parse_create_table(P *p, bool temp, bool unlogged)
{
    expect_kw(p, "table");
    CreateTableStmt *st = xmalloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->temporary = temp; st->unlogged = unlogged;
    if (at(p,"if") && is_kw(p,1,"not") && is_kw(p,2,"exists")) { p->i += 3; st->if_not_exists = true; }
    st->name_pos = CUR(p).pos;
    qname(p, &st->schema, &st->name);
    if (accept_kw(p, "partition")) {
        expect_kw(p, "of");
        qname(p, &st->parent_schema, &st->parent_name);
        if (at_kind(p, T_LPAREN)) {
            p->i++;
            do {
                if (at(p,"constraint")||at(p,"primary")||at(p,"unique")||at(p,"check")||
                    at(p,"foreign")||at(p,"exclude")) {
                    TblCons *c = parse_table_constraint(p);
                    st->cons = xrealloc(st->cons, (size_t)(st->ncons+1)*sizeof(TblCons*));
                    st->cons[st->ncons++] = c;
                } else {
                    ColDef *c = xmalloc(sizeof *c); memset(c, 0, sizeof *c);
                    c->name = ident_any(p);
                    if (accept_kw(p, "with")) { expect_kw(p, "options"); }
                    parse_col_constraints(p, c);
                    st->cols = xrealloc(st->cols, (size_t)(st->ncols+1)*sizeof(ColDef*));
                    st->cols[st->ncols++] = c;
                }
            } while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
        parse_partbound(p, st);
    } else if (at_kind(p, T_LPAREN)) {
        p->i++;
        if (!at_kind(p, T_RPAREN)) {
            do {
                if (at(p,"constraint")||at(p,"primary")||at(p,"unique")||at(p,"check")||
                    at(p,"foreign")||at(p,"exclude")) {
                    TblCons *c = parse_table_constraint(p);
                    st->cons = xrealloc(st->cons, (size_t)(st->ncons+1)*sizeof(TblCons*));
                    st->cons[st->ncons++] = c;
                } else if (accept_kw(p, "like")) {
                    char *a=NULL,*b=NULL; qname(p,&a,&b); free(a);
                    st->like_table = b;
                    while (at(p,"including")||at(p,"excluding")) { p->i++; free(ident_any(p)); }
                } else {
                    ColDef *c = xmalloc(sizeof *c); memset(c, 0, sizeof *c);
                    c->name = ident_any(p);
                    c->typepos = CUR(p).pos;
                    c->typename = type_name_text(p, &c->type, &c->typmod);
                    if (accept_kw(p, "collate")) { char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); c->collation = b; }
                    parse_col_constraints(p, c);
                    st->cols = xrealloc(st->cols, (size_t)(st->ncols+1)*sizeof(ColDef*));
                    st->cols[st->ncols++] = c;
                }
            } while (accept_kind(p, T_COMMA));
        }
        expect_kind(p, T_RPAREN);
    }
    if (accept_kw(p, "inherits")) {
        expect_kind(p, T_LPAREN);
        do { char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); spush(&st->inherits,&st->ninherits,b); }
        while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
    }
    if (accept_kw(p, "partition")) {
        expect_kw(p, "by");
        char *k = ident_any(p);
        st->partstrat = (!strcmp(k,"range")) ? 'r' : (!strcmp(k,"list")) ? 'l' :
                        (!strcmp(k,"hash")) ? 'h' : 0;
        if (!st->partstrat) ereport(p->s, "42601", "unrecognized partitioning strategy \"%s\"", k);
        free(k);
        expect_kind(p, T_LPAREN);
        do {
            char *nm = NULL, *ex = NULL;
            if (CUR(p).kind == T_LPAREN) {
                int st0 = p->i;
                p->i++;
                parse_expr(p);
                expect_kind(p, T_RPAREN);
                ex = expr_src(p, st0, p->i);
            } else nm = ident_any(p);
            st->partkey = xrealloc(st->partkey, (size_t)(st->npartkey+1) * sizeof(char *));
            st->partkeyexpr = xrealloc(st->partkeyexpr, (size_t)(st->npartkey+1) * sizeof(char *));
            st->partkey[st->npartkey] = nm;
            st->partkeyexpr[st->npartkey] = ex;
            st->npartkey++;
        } while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
    }
    if (accept_kw(p, "using")) { st->using_pos = CUR(p).pos; st->using_am = ident_any(p); }
    if (accept_kw(p, "with")) {
        if (at(p, "oids")) { st->oids_pos = CUR(p).pos; p->i++; st->with_oids = true; }
        else {
            expect_kind(p, T_LPAREN);
            do {
                int kp = CUR(p).pos;
                char *k = ident_any(p);
                for (char *q = k; *q; q++) *q = (char)tolower((unsigned char)*q);
                char *v = NULL;
                if (accept_op(p, "=")) {
                    if (CUR(p).kind == T_IDENT) v = ident_any(p);
                    else { v = xstrndup(CUR(p).kind == T_NUMBER ? CUR(p).start : CUR(p).sval,
                                        CUR(p).kind == T_NUMBER
                                            ? (size_t)(CUR(p).end - CUR(p).start)
                                            : strlen(CUR(p).sval)); p->i++; }
                }
                if (!strcmp(k, "oids")) { st->with_oids = true; st->oids_pos = kp; }
                st->reloptvals = xrealloc(st->reloptvals,
                                          (size_t)(st->nreloptions+1)*sizeof(char*));
                st->reloptvals[st->nreloptions] = v ? v : xstrdup("true");
                spush(&st->reloptions, &st->nreloptions, k);
            } while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
    }
    if (accept_kw(p, "without")) { expect_kw(p, "oids"); }
    if (accept_kw(p, "using")) { st->using_pos = CUR(p).pos; st->using_am = ident_any(p); }
    if (accept_kw(p, "on")) { expect_kw(p, "commit"); accept_kw(p,"preserve"); accept_kw(p,"delete");
                              accept_kw(p,"drop"); accept_kw(p,"rows"); }
    if (accept_kw(p, "tablespace")) free(ident_any(p));
    if (accept_kw(p, "as")) {
        st->as_query = parse_select_stmt(p, NULL, 0);
        if (accept_kw(p, "with")) { accept_kw(p, "no"); expect_kw(p, "data"); }
    }
    return mknode(N_CREATE_TABLE, st);
}

static Node *parse_create_index(P *p, bool unique)
{
    expect_kw(p, "index");
    CreateIndexStmt *st = xmalloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->unique = unique;
    st->concurrent = accept_kw(p, "concurrently");
    if (at(p,"if") && is_kw(p,1,"not") && is_kw(p,2,"exists")) { p->i += 3; st->if_not_exists = true; }
    if (!at(p, "on")) st->name = ident(p);
    expect_kw(p, "on");
    st->only = accept_kw(p, "only");
    qname(p, &st->schema, &st->table);
    if (accept_kw(p, "using")) st->method = ident_any(p);
    expect_kind(p, T_LPAREN);
    do {
        int st0 = p->i;
        Expr *e = parse_expr(p);
        char *src = expr_src(p, st0, p->i);
        if (accept_kw(p, "collate")) { char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); free(b); }
        if (at_ident(p) && !at(p,"asc") && !at(p,"desc") && !at(p,"nulls")) free(ident_any(p)); /* opclass */
        int dir = 0, nulls = 0;
        if (accept_kw(p, "asc")) dir = 0;
        else if (accept_kw(p, "desc")) { dir = 1; nulls = 1; }
        if (accept_kw(p, "nulls")) { nulls = accept_kw(p, "first") ? 1 : (expect_kw(p,"last"), 2); }
        st->cols = xrealloc(st->cols, (size_t)(st->ncols+1)*sizeof(char*));
        st->colexprs = xrealloc(st->colexprs, (size_t)(st->ncols+1)*sizeof(Expr*));
        st->dir = xrealloc(st->dir, (size_t)(st->ncols+1)*sizeof(int));
        st->nulls = xrealloc(st->nulls, (size_t)(st->ncols+1)*sizeof(int));
        st->cols[st->ncols] = (e->tag == X_COLREF) ? xstrdup(e->colname) : src;
        st->colexprs[st->ncols] = (e->tag == X_COLREF) ? NULL : e;
        st->dir[st->ncols] = dir; st->nulls[st->ncols] = nulls;
        st->ncols++;
    } while (accept_kind(p, T_COMMA));
    expect_kind(p, T_RPAREN);
    if (accept_kw(p, "include")) {
        expect_kind(p, T_LPAREN);
        do {
            if (at_kind(p, T_LPAREN)) {        /* an expression, rejected below */
                int s0 = p->i;
                p->i++;
                parse_expr(p);
                expect_kind(p, T_RPAREN);
                spush(&st->incl, &st->nincl, expr_src(p, s0, p->i));
            } else spush(&st->incl, &st->nincl, ident_any(p));
        } while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
    }
    if (accept_kw(p, "nulls")) {
        st->nulls_nd = accept_kw(p, "not");
        expect_kw(p, "distinct");
    }
    if (accept_kw(p, "with")) {
        expect_kind(p, T_LPAREN);
        do {
            int kp = CUR(p).pos;
            char *k = ident_any(p);
            for (char *q = k; *q; q++) *q = (char)tolower((unsigned char)*q);
            char *v = NULL;
            if (accept_op(p, "=")) {
                if (CUR(p).kind == T_IDENT) v = ident_any(p);
                else if (CUR(p).kind == T_NUMBER || CUR(p).kind == T_STRING) {
                    v = xstrndup(CUR(p).kind == T_NUMBER ? CUR(p).start : CUR(p).sval,
                                 CUR(p).kind == T_NUMBER
                                     ? (size_t)(CUR(p).end - CUR(p).start)
                                     : strlen(CUR(p).sval));
                    p->i++;
                } else syntax_error(p);
            }
            st->optname = xrealloc(st->optname, (size_t)(st->nopts+1)*sizeof(char*));
            st->optval  = xrealloc(st->optval,  (size_t)(st->nopts+1)*sizeof(char*));
            st->optpos  = xrealloc(st->optpos,  (size_t)(st->nopts+1)*sizeof(int));
            st->optname[st->nopts] = k;
            st->optval[st->nopts] = v ? v : xstrdup("true");
            st->optpos[st->nopts] = kp;
            st->nopts++;
        } while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
    }
    if (accept_kw(p, "tablespace")) free(ident_any(p));
    if (accept_kw(p, "where")) st->predicate = parse_expr_src(p, NULL);
    return mknode(N_CREATE_INDEX, st);
}

/* One `name = value` item of a subscription option list.  CREATE and ALTER
 * accept the same spellings, so both come through here; an option the target
 * release has not grown yet is recorded as unrecognised and reported by
 * check_sub_options() when the statement runs. */
static void sub_option(P *p, CreateSubStmt *st)
{
    char *k = ident_any(p);
    char *v = NULL;
    if (accept_op(p, "=")) {
        if (CUR(p).kind == T_IDENT) v = ident_any(p);
        else { v = xstrdup(CUR(p).sval); p->i++; }
    }
    /* PostgreSQL's defGetBoolean spelling; a bare option name means true */
    int truth = -1;
    if (!v || !strcasecmp(v,"true") || !strcasecmp(v,"on") || !strcmp(v,"1") ||
        !strcasecmp(v,"yes")) truth = 1;
    else if (!strcasecmp(v,"false") || !strcasecmp(v,"off") || !strcmp(v,"0") ||
             !strcasecmp(v,"no")) truth = 0;
    st->nopts++;
    /* An option a release has not grown yet is not an option: PostgreSQL
     * rejects it by name.  binary and streaming arrived in 14, two_phase and
     * disable_on_error in 15, password_required, run_as_owner and origin in
     * 16, failover in 17; the rest have been there since subscriptions were. */
    bool known = true, wantbool = false, *flag = NULL, *seen = NULL;
    if (!strcmp(k, "connect"))        { wantbool = true; flag = &st->connect; seen = &st->has_connect; }
    else if (!strcmp(k, "enabled"))   { wantbool = true; flag = &st->enabled; seen = &st->has_enabled; }
    else if (PGVC_VN >= 140 && !strcmp(k, "binary"))
                                      { wantbool = true; flag = &st->binary;  seen = &st->has_binary; }
    else if (PGVC_VN >= 170 && !strcmp(k, "failover"))
                                      { wantbool = true; flag = &st->failover; seen = &st->has_failover; }
    else if (PGVC_VN >= 150 && !strcmp(k, "disable_on_error"))
                                      { wantbool = true; flag = &st->disableonerr; }
    else if (!strcmp(k, "copy_data") || !strcmp(k, "create_slot") ||
             (PGVC_VN >= 160 && (!strcmp(k, "password_required") ||
                                 !strcmp(k, "run_as_owner"))))
        wantbool = true;                       /* accepted, nothing to record */
    else if (PGVC_VN >= 160 && !strcmp(k, "origin")) st->origin = xstrdup(v ? v : "");
    else if (!strcmp(k, "slot_name")) {
        /* slot_name = NONE leaves the subscription without a slot */
        st->has_slotname = true;
        st->slotname = (v && strcasecmp(v, "none")) ? xstrdup(v) : NULL;
    }
    else if ((PGVC_VN >= 140 && !strcmp(k, "streaming")) ||
             (PGVC_VN >= 150 && !strcmp(k, "two_phase")) ||
             !strcmp(k, "synchronous_commit")) { }
    else known = false;
    if (wantbool) {
        if (truth < 0) { if (!st->bad_bool) st->bad_bool = xstrdup(k); }
        else { if (flag) *flag = truth != 0; if (seen) *seen = true; }
    }
    if (!known && !st->bad_param) st->bad_param = xstrdup(k);
    free(k); free(v);
}

static Node *parse_create(P *p)
{
    expect_kw(p, "create");
    bool replace = false, temp = false, unlogged = false;
    if (accept_kw(p, "or")) { expect_kw(p, "replace"); replace = true; }
    if (accept_kw(p, "temporary") || accept_kw(p, "temp")) temp = true;
    if (accept_kw(p, "unlogged")) unlogged = true;
    if (accept_kw(p, "global") || accept_kw(p, "local")) { accept_kw(p,"temporary"); accept_kw(p,"temp"); temp = true; }

    if (at(p, "table")) return parse_create_table(p, temp, unlogged);
    if (at(p, "index")) return parse_create_index(p, false);
    if (at(p, "unique")) { p->i++; return parse_create_index(p, true); }
    if (at(p, "view") || (at(p, "materialized") && is_kw(p,1,"view")) ||
        (at(p, "recursive") && is_kw(p,1,"view"))) {
        CreateViewStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        st->replace = replace;
        st->materialized = accept_kw(p, "materialized");
        st->recursive = accept_kw(p, "recursive");
        expect_kw(p, "view");
        qname(p, &st->schema, &st->name);
        if (at_kind(p, T_LPAREN)) {
            p->i++;
            do spush(&st->cols, &st->ncols, ident_any(p)); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
        if (accept_kw(p, "with")) {
            expect_kind(p, T_LPAREN);
            do {
                char *k = ident_any(p);
                for (char *q = k; *q; q++) *q = (char)tolower((unsigned char)*q);
                char *v = NULL;
                if (accept_op(p, "=")) { if (CUR(p).kind == T_IDENT) v = ident_any(p);
                                         else { v = xstrdup(CUR(p).sval); p->i++; } }
                if (!strcmp(k, "security_invoker")) st->security_invoker = !v || !strcmp(v, "true") || !strcmp(v, "on");
                if (!strcmp(k, "security_barrier")) st->security_barrier = !v || !strcmp(v, "true");
                st->optname = xrealloc(st->optname, (size_t)(st->nopts+1)*sizeof(char*));
                st->optval  = xrealloc(st->optval,  (size_t)(st->nopts+1)*sizeof(char*));
                st->optname[st->nopts] = k;
                st->optval[st->nopts] = v ? v : xstrdup("true");
                st->nopts++;
            } while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
        expect_kw(p, "as");
        int st0 = p->i;
        st->query = parse_select_stmt(p, NULL, 0);
        st->querysrc = expr_src(p, st0, p->i);
        if (accept_kw(p, "with")) {
            if (accept_kw(p, "local")) st->check_option = 1;
            else { accept_kw(p, "cascaded"); st->check_option = 2; }
            expect_kw(p, "check"); expect_kw(p, "option");
        }
        return mknode(N_CREATE_VIEW, st);
    }
    if (accept_kw(p, "schema")) {
        CreateTableStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        if (at(p,"if") && is_kw(p,1,"not") && is_kw(p,2,"exists")) { p->i += 3; st->if_not_exists = true; }
        if (!at(p, "authorization")) st->name = ident(p);
        if (accept_kw(p, "authorization")) { char *o = ident_any(p); if (!st->name) st->name = xstrdup(o); free(o); }
        return mknode(N_CREATE_SCHEMA, st);
    }
    if (accept_kw(p, "sequence")) {
        CreateSeqStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        st->inc = 1; st->cache = 1; st->temp = temp;
        if (at(p,"if") && is_kw(p,1,"not") && is_kw(p,2,"exists")) { p->i += 3; st->if_not_exists = true; }
        qname(p, &st->schema, &st->name);
        for (;;) {
            if (accept_kw(p, "as")) { Oid t; int32 tm; st->astype = type_name_text(p, &t, &tm); }
            else if (accept_kw(p, "increment")) { accept_kw(p,"by"); st->inc = strtoll(CUR(p).sval,NULL,10);
                if (at_op(p,"-")) { p->i++; st->inc = -strtoll(CUR(p).sval,NULL,10); } p->i++; }
            else if (accept_kw(p, "start")) { accept_kw(p,"with");
                bool neg = accept_op(p,"-"); st->start = strtoll(CUR(p).sval,NULL,10); if (neg) st->start = -st->start;
                p->i++; st->has_start = true; }
            else if (accept_kw(p, "minvalue")) { bool neg = accept_op(p,"-"); st->minv = strtoll(CUR(p).sval,NULL,10);
                if (neg) st->minv = -st->minv; p->i++; st->has_min = true; }
            else if (accept_kw(p, "maxvalue")) { bool neg = accept_op(p,"-"); st->maxv = strtoll(CUR(p).sval,NULL,10);
                if (neg) st->maxv = -st->maxv; p->i++; st->has_max = true; }
            else if (at(p,"no") && (is_kw(p,1,"minvalue")||is_kw(p,1,"maxvalue")||is_kw(p,1,"cycle"))) p->i += 2;
            else if (accept_kw(p, "cache")) { st->cache = strtoll(CUR(p).sval,NULL,10); p->i++; }
            else if (accept_kw(p, "cycle")) st->cycle = true;
            else if (accept_kw(p, "owned")) { expect_kw(p,"by"); char *a=NULL,*b=NULL;
                if (at(p,"none")) { p->i++; } else { qname(p,&a,&b); if (at_kind(p,T_DOT)) { p->i++; free(ident_any(p)); } }
                free(a); free(b); }
            else break;
        }
        return mknode(N_CREATE_SEQ, st);
    }
    if (at(p, "role") || at(p, "user") || at(p, "group")) {
        bool isuser = at(p, "user");
        p->i++;
        CreateRoleStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        st->inherit = true; st->is_user = isuser; st->login = isuser;
        st->name = ident_any(p);
        accept_kw(p, "with");
        for (;;) {
            if (accept_kw(p, "login")) st->login = true;
            else if (accept_kw(p, "nologin")) st->login = false;
            else if (accept_kw(p, "superuser")) st->superuser = true;
            else if (accept_kw(p, "nosuperuser")) st->superuser = false;
            else if (accept_kw(p, "createdb")) st->createdb = true;
            else if (accept_kw(p, "nocreatedb")) st->createdb = false;
            else if (accept_kw(p, "createrole")) st->createrole = true;
            else if (accept_kw(p, "nocreaterole")) st->createrole = false;
            else if (accept_kw(p, "replication")) st->replication = true;
            else if (accept_kw(p, "noreplication")) st->replication = false;
            else if (accept_kw(p, "inherit")) st->inherit = true;
            else if (accept_kw(p, "noinherit")) st->inherit = false;
            else if (accept_kw(p, "bypassrls") || accept_kw(p, "nobypassrls")) { }
            else if (accept_kw(p, "encrypted") || accept_kw(p, "unencrypted")) { }
            else if (accept_kw(p, "password")) {
                if (accept_kw(p, "null")) { st->has_password = true; st->password = NULL; }
                else { st->password = xstrdup(CUR(p).sval); st->has_password = true;
                       if (CUR(p).kind != T_STRING) syntax_error(p); p->i++; }
            }
            else if (accept_kw(p, "connection")) { expect_kw(p, "limit"); p->i++; }
            else if (accept_kw(p, "valid")) { expect_kw(p, "until"); p->i++; }
            else if (accept_kw(p, "in")) { expect_kw(p, "role"); do free(ident_any(p)); while (accept_kind(p, T_COMMA)); }
            else if (accept_kw(p, "role") || accept_kw(p, "admin")) { do free(ident_any(p)); while (accept_kind(p, T_COMMA)); }
            else if (accept_kw(p, "sysid")) p->i++;
            else break;
        }
        return mknode(N_CREATE_ROLE, st);
    }
    if (accept_kw(p, "publication")) {
        CreatePubStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        st->insert_ = st->update_ = st->delete_ = st->truncate_ = true;
        st->name = ident(p);
        if (accept_kw(p, "for")) {
            if (accept_kw(p, "all")) {
                expect_kw(p, "tables"); st->alltables = true;
                if (at(p, "where")) syntax_error(p);
            }
            else {
                do {
                    if (accept_kw(p, "tables")) { expect_kw(p, "in"); expect_kw(p, "schema");
                        do free(ident_any(p)); while (accept_kind(p, T_COMMA)); continue; }
                    accept_kw(p, "table");
                    char *sch=NULL,*nm=NULL; qname(p,&sch,&nm);
                    char **pair = xmalloc(2*sizeof(char*)); pair[0]=sch; pair[1]=nm;
                    st->tables = xrealloc(st->tables, (size_t)(st->ntables+1)*sizeof(char**));
                    st->where = xrealloc(st->where, (size_t)(st->ntables+1)*sizeof(char*));
                    st->collist = xrealloc(st->collist, (size_t)(st->ntables+1)*sizeof(char**));
                    st->ncollist = xrealloc(st->ncollist, (size_t)(st->ntables+1)*sizeof(int));
                    st->tables[st->ntables] = pair;
                    st->where[st->ntables] = NULL;
                    st->collist[st->ntables] = NULL;
                    st->ncollist[st->ntables] = 0;
                    if (at_kind(p, T_LPAREN)) {
                        p->i++;
                        char **cl = NULL; int ncl = 0;
                        do spush(&cl, &ncl, ident_any(p)); while (accept_kind(p, T_COMMA));
                        expect_kind(p, T_RPAREN);
                        st->collist[st->ntables] = cl; st->ncollist[st->ntables] = ncl;
                    }
                    if (accept_kw(p, "where")) {
                        expect_kind(p, T_LPAREN);
                        int s0 = p->i;
                        st->wherepos = xrealloc(st->wherepos,
                                                (size_t)(st->ntables+1)*sizeof(int));
                        st->wherepos[st->ntables] = CUR(p).pos;
                        parse_expr(p);
                        st->where[st->ntables] = expr_src(p, s0, p->i);
                        expect_kind(p, T_RPAREN);
                    } else {
                        st->wherepos = xrealloc(st->wherepos,
                                                (size_t)(st->ntables+1)*sizeof(int));
                        st->wherepos[st->ntables] = 0;
                    }
                    st->ntables++;
                } while (accept_kind(p, T_COMMA));
            }
        }
        if (accept_kw(p, "with")) {
            expect_kind(p, T_LPAREN);
            st->has_publish = true;
            do {
                char *k = ident_any(p);
                char *v = NULL;
                if (accept_op(p, "=")) { if (CUR(p).kind == T_IDENT) v = ident_any(p);
                                         else { v = xstrdup(CUR(p).sval); p->i++; } }
                if (!strcmp(k, "publish")) {
                    st->has_publish_opt = true;
                    st->insert_ = st->update_ = st->delete_ = st->truncate_ = false;
                    if (v) for (char *tok = strtok(v, ", "); tok; tok = strtok(NULL, ", ")) {
                        if (!strcmp(tok,"insert")) st->insert_ = true;
                        else if (!strcmp(tok,"update")) st->update_ = true;
                        else if (!strcmp(tok,"delete")) st->delete_ = true;
                        else if (!strcmp(tok,"truncate")) st->truncate_ = true;
                        else if (!st->bad_publish) st->bad_publish = xstrdup(tok);
                    }
                } else if (!strcmp(k, "publish_via_partition_root")) {
                    st->has_viaroot = true;
                    if (!v || !strcasecmp(v,"true") || !strcasecmp(v,"on") ||
                        !strcmp(v,"1") || !strcasecmp(v,"yes")) st->viaroot = true;
                    else if (!strcasecmp(v,"false") || !strcasecmp(v,"off") ||
                             !strcmp(v,"0") || !strcasecmp(v,"no")) st->viaroot = false;
                    else if (!st->bad_bool) st->bad_bool = xstrdup(k);
                }
                else if (!st->bad_param) st->bad_param = xstrdup(k);
                free(k); free(v);
            } while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
        return mknode(N_CREATE_PUB, st);
    }
    if (at(p, "function") || at(p, "procedure")) {
        bool proc = at(p, "procedure");
        p->i++;
        CreateFuncStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        st->replace = replace; st->prokind = proc ? 'p' : 'f'; st->volatility = 'v';
        qname(p, &st->schema, &st->name);
        expect_kind(p, T_LPAREN);
        if (!at_kind(p, T_RPAREN)) {
            do {
                char mode = 'i';
                if (at(p,"in") && !is_kw(p,1,"")) { }
                if (accept_kw(p, "in")) mode = 'i';
                else if (accept_kw(p, "out")) mode = 'o';
                else if (accept_kw(p, "inout")) mode = 'b';
                else if (accept_kw(p, "variadic")) mode = 'v';
                char *nm = NULL;
                int save = p->i;
                if (at_ident(p)) {
                    nm = ident_any(p);
                    if (at_kind(p, T_COMMA) || at_kind(p, T_RPAREN)) { p->i = save; free(nm); nm = NULL; }
                    else if (at_kind(p, T_DOT)) { p->i = save; free(nm); nm = NULL; }
                }
                Oid t; int32 tm;
                char *tn = type_name_text(p, &t, &tm);
                st->argmodes = xrealloc(st->argmodes, (size_t)(st->nargs+1));
                st->argmodes[st->nargs] = mode;
                spush(&st->argnames, &st->nargs, nm ? nm : xstrdup(""));
                st->nargs--;
                spush(&st->argtypes, &st->nargs, tn);
                st->argdefaults = xrealloc(st->argdefaults, (size_t)st->nargs * sizeof(char*));
                st->argdefaults[st->nargs-1] = NULL;
                if (accept_kw(p, "default") || accept_op(p, "="))
                    st->argdefaults[st->nargs-1] = parse_expr_src(p, NULL);
            } while (accept_kind(p, T_COMMA));
        }
        expect_kind(p, T_RPAREN);
        if (accept_kw(p, "returns")) {
            if (accept_kw(p, "table")) {
                expect_kind(p, T_LPAREN);
                st->retset = true;
                do { free(ident_any(p)); Oid t; int32 tm; free(type_name_text(p,&t,&tm)); }
                while (accept_kind(p, T_COMMA));
                expect_kind(p, T_RPAREN);
                st->rettype = xstrdup("record");
            } else {
                st->retset = accept_kw(p, "setof");
                Oid t; int32 tm;
                st->rettype = type_name_text(p, &t, &tm);
            }
        }
        for (;;) {
            if (accept_kw(p, "language")) st->lang = at_ident(p) ? ident_any(p) : (char*)({ char *v = xstrdup(CUR(p).sval); p->i++; v; });
            else if (accept_kw(p, "immutable")) st->volatility = 'i';
            else if (accept_kw(p, "stable")) st->volatility = 's';
            else if (accept_kw(p, "volatile")) st->volatility = 'v';
            else if (accept_kw(p, "strict")) st->strict_ = true;
            else if (accept_kw(p, "called")) { expect_kw(p, "on"); expect_kw(p, "null"); expect_kw(p, "input"); }
            else if (accept_kw(p, "returns")) { expect_kw(p, "null"); expect_kw(p, "on"); expect_kw(p, "null"); expect_kw(p, "input"); st->strict_ = true; }
            else if (accept_kw(p, "security")) { st->secdef = ident_any(p); }
            else if (accept_kw(p, "external")) { expect_kw(p, "security"); free(ident_any(p)); }
            else if (accept_kw(p, "cost") || accept_kw(p, "rows")) p->i++;
            else if (accept_kw(p, "parallel")) free(ident_any(p));
            else if (accept_kw(p, "leakproof")) { }
            else if (accept_kw(p, "not")) { expect_kw(p, "leakproof"); }
            else if (accept_kw(p, "set")) { free(ident_any(p)); if (accept_op(p,"=")||accept_kw(p,"to")) p->i++; }
            else if (accept_kw(p, "window")) { }
            else if (accept_kw(p, "as")) {
                if (CUR(p).kind != T_STRING) syntax_error(p);
                st->body = xstrdup(CUR(p).sval);
                { const char *tk = CUR(p).start;
                  int off = 1;
                  if (*tk == '$') { const char *q = tk + 1; while (*q && *q != '$') q++;
                                    off = (int)(q - tk) + 1; }
                  st->body_pos = CUR(p).pos + off; }
                p->i++;
                if (accept_kind(p, T_COMMA)) { p->i++; }
            }
            else if (accept_kw(p, "begin")) {   /* SQL-standard BEGIN ATOMIC */
                expect_kw(p, "atomic");
                int s0 = p->i;
                int depth = 1;
                while (depth > 0 && !at_kind(p, T_EOF)) {
                    if (at(p, "end") && depth == 1) break;
                    if (at(p, "case")) depth++;
                    if (at(p, "end")) depth--;
                    p->i++;
                }
                st->body = expr_src(p, s0, p->i);
                expect_kw(p, "end");
            }
            else break;
        }
        return mknode(N_CREATE_FUNC, st);
    }
    if (accept_kw(p, "database")) {
        CreateDbStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        st->name = ident_any(p);
        accept_kw(p, "with");
        for (;;) {
            if (at_ident(p)) {
                char *k = ident_any(p);
                if (accept_op(p, "=") || true) { if (at_kind(p,T_CAST)) p->i++; }
                if (at_ident(p) || CUR(p).kind == T_STRING || CUR(p).kind == T_NUMBER) {
                    char *v = at_ident(p) ? ident_any(p) : (char*)({ char *q = xstrdup(CUR(p).sval); p->i++; q; });
                    if (!strcmp(k, "template")) st->template_ = xstrdup(v);
                    if (!strcmp(k, "encoding")) st->encoding = xstrdup(v);
                    if (!strcmp(k, "owner")) st->owner = xstrdup(v);
                    free(v);
                }
                free(k);
                continue;
            }
            break;
        }
        return mknode(N_CREATE_DB, st);
    }
    if (accept_kw(p, "subscription")) {
        CreateSubStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        st->enabled = true; st->connect = true;
        st->name = ident(p);
        expect_kw(p, "connection");
        st->conninfo = xstrdup(CUR(p).sval); p->i++;
        expect_kw(p, "publication");
        do spush(&st->pubs, &st->npubs, ident_any(p)); while (accept_kind(p, T_COMMA));
        if (accept_kw(p, "with")) {
            expect_kind(p, T_LPAREN);
            do sub_option(p, st); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
        return mknode(N_CREATE_SUB, st);
    }
    if (accept_kw(p, "domain")) {
        CreateDomainStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        { char *sch = NULL; qname(p, &sch, &st->name); free(sch); }
        accept_kw(p, "as");
        st->typename = type_name_text(p, &st->type, &st->typmod);
        for (;;) {
            if (accept_kw(p, "constraint")) { free(ident(p)); continue; }
            if (accept_kw(p, "check")) {
                expect_kind(p, T_LPAREN);
                int s0 = p->i;
                parse_expr(p);
                st->checksrc = expr_src(p, s0, p->i);
                expect_kind(p, T_RPAREN);
                continue;
            }
            if (at(p, "not") && is_kw(p, 1, "null")) { p->i += 2; st->notnull = true; continue; }
            if (accept_kw(p, "null")) continue;
            if (accept_kw(p, "default")) { free(parse_expr_src(p, NULL)); continue; }
            if (accept_kw(p, "collate")) { char *a=NULL,*b2=NULL; qname(p,&a,&b2); free(a); free(b2); continue; }
            break;
        }
        return mknode(N_CREATE_DOMAIN, st);
    }
    if (accept_kw(p, "type")) {
        CreateFuncStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        qname(p, &st->schema, &st->name);
        if (accept_kw(p, "as")) {
            if (accept_kw(p, "enum")) {
                expect_kind(p, T_LPAREN);
                if (!at_kind(p, T_RPAREN))
                    do { spush(&st->argtypes, &st->nargs, xstrdup(CUR(p).sval)); p->i++; }
                    while (accept_kind(p, T_COMMA));
                expect_kind(p, T_RPAREN);
                st->prokind = 'e';
            } else if (accept_kw(p, "range")) {
                expect_kind(p, T_LPAREN);
                while (!at_kind(p, T_RPAREN)) p->i++;
                expect_kind(p, T_RPAREN);
                st->prokind = 'r';
            } else {
                expect_kind(p, T_LPAREN);
                st->prokind = 'c';
                if (!at_kind(p, T_RPAREN))
                    do { spush(&st->argnames, &st->nargs, ident_any(p)); st->nargs--;
                         Oid t; int32 tm; spush(&st->argtypes, &st->nargs, type_name_text(p,&t,&tm)); }
                    while (accept_kind(p, T_COMMA));
                expect_kind(p, T_RPAREN);
            }
        }
        return mknode(N_CREATE_TYPE, st);
    }
    if (accept_kw(p, "trigger") || (at(p, "constraint") && is_kw(p, 1, "trigger"))) {
        CreateTrigStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        if (accept_kw(p, "constraint")) expect_kw(p, "trigger");
        st->name = ident_any(p);
        if (accept_kw(p, "before")) st->timing = 0;
        else if (accept_kw(p, "after")) st->timing = 1;
        else if (accept_kw(p, "instead")) { expect_kw(p, "of"); st->timing = 2; }
        else syntax_error(p);
        do {
            if (accept_kw(p, "insert")) st->events |= 1;
            else if (accept_kw(p, "update")) { st->events |= 2;
                if (accept_kw(p, "of")) do free(ident_any(p)); while (accept_kind(p, T_COMMA)); }
            else if (accept_kw(p, "delete")) st->events |= 4;
            else if (accept_kw(p, "truncate")) st->events |= 8;
            else syntax_error(p);
        } while (accept_kw(p, "or"));
        expect_kw(p, "on");
        qname(p, &st->schema, &st->table);
        if (accept_kw(p, "from")) { char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); free(b); }
        while (accept_kw(p, "not") || accept_kw(p, "deferrable") ||
               accept_kw(p, "initially")) { accept_kw(p, "deferrable");
                                            accept_kw(p, "immediate"); accept_kw(p, "deferred"); }
        if (accept_kw(p, "referencing")) {
            for (;;) {
                bool isold;
                if (accept_kw(p, "old")) isold = true;
                else if (accept_kw(p, "new")) isold = false;
                else break;
                expect_kw(p, "table");
                accept_kw(p, "as");
                char *nm = ident_any(p);
                if (isold) { st->nold++; free(st->oldtable); st->oldtable = nm; }
                else { st->nnew++; free(st->newtable); st->newtable = nm; }
            }
        }
        if (accept_kw(p, "for")) {
            accept_kw(p, "each");
            if (accept_kw(p, "row")) st->forrow = true;
            else expect_kw(p, "statement");
        }
        if (accept_kw(p, "when")) {
            expect_kind(p, T_LPAREN);
            int s0 = p->i;
            parse_expr(p);
            st->whensrc = expr_src(p, s0, p->i);
            expect_kind(p, T_RPAREN);
        }
        expect_kw(p, "execute");
        if (!accept_kw(p, "function")) expect_kw(p, "procedure");
        { char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); st->funcname = b; }
        expect_kind(p, T_LPAREN);
        while (!at_kind(p, T_RPAREN) && !at_kind(p, T_EOF)) p->i++;
        expect_kind(p, T_RPAREN);
        return mknode(N_CREATE_TRIGGER, st);
    }
    if (accept_kw(p, "statistics")) {
        /* CREATE STATISTICS [IF NOT EXISTS] name [ (kinds) ] ON cols FROM table */
        CreateIndexStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        if (at(p,"if") && is_kw(p,1,"not") && is_kw(p,2,"exists")) { p->i += 3; st->if_not_exists = true; }
        qname(p, &st->schema, &st->name);
        if (at_kind(p, T_LPAREN)) {
            p->i++;
            do spush(&st->incl, &st->nincl, ident_any(p)); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
        expect_kw(p, "on");
        do {
#if PGVC_VN >= 140
            if (at_kind(p, T_LPAREN)) {          /* an expression key */
                int s0 = p->i;
                p->i++;
                parse_expr(p);
                expect_kind(p, T_RPAREN);
                spush(&st->exprs, &st->nexprs, expr_src(p, s0, p->i));
                continue;
            }
#endif
            spush(&st->cols, &st->ncols, ident_any(p));
        } while (accept_kind(p, T_COMMA));
        expect_kw(p, "from");
        { char *sch=NULL,*nm=NULL; qname(p,&sch,&nm); free(sch); st->table = nm; }
        return mknode(N_CREATE_STATS, st);
    }
    if (accept_kw(p, "trigger") || accept_kw(p, "rule") || accept_kw(p, "policy") ||
        accept_kw(p, "extension") || accept_kw(p, "domain") || accept_kw(p, "collation") ||
        accept_kw(p, "operator") || accept_kw(p, "aggregate") || accept_kw(p, "cast") ||
        accept_kw(p, "tablespace") || accept_kw(p, "server")) {
        while (!at_kind(p, T_SEMI) && !at_kind(p, T_EOF)) p->i++;
        return mknode(N_CREATE_TRIGGER, NULL);
    }
    syntax_error(p);
}

/* ---------------------------------------------------------------- ALTER */
static Node *parse_alter(P *p)
{
    expect_kw(p, "alter");
    if (at(p, "table") || (at(p,"materialized") && is_kw(p,1,"view")) ||
        at(p, "view") || at(p, "index") || at(p, "sequence") || at(p, "foreign")) {
        AlterTableStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        if (accept_kw(p, "foreign")) { }
        if (accept_kw(p, "materialized")) { expect_kw(p, "view"); st->objtype = xstrdup("materialized view"); }
        else st->objtype = ident_any(p);
        if (at(p,"if") && is_kw(p,1,"exists")) { p->i += 2; st->if_exists = true; }
        st->only = accept_kw(p, "only");
        qname(p, &st->schema, &st->name);
        accept_op(p, "*");
        do {
            AlterTableCmd *c = xmalloc(sizeof *c);
            memset(c, 0, sizeof *c);
            if (accept_kw(p, "add")) {
                if (at(p,"constraint")||at(p,"primary")||at(p,"unique")||at(p,"check")||
                    at(p,"foreign")||at(p,"exclude")) {
                    c->kind = 8; c->cons = parse_table_constraint(p);
                    if (accept_kw(p, "not")) { expect_kw(p, "valid"); }
                } else {
                    accept_kw(p, "column");
                    if (at(p,"if") && is_kw(p,1,"not") && is_kw(p,2,"exists")) { p->i += 3; c->missing_ok = true; }
                    c->kind = 1;
                    ColDef *cd = xmalloc(sizeof *cd); memset(cd, 0, sizeof *cd);
                    cd->name = ident_any(p);
                    cd->typename = type_name_text(p, &cd->type, &cd->typmod);
                    if (accept_kw(p, "collate")) { char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); cd->collation = b; }
                    parse_col_constraints(p, cd);
                    c->col = cd;
                }
#if PGVC_VN >= 180
            } else if (at(p, "alter") && is_kw(p, 1, "constraint")) {
                p->i += 2;
                c->kind = 28;
                c->name = ident_any(p);
                while (!at_kind(p, T_COMMA) && !at_kind(p, T_SEMI) && !at_kind(p, T_EOF)) p->i++;
#endif
            } else if (accept_kw(p, "drop")) {
                if (accept_kw(p, "constraint")) {
                    c->kind = 9;
                    if (at(p,"if") && is_kw(p,1,"exists")) { p->i += 2; c->if_exists = true; }
                    c->name = ident_any(p);
                    c->cascade = accept_kw(p, "cascade"); accept_kw(p, "restrict");
                } else {
                    accept_kw(p, "column");
                    c->kind = 2;
                    if (at(p,"if") && is_kw(p,1,"exists")) { p->i += 2; c->if_exists = true; }
                    c->name = ident_any(p);
                    c->cascade = accept_kw(p, "cascade"); accept_kw(p, "restrict");
                }
            } else if (at(p, "alter") && (is_kw(p, 1, "column") ?
                       PEEK(p, 2).kind == T_NUMBER : PEEK(p, 1).kind == T_NUMBER)) {
                /* ALTER INDEX ... ALTER [COLUMN] n SET STATISTICS m */
                p->i++;
                accept_kw(p, "column");
                c->kind = 25;
                c->seq_arg = strtoll(CUR(p).sval, NULL, 10); p->i++;
                expect_kw(p, "set"); expect_kw(p, "statistics");
                c->seq_what = (int)strtoll(CUR(p).sval, NULL, 10); p->i++;
            } else if (accept_kw(p, "alter")) {
                accept_kw(p, "column");
                c->name = ident_any(p);
                bool want_type = false;
                if (at(p, "type")) { p->i++; want_type = true; }
                else if (at(p, "set") && is_kw(p, 1, "data") && is_kw(p, 2, "type")) { p->i += 3; want_type = true; }
                if (want_type) {
                    c->kind = 3;
                    c->newtype = type_name_text(p, &c->type, &c->typmod);
                    if (accept_kw(p, "collate")) { char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); free(b); }
                    if (accept_kw(p, "using")) c->using_expr = parse_expr_src(p, NULL);
                } else if (accept_kw(p, "restart")) {
                    c->kind = 22; c->seq_what = 1;
                    accept_kw(p, "with");
                    c->seq_arg = (CUR(p).kind == T_NUMBER) ? seq_number(p) : 1;
                } else if (accept_kw(p, "set")) {
                    if (accept_kw(p, "default")) { c->kind = 4; c->name2 = parse_expr_src(p, NULL); }
                    else if (accept_kw(p, "not")) { expect_kw(p, "null"); c->kind = 6; }
                    else if (accept_kw(p, "statistics")) { c->kind = 18; p->i++; }
                    else if (accept_kw(p, "storage")) { c->kind = 18; free(ident_any(p)); }
#if PGVC_VN >= 140
                    else if (accept_kw(p, "compression")) { c->kind = 33; c->name2 = ident_any(p); }
#endif
                    else if (accept_kw(p, "generated")) { c->kind = 21; c->name2 = ident_any(p); }
#if PGVC_VN >= 170
                    else if (accept_kw(p, "expression")) {
                        /* ALTER COLUMN ... SET EXPRESSION AS (expr) */
                        c->kind = 26;
                        expect_kw(p, "as");
                        c->name2 = parse_expr_src(p, NULL);
                    }
#endif
                    else if (accept_kw(p, "increment")) { c->kind = 22; c->seq_what = 2;
                                                         accept_kw(p, "by"); c->seq_arg = seq_number(p); }
                    else if (accept_kw(p, "start")) { c->kind = 22; c->seq_what = 3;
                                                      accept_kw(p, "with"); c->seq_arg = seq_number(p); }
                    else if (at_kind(p, T_LPAREN)) { c->kind = 18; p->i++; while (!at_kind(p,T_RPAREN)) p->i++; p->i++; }
                    else syntax_error(p);
                } else if (accept_kw(p, "drop")) {
                    if (accept_kw(p, "default")) c->kind = 5;
                    else if (accept_kw(p, "not")) { expect_kw(p, "null"); c->kind = 7; }
                    else if (accept_kw(p, "identity")) { c->kind = 17;
                        if (at(p,"if") && is_kw(p,1,"exists")) { p->i += 2; c->if_exists = true; } }
                    else if (accept_kw(p, "expression")) { c->kind = 30;
                        if (at(p,"if") && is_kw(p,1,"exists")) { p->i += 2; c->if_exists = true; } }
                    else syntax_error(p);
                } else if (accept_kw(p, "add")) {
                    expect_kw(p, "generated");
                    c->kind = 16;
                    if (accept_kw(p, "always")) c->name2 = xstrdup("a");
                    else { expect_kw(p, "by"); expect_kw(p, "default"); c->name2 = xstrdup("d"); }
                    expect_kw(p, "as"); expect_kw(p, "identity");
                    c->col = xmalloc(sizeof(ColDef)); memset(c->col, 0, sizeof(ColDef));
                    parse_identity_seqopts(p, c->col);
                } else syntax_error(p);
            } else if (accept_kw(p, "attach")) {
                expect_kw(p, "partition");
                CreateTableStmt *b = xmalloc(sizeof *b); memset(b, 0, sizeof *b);
                qname(p, &b->schema, &b->name);
                if (st->objtype && !strcmp(st->objtype, "index")) {
                    c->kind = 24;               /* ALTER INDEX ... ATTACH PARTITION */
                    c->name = xstrdup(b->name);
                } else {
                    c->kind = 10;
                    parse_partbound(p, b);
                }
                c->bound = b;
            } else if (accept_kw(p, "detach")) {
                expect_kw(p, "partition");
                c->kind = 11;
                char *sch=NULL,*nm=NULL; qname(p,&sch,&nm); free(sch);
                c->name = nm;
                c->concurrently = accept_kw(p, "concurrently");
                c->finalize = accept_kw(p, "finalize");
            } else if (accept_kw(p, "rename")) {
                if (accept_kw(p, "to")) { c->kind = 12; c->name = ident_any(p); }
                else { accept_kw(p, "column"); c->kind = 13; c->name = ident_any(p);
                       expect_kw(p, "to"); c->name2 = ident_any(p); }
            } else if (accept_kw(p, "owner")) { expect_kw(p, "to"); c->kind = 14; c->name = ident_any(p); }
            else if (accept_kw(p, "set")) {
                if (accept_kw(p, "schema")) { c->kind = 15; c->name = ident_any(p); }
                else if (at(p, "with") && is_kw(p, 1, "oids") && PGVC_VN >= 120)
                    ereport_pos(p->s, "42601", CUR(p).pos, "syntax error at or near \"WITH\"");
                else if (at(p, "access") && is_kw(p, 1, "method")) {
                    p->i += 2; c->kind = 34; c->name = ident_any(p);
                }
                else if (at_kind(p, T_LPAREN) && st->objtype &&
                         (!strcmp(st->objtype, "index") || !strcmp(st->objtype, "view") ||
                          !strcmp(st->objtype, "table") ||
                          !strcmp(st->objtype, "materialized view"))) {
                    c->kind = 31;
                    p->i++;
                    do {
                        int kp = CUR(p).pos;
                        char *k = ident_any(p);
                        for (char *q = k; *q; q++) *q = (char)tolower((unsigned char)*q);
                        char *v = NULL;
                        if (accept_op(p, "=")) {
                            if (CUR(p).kind == T_IDENT) v = ident_any(p);
                            else if (CUR(p).kind == T_NUMBER || CUR(p).kind == T_STRING) {
                                v = xstrndup(CUR(p).kind == T_NUMBER ? CUR(p).start : CUR(p).sval,
                                             CUR(p).kind == T_NUMBER
                                                 ? (size_t)(CUR(p).end - CUR(p).start)
                                                 : strlen(CUR(p).sval));
                                p->i++;
                            } else syntax_error(p);
                        }
                        c->optname = xrealloc(c->optname, (size_t)(c->nopts+1)*sizeof(char*));
                        c->optval  = xrealloc(c->optval,  (size_t)(c->nopts+1)*sizeof(char*));
                        c->optpos  = xrealloc(c->optpos,  (size_t)(c->nopts+1)*sizeof(int));
                        c->optname[c->nopts] = k;
                        c->optval[c->nopts] = v ? v : xstrdup("true");
                        c->optpos[c->nopts] = kp;
                        c->nopts++;
                    } while (accept_kind(p, T_COMMA));
                    expect_kind(p, T_RPAREN);
                }
                else { c->kind = 18; while (!at_kind(p,T_SEMI) && !at_kind(p,T_EOF) && !at_kind(p,T_COMMA)) p->i++; }
            }
            else if (at(p, "reset") && st->objtype &&
                     (!strcmp(st->objtype, "index") || !strcmp(st->objtype, "view") ||
                      !strcmp(st->objtype, "table") ||
                      !strcmp(st->objtype, "materialized view"))) {
                p->i++;
                c->kind = 32;
                expect_kind(p, T_LPAREN);
                do {
                    char *k = ident_any(p);
                    for (char *q = k; *q; q++) *q = (char)tolower((unsigned char)*q);
                    c->optname = xrealloc(c->optname, (size_t)(c->nopts+1)*sizeof(char*));
                    c->optname[c->nopts] = k;
                    c->nopts++;
                } while (accept_kind(p, T_COMMA));
                expect_kind(p, T_RPAREN);
            }
            else if (accept_kw(p, "enable") || accept_kw(p, "disable")) {
                c->kind = 19; while (!at_kind(p,T_SEMI) && !at_kind(p,T_EOF) && !at_kind(p,T_COMMA)) p->i++;
            }
            else if (accept_kw(p, "replica")) { c->kind = 20; expect_kw(p, "identity");
                while (!at_kind(p,T_SEMI) && !at_kind(p,T_EOF) && !at_kind(p,T_COMMA)) p->i++; }
            else if (accept_kw(p, "owned")) { c->kind = 18; expect_kw(p, "by");
                if (accept_kw(p, "none")) { }
                else { char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); free(b);
                       if (at_kind(p, T_DOT)) { p->i++; free(ident_any(p)); } } }
            else if (accept_kw(p, "validate")) { c->kind = 27; expect_kw(p, "constraint");
                                                 c->name = ident_any(p); }
            else if (accept_kw(p, "inherit") || accept_kw(p, "no")) { c->kind = 18;
                while (!at_kind(p,T_SEMI) && !at_kind(p,T_EOF) && !at_kind(p,T_COMMA)) p->i++; }
            else if (at(p,"restart") || at(p,"increment") || at(p,"minvalue") ||
                     at(p,"maxvalue") || at(p,"cache") || at(p,"cycle") || at(p,"start") ||
                     at(p,"as")) {
                /* ALTER SEQUENCE takes a space-separated option list */
                c->kind = 23;
                for (;;) {
                    if (accept_kw(p, "restart")) { accept_kw(p, "with");
                        c->sq_has_restart = true;
                        c->sq_restart = (CUR(p).kind == T_NUMBER || at_op(p,"-")) ? seq_number(p) : INT64_MIN; }
                    else if (accept_kw(p, "increment")) { accept_kw(p, "by");
                        c->sq_inc = seq_number(p); c->sq_has_inc = true; }
                    else if (accept_kw(p, "minvalue")) { c->sq_min = seq_number(p); c->sq_has_min = true; }
                    else if (accept_kw(p, "maxvalue")) { c->sq_max = seq_number(p); c->sq_has_max = true; }
                    else if (accept_kw(p, "cache")) { c->sq_cache = seq_number(p); c->sq_has_cache = true; }
                    else if (accept_kw(p, "cycle")) { c->sq_cycle = true; c->sq_has_cycle = true; }
                    else if (accept_kw(p, "start")) { accept_kw(p, "with");
                        c->sq_start = seq_number(p); c->sq_has_start = true; }
                    else if (accept_kw(p, "as")) { Oid ty; int32 tm; free(type_name_text(p, &ty, &tm)); }
                    else if (at(p,"no") && (is_kw(p,1,"cycle") || is_kw(p,1,"minvalue") ||
                                            is_kw(p,1,"maxvalue"))) {
                        p->i++;
                        if (accept_kw(p, "cycle")) { c->sq_cycle = false; c->sq_has_cycle = true; }
                        else p->i++;
                    }
                    else break;
                }
            }
            else syntax_error(p);
            st->cmds = xrealloc(st->cmds, (size_t)(st->ncmds+1)*sizeof(AlterTableCmd*));
            st->cmds[st->ncmds++] = c;
        } while (accept_kind(p, T_COMMA));
        return mknode(N_ALTER_TABLE, st);
    }
    if (accept_kw(p, "statistics")) {
        CreateIndexStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        if (at(p,"if") && is_kw(p,1,"exists")) { p->i += 2; st->if_not_exists = true; }
        qname(p, &st->schema, &st->name);
        if (accept_kw(p, "rename")) { expect_kw(p, "to"); st->stats_alter = 2;
                                      st->newname = ident_any(p); }
        else if (accept_kw(p, "owner")) { expect_kw(p, "to"); st->stats_alter = 3;
                                          free(ident_any(p)); }
        else if (accept_kw(p, "set")) {
            if (accept_kw(p, "schema")) { st->stats_alter = 3; free(ident_any(p)); }
            else {
                expect_kw(p, "statistics");
                st->stats_alter = 1;
                bool neg = accept_op(p, "-");
                if (CUR(p).kind != T_NUMBER) syntax_error(p);
                st->stattarget = (int)strtol(CUR(p).start, NULL, 10);
                if (neg) st->stattarget = -st->stattarget;
                p->i++;
            }
        }
        else syntax_error(p);
        return mknode(N_CREATE_STATS, st);
    }
    if (accept_kw(p, "system")) {
        VarSetStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        if (accept_kw(p, "reset")) { st->is_reset = true; st->name = at_ident(p) ? ident_any(p) : NULL;
                                     if (!st->name) { expect_kw(p, "all"); st->is_all = true; } }
        else { expect_kw(p, "set"); st->name = ident_any(p);
               if (accept_op(p, "=") || accept_kw(p, "to")) {
                   if (at_ident(p)) st->value = ident_any(p);
                   else { st->value = xstrdup(CUR(p).sval); p->i++; } } }
        return mknode(N_ALTER_SYSTEM, st);
    }
    if (accept_kw(p, "publication")) {
        CreatePubStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        st->insert_ = st->update_ = st->delete_ = st->truncate_ = true;
        st->name = ident(p);
        if (accept_kw(p, "owner")) { expect_kw(p, "to"); st->alter_kind = 4;
                                     free(ident_any(p)); }
        else if (accept_kw(p, "rename")) { expect_kw(p, "to"); st->alter_kind = 5;
                                           st->newname = ident_any(p); }
        else if (at(p,"add") || at(p,"set") || at(p,"drop")) {
            int kind = at(p,"add") ? 1 : at(p,"set") ? 2 : 3;
            p->i++;
            if (kind == 2 && at_kind(p, T_LPAREN)) {
                st->alter_kind = 0;
                p->i++;
                st->has_publish = true;
                do {
                    char *k = ident_any(p);
                    char *v = NULL;
                    if (accept_op(p, "=")) { if (CUR(p).kind == T_IDENT) v = ident_any(p);
                                             else { v = xstrdup(CUR(p).sval); p->i++; } }
                    if (!strcmp(k, "publish")) {
                        st->has_publish_opt = true;
                        st->insert_ = st->update_ = st->delete_ = st->truncate_ = false;
                        if (v) for (char *tok = strtok(v, ", "); tok; tok = strtok(NULL, ", ")) {
                            if (!strcmp(tok,"insert")) st->insert_ = true;
                            else if (!strcmp(tok,"update")) st->update_ = true;
                            else if (!strcmp(tok,"delete")) st->delete_ = true;
                            else if (!strcmp(tok,"truncate")) st->truncate_ = true;
                            else if (!st->bad_publish) st->bad_publish = xstrdup(tok);
                        }
                    } else if (!strcmp(k, "publish_via_partition_root")) {
                        st->has_viaroot = true;
                        if (!v || !strcasecmp(v,"true") || !strcasecmp(v,"on") ||
                            !strcmp(v,"1") || !strcasecmp(v,"yes")) st->viaroot = true;
                        else if (!strcasecmp(v,"false") || !strcasecmp(v,"off") ||
                                 !strcmp(v,"0") || !strcasecmp(v,"no")) st->viaroot = false;
                        else if (!st->bad_bool) st->bad_bool = xstrdup(k);
                    }
                    else if (!st->bad_param) st->bad_param = xstrdup(k);
                    free(k); free(v);
                } while (accept_kind(p, T_COMMA));
                expect_kind(p, T_RPAREN);
            } else {
                st->alter_kind = kind;
                do {
                    if (accept_kw(p, "tables")) { expect_kw(p, "in"); expect_kw(p, "schema");
                        do free(ident_any(p)); while (accept_kind(p, T_COMMA)); continue; }
                    accept_kw(p, "table");
                    char *sch=NULL,*nm=NULL; qname(p,&sch,&nm);
                    char **pair = xmalloc(2*sizeof(char*)); pair[0]=sch; pair[1]=nm;
                    st->tables = xrealloc(st->tables, (size_t)(st->ntables+1)*sizeof(char**));
                    st->where = xrealloc(st->where, (size_t)(st->ntables+1)*sizeof(char*));
                    st->collist = xrealloc(st->collist, (size_t)(st->ntables+1)*sizeof(char**));
                    st->ncollist = xrealloc(st->ncollist, (size_t)(st->ntables+1)*sizeof(int));
                    st->where[st->ntables] = NULL;
                    st->collist[st->ntables] = NULL;
                    st->ncollist[st->ntables] = 0;
                    if (at_kind(p, T_LPAREN)) {
                        p->i++;
                        char **cl = NULL; int ncl = 0;
                        do spush(&cl, &ncl, ident_any(p)); while (accept_kind(p, T_COMMA));
                        expect_kind(p, T_RPAREN);
                        st->collist[st->ntables] = cl; st->ncollist[st->ntables] = ncl;
                    }
                    if (accept_kw(p, "where")) {
                        expect_kind(p, T_LPAREN);
                        int s0 = p->i;
                        st->wherepos = xrealloc(st->wherepos,
                                                (size_t)(st->ntables+1)*sizeof(int));
                        st->wherepos[st->ntables] = CUR(p).pos;
                        parse_expr(p);
                        st->where[st->ntables] = expr_src(p, s0, p->i);
                        expect_kind(p, T_RPAREN);
                    } else {
                        st->wherepos = xrealloc(st->wherepos,
                                                (size_t)(st->ntables+1)*sizeof(int));
                        st->wherepos[st->ntables] = 0;
                    }
                    st->tables[st->ntables++] = pair;
                } while (accept_kind(p, T_COMMA));
            }
        } else while (!at_kind(p, T_SEMI) && !at_kind(p, T_EOF)) p->i++;
        return mknode(N_ALTER_PUB, st);
    }
    if (accept_kw(p, "role") || accept_kw(p, "user") || accept_kw(p, "group")) {
        CreateRoleStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        st->name = ident_any(p);
        accept_kw(p, "with");
        for (;;) {
            if (accept_kw(p, "password")) {
                if (accept_kw(p, "null")) { st->has_password = true; st->password = NULL; }
                else { st->password = xstrdup(CUR(p).sval); st->has_password = true; p->i++; }
            }
            else if (accept_kw(p, "login")) st->login = true;
            else if (accept_kw(p, "superuser")) st->superuser = true;
            else if (at_kind(p, T_SEMI) || at_kind(p, T_EOF)) break;
            else p->i++;
        }
        return mknode(N_ALTER_ROLE, st);
    }
    if (accept_kw(p, "subscription")) {
        CreateSubStmt *st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        st->name = ident(p);
        if (at(p, "set") && PEEK(p,1).kind == T_LPAREN) {
            p->i += 2;
            do sub_option(p, st); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        } else if (accept_kw(p, "enable")) { st->enabled = true;  st->has_enabled = true; }
        else if (accept_kw(p, "disable"))  { st->enabled = false; st->has_enabled = true; }
        else if (accept_kw(p, "connection")) {
            if (CUR(p).kind != T_EOF) { st->conninfo = xstrdup(CUR(p).sval); p->i++; }
        }
        else { st->alter_kind = 1;
               while (!at_kind(p, T_SEMI) && !at_kind(p, T_EOF)) p->i++; }
        return mknode(N_ALTER_SUB, st);
    }
    if (accept_kw(p, "schema") || accept_kw(p, "database") || accept_kw(p, "type") ||
        accept_kw(p, "function") || accept_kw(p, "procedure") ||
        accept_kw(p, "default") || accept_kw(p, "extension")) {
        while (!at_kind(p, T_SEMI) && !at_kind(p, T_EOF)) p->i++;
        return mknode(N_CREATE_TRIGGER, NULL);
    }
    syntax_error(p);
}

static Node *parse_drop(P *p)
{
    expect_kw(p, "drop");
    if (at(p, "owned")) {           /* DROP OWNED BY role [, ...] [CASCADE|RESTRICT] */
        p->i++;
        expect_kw(p, "by");
        do free(ident_any(p)); while (accept_kind(p, T_COMMA));
        accept_kw(p, "cascade"); accept_kw(p, "restrict");
        return mknode(N_ANALYZE, xstrdup("drop"));
    }
    DropStmt *st = xmalloc(sizeof *st);
    memset(st, 0, sizeof *st);
    if (accept_kw(p, "materialized")) { expect_kw(p, "view"); st->objtype = xstrdup("materialized view"); }
    else st->objtype = ident_any(p);
    if (!strcmp(st->objtype, "index")) accept_kw(p, "concurrently");
    if (at(p,"if") && is_kw(p,1,"exists")) { p->i += 2; st->if_exists = true; }
    bool isfunc = !strcmp(st->objtype, "function") || !strcmp(st->objtype, "procedure") ||
                  !strcmp(st->objtype, "aggregate") || !strcmp(st->objtype, "routine");
    do {
        char *sch=NULL,*nm=NULL;
        char *sig = NULL; int nsig = 0;
        qname(p, &sch, &nm);
        if (isfunc && at_kind(p, T_LPAREN)) {
            /* remember the argument type list so overloads resolve */
            p->i++;
            Buf b; buf_init(&b);
            while (!at_kind(p, T_RPAREN) && !at_kind(p, T_EOF)) {
                accept_kw(p, "in"); accept_kw(p, "out"); accept_kw(p, "inout");
                accept_kw(p, "variadic");
                Oid ty; int32 tm;
                char *tn = type_name_text(p, &ty, &tm);
                if (b.len) buf_adds(&b, ", ");
                { char *ft = format_type_str(ty, -1); buf_adds(&b, ft ? ft : tn); free(ft); }
                nsig++;
                free(tn);
                if (!accept_kind(p, T_COMMA)) break;
            }
            expect_kind(p, T_RPAREN);
            sig = buf_steal(&b);
        }
        char **pair = xmalloc(2*sizeof(char*)); pair[0]=sch; pair[1]=nm;
        st->names = xrealloc(st->names, (size_t)(st->nnames+1)*sizeof(char**));
        st->argsig = xrealloc(st->argsig, (size_t)(st->nnames+1)*sizeof(char*));
        st->nargcount = xrealloc(st->nargcount, (size_t)(st->nnames+1)*sizeof(int));
        st->argsig[st->nnames] = sig;
        st->nargcount[st->nnames] = sig ? nsig : -1;
        st->names[st->nnames++] = pair;
    } while (accept_kind(p, T_COMMA));
    st->nargsig = st->nnames;
    if (accept_kw(p, "on")) { char *a=NULL,*b2=NULL; qname(p,&a,&b2); free(a); st->onrel = b2; }
#if PGVC_VN >= 130
    /* DROP DATABASE name [ [ WITH ] ( FORCE ) ] */
    if (!strcmp(st->objtype, "database") &&
        (at_kind(p, T_LPAREN) || (at(p, "with") && PEEK(p,1).kind == T_LPAREN))) {
        accept_kw(p, "with");
        expect_kind(p, T_LPAREN);
        do {
            if (!at(p, "force")) syntax_error(p);
            p->i++;
        } while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
    }
#endif
    st->cascade = accept_kw(p, "cascade");
    accept_kw(p, "restrict");
    return mknode(N_DROP, st);
}

static Node *parse_copy(P *p)
{
    expect_kw(p, "copy");
    CopyStmt *st = xmalloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->delim = '\t'; st->quote = '"'; st->escape = '"';
    st->format = xstrdup("text");
    if (at_kind(p, T_LPAREN)) {
        p->i++;
        st->query = parse_stmt(p);
        expect_kind(p, T_RPAREN);
    } else {
        qname(p, &st->schema, &st->name);
        if (at_kind(p, T_LPAREN)) {
            p->i++;
            do spush(&st->cols, &st->ncols, ident_any(p)); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
    }
    if (accept_kw(p, "from")) st->from = true;
    else expect_kw(p, "to");
    if (accept_kw(p, "stdin")) st->stdin_ = true;
    else if (accept_kw(p, "stdout")) st->stdout_ = true;
    else if (accept_kw(p, "program")) { st->filename = xstrdup(CUR(p).sval); p->i++; }
    else { st->filename = xstrdup(CUR(p).sval); p->i++; }
    bool paren = false;
    if (accept_kw(p, "with")) paren = at_kind(p, T_LPAREN);
    if (at_kind(p, T_LPAREN)) { paren = true; p->i++; }
    for (;;) {
        if (paren && at_kind(p, T_RPAREN)) { p->i++; break; }
        if (!paren && (at_kind(p, T_SEMI) || at_kind(p, T_EOF))) break;
        if (accept_kw(p, "format")) { free(st->format); st->format = ident_any(p); }
        else if (accept_kw(p, "csv")) { free(st->format); st->format = xstrdup("csv"); st->delim = ','; }
        else if (accept_kw(p, "binary")) { free(st->format); st->format = xstrdup("binary"); }
        else if (accept_kw(p, "freeze")) { st->freeze = true; accept_kw(p,"true"); accept_kw(p,"false"); }
        else if (accept_kw(p, "delimiter")) { accept_kw(p, "as"); st->delim = CUR(p).sval[0]; p->i++; }
        else if (accept_kw(p, "null")) { accept_kw(p, "as"); st->null_str = xstrdup(CUR(p).sval); p->i++; }
        else if (accept_kw(p, "header")) {
            st->header = true;
            if (accept_kw(p, "true") || accept_kw(p, "on")) { }
            else if (accept_kw(p, "false") || accept_kw(p, "off")) st->header = false;
#if PGVC_VN >= 150
            else if (accept_kw(p, "match")) st->header_match = true;
#endif
            else if (CUR(p).kind == T_IDENT || CUR(p).kind == T_NUMBER ||
                     CUR(p).kind == T_STRING) {
                if (!at_kind(p, T_COMMA) && !at_kind(p, T_RPAREN)) {
                    st->bad_header = true;
                    p->i++;
                }
            }
        }
        else if (accept_kw(p, "quote")) { accept_kw(p, "as"); st->quote = CUR(p).sval[0]; p->i++; }
        else if (accept_kw(p, "escape")) { accept_kw(p, "as"); st->escape = CUR(p).sval[0]; p->i++; }
        else if (at(p, "on_error")) {
            st->on_error_pos = CUR(p).pos; p->i++;
            st->on_error = ident_any(p);
        }
        else if (at(p, "log_verbosity")) { p->i++; st->log_verbosity = ident_any(p); }
        else if (accept_kw(p, "encoding")) { p->i++; }
        else if (accept_kw(p, "force")) { accept_kw(p,"not"); accept_kw(p,"null"); accept_kw(p,"quote");
            if (accept_op(p,"*")) { } else if (at_kind(p,T_LPAREN)) { p->i++; while (!at_kind(p,T_RPAREN)) p->i++; p->i++; }
            else do free(ident_any(p)); while (accept_kind(p, T_COMMA)); }
        else if (accept_kind(p, T_COMMA)) continue;
        else break;
    }
    if (at(p, "where")) {
        st->where_pos = CUR(p).pos;
        p->i++;
        st->where = parse_expr(p);
    }
    if (!strcmp(st->format, "csv") && st->delim == '\t') st->delim = ',';
    if (!st->null_str) st->null_str = xstrdup(!strcmp(st->format, "csv") ? "" : "\\N");
    return mknode(N_COPY, st);
}

static Node *parse_transaction(P *p)
{
    TransactionStmt *st = xmalloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->isolation = -1; st->readonly = -1; st->deferrable = -1;
    if (accept_kw(p, "begin")) { st->kind = 0; accept_kw(p, "work"); accept_kw(p, "transaction"); }
    else if (accept_kw(p, "start")) { expect_kw(p, "transaction"); st->kind = 0; }
    else if (accept_kw(p, "commit")) { st->kind = 1; accept_kw(p, "work"); accept_kw(p, "transaction");
        if (accept_kw(p, "and")) { bool no = accept_kw(p, "no"); expect_kw(p, "chain"); (void)no; } }
    else if (accept_kw(p, "end")) { st->kind = 1; accept_kw(p, "work"); accept_kw(p, "transaction"); }
    else if (accept_kw(p, "abort")) { st->kind = 2; accept_kw(p, "work"); accept_kw(p, "transaction"); }
    else if (accept_kw(p, "rollback")) {
        accept_kw(p, "work"); accept_kw(p, "transaction");
        if (accept_kw(p, "to")) { accept_kw(p, "savepoint"); st->kind = 4; st->name = ident_any(p); }
        else { st->kind = 2; if (accept_kw(p, "and")) { accept_kw(p, "no"); expect_kw(p, "chain"); } }
    }
    else if (accept_kw(p, "savepoint")) { st->kind = 3; st->name = ident_any(p); }
    else if (accept_kw(p, "release")) { accept_kw(p, "savepoint"); st->kind = 5; st->name = ident_any(p); }
    else if (accept_kw(p, "set")) { expect_kw(p, "transaction"); st->kind = 7; }
    else syntax_error(p);
    /* transaction modes */
    for (;;) {
        if (accept_kw(p, "isolation")) {
            expect_kw(p, "level");
            if (accept_kw(p, "read")) { if (accept_kw(p, "committed")) st->isolation = 0; else { expect_kw(p,"uncommitted"); st->isolation = 0; } }
            else if (accept_kw(p, "repeatable")) { expect_kw(p, "read"); st->isolation = 1; }
            else { expect_kw(p, "serializable"); st->isolation = 2; }
        } else if (accept_kw(p, "read")) { st->readonly = accept_kw(p, "only") ? 1 : (expect_kw(p,"write"), 0); }
        else if (accept_kw(p, "not")) { expect_kw(p, "deferrable"); st->deferrable = 0; }
        else if (accept_kw(p, "deferrable")) st->deferrable = 1;
        else break;
        accept_kind(p, T_COMMA);
    }
    return mknode(N_TRANSACTION, st);
}

static Node *parse_set(P *p)
{
    VarSetStmt *st = xmalloc(sizeof *st);
    memset(st, 0, sizeof *st);
    expect_kw(p, "set");
    if (accept_kw(p, "local")) st->is_local = true;
    else accept_kw(p, "session");
    if (at(p, "transaction") || at2(p, "session", "characteristics")) { p->i = 0; return parse_transaction(p); }
    if (accept_kw(p, "constraints")) { while (!at_kind(p,T_SEMI) && !at_kind(p,T_EOF)) p->i++;
                                       st->name = xstrdup("constraints"); return mknode(N_SET, st); }
    if (accept_kw(p, "role")) { st->name = xstrdup("role");
        if (at_ident(p)) st->value = ident_any(p); else { st->value = xstrdup(CUR(p).sval); p->i++; }
        return mknode(N_SET, st); }
    if (accept_kw(p, "time")) { expect_kw(p, "zone"); st->name = xstrdup("timezone");
        if (accept_kw(p, "local") || accept_kw(p, "default")) st->is_default = true;
        else if (at_ident(p)) st->value = ident_any(p);
        else { st->value = xstrdup(CUR(p).sval); p->i++; }
        return mknode(N_SET, st); }
    /* name may be qualified: SET a.b = c */
    {
        Buf b; buf_init(&b);
        buf_adds(&b, ident_any(p));
        while (at_kind(p, T_DOT)) { p->i++; buf_addc(&b, '.'); buf_adds(&b, ident_any(p)); }
        st->name = buf_steal(&b);
    }
    if (!accept_op(p, "=")) expect_kw(p, "to");
    if (accept_kw(p, "default")) st->is_default = true;
    else {
        do {
            char *v;
            if (CUR(p).kind == T_IDENT) v = ident_any(p);
            else if (CUR(p).kind == T_STRING || CUR(p).kind == T_NUMBER) { v = xstrdup(CUR(p).sval); p->i++; }
            else if (at_op(p, "-")) { p->i++; Buf b; buf_init(&b); buf_addc(&b,'-'); buf_adds(&b, CUR(p).sval); p->i++; v = buf_steal(&b); }
            else syntax_error(p);
            spush(&st->values, &st->nvalues, v);
        } while (accept_kind(p, T_COMMA));
        Buf b; buf_init(&b);
        for (int k = 0; k < st->nvalues; k++) { if (k) buf_adds(&b, ", "); buf_adds(&b, st->values[k]); }
        st->value = buf_steal(&b);
    }
    return mknode(N_SET, st);
}

static Node *parse_stmt(P *p)
{
    CteDef **ctes = NULL; int nctes = 0; bool rec = false;
    if (at(p, "with")) {
        int save = p->i;
        ctes = parse_with(p, &nctes, &rec);
        if (at(p, "insert")) return parse_insert(p, ctes, nctes);
        if (at(p, "update")) return parse_update(p, ctes, nctes);
        if (at(p, "delete")) return parse_delete(p, ctes, nctes);
        if (at(p, "merge"))  return parse_merge(p, ctes, nctes);
        p->i = save;
        return parse_select_stmt(p, NULL, 0);
    }
    if (at(p, "select") || at(p, "values") || at(p, "table") || at_kind(p, T_LPAREN))
        return parse_select_stmt(p, NULL, 0);
    if (at(p, "insert")) return parse_insert(p, NULL, 0);
    if (at(p, "update")) return parse_update(p, NULL, 0);
    if (at(p, "delete")) return parse_delete(p, NULL, 0);
    if (at(p, "merge"))  return parse_merge(p, NULL, 0);
    if (at(p, "create")) return parse_create(p);
    if (at(p, "alter"))  return parse_alter(p);
    if (at(p, "drop"))   return parse_drop(p);
    if (at(p, "copy"))   return parse_copy(p);
    if (at(p, "do")) {
        p->i++;
        DoStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        if (accept_kw(p, "language")) st->lang = ident_any(p);
        if (CUR(p).kind != T_STRING) syntax_error(p);
        st->body = xstrdup(CUR(p).sval);
        p->i++;
        if (accept_kw(p, "language")) st->lang = ident_any(p);
        return mknode(N_DO, st);
    }
    if (at(p, "begin") || at(p, "commit") || at(p, "rollback") || at(p, "abort") ||
        at(p, "savepoint") || at(p, "release") || at(p, "end") ||
        at2(p, "start", "transaction") || at2(p, "set", "transaction"))
        return parse_transaction(p);
    if (at(p, "set")) return parse_set(p);
    if (at(p, "show")) {
        p->i++;
        VarSetStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        if (accept_kw(p, "all")) st->is_all = true;
        else if (accept_kw(p, "time")) { expect_kw(p, "zone"); st->name = xstrdup("timezone"); }
        else {
            Buf b; buf_init(&b);
            buf_adds(&b, ident_any(p));
            while (at_kind(p, T_DOT)) { p->i++; buf_addc(&b, '.'); buf_adds(&b, ident_any(p)); }
            st->name = buf_steal(&b);
        }
        return mknode(N_SHOW, st);
    }
    if (at(p, "reset")) {
        p->i++;
        VarSetStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        st->is_reset = true;
        if (accept_kw(p, "all")) st->is_all = true;
        else if (accept_kw(p, "time")) { expect_kw(p, "zone"); st->name = xstrdup("timezone"); }
        else if (accept_kw(p, "role")) st->name = xstrdup("role");
        else {
            Buf b; buf_init(&b);
            buf_adds(&b, ident_any(p));
            while (at_kind(p, T_DOT)) { p->i++; buf_addc(&b, '.'); buf_adds(&b, ident_any(p)); }
            st->name = buf_steal(&b);
        }
        return mknode(N_RESET, st);
    }
    if (at(p, "explain")) {
        p->i++;
        ExplainStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        st->costs = true;
        if (at_kind(p, T_LPAREN)) {
            static const char *eopts[] = {"analyze","analyse","verbose","costs","buffers",
                "timing","summary","format","settings","wal","generic_plan",
                "serialize","memory", NULL};
            static const int eminvn[] = {0,0,0,0,0, 0,0,0,120,130,160, 170,170, 0};
            p->i++;
            do {
                int kp = CUR(p).pos;
                char *k = ident_any(p);
                for (char *q = k; *q; q++) *q = (char)tolower((unsigned char)*q);
                char *v = NULL;
                if (!at_kind(p, T_COMMA) && !at_kind(p, T_RPAREN)) {
                    Tok t2 = CUR(p);
                    if (t2.kind == T_IDENT || t2.kind == T_STRING) { v = xstrdup(t2.sval); p->i++; }
                    else if (t2.kind == T_NUMBER) { v = xstrndup(t2.start, (size_t)(t2.end - t2.start)); p->i++; }
                    else syntax_error(p);
                    for (char *q = v; *q; q++) *q = (char)tolower((unsigned char)*q);
                }
                bool known = false;
                for (int i = 0; eopts[i]; i++)
                    if (!strcmp(eopts[i], k) && (!eminvn[i] || PGVC_VN >= eminvn[i])) known = true;
                if (!known) {
                    if (!st->errkind) { st->errkind = 1; st->errarg = xstrdup(k); st->errpos = kp; }
                } else if (!strcmp(k, "format")) {
                    st->format = v ? xstrdup(v) : NULL;
                } else {
                    /* defGetBoolean: no value means true */
                    int on = -1;
                    if (!v) on = 1;
                    else if (!strcmp(v,"true")||!strcmp(v,"on")||!strcmp(v,"1")||!strcmp(v,"t")||
                             !strcmp(v,"yes")||!strcmp(v,"y")) on = 1;
                    else if (!strcmp(v,"false")||!strcmp(v,"off")||!strcmp(v,"0")||!strcmp(v,"f")||
                             !strcmp(v,"no")||!strcmp(v,"n")) on = 0;
                    if (on < 0) {
                        if (!st->errkind) { st->errkind = 2; st->errarg = xstrdup(k); }
                    } else {
                        if (!strcmp(k,"analyze") || !strcmp(k,"analyse")) st->analyze_ = on;
                        else if (!strcmp(k,"verbose")) st->verbose = on;
                        else if (!strcmp(k,"costs")) st->costs = on;
                        else if (!strcmp(k,"buffers")) st->buffers = on;
                        else if (!strcmp(k,"wal")) st->wal = on;
                    }
                }
                free(k); free(v);
            } while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        } else {
            if (accept_kw(p, "analyze") || accept_kw(p, "analyse")) st->analyze_ = true;
            if (accept_kw(p, "verbose")) st->verbose = true;
        }
        st->query = parse_stmt(p);
        return mknode(N_EXPLAIN, st);
    }
    if (at(p, "prepare") && !is_kw(p, 1, "transaction")) {
        p->i++;
        PrepareStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        st->name = ident_any(p);
        if (at_kind(p, T_LPAREN)) {
            p->i++;
            do { Oid t; int32 tm; spush(&st->argtypes, &st->nargs, type_name_text(p, &t, &tm)); }
            while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
        expect_kw(p, "as");
        int s0 = p->i;
        st->query = parse_stmt(p);
        st->src = expr_src(p, s0, p->i);
        return mknode(N_PREPARE, st);
    }
    if (at(p, "execute")) {
        p->i++;
        ExecuteStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        st->name = ident_any(p);
        if (at_kind(p, T_LPAREN)) {
            p->i++;
            if (!at_kind(p, T_RPAREN))
                do xpush(&st->args, &st->nargs, parse_expr(p)); while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
        return mknode(N_EXECUTE, st);
    }
    if (at(p, "deallocate")) {
        p->i++;
        ExecuteStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        if (accept_kw(p, "all")) st->name = NULL;
        else { accept_kw(p, "prepare"); st->name = ident_any(p); }
        return mknode(N_DEALLOCATE, st);
    }
    if (at(p, "call")) {
        p->i++;
        CallStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        st->pos = CUR(p).pos;
        qname(p, &st->schema, &st->name);
        expect_kind(p, T_LPAREN);
        if (!at_kind(p, T_RPAREN))
            do xpush(&st->args, &st->nargs, parse_expr(p)); while (accept_kind(p, T_COMMA));
        expect_kind(p, T_RPAREN);
        return mknode(N_CALL, st);
    }
    if (at(p, "declare")) {
        p->i++;
        CursorStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        st->name = ident_any(p);
        for (;;) {
            if (accept_kw(p, "binary")) st->binary = true;
            else if (accept_kw(p, "insensitive") || accept_kw(p, "asensitive")) continue;
            else if (accept_kw(p, "scroll")) st->scroll = true;
            else if (accept_kw(p, "no")) { expect_kw(p, "scroll"); }
            else break;
        }
        expect_kw(p, "cursor");
        if (accept_kw(p, "with")) { expect_kw(p, "hold"); st->hold = true; }
        else if (accept_kw(p, "without")) expect_kw(p, "hold");
        expect_kw(p, "for");
        st->query = parse_select_stmt(p, NULL, 0);
        if (accept_kw(p, "for")) { accept_kw(p, "read"); accept_kw(p, "only"); accept_kw(p, "update"); }
        return mknode(N_DECLARE, st);
    }
    if (at(p, "fetch") && !is_kw(p, 1, "first") && !is_kw(p, 1, "next")) {
        p->i++;
        CursorStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        st->count = 1; st->dir = 0;
        if (accept_kw(p, "all")) st->count = -1;
        else if (accept_kw(p, "next")) st->count = 1;
        else if (accept_kw(p, "prior")) { st->count = 1; st->dir = 1; }
        else if (accept_kw(p, "first")) st->seek = 1;
        else if (accept_kw(p, "last")) st->seek = 2;
        else if (accept_kw(p, "absolute")) { st->seek = 3; st->seekn = seq_number(p); }
        else if (accept_kw(p, "relative")) { st->seek = 4; st->seekn = seq_number(p); }
        else if (accept_kw(p, "forward")) { if (accept_kw(p, "all")) st->count = -1;
                                            else if (CUR(p).kind == T_NUMBER || at_op(p, "-"))
                                                st->count = seq_number(p); }
        else if (accept_kw(p, "backward")) { st->dir = 1;
                                             if (accept_kw(p, "all")) st->count = -1;
                                             else if (CUR(p).kind == T_NUMBER || at_op(p, "-"))
                                                 st->count = seq_number(p); }
        else if (CUR(p).kind == T_NUMBER || at_op(p, "-")) { st->count = seq_number(p);
                 if (st->count < 0) { st->dir = 1; st->count = -st->count; } }
        accept_kw(p, "from"); accept_kw(p, "in");
        st->name = ident_any(p);
        return mknode(N_FETCH, st);
    }
    if (at(p, "move")) {
        p->i++;
        CursorStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        st->count = 1; st->dir = 0;
        if (accept_kw(p, "all")) st->count = -1;
        else if (accept_kw(p, "next")) st->count = 1;
        else if (accept_kw(p, "prior")) { st->count = 1; st->dir = 1; }
        else if (accept_kw(p, "first")) st->seek = 1;
        else if (accept_kw(p, "last")) st->seek = 2;
        else if (accept_kw(p, "absolute")) { st->seek = 3; st->seekn = seq_number(p); }
        else if (accept_kw(p, "relative")) { st->seek = 4; st->seekn = seq_number(p); }
        else if (accept_kw(p, "forward")) { if (accept_kw(p, "all")) st->count = -1;
                                            else if (CUR(p).kind == T_NUMBER || at_op(p, "-"))
                                                st->count = seq_number(p); }
        else if (accept_kw(p, "backward")) { st->dir = 1;
                                             if (accept_kw(p, "all")) st->count = -1;
                                             else if (CUR(p).kind == T_NUMBER || at_op(p, "-"))
                                                 st->count = seq_number(p); }
        else if (CUR(p).kind == T_NUMBER || at_op(p, "-")) { st->count = seq_number(p);
                 if (st->count < 0) { st->dir = 1; st->count = -st->count; } }
        accept_kw(p, "from"); accept_kw(p, "in");
        st->name = ident_any(p);
        st->all = true;
        return mknode(N_FETCH, st);
    }
    if (at(p, "close")) {
        p->i++;
        CursorStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        if (accept_kw(p, "all")) st->all = true; else st->name = ident_any(p);
        return mknode(N_CLOSE_CURSOR, st);
    }
    if (at(p, "truncate")) {
        p->i++;
        accept_kw(p, "table");
        TruncateStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        do {
            accept_kw(p, "only");
            char *sch=NULL,*nm=NULL; qname(p,&sch,&nm); accept_op(p, "*");
            char **pair = xmalloc(2*sizeof(char*)); pair[0]=sch; pair[1]=nm;
            st->names = xrealloc(st->names, (size_t)(st->nnames+1)*sizeof(char**));
            st->names[st->nnames++] = pair;
        } while (accept_kind(p, T_COMMA));
        if (accept_kw(p, "restart")) { expect_kw(p, "identity"); st->restart = true; }
        else if (accept_kw(p, "continue")) expect_kw(p, "identity");
        st->cascade = accept_kw(p, "cascade"); accept_kw(p, "restrict");
        return mknode(N_TRUNCATE, st);
    }
    if (at(p, "reindex")) {
        p->i++;
        ReindexStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        if (at_kind(p, T_LPAREN)) { p->i++; while (!at_kind(p,T_RPAREN)) p->i++; p->i++; }
        st->kind = ident_any(p);
        st->concurrent = accept_kw(p, "concurrently");
        if (!at_kind(p, T_SEMI) && !at_kind(p, T_EOF)) { char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); st->name = b; }
        return mknode(N_REINDEX, st);
    }
    if (at(p, "grant") || at(p, "revoke")) {
        bool g = at(p, "grant"); p->i++;
        GrantStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        st->is_grant = g;
        int s0 = p->i;
        while (!at(p, "on") && !at_kind(p, T_EOF) && !at_kind(p, T_SEMI)) p->i++;
        st->privs = expr_src(p, s0, p->i);
        if (accept_kw(p, "on")) {
            if (accept_kw(p, "schema")) st->objtype = xstrdup("schema");
            else if (accept_kw(p, "database")) st->objtype = xstrdup("database");
            else if (accept_kw(p, "all")) { free(ident_any(p)); expect_kw(p, "in"); expect_kw(p, "schema");
                                           st->objtype = xstrdup("alltables"); }
            else { accept_kw(p, "table"); st->objtype = xstrdup("table"); }
            do { char *a=NULL,*b=NULL; qname(p,&a,&b); free(a); spush(&st->objs,&st->nobjs,b); }
            while (accept_kind(p, T_COMMA));
        }
        if (accept_kw(p, "from") || accept_kw(p, "to")) {
            do { if (accept_kw(p, "public")) spush(&st->roles,&st->nroles,xstrdup("public"));
                 else if (accept_kw(p, "group")) spush(&st->roles,&st->nroles,ident_any(p));
                 else spush(&st->roles,&st->nroles,ident_any(p)); }
            while (accept_kind(p, T_COMMA));
        }
        while (!at_kind(p, T_SEMI) && !at_kind(p, T_EOF)) p->i++;
        return mknode(N_GRANT, st);
    }
    if (at(p, "vacuum")) {
        p->i++;
        VacuumStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        st->tag = xstrdup("VACUUM");
        if (at_kind(p, T_LPAREN)) {
            static const char *opts[] = {"full","freeze","verbose","analyze","analyse",
                "disable_page_skipping","skip_locked","index_cleanup","truncate",
#if PGVC_VN >= 130
                "parallel",
#endif
                "process_toast","buffer_usage_limit","only_database_stats",
                "skip_database_stats","process_main", NULL};
            p->i++;
            do {
                int kp = CUR(p).pos;
                char *k = (CUR(p).kind == T_IDENT) ? xstrdup(CUR(p).sval) : xstrdup("");
                for (char *q = k; *q; q++) *q = (char)tolower((unsigned char)*q);
                if (CUR(p).kind == T_IDENT) p->i++;
                bool known = false;
                for (int i = 0; opts[i]; i++) if (!strcmp(opts[i], k)) known = true;
                /* the argument, if any */
                int vstart = p->i;
                while (!at_kind(p, T_COMMA) && !at_kind(p, T_RPAREN) && !at_kind(p, T_EOF)) p->i++;
                if (!known) {
                    if (!st->badopt) { st->badopt = xstrdup(k); st->badpos = kp; }
                } else if (!strcmp(k, "full")) {
                    /* FULL, FULL true and FULL on all turn it on */
                    st->full = true;
                    if (p->i > vstart && p->L.toks[vstart].kind == T_IDENT &&
                        p->L.toks[vstart].sval &&
                        (!strcasecmp(p->L.toks[vstart].sval, "false") ||
                         !strcasecmp(p->L.toks[vstart].sval, "off")))
                        st->full = false;
                } else if (!strcmp(k, "parallel")) {
                    st->par_seen = true;
                    if (p->i == vstart) {
                        if (!st->errkind) { st->errkind = 2; st->errpos2 = kp; }
                    } else {
                        Tok tv = p->L.toks[vstart];
                        bool neg = false;
                        int vi = vstart;
                        if (tv.kind == T_OP && tv.sval && !strcmp(tv.sval, "-")) { neg = true; vi++; }
                        Tok tn = p->L.toks[vi];
                        if (tn.kind != T_NUMBER) {
                            if (!st->errkind) { st->errkind = 4; }
                        } else {
                            long v = strtol(tn.start, NULL, 10);
                            if (neg) v = -v;
                            if (v < 0 || v > 1024) {
                                if (!st->errkind) { st->errkind = 3; st->errpos2 = kp; }
                            } else st->parallel = (int)v;
                        }
                    }
                }
                free(k);
            } while (accept_kind(p, T_COMMA));
            expect_kind(p, T_RPAREN);
        }
        while (!at_kind(p, T_SEMI) && !at_kind(p, T_EOF)) p->i++;
        return mknode(N_VACUUM, st);
    }
    if (at(p, "analyze") || at(p, "analyse") || at(p, "checkpoint") ||
        at(p, "cluster") || at(p, "discard") || at(p, "listen") || at(p, "unlisten") ||
        at(p, "notify") || at(p, "comment") || at(p, "lock") || at(p, "security") ||
        at(p, "refresh") || at(p, "do") || at(p, "load") || at(p, "reassign")) {
        NodeTag tg = at(p,"checkpoint") ? N_CHECKPOINT : at(p,"discard") ? N_DISCARD :
                     at(p,"lock") ? N_LOCK : at(p,"comment") ? N_COMMENT : N_ANALYZE;
        char *kw0 = xstrdup(CUR(p).sval);
        while (!at_kind(p, T_SEMI) && !at_kind(p, T_EOF)) p->i++;
        Node *n = mknode(tg, kw0);
        return n;
    }
    /* replication protocol commands (used on replication connections) */
    if (at(p, "identify_system")) { p->i++; return mknode(N_IDENT_SYS, NULL); }
    if (at(p, "show") ) { p->i++; return mknode(N_SHOW_REPL, NULL); }
    if (at(p, "timeline_history")) { while (!at_kind(p,T_EOF)) p->i++; return mknode(N_TIMELINE_HISTORY, NULL); }
    if (at(p, "create_replication_slot") || at(p, "drop_replication_slot") ||
        at(p, "start_replication") || at(p, "base_backup") || at(p, "read_replication_slot") ||
        at(p, "upload_manifest")) {
        NodeTag tg = at(p,"create_replication_slot") ? N_CREATE_REPL_SLOT :
                     at(p,"drop_replication_slot") ? N_DROP_REPL_SLOT :
                     at(p,"start_replication") ? N_START_REPL :
                     at(p,"read_replication_slot") ? N_READ_REPL_SLOT :
                     at(p,"upload_manifest") ? N_UPLOAD_MANIFEST : N_BASE_BACKUP;
        ReplStmt *st = xmalloc(sizeof *st); memset(st, 0, sizeof *st);
        p->i++;
        if (tg == N_CREATE_REPL_SLOT || tg == N_DROP_REPL_SLOT || tg == N_START_REPL ||
            tg == N_READ_REPL_SLOT) {
            if (accept_kw(p, "slot")) st->slot = ident_any(p);
            else if (at_ident(p)) st->slot = ident_any(p);
        }
        while (!at_kind(p, T_EOF) && !at_kind(p, T_SEMI)) {
            if (at(p, "logical")) { st->logical = true; p->i++;
                if (at_ident(p) || CUR(p).kind == T_STRING) { st->plugin = xstrdup(CUR(p).sval); p->i++; }
                continue; }
            if (at(p, "temporary")) { st->temporary = true; p->i++; continue; }
            if (at(p, "failover")) { st->failover = true; p->i++; continue; }
            if (CUR(p).kind == T_NUMBER || CUR(p).kind == T_IDENT) {
                const char *sv = CUR(p).sval;
                if (sv && strchr(sv, '/')) st->lsn = xstrdup(sv);
                spush(&st->opts, &st->nopts, xstrdup(sv ? sv : ""));
            }
            p->i++;
        }
        return mknode(tg, st);
    }
    syntax_error(p);
}

/* Resolve a type name written as text ("numeric(4,2)", "int[]", "mydomain")
 * the way the grammar would.  Returns false when the text is not a type name
 * this server knows; *oid is 0 when the name parses but no such type exists. */
bool parse_type_from_text(Session *s, const char *txt, Oid *oid, int32 *typmod,
                          char **canonical)
{
    P p; memset(&p, 0, sizeof p);
    p.s = s; p.src = txt;
    lex_init(&p.L, s, txt);
    lex_all(&p.L);
    p.i = 0;
    if (at_kind(&p, T_EOF)) return false;
    Oid t = 0; int32 tm = -1;
    char *nm = type_name_text(&p, &t, &tm);
    if (!at_kind(&p, T_EOF) && !at_kind(&p, T_SEMI)) { free(nm); return false; }
    *oid = t; *typmod = tm;
    if (canonical) *canonical = nm; else free(nm);
    return true;
}

Node *parse_sql(Session *s, const char *sql, const char **endptr)
{
    P p; memset(&p, 0, sizeof p);
    p.s = s; p.src = sql;
    lex_init(&p.L, s, sql);
    lex_all(&p.L);
    p.i = 0;
    while (at_kind(&p, T_SEMI)) p.i++;
    if (at_kind(&p, T_EOF)) { if (endptr) *endptr = NULL; return mknode(N_EMPTY, NULL); }
    int start = p.i;
    Node *n = parse_stmt(&p);
    if (!at_kind(&p, T_SEMI) && !at_kind(&p, T_EOF)) syntax_error(&p);
    n->sql = expr_src(&p, start, p.i);
    if (endptr) {
        while (at_kind(&p, T_SEMI)) p.i++;
        *endptr = at_kind(&p, T_EOF) ? NULL : CUR(&p).start;
    }
    return n;
}

/* Parse a bare expression, for the deparser.  Returns NULL if it does not
 * consume the whole fragment. */
Expr *parse_expression_text(Session *s, const char *src)
{
    P p; memset(&p, 0, sizeof p);
    p.s = s; p.src = src;
    lex_init(&p.L, s, src);
    lex_all(&p.L);
    p.i = 0;
    if (at_kind(&p, T_EOF)) return NULL;
    Expr *e = parse_expr(&p);
    if (!at_kind(&p, T_EOF) && !at_kind(&p, T_SEMI)) return NULL;
    return e;
}
