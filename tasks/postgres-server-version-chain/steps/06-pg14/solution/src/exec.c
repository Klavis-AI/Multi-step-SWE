/* exec.c - analysis and execution of parsed statements. */
#include "engine.h"
#include "stmt.h"

typedef struct ColMeta { char *rel; char *name; Oid type; int32 typmod; bool hidden; } ColMeta;

typedef struct RowSet {
    int ncols;
    ColMeta *cols;
    int nrows, cap;
    Datum **rows;
} RowSet;

typedef struct CteRt { char *name; RowSet *rs; int materialized; Node *query; CteDef *def;
                       /* set when PostgreSQL 12 would fold the WITH query into the
                        * main query instead of materializing it once */
                       bool inlined; struct ExecCtx *defctx; SelectStmt *inlsel; } CteRt;

typedef struct ExecCtx {
    Session *s;
    RowSet  *rs;
    Datum   *row;
    struct ExecCtx *outer;
    Datum   *params; int nparams;
    CteRt   *ctes; int nctes;
    /* aggregate evaluation */
    Datum   *aggvals; int naggvals;
    Expr   **aggexprs; int naggexprs;
    int      agg_phase;      /* 0 none, 1 collecting, 2 substituting */
    RowSet  *group_rows;     /* rows of the current group */
    int     *group_idx; int group_n;
    /* MERGE / RETURNING old-new */
    Datum   *oldrow, *newrow;
    RowSet  *oldnew_rs;
    int      merge_action;      /* 0 none, 1 INSERT, 2 UPDATE, 3 DELETE */
} ExecCtx;

static RowSet *rs_new(void)
{
    RowSet *r = xmalloc(sizeof *r);
    memset(r, 0, sizeof *r);
    return r;
}
static void rs_addcol_meta(RowSet *r, ColMeta m)
{
    r->cols = xrealloc(r->cols, (size_t)(r->ncols + 1) * sizeof(ColMeta));
    r->cols[r->ncols] = m;
    r->cols[r->ncols].rel = m.rel ? xstrdup(m.rel) : NULL;
    r->cols[r->ncols].name = xstrdup(m.name);
    r->ncols++;
}

static void rs_addcol(RowSet *r, const char *rel, const char *name, Oid type, int32 typmod)
{
    r->cols = xrealloc(r->cols, (size_t)(r->ncols + 1) * sizeof(ColMeta));
    r->cols[r->ncols].rel = rel ? xstrdup(rel) : NULL;
    r->cols[r->ncols].name = xstrdup(name);
    r->cols[r->ncols].type = type;
    r->cols[r->ncols].typmod = typmod;
    r->cols[r->ncols].hidden = false;
    r->ncols++;
}
static void rs_addrow(RowSet *r, Datum *vals)
{
    if (r->nrows == r->cap) { r->cap = r->cap ? r->cap * 2 : 16;
                              r->rows = xrealloc(r->rows, (size_t)r->cap * sizeof(Datum *)); }
    r->rows[r->nrows++] = vals;
}
/* The largest set a set-returning function will materialise.  PostgreSQL
 * streams these, so a very large generate_series runs for a long time there;
 * the cap keeps memory bounded without making the query finish instantly. */
#define PGVC_SRF_MAX 8000000

/* A CancelRequest from another connection sets a flag; the loops that can run
 * long poll it and raise PostgreSQL's query_canceled. */
void check_cancel(Session *s)
{
    if (s && s->cancel_pending) {
        s->cancel_pending = 0;
        ereport(s, "57014", "canceling statement due to user request");
    }
}

static Datum *row_new(int n)
{
    Datum *d = xmalloc((size_t)(n ? n : 1) * sizeof(Datum));
    for (int i = 0; i < n; i++) d[i] = d_null(TEXTOID);
    return d;
}

/* Lets support code evaluate an expression in the executor's context. */
Datum eval_thunk(void *ctx, Expr *e);

static Datum eval(ExecCtx *c, Expr *e);
static ArrayVal *mk_array(Oid elem, Datum *vals, int n);
static RowSet *exec_select(ExecCtx *c, SelectStmt *sel);
static QueryResult *rs_to_qr(Session *s, RowSet *rs);

/* --------------------------------------------------------- column lookup */
static int find_col(RowSet *rs, const char *qual, const char *name, Session *s, int pos)
{
    int found = -1, nfound = 0;
    for (int i = 0; i < rs->ncols; i++) {
        if (strcmp(rs->cols[i].name, name)) continue;
        if (qual && (!rs->cols[i].rel || strcmp(rs->cols[i].rel, qual))) continue;
        if (found < 0) found = i;
        nfound++;
    }
    if (nfound > 1) {
        /* USING/joined columns merge; PostgreSQL reports ambiguity only for
         * genuinely distinct sources */
        for (int i = 0; i < rs->ncols; i++)
            if (!strcmp(rs->cols[i].name, name) && (!qual || (rs->cols[i].rel && !strcmp(rs->cols[i].rel, qual))))
                return i;
        ereport_pos(s, "42702", pos, "column reference \"%s\" is ambiguous", name);
    }
    return found;
}

/* Levenshtein distance, capped: PostgreSQL only suggests a column name that
 * is within MAX_FUZZY_DISTANCE (3) edits of the one that was not found. */
static int lev_dist(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la > 128 || lb > 128) return 99;
    int prev[129], curr[129];
    for (size_t j = 0; j <= lb; j++) prev[j] = (int)j;
    for (size_t i = 1; i <= la; i++) {
        curr[0] = (int)i;
        for (size_t j = 1; j <= lb; j++) {
            int del = prev[j] + 1, ins = curr[j-1] + 1;
            int sub = prev[j-1] + (a[i-1] != b[j-1] ? 1 : 0);
            int m = del < ins ? del : ins;
            curr[j] = m < sub ? m : sub;
        }
        memcpy(prev, curr, (lb + 1) * sizeof(int));
    }
    return prev[lb];
}

static void suggest_column(Session *s, ExecCtx *c, const char *name)
{
    const char *bestrel = NULL, *bestcol = NULL;
    int best = 4, ties = 0;
    for (ExecCtx *x = c; x; x = x->outer) {
        if (!x->rs) continue;
        for (int i = 0; i < x->rs->ncols; i++) {
            if (x->rs->cols[i].hidden) continue;
            int d = lev_dist(name, x->rs->cols[i].name);
            if (d < best) { best = d; bestrel = x->rs->cols[i].rel;
                            bestcol = x->rs->cols[i].name; ties = 1; }
            else if (d == best) ties++;
        }
    }
    if (bestcol && ties == 1) {
        if (bestrel) err_hint(s, "Perhaps you meant to reference the column \"%s.%s\".",
                              bestrel, bestcol);
        else err_hint(s, "Perhaps you meant to reference the column \"%s\".", bestcol);
    }
}

/* ------------------------------------------------------------ aggregates */
static bool expr_has_agg(Expr *e)
{
    if (!e) return false;
    if (e->tag == X_AGG) return true;
    if (e->tag == X_WINDOW) return false;
    if (e->tag == X_SUBLINK || e->tag == X_EXISTS) return false;
    for (int i = 0; i < e->nargs; i++) if (expr_has_agg(e->args[i])) return true;
    for (int i = 0; i < e->nwhen; i++)
        if (expr_has_agg(e->when_cond[i]) || expr_has_agg(e->when_res[i])) return true;
    if (expr_has_agg(e->case_arg) || expr_has_agg(e->case_else)) return true;
    if (expr_has_agg(e->testexpr) || expr_has_agg(e->filter)) return true;
    return false;
}
static bool expr_has_window(Expr *e)
{
    if (!e) return false;
    if (e->tag == X_WINDOW) return true;
    for (int i = 0; i < e->nargs; i++) if (expr_has_window(e->args[i])) return true;
    for (int i = 0; i < e->nwhen; i++)
        if (expr_has_window(e->when_cond[i]) || expr_has_window(e->when_res[i])) return true;
    if (expr_has_window(e->case_arg) || expr_has_window(e->case_else)) return true;
    return false;
}

/* --------------------------------------------------------------- helpers */
static bool datum_true(Datum d) { return !d.isnull && d.type == BOOLOID && d.v.i != 0; }

static Datum to_bool(Session *s, Datum d)
{
    if (d.isnull) return d_null(BOOLOID);
    if (d.type == BOOLOID) return d;
    return coerce_to(s, d, BOOLOID, -1, 1);
}

const char *expr_label(Expr *e);

static Datum eval_sublink(ExecCtx *c, Expr *e);

extern int64 seq_call(Session *s, const char *name, int which, int64 arg, bool called);

static Datum eval_args_func(ExecCtx *c, Expr *e)
{
    Datum args[16];
    int n = e->nargs > 16 ? 16 : e->nargs;
    for (int i = 0; i < n; i++) args[i] = eval(c, e->args[i]);
    if (!strcmp(e->name, "__field__")) {
        if (args[0].type == RECORDOID && args[0].v.rec) {
            RecordVal *r = args[0].v.rec;
            for (int i = 0; i < r->n; i++)
                if (r->names && r->names[i] && !strcmp(r->names[i], e->colname)) return r->vals[i];
#if PGVC_VN >= 120
            /* PostgreSQL 12 lets an anonymous record's fields be reached as
             * f1, f2, ...; earlier releases refuse. */
            if (e->colname[0] == 'f' && isdigit((unsigned char)e->colname[1])) {
                char *end;
                long fno = strtol(e->colname + 1, &end, 10);
                if (!*end && fno >= 1 && fno <= r->n) return r->vals[fno - 1];
            }
#endif
        }
        ereport_pos(c->s, "42703", e->args[0]->pos,
                    "could not identify column \"%s\" in record data type", e->colname);
    }
    if (!strcmp(e->name, "generate_series") && n >= 2) {
        int64 a = args[0].isnull ? 0 : coerce_to(c->s, args[0], INT8OID, -1, 1).v.i;
        int64 b2 = args[1].isnull ? 0 : coerce_to(c->s, args[1], INT8OID, -1, 1).v.i;
        int64 st = (n > 2 && !args[2].isnull) ? coerce_to(c->s, args[2], INT8OID, -1, 1).v.i : 1;
        Oid et = (args[0].type == INT8OID || args[1].type == INT8OID) ? INT8OID : INT4OID;
        if (args[0].isnull || args[1].isnull) return d_null(array_type_of(et));
        if (st == 0) ereport(c->s, "22023", "step size cannot equal zero");
        int cnt = 0;
        Datum *vals = NULL;
        for (int64 x = a; st > 0 ? x <= b2 : x >= b2; x += st) {
            vals = xrealloc(vals, (size_t)(cnt + 1) * sizeof(Datum));
            vals[cnt++] = d_int(et, x);
            if ((cnt & 0x3ff) == 0) check_cancel(c->s);
            if (cnt > PGVC_SRF_MAX) break;
        }
        Datum r; memset(&r, 0, sizeof r);
        r.type = array_type_of(et); r.v.arr = mk_array(et, vals, cnt);
        return r;
    }
#if PGVC_VN >= 140
    if (!strcmp(e->name, "pg_column_compression") && n == 1) {
        if (args[0].isnull) return d_null(TEXTOID);
        int32 blen = 0;
        Oid at = args[0].type;
        if (at == TEXTOID || at == VARCHAROID || at == BPCHAROID || at == BYTEAOID ||
            at == UNKNOWNOID || at == JSONOID || at == XMLOID)
            blen = args[0].v.s.len;
        else if (at == JSONBOID) { char *x = out_func(c->s, args[0]);
                                   blen = (int32)strlen(x); free(x); }
        /* only a value large enough to be toasted carries a method */
        if (blen < 2048) return d_null(TEXTOID);
        char m = 0;
        Expr *a0 = e->args[0];
        if (a0->tag == X_COLREF && c->rs) {
            int ci = find_col(c->rs, a0->qual, a0->colname, c->s, 0);
            if (ci >= 0 && c->rs->cols[ci].rel) {
                Table *bt = tbl_find(c->s->db, NULL, c->rs->cols[ci].rel);
                if (bt) { int k = col_index(bt, a0->colname);
                          if (k >= 0) m = bt->cols[k].compression; }
            }
        }
        if (!m) {
            const char *d = guc_get(c->s, "default_toast_compression");
            m = (d && !strcasecmp(d, "lz4")) ? 'l' : 'p';
        }
        return d_text(TEXTOID, m == 'l' ? "lz4" : "pglz");
    }
#endif
#if PGVC_VN >= 140
    /* extract() became one polymorphic function at 14, so an argument that is
     * not a date/time value no longer resolves */
    if (!strcmp(e->name, "extract") && n == 2) {
        Oid vt = args[1].type;
        if (vt == UNKNOWNOID) {
            err_hint(c->s, "Could not choose a best candidate function. "
                           "You might need to add explicit type casts.");
            ereport_pos(c->s, "42725", e->pos,
                        "function pg_catalog.extract(unknown, unknown) is not unique");
        }
        if (vt != DATEOID && vt != TIMEOID && vt != TIMETZOID && vt != TIMESTAMPOID &&
            vt != TIMESTAMPTZOID && vt != INTERVALOID) {
            err_hint(c->s, "No function matches the given name and argument types. "
                           "You might need to add explicit type casts.");
            ereport_pos(c->s, "42883", e->pos,
                        "function pg_catalog.extract(unknown, %s) does not exist",
                        format_type_str(vt, -1));
        }
    }
#endif
    if (!strcmp(e->name, "nextval") || !strcmp(e->name, "currval") || !strcmp(e->name, "setval")) {
        if (!n || args[0].isnull) return d_null(INT8OID);
        char *nm = args[0].regname ? xstrdup(args[0].regname) : out_func(c->s, args[0]);
        char *bare = strrchr(nm, '.');
        if (bare) { char *q = xstrdup(bare + 1); free(nm); nm = q; }
        int which = (e->name[0] == 'n') ? 0 : (e->name[0] == 'c') ? 1 : 2;
        int64 arg = (n > 1 && !args[1].isnull) ? coerce_to(c->s, args[1], INT8OID, -1, 1).v.i : 0;
        bool called = (n > 2) ? (!args[2].isnull && args[2].v.i != 0) : true;
        int64 r = seq_call(c->s, nm, which, arg, called);
        free(nm);
        return d_int(INT8OID, r);
    }
    /* PostgreSQL resolves an unknown-typed literal against its sibling during
     * parse analysis, and reports input errors at the literal's position. */
    if (n == 2 && !isalpha((unsigned char)e->name[0]) && e->name[0] != '_' &&
        strcmp(e->name, "||") && strcmp(e->name, "~") && strcmp(e->name, "!~") &&
        strcmp(e->name, "~*") && strcmp(e->name, "!~*")) {
        for (int i = 0; i < 2; i++) {
            if (args[i].type != UNKNOWNOID || args[1-i].type == UNKNOWNOID) continue;
            Oid other = args[1-i].type;
            if (other == TEXTOID || other == VARCHAROID || other == BPCHAROID ||
                other == NAMEOID || other == JSONBOID || other == JSONOID ||
                type_is_array(other)) continue;
            c->s->err.position = e->args[i]->pos;
            args[i] = coerce_to(c->s, args[i], other, -1, 1);
            c->s->err.position = 0;
        }
    }
#if PGVC_VN >= 120
    /* The jsonb_path_* family and the @? and @@ operators take a jsonpath and
     * a jsonb "vars" object; PostgreSQL resolves those literals during parse
     * analysis and reports their input errors at the literal's position. */
    {
        static const Oid jpargs[4] = { JSONBOID, JSONPATHOID, JSONBOID, BOOLOID };
        int lo = -1, hi = 0;
        if (n >= 2 && !strncasecmp(e->name, "jsonb_path_", 11)) {
            lo = 0; hi = n < 4 ? n : 4;
        } else if (n == 2 && args[0].type == JSONBOID &&
                   (!strcmp(e->name, "@?") || !strcmp(e->name, "@@"))) {
            lo = 1; hi = 2;
        }
        for (int i = lo; i >= 0 && i < hi; i++) {
            if (args[i].type != UNKNOWNOID) continue;
            c->s->err.position = e->args[i]->pos;
            args[i] = coerce_to(c->s, args[i], jpargs[i], -1, 1);
            c->s->err.position = 0;
        }
    }
#endif
    bool found = false;
    int save_oppos = c->s->oppos;
    if (!isalpha((unsigned char)e->name[0]) && e->name[0] != '_') c->s->oppos = e->pos;
    /* a jsonpath syntax error is reported at the path literal */
    if (n >= 2 && !strncmp(e->name, "jsonb_path_", 11))
        c->s->jsonpath_pos = e->args[1]->pos;
    c->s->funcargpos = n ? e->args[0]->pos : 0;
    for (int i = 0; i < n && i < 16; i++) c->s->argpos[i] = e->args[i]->pos;
    Datum r = call_builtin(c->s, e->name, args, n, &found);
    c->s->oppos = save_oppos;
    if (!found) {
        Buf b; buf_init(&b);
        for (int i = 0; i < n; i++) {
            if (i) buf_adds(&b, ", ");
            /* an untyped literal has no type yet, and PostgreSQL says so */
            if (args[i].type == UNKNOWNOID) buf_adds(&b, "unknown");
            else buf_adds(&b, format_type_str(args[i].type, -1));
        }
        char *sig = buf_steal(&b);
        if (!isalpha((unsigned char)e->name[0]) && e->name[0] != '_') {
            const char *op = e->name;
            if (!strcmp(op, "!post")) op = "!";
            err_hint(c->s, n == 1
                     ? "No operator matches the given name and argument type. "
                       "You might need to add an explicit type cast."
                     : "No operator matches the given name and argument types. "
                       "You might need to add explicit type casts.");
            if (n == 2)
                ereport_pos(c->s, "42883", e->pos, "operator does not exist: %s %s %s",
                            args[0].type == UNKNOWNOID ? "unknown"
                                : format_type_str(args[0].type, -1),
                            op,
                            args[1].type == UNKNOWNOID ? "unknown"
                                : format_type_str(args[1].type, -1));
            ereport_pos(c->s, "42883", e->pos, "operator does not exist: %s %s", op, sig);
        }
        err_hint(c->s, "No function matches the given name and argument types. "
                       "You might need to add explicit type casts.");
        ereport_pos(c->s, "42883", e->pos, "function %s%s(%s) does not exist",
                    e->grammar_func ? "pg_catalog." : "", e->name, sig);
    }
    return r;
}

/* SQL-language user functions are expanded by running their body. */
static SqlRoutine *routine_find(Database *db, const char *schema, const char *name, int nargs)
{
    for (int i = 0; i < db->nroutines; i++) {
        SqlRoutine *p = db->routines[i];
        if (strcmp(p->name, name)) continue;
        if (schema && strcmp(p->schema, schema)) continue;
        if (nargs >= 0 && p->nargs != nargs) continue;
        return p;
    }
    return NULL;
}

/* PostgreSQL's spelling of an argument type list, for the error messages. */
char *routine_sig(Session *s, const Oid *types, int n, bool internal)
{
    (void)s;
    Buf b; buf_init(&b);
    for (int i = 0; i < n; i++) {
        if (i) buf_adds(&b, ", ");
        Oid t = types[i];
        if (!internal && (t == UNKNOWNOID || !t)) { buf_adds(&b, "unknown"); continue; }
        if (t == UNKNOWNOID || !t) t = TEXTOID;
        if (internal) buf_printf(&b, "pg_catalog.%s", type_name(t));
        else { char *ft = format_type_str(t, -1); buf_adds(&b, ft); free(ft); }
    }
    return buf_steal(&b);
}

/* Pick the overload whose argument types fit best. */
SqlRoutine *routine_resolve(Database *db, const char *schema, const char *name,
                            const Oid *types, int n, char kind)
{
    SqlRoutine *best = NULL;
    int bestscore = -1;
    for (int i = 0; i < db->nroutines; i++) {
        SqlRoutine *p = db->routines[i];
        if (strcmp(p->name, name)) continue;
        if (schema && strcmp(p->schema, schema)) continue;
        if (kind && p->prokind != kind) continue;
        int want = 0;
        for (int k = 0; k < p->nargs; k++)
            if (!p->argmodes || p->argmodes[k] != 'o') want++;  /* OUT args are not passed */
#if PGVC_VN >= 140
        /* a CALL supplies a placeholder for every OUT argument too */
        if (kind == 'p') { if (p->nargs != n) continue; }
        else if (want != n && p->nargs != n) continue;
#else
        if (want != n && p->nargs != n) continue;
#endif
        int score = 0;
        bool ok = true;
        for (int k = 0; k < n && k < p->nargs && ok; k++) {
            Oid a = types[k], f = p->argtypes[k];
            if (a == f) score += 4;
            else if (a == UNKNOWNOID || !a) score += (f == TEXTOID) ? 3 : 2;
            else if ((a == INT2OID || a == INT4OID) && f == INT8OID) score += 1;
            else if (a == INT2OID && f == INT4OID) score += 1;
            else ok = false;
        }
        if (!ok) continue;
        if (score > bestscore) { bestscore = score; best = p; }
    }
    return best;
}

static Datum eval_user_func(ExecCtx *c, SqlRoutine *p, Expr *e)
{
    /* Substitute the arguments textually as $n parameters and run the body. */
    Datum *args = xmalloc((size_t)(e->nargs ? e->nargs : 1) * sizeof(Datum));
    for (int i = 0; i < e->nargs; i++) {
        args[i] = eval(c, e->args[i]);
        if (i < p->nargs) args[i] = coerce_to(c->s, args[i], p->argtypes[i], -1, 1);
    }
    if (p->lang && !strcasecmp(p->lang, "plpgsql")) {
        Datum r = plpgsql_run(c->s, p, args, e->nargs, NULL, NULL, NULL);
        if (p->rettype && p->rettype != RECORDOID && p->rettype != VOIDOID && !r.isnull)
            r = coerce_to(c->s, r, p->rettype, -1, 1);
        return r;
    }
    Buf body; buf_init(&body);
    buf_adds(&body, p->body);
    char *src = buf_steal(&body);
    /* replace named parameters with $n so the parser sees positional params */
    Buf out; buf_init(&out);
    for (char *q = src; *q; ) {
        bool sub = false;
        if (isalpha((unsigned char)*q) || *q == '_') {
            char *st = q;
            while (isalnum((unsigned char)*q) || *q == '_') q++;
            char *id = xstrndup(st, (size_t)(q - st));
            char *lo = lower_ident(id);
            for (int i = 0; i < p->nargs; i++)
                if (p->argnames && p->argnames[i] && *p->argnames[i] && !strcmp(p->argnames[i], lo)) {
                    buf_printf(&out, "$%d", i + 1); sub = true; break;
                }
            if (!sub) buf_adds(&out, id);
            free(id); free(lo);
            continue;
        }
        buf_addc(&out, *q); q++;
    }
    free(src);
    char *text = buf_steal(&out);
    Session *s = c->s;
    Node *n = parse_sql(s, text, NULL);
    ExecCtx sub; memset(&sub, 0, sizeof sub);
    sub.s = s; sub.params = args; sub.nparams = e->nargs;
    Datum result = d_null(p->rettype);
    if (n->tag == N_SELECT) {
        RowSet *rs = exec_select(&sub, n->select);
        if (rs->nrows > 0 && rs->ncols > 0) result = rs->rows[0][0];
    }
    free(text);
    if (p->rettype && p->rettype != RECORDOID && !result.isnull)
        result = coerce_to(s, result, p->rettype, -1, 1);
    return result;
}

/* ------------------------------------------------------------- eval */
static Datum eval_case(ExecCtx *c, Expr *e)
{
    Datum arg; bool have_arg = false;
    if (e->case_arg) { arg = eval(c, e->case_arg); have_arg = true; }
    for (int i = 0; i < e->nwhen; i++) {
        bool hit;
        if (have_arg) {
            Datum w = eval(c, e->when_cond[i]);
            hit = !arg.isnull && !w.isnull && d_cmp(c->s, arg, w) == 0;
        } else hit = datum_true(to_bool(c->s, eval(c, e->when_cond[i])));
        if (hit) return eval(c, e->when_res[i]);
    }
    if (e->case_else) return eval(c, e->case_else);
    for (int i = 0; i < e->nwhen; i++)
        if (e->when_res[i]->tag == X_CONST && e->when_res[i]->val.type)
            return d_null(e->when_res[i]->val.type);
    return d_null(TEXTOID);
}

static bool like_match(const char *s, int slen, const char *p, int plen, char esc, bool ci);

static Datum eval_like(ExecCtx *c, Expr *e)
{
    Datum a = eval(c, e->args[0]), b = eval(c, e->args[1]);
    if (a.isnull || b.isnull) return d_null(BOOLOID);
    a = coerce_to(c->s, a, TEXTOID, -1, 2);
    b = coerce_to(c->s, b, TEXTOID, -1, 2);
    char esc = '\\';
    if (e->like_escape) {
        Datum x = coerce_to(c->s, eval(c, e->like_escape), TEXTOID, -1, 2);
        esc = (!x.isnull && x.v.s.len > 0) ? x.v.s.p[0] : '\0';
    }
    bool m;
    if (e->like_similar) {
        /* SIMILAR TO is a regular expression with SQL wildcards */
        Buf re; buf_init(&re);
        buf_addc(&re, '^');
        for (int i = 0; i < b.v.s.len; i++) {
            char ch = b.v.s.p[i];
            if (ch == '%') buf_adds(&re, ".*");
            else if (ch == '_') buf_addc(&re, '.');
            else if (strchr(".^$", ch)) { buf_addc(&re, '\\'); buf_addc(&re, ch); }
            else buf_addc(&re, ch);
        }
        buf_addc(&re, '$');
        Datum ar[2]; ar[0] = a; ar[1] = d_text(TEXTOID, re.data);
        bool f = false;
        Datum rr = call_builtin(c->s, "~", ar, 2, &f);
        buf_free(&re);
        m = datum_true(rr);
    } else m = like_match(a.v.s.p, a.v.s.len, b.v.s.p, b.v.s.len, esc, e->like_ilike);
    return d_bool(e->like_not ? !m : m);
}

static bool like_match(const char *s, int slen, const char *p, int plen, char esc, bool ci)
{
    int si = 0, pi = 0, star_p = -1, star_s = 0;
    while (si < slen) {
        if (pi < plen) {
            char pc = p[pi];
            if (esc && pc == esc && pi + 1 < plen) {
                char lit = p[pi+1];
                char sc = s[si];
                if (ci) { lit = (char)tolower((unsigned char)lit); sc = (char)tolower((unsigned char)sc); }
                if (lit == sc) { pi += 2; si++; continue; }
            } else if (pc == '%') { star_p = pi++; star_s = si; continue; }
            else if (pc == '_') { pi++; si++; continue; }
            else {
                char sc = s[si], cc = pc;
                if (ci) { sc = (char)tolower((unsigned char)sc); cc = (char)tolower((unsigned char)cc); }
                if (sc == cc) { pi++; si++; continue; }
            }
        }
        if (star_p >= 0) { pi = star_p + 1; si = ++star_s; continue; }
        return false;
    }
    while (pi < plen && p[pi] == '%') pi++;
    return pi == plen;
}

static ArrayVal *mk_array(Oid elem, Datum *vals, int n)
{
    ArrayVal *a = xmalloc(sizeof *a);
    memset(a, 0, sizeof *a);
    a->elemtype = elem; a->ndim = n ? 1 : 0; a->dims[0] = n; a->lbound[0] = 1;
    a->nelem = n; a->elems = vals;
    return a;
}

static Oid common_type(Session *s, Oid a, Oid b)
{
    if (a == b) return a;
    if (a == UNKNOWNOID) return b;
    if (b == UNKNOWNOID) return a;
    if (a == 0) return b;
    if (b == 0) return a;
    static const Oid order[] = {BOOLOID, INT2OID, INT4OID, INT8OID, NUMERICOID, FLOAT4OID,
                                FLOAT8OID, 0};
    int ia = -1, ib = -1;
    for (int i = 0; order[i]; i++) { if (order[i] == a) ia = i; if (order[i] == b) ib = i; }
    if (ia >= 0 && ib >= 0) return order[ia > ib ? ia : ib];
    if (a == TEXTOID || b == TEXTOID) return TEXTOID;
    if ((a == VARCHAROID && b == BPCHAROID) || (a == BPCHAROID && b == VARCHAROID)) return TEXTOID;
    if (a == DATEOID && (b == TIMESTAMPOID || b == TIMESTAMPTZOID)) return b;
    if (b == DATEOID && (a == TIMESTAMPOID || a == TIMESTAMPTZOID)) return a;
    if (a == JSONBOID || b == JSONBOID) return JSONBOID;
    (void)s;
    return a;
}

static Datum eval(ExecCtx *c, Expr *e)
{
    Session *s = c->s;
    if (c->agg_phase == 2)
        for (int i = 0; i < c->naggexprs; i++)
            if (c->aggexprs[i] == e) return c->aggvals[i];
    switch (e->tag) {
    case X_CONST: {
        if (e->isnull_const) return d_null(UNKNOWNOID);
        return e->val;
    }
    case X_PARAM: {
        if (!c->params || e->paramno < 1 || e->paramno > c->nparams)
            ereport_pos(s, "42P02", e->pos, "there is no parameter $%d", e->paramno);
        return c->params[e->paramno - 1];
    }
    case X_COLREF: {
        for (ExecCtx *x = c; x; x = x->outer) {
            if (!x->rs || !x->row) continue;
            int i = find_col(x->rs, e->qual, e->colname, s, e->pos);
            if (i >= 0) return x->row[i];
        }
        /* a qualifier that names no relation at all is a missing FROM entry */
        if (e->qual) {
            bool relseen = false;
            for (ExecCtx *x = c; x && !relseen; x = x->outer) {
                if (!x->rs) continue;
                for (int i = 0; i < x->rs->ncols; i++)
                    if (x->rs->cols[i].rel && !strcmp(x->rs->cols[i].rel, e->qual))
                        { relseen = true; break; }
            }
            if (!relseen)
                ereport_pos(s, "42P01", e->pos,
                            "missing FROM-clause entry for table \"%s\"", e->qual);
        }
        /* PostgreSQL offers the closest column name it can find */
        suggest_column(s, c, e->colname);
        if (e->qual)
            ereport_pos(s, "42703", e->pos, "column %s.%s does not exist", e->qual, e->colname);
        ereport_pos(s, "42703", e->pos, "column \"%s\" does not exist", e->colname);
    }
    case X_OLDNEW: {
        if (!c->oldnew_rs)
            ereport_pos(s, "42P01", e->pos, "missing FROM-clause entry for table \"%s\"",
                        e->oldnew == 1 ? "old" : "new");
        int i = find_col(c->oldnew_rs, NULL, e->colname, s, e->pos);
        if (i < 0) ereport_pos(s, "42703", e->pos, "column %s.%s does not exist",
                               e->oldnew == 1 ? "old" : "new", e->colname);
        Datum *src = (e->oldnew == 1) ? c->oldrow : c->newrow;
        if (!src) return d_null(c->oldnew_rs->cols[i].type);
        return src[i];
    }
    case X_AND: {
        Datum a = to_bool(s, eval(c, e->args[0]));
        if (!a.isnull && !a.v.i) return d_bool(false);
        Datum b = to_bool(s, eval(c, e->args[1]));
        if (!b.isnull && !b.v.i) return d_bool(false);
        if (a.isnull || b.isnull) return d_null(BOOLOID);
        return d_bool(true);
    }
    case X_OR: {
        Datum a = to_bool(s, eval(c, e->args[0]));
        if (!a.isnull && a.v.i) return d_bool(true);
        Datum b = to_bool(s, eval(c, e->args[1]));
        if (!b.isnull && b.v.i) return d_bool(true);
        if (a.isnull || b.isnull) return d_null(BOOLOID);
        return d_bool(false);
    }
    case X_NOT: {
        Datum a = to_bool(s, eval(c, e->args[0]));
        if (a.isnull) return d_null(BOOLOID);
        return d_bool(!a.v.i);
    }
    case X_IS: {
        Datum a = eval(c, e->args[0]);
        switch (e->is_kind) {
        case 0: return d_bool(a.isnull);
        case 1: return d_bool(!a.isnull);
        case 2: { Datum b = to_bool(s, a); return d_bool(!b.isnull && b.v.i); }
        case 3: { Datum b = to_bool(s, a); return d_bool(b.isnull || !b.v.i); }
        case 4: { Datum b = to_bool(s, a); return d_bool(!b.isnull && !b.v.i); }
        case 5: { Datum b = to_bool(s, a); return d_bool(b.isnull || b.v.i); }
        case 6: return d_bool(a.isnull);
        case 7: return d_bool(!a.isnull);
        case 8: case 9: {
            Datum b = eval(c, e->args[1]);
            bool dist = (a.isnull != b.isnull) || (!a.isnull && d_cmp(s, a, b) != 0);
            return d_bool(e->is_kind == 8 ? dist : !dist);
        }
        }
        return d_null(BOOLOID);
    }
    case X_BETWEEN: {
        Datum v = eval(c, e->args[0]), lo = eval(c, e->args[1]), hi = eval(c, e->args[2]);
        if (v.isnull || lo.isnull || hi.isnull) return d_null(BOOLOID);
        int a = d_cmp(s, v, lo), b = d_cmp(s, v, hi);
        bool m = (a >= 0 && b <= 0);
        if (e->between_sym && !m) m = (d_cmp(s, v, hi) >= 0 && d_cmp(s, v, lo) <= 0);
        return d_bool(e->between_not ? !m : m);
    }
    case X_IN: {
        Datum v = eval(c, e->testexpr);
        bool sawnull = false, hit = false;
        if (e->subquery) {
            ExecCtx sub; memset(&sub, 0, sizeof sub);
            sub.s = s; sub.outer = c; sub.params = c->params; sub.nparams = c->nparams;
            sub.ctes = c->ctes; sub.nctes = c->nctes;
            RowSet *rs = exec_select(&sub, e->subquery->select);
            for (int i = 0; i < rs->nrows; i++) {
                Datum d = rs->rows[i][0];
                if (v.isnull || d.isnull) { sawnull = true; continue; }
                Datum dd = (d.type != v.type) ? coerce_to(s, d, common_type(s, v.type, d.type), -1, 1) : d;
                Datum vv = (d.type != v.type) ? coerce_to(s, v, common_type(s, v.type, d.type), -1, 1) : v;
                if (d_cmp(s, vv, dd) == 0) { hit = true; break; }
            }
        } else {
            for (int i = 0; i < e->nargs; i++) {
                Datum d = eval(c, e->args[i]);
                if (v.isnull || d.isnull) { sawnull = true; continue; }
                Oid ct = common_type(s, v.type, d.type);
                if (d_cmp(s, coerce_to(s, v, ct, -1, 1), coerce_to(s, d, ct, -1, 1)) == 0) { hit = true; break; }
            }
        }
        if (hit) return d_bool(!e->between_not);
        if (sawnull) return d_null(BOOLOID);
        return d_bool(e->between_not);
    }
    case X_LIKE: return eval_like(c, e);
    case X_CAST: {
        Datum a = eval(c, e->args[0]);
        if (!e->casttype)
            ereport_pos(s, "42704", e->pos, "type \"%s\" does not exist", e->casttypename);
        s->err.position = e->args[0]->pos;
        int savecast = s->castpos;
        s->castpos = e->pos;
        Datum r = coerce_to(s, a, e->casttype, e->casttypmod, 2);
        s->castpos = savecast;
        s->err.position = 0;
        return r;
    }
    case X_COALESCE: {
        for (int i = 0; i < e->nargs; i++) { Datum d = eval(c, e->args[i]); if (!d.isnull) return d; }
        return d_null(TEXTOID);
    }
    case X_ARRAY: {
        Datum *vals = xmalloc((size_t)(e->nargs ? e->nargs : 1) * sizeof(Datum));
        Oid et = 0;
        for (int i = 0; i < e->nargs; i++) { vals[i] = eval(c, e->args[i]); et = common_type(s, et, vals[i].type); }
        if (!et) et = TEXTOID;
        if (et == UNKNOWNOID) et = TEXTOID;
        /* array of arrays: flatten one level, PostgreSQL-style */
        if (e->nargs && type_is_array(vals[0].type)) {
            ArrayVal *first = vals[0].v.arr;
            ArrayVal *a = xmalloc(sizeof *a);
            memset(a, 0, sizeof *a);
            a->elemtype = first->elemtype;
            a->ndim = first->ndim + 1;
            a->dims[0] = e->nargs; a->lbound[0] = 1;
            for (int k = 0; k < first->ndim; k++) { a->dims[k+1] = first->dims[k]; a->lbound[k+1] = 1; }
            for (int i = 0; i < e->nargs; i++) {
                ArrayVal *x = vals[i].v.arr;
                a->elems = xrealloc(a->elems, (size_t)(a->nelem + x->nelem) * sizeof(Datum));
                for (int k = 0; k < x->nelem; k++) a->elems[a->nelem++] = x->elems[k];
            }
            Datum r; memset(&r, 0, sizeof r);
            r.type = array_type_of(a->elemtype); r.v.arr = a;
            return r;
        }
        for (int i = 0; i < e->nargs; i++) vals[i] = coerce_to(s, vals[i], et, -1, 1);
        Datum r; memset(&r, 0, sizeof r);
        r.type = array_type_of(et); r.v.arr = mk_array(et, vals, e->nargs);
        if (!r.type) r.type = TEXTARRAYOID;
        return r;
    }
    case X_ROW: {
        RecordVal *rv = xmalloc(sizeof *rv);
        rv->n = e->nargs;
        rv->vals = xmalloc((size_t)(e->nargs ? e->nargs : 1) * sizeof(Datum));
        rv->names = NULL;
        for (int i = 0; i < e->nargs; i++) rv->vals[i] = eval(c, e->args[i]);
        Datum r; memset(&r, 0, sizeof r);
        r.type = RECORDOID; r.v.rec = rv;
        return r;
    }
    case X_SUBSCRIPT: {
        Datum base = eval(c, e->args[0]);
        if (e->is_kind == 2) {   /* assignment form, handled by UPDATE */
            return eval(c, e->args[1]);
        }
        if (base.isnull) return d_null(base.type);
        if (base.type == JSONBOID) {
            Datum k = eval(c, e->args[1]);
            JsonVal *v = NULL;
            if (!k.isnull) {
                /* an integer subscript indexes an array but names a key in an
                 * object, and a text subscript only ever names a key */
                if (json_kind(base.v.jb) == 4 &&
                    (k.type == INT4OID || k.type == INT8OID))
                    v = json_get_idx(base.v.jb, (int)k.v.i);
                else if (json_kind(base.v.jb) == 5) {
                    char *t = out_func(s, k); v = json_get_key(base.v.jb, t); free(t);
                } else if (k.type == INT4OID || k.type == INT8OID)
                    v = json_get_idx(base.v.jb, (int)k.v.i);
            }
            if (!v) return d_null(JSONBOID);
            Datum r; memset(&r, 0, sizeof r);
            r.type = JSONBOID; r.v.jb = v;
            return r;
        }
        if (!type_is_array(base.type)) {
#if PGVC_VN >= 140
            if (e->is_kind == 0) {
                /* PostgreSQL reports the innermost expression of the base */
                Expr *inner = e->args[0];
                while (inner->tag == X_CAST && inner->nargs) inner = inner->args[0];
                ereport_pos(s, "42804", inner->pos,
                            "cannot subscript type %s because it does not support subscripting",
                            format_type_str(base.type == UNKNOWNOID ? TEXTOID : base.type, -1));
            }
#endif
            return d_null(base.type);
        }
        ArrayVal *a = base.v.arr;
        if (e->is_kind == 1) {
            Datum lo = eval(c, e->args[1]), hi = eval(c, e->args[2]);
            int l = lo.isnull ? 1 : (int)lo.v.i, h = hi.isnull ? a->nelem : (int)hi.v.i;
            if (l < 1) l = 1;
            if (h > a->nelem) h = a->nelem;
            int n = h - l + 1; if (n < 0) n = 0;
            Datum *vals = xmalloc((size_t)(n ? n : 1) * sizeof(Datum));
            for (int i = 0; i < n; i++) vals[i] = a->elems[l - 1 + i];
            Datum r; memset(&r, 0, sizeof r);
            r.type = base.type; r.v.arr = mk_array(a->elemtype, vals, n);
            return r;
        }
        Datum idx = eval(c, e->args[1]);
        if (idx.isnull) return d_null(a->elemtype);
        int i = (int)idx.v.i;
        if (i < 1 || i > a->nelem) return d_null(a->elemtype);
        return a->elems[i - 1];
    }
    case X_EXISTS: case X_SUBLINK: case X_ANYALL: return eval_sublink(c, e);
    case X_CASE: return eval_case(c, e);
    case X_ISJSON: {
        Datum a = eval(c, e->args[0]);
        Oid at = a.type;
        if (!(at == TEXTOID || at == VARCHAROID || at == BPCHAROID || at == CHAROID ||
              at == NAMEOID || at == UNKNOWNOID || at == BYTEAOID ||
              at == JSONOID || at == JSONBOID)) {
            char *tn = format_type_str(at, -1);
            ereport(s, "42804", "cannot use type %s in IS JSON predicate", tn);
        }
        if (a.isnull) return d_null(BOOLOID);
        const char *p; size_t n; char *own = NULL;
        if (at == BYTEAOID) { p = a.v.s.p; n = (size_t)a.v.s.len; }
        else { own = out_func(s, a); p = own; n = strlen(own); }
        const char *err = NULL;
        /* the predicate validates with json_in rules and reports repeated keys */
        bool dupkey = false;
        JsonVal *v = json_parse_keys(p, n, false, &err, &dupkey);
        bool ok = (v != NULL);
        if (ok && e->isjson_kind) {
            int k = json_kind(v);
            if (e->isjson_kind == 1) ok = (k == 4);
            else if (e->isjson_kind == 2) ok = (k == 5);
            else ok = (k >= 0 && k <= 3);
        }
        if (ok && e->isjson_unique) ok = !dupkey;
        free(own);
        return d_bool(e->isjson_not ? !ok : ok);
    }
    case X_GROUPING: return d_int(INT4OID, 0);
    case X_STAR: ereport_pos(s, "42601", e->pos, "syntax error at or near \"*\"");
    case X_WINDOW:
        ereport_pos(s, "42P20", e->pos, "window functions are not allowed in this context");
    case X_AGG: {
        if (c->agg_phase == 2) {
            for (int i = 0; i < c->naggexprs; i++)
                if (c->aggexprs[i] == e) return c->aggvals[i];
        }
        ereport_pos(s, "42803", e->pos, "aggregate functions are not allowed in this context");
    }
    case X_JSONCTOR: {
#if PGVC_VN >= 170
        if (e->json_ctor >= 7 && e->json_ctor <= 9) {
            Datum ci = eval(c, e->args[0]);
            Datum pa = eval(c, e->args[1]);
            return eval_json_query_fn(s, e, ci, pa, eval_thunk, c);
        }
#endif
        if (e->subquery) {
            /* JSON_ARRAY(query): aggregate the single column the query returns */
            ExecCtx sub; memset(&sub, 0, sizeof sub);
            sub.s = s; sub.outer = c; sub.params = c->params; sub.nparams = c->nparams;
            sub.ctes = c->ctes; sub.nctes = c->nctes;
            RowSet *rs2 = exec_select(&sub, e->subquery->select);
            if (rs2->ncols != 1)
                ereport_pos(s, "42601", e->pos, "subquery must return only one column");
            Datum *vv = xmalloc((size_t)(rs2->nrows ? rs2->nrows : 1) * sizeof(Datum));
            for (int i = 0; i < rs2->nrows; i++) vv[i] = rs2->rows[i][0];
            Expr tmp = *e;
            tmp.json_ctor = 2;                  /* render like JSON_ARRAYAGG */
            return eval_json_agg(s, &tmp, vv, NULL, rs2->nrows);
        }
        Datum *vals = xmalloc((size_t)(e->nargs ? e->nargs : 1) * sizeof(Datum));
        for (int i = 0; i < e->nargs; i++) vals[i] = eval(c, e->args[i]);
        return eval_json_ctor(s, e, vals, e->nargs);
    }
    case X_FUNC: {
#if PGVC_VN >= 170
        if (!strcmp(e->name, "merge_action") && e->nargs == 0) {
            if (!c->merge_action)
                ereport_pos(s, "0A000", e->pos, "MERGE_ACTION() can only be used in the "
                                                "RETURNING list of a MERGE command");
            return d_text(TEXTOID, c->merge_action == 1 ? "INSERT" :
                                   c->merge_action == 2 ? "UPDATE" : "DELETE");
        }
#endif
        if (e->nspname && strcmp(e->nspname, "pg_catalog") && strcmp(e->nspname, "public")) {
            SqlRoutine *p = routine_find(s->db, e->nspname, e->name, e->nargs);
            if (p) return eval_user_func(c, p, e);
        }
        SqlRoutine *p = routine_find(s->db, NULL, e->name, e->nargs);
        if (p && p->prokind == 'p') {
            /* PostgreSQL will not let a procedure be called in an expression */
            Oid *at = xmalloc((size_t)(e->nargs ? e->nargs : 1) * sizeof(Oid));
            for (int i = 0; i < e->nargs; i++) at[i] = eval(c, e->args[i]).type;
            char *sig = routine_sig(s, at, e->nargs, false);
            err_hint(s, "To call a procedure, use CALL.");
            ereport_pos(s, "42809", e->pos, "%s(%s) is a procedure", e->name, sig);
        }
        if (p && p->prokind == 'f') return eval_user_func(c, p, e);
        return eval_args_func(c, e);
    }
    default:
        ereport_pos(s, "0A000", e->pos, "unsupported expression");
    }
}

static Datum eval_sublink(ExecCtx *c, Expr *e)
{
    Session *s = c->s;
    if (e->tag == X_ANYALL && !e->subquery) {
        /* op ANY (array) */
        Datum l = eval(c, e->testexpr);
        Datum arr = eval(c, e->args[0]);
        if (arr.isnull) return d_null(BOOLOID);
        if (!type_is_array(arr.type)) {
            Datum ar[2]; ar[0] = l; ar[1] = arr;
            bool f = false;
            return call_builtin(s, e->name, ar, 2, &f);
        }
        ArrayVal *a = arr.v.arr;
        bool any = false, all = true, sawnull = false;
        for (int i = 0; i < a->nelem; i++) {
            Datum ar[2]; ar[0] = l; ar[1] = a->elems[i];
            bool f = false;
            Datum r = call_builtin(s, e->name, ar, 2, &f);
            if (!f) ereport_pos(s, "42883", e->pos, "operator does not exist");
            if (r.isnull) { sawnull = true; all = false; continue; }
            if (r.v.i) any = true; else all = false;
        }
        if (e->sublink_kind == 3) return any ? d_bool(true) : (sawnull ? d_null(BOOLOID) : d_bool(false));
        return (!all && !sawnull) ? d_bool(false) : (sawnull ? d_null(BOOLOID) : d_bool(all));
    }
    ExecCtx sub; memset(&sub, 0, sizeof sub);
    sub.s = s; sub.outer = c; sub.params = c->params; sub.nparams = c->nparams;
    sub.ctes = c->ctes; sub.nctes = c->nctes;
    RowSet *rs = exec_select(&sub, e->subquery->select);
    if (e->tag == X_EXISTS) return d_bool(rs->nrows > 0);
    if (e->sublink_kind == 5) {               /* ARRAY(subquery) */
        Datum *vals = xmalloc((size_t)(rs->nrows ? rs->nrows : 1) * sizeof(Datum));
        Oid et = rs->ncols ? rs->cols[0].type : TEXTOID;
        for (int i = 0; i < rs->nrows; i++) vals[i] = rs->rows[i][0];
        if (et == UNKNOWNOID) et = TEXTOID;
        Datum r; memset(&r, 0, sizeof r);
        r.type = array_type_of(et) ? array_type_of(et) : TEXTARRAYOID;
        r.v.arr = mk_array(et, vals, rs->nrows);
        return r;
    }
    if (e->tag == X_ANYALL) {
        Datum l = eval(c, e->testexpr);
        bool any = false, all = true, sawnull = false;
        for (int i = 0; i < rs->nrows; i++) {
            Datum ar[2]; ar[0] = l; ar[1] = rs->rows[i][0];
            bool f = false;
            Datum r = call_builtin(s, e->name, ar, 2, &f);
            if (!f) ereport_pos(s, "42883", e->pos, "operator does not exist");
            if (r.isnull) { sawnull = true; all = false; continue; }
            if (r.v.i) any = true; else all = false;
        }
        if (e->sublink_kind == 3) return any ? d_bool(true) : (sawnull ? d_null(BOOLOID) : d_bool(false));
        return (!all && !sawnull) ? d_bool(false) : (sawnull ? d_null(BOOLOID) : d_bool(all));
    }
    if (rs->nrows > 1)
        ereport(s, "21000", "more than one row returned by a subquery used as an expression");
    if (rs->nrows == 0) return d_null(rs->ncols ? rs->cols[0].type : TEXTOID);
    if (rs->ncols == 1) return rs->rows[0][0];
    RecordVal *rv = xmalloc(sizeof *rv);
    rv->n = rs->ncols;
    rv->vals = rs->rows[0];
    rv->names = NULL;
    Datum r; memset(&r, 0, sizeof r);
    r.type = RECORDOID; r.v.rec = rv;
    return r;
}

/* ===================================================================== FROM */
uint64 snapshot_xid(Session *s);
bool row_visible(Session *s, Row *r);
bool type_is_rangey(Oid t);
static char *rel_opt_check(Session *s, char relkind, const char *name, const char *val)
    __attribute__((warn_unused_result));
static void rel_set_opt(Table *t, const char *name, const char *val);
void lock_table_read(Session *s, Table *t);
void lock_table_write(Session *s, Table *t);
void row_lock_wait(Session *s, Row *r);
void row_mark_locked(Session *s, Row *r);
bool row_is_mine(Session *s, uint64 x);

static void collect_partitions(Database *db, Table *t, Table ***out, int *n)
{
    *out = xrealloc(*out, (size_t)(*n + 1) * sizeof(Table *));
    (*out)[(*n)++] = t;
    if (!t->partstrat) return;
    for (int i = 0; i < db->ntables; i++)
        if (db->tables[i]->parent == t->oid) collect_partitions(db, db->tables[i], out, n);
}

static RowSet *scan_view(ExecCtx *c, Table *t, const char *alias);

/* PostgreSQL checks SELECT on the relation named in the query; a view that is
 * not security_invoker then runs its body as its own owner. */
static void check_read_priv(Session *s, Table *t)
{
    if (table_priv(s, t, NULL, 'r')) return;
    ereport(s, "42501", "permission denied for %s %s",
            t->relkind == 'v' ? "view" : t->relkind == 'm' ? "materialized view" :
            t->relkind == 'S' ? "sequence" : "table", t->name);
}

static RowSet *scan_table(ExecCtx *c, Table *t, const char *alias, bool only)
{
    Session *s = c->s;
    check_read_priv(s, t);
    if (t->relkind == 'v') return scan_view(c, t, alias ? alias : t->name);
    RowSet *rs = rs_new();
    for (int i = 0; i < t->ncols; i++)
        if (!t->cols[i].dropped)
            rs_addcol(rs, alias ? alias : t->name, t->cols[i].name, t->cols[i].type, t->cols[i].typmod);
    /* The pseudo-columns PostgreSQL exposes on every table.  tableoid names
     * the partition a row physically lives in, not the table that was named
     * in the query, so it has to be filled while the rows are collected. */
    int pbase = rs->ncols;
    rs_addcol(rs, alias ? alias : t->name, "tableoid", OIDOID, -1);
    rs_addcol(rs, alias ? alias : t->name, "ctid", TEXTOID, -1);
    rs->cols[pbase].hidden = rs->cols[pbase+1].hidden = true;
    Table **parts = NULL; int nparts = 0;
    if (!only) collect_partitions(s->db, t, &parts, &nparts);
    else { parts = xmalloc(sizeof(Table *)); parts[0] = t; nparts = 1; }
    for (int p = 0; p < nparts; p++) {
        Table *pt = parts[p];
        if (pt->relkind == 'p') continue;
        lock_table_read(s, pt);
        int ctid = 0;
        for (int i = 0; i < pt->nrows; i++) {
            Row *r = pt->rows[i];
            if (!row_visible(s, r)) continue;
            Datum *row = row_new(rs->ncols);
            int k = 0;
            for (int q = 0; q < t->ncols; q++) {
                if (t->cols[q].dropped) continue;
                int src = (pt == t) ? q : col_index(pt, t->cols[q].name);
                row[k++] = (src >= 0 && src < r->n) ? r->vals[src] : d_null(t->cols[q].type);
            }
            row[pbase] = d_int(OIDOID, pt->oid);
            char cb[32]; snprintf(cb, sizeof cb, "(0,%d)", ++ctid);
            row[pbase+1] = d_text(TEXTOID, cb);
            rs_addrow(rs, row);
        }
    }
    free(parts);
    return rs;
}

static RowSet *exec_from(ExecCtx *c, RangeTbl *rt);
static RowSet *xmltable_rowset(ExecCtx *c, RangeTbl *rt);

static RowSet *scan_view(ExecCtx *c, Table *t, const char *alias)
{
    Session *s = c->s;
    Node *n = parse_sql(s, t->viewdef, NULL);
    ExecCtx sub; memset(&sub, 0, sizeof sub);
    sub.s = s; sub.params = c->params; sub.nparams = c->nparams;
    sub.ctes = c->ctes; sub.nctes = c->nctes;
    const char *saverole = s->eff_role;
    if (!t->security_invoker && t->owner) s->eff_role = t->owner;
    RowSet *rs = exec_select(&sub, n->select);
    s->eff_role = saverole;
    RowSet *out = rs_new();
    for (int i = 0; i < t->ncols && i < rs->ncols; i++)
        rs_addcol(out, alias, t->cols[i].name, rs->cols[i].type, rs->cols[i].typmod);
    for (int i = 0; i < rs->nrows; i++) rs_addrow(out, rs->rows[i]);
    return out;
}

static RowSet *sysrel_rowset(Session *s, const char *schema, const char *name,
                             const char *alias, bool *found)
{
    QueryResult *q = sysview_scan(s, schema, name, found);
    if (!*found) return NULL;
    RowSet *rs = rs_new();
    for (int i = 0; i < q->ncols; i++)
        rs_addcol(rs, alias ? alias : name, q->colnames[i], q->coltypes[i], q->coltypmods[i]);
    for (int i = 0; i < q->nrows; i++) rs_addrow(rs, q->rows[i]);
    return rs;
}

static RowSet *cross_join(RowSet *a, RowSet *b)
{
    RowSet *r = rs_new();
    for (int i = 0; i < a->ncols; i++) rs_addcol_meta(r, a->cols[i]);
    for (int i = 0; i < b->ncols; i++) rs_addcol_meta(r, b->cols[i]);
    for (int i = 0; i < a->nrows; i++)
        for (int k = 0; k < b->nrows; k++) {
            Datum *row = xmalloc((size_t)(r->ncols ? r->ncols : 1) * sizeof(Datum));
            memcpy(row, a->rows[i], (size_t)a->ncols * sizeof(Datum));
            memcpy(row + a->ncols, b->rows[k], (size_t)b->ncols * sizeof(Datum));
            rs_addrow(r, row);
        }
    return r;
}

static RowSet *exec_join(ExecCtx *c, RangeTbl *rt)
{
    Session *s = c->s;
    RowSet *L = exec_from(c, rt->left);
    if (rt->right->lateral && rt->jointype != 2 && rt->jointype != 3) {
        /* LATERAL: the right side is re-evaluated for each left row */
        RowSet *out = rs_new();
        Datum *dummy = row_new(L->ncols);
        for (int k = 0; k < L->ncols; k++) dummy[k] = d_null(L->cols[k].type);
        { ExecCtx lc = *c; lc.rs = L; lc.row = L->nrows ? L->rows[0] : dummy;
          RowSet *shape = exec_from(&lc, rt->right);
          for (int k = 0; k < L->ncols; k++) rs_addcol_meta(out, L->cols[k]);
          for (int k = 0; k < shape->ncols; k++) rs_addcol_meta(out, shape->cols[k]); }
        int rncols = out->ncols - L->ncols;
        for (int r = 0; r < L->nrows; r++) {
            ExecCtx lc = *c; lc.rs = L; lc.row = L->rows[r];
            RowSet *b = exec_from(&lc, rt->right);
            bool any = false;
            for (int k = 0; k < b->nrows; k++) {
                Datum *row = xmalloc((size_t)(out->ncols ? out->ncols : 1) * sizeof(Datum));
                memcpy(row, L->rows[r], (size_t)L->ncols * sizeof(Datum));
                memcpy(row + L->ncols, b->rows[k], (size_t)rncols * sizeof(Datum));
                if (rt->joinqual) {
                    ExecCtx jc = *c; jc.rs = out; jc.row = row;
                    if (!datum_true(to_bool(s, eval(&jc, rt->joinqual)))) continue;
                }
                rs_addrow(out, row);
                any = true;
            }
            if (!any && rt->jointype == 1) {
                Datum *row = xmalloc((size_t)(out->ncols ? out->ncols : 1) * sizeof(Datum));
                memcpy(row, L->rows[r], (size_t)L->ncols * sizeof(Datum));
                for (int k = 0; k < rncols; k++)
                    row[L->ncols + k] = d_null(out->cols[L->ncols + k].type);
                rs_addrow(out, row);
            }
        }
        return out;
    }
    RowSet *R = exec_from(c, rt->right);
    char **using = rt->usingcols; int nusing = rt->nusing;
    if (rt->natural) {
        using = NULL; nusing = 0;
        for (int i = 0; i < L->ncols; i++) {
            if (L->cols[i].hidden) continue;
            for (int k = 0; k < R->ncols; k++)
                if (!R->cols[k].hidden && !strcmp(L->cols[i].name, R->cols[k].name)) {
                    using = xrealloc(using, (size_t)(nusing + 1) * sizeof(char *));
                    using[nusing++] = L->cols[i].name;
                    break;
                }
        }
    }
    RowSet *r = rs_new();
    /* USING columns are merged and appear first, unqualified */
    int *lu = NULL, *ru = NULL;
    if (nusing) {
        lu = xmalloc((size_t)nusing * sizeof(int));
        ru = xmalloc((size_t)nusing * sizeof(int));
        for (int i = 0; i < nusing; i++) {
            lu[i] = find_col(L, NULL, using[i], s, 0);
            ru[i] = find_col(R, NULL, using[i], s, 0);
            if (lu[i] < 0 || ru[i] < 0)
                ereport(s, "42703", "column \"%s\" specified in USING clause does not exist in %s table",
                        using[i], lu[i] < 0 ? "left" : "right");
            rs_addcol(r, rt->alias, using[i], L->cols[lu[i]].type, L->cols[lu[i]].typmod);
        }
    }
    for (int i = 0; i < L->ncols; i++) {
        rs_addcol_meta(r, L->cols[i]);
        for (int k = 0; k < nusing; k++) if (lu[k] == i) r->cols[r->ncols-1].hidden = true;
    }
    for (int i = 0; i < R->ncols; i++) {
        rs_addcol_meta(r, R->cols[i]);
        for (int k = 0; k < nusing; k++) if (ru[k] == i) r->cols[r->ncols-1].hidden = true;
    }
    int nl = L->ncols, nr = R->ncols;

    bool *rmatched = xmalloc((size_t)(R->nrows ? R->nrows : 1) * sizeof(bool));
    memset(rmatched, 0, (size_t)(R->nrows ? R->nrows : 1) * sizeof(bool));

    Datum *joined = xmalloc((size_t)(L->ncols + R->ncols ? L->ncols + R->ncols : 1) * sizeof(Datum));
    RowSet probe; memset(&probe, 0, sizeof probe);
    probe.ncols = L->ncols + R->ncols;
    probe.cols = xmalloc((size_t)(probe.ncols ? probe.ncols : 1) * sizeof(ColMeta));
    memcpy(probe.cols, L->cols, (size_t)L->ncols * sizeof(ColMeta));
    memcpy(probe.cols + L->ncols, R->cols, (size_t)R->ncols * sizeof(ColMeta));

    for (int i = 0; i < L->nrows; i++) {
        bool lmatched = false;
        for (int k = 0; k < R->nrows; k++) {
            memcpy(joined, L->rows[i], (size_t)L->ncols * sizeof(Datum));
            memcpy(joined + L->ncols, R->rows[k], (size_t)R->ncols * sizeof(Datum));
            bool ok = true;
            if (nusing) {
                for (int q = 0; q < nusing && ok; q++) {
                    Datum a = L->rows[i][lu[q]], b = R->rows[k][ru[q]];
                    if (a.isnull || b.isnull || d_cmp(s, a, b) != 0) ok = false;
                }
            } else if (rt->joinqual) {
                ExecCtx jc = *c;
                jc.rs = &probe; jc.row = joined; jc.outer = c;
                ok = datum_true(to_bool(s, eval(&jc, rt->joinqual)));
            }
            if (!ok) continue;
            lmatched = true; rmatched[k] = true;
            Datum *row = xmalloc((size_t)(r->ncols ? r->ncols : 1) * sizeof(Datum));
            int o = 0;
            for (int q = 0; q < nusing; q++) row[o++] = L->rows[i][lu[q]];
            for (int q = 0; q < L->ncols; q++) row[o++] = L->rows[i][q];
            for (int q = 0; q < R->ncols; q++) row[o++] = R->rows[k][q];
            rs_addrow(r, row);
        }
        if (!lmatched && (rt->jointype == 1 || rt->jointype == 3)) {
            Datum *row = xmalloc((size_t)(r->ncols ? r->ncols : 1) * sizeof(Datum));
            int o = 0;
            for (int q = 0; q < nusing; q++) row[o++] = L->rows[i][lu[q]];
            for (int q = 0; q < L->ncols; q++) row[o++] = L->rows[i][q];
            for (int q = 0; q < nr; q++) row[o++] = d_null(r->cols[nusing + nl + q].type);
            rs_addrow(r, row);
        }
    }
    if (rt->jointype == 2 || rt->jointype == 3) {
        for (int k = 0; k < R->nrows; k++) {
            if (rmatched[k]) continue;
            Datum *row = xmalloc((size_t)(r->ncols ? r->ncols : 1) * sizeof(Datum));
            int o = 0;
            for (int q = 0; q < nusing; q++) row[o++] = R->rows[k][ru[q]];
            for (int q = 0; q < nl; q++) row[o++] = d_null(r->cols[nusing + q].type);
            for (int q = 0; q < R->ncols; q++) row[o++] = R->rows[k][q];
            rs_addrow(r, row);
        }
    }
    if (rt->jointype == 4) return cross_join(L, R);
    free(rmatched); free(joined);
    return r;
}

static RowSet *srf_rowset(ExecCtx *c, Expr *f, const char *alias, char **colaliases, int ncolaliases);

/* WITH ORDINALITY adds a bigint column numbering the rows from one. */
static void rs_add_ordinality(RowSet *rs, const char *alias, char **colaliases, int ncolaliases)
{
    int k = rs->ncols;
    rs_addcol(rs, alias, (colaliases && ncolaliases > k) ? colaliases[k] : "ordinality",
              INT8OID, -1);
    for (int i = 0; i < rs->nrows; i++) {
        Datum *row = xrealloc(rs->rows[i], (size_t)(k + 1) * sizeof(Datum));
        row[k] = d_int(INT8OID, i + 1);
        rs->rows[i] = row;
    }
}

static RowSet *exec_from(ExecCtx *c, RangeTbl *rt)
{
    Session *s = c->s;
    switch (rt->kind) {
    case 0: {
        for (int i = 0; i < c->nctes; i++)
            if (!rt->schema && !strcmp(c->ctes[i].name, rt->relname)) {
                RowSet *src = c->ctes[i].rs;
                if (c->ctes[i].inlined) {
                    /* An inlined WITH query is evaluated afresh at every site. */
                    src = exec_select(c->ctes[i].defctx, c->ctes[i].inlsel);
                    CteDef *d = c->ctes[i].def;
                    if (d && d->ncols)
                        for (int q = 0; q < d->ncols && q < src->ncols; q++) {
                            free(src->cols[q].name);
                            src->cols[q].name = xstrdup(d->cols[q]);
                        }
                }
                RowSet *rs = rs_new();
                for (int k = 0; k < src->ncols; k++)
                    rs_addcol(rs, rt->alias ? rt->alias : rt->relname,
                              (rt->colaliases && k < rt->ncolaliases) ? rt->colaliases[k] : src->cols[k].name,
                              src->cols[k].type, src->cols[k].typmod);
                for (int k = 0; k < src->nrows; k++) rs_addrow(rs, src->rows[k]);
                return rs;
            }
        Table *t = tbl_find_sp(s, rt->schema, rt->relname);
        if (!t) {
            bool found = false;
            RowSet *rs = sysrel_rowset(s, rt->schema, rt->relname,
                                       rt->alias ? rt->alias : rt->relname, &found);
            if (found) return rs;
            if (rt->schema)
                ereport_pos(s, "42P01", rt->pos, "relation \"%s.%s\" does not exist",
                            rt->schema, rt->relname);
            ereport_pos(s, "42P01", rt->pos, "relation \"%s\" does not exist", rt->relname);
        }
        if (t->relkind == 'S') {
            RowSet *rs = rs_new();
            rs_addcol(rs, rt->alias ? rt->alias : t->name, "last_value", INT8OID, -1);
            rs_addcol(rs, rt->alias ? rt->alias : t->name, "log_cnt", INT8OID, -1);
            rs_addcol(rs, rt->alias ? rt->alias : t->name, "is_called", BOOLOID, -1);
            Datum *row = row_new(3);
            row[0] = d_int(INT8OID, t->seq_called ? t->seq_last : t->seq_start);
            row[1] = d_int(INT8OID, 0);
            row[2] = d_bool(t->seq_called);
            rs_addrow(rs, row);
            return rs;
        }
        RowSet *rs = scan_table(c, t, rt->alias ? rt->alias : t->name, rt->only);
        if (rt->ncolaliases) {
            int nuser = rs->ncols;
            while (nuser > 0 && rs->cols[nuser-1].hidden) nuser--;
            for (int i = 0; i < rt->ncolaliases && i < nuser; i++) {
                free(rs->cols[i].name);
                rs->cols[i].name = xstrdup(rt->colaliases[i]);
            }
        }
        return rs;
    }
    case 1: {
        ExecCtx sub; memset(&sub, 0, sizeof sub);
        sub.s = s; sub.params = c->params; sub.nparams = c->nparams;
        sub.ctes = c->ctes; sub.nctes = c->nctes;
        if (rt->lateral) { sub.outer = c; }
        RowSet *src = exec_select(&sub, rt->subquery->select);
        RowSet *rs = rs_new();
        for (int i = 0; i < src->ncols; i++) {
            rs_addcol(rs, rt->alias, (rt->colaliases && i < rt->ncolaliases) ?
                      rt->colaliases[i] : src->cols[i].name, src->cols[i].type, src->cols[i].typmod);
            rs->cols[rs->ncols-1].hidden = src->cols[i].hidden;
        }
        for (int i = 0; i < src->nrows; i++) rs_addrow(rs, src->rows[i]);
        return rs;
    }
    case 2: return exec_join(c, rt);
    case 3: {
        RowSet *rs = srf_rowset(c, rt->funcexpr, rt->alias, rt->colaliases, rt->ncolaliases);
        if (rt->ordinality) rs_add_ordinality(rs, rt->alias, rt->colaliases, rt->ncolaliases);
        return rs;
    }
    case 6: return xmltable_rowset(c, rt);
#if PGVC_VN >= 170
    case 7: {
        JsonTableDef *jt = rt->jtable;
        RowSet *rs = rs_new();
        Datum ctxd = eval(c, jt->ctx);
        if (!ctxd.isnull && ctxd.type != JSONOID && ctxd.type != JSONBOID)
            ctxd = coerce_to(s, ctxd, JSONBOID, -1, 2);
        Datum pathd = eval(c, jt->path);
        JsonTableCol **cols = NULL; Datum **rows = NULL; int ncols = 0;
        int nrows = json_table_run(s, jt, ctxd, pathd, eval_thunk, c, &cols, &ncols, &rows);
        for (int i = 0; i < ncols; i++)
            rs_addcol(rs, rt->alias, cols[i]->name, cols[i]->type, cols[i]->typmod);
        for (int i = 0; i < nrows; i++) rs_addrow(rs, rows[i]);
        return rs;
    }
#endif
    }
    ereport(s, "0A000", "unsupported FROM item");
}

/* XMLTABLE: one row per node the row expression selects. */
static RowSet *xmltable_rowset(ExecCtx *c, RangeTbl *rt)
{
    Session *s = c->s;
    XmlTableDef *xt = rt->xmlt;
    RowSet *rs = rs_new();
    const char *alias = rt->alias ? rt->alias : "xmltable";
    for (int i = 0; i < xt->ncols; i++)
        rs_addcol(rs, alias, xt->cols[i].name, xt->cols[i].type, xt->cols[i].typmod);
    Datum doc = eval(c, xt->docexpr);
    Datum rowp = eval(c, xt->rowexpr);
    if (doc.isnull || rowp.isnull) return rs;
    char *dt = out_func(s, doc);
    char *rp = out_func(s, rowp);
    char *perr = NULL;
    XmlNode *root = xml_parse_doc(dt, &perr);
    if (!root) {
        err_detail(s, "%s", perr ? perr : "");
        ereport(s, "2200M", "could not parse XML document");
    }
    XmlNode **nodes = NULL; int nn = 0;
    if (!xml_path(root, root, rp, &nodes, &nn)) {
        err_detail(s, "Invalid expression");
        ereport(s, "2200V", "invalid XPath expression");
    }
    for (int i = 0; i < nn; i++) {
        Datum *row = row_new(rs->ncols);
        for (int k = 0; k < xt->ncols; k++) {
            XmlTableCol *col = &xt->cols[k];
            if (col->forordinality) { row[k] = d_int(INT4OID, i + 1); continue; }
            char *path = col->path ? xstrdup(col->path) : NULL;
            if (!path) {
                Datum pv = eval(c, col->pathexpr);
                path = pv.isnull ? xstrdup("") : out_func(s, pv);
            }
            XmlNode **hits = NULL; int nh = 0;
            if (!xml_path(root, nodes[i], path, &hits, &nh)) {
                err_detail(s, "Invalid expression");
                ereport(s, "2200V", "invalid XPath expression");
            }
            free(path);
            if (!nh) {
                if (col->defaultexpr)
                    row[k] = coerce_to(s, eval(c, col->defaultexpr), col->type, col->typmod, 1);
                else row[k] = d_null(col->type);
            } else {
                char *txt = xml_node_text(hits[0]);
                row[k] = in_func(s, col->type, col->typmod, txt, (int32)strlen(txt));
                free(txt);
            }
            free(hits);
            if (col->notnull && row[k].isnull)
                ereport(s, "22004", "null is not allowed in column \"%s\"", col->name);
        }
        rs_addrow(rs, row);
    }
    free(nodes); free(dt); free(rp);
    return rs;
}

/* set-returning functions in FROM */
static RowSet *srf_rowset(ExecCtx *c, Expr *f, const char *alias, char **colaliases, int ncolaliases)
{
    Session *s = c->s;
    RowSet *rs = rs_new();
    const char *nm = (f->tag == X_FUNC || f->tag == X_AGG) ? f->name : "";
    if (!strcmp(nm, "generate_series")) {
        Datum a = eval(c, f->args[0]), b = eval(c, f->args[1]);
        Datum st = f->nargs > 2 ? eval(c, f->args[2]) : d_int(INT8OID, 1);
        rs_addcol(rs, alias ? alias : "generate_series",
                  (colaliases && ncolaliases) ? colaliases[0] : (alias ? alias : "generate_series"),
                  (a.type == INT8OID || b.type == INT8OID) ? INT8OID : INT4OID, -1);
        if (a.isnull || b.isnull) return rs;
        int64 step = st.isnull ? 1 : st.v.i;
        if (step == 0) ereport(s, "22023", "step size cannot equal zero");
        for (int64 v = a.v.i; step > 0 ? v <= b.v.i : v >= b.v.i; v += step) {
            Datum *row = row_new(1);
            row[0] = d_int(rs->cols[0].type, v);
            rs_addrow(rs, row);
            if ((rs->nrows & 0x3ff) == 0) check_cancel(s);
            if (rs->nrows > PGVC_SRF_MAX) break;
        }
        return rs;
    }
#if PGVC_VN >= 130
    if (!strcmp(nm, "pg_snapshot_xip")) {
        rs_addcol(rs, alias ? alias : nm,
                  (colaliases && ncolaliases) ? colaliases[0] : (alias ? alias : nm),
                  XID8OID, -1);
        Datum a = eval(c, f->args[0]);
        if (a.isnull) return rs;
        Datum sn = coerce_to(s, a, PGSNAPSHOTOID, -1, 1);
        uint64 xmin = 0, xmax = 0, xip[1024]; int nx = 0;
        snapshot_parts(sn.v.s.p, &xmin, &xmax, xip, 1024, &nx);
        for (int i = 0; i < nx && i < 1024; i++) {
            Datum *row = row_new(1);
            row[0] = d_int(XID8OID, (int64)xip[i]);
            rs_addrow(rs, row);
        }
        return rs;
    }
#endif
#if PGVC_VN >= 120
    if (!strcmp(nm, "pg_partition_tree") || !strcmp(nm, "pg_partition_ancestors")) {
        bool tree = !strcmp(nm, "pg_partition_tree");
        Datum a = eval(c, f->args[0]);
        rs_addcol(rs, alias ? alias : nm, "relid", REGCLASSOID, -1);
        if (tree) {
            rs_addcol(rs, alias ? alias : nm, "parentrelid", REGCLASSOID, -1);
            rs_addcol(rs, alias ? alias : nm, "isleaf", BOOLOID, -1);
            rs_addcol(rs, alias ? alias : nm, "level", INT4OID, -1);
        }
        if (a.isnull) return rs;
        s->err.position = f->args[0]->pos;
        Datum rc = coerce_to(s, a, REGCLASSOID, -1, 2);
        s->err.position = 0;
        Oid oid = (Oid)rc.v.i;
        Table *t = tbl_find_oid(s->db, oid);
        Index *ix = NULL;
        if (!t) for (int i = 0; i < s->db->nindexes; i++)
                    if (s->db->indexes[i]->oid == oid) ix = s->db->indexes[i];
        if (!t && !ix) return rs;
        if (tree) {
            /* nothing at all for a plain table that is neither parent nor part */
            if (t && !t->partstrat && !t->parent) return rs;
            if (ix && !ix->partitioned && !ix->parent_index) return rs;
            /* breadth-first from the relation itself */
            Oid *q = xmalloc(sizeof(Oid)); int nq = 0, cap = 1;
            int *lv = xmalloc(sizeof(int));
            q[nq] = oid; lv[nq] = 0; nq++;
            for (int i = 0; i < nq; i++) {
                Oid cur = q[i];
                Table *ct = tbl_find_oid(s->db, cur);
                Index *ci = NULL;
                if (!ct) for (int k = 0; k < s->db->nindexes; k++)
                             if (s->db->indexes[k]->oid == cur) ci = s->db->indexes[k];
                Oid parent = ct ? ct->parent : (ci ? ci->parent_index : 0);
                bool leaf = true;
                for (int k = 0; k < s->db->ntables; k++)
                    if (s->db->tables[k]->parent == cur) {
                        leaf = false;
                        if (nq == cap) { cap *= 2; q = xrealloc(q, (size_t)cap * sizeof(Oid));
                                         lv = xrealloc(lv, (size_t)cap * sizeof(int)); }
                        q[nq] = s->db->tables[k]->oid; lv[nq] = lv[i] + 1; nq++;
                    }
                for (int k = 0; k < s->db->nindexes; k++)
                    if (s->db->indexes[k]->parent_index == cur) {
                        leaf = false;
                        if (nq == cap) { cap *= 2; q = xrealloc(q, (size_t)cap * sizeof(Oid));
                                         lv = xrealloc(lv, (size_t)cap * sizeof(int)); }
                        q[nq] = s->db->indexes[k]->oid; lv[nq] = lv[i] + 1; nq++;
                    }
                Datum *row = row_new(4);
                row[0] = d_int(REGCLASSOID, cur);
                row[1] = parent ? d_int(REGCLASSOID, parent) : d_null(REGCLASSOID);
                row[2] = d_bool(leaf);
                row[3] = d_int(INT4OID, lv[i]);
                rs_addrow(rs, row);
            }
            free(q); free(lv);
            return rs;
        }
        /* pg_partition_ancestors: the relation, then each parent upward */
        if (t && !t->partstrat && !t->parent) return rs;
        for (Oid cur = oid; cur; ) {
            Datum *row = row_new(1);
            row[0] = d_int(REGCLASSOID, cur);
            rs_addrow(rs, row);
            Table *ct = tbl_find_oid(s->db, cur);
            Index *ci = NULL;
            if (!ct) for (int k = 0; k < s->db->nindexes; k++)
                         if (s->db->indexes[k]->oid == cur) ci = s->db->indexes[k];
            cur = ct ? ct->parent : (ci ? ci->parent_index : 0);
        }
        return rs;
    }
#endif
#if PGVC_VN >= 160
    if (!strcmp(nm, "pg_input_error_info")) {
        rs_addcol(rs, alias ? alias : nm, (colaliases && ncolaliases > 0) ? colaliases[0] : "message", TEXTOID, -1);
        rs_addcol(rs, alias ? alias : nm, (colaliases && ncolaliases > 1) ? colaliases[1] : "detail", TEXTOID, -1);
        rs_addcol(rs, alias ? alias : nm, (colaliases && ncolaliases > 2) ? colaliases[2] : "hint", TEXTOID, -1);
        rs_addcol(rs, alias ? alias : nm, (colaliases && ncolaliases > 3) ? colaliases[3] : "sql_error_code", TEXTOID, -1);
        Datum a0 = eval(c, f->args[0]);
        Datum a1 = f->nargs > 1 ? eval(c, f->args[1]) : d_null(TEXTOID);
        extern void pgvc_input_type(Session *s, Datum d, Oid *ty, int32 *tm);
        extern bool pgvc_try_input(Session *s, Oid ty, int32 tm, const char *txt, ErrState *cap);
        Oid ty; int32 tm;
        pgvc_input_type(s, a1, &ty, &tm);
        Datum *row = row_new(4);
        if (a0.isnull) { for (int i = 0; i < 4; i++) row[i] = d_null(TEXTOID); rs_addrow(rs, row); return rs; }
        char *t = out_func(s, a0);
        ErrState cap; memset(&cap, 0, sizeof cap);
        bool ok = pgvc_try_input(s, ty, tm, t, &cap);
        free(t);
        if (ok) { for (int i = 0; i < 4; i++) row[i] = d_null(TEXTOID); }
        else {
            row[0] = d_text(TEXTOID, cap.message);
            row[1] = cap.detail[0] ? d_text(TEXTOID, cap.detail) : d_null(TEXTOID);
            row[2] = cap.hint[0] ? d_text(TEXTOID, cap.hint) : d_null(TEXTOID);
            row[3] = d_text(TEXTOID, cap.sqlstate);
        }
        rs_addrow(rs, row);
        return rs;
    }
    if (!strcmp(nm, "pg_split_walfile_name")) {
        rs_addcol(rs, alias ? alias : nm, (colaliases && ncolaliases > 0) ? colaliases[0] : "segment_number", NUMERICOID, -1);
        rs_addcol(rs, alias ? alias : nm, (colaliases && ncolaliases > 1) ? colaliases[1] : "timeline_id", INT8OID, -1);
        Datum a = eval(c, f->args[0]);
        Datum *row = row_new(2);
        if (a.isnull) { row[0] = d_null(NUMERICOID); row[1] = d_null(INT8OID); rs_addrow(rs, row); return rs; }
        char *t = out_func(s, a);
        extern bool wal_split_name(const char *t, uint64 *seg, uint32 *tli);
        uint64 seg = 0; uint32 tli = 0;
        if (!wal_split_name(t, &seg, &tli))
            ereport(s, "22023", "invalid WAL file name \"%s\"", t);
        free(t);
        char nb[32];
        snprintf(nb, sizeof nb, "%llu", (unsigned long long)seg);
        bool nok = false;
        row[0] = d_num(num_from_str(nb, &nok));
        row[1] = d_int(INT8OID, (int64)tli);
        rs_addrow(rs, row);
        return rs;
    }
#endif
    if (!strcmp(nm, "pg_create_physical_replication_slot") ||
        !strcmp(nm, "pg_create_logical_replication_slot")) {
        bool logical = strstr(nm, "logical") != NULL;
        Datum a = eval(c, f->args[0]);
        Datum pl = (f->nargs > 1) ? eval(c, f->args[1]) : d_null(NAMEOID);
        rs_addcol(rs, alias ? alias : nm, "slot_name", NAMEOID, -1);
        rs_addcol(rs, alias ? alias : nm, logical ? "lsn" : "lsn", PGLSNOID, -1);
        extern void repl_slot_create(Session *s, const char *name, const char *plugin,
                                     bool logical, bool temporary);
        char *sn = out_func(s, a);
        char *pn = pl.isnull ? NULL : out_func(s, pl);
        repl_slot_create(s, sn, pn, logical, f->nargs > 2 && !logical ? false : false);
        Datum *row = row_new(2);
        row[0] = d_text(NAMEOID, sn);
        row[1] = d_int(PGLSNOID, 0);
        rs_addrow(rs, row);
        free(sn); free(pn);
        return rs;
    }
    if (!strcmp(nm, "aclexplode")) {
        /* aclexplode(aclitem[]) returns (grantor, grantee, privilege_type,
         * is_grantable); ACLs are kept here as their text rendering. */
        static const struct { char c; const char *nm; } privs[] = {
            {'a',"INSERT"}, {'r',"SELECT"}, {'w',"UPDATE"}, {'d',"DELETE"},
            {'D',"TRUNCATE"}, {'x',"REFERENCES"}, {'t',"TRIGGER"}, {'X',"EXECUTE"},
            {'U',"USAGE"}, {'C',"CREATE"}, {'c',"CONNECT"}, {'T',"TEMPORARY"},
            {0,NULL}
        };
        Datum a = eval(c, f->args[0]);
        rs_addcol(rs, alias ? alias : nm,
                  (colaliases && ncolaliases > 0) ? colaliases[0] : "grantor", OIDOID, -1);
        rs_addcol(rs, alias ? alias : nm,
                  (colaliases && ncolaliases > 1) ? colaliases[1] : "grantee", OIDOID, -1);
        rs_addcol(rs, alias ? alias : nm,
                  (colaliases && ncolaliases > 2) ? colaliases[2] : "privilege_type", TEXTOID, -1);
        rs_addcol(rs, alias ? alias : nm,
                  (colaliases && ncolaliases > 3) ? colaliases[3] : "is_grantable", BOOLOID, -1);
        if (a.isnull || !type_is_array(a.type)) return rs;
        ArrayVal *av = a.v.arr;
        int nitem = av->nelem;
        for (int i = 0; i < nitem; i++) {
            if (av->elems[i].isnull) continue;
            char *item = out_func(s, av->elems[i]);
            char *eq = strchr(item, '=');
            if (!eq) { free(item); continue; }
            char *sl = strrchr(eq, '/');
            char *gee = xstrndup(item, (size_t)(eq - item));
            char *gor = sl ? xstrdup(sl + 1) : xstrdup("");
            char *pv  = sl ? xstrndup(eq + 1, (size_t)(sl - eq - 1)) : xstrdup(eq + 1);
            RoleEntry *ree = *gee ? role_find(gee) : NULL;
            RoleEntry *ror = *gor ? role_find(gor) : NULL;
            for (int k = 0; privs[k].c; k++) {
                char *at = strchr(pv, privs[k].c);
                if (!at) continue;
                Datum *row = row_new(4);
                row[0] = d_int(OIDOID, ror ? (int64)ror->oid : 0);
                row[1] = d_int(OIDOID, ree ? (int64)ree->oid : 0);
                row[2] = d_text(TEXTOID, privs[k].nm);
                row[3] = d_bool(at[1] == '*');
                rs_addrow(rs, row);
            }
            free(gee); free(gor); free(pv); free(item);
        }
        return rs;
    }
    if (!strcmp(nm, "unnest") || !strcmp(nm, "jsonb_array_elements") ||
        !strcmp(nm, "jsonb_array_elements_text") || !strcmp(nm, "json_array_elements") ||
        !strcmp(nm, "json_array_elements_text") || !strcmp(nm, "regexp_split_to_table") ||
        !strcmp(nm, "jsonb_object_keys") || !strcmp(nm, "json_object_keys") ||
        !strcmp(nm, "jsonb_path_query") || !strcmp(nm, "jsonb_path_query_tz") ||
        !strcmp(nm, "string_to_table") ||
        !strcmp(nm, "generate_subscripts")) {
        Datum args[4];
        for (int i = 0; i < f->nargs && i < 4; i++) args[i] = eval(c, f->args[i]);
        bool found = false;
        Datum arr = call_builtin(s, nm, args, f->nargs, &found);
        if (!found) ereport(s, "42883", "function %s does not exist", nm);
        Oid et = type_is_array(arr.type) ? elem_type_of(arr.type) : TEXTOID;
        /* functions declared with a named OUT parameter keep that column name */
        const char *outname = nm;
        if (!strcmp(nm, "jsonb_array_elements") || !strcmp(nm, "json_array_elements") ||
            !strcmp(nm, "jsonb_array_elements_text") || !strcmp(nm, "json_array_elements_text"))
            outname = "value";
        bool named = (outname != nm);
        rs_addcol(rs, alias ? alias : nm,
                  (colaliases && ncolaliases) ? colaliases[0]
                  : (named ? outname : (alias ? alias : nm)), et, -1);
        if (arr.isnull) return rs;
        ArrayVal *a = arr.v.arr;
        for (int i = 0; i < a->nelem; i++) {
            Datum *row = row_new(1);
            row[0] = a->elems[i];
            rs_addrow(rs, row);
        }
        return rs;
    }
    if (!strcmp(nm, "jsonb_each") || !strcmp(nm, "jsonb_each_text") ||
        !strcmp(nm, "json_each") || !strcmp(nm, "json_each_text")) {
        Datum a = eval(c, f->args[0]);
        bool txt = strstr(nm, "_text") != NULL;
        rs_addcol(rs, alias ? alias : nm, (colaliases && ncolaliases > 0) ? colaliases[0] : "key", TEXTOID, -1);
        rs_addcol(rs, alias ? alias : nm, (colaliases && ncolaliases > 1) ? colaliases[1] : "value",
                  txt ? TEXTOID : JSONBOID, -1);
        if (a.isnull) return rs;
        extern int json_each_rows(Session *s, JsonVal *v, bool txt, RowSet *rs,
                                  void (*addrow)(RowSet *, Datum *));
        json_each_rows(s, a.v.jb, txt, rs, rs_addrow);
        return rs;
    }
    /* ordinary function used in FROM: single row, single column */
    Datum v = eval(c, f);
    rs_addcol(rs, alias ? alias : nm,
              (colaliases && ncolaliases) ? colaliases[0] : (alias ? alias : (*nm ? nm : "?column?")),
              v.type, -1);
    Datum *row = row_new(1);
    row[0] = v;
    rs_addrow(rs, row);
    return rs;
}

/* =================================================================== SELECT */
const char *expr_label(Expr *e)
{
    if (e->alias) return e->alias;
    switch (e->tag) {
    case X_CONST:
        /* PostgreSQL labels a bare boolean literal with its type name; every
         * other untyped constant stays "?column?". */
        if (!e->isnull_const && e->val.type == BOOLOID) return "bool";
        return "?column?";
    case X_COLREF: return e->colname;
    case X_FUNC: case X_AGG: case X_WINDOW: {
        if (!strcmp(e->name, "__field__")) return e->colname;
        if (!strcmp(e->name, "extract"))
            return (PGVC_VN >= 140) ? "extract" : "date_part";
        if (!isalpha((unsigned char)e->name[0]) && e->name[0] != '_') return "?column?";
        return e->name;
    }
    case X_CAST: {
        /* PostgreSQL names a cast after the expression inside it when that
         * expression has a name of its own, and after the type otherwise. */
        Expr *inner = e->args[0];
        while (inner->tag == X_CAST) inner = inner->args[0];
        /* Only a strong inner name survives: a column reference, a function
         * or aggregate call, ARRAY[], ROW(), COALESCE(), EXISTS.  A CASE is
         * one of the weak ones and loses to the type name. */
        if (inner->tag != X_CASE) {
            const char *in = expr_label(inner);
            if (strcmp(in, "?column?")) return in;
        }
        const char *t = type_name(e->casttype);
        return (t[0] == '_') ? t + 1 : t;
    }
    case X_CASE: return "case";
    case X_COALESCE: return "coalesce";
    case X_ARRAY: return "array";
    case X_ROW: return "row";
    case X_EXISTS: return "exists";
    case X_SUBLINK:
        if (e->sublink_kind == 0 && e->subquery && e->subquery->select &&
            e->subquery->select->ntarget == 1)
            return expr_label(e->subquery->select->target[0]);
        if (e->sublink_kind == 5) return "array";
        return "?column?";
    case X_IS: return (e->is_kind <= 1) ? "?column?" : "?column?";
    case X_JSONCTOR: return e->name;
    case X_ISJSON: return "?column?";
    case X_OLDNEW: return e->colname;
    case X_SUBSCRIPT: return expr_label(e->args[0]);
    default: return "?column?";
    }
}

static void collect_bare_cols(Expr *e, Expr ***list, int *n)
{
    if (!e) return;
    if (e->tag == X_AGG || e->tag == X_SUBLINK || e->tag == X_EXISTS || e->tag == X_WINDOW) return;
    if (e->tag == X_COLREF) {
        *list = xrealloc(*list, (size_t)(*n + 1) * sizeof(Expr *));
        (*list)[(*n)++] = e;
        return;
    }
    for (int i = 0; i < e->nargs; i++) collect_bare_cols(e->args[i], list, n);
    for (int i = 0; i < e->nwhen; i++) { collect_bare_cols(e->when_cond[i], list, n);
                                          collect_bare_cols(e->when_res[i], list, n); }
    collect_bare_cols(e->case_arg, list, n);
    collect_bare_cols(e->case_else, list, n);
    collect_bare_cols(e->testexpr, list, n);
}

static void collect_aggs(Expr *e, Expr ***list, int *n)
{
    if (!e) return;
    if (e->tag == X_AGG) {
        *list = xrealloc(*list, (size_t)(*n + 1) * sizeof(Expr *));
        (*list)[(*n)++] = e;
        return;
    }
    if (e->tag == X_SUBLINK || e->tag == X_EXISTS) return;
    for (int i = 0; i < e->nargs; i++) collect_aggs(e->args[i], list, n);
    for (int i = 0; i < e->nwhen; i++) { collect_aggs(e->when_cond[i], list, n);
                                          collect_aggs(e->when_res[i], list, n); }
    collect_aggs(e->case_arg, list, n);
    collect_aggs(e->case_else, list, n);
    collect_aggs(e->testexpr, list, n);
}
static bool is_srf_name(const char *n)
{
    static const char *a[] = {"generate_series","unnest","jsonb_array_elements",
        "jsonb_array_elements_text","json_array_elements","json_array_elements_text",
        "jsonb_object_keys","json_object_keys","regexp_split_to_table","string_to_table",
        "generate_subscripts","jsonb_path_query","jsonb_path_query_tz",
        "regexp_matches", NULL};
    for (int i = 0; a[i]; i++) if (!strcmp(a[i], n)) return true;
    return false;
}

static void collect_srfs(Expr *e, Expr ***list, int *n)
{
    if (!e) return;
    if ((e->tag == X_FUNC || e->tag == X_AGG) && e->name && is_srf_name(e->name)) {
        *list = xrealloc(*list, (size_t)(*n + 1) * sizeof(Expr *));
        (*list)[(*n)++] = e;
        return;
    }
    if (e->tag == X_SUBLINK || e->tag == X_EXISTS) return;
    for (int i = 0; i < e->nargs; i++) collect_srfs(e->args[i], list, n);
    for (int i = 0; i < e->nwhen; i++) { collect_srfs(e->when_cond[i], list, n);
                                          collect_srfs(e->when_res[i], list, n); }
    collect_srfs(e->case_arg, list, n);
    collect_srfs(e->case_else, list, n);
}

static void collect_wins(Expr *e, Expr ***list, int *n)
{
    if (!e) return;
    if (e->tag == X_WINDOW) {
        *list = xrealloc(*list, (size_t)(*n + 1) * sizeof(Expr *));
        (*list)[(*n)++] = e;
        return;
    }
    for (int i = 0; i < e->nargs; i++) collect_wins(e->args[i], list, n);
    for (int i = 0; i < e->nwhen; i++) { collect_wins(e->when_cond[i], list, n);
                                          collect_wins(e->when_res[i], list, n); }
    collect_wins(e->case_arg, list, n);
    collect_wins(e->case_else, list, n);
}

Datum agg_compute(ExecCtx *c, Expr *agg, RowSet *rs, int *idx, int n);

typedef struct SortKey { Expr *e; int dir; int nulls; int tgtidx; } SortKey;

typedef struct { ExecCtx *c; SortKey *keys; int nkeys; RowSet *rs; } SortCtx;
static SortCtx *g_sortctx;

static int sort_cmp(const void *pa, const void *pb)
{
    Datum *a = *(Datum *const *)pa, *b = *(Datum *const *)pb;
    SortCtx *sc = g_sortctx;
    for (int i = 0; i < sc->nkeys; i++) {
        ExecCtx c = *sc->c;
        c.rs = sc->rs; c.row = a;
        Datum x = sc->keys[i].tgtidx >= 0 ? a[sc->keys[i].tgtidx] : eval(&c, sc->keys[i].e);
        c.row = b;
        Datum y = sc->keys[i].tgtidx >= 0 ? b[sc->keys[i].tgtidx] : eval(&c, sc->keys[i].e);
        int r;
        if (x.isnull || y.isnull) {
            if (x.isnull && y.isnull) r = 0;
            else {
                int nulls = sc->keys[i].nulls;
                if (nulls == 0) nulls = sc->keys[i].dir ? 1 : 2;
                r = x.isnull ? (nulls == 1 ? -1 : 1) : (nulls == 1 ? 1 : -1);
                if (r) return r;
                continue;
            }
        } else r = d_cmp(sc->c->s, x, y);
        if (sc->keys[i].dir) r = -r;
        if (r) return r;
    }
    return 0;
}

/* ---------------------------------------------------------------- sorting
 * PostgreSQL's tuplesort leaves rows with equal keys in the order its own
 * quicksort happens to produce, which is not the order a stable sort gives,
 * and psql prints that order verbatim.  These two routines reproduce
 * src/include/lib/sort_template.h and tuplesort.c's bounded heap sort so the
 * tie order matches byte for byte. */

static void pgs_swap(Datum **a, Datum **b) { Datum *t = *a; *a = *b; *b = t; }

static void pgs_vecswap(Datum **a, Datum **b, size_t n)
{
    while (n-- > 0) { pgs_swap(a, b); a++; b++; }
}

static Datum **pgs_med3(Datum **a, Datum **b, Datum **c)
{
    return sort_cmp(a, b) < 0
        ? (sort_cmp(b, c) < 0 ? b : (sort_cmp(a, c) < 0 ? c : a))
        : (sort_cmp(b, c) > 0 ? b : (sort_cmp(a, c) < 0 ? a : c));
}

static void pg_sort_rows(Datum **a, size_t n)
{
    Datum **pa, **pb, **pc, **pd, **pl, **pm, **pn;
    size_t  d1, d2;
    int     r, presorted;

loop:
    if (n < 7) {
        for (pm = a + 1; pm < a + n; pm++)
            for (pl = pm; pl > a && sort_cmp(pl - 1, pl) > 0; pl--)
                pgs_swap(pl, pl - 1);
        return;
    }
    presorted = 1;
    for (pm = a + 1; pm < a + n; pm++)
        if (sort_cmp(pm - 1, pm) > 0) { presorted = 0; break; }
    if (presorted) return;
    pm = a + (n / 2);
    if (n > 7) {
        pl = a;
        pn = a + (n - 1);
        if (n > 40) {
            size_t d = n / 8;
            pl = pgs_med3(pl, pl + d, pl + 2 * d);
            pm = pgs_med3(pm - d, pm, pm + d);
            pn = pgs_med3(pn - 2 * d, pn - d, pn);
        }
        pm = pgs_med3(pl, pm, pn);
    }
    pgs_swap(a, pm);
    pa = pb = a + 1;
    pc = pd = a + (n - 1);
    for (;;) {
        while (pb <= pc && (r = sort_cmp(pb, a)) <= 0) {
            if (r == 0) { pgs_swap(pa, pb); pa++; }
            pb++;
        }
        while (pb <= pc && (r = sort_cmp(pc, a)) >= 0) {
            if (r == 0) { pgs_swap(pc, pd); pd--; }
            pc--;
        }
        if (pb > pc) break;
        pgs_swap(pb, pc);
        pb++; pc--;
    }
    pn = a + n;
    d1 = (size_t)((pa - a) < (pb - pa) ? (pa - a) : (pb - pa));
    pgs_vecswap(a, pb - d1, d1);
    d1 = (size_t)((pd - pc) < (pn - pd - 1) ? (pd - pc) : (pn - pd - 1));
    pgs_vecswap(pb, pn - d1, d1);
    d1 = (size_t)(pb - pa);
    d2 = (size_t)(pd - pc);
    if (d1 <= d2) {
        if (d1 > 1) pg_sort_rows(a, d1);
        if (d2 > 1) { a = pn - d2; n = d2; goto loop; }
    } else {
        if (d2 > 1) pg_sort_rows(pn - d2, d2);
        if (d1 > 1) { n = d1; goto loop; }
    }
}

/* the bounded heap runs the comparator backwards so the largest entry sits at
 * the root; reversing both the direction and the nulls order of every key is
 * exactly a negated comparison. */
static int pgs_rcmp(Datum **a, Datum **b) { return -sort_cmp(a, b); }

static void pgs_heap_replace_top(Datum **mt, int cnt, Datum *tuple)
{
    unsigned int i = 0, n = (unsigned int)cnt;
    for (;;) {
        unsigned int j = 2 * i + 1;
        if (j >= n) break;
        if (j + 1 < n && pgs_rcmp(&mt[j], &mt[j + 1]) > 0) j++;
        if (pgs_rcmp(&tuple, &mt[j]) <= 0) break;
        mt[i] = mt[j];
        i = j;
    }
    mt[i] = tuple;
}

static void pgs_heap_insert(Datum **mt, int *cnt, Datum *tuple)
{
    int j = (*cnt)++;
    while (j > 0) {
        int i = (j - 1) >> 1;
        if (pgs_rcmp(&tuple, &mt[i]) >= 0) break;
        mt[j] = mt[i];
        j = i;
    }
    mt[j] = tuple;
}

/* Sort with a row-count bound pushed down from LIMIT, the way nodeLimit does.
 * Returns the number of rows left in a[]. */
static size_t pg_sort_rows_bounded(Datum **a, size_t n, int64 bound)
{
    if (bound <= 0 || bound > (int64)(0x7fffffff / 2)) {
        pg_sort_rows(a, n);
        return n;
    }
    int b = (int)bound;
    /* tuplesort stays in its unsorted array until it has seen more than twice
     * the bound; below that it just quicksorts everything. */
    if (n <= (size_t)b * 2) {
        pg_sort_rows(a, n);
        return n;
    }
    size_t tupcount = (size_t)b * 2 + 1;      /* make_bounded_heap fires here */
    Datum **mt = xmalloc((size_t)b * sizeof(Datum *));
    int hc = 0;
    for (size_t k = 0; k < tupcount; k++) {
        Datum *stup = a[k];
        if (hc < b) pgs_heap_insert(mt, &hc, stup);
        else if (pgs_rcmp(&stup, &mt[0]) > 0) pgs_heap_replace_top(mt, hc, stup);
    }
    for (size_t k = tupcount; k < n; k++) {
        Datum *stup = a[k];
        if (pgs_rcmp(&stup, &mt[0]) > 0) pgs_heap_replace_top(mt, hc, stup);
    }
    int total = hc;
    while (hc > 1) {
        Datum *stup = mt[0];
        hc--;
        if (hc > 0) pgs_heap_replace_top(mt, hc, mt[hc]);
        mt[hc] = stup;
    }
    for (int k = 0; k < total; k++) a[k] = mt[k];
    free(mt);
    return (size_t)total;
}

/* GROUPING SETS, ROLLUP and CUBE expand the GROUP BY list into a list of
 * grouping sets, and several items in the list are crossed with each other. */
typedef struct { Expr ***set; int *n; int cnt; } GSetList;

static void gs_add(GSetList *l, Expr **e, int n)
{
    l->set = xrealloc(l->set, (size_t)(l->cnt + 1) * sizeof(Expr **));
    l->n   = xrealloc(l->n,   (size_t)(l->cnt + 1) * sizeof(int));
    l->set[l->cnt] = e; l->n[l->cnt] = n; l->cnt++;
}

/* the grouping sets one GROUP BY item stands for */
static void gs_of_item(Expr *g, GSetList *out)
{
    if (g->tag != X_LIST || !g->name) {
        Expr **one = xmalloc(sizeof(Expr *)); one[0] = g;
        gs_add(out, one, 1);
        return;
    }
    if (!strcmp(g->name, "grouping")) {
        for (int i = 0; i < g->nargs; i++) {
            Expr *a = g->args[i];
            if (a->tag == X_LIST && a->name && !strcmp(a->name, "()")) {
                Expr **e = xmalloc((size_t)(a->nargs ? a->nargs : 1) * sizeof(Expr *));
                for (int k = 0; k < a->nargs; k++) e[k] = a->args[k];
                gs_add(out, e, a->nargs);
            } else {
                Expr **e = xmalloc(sizeof(Expr *)); e[0] = a;
                gs_add(out, e, 1);
            }
        }
        return;
    }
    /* rollup and cube work on the items, a parenthesised group counting as one */
    Expr **flat[32]; int flatn[32]; int nf = 0;
    for (int i = 0; i < g->nargs && nf < 32; i++) {
        Expr *a = g->args[i];
        if (a->tag == X_LIST && a->name && !strcmp(a->name, "()")) {
            flat[nf] = a->args; flatn[nf] = a->nargs; nf++;
        } else {
            Expr **one = xmalloc(sizeof(Expr *)); one[0] = a;
            flat[nf] = one; flatn[nf] = 1; nf++;
        }
    }
    if (!strcmp(g->name, "rollup")) {
        for (int keep = nf; keep >= 0; keep--) {
            int tot = 0;
            for (int i = 0; i < keep; i++) tot += flatn[i];
            Expr **e = xmalloc((size_t)(tot ? tot : 1) * sizeof(Expr *));
            int w = 0;
            for (int i = 0; i < keep; i++)
                for (int k = 0; k < flatn[i]; k++) e[w++] = flat[i][k];
            gs_add(out, e, tot);
        }
        return;
    }
    /* cube */
    for (long mask = (1L << nf) - 1; mask >= 0; mask--) {
        int tot = 0;
        for (int i = 0; i < nf; i++) if (mask & (1L << (nf - 1 - i))) tot += flatn[i];
        Expr **e = xmalloc((size_t)(tot ? tot : 1) * sizeof(Expr *));
        int w = 0;
        for (int i = 0; i < nf; i++)
            if (mask & (1L << (nf - 1 - i)))
                for (int k = 0; k < flatn[i]; k++) e[w++] = flat[i][k];
        gs_add(out, e, tot);
    }
}

static bool gs_same_expr(Expr *a, Expr *b)
{
    if (a == b) return true;
    if (a->tag != b->tag) return false;
    if (a->tag == X_COLREF)
        return a->colname && b->colname && !strcmp(a->colname, b->colname) &&
               (!a->qual || !b->qual || !strcmp(a->qual, b->qual));
    return false;
}

static void gs_expand(SelectStmt *sel, GSetList *out)
{
    GSetList acc; memset(&acc, 0, sizeof acc);
    gs_add(&acc, NULL, 0);                     /* start with the empty set */
    for (int i = 0; i < sel->ngroupby; i++) {
        GSetList item; memset(&item, 0, sizeof item);
        gs_of_item(sel->groupby[i], &item);
        GSetList nx; memset(&nx, 0, sizeof nx);
        for (int a = 0; a < acc.cnt; a++)
            for (int b = 0; b < item.cnt; b++) {
                int tot = acc.n[a] + item.n[b];
                Expr **e = xmalloc((size_t)(tot ? tot : 1) * sizeof(Expr *));
                int w = 0;
                for (int k = 0; k < acc.n[a]; k++) e[w++] = acc.set[a][k];
                for (int k = 0; k < item.n[b]; k++) {
                    bool dup = false;
                    for (int q = 0; q < w; q++) if (gs_same_expr(e[q], item.set[b][k])) dup = true;
                    if (!dup) e[w++] = item.set[b][k];
                }
                gs_add(&nx, e, w);
            }
        free(acc.set); free(acc.n);
        free(item.set); free(item.n);
        acc = nx;
    }
    if (sel->group_distinct) {
        GSetList d; memset(&d, 0, sizeof d);
        for (int a = 0; a < acc.cnt; a++) {
            bool dup = false;
            for (int b = 0; b < d.cnt && !dup; b++) {
                if (d.n[b] != acc.n[a]) continue;
                bool same = true;
                for (int k = 0; k < acc.n[a] && same; k++)
                    if (!gs_same_expr(d.set[b][k], acc.set[a][k])) same = false;
                if (same) dup = true;
            }
            if (!dup) gs_add(&d, acc.set[a], acc.n[a]);
        }
        free(acc.set); free(acc.n);
        acc = d;
    }
    *out = acc;
}

static RowSet *apply_setop(ExecCtx *c, SelectStmt *sel);

static bool rows_equal(Session *s, Datum *a, Datum *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i].isnull != b[i].isnull) return false;
        if (!a[i].isnull && d_cmp(s, a[i], b[i]) != 0) return false;
    }
    return true;
}

static void run_ctes(ExecCtx *c, SelectStmt *sel, ExecCtx *out);

static RowSet *exec_select(ExecCtx *c0, SelectStmt *sel)
{
    Session *s = c0->s;
    ExecCtx cx = *c0;
    ExecCtx *c = &cx;
    if (sel->nctes) run_ctes(c, sel, c);
    if (sel->setop) return apply_setop(c, sel);

    /* VALUES list */
    if (sel->nvalrows) {
        RowSet *rs = rs_new();
        Oid *ts = xmalloc((size_t)(sel->nvalcols ? sel->nvalcols : 1) * sizeof(Oid));
        for (int i = 0; i < sel->nvalcols; i++) ts[i] = 0;
        Datum **rows = xmalloc((size_t)sel->nvalrows * sizeof(Datum *));
        for (int i = 0; i < sel->nvalrows; i++) {
            rows[i] = row_new(sel->nvalcols);
            for (int k = 0; k < sel->nvalcols; k++) {
                rows[i][k] = eval(c, sel->values[i][k]);
                ts[k] = common_type(s, ts[k], rows[i][k].type);
            }
        }
        for (int k = 0; k < sel->nvalcols; k++) {
            char nm[16]; snprintf(nm, sizeof nm, "column%d", k + 1);
            rs_addcol(rs, NULL, nm, ts[k] == UNKNOWNOID || !ts[k] ? TEXTOID : ts[k], -1);
        }
        for (int i = 0; i < sel->nvalrows; i++) {
            for (int k = 0; k < sel->nvalcols; k++) {
                Oid ft = rows[i][k].type, tt = rs->cols[k].type;
                /* a range or multirange of another family cannot be converted */
                if (ft != tt && (type_is_rangey(ft) || type_is_rangey(tt))) {
                    Expr *ve2 = sel->values[i][k];
                    while (ve2->tag == X_CAST && ve2->nargs) ve2 = ve2->args[0];
                    ereport_pos(s, "42804", ve2->pos,
                                "VALUES could not convert type %s to %s",
                                format_type_str(ft, -1), format_type_str(tt, -1));
                }
                rows[i][k] = coerce_to(s, rows[i][k], tt, -1, 1);
            }
            rs_addrow(rs, rows[i]);
        }
        goto sort_and_limit_values;
    sort_and_limit_values:
        if (sel->norderby) {
            SortKey *keys = xmalloc((size_t)sel->norderby * sizeof(SortKey));
            for (int i = 0; i < sel->norderby; i++) {
                keys[i].e = sel->orderby[i].e; keys[i].dir = sel->orderby[i].dir;
                keys[i].nulls = sel->orderby[i].nulls; keys[i].tgtidx = -1;
                if (sel->orderby[i].e->tag == X_CONST && sel->orderby[i].e->val.type == INT4OID)
                    keys[i].tgtidx = (int)sel->orderby[i].e->val.v.i - 1;
            }
            SortCtx sc = { c, keys, sel->norderby, rs };
            g_sortctx = &sc;
            pg_sort_rows(rs->rows, (size_t)rs->nrows);
        }
        if (sel->limit || sel->offset) {
            int64 off = 0, lim = -1;
            if (sel->offset) { Datum d = eval(c, sel->offset); if (!d.isnull) off = coerce_to(s,d,INT8OID,-1,1).v.i; }
            if (sel->limit)  { Datum d = eval(c, sel->limit);  if (!d.isnull) lim = coerce_to(s,d,INT8OID,-1,1).v.i; }
            int start = (int)(off < 0 ? 0 : off);
            if (start > rs->nrows) start = rs->nrows;
            int end = (lim < 0) ? rs->nrows : start + (int)lim;
            if (end > rs->nrows) end = rs->nrows;
            Datum **nr = xmalloc((size_t)((end - start) ? (end - start) : 1) * sizeof(Datum *));
            for (int i = start; i < end; i++) nr[i - start] = rs->rows[i];
            rs->rows = nr; rs->nrows = end - start;
        }
        return rs;
    }

    /* FROM */
    RowSet *src;
    if (sel->nfrom == 0) {
        src = rs_new();
        rs_addrow(src, row_new(0));
    } else {
        src = exec_from(c, sel->from[0]);
        for (int i = 1; i < sel->nfrom; i++) {
            RangeTbl *rt = sel->from[i];
            if (!rt->lateral) { src = cross_join(src, exec_from(c, rt)); continue; }
            RowSet *acc = NULL;
            for (int r = 0; r < src->nrows; r++) {
                ExecCtx lc = *c; lc.rs = src; lc.row = src->rows[r]; lc.outer = c0;
                RowSet *b = exec_from(&lc, rt);
                if (!acc) {
                    acc = rs_new();
                    for (int k = 0; k < src->ncols; k++) rs_addcol_meta(acc, src->cols[k]);
                    for (int k = 0; k < b->ncols; k++) rs_addcol_meta(acc, b->cols[k]);
                }
                for (int k = 0; k < b->nrows; k++) {
                    Datum *row = xmalloc((size_t)acc->ncols * sizeof(Datum));
                    memcpy(row, src->rows[r], (size_t)src->ncols * sizeof(Datum));
                    memcpy(row + src->ncols, b->rows[k], (size_t)b->ncols * sizeof(Datum));
                    rs_addrow(acc, row);
                }
            }
            if (!acc) { acc = rs_new(); for (int k = 0; k < src->ncols; k++) rs_addcol_meta(acc, src->cols[k]); }
            src = acc;
        }
    }
    c->rs = src;

    /* WHERE */
    if (sel->where) {
        {
            Expr **wa = NULL; int nwa = 0;
            collect_aggs(sel->where, &wa, &nwa);
            if (nwa) ereport_pos(s, "42803", wa[0]->pos,
                                 "aggregate functions are not allowed in WHERE");
            free(wa);
        }
        RowSet *f = rs_new();
        f->ncols = src->ncols; f->cols = src->cols;
        for (int i = 0; i < src->nrows; i++) {
            ExecCtx wc = *c; wc.rs = src; wc.row = src->rows[i]; wc.outer = c0;
            if ((i & 0x3ff) == 0) check_cancel(s);
            if (datum_true(to_bool(s, eval(&wc, sel->where)))) rs_addrow(f, src->rows[i]);
        }
        src = f;
        c->rs = src;
    }

    /* expand * in the target list */
    Expr **tgt = NULL; int ntgt = 0;
    char **labels = NULL;
    for (int i = 0; i < sel->ntarget; i++) {
        Expr *t = sel->target[i];
        if (t->tag == X_STAR) {
            for (int k = 0; k < src->ncols; k++) {
                if (src->cols[k].hidden) continue;
                if (t->qual && (!src->cols[k].rel || strcmp(src->cols[k].rel, t->qual))) continue;
                Expr *cr = xmalloc(sizeof *cr);
                memset(cr, 0, sizeof *cr);
                cr->tag = X_COLREF; cr->colname = xstrdup(src->cols[k].name);
                cr->qual = src->cols[k].rel ? xstrdup(src->cols[k].rel) : NULL;
                cr->pos = t->pos;
                tgt = xrealloc(tgt, (size_t)(ntgt + 1) * sizeof(Expr *));
                labels = xrealloc(labels, (size_t)(ntgt + 1) * sizeof(char *));
                labels[ntgt] = xstrdup(src->cols[k].name);
                tgt[ntgt++] = cr;
            }
            if (t->qual && ntgt == 0)
                ereport_pos(s, "42P01", t->pos, "missing FROM-clause entry for table \"%s\"", t->qual);
            continue;
        }
        tgt = xrealloc(tgt, (size_t)(ntgt + 1) * sizeof(Expr *));
        labels = xrealloc(labels, (size_t)(ntgt + 1) * sizeof(char *));
        labels[ntgt] = xstrdup(expr_label(t));
        tgt[ntgt++] = t;
    }

    /* ORDER BY entries that contain an aggregate or a window function have to
     * be computed with the projection, so add them as hidden target columns. */
    int nvisible = ntgt;
    int *ob_tgt = NULL;
    if (sel->norderby) {
        ob_tgt = xmalloc((size_t)sel->norderby * sizeof(int));
        for (int i = 0; i < sel->norderby; i++) {
            ob_tgt[i] = -1;
            Expr *oe = sel->orderby[i].e;
            if (!expr_has_agg(oe) && !expr_has_window(oe)) continue;
            bool same = false;
            for (int k = 0; k < ntgt; k++) if (tgt[k] == oe) { ob_tgt[i] = k; same = true; break; }
            if (same) continue;
            tgt = xrealloc(tgt, (size_t)(ntgt + 1) * sizeof(Expr *));
            labels = xrealloc(labels, (size_t)(ntgt + 1) * sizeof(char *));
            labels[ntgt] = xstrdup("?sortkey?");
            tgt[ntgt] = oe;
            ob_tgt[i] = ntgt;
            ntgt++;
        }
    }

    /* aggregates / grouping */
    bool has_agg = (sel->ngroupby > 0) || (sel->having != NULL);
    for (int i = 0; i < ntgt && !has_agg; i++) if (expr_has_agg(tgt[i])) has_agg = true;

    RowSet *out = rs_new();
    Expr **wins = NULL; int nwins = 0;
    for (int i = 0; i < ntgt; i++) collect_wins(tgt[i], &wins, &nwins);
    for (int i = 0; i < sel->norderby; i++) collect_wins(sel->orderby[i].e, &wins, &nwins);
    for (int i = 0; i < nwins; i++) {
        WindowDef *w = wins[i]->over;
        if (!w || !w->refname) continue;
        WindowDef *named = NULL;
        for (int k = 0; k < sel->nwindows; k++)
            if (sel->windows[k]->name && !strcmp(sel->windows[k]->name, w->refname)) named = sel->windows[k];
        if (!named) ereport_pos(s, "42704", wins[i]->pos, "window \"%s\" does not exist", w->refname);
        WindowDef *merged = xmalloc(sizeof *merged);
        *merged = *named;
        if (w->norder) { merged->order = w->order; merged->norder = w->norder; }
        if (w->frame_start >= 0) { merged->frame_mode = w->frame_mode;
                                   merged->frame_start = w->frame_start; merged->frame_end = w->frame_end;
                                   merged->start_off = w->start_off; merged->end_off = w->end_off; }
        merged->refname = NULL;
        wins[i]->over = merged;
    }

    if (has_agg) {
        /* a grouping expression is resolved before anything else */
        for (int k = 0; k < sel->ngroupby; k++) {
            Expr *g = sel->groupby[k];
            if (g->tag != X_COLREF) continue;
            if (find_col(src, g->qual, g->colname, s, g->pos) < 0)
                ereport_pos(s, "42703", g->pos, "column \"%s\" does not exist", g->colname);
        }
        /* every bare column reference must be grouped or aggregated */
        {
            Expr **bare = NULL; int nbare = 0;
            for (int i = 0; i < ntgt; i++) collect_bare_cols(tgt[i], &bare, &nbare);
            collect_bare_cols(sel->having, &bare, &nbare);
            for (int i = 0; i < nbare; i++) {
                bool grouped = false;
                for (int k = 0; k < sel->ngroupby && !grouped; k++) {
                    Expr *g = sel->groupby[k];
                    if (g->tag == X_CONST && g->val.type == INT4OID &&
                        g->val.v.i >= 1 && g->val.v.i <= ntgt)
                        g = tgt[g->val.v.i - 1];        /* GROUP BY ordinal */
                    if (g == bare[i]) { grouped = true; break; }
                    if (g->tag == X_COLREF && !strcmp(g->colname, bare[i]->colname) &&
                        (!g->qual || !bare[i]->qual || !strcmp(g->qual, bare[i]->qual)))
                        grouped = true;
                    if (g->tag == X_LIST) grouped = true;
                    if (!grouped) {
                        /* grouping by a whole expression groups every column in it */
                        Expr **gb = NULL; int ngb = 0;
                        collect_bare_cols(g, &gb, &ngb);
                        for (int q = 0; q < ngb; q++)
                            if (gb[q] == bare[i] ||
                                (gb[q]->colname && bare[i]->colname &&
                                 !strcmp(gb[q]->colname, bare[i]->colname) &&
                                 (!gb[q]->qual || !bare[i]->qual ||
                                  !strcmp(gb[q]->qual, bare[i]->qual))))
                                grouped = true;
                        free(gb);
                    }
                }
                if (grouped) continue;
                int ci = find_col(src, bare[i]->qual, bare[i]->colname, s, bare[i]->pos);
                const char *rel = (ci >= 0 && src->cols[ci].rel) ? src->cols[ci].rel : NULL;
                if (rel)
                    ereport_pos(s, "42803", bare[i]->pos,
                                "column \"%s.%s\" must appear in the GROUP BY clause "
                                "or be used in an aggregate function", rel, bare[i]->colname);
                ereport_pos(s, "42803", bare[i]->pos,
                            "column \"%s\" must appear in the GROUP BY clause "
                            "or be used in an aggregate function", bare[i]->colname);
            }
            free(bare);
        }
        Expr **aggs = NULL; int naggs = 0;
        for (int i = 0; i < ntgt; i++) collect_aggs(tgt[i], &aggs, &naggs);
        collect_aggs(sel->having, &aggs, &naggs);
        for (int i = 0; i < sel->norderby; i++) collect_aggs(sel->orderby[i].e, &aggs, &naggs);

        /* group rows, once per grouping set */
        bool have_sets = false;
        for (int k = 0; k < sel->ngroupby; k++)
            if (sel->groupby[k]->tag == X_LIST) have_sets = true;
        GSetList gsl; memset(&gsl, 0, sizeof gsl);
        if (have_sets || sel->group_distinct) gs_expand(sel, &gsl);
        else gs_add(&gsl, sel->groupby, sel->ngroupby);
        for (int i = 0; i < ntgt; i++) {
            Oid ty = TEXTOID;
            rs_addcol(out, NULL, labels[i], ty, -1);
        }
        bool firstrow = true;
      for (int gsi = 0; gsi < gsl.cnt; gsi++) {
        Expr **gexpr = gsl.set[gsi];
        int ngrp = gsl.n[gsi];
        int **groups = NULL; int *gsize = NULL; int ngroups = 0;
        Datum **gkeys = NULL;
        for (int i = 0; i < src->nrows; i++) {
            if ((i & 0x3ff) == 0) check_cancel(s);
            ExecCtx gc = *c; gc.rs = src; gc.row = src->rows[i]; gc.outer = c0;
            Datum *key = row_new(ngrp ? ngrp : 1);
            for (int k = 0; k < ngrp; k++) {
                Expr *g = gexpr[k];
                if (g->tag == X_CONST && g->val.type == INT4OID && g->val.v.i >= 1 &&
                    g->val.v.i <= ntgt) key[k] = eval(&gc, tgt[g->val.v.i - 1]);
                else key[k] = eval(&gc, g);
            }
            int found = -1;
            for (int k = 0; k < ngroups && ngrp; k++)
                if (rows_equal(s, gkeys[k], key, ngrp)) { found = k; break; }
            if (!ngrp && ngroups) found = 0;
            if (found < 0) {
                groups = xrealloc(groups, (size_t)(ngroups + 1) * sizeof(int *));
                gsize  = xrealloc(gsize,  (size_t)(ngroups + 1) * sizeof(int));
                gkeys  = xrealloc(gkeys,  (size_t)(ngroups + 1) * sizeof(Datum *));
                groups[ngroups] = NULL; gsize[ngroups] = 0; gkeys[ngroups] = key;
                found = ngroups++;
            }
            groups[found] = xrealloc(groups[found], (size_t)(gsize[found] + 1) * sizeof(int));
            groups[found][gsize[found]++] = i;
        }
        if (ngroups == 0 && ngrp == 0) {
            groups = xmalloc(sizeof(int *)); groups[0] = NULL;
            gsize = xmalloc(sizeof(int)); gsize[0] = 0;
            gkeys = xmalloc(sizeof(Datum *)); gkeys[0] = row_new(1);
            ngroups = 1;
        }
        for (int g = 0; g < ngroups; g++) {
            ExecCtx ac = *c;
            ac.rs = src;
            ac.row = gsize[g] ? src->rows[groups[g][0]] : row_new(src->ncols);
            ac.outer = c0;
            ac.aggexprs = aggs; ac.naggexprs = naggs;
            ac.aggvals = xmalloc((size_t)(naggs ? naggs : 1) * sizeof(Datum));
            for (int k = 0; k < naggs; k++)
                ac.aggvals[k] = agg_compute(&ac, aggs[k], src, groups[g], gsize[g]);
            ac.agg_phase = 2;
            if (sel->having && !datum_true(to_bool(s, eval(&ac, sel->having)))) continue;
            Datum *row = row_new(ntgt);
            for (int i = 0; i < ntgt; i++) {
                row[i] = eval(&ac, tgt[i]);
                /* a grouping column this set does not group by reads as NULL */
                if (gsl.cnt > 1) {
                    bool dropped = false, ingroup = false;
                    for (int k = 0; k < sel->ngroupby; k++) {
                        Expr *gg = sel->groupby[k];
                        Expr *flat[32]; int nfl = 0;
                        if (gg->tag == X_LIST) {
                            for (int q = 0; q < gg->nargs && nfl < 32; q++) {
                                Expr *a2 = gg->args[q];
                                if (a2->tag == X_LIST) {
                                    for (int w = 0; w < a2->nargs && nfl < 32; w++) flat[nfl++] = a2->args[w];
                                } else flat[nfl++] = a2;
                            }
                        } else flat[nfl++] = gg;
                        for (int q = 0; q < nfl; q++)
                            if (gs_same_expr(flat[q], tgt[i])) dropped = true;
                    }
                    for (int k = 0; k < ngrp; k++)
                        if (gs_same_expr(gexpr[k], tgt[i])) ingroup = true;
                    if (dropped && !ingroup) row[i] = d_null(row[i].type);
                }
                if (firstrow || out->cols[i].type == TEXTOID)
                    if (row[i].type) out->cols[i].type = row[i].type;
            }
            firstrow = false;
            rs_addrow(out, row);
        }
      }
        /* fix column types from the first row */
        for (int i = 0; i < ntgt && out->nrows; i++)
            if (out->rows[0][i].type) out->cols[i].type = out->rows[0][i].type;
    } else {
        for (int i = 0; i < ntgt; i++) rs_addcol(out, NULL, labels[i], TEXTOID, -1);
        /* window functions need the full row set first */
        Datum **winvals = NULL;
        if (nwins) {
            extern void compute_windows(ExecCtx *c, RowSet *src, Expr **wins, int nwins, Datum ***out);
            compute_windows(c, src, wins, nwins, &winvals);
        }
        Expr **srfs = NULL; int nsrf = 0;
        for (int i = 0; i < ntgt; i++) collect_srfs(tgt[i], &srfs, &nsrf);
        for (int i = 0; i < src->nrows; i++) {
            if ((i & 0x3ff) == 0) check_cancel(s);
            ExecCtx rc = *c; rc.rs = src; rc.row = src->rows[i]; rc.outer = c0;
            if (nwins) {
                rc.aggexprs = wins; rc.naggexprs = nwins;
                rc.aggvals = winvals[i]; rc.agg_phase = 2;
            }
            if (!nsrf) {
                Datum *row = row_new(ntgt);
                for (int k = 0; k < ntgt; k++) row[k] = eval(&rc, tgt[k]);
                rs_addrow(out, row);
                continue;
            }
            /* evaluate each set-returning target once, then emit one row per element */
            Datum *sets = xmalloc((size_t)nsrf * sizeof(Datum));
            int *lens = xmalloc((size_t)nsrf * sizeof(int));
            int maxn = 0;
            for (int k = 0; k < nsrf; k++) {
                ExecCtx sc = rc; sc.agg_phase = 0;
                sets[k] = eval(&sc, srfs[k]);
                lens[k] = (!sets[k].isnull && type_is_array(sets[k].type)) ? sets[k].v.arr->nelem : 1;
                if (lens[k] > maxn) maxn = lens[k];
            }
#if PGVC_VN < 100
            /* before PostgreSQL 10 the row count is the least common multiple */
            int total = 1;
            for (int k = 0; k < nsrf; k++) {
                int a = total, b2 = lens[k] ? lens[k] : 1;
                while (b2) { int t = a % b2; a = b2; b2 = t; }
                total = (a > 0) ? total / a * (lens[k] ? lens[k] : 1) : total;
            }
#else
            int total = maxn;
#endif
            Datum *slots = xmalloc((size_t)nsrf * sizeof(Datum));
            ExecCtx ec = rc;
            ec.aggexprs = srfs; ec.naggexprs = nsrf; ec.aggvals = slots; ec.agg_phase = 2;
            for (int r2 = 0; r2 < total; r2++) {
                for (int k = 0; k < nsrf; k++) {
                    if (sets[k].isnull || !type_is_array(sets[k].type)) { slots[k] = sets[k]; continue; }
                    ArrayVal *av = sets[k].v.arr;
                    if (!av->nelem) { slots[k] = d_null(av->elemtype); continue; }
#if PGVC_VN < 100
                    slots[k] = av->elems[r2 % av->nelem];
#else
                    slots[k] = (r2 < av->nelem) ? av->elems[r2] : d_null(av->elemtype);
#endif
                }
                Datum *row = row_new(ntgt);
                for (int k = 0; k < ntgt; k++) row[k] = eval(&ec, tgt[k]);
                rs_addrow(out, row);
            }
            free(sets); free(lens);
        }
        for (int i = 0; i < ntgt; i++) {
            Oid ty = 0;
            for (int k = 0; k < out->nrows; k++) ty = common_type(s, ty, out->rows[k][i].type);
            if (!ty || ty == UNKNOWNOID) {
                if (tgt[i]->tag == X_COLREF) {
                    int ci = find_col(src, tgt[i]->qual, tgt[i]->colname, s, tgt[i]->pos);
                    if (ci >= 0) { out->cols[i].type = src->cols[ci].type;
                                   out->cols[i].typmod = src->cols[ci].typmod; continue; }
                }
                /* a bare NULL stays unknown so an enclosing UNION can resolve it */
                if (tgt[i]->tag == X_CONST && tgt[i]->isnull_const) { out->cols[i].type = UNKNOWNOID; continue; }
                ty = TEXTOID;
            }
            out->cols[i].type = ty;
            if (tgt[i]->tag == X_COLREF) {
                int ci = find_col(src, tgt[i]->qual, tgt[i]->colname, s, tgt[i]->pos);
                if (ci >= 0) out->cols[i].typmod = src->cols[ci].typmod;
            } else if (tgt[i]->tag == X_CAST) out->cols[i].typmod = tgt[i]->casttypmod;
        }
        for (int i = 0; i < out->nrows; i++)
            for (int k = 0; k < ntgt; k++)
                if (out->rows[i][k].type != out->cols[k].type && !out->rows[i][k].isnull)
                    out->rows[i][k] = coerce_to(s, out->rows[i][k], out->cols[k].type, -1, 1);
    }

    /* ORDER BY (may reference target list entries or source columns) */
    SortKey *ob_keys = NULL;                  /* kept for FETCH ... WITH TIES */
    RowSet  *ob_rs = NULL;
    if (sel->norderby) {
        SortKey *keys = xmalloc((size_t)sel->norderby * sizeof(SortKey));
        RowSet *sortsrc = out;
        bool use_src = false;
        /* PostgreSQL hands the row count from LIMIT to the sort node, which
         * then runs a bounded top-N heap sort instead of a quicksort; the two
         * leave rows with equal keys in a different order.  The bound does not
         * reach the sort through DISTINCT, a row lock or WITH TIES. */
        int64 sortbound = -1;
        if (sel->limit && !sel->with_ties && !sel->distinct && !sel->lock_kind) {
            Datum dl = eval(c, sel->limit);
            if (!dl.isnull) {
                int64 lv = coerce_to(s, dl, INT8OID, -1, 1).v.i, ov = 0;
                if (sel->offset) {
                    Datum d2 = eval(c, sel->offset);
                    if (!d2.isnull) ov = coerce_to(s, d2, INT8OID, -1, 1).v.i;
                }
                if (lv >= 0 && ov >= 0) sortbound = lv + ov;
            }
        }
        for (int i = 0; i < sel->norderby; i++) {
            Expr *e = sel->orderby[i].e;
            keys[i].e = e; keys[i].dir = sel->orderby[i].dir;
            keys[i].nulls = sel->orderby[i].nulls; keys[i].tgtidx = -1;
            if (ob_tgt && ob_tgt[i] >= 0) { keys[i].tgtidx = ob_tgt[i]; continue; }
            if (e->tag == X_CONST && e->val.type == INT4OID) {
                int n = (int)e->val.v.i;
                if (n < 1 || n > nvisible) ereport_pos(s, "42P10", e->pos,
                    "ORDER BY position %d is not in select list", n);
                keys[i].tgtidx = n - 1;
                continue;
            }
            if (e->tag == X_COLREF && !e->qual) {
                int f = -1;
                for (int k = 0; k < ntgt; k++) if (!strcmp(labels[k], e->colname)) { f = k; break; }
                if (f >= 0) { keys[i].tgtidx = f; continue; }
            }
            use_src = true;
        }
        if (use_src && !has_agg && src->nrows == out->nrows) {
            /* sort the projected rows using source values: build a combined set */
            RowSet *comb = rs_new();
            for (int k = 0; k < out->ncols; k++)
                rs_addcol(comb, NULL, out->cols[k].name, out->cols[k].type, out->cols[k].typmod);
            for (int k = 0; k < src->ncols; k++) {
                rs_addcol(comb, src->cols[k].rel, src->cols[k].name, src->cols[k].type, src->cols[k].typmod);
                comb->cols[comb->ncols-1].hidden = true;
            }
            for (int i = 0; i < out->nrows; i++) {
                Datum *row = xmalloc((size_t)comb->ncols * sizeof(Datum));
                memcpy(row, out->rows[i], (size_t)out->ncols * sizeof(Datum));
                memcpy(row + out->ncols, src->rows[i], (size_t)src->ncols * sizeof(Datum));
                rs_addrow(comb, row);
            }
            static SortCtx sc; sc.c = c; sc.keys = keys;
            sc.nkeys = sel->norderby; sc.rs = comb;
            g_sortctx = &sc;
            size_t left = pg_sort_rows_bounded(comb->rows, (size_t)comb->nrows, sortbound);
            comb->nrows = (int)left;
            if (out->nrows > (int)left) out->nrows = (int)left;
            for (int i = 0; i < out->nrows; i++) out->rows[i] = comb->rows[i];
            ob_keys = keys; ob_rs = comb;
        } else {
            static SortCtx sc; sc.c = c; sc.keys = keys;
            sc.nkeys = sel->norderby; sc.rs = sortsrc;
            g_sortctx = &sc;
            size_t left = pg_sort_rows_bounded(out->rows, (size_t)out->nrows, sortbound);
            out->nrows = (int)left;
            ob_keys = keys; ob_rs = sortsrc;
        }
    }

    for (int i = nvisible; i < ntgt; i++) out->cols[i].hidden = true;

    /* DISTINCT */
    if (sel->distinct) {
        RowSet *d = rs_new();
        d->ncols = out->ncols; d->cols = out->cols;
        int ncmp = sel->ndistinct_on ? sel->ndistinct_on : out->ncols;
        for (int i = 0; i < out->nrows; i++) {
            bool dup = false;
            for (int k = 0; k < d->nrows; k++) {
                if (sel->ndistinct_on) {
                    bool same = true;
                    for (int q = 0; q < sel->ndistinct_on && same; q++) {
                        ExecCtx a = *c; a.rs = out; a.row = out->rows[i];
                        ExecCtx b = *c; b.rs = out; b.row = d->rows[k];
                        Datum x = eval(&a, sel->distinct_on[q]), y = eval(&b, sel->distinct_on[q]);
                        if (x.isnull != y.isnull || (!x.isnull && d_cmp(s, x, y) != 0)) same = false;
                    }
                    if (same) { dup = true; break; }
                } else if (rows_equal(s, out->rows[i], d->rows[k], ncmp)) { dup = true; break; }
            }
            if (!dup) rs_addrow(d, out->rows[i]);
        }
        out = d;
    }

    /* FOR UPDATE / FOR SHARE take a row lock, so a concurrent writer blocks */
    if (sel->lock_kind && sel->nfrom == 1 && sel->from[0]->kind == 0) {
        Table *lt = tbl_find_sp(s, sel->from[0]->schema, sel->from[0]->relname);
        if (lt) {
            for (int i = 0; i < lt->nrows; i++) {
                Row *rw = lt->rows[i];
                if (!row_visible(s, rw)) continue;
                bool wanted = false;
                for (int k = 0; k < src->nrows && !wanted; k++) {
                    bool same = true;
                    for (int q = 0, cq = 0; q < lt->ncols && same; q++) {
                        if (lt->cols[q].dropped) continue;
                        Datum a = src->rows[k][cq], b2 = rw->vals[q];
                        if (a.isnull != b2.isnull || (!a.isnull && d_cmp(s, a, b2) != 0)) same = false;
                        cq++;
                    }
                    if (same) wanted = true;
                }
                if (!wanted) continue;
                row_lock_wait(s, rw);
                row_mark_locked(s, rw);
            }
        }
    }

    /* LIMIT / OFFSET / WITH TIES */
    if (sel->limit || sel->offset) {
        int64 off = 0, lim = -1;
        if (sel->offset) { Datum d = eval(c, sel->offset); if (!d.isnull) off = coerce_to(s,d,INT8OID,-1,1).v.i; }
        if (sel->limit) {
            Datum d = eval(c, sel->limit);
            if (!d.isnull) lim = coerce_to(s,d,INT8OID,-1,1).v.i;
            if (lim < 0) ereport(s, "2201W", "LIMIT must not be negative");
        }
        int start = (int)(off < 0 ? 0 : off);
        if (start > out->nrows) start = out->nrows;
        int end = (lim < 0) ? out->nrows : start + (int)lim;
        if (end > out->nrows) end = out->nrows;
        if (sel->with_ties && sel->norderby && ob_keys && end < out->nrows && end > start) {
            /* peers of the last returned row join it, compared with exactly the
             * keys the sort used (which may live in hidden source columns) */
            static SortCtx sc; sc.c = c; sc.keys = ob_keys;
            sc.nkeys = sel->norderby; sc.rs = ob_rs;
            g_sortctx = &sc;
            while (end < out->nrows &&
                   sort_cmp(&out->rows[end - 1], &out->rows[end]) == 0) end++;
        }
        Datum **nr = xmalloc((size_t)((end - start) > 0 ? (end - start) : 1) * sizeof(Datum *));
        for (int i = start; i < end; i++) nr[i - start] = out->rows[i];
        out->rows = nr; out->nrows = (end - start) > 0 ? end - start : 0;
    }
    return out;
}

static RowSet *apply_setop(ExecCtx *c, SelectStmt *sel)
{
    Session *s = c->s;
    RowSet *L = exec_select(c, sel->larg);
    RowSet *R = exec_select(c, sel->rarg);
    if (L->ncols != R->ncols) {
        int pos = 0;
        SelectStmt *ra = sel->rarg;
        while (ra && ra->setop) ra = ra->larg;
        if (ra && ra->ntarget) pos = ra->target[ra->ntarget - 1]->pos;
        ereport_pos(s, "42601", pos, "each %s query must have the same number of columns",
                    sel->setop == 1 ? "UNION" : sel->setop == 2 ? "INTERSECT" : "EXCEPT");
    }
    RowSet *out = rs_new();
    for (int i = 0; i < L->ncols; i++) {
        Oid t = common_type(s, L->cols[i].type, R->cols[i].type);
        rs_addcol(out, NULL, L->cols[i].name, t == UNKNOWNOID ? TEXTOID : t, L->cols[i].typmod);
    }
    for (int i = 0; i < L->nrows; i++)
        for (int k = 0; k < out->ncols; k++)
            L->rows[i][k] = coerce_to(s, L->rows[i][k], out->cols[k].type, -1, 1);
    for (int i = 0; i < R->nrows; i++)
        for (int k = 0; k < out->ncols; k++)
            R->rows[i][k] = coerce_to(s, R->rows[i][k], out->cols[k].type, -1, 1);

    if (sel->setop == 1) {
        for (int i = 0; i < L->nrows; i++) rs_addrow(out, L->rows[i]);
        for (int i = 0; i < R->nrows; i++) rs_addrow(out, R->rows[i]);
    } else if (sel->setop == 2) {
        for (int i = 0; i < L->nrows; i++) {
            bool hit = false;
            for (int k = 0; k < R->nrows && !hit; k++)
                if (rows_equal(s, L->rows[i], R->rows[k], out->ncols)) hit = true;
            if (hit) rs_addrow(out, L->rows[i]);
        }
    } else {
        int *used = xmalloc((size_t)(R->nrows ? R->nrows : 1) * sizeof(int));
        memset(used, 0, (size_t)(R->nrows ? R->nrows : 1) * sizeof(int));
        for (int i = 0; i < L->nrows; i++) {
            bool hit = false;
            for (int k = 0; k < R->nrows && !hit; k++) {
                if (sel->setop_all && used[k]) continue;
                if (rows_equal(s, L->rows[i], R->rows[k], out->ncols)) { hit = true; used[k] = 1; }
            }
            if (!hit) rs_addrow(out, L->rows[i]);
        }
        free(used);
    }
    if (!sel->setop_all) {
        RowSet *d = rs_new();
        d->ncols = out->ncols; d->cols = out->cols;
        for (int i = 0; i < out->nrows; i++) {
            bool dup = false;
            for (int k = 0; k < d->nrows && !dup; k++)
                if (rows_equal(s, out->rows[i], d->rows[k], out->ncols)) dup = true;
            if (!dup) rs_addrow(d, out->rows[i]);
        }
        out = d;
    }
    if (sel->norderby) {
        SortKey *keys = xmalloc((size_t)sel->norderby * sizeof(SortKey));
        for (int i = 0; i < sel->norderby; i++) {
            keys[i].e = sel->orderby[i].e; keys[i].dir = sel->orderby[i].dir;
            keys[i].nulls = sel->orderby[i].nulls; keys[i].tgtidx = -1;
            Expr *e = sel->orderby[i].e;
            if (e->tag == X_CONST && e->val.type == INT4OID) keys[i].tgtidx = (int)e->val.v.i - 1;
            else if (e->tag == X_COLREF)
                for (int k = 0; k < out->ncols; k++)
                    if (!strcmp(out->cols[k].name, e->colname)) keys[i].tgtidx = k;
        }
        SortCtx sc = { c, keys, sel->norderby, out };
        g_sortctx = &sc;
        pg_sort_rows(out->rows, (size_t)out->nrows);
    }
    if (sel->limit || sel->offset) {
        int64 off = 0, lim = -1;
        if (sel->offset) { Datum d = eval(c, sel->offset); if (!d.isnull) off = coerce_to(s,d,INT8OID,-1,1).v.i; }
        if (sel->limit)  { Datum d = eval(c, sel->limit);  if (!d.isnull) lim = coerce_to(s,d,INT8OID,-1,1).v.i; }
        int start = (int)(off < 0 ? 0 : off);
        if (start > out->nrows) start = out->nrows;
        int end = (lim < 0) ? out->nrows : start + (int)lim;
        if (end > out->nrows) end = out->nrows;
        Datum **nr = xmalloc((size_t)((end - start) > 0 ? (end - start) : 1) * sizeof(Datum *));
        for (int i = start; i < end; i++) nr[i - start] = out->rows[i];
        out->rows = nr; out->nrows = (end - start) > 0 ? end - start : 0;
    }
    return out;
}

/* ------------------------------------------------------------------ CTEs */
/* ----------------------------------------------------------- CTE inlining
 * From PostgreSQL 12 a WITH query that is referenced once, or that was
 * written NOT MATERIALIZED, is folded into the enclosing query instead of
 * being evaluated once into a tuplestore.  Two things become visible:
 * the sub-query runs once per reference site rather than once, and output
 * columns nobody reads are never evaluated at all.
 */
typedef struct CteScan {
    const char **names; int nnames;   /* every column name the query mentions */
    bool  star;                       /* a * appeared, so assume all are read */
    const char *cte; int refs;        /* references to one particular WITH name */
} CteScan;

static void cs_name(CteScan *r, const char *n)
{
    if (!n) return;
    for (int i = 0; i < r->nnames; i++) if (!strcmp(r->names[i], n)) return;
    r->names = xrealloc(r->names, (size_t)(r->nnames + 1) * sizeof(char *));
    r->names[r->nnames++] = n;
}

static bool cs_uses(CteScan *r, const char *n)
{
    if (r->star || !n) return true;
    for (int i = 0; i < r->nnames; i++) if (!strcmp(r->names[i], n)) return true;
    return false;
}

static void cs_sel(CteScan *r, SelectStmt *sel);
static void cs_node(CteScan *r, Node *n);

static void cs_expr(CteScan *r, Expr *e)
{
    if (!e) return;
    if (e->tag == X_COLREF) cs_name(r, e->colname);
    else if (e->tag == X_STAR) r->star = true;
    for (int i = 0; i < e->nargs; i++) cs_expr(r, e->args[i]);
    cs_expr(r, e->case_arg); cs_expr(r, e->case_else);
    for (int i = 0; i < e->nwhen; i++) { cs_expr(r, e->when_cond[i]); cs_expr(r, e->when_res[i]); }
    cs_expr(r, e->filter); cs_expr(r, e->testexpr); cs_expr(r, e->like_escape);
    for (int i = 0; i < e->agg_norder; i++) cs_expr(r, e->agg_order[i].e);
    if (e->over) {
        for (int i = 0; i < e->over->npartition; i++) cs_expr(r, e->over->partition[i]);
        for (int i = 0; i < e->over->norder; i++) cs_expr(r, e->over->order[i].e);
        cs_expr(r, e->over->start_off); cs_expr(r, e->over->end_off);
    }
    if (e->subquery) cs_node(r, e->subquery);
}

static void cs_rt(CteScan *r, RangeTbl *rt)
{
    if (!rt) return;
    if (rt->kind == 0 && !rt->schema && r->cte && rt->relname &&
        !strcmp(rt->relname, r->cte)) r->refs++;
    cs_expr(r, rt->funcexpr); cs_expr(r, rt->joinqual);
    if (rt->subquery) cs_node(r, rt->subquery);
    cs_rt(r, rt->left); cs_rt(r, rt->right);
    for (int i = 0; i < rt->nvalrows; i++)
        for (int k = 0; k < rt->nvalcols; k++) cs_expr(r, rt->values[i][k]);
    if (rt->xmlt) {
        cs_expr(r, rt->xmlt->rowexpr); cs_expr(r, rt->xmlt->docexpr);
        for (int i = 0; i < rt->xmlt->ncols; i++) {
            cs_expr(r, rt->xmlt->cols[i].pathexpr);
            cs_expr(r, rt->xmlt->cols[i].defaultexpr);
        }
    }
    for (int i = 0; i < rt->nusing; i++) cs_name(r, rt->usingcols[i]);
    if (rt->natural) r->star = true;
}

static void cs_sel(CteScan *r, SelectStmt *sel)
{
    if (!sel) return;
    for (int i = 0; i < sel->ntarget; i++) cs_expr(r, sel->target[i]);
    for (int i = 0; i < sel->ndistinct_on; i++) cs_expr(r, sel->distinct_on[i]);
    for (int i = 0; i < sel->nfrom; i++) cs_rt(r, sel->from[i]);
    cs_expr(r, sel->where);
    for (int i = 0; i < sel->ngroupby; i++) cs_expr(r, sel->groupby[i]);
    cs_expr(r, sel->having);
    for (int i = 0; i < sel->nwindows; i++) {
        WindowDef *w = sel->windows[i];
        for (int k = 0; k < w->npartition; k++) cs_expr(r, w->partition[k]);
        for (int k = 0; k < w->norder; k++) cs_expr(r, w->order[k].e);
        cs_expr(r, w->start_off); cs_expr(r, w->end_off);
    }
    for (int i = 0; i < sel->norderby; i++) cs_expr(r, sel->orderby[i].e);
    cs_expr(r, sel->limit); cs_expr(r, sel->offset);
    for (int i = 0; i < sel->nvalrows; i++)
        for (int k = 0; k < sel->nvalcols; k++) cs_expr(r, sel->values[i][k]);
    cs_sel(r, sel->larg); cs_sel(r, sel->rarg);
    for (int i = 0; i < sel->nctes; i++) cs_node(r, sel->ctes[i]->query);
}

static void cs_node(CteScan *r, Node *n)
{
    if (!n) return;
    if (n->select) cs_sel(r, n->select);
    else { r->star = true; if (r->cte) r->refs += 2; }   /* not walked: stay safe */
}

/* Declared volatility of a function, as the planner sees it. */
static bool cte_func_volatile(Session *s, Expr *e)
{
    if (!e->name) return false;
    SqlRoutine *p = routine_find(s->db, e->nspname, e->name, -1);
    if (p) return (p->volatility ? p->volatility : 'v') == 'v';
    static const char *vol[] = {"random", "random_normal", "nextval", "setval",
                                "currval", "lastval", "clock_timestamp", "timeofday",
                                "gen_random_uuid", "uuid_generate_v1", "uuid_generate_v4",
                                "txid_current", "pg_sleep", "pg_backend_pid",
                                "pg_stat_get_backend_pid", "currtid", NULL};
    for (int i = 0; vol[i]; i++) if (!strcasecmp(e->name, vol[i])) return true;
    return false;
}

/* A set-returning function in a target list keeps the entry alive. */
static bool cte_func_srf(Expr *e)
{
    static const char *srf[] = {"generate_series", "generate_subscripts", "unnest",
                                "regexp_split_to_table", "regexp_matches",
                                "json_each", "jsonb_each", "json_each_text",
                                "jsonb_each_text", "json_array_elements",
                                "jsonb_array_elements", "json_object_keys",
                                "jsonb_object_keys", "string_to_table", NULL};
    if (!e->name) return false;
    for (int i = 0; srf[i]; i++) if (!strcasecmp(e->name, srf[i])) return true;
    return false;
}

static bool cte_sel_volatile(Session *s, SelectStmt *sel);

static bool cte_expr_volatile(Session *s, Expr *e)
{
    if (!e) return false;
    if ((e->tag == X_FUNC || e->tag == X_AGG || e->tag == X_WINDOW) &&
        cte_func_volatile(s, e)) return true;
    for (int i = 0; i < e->nargs; i++) if (cte_expr_volatile(s, e->args[i])) return true;
    if (cte_expr_volatile(s, e->case_arg) || cte_expr_volatile(s, e->case_else)) return true;
    for (int i = 0; i < e->nwhen; i++)
        if (cte_expr_volatile(s, e->when_cond[i]) || cte_expr_volatile(s, e->when_res[i]))
            return true;
    if (cte_expr_volatile(s, e->filter) || cte_expr_volatile(s, e->testexpr) ||
        cte_expr_volatile(s, e->like_escape)) return true;
    for (int i = 0; i < e->agg_norder; i++)
        if (cte_expr_volatile(s, e->agg_order[i].e)) return true;
    if (e->subquery && e->subquery->select && cte_sel_volatile(s, e->subquery->select))
        return true;
    return false;
}

static bool cte_rt_volatile(Session *s, RangeTbl *rt)
{
    if (!rt) return false;
    if (cte_expr_volatile(s, rt->funcexpr) || cte_expr_volatile(s, rt->joinqual)) return true;
    if (rt->subquery && rt->subquery->select && cte_sel_volatile(s, rt->subquery->select))
        return true;
    if (cte_rt_volatile(s, rt->left) || cte_rt_volatile(s, rt->right)) return true;
    for (int i = 0; i < rt->nvalrows; i++)
        for (int k = 0; k < rt->nvalcols; k++)
            if (cte_expr_volatile(s, rt->values[i][k])) return true;
    return false;
}

static bool cte_sel_volatile(Session *s, SelectStmt *sel)
{
    if (!sel) return false;
    for (int i = 0; i < sel->ntarget; i++) if (cte_expr_volatile(s, sel->target[i])) return true;
    for (int i = 0; i < sel->nfrom; i++) if (cte_rt_volatile(s, sel->from[i])) return true;
    if (cte_expr_volatile(s, sel->where) || cte_expr_volatile(s, sel->having)) return true;
    for (int i = 0; i < sel->ngroupby; i++)
        if (cte_expr_volatile(s, sel->groupby[i])) return true;
    for (int i = 0; i < sel->norderby; i++)
        if (cte_expr_volatile(s, sel->orderby[i].e)) return true;
    for (int i = 0; i < sel->nvalrows; i++)
        for (int k = 0; k < sel->nvalcols; k++)
            if (cte_expr_volatile(s, sel->values[i][k])) return true;
    if (cte_sel_volatile(s, sel->larg) || cte_sel_volatile(s, sel->rarg)) return true;
    return false;
}

/* Replace the target entries of an inlined WITH query that the enclosing
 * query never reads, the way remove_unused_subquery_outputs() does. */
static SelectStmt *cte_prune_target(Session *s, SelectStmt *src, CteDef *d, CteScan *scan)
{
    if (scan->star || !src || src->setop || src->distinct || src->ngroupby ||
        src->having || src->norderby || src->ndistinct_on || src->nvalrows ||
        src->ntarget == 0)
        return src;
    int drop = 0;
    bool *kill = xmalloc((size_t)src->ntarget * sizeof(bool));
    for (int i = 0; i < src->ntarget; i++) {
        Expr *t = src->target[i];
        const char *lbl = (d && i < d->ncols) ? d->cols[i] : expr_label(t);
        kill[i] = t->tag != X_STAR && !expr_has_agg(t) && !cte_func_srf(t) &&
                  !cte_expr_volatile(s, t) && !cs_uses(scan, lbl);
        if (kill[i]) drop++;
    }
    if (!drop) { free(kill); return src; }
    SelectStmt *out = xmalloc(sizeof *out);
    *out = *src;
    out->target = xmalloc((size_t)src->ntarget * sizeof(Expr *));
    for (int i = 0; i < src->ntarget; i++) {
        if (!kill[i]) { out->target[i] = src->target[i]; continue; }
        Expr *e = xmalloc(sizeof *e); memset(e, 0, sizeof *e);
        e->tag = X_CONST; e->isnull_const = true; e->pos = src->target[i]->pos;
        e->alias = src->target[i]->alias ? src->target[i]->alias
                                         : (char *)expr_label(src->target[i]);
        out->target[i] = e;
    }
    free(kill);
    return out;
}

/* ------------------------------------------------- SEARCH and CYCLE (PG 14) */
/* A recursive query with SEARCH or CYCLE has to know which working-set row each
 * new row descended from, so a path can be carried down the tree.  The ordinary
 * recursion feeds the whole working set to the recursive term at once and loses
 * that; with SEARCH or CYCLE present the term is evaluated one working row at a
 * time instead.  That is equivalent -- PostgreSQL forbids aggregating over the
 * recursive reference -- and it makes the parent of every produced row known. */

/* The last reference to <cte>.<col> in a query, which is the one PostgreSQL
 * names when adding a SEARCH column of that name makes it ambiguous. */
static void cte_find_qualref(Expr *e, const char *cte, const char *col, int *best)
{
    if (!e) return;
    if (e->tag == X_COLREF && e->qual && e->colname &&
        !strcmp(e->qual, cte) && !strcmp(e->colname, col) && e->pos > *best)
        *best = e->pos;
    for (int i = 0; i < e->nargs; i++) cte_find_qualref(e->args[i], cte, col, best);
}

static void cte_find_qualref_sel(SelectStmt *q, const char *cte, const char *col, int *best);

static void cte_find_qualref_from(RangeTbl *r, const char *cte, const char *col, int *best)
{
    if (!r) return;
    cte_find_qualref(r->joinqual, cte, col, best);
    cte_find_qualref_from(r->left, cte, col, best);
    cte_find_qualref_from(r->right, cte, col, best);
    if (r->subquery && r->subquery->tag == N_SELECT)
        cte_find_qualref_sel(r->subquery->select, cte, col, best);
}

static void cte_find_qualref_sel(SelectStmt *q, const char *cte, const char *col, int *best)
{
    if (!q) return;
    for (int i = 0; i < q->ntarget; i++) cte_find_qualref(q->target[i], cte, col, best);
    cte_find_qualref(q->where, cte, col, best);
    for (int i = 0; i < q->nfrom; i++) cte_find_qualref_from(q->from[i], cte, col, best);
    if (q->larg) cte_find_qualref_sel(q->larg, cte, col, best);
    if (q->rarg) cte_find_qualref_sel(q->rarg, cte, col, best);
}

static int cte_col_by_name(RowSet *rs, const char *name)
{
    for (int i = 0; i < rs->ncols; i++)
        if (rs->cols[i].name && !strcmp(rs->cols[i].name, name)) return i;
    return -1;
}

/* the record (cols...) of one row, as PostgreSQL's ROW() would build it */
static Datum cte_row_record(Datum *row, const int *idx, int nidx)
{
    RecordVal *r = xmalloc(sizeof *r);
    r->n = nidx;
    r->vals = xmalloc((size_t)(nidx ? nidx : 1) * sizeof(Datum));
    r->names = xmalloc((size_t)(nidx ? nidx : 1) * sizeof(char *));
    for (int i = 0; i < nidx; i++) {
        r->vals[i] = row[idx[i]];
        r->names[i] = xstrdup("f");
    }
    Datum d; memset(&d, 0, sizeof d);
    d.type = RECORDOID; d.v.rec = r;
    return d;
}

static ArrayVal *cte_path_append(ArrayVal *base, Datum rec)
{
    int n = base ? base->nelem : 0;
    Datum *v = xmalloc((size_t)(n + 1) * sizeof(Datum));
    for (int i = 0; i < n; i++) v[i] = base->elems[i];
    v[n] = rec;
    ArrayVal *a = xmalloc(sizeof *a);
    memset(a, 0, sizeof *a);
    a->elemtype = RECORDOID; a->ndim = 1; a->dims[0] = n + 1; a->lbound[0] = 1;
    a->nelem = n + 1; a->elems = v;
    return a;
}

static bool cte_path_has(Session *s, ArrayVal *path, Datum rec)
{
    for (int i = 0; path && i < path->nelem; i++)
        if (d_cmp(s, path->elems[i], rec) == 0) return true;
    return false;
}

typedef struct CteTrack {
    ArrayVal *spath;      /* SEARCH: the path of search-key records */
    ArrayVal *cpath;      /* CYCLE: the path of cycle-key records */
    int64     depth;
    bool      cycled;
} CteTrack;

/* Where, if anywhere, does this statement select FROM a WITH query by name? */
static int cte_from_pos(SelectStmt *sel, const char *name);

static int cte_from_pos_rt(RangeTbl *rt, const char *name)
{
    if (!rt) return 0;
    if (rt->kind == 0 && !rt->schema && rt->relname && !strcmp(rt->relname, name))
        return rt->pos ? rt->pos : -1;
    if (rt->kind == 1 && rt->subquery && rt->subquery->tag == N_SELECT) {
        int p = cte_from_pos(rt->subquery->select, name);
        if (p) return p;
    }
    if (rt->kind == 2) {
        int p = cte_from_pos_rt(rt->left, name);
        if (p) return p;
        return cte_from_pos_rt(rt->right, name);
    }
    return 0;
}

static int cte_from_pos(SelectStmt *sel, const char *name)
{
    if (!sel) return 0;
    for (int i = 0; i < sel->nfrom; i++) {
        int p = cte_from_pos_rt(sel->from[i], name);
        if (p) return p;
    }
    int p = cte_from_pos(sel->larg, name);
    if (p) return p;
    return cte_from_pos(sel->rarg, name);
}

/* A data-modifying WITH query has rows to offer only if it says RETURNING.
 * PostgreSQL rejects selecting from one that does not, at parse-analysis time,
 * so the statement does not run at all -- which is why this is checked before
 * the CTE is executed rather than when its rows are wanted. */
static bool cte_is_dml_without_returning(Node *q)
{
    if (!q) return false;
    switch (q->tag) {
    case N_INSERT: return ((InsertStmt *)q->x)->nret == 0;
    case N_UPDATE: return ((UpdateStmt *)q->x)->nret == 0;
    case N_DELETE: return ((DeleteStmt *)q->x)->nret == 0;
    case N_MERGE:  return ((MergeStmt *)q->x)->nret == 0;
    default:       return false;
    }
}

static void run_ctes(ExecCtx *c, SelectStmt *sel, ExecCtx *out)
{
    Session *s = c->s;
    CteRt *list = xmalloc((size_t)(c->nctes + sel->nctes) * sizeof(CteRt));
    for (int i = 0; i < c->nctes; i++) list[i] = c->ctes[i];
    int n = c->nctes;
    for (int i = 0; i < sel->nctes; i++) {
        CteDef *d = sel->ctes[i];
        if (cte_is_dml_without_returning(d->query)) {
            int at = cte_from_pos(sel, d->name);
            if (at)
                ereport_pos(s, "42601", at > 0 ? at : 0,
                            "WITH query \"%s\" does not have a RETURNING clause",
                            d->name);
        }
        CteRt rt; memset(&rt, 0, sizeof rt);
        rt.name = d->name; rt.materialized = d->materialized; rt.query = d->query; rt.def = d;
        ExecCtx sub; memset(&sub, 0, sizeof sub);
        sub.s = s; sub.params = c->params; sub.nparams = c->nparams;
        sub.ctes = list; sub.nctes = n;
#if PGVC_VN >= 120
        /* Decide whether the planner would inline this WITH query. */
        if (!d->recursive && d->query->tag == N_SELECT && !d->search_col && !d->has_cycle) {
            CteScan scan; memset(&scan, 0, sizeof scan);
            scan.cte = d->name;
            if (sel->ntarget || sel->setop || sel->nvalrows) cs_sel(&scan, sel);
            else { scan.star = true; scan.refs = 2; }
            bool inl = d->materialized < 0 ||
                       (d->materialized == 0 && scan.refs == 1 &&
                        !cte_sel_volatile(s, d->query->select));
            if (inl) {
                ExecCtx *dc = xmalloc(sizeof *dc);
                *dc = sub;
                rt.inlined = true; rt.defctx = dc;
                rt.inlsel = cte_prune_target(s, d->query->select, d, &scan);
                free(scan.names);
                list[n++] = rt;
                continue;
            }
            free(scan.names);
        }
#endif
        CteTrack *tr = NULL;              /* per-row SEARCH/CYCLE state */
        int sidx[8], nsidx = 0, cidx[8], ncidx = 0;
        if (d->recursive && d->query->tag == N_SELECT && d->query->select->setop) {
            /* WITH RECURSIVE: seed from the left arm, iterate the right arm */
            SelectStmt *ss = d->query->select;
            RowSet *acc = exec_select(&sub, ss->larg);
            RowSet *work = acc;
            RowSet *all = rs_new();
            all->ncols = acc->ncols; all->cols = acc->cols;
            for (int k = 0; k < acc->nrows; k++) rs_addrow(all, acc->rows[k]);
            int guard = 0;
            bool track = (d->search_col || d->has_cycle);
            if (track) {
                if (d->search_col) {
                    for (int q = 0; q < d->nsearch && q < 8; q++) {
                        const char *nm = d->search_by[q].e && d->search_by[q].e->colname
                                       ? d->search_by[q].e->colname : "";
                        int ci = cte_col_by_name(all, nm);
                        if (ci < 0)
                            ereport_pos(s, "42703", d->search_pos,
                                    "search column \"%s\" not in WITH query column list", nm);
                        sidx[nsidx++] = ci;
                    }
                    if (cte_col_by_name(all, d->search_col->colname) >= 0) {
                        /* the search column joins the CTE's column list, so a
                         * qualified reference to the same name is now ambiguous */
                        int found = 0;
                        cte_find_qualref_sel(ss->rarg, d->name, d->search_col->colname, &found);
                        int at = found ? found : d->search_pos;
                        ereport_pos(s, "42702", at,
                                "column reference \"%s\" is ambiguous",
                                d->search_col->colname);
                    }
                }
                if (d->has_cycle) {
                    if (d->cycle_col && d->cycle_path && !strcmp(d->cycle_col, d->cycle_path))
                        ereport_pos(s, "42P17", d->cycle_pos,
                                "cycle mark column name and cycle path column "
                                "name are the same");
                    for (int q = 0; q < d->ncycle && q < 8; q++) {
                        const char *nm = d->cycle_cols[q] ? d->cycle_cols[q]->colname : "";
                        int ci = cte_col_by_name(all, nm);
                        if (ci < 0)
                            ereport(s, "42703",
                                    "cycle column \"%s\" not in WITH query column list", nm);
                        cidx[ncidx++] = ci;
                    }
                }
                tr = xmalloc((size_t)(all->nrows ? all->nrows : 1) * sizeof(CteTrack));
                for (int k = 0; k < all->nrows; k++) {
                    memset(&tr[k], 0, sizeof tr[k]);
                    if (nsidx) tr[k].spath = cte_path_append(NULL,
                                   cte_row_record(all->rows[k], sidx, nsidx));
                    if (ncidx) tr[k].cpath = cte_path_append(NULL,
                                   cte_row_record(all->rows[k], cidx, ncidx));
                }
            }
            int *workidx = NULL;          /* index into `all` for each work row */
            if (track) {
                workidx = xmalloc((size_t)(work->nrows ? work->nrows : 1) * sizeof(int));
                for (int k = 0; k < work->nrows; k++) workidx[k] = k;
            }
            while (work->nrows > 0 && guard++ < 10000) {
                if (d->ncols) {
                    for (int q = 0; q < d->ncols && q < work->ncols; q++) {
                        free(work->cols[q].name);
                        work->cols[q].name = xstrdup(d->cols[q]);
                    }
                }
                RowSet *fresh = rs_new();
                int *freshidx = NULL; int nfresh = 0;
                /* with SEARCH or CYCLE the term runs once per working row, so
                 * each produced row's parent -- and therefore its path -- is
                 * known; without them the whole working set goes at once */
                int batches = track ? work->nrows : 1;
                for (int bi = 0; bi < batches; bi++) {
                    RowSet *feed = work;
                    if (track) {
                        feed = rs_new();
                        feed->ncols = work->ncols; feed->cols = work->cols;
                        rs_addrow(feed, work->rows[bi]);
                    }
                    CteRt self = rt;
                    self.rs = feed;
                    CteRt *l2 = xmalloc((size_t)(n + 1) * sizeof(CteRt));
                    memcpy(l2, list, (size_t)n * sizeof(CteRt));
                    l2[n] = self;
                    ExecCtx it; memset(&it, 0, sizeof it);
                    it.s = s; it.params = c->params; it.nparams = c->nparams;
                    it.ctes = l2; it.nctes = n + 1;
                    RowSet *next = exec_select(&it, ss->rarg);
                    /* A recursive union takes its column names from the
                     * non-recursive term, so the self-reference keeps them for
                     * every iteration after the first. */
                    for (int q = 0; q < next->ncols && q < all->ncols; q++)
                        if (all->cols[q].name) {
                            char *nm = xstrdup(all->cols[q].name);
                            free(next->cols[q].name);
                            next->cols[q].name = nm;
                        }
                    if (!fresh->ncols) { fresh->ncols = next->ncols; fresh->cols = next->cols; }
                    for (int k = 0; k < next->nrows; k++) {
                        bool dup = false;
                        if (!ss->setop_all)
                            for (int q = 0; q < all->nrows && !dup; q++)
                                if (rows_equal(s, all->rows[q], next->rows[k], all->ncols)) dup = true;
                        if (dup) continue;
                        int parent = track ? workidx[bi] : -1;
                        bool cycled = false;
                        ArrayVal *sp = NULL, *cp = NULL;
                        if (track) {
                            if (nsidx)
                                sp = cte_path_append(tr[parent].spath,
                                        cte_row_record(next->rows[k], sidx, nsidx));
                            if (ncidx) {
                                Datum crec = cte_row_record(next->rows[k], cidx, ncidx);
                                cycled = cte_path_has(s, tr[parent].cpath, crec);
                                cp = cte_path_append(tr[parent].cpath, crec);
                            }
                        }
                        rs_addrow(all, next->rows[k]);
                        if (track) {
                            tr = xrealloc(tr, (size_t)all->nrows * sizeof(CteTrack));
                            CteTrack *e = &tr[all->nrows - 1];
                            memset(e, 0, sizeof *e);
                            e->spath = sp; e->cpath = cp; e->cycled = cycled;
                            e->depth = tr[parent].depth + 1;
                            if (cycled) continue;     /* a cycle stops here */
                            freshidx = xrealloc(freshidx, (size_t)(nfresh + 1) * sizeof(int));
                            freshidx[nfresh++] = all->nrows - 1;
                        }
                        rs_addrow(fresh, next->rows[k]);
                    }
                }
                work = fresh;
                if (track) { free(workidx); workidx = freshidx; }
            }
            rt.rs = all;
        } else {
            rt.rs = (d->query->tag == N_SELECT) ? exec_select(&sub, d->query->select)
                                                : rs_new();
            if (d->query->tag != N_SELECT) {
                extern QueryResult *exec_node(Session *s, Node *n);
                QueryResult *q = exec_node(s, d->query);
                RowSet *r = rs_new();
                for (int k = 0; k < q->ncols; k++)
                    rs_addcol(r, NULL, q->colnames[k], q->coltypes[k], q->coltypmods[k]);
                for (int k = 0; k < q->nrows; k++) rs_addrow(r, q->rows[k]);
                rt.rs = r;
            }
        }
        if (d->ncols && rt.rs)
            for (int q = 0; q < d->ncols && q < rt.rs->ncols; q++) {
                free(rt.rs->cols[q].name);
                rt.rs->cols[q].name = xstrdup(d->cols[q]);
            }
        if (d->search_col && rt.rs) {
            if (!tr)
                ereport_pos(s, "42P19", d->name_pos, "WITH query is not recursive");
            /* DEPTH FIRST sets the column to the path of search-key records, so
             * that comparing the arrays element by element is the traversal
             * order; BREADTH FIRST sets it to (depth, keys...). */
            bool depth_first = (d->search_kind == 0);
            rs_addcol(rt.rs, NULL, d->search_col->colname,
                      depth_first ? RECORDARRAYOID : RECORDOID, -1);
            for (int k = 0; k < rt.rs->nrows; k++) {
                Datum *nr = xmalloc((size_t)rt.rs->ncols * sizeof(Datum));
                memcpy(nr, rt.rs->rows[k], (size_t)(rt.rs->ncols - 1) * sizeof(Datum));
                Datum v; memset(&v, 0, sizeof v);
                if (depth_first) {
                    v.type = RECORDARRAYOID; v.v.arr = tr[k].spath;
                    if (!tr[k].spath) v.isnull = true;
                } else {
                    RecordVal *r = xmalloc(sizeof *r);
                    r->n = nsidx + 1;
                    r->vals = xmalloc((size_t)(nsidx + 1) * sizeof(Datum));
                    r->names = xmalloc((size_t)(nsidx + 1) * sizeof(char *));
                    r->vals[0] = d_int(INT8OID, tr[k].depth);
                    r->names[0] = xstrdup("f");
                    for (int q = 0; q < nsidx; q++) {
                        r->vals[q + 1] = rt.rs->rows[k][sidx[q]];
                        r->names[q + 1] = xstrdup("f");
                    }
                    v.type = RECORDOID; v.v.rec = r;
                }
                nr[rt.rs->ncols - 1] = v;
                rt.rs->rows[k] = nr;
            }
        }
        if (d->has_cycle && rt.rs) {
            if (!tr)
                ereport_pos(s, "42P19", d->name_pos, "WITH query is not recursive");
            /* CYCLE ... SET c [TO mark DEFAULT other]: without TO the markers
             * are the booleans, with it they are whatever was written. */
            Datum mark_y = d_bool(true), mark_n = d_bool(false);
            if (d->cycle_mark_e) {
                ExecCtx mc; memset(&mc, 0, sizeof mc);
                mc.s = s; mc.params = c->params; mc.nparams = c->nparams;
                mark_y = eval(&mc, d->cycle_mark_e);
                mark_n = eval(&mc, d->cycle_default_e);
                if (mark_y.type == UNKNOWNOID) mark_y = coerce_to(s, mark_y, TEXTOID, -1, 1);
                if (mark_n.type == UNKNOWNOID) mark_n = coerce_to(s, mark_n, TEXTOID, -1, 1);
            }
            rs_addcol(rt.rs, NULL, d->cycle_col, mark_y.type, -1);
            rs_addcol(rt.rs, NULL, d->cycle_path, RECORDARRAYOID, -1);
            for (int k = 0; k < rt.rs->nrows; k++) {
                Datum *nr = xmalloc((size_t)rt.rs->ncols * sizeof(Datum));
                memcpy(nr, rt.rs->rows[k], (size_t)(rt.rs->ncols - 2) * sizeof(Datum));
                Datum mark = tr[k].cycled ? mark_y : mark_n;
                nr[rt.rs->ncols - 2] = mark;
                Datum pv; memset(&pv, 0, sizeof pv);
                pv.type = RECORDARRAYOID; pv.v.arr = tr[k].cpath;
                if (!tr[k].cpath) pv.isnull = true;
                nr[rt.rs->ncols - 1] = pv;
                rt.rs->rows[k] = nr;
            }
        }
        list[n++] = rt;
    }
    out->ctes = list; out->nctes = n;
}

static QueryResult *rs_to_qr(Session *s, RowSet *rs)
{
    check_cancel(s);
    QueryResult *q = qr_new();
    for (int i = 0; i < rs->ncols; i++) {
        if (rs->cols[i].hidden) continue;
        qr_addcol(q, rs->cols[i].name, rs->cols[i].type == UNKNOWNOID ? TEXTOID : rs->cols[i].type,
                  rs->cols[i].typmod);
    }
    for (int i = 0; i < rs->nrows; i++) {
        Datum *row = xmalloc((size_t)(q->ncols ? q->ncols : 1) * sizeof(Datum));
        int o = 0;
        for (int k = 0; k < rs->ncols; k++) if (!rs->cols[k].hidden) row[o++] = rs->rows[i][k];
        qr_addrow(q, row);
    }
    return q;
}

/* ================================================================ aggregates */
static Datum num_add_d(Session *s, Datum a, Datum b)
{
    if (a.isnull) return b;
    if (b.isnull) return a;
    Datum ar[2] = {a, b};
    bool f = false;
    Datum r = call_builtin(s, "+", ar, 2, &f);
    if (!f) ereport(s, "42883", "operator does not exist: + ");
    return r;
}

bool type_is_rangey(Oid t)
{
    return t == INT4RANGEOID || t == INT8RANGEOID || t == NUMRANGEOID ||
           t == TSRANGEOID || t == TSTZRANGEOID || t == DATERANGEOID ||
           t == INT4MULTIRANGEOID || t == INT8MULTIRANGEOID || t == NUMMULTIRANGEOID ||
           t == TSMULTIRANGEOID || t == TSTZMULTIRANGEOID || t == DATEMULTIRANGEOID;
}
static bool type_is_multirange(Oid t)
{
    return t == INT4MULTIRANGEOID || t == INT8MULTIRANGEOID || t == NUMMULTIRANGEOID ||
           t == TSMULTIRANGEOID || t == TSTZMULTIRANGEOID || t == DATEMULTIRANGEOID;
}
static Oid range_of_multirange(Oid t)
{
    return t == INT4MULTIRANGEOID ? INT4RANGEOID : t == INT8MULTIRANGEOID ? INT8RANGEOID :
           t == NUMMULTIRANGEOID ? NUMRANGEOID : t == TSMULTIRANGEOID ? TSRANGEOID :
           t == TSTZMULTIRANGEOID ? TSTZRANGEOID : DATERANGEOID;
}

Datum agg_compute(ExecCtx *c, Expr *agg, RowSet *rs, int *idx, int n)
{
    Session *s = c->s;
    const char *nm = agg->name;
    /* gather the argument values for the rows of this group */
    Datum *vals = xmalloc((size_t)(n ? n : 1) * sizeof(Datum));
    Datum *vals2 = xmalloc((size_t)(n ? n : 1) * sizeof(Datum));
    int nord = agg->agg_norder;
    Datum *okey = nord ? xmalloc((size_t)(n ? n : 1) * (size_t)nord * sizeof(Datum)) : NULL;
    int nv = 0;
    for (int i = 0; i < n; i++) {
        if ((i & 0x3ff) == 0) check_cancel(s);
        ExecCtx rc = *c;
        rc.rs = rs; rc.row = rs->rows[idx[i]]; rc.agg_phase = 0;
        if (agg->filter && !datum_true(to_bool(s, eval(&rc, agg->filter)))) continue;
        for (int k = 0; k < nord; k++) okey[nv * nord + k] = eval(&rc, agg->agg_order[k].e);
        if (agg->agg_star) { vals[nv++] = d_int(INT4OID, 1); continue; }
        if (agg->nargs == 0) { vals[nv++] = d_int(INT4OID, 1); continue; }
        Datum v = eval(&rc, agg->args[0]);
        if (agg->nargs > 1) vals2[nv] = eval(&rc, agg->args[1]);
        vals[nv++] = v;
    }
    if (okey && nv > 1) {
        /* an aggregate's own ORDER BY sorts its input before accumulation;
         * a stable insertion sort keeps ties in arrival order */
        Datum *kbuf = xmalloc((size_t)nord * sizeof(Datum));
        for (int i = 1; i < nv; i++) {
            memcpy(kbuf, &okey[i * nord], (size_t)nord * sizeof(Datum));
            Datum v = vals[i], v2 = vals2[i];
            int j = i - 1;
            while (j >= 0) {
                int cmp = 0;
                for (int k = 0; k < nord && cmp == 0; k++) {
                    Datum a = okey[j * nord + k], b2 = kbuf[k];
                    bool nullsfirst = agg->agg_order[k].nulls
                                      ? (agg->agg_order[k].nulls == 1)
                                      : (agg->agg_order[k].dir != 0);
                    if (a.isnull || b2.isnull) {
                        if (a.isnull && b2.isnull) cmp = 0;
                        else if (a.isnull) cmp = nullsfirst ? -1 : 1;
                        else cmp = nullsfirst ? 1 : -1;
                    } else {
                        cmp = d_cmp(s, a, b2);
                        if (agg->agg_order[k].dir) cmp = -cmp;
                    }
                }
                if (cmp <= 0) break;
                memcpy(&okey[(j+1) * nord], &okey[j * nord], (size_t)nord * sizeof(Datum));
                vals[j+1] = vals[j]; vals2[j+1] = vals2[j];
                j--;
            }
            memcpy(&okey[(j+1) * nord], kbuf, (size_t)nord * sizeof(Datum));
            vals[j+1] = v; vals2[j+1] = v2;
        }
        free(kbuf);
    }
    if (agg->agg_distinct) {
        int m = 0;
        for (int i = 0; i < nv; i++) {
            bool dup = false;
            for (int k = 0; k < m && !dup; k++)
                if (vals[k].isnull == vals[i].isnull &&
                    (vals[i].isnull || d_cmp(s, vals[k], vals[i]) == 0)) dup = true;
            if (!dup) { vals[m] = vals[i]; vals2[m] = vals2[i]; m++; }
        }
        nv = m;
    }
    if (!strcmp(nm, "count")) {
        int64 k = 0;
        for (int i = 0; i < nv; i++) if (agg->agg_star || !vals[i].isnull) k++;
        return d_int(INT8OID, k);
    }
    if ((!strcmp(nm, "sum") || !strcmp(nm, "avg")) &&
        ((nv && vals[0].type == INTERVALOID) ||
         (agg->nargs && agg->args[0]->tag == X_COLREF && !nv &&
          find_col(rs, agg->args[0]->qual, agg->args[0]->colname, s, agg->args[0]->pos) >= 0 &&
          rs->cols[find_col(rs, agg->args[0]->qual, agg->args[0]->colname, s, agg->args[0]->pos)].type
              == INTERVALOID))) {
        extern Datum interval_agg(Session *s, Datum *vals, int nv, bool isavg);
        return interval_agg(s, vals, nv, nm[0] == 'a');
    }
    if (!strcmp(nm, "sum")) {
        Datum acc = d_null(INT8OID);
        bool any = false;
        Oid rt = 0;
        for (int i = 0; i < nv; i++) {
            if (vals[i].isnull) continue;
            Datum v = vals[i];
            if (v.type == INT2OID || v.type == INT4OID) { v = coerce_to(s, v, INT8OID, -1, 1); rt = INT8OID; }
            else if (v.type == INT8OID) { v = coerce_to(s, v, NUMERICOID, -1, 1); rt = NUMERICOID; }
            else rt = v.type;
            acc = any ? num_add_d(s, acc, v) : v;
            any = true;
        }
        if (!any) return d_null(rt ? rt : INT8OID);
        return acc;
    }
    if (!strcmp(nm, "avg")) {
        Datum acc = d_null(NUMERICOID);
        int k = 0;
        bool isfloat = false;
        for (int i = 0; i < nv; i++) {
            if (vals[i].isnull) continue;
            Datum v = vals[i];
            if (v.type == FLOAT4OID || v.type == FLOAT8OID) isfloat = true;
            v = coerce_to(s, v, isfloat ? FLOAT8OID : NUMERICOID, -1, 2);
            acc = k ? num_add_d(s, acc, v) : v;
            k++;
        }
        if (!k) return d_null(isfloat ? FLOAT8OID : NUMERICOID);
        Datum ar[2]; ar[0] = acc;
        ar[1] = isfloat ? d_float(FLOAT8OID, (double)k) : d_num(num_from_int64(k));
        bool f = false;
        return call_builtin(s, "/", ar, 2, &f);
    }
    if (!strcmp(nm, "min") || !strcmp(nm, "max")) {
        bool ismax = (nm[1] == 'a');
        Oid argtype = TEXTOID;
        if (agg->nargs && agg->args[0]->tag == X_COLREF) {
            int ci = find_col(rs, agg->args[0]->qual, agg->args[0]->colname, s, agg->args[0]->pos);
            if (ci >= 0) argtype = rs->cols[ci].type;
        }
        Datum best = d_null(argtype);
        bool any = false;
        for (int i = 0; i < nv; i++) {
            if (vals[i].isnull) continue;
            if (!any) { best = vals[i]; any = true; continue; }
            int cc = d_cmp(s, vals[i], best);
            if (ismax ? (cc > 0) : (cc < 0)) best = vals[i];
        }
        if (!any) return d_null(nv ? vals[0].type : argtype);
        return best;
    }
    if (!strcmp(nm, "bool_and") || !strcmp(nm, "every") || !strcmp(nm, "bool_or")) {
        bool isand = (nm[5] == 'a' || nm[0] == 'e');
        bool res = isand, any = false;
        for (int i = 0; i < nv; i++) {
            if (vals[i].isnull) continue;
            any = true;
            bool b = datum_true(to_bool(s, vals[i]));
            if (isand) res = res && b; else res = res || b;
        }
        return any ? d_bool(res) : d_null(BOOLOID);
    }
#if PGVC_VN >= 140
    if (!strcmp(nm, "range_intersect_agg")) {
        for (int i = 0; i < nv; i++)
            if (!vals[i].isnull && !type_is_rangey(vals[i].type)) {
                err_hint(s, "No function matches the given name and argument types. "
                            "You might need to add explicit type casts.");
                ereport_pos(s, "42883", agg->pos,
                            "function range_intersect_agg(%s) does not exist",
                            format_type_str(vals[i].type == UNKNOWNOID ? TEXTOID
                                                                       : vals[i].type, -1));
            }
        Oid et = 0;
        for (int i = 0; i < nv; i++) et = common_type(s, et, vals[i].type);
        Datum acc; memset(&acc, 0, sizeof acc);
        bool any = false;
        for (int i = 0; i < nv; i++) {
            if (vals[i].isnull) continue;
            if (!any) { acc = vals[i]; any = true; continue; }
            Datum ar[2]; ar[0] = acc; ar[1] = vals[i];
            bool f = false;
            acc = call_builtin(s, "*", ar, 2, &f);
        }
        if (!any) return d_null(et ? et : INT4RANGEOID);
        return acc;
    }
#endif
#if PGVC_VN >= 160
    if (agg->tag == X_AGG && (agg->json_ctor == 2 || agg->json_ctor == 3) &&
        (!strcmp(nm, "json_arrayagg") || !strcmp(nm, "json_objectagg"))) {
        /* JSON_OBJECTAGG holds the key in args[0] and the value in args[1] */
        if (agg->json_ctor == 3) return eval_json_agg(s, agg, vals2, vals, nv);
        return eval_json_agg(s, agg, vals, NULL, nv);
    }
#endif
    if (!strcmp(nm, "array_agg") || !strcmp(nm, "json_agg") || !strcmp(nm, "jsonb_agg") ||
        !strcmp(nm, "range_agg") || !strcmp(nm, "json_arrayagg")) {
        Oid et = 0;
        for (int i = 0; i < nv; i++) et = common_type(s, et, vals[i].type);
        if (!et || et == UNKNOWNOID) et = TEXTOID;
        if (!strcmp(nm, "array_agg")) {
            if (!nv) return d_null(array_type_of(et) ? array_type_of(et) : TEXTARRAYOID);
            Datum *cp = xmalloc((size_t)nv * sizeof(Datum));
            for (int i = 0; i < nv; i++) cp[i] = coerce_to(s, vals[i], et, -1, 1);
            Datum r; memset(&r, 0, sizeof r);
            r.type = array_type_of(et) ? array_type_of(et) : TEXTARRAYOID;
            r.v.arr = mk_array(et, cp, nv);
            return r;
        }
        if (!strcmp(nm, "range_agg")) {
            if (agg->nargs > 1) {
                Buf sb; buf_init(&sb);
                for (int i = 0; i < agg->nargs; i++) {
                    if (i) buf_adds(&sb, ", ");
                    Datum av = eval(c, agg->args[i]);
                    buf_adds(&sb, format_type_str(av.type == UNKNOWNOID ? TEXTOID : av.type, -1));
                }
                err_hint(s, "No function matches the given name and argument types. "
                            "You might need to add explicit type casts.");
                ereport_pos(s, "42883", agg->pos, "function range_agg(%s) does not exist",
                            sb.data ? sb.data : "");
            }
            for (int i = 0; i < nv; i++)
                if (!vals[i].isnull && !type_is_rangey(vals[i].type)) {
                    err_hint(s, "No function matches the given name and argument types. "
                                "You might need to add explicit type casts.");
                    ereport_pos(s, "42883", agg->pos, "function range_agg(%s) does not exist",
                                format_type_str(vals[i].type == UNKNOWNOID ? TEXTOID
                                                                           : vals[i].type, -1));
                }
            bool mr = false;
            for (int i = 0; i < nv; i++) if (type_is_multirange(vals[i].type)) mr = true;
            MRangeVal *m = xmalloc(sizeof *m);
            memset(m, 0, sizeof *m);
            bool anyval = false;
            for (int i = 0; i < nv; i++) if (!vals[i].isnull) anyval = true;
            if (mr) { m->mrngtype = et; m->rngtype = range_of_multirange(et); }
            else { m->rngtype = et; m->mrngtype = multirange_of_range(et); }
            for (int i = 0; i < nv; i++) {
                if (vals[i].isnull) continue;
                if (type_is_multirange(vals[i].type)) {
                    MRangeVal *src = vals[i].v.mrng;
                    for (int q = 0; src && q < src->n; q++) {
                        m->ranges = xrealloc(m->ranges, (size_t)(m->n + 1) * sizeof(RangeVal *));
                        m->ranges[m->n++] = src->ranges[q];
                    }
                    continue;
                }
                if (vals[i].v.rng->empty) continue;
                m->ranges = xrealloc(m->ranges, (size_t)(m->n + 1) * sizeof(RangeVal *));
                m->ranges[m->n++] = vals[i].v.rng;
            }
            et = m->rngtype;
            /* PostgreSQL returns the ranges sorted and merged */
            for (int i = 0; i < m->n; i++)
                for (int k = i + 1; k < m->n; k++) {
                    Datum a; memset(&a,0,sizeof a); a.type = et; a.v.rng = m->ranges[i];
                    Datum b; memset(&b,0,sizeof b); b.type = et; b.v.rng = m->ranges[k];
                    if (d_cmp(s, a, b) > 0) { RangeVal *t = m->ranges[i]; m->ranges[i] = m->ranges[k]; m->ranges[k] = t; }
                }
            for (int i = 0; i + 1 < m->n; ) {
                Datum a; memset(&a,0,sizeof a); a.type = et; a.v.rng = m->ranges[i];
                Datum b; memset(&b,0,sizeof b); b.type = et; b.v.rng = m->ranges[i+1];
                Datum ov[2]; ov[0] = a; ov[1] = b;
                bool f = false;
                Datum lap = call_builtin(s, "&&", ov, 2, &f);
                f = false;
                Datum adj = call_builtin(s, "-|-", ov, 2, &f);
                if ((!lap.isnull && lap.v.i) || (!adj.isnull && adj.v.i)) {
                    f = false;
                    Datum u = call_builtin(s, "+", ov, 2, &f);
                    m->ranges[i] = u.v.rng;
                    memmove(&m->ranges[i+1], &m->ranges[i+2],
                            (size_t)(m->n - i - 2) * sizeof(RangeVal *));
                    m->n--;
                    continue;
                }
                i++;
            }
            Datum r; memset(&r, 0, sizeof r);
            r.type = m->mrngtype; r.v.mrng = m;
            /* an aggregate over no rows is NULL, but an empty multirange input
             * still aggregates to the empty multirange */
            if (!m->n && !anyval) return d_null(r.type);
            return r;
        }
        /* json_agg / jsonb_agg */
        Buf b; buf_init(&b);
        buf_addc(&b, '[');
        for (int i = 0; i < nv; i++) {
            if (i) buf_adds(&b, ", ");
            Datum jd = coerce_to(s, vals[i], JSONBOID, -1, 2);
            char *t = jd.isnull ? xstrdup("null") : json_to_text(jd.v.jb, true);
            buf_adds(&b, t); free(t);
        }
        buf_addc(&b, ']');
        if (!nv) { buf_free(&b); return d_null(JSONBOID); }
        Datum r = in_func(s, !strcmp(nm, "json_agg") ? JSONOID : JSONBOID, -1, b.data, (int32)b.len);
        buf_free(&b);
        return r;
    }
    if (!strcmp(nm, "string_agg")) {
        Buf b; buf_init(&b);
        bool any = false;
        for (int i = 0; i < nv; i++) {
            if (vals[i].isnull) continue;
            if (any && !vals2[i].isnull) { Datum sep = coerce_to(s, vals2[i], TEXTOID, -1, 2);
                                            buf_add(&b, sep.v.s.p, (size_t)sep.v.s.len); }
            Datum v = coerce_to(s, vals[i], TEXTOID, -1, 2);
            buf_add(&b, v.v.s.p, (size_t)v.v.s.len);
            any = true;
        }
        if (!any) { buf_free(&b); return d_null(TEXTOID); }
        Datum r = d_textn(TEXTOID, b.data, (int32)b.len);
        buf_free(&b);
        return r;
    }
    if (!strcmp(nm, "json_object_agg") || !strcmp(nm, "jsonb_object_agg") ||
        !strcmp(nm, "json_objectagg")) {
        Buf b; buf_init(&b);
        buf_addc(&b, '{');
        for (int i = 0; i < nv; i++) {
            if (i) buf_adds(&b, ", ");
            char *k = out_func(s, vals[i]);
            buf_addc(&b, '"');
            for (char *q = k; *q; q++) { if (*q == '"' || *q == '\\') buf_addc(&b, '\\'); buf_addc(&b, *q); }
            buf_adds(&b, "\": ");
            free(k);
            Datum jd = coerce_to(s, vals2[i], JSONBOID, -1, 2);
            char *t = jd.isnull ? xstrdup("null") : json_to_text(jd.v.jb, true);
            buf_adds(&b, t); free(t);
        }
        buf_addc(&b, '}');
        Datum r = in_func(s, !strcmp(nm, "json_object_agg") ? JSONOID : JSONBOID, -1, b.data, (int32)b.len);
        buf_free(&b);
        return r;
    }
    if (!strcmp(nm, "any_value")) {
        for (int i = 0; i < nv; i++) if (!vals[i].isnull) return vals[i];
        if (nv) return d_null(vals[0].type);
        Oid argtype = TEXTOID;
        if (agg->nargs && agg->args[0]->tag == X_COLREF) {
            int ci = find_col(rs, agg->args[0]->qual, agg->args[0]->colname, s, agg->args[0]->pos);
            if (ci >= 0) argtype = rs->cols[ci].type;
        }
        return d_null(argtype);
    }
    if (!strcmp(nm, "count") || !strcmp(nm, "sum")) { /* handled above */ }
    if (!strcmp(nm, "stddev") || !strcmp(nm, "stddev_samp") || !strcmp(nm, "stddev_pop") ||
        !strcmp(nm, "variance") || !strcmp(nm, "var_samp") || !strcmp(nm, "var_pop")) {
        double sum = 0, sum2 = 0; int k = 0;
        for (int i = 0; i < nv; i++) {
            if (vals[i].isnull) continue;
            double v = coerce_to(s, vals[i], FLOAT8OID, -1, 2).v.f;
            sum += v; sum2 += v * v; k++;
        }
        bool pop = strstr(nm, "_pop") != NULL;
        if (k == 0 || (!pop && k < 2)) return d_null(NUMERICOID);
        double mean = sum / k;
        double var = (sum2 - k * mean * mean) / (pop ? k : (k - 1));
        if (var < 0) var = 0;
        bool isvar = (nm[0] == 'v');
        double res = isvar ? var : sqrt(var);
        char buf[64];
        snprintf(buf, sizeof buf, "%.15g", res);
        bool ok;
        return d_num(num_from_str(buf, &ok));
    }
    ereport_pos(s, "42883", agg->pos, "aggregate function %s does not exist", nm);
}

/* ------------------------------------------------------------- windows */
/* Are the ordered rows at positions i and j peers under the window ORDER BY? */
static bool win_peers(ExecCtx *c, RowSet *src, int *idx, WindowDef *def, int i, int j)
{
    if (!def || !def->norder) return true;
    for (int z = 0; z < def->norder; z++) {
        ExecCtx a = *c; a.rs = src; a.row = src->rows[idx[i]]; a.agg_phase = 0;
        ExecCtx b = *c; b.rs = src; b.row = src->rows[idx[j]]; b.agg_phase = 0;
        Datum x = eval(&a, def->order[z].e), y = eval(&b, def->order[z].e);
        if (x.isnull != y.isnull) return false;
        if (!x.isnull && d_cmp(c->s, x, y) != 0) return false;
    }
    return true;
}

/* The single ORDER BY value of ordered row k. */
static Datum win_ordval(ExecCtx *c, RowSet *src, int *idx, WindowDef *def, int k)
{
    ExecCtx a = *c; a.rs = src; a.row = src->rows[idx[k]]; a.agg_phase = 0;
    return eval(&a, def->order[0].e);
}

/* The frame [*lo,*hi] of ordered row k, in whichever of the three modes the
 * window uses.  ROWS counts rows, GROUPS counts peer groups and RANGE
 * compares the ORDER BY value itself. */
static void win_frame(ExecCtx *c, RowSet *src, int *idx, int ni, WindowDef *def,
                      int k, int *lo, int *hi)
{
    Session *s = c->s;
    int mode = def ? def->frame_mode : 0;
    /* the peer group of row k */
    int plo = k, phi = k;
    if (def && def->norder) {
        while (plo > 0 && win_peers(c, src, idx, def, plo - 1, k)) plo--;
        while (phi + 1 < ni && win_peers(c, src, idx, def, phi + 1, k)) phi++;
    } else { plo = 0; phi = ni - 1; }

    if (!def || def->frame_start < 0) {      /* the default frame */
        *lo = 0; *hi = (def && def->norder) ? phi : ni - 1;
        return;
    }
    int st = def->frame_start, en = def->frame_end;
    /* start */
    if (st == 0) *lo = 0;
    else if (st == 2) *lo = (mode == 1) ? k : plo;
    else {
        ExecCtx rc = *c; rc.rs = src; rc.row = src->rows[idx[k]]; rc.agg_phase = 0;
        Datum ov = eval(&rc, def->start_off);
        int sign = (st == 1) ? -1 : 1;
        if (mode == 1) {
            int64 off = coerce_to(s, ov, INT8OID, -1, 1).v.i;
            if (off < 0) ereport(s, "22013", "invalid preceding or following size in window function");
            *lo = k + (int)(sign * off);
        } else if (mode == 2) {
            int64 off = coerce_to(s, ov, INT8OID, -1, 1).v.i;
            if (off < 0) ereport(s, "22013", "invalid preceding or following size in window function");
            int want = (int)(sign * off), g = 0, i;
            /* walk peer groups */
            for (i = plo; want < 0 && i > 0; ) {
                int j = i - 1;
                while (j > 0 && win_peers(c, src, idx, def, j - 1, j)) j--;
                i = j; want++; g++;
            }
            if (want > 0) {
                i = phi;
                while (want > 0 && i + 1 < ni) {
                    int j = i + 1;
                    while (j + 1 < ni && win_peers(c, src, idx, def, j + 1, j)) j++;
                    i = j; want--;
                }
                while (i > 0 && win_peers(c, src, idx, def, i - 1, i)) i--;
            }
            *lo = (want > 0 && i >= ni - 1) ? ni : i;
            (void)g;
        } else {
            /* RANGE: the first row whose ordering value is inside the bound */
            if (!ov.isnull && (ov.type == INT2OID || ov.type == INT4OID || ov.type == INT8OID ||
                               ov.type == NUMERICOID || ov.type == FLOAT4OID ||
                               ov.type == FLOAT8OID)) {
                Datum z = d_int(INT4OID, 0);
                if (d_cmp(s, ov, z) < 0)
                    ereport(s, "22013", "invalid preceding or following size in window function");
            }
            bool desc = def->order[0].dir != 0;
            Datum base = win_ordval(c, src, idx, def, k);
            Datum args[2]; args[0] = base; args[1] = ov;
            bool f = false;
            Datum lim = call_builtin(s, (sign < 0) == !desc ? "-" : "+", args, 2, &f);
            int i = 0;
            while (i < ni) {
                Datum x = win_ordval(c, src, idx, def, i);
                if (x.type != lim.type && !x.isnull && !lim.isnull)
                    x = coerce_to(s, x, lim.type, -1, 2);
                int cmp = d_cmp(s, x, lim);
                if (desc ? cmp <= 0 : cmp >= 0) break;
                i++;
            }
            *lo = i;
        }
    }
    /* end */
    if (en == 4 || en < 0) *hi = ni - 1;
    else if (en == 2) *hi = (mode == 1) ? k : phi;
    else if (en == 0) *hi = -1;
    else {
        ExecCtx rc = *c; rc.rs = src; rc.row = src->rows[idx[k]]; rc.agg_phase = 0;
        Datum ov = eval(&rc, def->end_off);
        int sign = (en == 1) ? -1 : 1;
        if (mode == 1) {
            int64 off = coerce_to(s, ov, INT8OID, -1, 1).v.i;
            if (off < 0) ereport(s, "22013", "invalid preceding or following size in window function");
            *hi = k + (int)(sign * off);
        } else if (mode == 2) {
            int64 off = coerce_to(s, ov, INT8OID, -1, 1).v.i;
            if (off < 0) ereport(s, "22013", "invalid preceding or following size in window function");
            int want = (int)(sign * off), i;
            if (want >= 0) {
                i = phi;
                while (want > 0 && i + 1 < ni) {
                    int j = i + 1;
                    while (j + 1 < ni && win_peers(c, src, idx, def, j + 1, j)) j++;
                    i = j; want--;
                }
            } else {
                i = plo;
                while (want < 0 && i > 0) {
                    int j = i - 1;
                    while (j > 0 && win_peers(c, src, idx, def, j - 1, j)) j--;
                    i = j; want++;
                }
                while (i + 1 < ni && win_peers(c, src, idx, def, i + 1, i)) i++;
            }
            *hi = i;
        } else {
            if (!ov.isnull && (ov.type == INT2OID || ov.type == INT4OID || ov.type == INT8OID ||
                               ov.type == NUMERICOID || ov.type == FLOAT4OID ||
                               ov.type == FLOAT8OID)) {
                Datum z = d_int(INT4OID, 0);
                if (d_cmp(s, ov, z) < 0)
                    ereport(s, "22013", "invalid preceding or following size in window function");
            }
            bool desc = def->order[0].dir != 0;
            Datum base = win_ordval(c, src, idx, def, k);
            Datum args[2]; args[0] = base; args[1] = ov;
            bool f = false;
            Datum lim = call_builtin(s, (sign < 0) == !desc ? "-" : "+", args, 2, &f);
            int i = ni - 1;
            while (i >= 0) {
                Datum x = win_ordval(c, src, idx, def, i);
                if (x.type != lim.type && !x.isnull && !lim.isnull)
                    x = coerce_to(s, x, lim.type, -1, 2);
                int cmp = d_cmp(s, x, lim);
                if (desc ? cmp >= 0 : cmp <= 0) break;
                i--;
            }
            *hi = i;
        }
    }
    if (*lo < 0) *lo = 0;
    if (*hi >= ni) *hi = ni - 1;
}

void compute_windows(ExecCtx *c, RowSet *src, Expr **wins, int nwins, Datum ***out)
{
    Session *s = c->s;
    Datum **res = xmalloc((size_t)(src->nrows ? src->nrows : 1) * sizeof(Datum *));
    for (int i = 0; i < src->nrows; i++) {
        res[i] = xmalloc((size_t)(nwins ? nwins : 1) * sizeof(Datum));
        for (int k = 0; k < nwins; k++) res[i][k] = d_null(TEXTOID);
    }
    for (int w = 0; w < nwins; w++) {
        Expr *e = wins[w];
        WindowDef *def = e->over;
        /* PostgreSQL checks the frame clause before it runs anything */
        if (def && def->frame_mode == 0 &&
            (def->frame_start == 1 || def->frame_start == 3 ||
             def->frame_end == 1 || def->frame_end == 3)) {
            if (def->norder != 1)
                ereport_pos(s, "42P20", def->pos, "RANGE with offset PRECEDING/FOLLOWING "
                            "requires exactly one ORDER BY column");
            if (src->nrows) {
                ExecCtx pc = *c; pc.rs = src; pc.row = src->rows[0]; pc.agg_phase = 0;
                Datum probe = eval(&pc, def->order[0].e);
                Oid ot = probe.type;
                bool ok = (ot == INT2OID || ot == INT4OID || ot == INT8OID ||
                           ot == NUMERICOID || ot == FLOAT4OID || ot == FLOAT8OID ||
                           ot == DATEOID || ot == TIMESTAMPOID || ot == TIMESTAMPTZOID ||
                           ot == TIMEOID || ot == INTERVALOID);
                if (!ok) {
                    Expr *offe = def->start_off ? def->start_off : def->end_off;
                    char *tn = format_type_str(ot ? ot : TEXTOID, -1);
                    ereport_pos(s, "42883", offe ? offe->pos : def->pos,
                                "RANGE with offset PRECEDING/FOLLOWING is not supported "
                                "for column type %s", tn);
                }
            }
        }
        if (def && def->frame_start == 3 && def->frame_end == 1)
            ereport_pos(s, "42P20", def->end_off ? def->end_off->pos : def->pos,
                        "frame starting from following row cannot have preceding rows");
        if (def && def->frame_start == 4 && def->frame_end != 4)
            ereport_pos(s, "42P20", def->pos,
                        "frame starting from following row cannot end with current row");
        /* partition */
        int *part = xmalloc((size_t)(src->nrows ? src->nrows : 1) * sizeof(int));
        for (int i = 0; i < src->nrows; i++) part[i] = -1;
        int nparts = 0;
        Datum **pkeys = NULL;
        for (int i = 0; i < src->nrows; i++) {
            ExecCtx rc = *c; rc.rs = src; rc.row = src->rows[i]; rc.agg_phase = 0;
            Datum *key = row_new(def && def->npartition ? def->npartition : 1);
            for (int k = 0; def && k < def->npartition; k++) key[k] = eval(&rc, def->partition[k]);
            int found = -1;
            for (int k = 0; k < nparts; k++)
                if (!def || !def->npartition || rows_equal(s, pkeys[k], key, def->npartition)) { found = k; break; }
            if (found < 0) {
                pkeys = xrealloc(pkeys, (size_t)(nparts + 1) * sizeof(Datum *));
                pkeys[nparts] = key;
                found = nparts++;
            }
            part[i] = found;
        }
        for (int p = 0; p < nparts; p++) {
            int *idx = NULL, ni = 0;
            for (int i = 0; i < src->nrows; i++)
                if (part[i] == p) { idx = xrealloc(idx, (size_t)(ni + 1) * sizeof(int)); idx[ni++] = i; }
            /* order the partition */
            if (def && def->norder) {
                SortKey *keys = xmalloc((size_t)def->norder * sizeof(SortKey));
                for (int k = 0; k < def->norder; k++) {
                    keys[k].e = def->order[k].e; keys[k].dir = def->order[k].dir;
                    keys[k].nulls = def->order[k].nulls; keys[k].tgtidx = -1;
                }
                Datum **rows = xmalloc((size_t)(ni ? ni : 1) * sizeof(Datum *));
                for (int k = 0; k < ni; k++) rows[k] = src->rows[idx[k]];
                SortCtx sc = { c, keys, def->norder, src };
                g_sortctx = &sc;
                /* stable-ish: selection sort keeps ties in original order */
                for (int a = 1; a < ni; a++) {
                    Datum *rv = rows[a]; int iv = idx[a];
                    int b = a - 1;
                    while (b >= 0 && sort_cmp(&rv, &rows[b]) < 0) {
                        rows[b+1] = rows[b]; idx[b+1] = idx[b]; b--;
                    }
                    rows[b+1] = rv; idx[b+1] = iv;
                }
            }
            const char *nm = e->name;
            for (int k = 0; k < ni; k++) {
                int row = idx[k];
                Datum v;
                if (!strcmp(nm, "row_number")) v = d_int(INT8OID, k + 1);
                else if (!strcmp(nm, "rank") || !strcmp(nm, "dense_rank") ||
                         !strcmp(nm, "percent_rank") || !strcmp(nm, "cume_dist")) {
                    int rank = 1, dense = 1;
                    for (int q = 1; q <= k; q++) {
                        bool same = true;
                        if (def && def->norder) {
                            for (int z = 0; z < def->norder && same; z++) {
                                ExecCtx a = *c; a.rs = src; a.row = src->rows[idx[q]]; a.agg_phase = 0;
                                ExecCtx b = *c; b.rs = src; b.row = src->rows[idx[q-1]]; b.agg_phase = 0;
                                Datum x = eval(&a, def->order[z].e), y = eval(&b, def->order[z].e);
                                if (x.isnull != y.isnull || (!x.isnull && d_cmp(s, x, y) != 0)) same = false;
                            }
                        }
                        if (!same) { rank = q + 1; dense++; }
                    }
                    if (!strcmp(nm, "rank")) v = d_int(INT8OID, rank);
                    else if (!strcmp(nm, "dense_rank")) v = d_int(INT8OID, dense);
                    else if (!strcmp(nm, "percent_rank"))
                        v = d_float(FLOAT8OID, ni > 1 ? (double)(rank - 1) / (ni - 1) : 0);
                    else v = d_float(FLOAT8OID, (double)(k + 1) / ni);
                }
                else if (!strcmp(nm, "ntile")) {
                    ExecCtx rc = *c; rc.rs = src; rc.row = src->rows[row]; rc.agg_phase = 0;
                    int nb = e->nargs ? (int)coerce_to(s, eval(&rc, e->args[0]), INT4OID, -1, 1).v.i : 1;
                    if (nb < 1) nb = 1;
                    v = d_int(INT4OID, (int64)((int64)k * nb) / ni + 1);
                }
                else if (!strcmp(nm, "lag") || !strcmp(nm, "lead")) {
                    int off = 1;
                    if (e->nargs > 1) {
                        ExecCtx rc = *c; rc.rs = src; rc.row = src->rows[row]; rc.agg_phase = 0;
                        off = (int)coerce_to(s, eval(&rc, e->args[1]), INT4OID, -1, 1).v.i;
                    }
                    int tk = (!strcmp(nm, "lag")) ? k - off : k + off;
                    if (tk < 0 || tk >= ni) {
                        if (e->nargs > 2) {
                            ExecCtx rc = *c; rc.rs = src; rc.row = src->rows[row]; rc.agg_phase = 0;
                            v = eval(&rc, e->args[2]);
                        } else {
                            Oid at = TEXTOID;
                            if (e->nargs && e->args[0]->tag == X_COLREF) {
                                int ci = find_col(src, e->args[0]->qual, e->args[0]->colname, s, 0);
                                if (ci >= 0) at = src->cols[ci].type;
                            }
                            v = d_null(at);
                        }
                    } else {
                        ExecCtx rc = *c; rc.rs = src; rc.row = src->rows[idx[tk]]; rc.agg_phase = 0;
                        v = eval(&rc, e->args[0]);
                    }
                }
                else if (!strcmp(nm, "first_value") || !strcmp(nm, "last_value") ||
                         !strcmp(nm, "nth_value")) {
                    /* these read the frame, so the exclusions apply */
                    int flo, fhi;
                    win_frame(c, src, idx, ni, def, k, &flo, &fhi);
                    int *fr = xmalloc((size_t)(ni ? ni : 1) * sizeof(int)), nfr = 0;
                    for (int q = flo; q <= fhi; q++) {
                        if (def && def->frame_excl) {
                            if (def->frame_excl == 1 && q == k) continue;
                            if ((def->frame_excl == 2 || def->frame_excl == 3) &&
                                win_peers(c, src, idx, def, q, k) &&
                                !(def->frame_excl == 3 && q == k)) continue;
                        }
                        fr[nfr++] = q;
                    }
                    int tk = -1;
                    if (!strcmp(nm, "first_value")) tk = nfr ? fr[0] : -1;
                    else if (!strcmp(nm, "last_value")) tk = nfr ? fr[nfr-1] : -1;
                    else {
                        ExecCtx rc = *c; rc.rs = src; rc.row = src->rows[row]; rc.agg_phase = 0;
                        int nth = (int)coerce_to(s, eval(&rc, e->args[1]), INT4OID, -1, 1).v.i;
                        tk = (nth >= 1 && nth <= nfr) ? fr[nth-1] : -1;
                    }
                    free(fr);
                    if (tk < 0 || tk >= ni) v = d_null(TEXTOID);
                    else {
                        ExecCtx rc = *c; rc.rs = src; rc.row = src->rows[idx[tk]]; rc.agg_phase = 0;
                        v = eval(&rc, e->args[0]);
                    }
                }
                else {
                    /* aggregate over the window frame */
                    int lo, hi;
                    win_frame(c, src, idx, ni, def, k, &lo, &hi);
                    int *sub = NULL, nsub = 0;
                    if (hi >= lo) {
                        sub = xmalloc((size_t)(hi - lo + 1) * sizeof(int));
                        for (int q = lo; q <= hi; q++) {
                            if (def && def->frame_excl) {
                                if (def->frame_excl == 1 && q == k) continue;
                                if ((def->frame_excl == 2 || def->frame_excl == 3) &&
                                    win_peers(c, src, idx, def, q, k)) {
                                    if (def->frame_excl == 3 && q == k) { }
                                    else continue;
                                }
                            }
                            sub[nsub++] = idx[q];
                        }
                    }
                    if (!nsub) v = d_null(TEXTOID);
                    else {
                        Expr tmp = *e;
                        tmp.tag = X_AGG; tmp.over = NULL;
                        ExecCtx rc = *c; rc.rs = src; rc.row = src->rows[row]; rc.agg_phase = 0;
                        v = agg_compute(&rc, &tmp, src, sub, nsub);
                    }
                    free(sub);
                }
                res[row][w] = v;
            }
            free(idx);
        }
        free(part);
    }
    *out = res;
}

/* ====================================================================== DML */
uint64 xact_xid(Session *s);
void   xact_touch(Session *s, Table *t);

static Table *resolve_table_at(Session *s, const char *schema, const char *name,
                               bool forwrite, int pos)
{
    Table *t = tbl_find_sp(s, schema, name);
    if (!t) {
        if (schema) ereport_pos(s, "42P01", pos, "relation \"%s.%s\" does not exist", schema, name);
        ereport_pos(s, "42P01", pos, "relation \"%s\" does not exist", name);
    }
    if (forwrite && t->relkind == 'v' && !t->viewdef)
        ereport(s, "55000", "cannot change view \"%s\"", name);
    return t;
}

static Table *resolve_table(Session *s, const char *schema, const char *name, bool forwrite)
{
    Table *t = tbl_find_sp(s, schema, name);
    if (!t) {
        if (schema) ereport(s, "42P01", "relation \"%s.%s\" does not exist", schema, name);
        ereport(s, "42P01", "relation \"%s\" does not exist", name);
    }
    if (forwrite && t->relkind == 'v' && !t->viewdef)
        ereport(s, "55000", "cannot change view \"%s\"", name);
    return t;
}

/* PostgreSQL names a CHECK constraint after the first column its expression
 * mentions. */
static const char *check_first_col(Table *t, const char *src)
{
    if (!src) return NULL;
    for (const char *q = src; *q; ) {
        if (isalpha((unsigned char)*q) || *q == '_') {
            const char *st = q;
            while (isalnum((unsigned char)*q) || *q == '_') q++;
            char *id = xstrndup(st, (size_t)(q - st));
            char *lo = lower_ident(id);
            int ci = col_index(t, lo);
            free(id);
            if (ci >= 0) { free(lo); return t->cols[ci].name; }
            free(lo);
            continue;
        }
        if (*q == '\'') { q++; while (*q && *q != '\'') q++; if (*q) q++; continue; }
        q++;
    }
    return NULL;
}

/* The name PostgreSQL derives for a multi-column key constraint. */
bool expr_has_subquery(Expr *e);

/* The first subquery inside a generation expression, for the error position. */
static Expr *gen_find_sublink(Expr *e)
{
    if (!e) return NULL;
    if (e->tag == X_SUBLINK || e->tag == X_EXISTS) return e;
    for (int i = 0; i < e->nargs; i++) { Expr *r = gen_find_sublink(e->args[i]); if (r) return r; }
    return NULL;
}

/* The first reference to another generated column of the same CREATE TABLE. */
static Expr *gen_find_gencol(Expr *e, CreateTableStmt *st)
{
    if (!e) return NULL;
    if (e->tag == X_COLREF) {
        for (int i = 0; i < st->ncols; i++)
            if (st->cols[i]->name && !strcmp(st->cols[i]->name, e->colname) &&
                st->cols[i]->generated) return e;
        return NULL;
    }
    for (int i = 0; i < e->nargs; i++) { Expr *r = gen_find_gencol(e->args[i], st); if (r) return r; }
    return NULL;
}

/* Does a default expression call something volatile? */
/* Does the expression mention this column by name? */
static bool expr_names_column(Expr *e, const char *name)
{
    if (!e || !name) return false;
    if (e->tag == X_COLREF && e->colname && !strcmp(e->colname, name)) return true;
    for (int i = 0; i < e->nargs; i++) if (expr_names_column(e->args[i], name)) return true;
    for (int i = 0; i < e->nwhen; i++) {
        if (expr_names_column(e->when_cond[i], name)) return true;
        if (expr_names_column(e->when_res[i], name)) return true;
    }
    return expr_names_column(e->case_arg, name) || expr_names_column(e->case_else, name) ||
           expr_names_column(e->testexpr, name);
}

static bool expr_is_volatile(Session *s, const char *src)
{
    (void)s;
    static const char *vol[] = {"nextval", "random", "clock_timestamp",
                                "currval", "setval", "uuid_generate", NULL};
    for (int i = 0; vol[i]; i++) if (strstr(src, vol[i])) return true;
    return false;
}

static char *cols_join(Table *t, int *keys, int nkeys)
{
    Buf b; buf_init(&b);
    for (int i = 0; i < nkeys; i++) {
        if (i) buf_addc(&b, '_');
        if (keys[i] > 0 && keys[i] <= t->ncols) buf_adds(&b, t->cols[keys[i]-1].name);
        else buf_adds(&b, "expr");
    }
    return buf_steal(&b);
}

static char *default_con_name(Session *s, Table *t, const char *col, char kind)
{
    (void)s;
    Buf b; buf_init(&b);
    if (kind == 'p') buf_printf(&b, "%s_pkey", t->name);
    else if (kind == 'u') buf_printf(&b, "%s_%s_key", t->name, col ? col : "x");
    else if (kind == 'c') buf_printf(&b, "%s_%s_check", t->name, col ? col : "x");
    else if (kind == 'f') buf_printf(&b, "%s_%s_fkey", t->name, col ? col : "x");
    else if (kind == 'n') buf_printf(&b, "%s_%s_not_null", t->name, col ? col : "x");
    else buf_printf(&b, "%s_%s", t->name, col ? col : "x");
    return buf_steal(&b);
}

static Datum eval_default(Session *s, Table *t, Column *col, Datum *row);

static void check_constraints(Session *s, Table *t, Datum *vals, Row *skip);

#if PGVC_VN >= 180
/* WITHOUT OVERLAPS needs a range last and gist-indexable columns before it. */
static void check_without_overlaps(Session *s, Table *t, TblCons *tc)
{
    if (tc->ncols < 2)
        ereport(s, "42P16", "constraint using WITHOUT OVERLAPS needs at least two columns");
    for (int k = 0; k < tc->ncols; k++) {
        int ci = col_index(t, tc->cols[k]);
        if (ci < 0) continue;
        Oid ty = t->cols[ci].type;
        bool rangey = type_is_range_like(ty) || type_is_multirange_like(ty);
        if (k == tc->ncols - 1) {
            if (!rangey)
                ereport_pos(s, "42804", tc->pos,
                            "column \"%s\" in WITHOUT OVERLAPS is not a range or "
                            "multirange type", tc->cols[k]);
        } else if (!rangey) {
            char *tn = format_type_str(ty, -1);
            err_hint(s, "You must specify an operator class for the index or define a "
                        "default operator class for the data type.");
            ereport(s, "42704", "data type %s has no default operator class for access "
                    "method \"gist\"", tn);
        }
    }
}
#endif

/* The value of partition key column k for a row of the partitioned table t. */
static Datum partkey_val(Session *s, Table *t, Datum *vals, int k)
{
    if (t->partkeyexpr && t->partkeyexpr[k]) {
        Expr *e = parse_expr_string(s, t->partkeyexpr[k]);
        RowSet rs; memset(&rs, 0, sizeof rs);
        rs.ncols = t->ncols;
        rs.cols = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(ColMeta));
        for (int q = 0; q < t->ncols; q++) {
            rs.cols[q].rel = t->name; rs.cols[q].name = t->cols[q].name;
            rs.cols[q].type = t->cols[q].type; rs.cols[q].typmod = t->cols[q].typmod;
            rs.cols[q].hidden = false;
        }
        ExecCtx c2; memset(&c2, 0, sizeof c2);
        c2.s = s; c2.rs = &rs; c2.row = vals;
        Datum d = eval(&c2, e);
        free(rs.cols);
        return d;
    }
    return vals[t->partkey[k] - 1];
}

/* ---------------------------------------------------------- hash partitions
 * Which partition a row lands in is observable, so the hash has to be
 * PostgreSQL's: Bob Jenkins' lookup3 in the "extended" 64-bit form the
 * hash_* opclass support functions use, combined with HASH_PARTITION_SEED. */
#define PGVC_HROT(x,k) (((x) << (k)) | ((x) >> (32 - (k))))
#define PGVC_HMIX(a,b,c) \
{ \
  a -= c;  a ^= PGVC_HROT(c, 4);  c += b; \
  b -= a;  b ^= PGVC_HROT(a, 6);  a += c; \
  c -= b;  c ^= PGVC_HROT(b, 8);  b += a; \
  a -= c;  a ^= PGVC_HROT(c,16);  c += b; \
  b -= a;  b ^= PGVC_HROT(a,19);  a += c; \
  c -= b;  c ^= PGVC_HROT(b, 4);  b += a; \
}
#define PGVC_HFINAL(a,b,c) \
{ \
  c ^= b; c -= PGVC_HROT(b,14); \
  a ^= c; a -= PGVC_HROT(c,11); \
  b ^= a; b -= PGVC_HROT(a,25); \
  c ^= b; c -= PGVC_HROT(b,16); \
  a ^= c; a -= PGVC_HROT(c, 4); \
  b ^= a; b -= PGVC_HROT(a,14); \
  c ^= b; c -= PGVC_HROT(b,24); \
}

static uint64 hash_any_ext(const unsigned char *k, int keylen, uint64 seed)
{
    uint32 a, b, c;
    uint32 len = (uint32)keylen;
    a = b = c = 0x9e3779b9u + len + 3923095u;
    if (seed != 0) {
        a += (uint32)(seed >> 32);
        b += (uint32)seed;
        PGVC_HMIX(a, b, c);
    }
    while (len >= 12) {
        uint32 k0, k1, k2;
        memcpy(&k0, k, 4); memcpy(&k1, k + 4, 4); memcpy(&k2, k + 8, 4);
        a += k0; b += k1; c += k2;
        PGVC_HMIX(a, b, c);
        k += 12; len -= 12;
    }
    /* the tail, little-endian, exactly as hash_any_extended assembles it */
    switch (len) {
    case 11: c += ((uint32)k[10] << 24);  /* fall through */
    case 10: c += ((uint32)k[9] << 16);   /* fall through */
    case 9:  c += ((uint32)k[8] << 8);    /* fall through */
    case 8:  { uint32 k0, k1; memcpy(&k0, k, 4); memcpy(&k1, k + 4, 4);
               b += k1; a += k0; break; }
    case 7:  b += ((uint32)k[6] << 16);   /* fall through */
    case 6:  b += ((uint32)k[5] << 8);    /* fall through */
    case 5:  b += k[4];                   /* fall through */
    case 4:  { uint32 k0; memcpy(&k0, k, 4); a += k0; break; }
    case 3:  a += ((uint32)k[2] << 16);   /* fall through */
    case 2:  a += ((uint32)k[1] << 8);    /* fall through */
    case 1:  a += k[0];                   /* fall through */
    case 0:  break;
    }
    PGVC_HFINAL(a, b, c);
    return ((uint64)b << 32) | c;
}

static uint64 hash_uint32_ext(uint32 k, uint64 seed)
{
    uint32 a, b, c;
    a = b = c = 0x9e3779b9u + (uint32)sizeof(uint32) + 3923095u;
    if (seed != 0) {
        a += (uint32)(seed >> 32);
        b += (uint32)seed;
        PGVC_HMIX(a, b, c);
    }
    a += k;
    PGVC_HFINAL(a, b, c);
    return ((uint64)b << 32) | c;
}

#define PGVC_HASH_PARTITION_SEED 0x7A5B22367996DCFDULL

static uint64 hash_combine64(uint64 a, uint64 b)
{
    a ^= b + 0x49a0f4dd15e5a8e3ULL + (a << 54) + (a >> 7);
    return a;
}

/* One partition key column folded into the running row hash. */
uint64 part_hash_key(Session *s, Datum d, uint64 running)
{
    if (d.isnull) return running;              /* NULLs contribute nothing */
    uint64 h;
    switch (d.type) {
    case BOOLOID: case CHAROID:
        h = hash_uint32_ext((uint32)(int32)d.v.i, PGVC_HASH_PARTITION_SEED); break;
    case INT2OID:
        h = hash_uint32_ext((uint32)(int32)(int16)d.v.i, PGVC_HASH_PARTITION_SEED); break;
    case INT4OID: case DATEOID: case OIDOID:
    case REGCLASSOID: case REGTYPEOID: case REGPROCOID:
    case REGNAMESPACEOID: case REGROLEOID:
        h = hash_uint32_ext((uint32)(int32)d.v.i, PGVC_HASH_PARTITION_SEED); break;
    case INT8OID: case TIMESTAMPOID: case TIMESTAMPTZOID: case TIMEOID: {
        int64 val = d.v.i;
        uint32 lo = (uint32)val, hi = (uint32)((uint64)val >> 32);
        lo ^= (val >= 0) ? hi : ~hi;
        h = hash_uint32_ext(lo, PGVC_HASH_PARTITION_SEED);
        break;
    }
    case FLOAT4OID: case FLOAT8OID: {
        double v = d.v.f;
        if (v == 0) v = 0;                     /* -0 hashes as 0 */
        if (d.type == FLOAT4OID) { float f = (float)v;
            h = hash_any_ext((const unsigned char *)&f, 4, PGVC_HASH_PARTITION_SEED); }
        else h = hash_any_ext((const unsigned char *)&v, 8, PGVC_HASH_PARTITION_SEED);
        break;
    }
    case BPCHAROID: {
        int32 n = d.v.s.len;
        while (n > 0 && d.v.s.p[n-1] == ' ') n--;
        h = hash_any_ext((const unsigned char *)d.v.s.p, n, PGVC_HASH_PARTITION_SEED);
        break;
    }
    default: {
        if (d.type == TEXTOID || d.type == VARCHAROID || d.type == NAMEOID ||
            d.type == BYTEAOID || d.type == UUIDOID) {
            h = hash_any_ext((const unsigned char *)d.v.s.p, d.v.s.len,
                             PGVC_HASH_PARTITION_SEED);
        } else {
            char *o = out_func(s, d);
            h = hash_any_ext((const unsigned char *)o, (int)strlen(o),
                             PGVC_HASH_PARTITION_SEED);
            free(o);
        }
        break;
    }
    }
    return hash_combine64(running, h);
}

/* PostgreSQL compares a row against a range bound column by column; a bound
 * element of MINVALUE/MAXVALUE is an infinity that ends the comparison. */
static int rbound_cmp(Session *s, Table *t, Datum *vals,
                      const Datum *b, const signed char *kind, int nb)
{
    for (int k = 0; k < t->npartkey; k++) {
        if (k >= nb) break;
        if (kind && kind[k] < 0) return 1;        /* row is above -infinity */
        if (kind && kind[k] > 0) return -1;       /* row is below +infinity */
        int c = d_cmp(s, partkey_val(s, t, vals, k), b[k]);
        if (c) return c;
    }
    return 0;
}

uint64 part_hash_key(Session *s, Datum d, uint64 seed);

/* Does a row belong in partition p of partitioned table t? */
static bool partition_accepts(Session *s, Table *t, Table *p, Datum *vals)
{
    if (p->pbound.is_default) return false;
    if (t->partstrat == 'r') {
        for (int k = 0; k < t->npartkey; k++)
            if (partkey_val(s, t, vals, k).isnull) return false;
        return rbound_cmp(s, t, vals, p->pbound.from, p->pbound.fromkind, p->pbound.nfrom) >= 0 &&
               rbound_cmp(s, t, vals, p->pbound.to,   p->pbound.tokind,   p->pbound.nto)   <  0;
    }
    if (t->partstrat == 'l') {
        Datum v = partkey_val(s, t, vals, 0);
        for (int k = 0; k < p->pbound.nin; k++) {
            if (v.isnull) { if (p->pbound.in[k].isnull) return true; continue; }
            if (!p->pbound.in[k].isnull && d_cmp(s, v, p->pbound.in[k]) == 0) return true;
        }
        return false;
    }
    if (t->partstrat == 'h') {
        if (p->pbound.modulus <= 0) return false;
        uint64 h = 0;
        for (int k = 0; k < t->npartkey; k++)
            h = part_hash_key(s, partkey_val(s, t, vals, k), h);
        return (int)(h % (uint64)p->pbound.modulus) == p->pbound.remainder;
    }
    return false;
}

/* The whole "(cols) = (values)" clause PostgreSQL puts in the DETAIL when no
 * partition accepts a row. */
static void partkey_detail(Session *s, Table *t, Datum *vals)
{
    Buf kb; buf_init(&kb);
    Buf vb; buf_init(&vb);
    for (int k = 0; k < t->npartkey; k++) {
        if (k) { buf_adds(&kb, ", "); buf_adds(&vb, ", "); }
        if (t->partkeyexpr && t->partkeyexpr[k]) buf_adds(&kb, t->partkeyexpr[k]);
        else buf_adds(&kb, t->cols[t->partkey[k]-1].name);
        Datum v = partkey_val(s, t, vals, k);
        if (v.isnull) buf_adds(&vb, "null");
        else { char *o = out_func(s, v); buf_adds(&vb, o); free(o); }
    }
    err_detail(s, "Partition key of the failing row contains (%s) = (%s).",
               kb.data ? kb.data : "", vb.data ? vb.data : "");
    buf_free(&kb); buf_free(&vb);
}

/* Does the row still belong in this leaf partition, at every level? */
static bool row_still_in(Session *s, Table *base, Table *leaf, Datum *vals)
{
    Table *cur = leaf;
    while (cur && cur != base) {
        Table *par = tbl_find_oid(s->db, cur->parent);
        if (!par) return false;
        if (cur->pbound.is_default) {
            for (int i = 0; i < s->db->ntables; i++) {
                Table *sib = s->db->tables[i];
                if (sib->parent != par->oid || sib == cur || sib->pbound.is_default) continue;
                if (partition_accepts(s, par, sib, vals)) return false;
            }
        } else if (!partition_accepts(s, par, cur, vals)) return false;
        cur = par;
    }
    return true;
}

static Table *route_partition(Session *s, Table *t, Datum *vals)
{
    if (!t->partstrat) return t;
    Database *db = s->db;
    Table *deflt = NULL;
    for (int i = 0; i < db->ntables; i++) {
        Table *p = db->tables[i];
        if (p->parent != t->oid) continue;
        if (p->pbound.is_default) { deflt = p; continue; }
        if (partition_accepts(s, t, p, vals)) return route_partition(s, p, vals);
    }
    if (deflt) return route_partition(s, deflt, vals);
    partkey_detail(s, t, vals);
    ereport(s, "23514", "no partition of relation \"%s\" found for row", t->name);
}

/* A row inserted straight into a partition still has to satisfy the bound
 * every ancestor imposes on it. */
static void check_partition_bound(Session *s, Table *t, Datum *vals)
{
    Table *child = t;
    while (child->parent) {
        Table *parent = tbl_find_oid(s->db, child->parent);
        if (!parent) break;
        /* re-map the row into the parent's column order by name */
        Datum *pv = row_new(parent->ncols);
        for (int i = 0; i < parent->ncols; i++) {
            int src = col_index(child, parent->cols[i].name);
            pv[i] = (src >= 0) ? vals[src] : d_null(parent->cols[i].type);
        }
        bool ok;
        if (child->pbound.is_default) {
            ok = true;
            for (int i = 0; i < s->db->ntables && ok; i++) {
                Table *sib = s->db->tables[i];
                if (sib->parent != parent->oid || sib == child) continue;
                if (partition_accepts(s, parent, sib, pv)) ok = false;
            }
        } else ok = partition_accepts(s, parent, child, pv);
        free(pv);
        if (!ok) {
            Buf b; buf_init(&b);
            for (int k = 0; k < t->ncols; k++) {
                if (t->cols[k].dropped) continue;
                if (b.len) buf_adds(&b, ", ");
                char *o = out_func(s, vals[k]);
                buf_adds(&b, vals[k].isnull ? "null" : o);
                free(o);
            }
            err_detail(s, "Failing row contains (%s).", b.data ? b.data : "");
            ereport(s, "23514", "new row for relation \"%s\" violates partition constraint",
                    t->name);
        }
        child = parent;
    }
}

static int64 seq_nextval(Session *s, Table *seq);

static void apply_generated(Session *s, Table *t, Datum *vals)
{
    for (int i = 0; i < t->ncols; i++) {
        Column *c = &t->cols[i];
        if (!c->generated || !c->genexpr) continue;
        Expr *e = parse_expr_string(s, c->genexpr);
        RowSet rs; memset(&rs, 0, sizeof rs);
        rs.ncols = t->ncols;
        rs.cols = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(ColMeta));
        for (int k = 0; k < t->ncols; k++) {
            rs.cols[k].rel = t->name; rs.cols[k].name = t->cols[k].name;
            rs.cols[k].type = t->cols[k].type; rs.cols[k].typmod = t->cols[k].typmod;
            rs.cols[k].hidden = false;
        }
        ExecCtx c2; memset(&c2, 0, sizeof c2);
        c2.s = s; c2.rs = &rs; c2.row = vals;
        vals[i] = coerce_to(s, eval(&c2, e), c->type, c->typmod, 1);
    }
}

static void insert_row_into(Session *s, Table *base, Datum *vals, Table **landed)
{
    Table *t = route_partition(s, base, vals);
    if (landed) *landed = t;
    if (t != base) {
        /* re-map by column name */
        Datum *nv = row_new(t->ncols);
        for (int i = 0; i < t->ncols; i++) {
            int src = col_index(base, t->cols[i].name);
            nv[i] = (src >= 0) ? vals[src] : d_null(t->cols[i].type);
        }
        vals = nv;
    } else if (t->parent)
        check_partition_bound(s, t, vals);   /* inserted straight into a partition */
    check_constraints(s, t, vals, NULL);
    lock_table_write(s, t);
    Row *r = xmalloc(sizeof *r);
    memset(r, 0, sizeof *r);
    r->xmin = xact_xid(s); r->xmax = 0;
    r->n = t->ncols;
    r->vals = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(Datum));
    for (int i = 0; i < t->ncols; i++) r->vals[i] = vals[i];
    if (t->nrows == t->caprows) {
        t->caprows = t->caprows ? t->caprows * 2 : 16;
        t->rows = xrealloc(t->rows, (size_t)t->caprows * sizeof(Row *));
    }
    t->rows[t->nrows++] = r;
    xact_touch(s, t);
}

/* Check the FOREIGN KEY constraints `owner` declares against a row of
 * `reported`, naming `reported` in any error.  They differ when the constraint
 * belongs to a partitioned parent and the row is going into a partition. */
static void fk_check_insert(Session *s, Table *owner, Table *reported, Datum *vals)
{
    for (int i = 0; i < owner->ncons; i++) {
        Constraint *c = owner->cons[i];
        if (c->contype != 'f') continue;
        if (c->not_enforced) continue;      /* NOT ENFORCED is never applied */
        int idx[8];
        int nk = c->nkeys > 8 ? 8 : c->nkeys;
        bool mapped = true;
        for (int k = 0; k < nk; k++) {
            if (owner == reported) { idx[k] = c->keys[k] - 1; continue; }
            int ci = col_index(reported, owner->cols[c->keys[k]-1].name);
            if (ci < 0) { mapped = false; break; }
            idx[k] = ci;
        }
        if (!mapped) continue;
        bool anynull = false;
        for (int k = 0; k < nk; k++) if (vals[idx[k]].isnull) anynull = true;
        if (anynull) continue;
        Table *ref = tbl_find_oid(s->db, c->fk_reltable);
        if (!ref) continue;
#if PGVC_VN >= 180
        if (c->fk_period) {
            /* the child period must be covered by the matching parent periods */
            int pk = idx[nk-1], rk = c->fkeys[c->nfkeys-1] - 1;
            Datum need = vals[pk];
            bool covered = false;
            if (!need.isnull && need.v.rng && !need.v.rng->empty) {
                Datum acc = d_null(need.type);
                bool first = true;
                for (int r = 0; r < ref->nrows; r++) {
                    Row *row = ref->rows[r];
                    if (!row_visible(s, row)) continue;
                    bool same = true;
                    for (int k = 0; k + 1 < nk && same; k++) {
                        Datum a = vals[idx[k]], b2 = row->vals[c->fkeys[k]-1];
                        if (a.isnull || b2.isnull || d_cmp(s, a, b2) != 0) same = false;
                    }
                    if (!same) continue;
                    Datum pv = row->vals[rk];
                    if (pv.isnull) continue;
                    if (first) { acc = pv; first = false; continue; }
                    Datum ar[2]; ar[0] = acc; ar[1] = pv;
                    bool f4 = false;
                    Datum u = call_builtin(s, "+", ar, 2, &f4);
                    if (f4 && !u.isnull) acc = u;
                }
                if (!first) {
                    Datum ar[2]; ar[0] = acc; ar[1] = need;
                    bool f5 = false;
                    Datum ct = call_builtin(s, "@>", ar, 2, &f5);
                    if (f5 && !ct.isnull && ct.v.i) covered = true;
                }
            }
            if (!covered) {
                Buf kb; buf_init(&kb); Buf vb; buf_init(&vb);
                for (int k = 0; k < nk; k++) {
                    if (k) { buf_adds(&kb, ", "); buf_adds(&vb, ", "); }
                    buf_adds(&kb, reported->cols[idx[k]].name);
                    char *o = out_func(s, vals[idx[k]]);
                    buf_adds(&vb, o); free(o);
                }
                err_detail(s, "Key (%s)=(%s) is not present in table \"%s\".",
                           kb.data, vb.data, ref->name);
                ereport(s, "23503", "insert or update on table \"%s\" violates foreign key "
                        "constraint \"%s\"", reported->name, c->name);
            }
            continue;
        }
#endif
        bool hit = false;
        for (int r = 0; r < ref->nrows && !hit; r++) {
            Row *row = ref->rows[r];
            if (!row_visible(s, row)) continue;
            bool same = true;
            for (int k = 0; k < nk && same; k++) {
                Datum a = vals[idx[k]], b2 = row->vals[c->fkeys[k]-1];
                if (a.isnull || b2.isnull || d_cmp(s, a, b2) != 0) same = false;
            }
            if (same) hit = true;
        }
        if (hit) continue;
        Buf kb; buf_init(&kb); Buf vb; buf_init(&vb);
        for (int k = 0; k < nk; k++) {
            if (k) { buf_adds(&kb, ", "); buf_adds(&vb, ", "); }
            buf_adds(&kb, reported->cols[idx[k]].name);
            char *o = out_func(s, vals[idx[k]]);
            buf_adds(&vb, o); free(o);
        }
        err_detail(s, "Key (%s)=(%s) is not present in table \"%s\".", kb.data, vb.data, ref->name);
        ereport(s, "23503", "insert or update on table \"%s\" violates foreign key constraint \"%s\"",
                reported->name, c->name);
    }
}

static void check_constraints(Session *s, Table *t, Datum *vals, Row *skip)
{
    /* NOT NULL */
    for (int i = 0; i < t->ncols; i++) {
        if (t->cols[i].dropped || !t->cols[i].notnull) continue;
        if (!vals[i].isnull) continue;
        Buf b; buf_init(&b);
        for (int k = 0; k < t->ncols; k++) {
            if (k) buf_adds(&b, ", ");
            char *o = out_func(s, vals[k]);
            buf_adds(&b, vals[k].isnull ? "null" : o);
            free(o);
        }
        err_detail(s, "Failing row contains (%s).", b.data ? b.data : "");
#if PGVC_VN >= 130
        ereport(s, "23502", "null value in column \"%s\" of relation \"%s\" violates not-null constraint",
                t->cols[i].name, t->name);
#else
        ereport(s, "23502", "null value in column \"%s\" violates not-null constraint", t->cols[i].name);
#endif
    }
    /* CHECK */
    for (int i = 0; i < t->ncons; i++) {
        Constraint *c = t->cons[i];
        if (c->contype != 'c' || !c->checksrc) continue;
        if (c->not_enforced) continue;      /* NOT ENFORCED is never applied */
        Expr *e = parse_expr_string(s, c->checksrc);
        RowSet rs; memset(&rs, 0, sizeof rs);
        rs.ncols = t->ncols;
        rs.cols = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(ColMeta));
        for (int k = 0; k < t->ncols; k++) {
            rs.cols[k].rel = t->name; rs.cols[k].name = t->cols[k].name;
            rs.cols[k].type = t->cols[k].type; rs.cols[k].typmod = t->cols[k].typmod;
            rs.cols[k].hidden = false;
        }
        ExecCtx c2; memset(&c2, 0, sizeof c2);
        c2.s = s; c2.rs = &rs; c2.row = vals;
        Datum v = to_bool(s, eval(&c2, e));
        if (!v.isnull && !v.v.i) {
            Buf b; buf_init(&b);
            for (int k = 0; k < t->ncols; k++) {
                if (k) buf_adds(&b, ", ");
                char *o = out_func(s, vals[k]);
                buf_adds(&b, vals[k].isnull ? "null" : o);
                free(o);
            }
            err_detail(s, "Failing row contains (%s).", b.data ? b.data : "");
            ereport(s, "23514", "new row for relation \"%s\" violates check constraint \"%s\"",
                    t->name, c->name);
        }
    }
    /* UNIQUE / PRIMARY KEY */
    for (int i = 0; i < t->ncons; i++) {
        Constraint *c = t->cons[i];
        if (c->contype != 'p' && c->contype != 'u') continue;
        bool anynull = false;
        for (int k = 0; k < c->nkeys; k++) if (vals[c->keys[k]-1].isnull) anynull = true;
        if (anynull && c->contype == 'u' && !c->nulls_nd) continue;
#if PGVC_VN >= 180
        if (c->without_overlaps) {
            /* WITHOUT OVERLAPS: the last key overlaps instead of matching */
            int pk = c->keys[c->nkeys-1] - 1;
            Datum pv = vals[pk];
            if (!pv.isnull && ((pv.v.rng && type_is_range_like(pv.type) && pv.v.rng->empty) ||
                               (type_is_multirange_like(pv.type) && pv.v.mrng && pv.v.mrng->n == 0)))
                ereport(s, "22000", "empty WITHOUT OVERLAPS value found in column \"%s\" "
                        "in relation \"%s\"", t->cols[pk].name, t->name);
            if (anynull) continue;
            for (int r = 0; r < t->nrows; r++) {
                Row *row = t->rows[r];
                if (row == skip || !row_visible(s, row)) continue;
                bool same = true;
                for (int k = 0; k + 1 < c->nkeys && same; k++) {
                    Datum a = vals[c->keys[k]-1], b2 = row->vals[c->keys[k]-1];
                    if (a.isnull || b2.isnull || d_cmp(s, a, b2) != 0) same = false;
                }
                if (!same) continue;
                Datum ov[2]; ov[0] = pv; ov[1] = row->vals[pk];
                if (ov[1].isnull) continue;
                bool f2 = false;
                Datum r2 = call_builtin(s, "&&", ov, 2, &f2);
                if (!f2 || r2.isnull || !r2.v.i) continue;
                Buf kb; buf_init(&kb); Buf vb; buf_init(&vb); Buf ob; buf_init(&ob);
                for (int k = 0; k < c->nkeys; k++) {
                    if (k) { buf_adds(&kb, ", "); buf_adds(&vb, ", "); buf_adds(&ob, ", "); }
                    buf_adds(&kb, t->cols[c->keys[k]-1].name);
                    char *o1 = out_func(s, vals[c->keys[k]-1]);
                    char *o2 = out_func(s, row->vals[c->keys[k]-1]);
                    buf_adds(&vb, o1); buf_adds(&ob, o2);
                    free(o1); free(o2);
                }
                err_detail(s, "Key (%s)=(%s) conflicts with existing key (%s)=(%s).",
                           kb.data, vb.data, kb.data, ob.data);
                ereport(s, "23P01", "conflicting key value violates exclusion constraint \"%s\"",
                        c->name);
            }
            continue;
        }
#endif
        for (int r = 0; r < t->nrows; r++) {
            Row *row = t->rows[r];
            if (row == skip || !row_visible(s, row)) continue;
            bool same = true;
            for (int k = 0; k < c->nkeys && same; k++) {
                Datum a = vals[c->keys[k]-1], b2 = row->vals[c->keys[k]-1];
                if (a.isnull || b2.isnull) {
                    /* NULLS NOT DISTINCT makes two nulls equal */
                    if (c->nulls_nd && a.isnull && b2.isnull) continue;
                    same = false;
                }
                else if (d_cmp(s, a, b2) != 0) same = false;
            }
            if (!same) continue;
            Buf kb; buf_init(&kb);
            Buf vb; buf_init(&vb);
            for (int k = 0; k < c->nkeys; k++) {
                if (k) { buf_adds(&kb, ", "); buf_adds(&vb, ", "); }
                buf_adds(&kb, t->cols[c->keys[k]-1].name);
                char *o = out_func(s, vals[c->keys[k]-1]);
                buf_adds(&vb, vals[c->keys[k]-1].isnull ? "null" : o); free(o);
            }
            err_detail(s, "Key (%s)=(%s) already exists.", kb.data, vb.data);
            ereport(s, "23505", "duplicate key value violates unique constraint \"%s\"", c->name);
        }
    }
    /* FOREIGN KEY.  A foreign key declared on a partitioned table is enforced
     * on the partition the row lands in, and the error names that partition
     * while the constraint keeps the parent's name, so the owner of the
     * constraint and the table reported are not always the same table. */
    fk_check_insert(s, t, t, vals);
    for (Oid up = t->parent; up; ) {
        Table *anc = tbl_find_oid(s->db, up);
        if (!anc) break;
        fk_check_insert(s, anc, t, vals);
        up = anc->parent;
    }
}

#if PGVC_VN >= 180
/* After a parent row changes, every child row of a PERIOD foreign key must
 * still be covered by the parent periods it matches. */
static void fk_recheck_period_children(Session *s, Table *t, Datum *oldparent)
{
    Database *db = s->db;
    for (int i = 0; i < db->ntables; i++) {
        Table *ch = db->tables[i];
        for (int k = 0; k < ch->ncons; k++) {
            Constraint *c = ch->cons[k];
            if (c->contype != 'f' || !c->fk_period || c->fk_reltable != t->oid) continue;
            for (int r = 0; r < ch->nrows; r++) {
                Row *row = ch->rows[r];
                if (!row_visible(s, row)) continue;
                bool anynull = false;
                for (int q = 0; q < c->nkeys; q++)
                    if (row->vals[c->keys[q]-1].isnull) anynull = true;
                if (anynull) continue;
                Datum need = row->vals[c->keys[c->nkeys-1]-1];
                Datum acc = d_null(need.type);
                bool first = true;
                for (int pr = 0; pr < t->nrows; pr++) {
                    Row *prow = t->rows[pr];
                    if (!row_visible(s, prow)) continue;
                    bool same = true;
                    for (int q = 0; q + 1 < c->nkeys && same; q++) {
                        Datum a = row->vals[c->keys[q]-1], b2 = prow->vals[c->fkeys[q]-1];
                        if (a.isnull || b2.isnull || d_cmp(s, a, b2) != 0) same = false;
                    }
                    if (!same) continue;
                    Datum pv = prow->vals[c->fkeys[c->nfkeys-1]-1];
                    if (pv.isnull) continue;
                    if (first) { acc = pv; first = false; continue; }
                    Datum ar[2]; ar[0] = acc; ar[1] = pv;
                    bool f7 = false;
                    Datum u = call_builtin(s, "+", ar, 2, &f7);
                    if (f7 && !u.isnull) acc = u;
                }
                bool covered = false;
                if (!first) {
                    Datum ar[2]; ar[0] = acc; ar[1] = need;
                    bool f8 = false;
                    Datum ct = call_builtin(s, "@>", ar, 2, &f8);
                    if (f8 && !ct.isnull && ct.v.i) covered = true;
                }
                if (covered) continue;
                Buf kb; buf_init(&kb); Buf vb; buf_init(&vb);
                for (int q = 0; q < c->nfkeys; q++) {
                    if (q) { buf_adds(&kb, ", "); buf_adds(&vb, ", "); }
                    buf_adds(&kb, t->cols[c->fkeys[q]-1].name);
                    char *o = out_func(s, oldparent ? oldparent[c->fkeys[q]-1]
                                                    : row->vals[c->keys[q]-1]);
                    buf_adds(&vb, o); free(o);
                }
                err_detail(s, "Key (%s)=(%s) is still referenced from table \"%s\".",
                           kb.data, vb.data, ch->name);
                ereport(s, "23503", "update or delete on table \"%s\" violates foreign key "
                        "constraint \"%s\" on table \"%s\"", t->name, c->name, ch->name);
            }
        }
    }
}
#endif

static void fk_check_delete(Session *s, Table *t, Row *victim)
{
    Database *db = s->db;
    for (int i = 0; i < db->ntables; i++) {
        Table *ch = db->tables[i];
        for (int k = 0; k < ch->ncons; k++) {
            Constraint *c = ch->cons[k];
            if (c->contype != 'f' || c->fk_reltable != t->oid) continue;
            /* The rows of a partitioned referencing table live in its
             * partitions, so look there too; the error still names the parent,
             * which is where the constraint is declared. */
            Table **scan = NULL; int nscan = 0;
            if (ch->relkind == 'p') collect_partitions(s->db, ch, &scan, &nscan);
            else { scan = xmalloc(sizeof(Table *)); scan[0] = ch; nscan = 1; }
            for (int si = 0; si < nscan; si++) {
              Table *sc = scan[si];
              if (sc->relkind == 'p') continue;
              for (int r = 0; r < sc->nrows; r++) {
                Row *row = sc->rows[r];
                if (!row_visible(s, row)) continue;
                bool same = true;
                int ncmp = c->nkeys;
#if PGVC_VN >= 180
                if (c->fk_period) ncmp = c->nkeys - 1;
#endif
                for (int q = 0; q < ncmp && same; q++) {
                    Datum a = row->vals[c->keys[q]-1], b2 = victim->vals[c->fkeys[q]-1];
                    if (a.isnull || b2.isnull || d_cmp(s, a, b2) != 0) same = false;
                }
#if PGVC_VN >= 180
                if (same && c->fk_period) {
                    Datum ov[2];
                    ov[0] = row->vals[c->keys[c->nkeys-1]-1];
                    ov[1] = victim->vals[c->fkeys[c->nfkeys-1]-1];
                    if (ov[0].isnull || ov[1].isnull) same = false;
                    else { bool f6 = false;
                           Datum rr = call_builtin(s, "&&", ov, 2, &f6);
                           if (!f6 || rr.isnull || !rr.v.i) same = false; }
                }
#endif
                if (!same) continue;
                if (c->fk_del == 'c') { row->xmax = xact_xid(s); continue; }
                if (c->fk_del == 'n') {
                    for (int q = 0; q < c->nkeys; q++) row->vals[c->keys[q]-1] = d_null(ch->cols[c->keys[q]-1].type);
                    continue;
                }
                Buf kb; buf_init(&kb); Buf vb; buf_init(&vb);
                for (int q = 0; q < c->nkeys; q++) {
                    if (q) { buf_adds(&kb, ", "); buf_adds(&vb, ", "); }
#if PGVC_VN >= 180
                    if (c->fk_period) {
                        buf_adds(&kb, t->cols[c->fkeys[q]-1].name);
                        char *o = out_func(s, victim->vals[c->fkeys[q]-1]);
                        buf_adds(&vb, o); free(o);
                        continue;
                    }
#endif
                    buf_adds(&kb, ch->cols[c->keys[q]-1].name);
                    char *o = out_func(s, row->vals[c->keys[q]-1]);
                    buf_adds(&vb, o); free(o);
                }
                err_detail(s, "Key (%s)=(%s) is still referenced from table \"%s\".",
                           kb.data, vb.data, ch->name);
                ereport(s, "23503", "update or delete on table \"%s\" violates foreign key constraint \"%s\" on table \"%s\"",
                        t->name, c->name, ch->name);
              }
            }
            free(scan);
        }
    }
}

static Datum eval_default(Session *s, Table *t, Column *col, Datum *row)
{
    if (col->identity) {
        Table *seq = tbl_find(s->db, t->schema, col->identity_seq);
        if (seq) return d_int(col->type, seq_nextval(s, seq));
    }
    if (!col->defexpr) return d_null(col->type);
    Expr *e = parse_expr_string(s, col->defexpr);
    RowSet rs; memset(&rs, 0, sizeof rs);
    rs.ncols = t->ncols;
    rs.cols = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(ColMeta));
    for (int k = 0; k < t->ncols; k++) {
        rs.cols[k].rel = t->name; rs.cols[k].name = t->cols[k].name;
        rs.cols[k].type = t->cols[k].type; rs.cols[k].typmod = t->cols[k].typmod;
        rs.cols[k].hidden = false;
    }
    ExecCtx c2; memset(&c2, 0, sizeof c2);
    c2.s = s; c2.rs = &rs; c2.row = row;
    return coerce_to(s, eval(&c2, e), col->type, col->typmod, 1);
}

/* PostgreSQL stores a default or generation expression parsed and hands it
 * back deparsed: an operator expression gains parentheses and an untyped
 * literal gains its cast. */
/* PostgreSQL prints a SQL/JSON object constructor with spaces round the key
 * separator, whatever the query wrote. */
static char *json_ctor_spacing(const char *t)
{
    if (strncasecmp(t, "JSON_OBJECT", 11) && strncasecmp(t, "JSON_ARRAY", 10) &&
        strncasecmp(t, "JSON_SCALAR", 11) && strncasecmp(t, "JSON_SERIALIZE", 14))
        return NULL;
    Buf b; buf_init(&b);
    bool inq = false;
    for (const char *p = t; *p; p++) {
        if (inq) { buf_addc(&b, *p); if (*p == '\'') inq = false; continue; }
        if (*p == '\'') { inq = true; buf_addc(&b, *p); continue; }
        if (*p == ':' && p[1] != ':' && (p == t || p[-1] != ':')) {
            while (b.len && b.data[b.len-1] == ' ') b.len--;
            buf_adds(&b, " : ");
            while (p[1] == ' ') p++;
            continue;
        }
        buf_addc(&b, *p);
    }
    return buf_steal(&b);
}

char *deparse_default(Session *s, Oid coltype, const char *src)
{
    (void)s;
    if (!src) return NULL;
    { char *j = json_ctor_spacing(src);
      if (j) return j; }
    while (*src == ' ') src++;
    size_t n = strlen(src);
    while (n && src[n-1] == ' ') n--;
    char *t = xstrndup(src, n);
    /* a lone string literal takes the column's type */
    if (n >= 2 && t[0] == '\'' && t[n-1] == '\'') {
        bool lone = true;
        for (size_t i = 1; i + 1 < n; i++)
            if (t[i] == '\'' && t[i+1] != '\'') { lone = false; break; }
        if (lone) {
            char *ty = format_type_str(coltype ? coltype : TEXTOID, -1);
            Buf b; buf_init(&b);
            buf_printf(&b, "%s::%s", t, ty);
            free(ty); free(t);
            return buf_steal(&b);
        }
    }
    /* an operator at the top level means PostgreSQL parenthesises the whole */
    int depth = 0; bool inq = false, op = false;
    for (size_t i = 0; i < n; i++) {
        char ch = t[i];
        if (inq) { if (ch == '\'') inq = false; continue; }
        if (ch == '\'') { inq = true; continue; }
        if (ch == '(') depth++;
        else if (ch == ')') depth--;
        else if (depth == 0 && strchr("+-*/<>=~!@#%^&|?", ch)) op = true;
    }
    if (op && !(t[0] == '(' && t[n-1] == ')')) {
        Buf b; buf_init(&b);
        buf_printf(&b, "(%s)", t);
        free(t);
        return buf_steal(&b);
    }
    return t;
}

static RowSet *returning_rows(Session *s, Table *t, Expr **ret, int nret,
                              Datum **rows, int nrows, Datum **oldrows,
                              Datum **newrows, ExecCtx *base)
{
    RowSet *proj = rs_new();
    RowSet tmp; memset(&tmp, 0, sizeof tmp);
    tmp.ncols = t->ncols;
    tmp.cols = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(ColMeta));
    for (int k = 0; k < t->ncols; k++) {
        tmp.cols[k].rel = t->name; tmp.cols[k].name = t->cols[k].name;
        tmp.cols[k].type = t->cols[k].type; tmp.cols[k].typmod = t->cols[k].typmod;
        tmp.cols[k].hidden = false;
    }
    Expr **tgt = NULL; int ntgt = 0;
    char **labels = NULL;
    for (int i = 0; i < nret; i++) {
        if (ret[i]->tag == X_STAR) {
            for (int k = 0; k < t->ncols; k++) {
                if (t->cols[k].dropped) continue;
                Expr *cr = xmalloc(sizeof *cr); memset(cr, 0, sizeof *cr);
                if (ret[i]->oldnew) { cr->tag = X_OLDNEW; cr->oldnew = ret[i]->oldnew; }
                else cr->tag = X_COLREF;
                cr->colname = t->cols[k].name;
                cr->pos = ret[i]->pos;
                tgt = xrealloc(tgt, (size_t)(ntgt+1)*sizeof(Expr*));
                labels = xrealloc(labels, (size_t)(ntgt+1)*sizeof(char*));
                labels[ntgt] = t->cols[k].name;
                tgt[ntgt++] = cr;
            }
            continue;
        }
        tgt = xrealloc(tgt, (size_t)(ntgt+1)*sizeof(Expr*));
        labels = xrealloc(labels, (size_t)(ntgt+1)*sizeof(char*));
        labels[ntgt] = (char *)expr_label(ret[i]);
        tgt[ntgt++] = ret[i];
    }
    for (int i = 0; i < ntgt; i++) rs_addcol(proj, NULL, labels[i], TEXTOID, -1);
    for (int r = 0; r < nrows; r++) {
        ExecCtx c2; memset(&c2, 0, sizeof c2);
        c2.s = s; c2.rs = &tmp; c2.row = rows[r];
        if (base) { c2.params = base->params; c2.nparams = base->nparams;
                    c2.merge_action = base->merge_action; }
        c2.oldnew_rs = &tmp;
        c2.oldrow = oldrows ? oldrows[r] : NULL;
        c2.newrow = newrows ? newrows[r] : NULL;
        Datum *out = row_new(ntgt);
        for (int i = 0; i < ntgt; i++) out[i] = eval(&c2, tgt[i]);
        rs_addrow(proj, out);
    }
    for (int i = 0; i < ntgt; i++) {
        Oid ty = 0;
        for (int k = 0; k < proj->nrows; k++) ty = common_type(s, ty, proj->rows[k][i].type);
        if (!ty || ty == UNKNOWNOID) {
            if (tgt[i]->tag == X_COLREF || tgt[i]->tag == X_OLDNEW) {
                int ci = tgt[i]->colname ? col_index(t, tgt[i]->colname) : -1;
                if (ci >= 0) { proj->cols[i].type = t->cols[ci].type;
                               proj->cols[i].typmod = t->cols[ci].typmod; continue; }
            }
            ty = TEXTOID;
        }
        proj->cols[i].type = ty;
    }
    return proj;
}

/* ------------------------------------------------------------- triggers */
static int trig_cmp(const void *a, const void *b)
{
    return strcmp((*(Trigger *const *)a)->name, (*(Trigger *const *)b)->name);
}

/* The triggers of one shape on a relation, in the order PostgreSQL fires
 * them: by name. */
static Trigger **triggers_for(Database *db, Oid rel, int event, int timing,
                              bool forrow, int *n)
{
    Trigger **out = NULL; int k = 0;
    for (int i = 0; i < db->ntriggers; i++) {
        Trigger *t = db->triggers[i];
        if (t->table != rel || !(t->events & event) || t->timing != timing ||
            t->forrow != forrow) continue;
        out = xrealloc(out, (size_t)(k + 1) * sizeof(Trigger *));
        out[k++] = t;
    }
    if (k > 1) qsort(out, (size_t)k, sizeof(Trigger *), trig_cmp);
    *n = k;
    return out;
}

static bool table_has_triggers(Database *db, Oid rel)
{
    for (int i = 0; i < db->ntriggers; i++) if (db->triggers[i]->table == rel) return true;
    return false;
}

static const char *op_name(int event)
{
    return event == 1 ? "INSERT" : event == 2 ? "UPDATE" : event == 4 ? "DELETE" : "TRUNCATE";
}

/* Run one row-level trigger.  Returns the row to carry on with, or NULL when
 * a BEFORE trigger asks for the row to be skipped. */
static Datum *fire_row_trigger(Session *s, Trigger *tg, Table *rel, int event,
                               Datum *oldrow, Datum *newrow)
{
    SqlRoutine *p = routine_find(s->db, NULL, tg->funcname, 0);
    if (!p) return newrow;
    PlTrigCtx tc;
    memset(&tc, 0, sizeof tc);
    tc.name = tg->name;
    tc.when = tg->timing == 0 ? "BEFORE" : tg->timing == 1 ? "AFTER" : "INSTEAD OF";
    tc.level = "ROW";
    tc.op = op_name(event);
    tc.table = rel;
    tc.oldrow = oldrow;
    tc.newrow = newrow;
    if (p->lang && !strcasecmp(p->lang, "plpgsql")) {
        Datum r = plpgsql_run(s, p, NULL, 0, &tc, NULL, NULL);
        if (tg->timing == 0) {
            if (r.isnull && r.type != RECORDOID) return NULL;
            return tc.newrow;
        }
    }
    return newrow;
}

static Table *mk_table(Database *db, const char *schema, const char *name, char kind);
static void add_col(Table *t, const char *name, Oid type, int32 typmod);

/* A transition table is an ordinary relation for the life of the statement. */
static Table *make_transition(Session *s, Table *base, const char *name,
                              Datum **rows, int n)
{
    Table *tt = mk_table(s->db, base->schema, name, 'r');
    for (int i = 0; i < base->ncols; i++)
        add_col(tt, base->cols[i].name, base->cols[i].type, base->cols[i].typmod);
    for (int i = 0; i < n; i++) {
        Row *r = xmalloc(sizeof *r);
        memset(r, 0, sizeof *r);
        r->xmin = 1; r->xmax = 0; r->n = tt->ncols;
        r->vals = xmalloc((size_t)(tt->ncols ? tt->ncols : 1) * sizeof(Datum));
        for (int k = 0; k < tt->ncols; k++) r->vals[k] = rows[i][k];
        tt->rows = xrealloc(tt->rows, (size_t)(tt->nrows + 1) * sizeof(Row *));
        tt->rows[tt->nrows++] = r;
    }
    tbl_add(s->db, tt);
    return tt;
}

static void fire_stmt_triggers(Session *s, Table *t, int event, int timing,
                               Datum **oldrows, int nold, Datum **newrows, int nnew)
{
    if (!table_has_triggers(s->db, t->oid)) return;
    int n = 0;
    Trigger **tg = triggers_for(s->db, t->oid, event, timing, false, &n);
    for (int i = 0; i < n; i++) {
        Table *told = NULL, *tnew = NULL;
        if (tg[i]->oldtable) told = make_transition(s, t, tg[i]->oldtable, oldrows, nold);
        if (tg[i]->newtable) tnew = make_transition(s, t, tg[i]->newtable, newrows, nnew);
        SqlRoutine *p = routine_find(s->db, NULL, tg[i]->funcname, 0);
        if (p && p->lang && !strcasecmp(p->lang, "plpgsql")) {
            PlTrigCtx tc;
            memset(&tc, 0, sizeof tc);
            tc.name = tg[i]->name;
            tc.when = timing == 0 ? "BEFORE" : "AFTER";
            tc.level = "STATEMENT";
            tc.op = op_name(event);
            tc.table = t;
            plpgsql_run(s, p, NULL, 0, &tc, NULL, NULL);
        }
        if (told) tbl_drop(s->db, told);
        if (tnew) tbl_drop(s->db, tnew);
    }
    free(tg);
}

/* ------------------------------------------------------ auto-updatable views
 * A view over a single plain relation whose select list is nothing but column
 * references is automatically updatable; PostgreSQL rewrites INSERT, UPDATE
 * and DELETE on it into the same statement against the base relation.
 */
typedef struct ViewMap {
    Table *base;
    char **vname; char **bname; int n;
    Expr *where;              /* the view's own qualification, if any */
    const char *why;          /* why it is not automatically updatable */
} ViewMap;

static bool view_updatable(Session *s, Table *v, ViewMap *m)
{
    if (v->relkind != 'v' || !v->viewdef) return false;
    Node *n = parse_sql(s, v->viewdef, NULL);
    if (!n || n->tag != N_SELECT || !n->select) return false;
    SelectStmt *sel = n->select;
    memset(m, 0, sizeof *m);
    /* PostgreSQL names the construct that made the view read-only. */
    const char *why = NULL;
    if (sel->setop)          why = "UNION, INTERSECT or EXCEPT";
    else if (sel->distinct)  why = "DISTINCT";
    else if (sel->ngroupby)  why = "GROUP BY";
    else if (sel->having)    why = "HAVING";
    else if (sel->nwindows)  why = "window functions";
    else if (sel->limit || sel->offset) why = "LIMIT or OFFSET";
    else if (sel->nctes)     why = "WITH";
    else if (sel->nvalrows)  why = "VALUES";
    else if (sel->nfrom != 1) why = "more than one relation";
    if (why) { m->why = why; return false; }
    RangeTbl *rt = sel->from[0];
    if (!rt || rt->kind != 0 || rt->funcexpr || rt->subquery || rt->left || rt->right)
        return false;
    Table *b = tbl_find_sp(s, rt->schema, rt->relname);
    if (!b) return false;
    m->base = b;
    m->where = sel->where;
    m->vname = xmalloc((size_t)(v->ncols ? v->ncols : 1) * sizeof(char *));
    m->bname = xmalloc((size_t)(v->ncols ? v->ncols : 1) * sizeof(char *));
    if (sel->ntarget == 1 && sel->target[0]->tag == X_STAR) {
        for (int i = 0; i < v->ncols && i < b->ncols; i++) {
            m->vname[m->n] = v->cols[i].name;
            m->bname[m->n] = b->cols[i].name;
            m->n++;
        }
        return m->n == v->ncols;
    }
    if (sel->ntarget != v->ncols) return false;
    for (int i = 0; i < sel->ntarget; i++) {
        Expr *e = sel->target[i];
        if (expr_has_agg(e)) { m->why = "aggregate functions"; return false; }
        if (e->tag != X_COLREF || !e->colname) {
            m->why = "expressions in the select list";
            return false;
        }
        m->vname[m->n] = v->cols[i].name;
        m->bname[m->n] = e->colname;
        m->n++;
    }
    return true;
}

/* The refusal PostgreSQL gives for a view it cannot write through: the verb,
 * the reason, and the trigger that would make it possible.  `verb` is "insert
 * into", "update" or "delete from"; `via_merge` says the statement is MERGE,
 * whose hint mentions MERGE rather than the plain statement. */
static void view_not_updatable(Session *s, Table *v, ViewMap *m,
                               const char *verb, const char *trig, bool via_merge)
{
    if (m->why)
        err_detail(s, "Views containing %s are not automatically updatable.", m->why);
    if (via_merge)
        err_hint(s, "To enable %s the view using MERGE, provide an INSTEAD OF %s trigger.",
                 !strcmp(verb, "delete from") ? "deleting from" :
                 !strcmp(verb, "update") ? "updating" : "inserting into", trig);
    else
        err_hint(s, "To enable %s the view, provide an INSTEAD OF %s trigger or an "
                 "unconditional ON %s DO INSTEAD rule.",
                 !strcmp(verb, "delete from") ? "deleting from" :
                 !strcmp(verb, "update") ? "updating" : "inserting into", trig, trig);
    ereport(s, "55000", "cannot %s view \"%s\"", verb, v->name);
}

/* Translate a column reference written against the view into the base name. */
static void view_map_expr(Expr *e, ViewMap *m)
{
    if (!e) return;
    if (e->tag == X_COLREF && e->colname)
        for (int i = 0; i < m->n; i++)
            if (!strcmp(e->colname, m->vname[i])) {
                if (strcmp(m->vname[i], m->bname[i])) e->colname = m->bname[i];
                break;
            }
    for (int i = 0; i < e->nargs; i++) view_map_expr(e->args[i], m);
    view_map_expr(e->case_arg, m); view_map_expr(e->case_else, m);
    for (int i = 0; i < e->nwhen; i++) {
        view_map_expr(e->when_cond[i], m); view_map_expr(e->when_res[i], m);
    }
    view_map_expr(e->filter, m); view_map_expr(e->testexpr, m);
    view_map_expr(e->like_escape, m);
    for (int i = 0; i < e->agg_norder; i++) view_map_expr(e->agg_order[i].e, m);
}

static char *view_map_col(ViewMap *m, const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < m->n; i++)
        if (!strcmp(name, m->vname[i])) return m->bname[i];
    return (char *)name;
}

static QueryResult *exec_insert(Session *s, Node *n, ExecCtx *base)
{
    InsertStmt *st = n->x;
    Table *t = resolve_table_at(s, st->schema, st->name, true, st->pos);

    for (int guard = 0; t->relkind == 'v' && guard < 10; guard++) {
        ViewMap vm;
        if (!view_updatable(s, t, &vm))
            ereport(s, "55000", "cannot insert into view \"%s\"", t->name);
        InsertStmt *ns = xmalloc(sizeof *ns); *ns = *st;
        ns->schema = vm.base->schema; ns->name = vm.base->name;
        if (st->ncols) {
            ns->cols = xmalloc((size_t)st->ncols * sizeof(char *));
            for (int i = 0; i < st->ncols; i++) ns->cols[i] = view_map_col(&vm, st->cols[i]);
        }
        for (int i = 0; i < st->nconf_cols; i++)
            st->conf_cols[i] = view_map_col(&vm, st->conf_cols[i]);
        for (int i = 0; i < st->nset_cols; i++)
            st->set_cols[i] = view_map_col(&vm, st->set_cols[i]);
        for (int i = 0; i < st->nret; i++) view_map_expr(st->returning[i], &vm);
        for (int i = 0; i < st->nset_cols; i++) view_map_expr(st->set_vals[i], &vm);
        view_map_expr(st->conf_where, &vm); view_map_expr(st->set_where, &vm);
        st = ns; t = vm.base;
    }
    ExecCtx c; memset(&c, 0, sizeof c);
    c.s = s;
    if (base) { c.params = base->params; c.nparams = base->nparams; }
    if (st->nctes) {
        SelectStmt fake; memset(&fake, 0, sizeof fake);
        fake.ctes = st->ctes; fake.nctes = st->nctes;
        run_ctes(&c, &fake, &c);
    }
    int *map = NULL; int nmap = 0;
    if (st->ncols) {
        map = xmalloc((size_t)st->ncols * sizeof(int));
        for (int i = 0; i < st->ncols; i++) {
            int ci = col_index(t, st->cols[i]);
            if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                st->cols[i], t->name);
            map[i] = ci;
        }
        nmap = st->ncols;
    } else {
        /* PostgreSQL's implicit column list includes stored generated columns,
         * so giving one a value is an error rather than a count mismatch. */
        map = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(int));
        for (int i = 0; i < t->ncols; i++)
            /* a stored generated column is still in the implicit list, so a
             * value for it is rejected rather than counted as an extra */
            if (!t->cols[i].dropped && t->cols[i].generated != 'v') map[nmap++] = i;
    }
    RowSet *src = NULL;
    if (!st->default_values) {
        ExecCtx sc = c;
        src = exec_select(&sc, st->query->select);
        if (src->ncols != nmap) {
            if (src->ncols > nmap)
                ereport(s, "42601", "INSERT has more expressions than target columns");
            ereport(s, "42601", "INSERT has more target columns than expressions");
        }
    }
    int nrows = st->default_values ? 1 : src->nrows;
    Datum **inserted = xmalloc((size_t)(nrows ? nrows : 1) * sizeof(Datum *));
    /* the row a conflicting INSERT replaced, so RETURNING old.* can show it */
    Datum **confolds = xmalloc((size_t)(nrows ? nrows : 1) * sizeof(Datum *));
    int ninserted = 0;
    for (int r = 0; r < nrows; r++) {
        Datum *vals = row_new(t->ncols);
        bool *given = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(bool));
        memset(given, 0, (size_t)(t->ncols ? t->ncols : 1) * sizeof(bool));
        for (int i = 0; i < nmap && !st->default_values; i++) {
            Datum v = src->rows[r][i];
            /* a DEFAULT marker in a VALUES row */
            if (st->query->select->nvalrows && st->query->select->values[r][i]->alias &&
                !strcmp(st->query->select->values[r][i]->alias, "__default__")) continue;
            Column *col = &t->cols[map[i]];
            /* an untyped literal is converted by the type's input function,
             * which reports the literal's position */
            if (st->query->select->nvalrows && st->query->select->values[r][i] &&
                st->query->select->values[r][i]->tag == X_CONST &&
                !st->query->select->values[r][i]->isnull_const &&
                st->query->select->values[r][i]->val.type == UNKNOWNOID) {
                s->err.position = st->query->select->values[r][i]->pos;
                v = coerce_to(s, v, col->type, -1, 1);
                s->err.position = 0;
            }
            if (col->identity == 'd' && st->overriding == 2) continue;  /* OVERRIDING USER VALUE */
#if PGVC_VN >= 130
            /* PostgreSQL 13 made OVERRIDING USER VALUE legal on a GENERATED
             * ALWAYS identity column: the supplied value is simply dropped. */
            if (col->identity == 'a' && st->overriding == 2) continue;
#endif
            if (col->identity == 'a' && st->overriding != 1) {
                if (PGVC_VN >= 100) {
                    err_hint(s, "Use OVERRIDING SYSTEM VALUE to override.");
                    err_detail(s, "Column \"%s\" is an identity column defined as GENERATED ALWAYS.",
                               col->name);
#if PGVC_VN >= 140
                    ereport(s, "428C9", "cannot insert a non-DEFAULT value into column \"%s\"",
                            col->name);
#else
                    ereport(s, "428C9", "cannot insert into column \"%s\"", col->name);
#endif
                }
            }
            if (col->generated) {
                err_detail(s, "Column \"%s\" is a generated column.", col->name);
#if PGVC_VN >= 140
                ereport(s, "428C9", "cannot insert a non-DEFAULT value into column \"%s\"", col->name);
#else
                ereport(s, "428C9", "cannot insert into column \"%s\"", col->name);
#endif
            }
            vals[map[i]] = coerce_to(s, v, col->type, col->typmod, 1);
            s->err.position = 0;
            given[map[i]] = true;
        }
        for (int i = 0; i < t->ncols; i++)
            if (!given[i] && !t->cols[i].generated) vals[i] = eval_default(s, t, &t->cols[i], vals);
        apply_generated(s, t, vals);
        /* ON CONFLICT - the conflict is looked for in the partition the row
         * would land in, which is where its unique index lives. */
        Table *ct = t->partstrat ? route_partition(s, t, vals) : t;
        if (st->onconflict) {
            bool conflict = false;
            Row *victim = NULL;
            for (int ci = 0; ci < ct->ncons && !conflict; ci++) {
                Constraint *cc = ct->cons[ci];
                if (cc->contype != 'p' && cc->contype != 'u') continue;
                for (int rr = 0; rr < ct->nrows; rr++) {
                    Row *row = ct->rows[rr];
                    if (!row_visible(s, row)) continue;
                    bool same = true;
                    for (int k = 0; k < cc->nkeys && same; k++) {
                        Datum a = vals[cc->keys[k]-1], b2 = row->vals[cc->keys[k]-1];
                        if (a.isnull || b2.isnull || d_cmp(s, a, b2) != 0) same = false;
                    }
                    if (same) { conflict = true; victim = row; break; }
                }
            }
            if (conflict) {
                if (st->onconflict == 1) { free(given); continue; }
                Datum *prev = xmalloc((size_t)(t->ncols?t->ncols:1) * sizeof(Datum));
                memcpy(prev, victim->vals, (size_t)t->ncols * sizeof(Datum));
                confolds[ninserted] = prev;
                for (int k = 0; k < st->nset_cols; k++) {
                    int ci = col_index(ct, st->set_cols[k]);
                    if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                        st->set_cols[k], ct->name);
                    RowSet tmp; memset(&tmp, 0, sizeof tmp);
                    tmp.ncols = ct->ncols;
                    tmp.cols = xmalloc((size_t)(ct->ncols?ct->ncols:1)*sizeof(ColMeta));
                    for (int q = 0; q < ct->ncols; q++) {
                        tmp.cols[q].rel = ct->name; tmp.cols[q].name = ct->cols[q].name;
                        tmp.cols[q].type = ct->cols[q].type; tmp.cols[q].typmod = ct->cols[q].typmod;
                        tmp.cols[q].hidden = false;
                    }
                    ExecCtx uc = c; uc.rs = &tmp; uc.row = victim->vals;
                    victim->vals[ci] = coerce_to(s, eval(&uc, st->set_vals[k]),
                                                 ct->cols[ci].type, ct->cols[ci].typmod, 1);
                }
                inserted[ninserted++] = victim->vals;
                free(given);
                continue;
            }
        }
        /* PostgreSQL routes the row to its partition before the BEFORE row
         * triggers run, and refuses a trigger that would move it elsewhere. */
        Table *pre = (t->relkind == 'p') ? route_partition(s, t, vals) : NULL;
        if (table_has_triggers(s->db, t->oid)) {
            int nt = 0;
            Trigger **tg = triggers_for(s->db, t->oid, 1, 0, true, &nt);
            bool skip = false;
            for (int i = 0; i < nt && !skip; i++) {
                Datum *r = fire_row_trigger(s, tg[i], t, 1, NULL, vals);
                if (!r) skip = true;
                else {
                    vals = r;
                    if (pre && pre != t && !row_still_in(s, t, pre, vals)) {
                        err_detail(s, "Before executing trigger \"%s\", the row was to be "
                                      "in partition \"%s.%s\".",
                                   tg[i]->name, pre->schema, pre->name);
                        ereport(s, "23514", "moving row to another partition during a "
                                            "BEFORE FOR EACH ROW trigger is not supported");
                    }
                }
            }
            free(tg);
            if (skip) { free(given); continue; }
        }
        Table *landed = NULL;
        insert_row_into(s, t, vals, &landed);
        confolds[ninserted] = NULL;
        inserted[ninserted++] = landed->rows[landed->nrows-1]->vals;
        if (table_has_triggers(s->db, t->oid)) {
            int nt = 0;
            Trigger **tg = triggers_for(s->db, t->oid, 1, 1, true, &nt);
            for (int i = 0; i < nt; i++)
                fire_row_trigger(s, tg[i], landed, 1, NULL, landed->rows[landed->nrows-1]->vals);
            free(tg);
        }
        free(given);
    }
    fire_stmt_triggers(s, t, 1, 1, NULL, 0, inserted, ninserted);
    QueryResult *q;
    if (st->nret) {
        RowSet *proj = returning_rows(s, t, st->returning, st->nret, inserted, ninserted,
                                      confolds, inserted, &c);
        q = rs_to_qr(s, proj);
    } else q = qr_new();
    snprintf(q->tag, sizeof q->tag, "INSERT 0 %d", ninserted);
    return q;
}

#if PGVC_VN >= 150
/* A row filter may only reference columns of the replica identity, otherwise
 * the row cannot be replicated on UPDATE or DELETE. */
void check_pub_rowfilter_ri(Session *s, Table *t, const char *verb)
{
    Database *db = s->db;
    for (int i = 0; i < db->npubs; i++) {
        Publication *p = db->pubs[i];
        for (int k = 0; k < p->ntables; k++) {
            if (p->tables[k] != t->oid || !p->where || !p->where[k]) continue;
            Expr *e = parse_expression_text(s, p->where[k]);
            if (!e) continue;
            Expr **cols = NULL; int ncols = 0;
            collect_bare_cols(e, &cols, &ncols);
            for (int q = 0; q < ncols; q++) {
                int ci = col_index(t, cols[q]->colname);
                if (ci < 0) continue;
                bool inkey = false;
                for (int w = 0; w < t->ncons; w++) {
                    Constraint *cc = t->cons[w];
                    if (cc->contype != 'p') continue;
                    for (int z = 0; z < cc->nkeys; z++)
                        if (cc->keys[z] == ci + 1) inkey = true;
                }
                if (!inkey) {
                    free(cols);
                    err_detail(s, "Column used in the publication WHERE expression is not "
                                  "part of the replica identity.");
                    ereport(s, "55000", "cannot %s table \"%s\"", verb, t->name);
                }
            }
            free(cols);
        }
    }
}
#endif

static QueryResult *exec_update(Session *s, Node *n, ExecCtx *base)
{
    UpdateStmt *st = n->x;
    Table *t = resolve_table(s, st->schema, st->name, true);
    for (int guard = 0; t->relkind == 'v' && guard < 10; guard++) {
        ViewMap vm;
        if (!view_updatable(s, t, &vm))
            ereport(s, "55000", "cannot update view \"%s\"", t->name);
        UpdateStmt *ns = xmalloc(sizeof *ns); *ns = *st;
        ns->alias = st->alias ? st->alias : t->name;
        ns->schema = vm.base->schema; ns->name = vm.base->name;
        if (st->ncols) {
            ns->cols = xmalloc((size_t)st->ncols * sizeof(char *));
            for (int i = 0; i < st->ncols; i++) ns->cols[i] = view_map_col(&vm, st->cols[i]);
        }
        view_map_expr(st->where, &vm);
        for (int i = 0; i < st->ncols; i++) view_map_expr(st->vals[i], &vm);
        for (int i = 0; i < st->nret; i++) view_map_expr(st->returning[i], &vm);
        st = ns; t = vm.base;
    }
#if PGVC_VN >= 150
    check_pub_rowfilter_ri(s, t, "update");
#endif
    ExecCtx c; memset(&c, 0, sizeof c);
    c.s = s;
    if (base) { c.params = base->params; c.nparams = base->nparams; }
    if (st->nctes) {
        SelectStmt fake; memset(&fake, 0, sizeof fake);
        fake.ctes = st->ctes; fake.nctes = st->nctes;
        run_ctes(&c, &fake, &c);
    }
    /* PostgreSQL rejects an update of a generated or GENERATED ALWAYS identity
     * column when the statement is planned, not per row. */
    for (int k = 0; k < st->ncols; k++) {
        int ci = col_index(t, st->cols[k]);
        if (ci < 0) continue;
        bool isdflt = st->vals[k]->alias && !strcmp(st->vals[k]->alias, "__default__");
        if (isdflt) continue;
        if (t->cols[ci].generated) {
            err_detail(s, "Column \"%s\" is a generated column.", t->cols[ci].name);
            ereport(s, "428C9", "column \"%s\" can only be updated to DEFAULT", t->cols[ci].name);
        }
        if (t->cols[ci].identity == 'a') {
            err_detail(s, "Column \"%s\" is an identity column defined as GENERATED ALWAYS.",
                       t->cols[ci].name);
            ereport(s, "428C9", "column \"%s\" can only be updated to DEFAULT", t->cols[ci].name);
        }
        /* an untyped literal is converted when the statement is analysed */
        if (st->vals[k]->tag == X_CONST && !st->vals[k]->isnull_const &&
            st->vals[k]->val.type == UNKNOWNOID) {
            /* only the base-type conversion carries a position; the length or
             * precision check happens later and does not */
            s->err.position = st->vals[k]->pos;
            coerce_to(s, st->vals[k]->val, t->cols[ci].type, -1, 1);
            s->err.position = 0;
        }
    }
    RangeTbl rt; memset(&rt, 0, sizeof rt);
    rt.kind = 0; rt.schema = st->schema; rt.relname = st->name;
    rt.alias = st->alias; rt.only = st->only;
    RowSet *scan = exec_from(&c, &rt);
    RowSet *joined = scan;
    for (int i = 0; i < st->nfrom; i++) joined = cross_join(joined, exec_from(&c, st->from[i]));

    Table **parts = NULL; int nparts = 0;
    if (!st->only) collect_partitions(s->db, t, &parts, &nparts);
    else { parts = xmalloc(sizeof(Table*)); parts[0] = t; nparts = 1; }

    Datum **updated = NULL; Datum **olds = NULL; int nup = 0;
    int ri = 0;
    for (int p = 0; p < nparts; p++) {
        Table *pt = parts[p];
        if (pt->relkind == 'p') continue;
        lock_table_write(s, pt);
        /* An UPDATE sees the table as it was when the command started: the row
         * versions this command writes are appended here, and PostgreSQL never
         * updates a row its own command produced.  Capturing the count first is
         * what keeps `UPDATE t SET c = c` from re-finding the version it just
         * wrote and looping until memory runs out. */
        int nrows_at_start = pt->nrows;
        for (int r = 0; r < nrows_at_start; r++) {
            Row *row = pt->rows[r];
            if (!row_visible(s, row)) continue;
            /* find the matching scan rows (there may be several with the FROM join) */
            for (int jr = 0; jr < joined->nrows; jr++) {
                Datum *jrow = joined->rows[jr];
                bool same = true;
                for (int k = 0, q = 0; k < pt->ncols && same; k++) {
                    if (pt->cols[k].dropped) continue;
                    Datum a = jrow[q], b2 = row->vals[k];
                    if (a.isnull != b2.isnull || (!a.isnull && d_cmp(s, a, b2) != 0)) same = false;
                    q++;
                }
                if (!same) continue;
                ExecCtx uc = c; uc.rs = joined; uc.row = jrow;
                if (st->where && !datum_true(to_bool(s, eval(&uc, st->where)))) continue;
                row_lock_wait(s, row);
                if (!row_visible(s, row)) {
                    /* PostgreSQL re-evaluates against the row the winner wrote;
                     * follow the update chain to the live version. */
                    Row *succ = row->next_ver;
                    while (succ && !row_visible(s, succ)) succ = succ->next_ver;
                    if (!succ) break;
                    row = succ;
                }
                Datum *oldvals = xmalloc((size_t)(pt->ncols?pt->ncols:1) * sizeof(Datum));
                memcpy(oldvals, row->vals, (size_t)pt->ncols * sizeof(Datum));
                Datum *newvals = xmalloc((size_t)(pt->ncols?pt->ncols:1) * sizeof(Datum));
                memcpy(newvals, row->vals, (size_t)pt->ncols * sizeof(Datum));
                for (int k = 0; k < st->ncols; k++) {
                    int ci = col_index(pt, st->cols[k]);
                    if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                        st->cols[k], pt->name);
                    /* the generated / identity checks happen once per statement */
                    if (pt->cols[ci].identity == 'a' &&
                        !(st->vals[k]->alias && !strcmp(st->vals[k]->alias, "__default__"))) {
                        err_detail(s, "Column \"%s\" is an identity column defined as GENERATED ALWAYS.",
                                   pt->cols[ci].name);
                        ereport(s, "428C9", "column \"%s\" can only be updated to DEFAULT",
                                pt->cols[ci].name);
                    }
                    Expr *ve = st->vals[k];
                    if (ve->tag == X_SUBSCRIPT && ve->is_kind == 2) {
                        Datum cur = newvals[ci];
                        /* the target may be a chain: j['a']['b'] */
                        Expr *chain[16]; int nch = 0;
                        Expr *tg = ve->args[0];
                        while (tg->tag == X_SUBSCRIPT && nch < 16) {
                            chain[nch++] = tg->args[1];
                            tg = tg->args[0];
                        }
                        Datum idx = eval(&uc, chain[nch-1]);
                        Datum nv = eval(&uc, ve->args[1]);
                        if (pt->cols[ci].type == JSONBOID) {
                            ArrayVal *pa = mk_array(TEXTOID, NULL, 0);
                            for (int q = nch - 1; q >= 0; q--) {
                                Datum pv = eval(&uc, chain[q]);
                                char *ks = pv.isnull ? xstrdup("") : out_func(s, pv);
                                Datum e2 = d_text(TEXTOID, ks);
                                pa->elems = xrealloc(pa->elems,
                                                     (size_t)(pa->nelem + 1) * sizeof(Datum));
                                pa->elems[pa->nelem++] = e2;
                                pa->ndim = 1; pa->dims[0] = pa->nelem; pa->lbound[0] = 1;
                                free(ks);
                            }
                            Datum ar[3];
                            ar[0] = cur;
                            Datum pd; memset(&pd, 0, sizeof pd);
                            pd.type = TEXTARRAYOID; pd.v.arr = pa;
                            ar[1] = pd;
                            s->err.position = ve->args[1]->pos;
                            ar[2] = nv.isnull ? nv : coerce_to(s, nv, JSONBOID, -1, 2);
                            s->err.position = 0;
                            bool f = false;
                            newvals[ci] = call_builtin(s, "__jsonb_subscript_set", ar, 3, &f);
                        } else if (type_is_array(pt->cols[ci].type)) {
                            ArrayVal *a = cur.isnull ? mk_array(elem_type_of(pt->cols[ci].type), NULL, 0)
                                                     : cur.v.arr;
                            int want = (int)idx.v.i;
                            if (want > a->nelem) {
                                a->elems = xrealloc(a->elems, (size_t)want * sizeof(Datum));
                                for (int z = a->nelem; z < want; z++) a->elems[z] = d_null(a->elemtype);
                                a->nelem = want; a->ndim = 1; a->dims[0] = want; a->lbound[0] = 1;
                            }
                            if (want >= 1) a->elems[want-1] = coerce_to(s, nv, a->elemtype, -1, 1);
                            Datum d; memset(&d, 0, sizeof d);
                            d.type = pt->cols[ci].type; d.v.arr = a;
                            newvals[ci] = d;
                        }
                        continue;
                    }
                    if (ve->tag == X_CONST && ve->alias && !strcmp(ve->alias, "__default__")) {
                        newvals[ci] = eval_default(s, pt, &pt->cols[ci], newvals);
                        continue;
                    }
                    { Datum nv0 = eval(&uc, ve);
                      if (ve->tag == X_CONST && !ve->isnull_const &&
                          ve->val.type == UNKNOWNOID) {
                          s->err.position = ve->pos;
                          nv0 = coerce_to(s, nv0, pt->cols[ci].type, -1, 1);
                          s->err.position = 0;
                      }
                      newvals[ci] = coerce_to(s, nv0, pt->cols[ci].type, pt->cols[ci].typmod, 1); }
                }
                apply_generated(s, pt, newvals);
                if (pt->parent) {
                    /* PostgreSQL 11 moves a row that no longer fits its
                     * partition; before that the update simply fails. */
                    bool moved = false;
                    if (PGVC_VN >= 110 && pt != t) {
                        Datum *rv = row_new(t->ncols);
                        for (int q = 0; q < t->ncols; q++) {
                            int src = col_index(pt, t->cols[q].name);
                            rv[q] = (src >= 0 && src < pt->ncols) ? newvals[src]
                                                                  : d_null(t->cols[q].type);
                        }
                        Table *dest = route_partition(s, t, rv);
                        if (dest != pt) {
                            row->xmax = xact_xid(s);
                            insert_row_into(s, t, rv, NULL);
                            updated = xrealloc(updated, (size_t)(nup+1)*sizeof(Datum*));
                            olds = xrealloc(olds, (size_t)(nup+1)*sizeof(Datum*));
                            updated[nup] = newvals; olds[nup] = oldvals; nup++;
                            moved = true;
                        }
                    }
                    if (moved) { xact_touch(s, pt); break; }
                    check_partition_bound(s, pt, newvals);
                }
                check_constraints(s, pt, newvals, row);
                /* MVCC: end the old version and append a new one */
                row->xmax = xact_xid(s);
                Row *nrow = xmalloc(sizeof *nrow);
                memset(nrow, 0, sizeof *nrow);
                nrow->xmin = xact_xid(s);
                nrow->n = pt->ncols;
                nrow->vals = newvals;
                row->next_ver = nrow;
                if (pt->nrows == pt->caprows) {
                    pt->caprows = pt->caprows ? pt->caprows * 2 : 16;
                    pt->rows = xrealloc(pt->rows, (size_t)pt->caprows * sizeof(Row *));
                }
                pt->rows[pt->nrows++] = nrow;
#if PGVC_VN >= 180
                fk_recheck_period_children(s, pt, oldvals);
#endif
                updated = xrealloc(updated, (size_t)(nup+1)*sizeof(Datum*));
                olds = xrealloc(olds, (size_t)(nup+1)*sizeof(Datum*));
                updated[nup] = newvals; olds[nup] = oldvals; nup++;
                break;
            }
            ri++;
        }
        xact_touch(s, pt);
    }
    fire_stmt_triggers(s, t, 2, 1, olds, nup, updated, nup);
    QueryResult *q;
    if (st->nret) {
        RowSet *proj = returning_rows(s, t, st->returning, st->nret, updated, nup,
                                      olds, updated, &c);
        q = rs_to_qr(s, proj);
    } else q = qr_new();
    snprintf(q->tag, sizeof q->tag, "UPDATE %d", nup);
    return q;
}

static QueryResult *exec_delete(Session *s, Node *n, ExecCtx *base)
{
    DeleteStmt *st = n->x;
    Table *t = resolve_table(s, st->schema, st->name, true);
    for (int guard = 0; t->relkind == 'v' && guard < 10; guard++) {
        ViewMap vm;
        if (!view_updatable(s, t, &vm))
            ereport(s, "55000", "cannot delete from view \"%s\"", t->name);
        DeleteStmt *ns = xmalloc(sizeof *ns); *ns = *st;
        ns->alias = st->alias ? st->alias : t->name;
        ns->schema = vm.base->schema; ns->name = vm.base->name;
        view_map_expr(st->where, &vm);
        for (int i = 0; i < st->nret; i++) view_map_expr(st->returning[i], &vm);
        st = ns; t = vm.base;
    }
#if PGVC_VN >= 150
    check_pub_rowfilter_ri(s, t, "delete from");
#endif
    ExecCtx c; memset(&c, 0, sizeof c);
    c.s = s;
    if (base) { c.params = base->params; c.nparams = base->nparams; }
    if (st->nctes) {
        SelectStmt fake; memset(&fake, 0, sizeof fake);
        fake.ctes = st->ctes; fake.nctes = st->nctes;
        run_ctes(&c, &fake, &c);
    }
    Table **parts = NULL; int nparts = 0;
    if (!st->only) collect_partitions(s->db, t, &parts, &nparts);
    else { parts = xmalloc(sizeof(Table*)); parts[0] = t; nparts = 1; }
    Datum **deleted = NULL; int ndel = 0;
    for (int p = 0; p < nparts; p++) {
        Table *pt = parts[p];
        if (pt->relkind == 'p') continue;
        lock_table_write(s, pt);
        RangeTbl rt; memset(&rt, 0, sizeof rt);
        rt.kind = 0; rt.relname = pt->name; rt.schema = pt->schema;
        rt.alias = st->alias ? st->alias : t->name; rt.only = true;
        RowSet *scan = exec_from(&c, &rt);
        RowSet *joined = scan;
        for (int i = 0; i < st->nusing; i++) joined = cross_join(joined, exec_from(&c, st->using_[i]));
        for (int r = 0; r < pt->nrows; r++) {
            Row *row = pt->rows[r];
            if (!row_visible(s, row)) continue;
            bool matched = false;
            for (int jr = 0; jr < joined->nrows && !matched; jr++) {
                Datum *jrow = joined->rows[jr];
                bool same = true;
                for (int k = 0, q = 0; k < pt->ncols && same; k++) {
                    if (pt->cols[k].dropped) continue;
                    Datum a = jrow[q], b2 = row->vals[k];
                    if (a.isnull != b2.isnull || (!a.isnull && d_cmp(s, a, b2) != 0)) same = false;
                    q++;
                }
                if (!same) continue;
                ExecCtx uc = c; uc.rs = joined; uc.row = jrow;
                if (st->where && !datum_true(to_bool(s, eval(&uc, st->where)))) continue;
                matched = true;
            }
            if (!matched) continue;
            row_lock_wait(s, row);
            if (!row_visible(s, row)) continue;
            fk_check_delete(s, pt, row);
            row->xmax = xact_xid(s);
            deleted = xrealloc(deleted, (size_t)(ndel+1)*sizeof(Datum*));
            deleted[ndel++] = row->vals;
        }
        xact_touch(s, pt);
    }
    fire_stmt_triggers(s, t, 4, 1, deleted, ndel, NULL, 0);
    QueryResult *q;
    if (st->nret) {
        RowSet *proj = returning_rows(s, t, st->returning, st->nret, deleted, ndel,
                                      deleted, NULL, &c);
        q = rs_to_qr(s, proj);
    } else q = qr_new();
    snprintf(q->tag, sizeof q->tag, "DELETE %d", ndel);
    return q;
}

/* The first column reference qualified by the given relation name, if any. */
static Expr *expr_find_qual(Expr *e, const char *rel)
{
    if (!e || !rel) return NULL;
    if (e->tag == X_COLREF && e->qual && !strcmp(e->qual, rel)) return e;
    for (int i = 0; i < e->nargs; i++) {
        Expr *r = expr_find_qual(e->args[i], rel);
        if (r) return r;
    }
    for (int i = 0; i < e->nwhen; i++) {
        Expr *r = expr_find_qual(e->when_cond[i], rel); if (r) return r;
        r = expr_find_qual(e->when_res[i], rel); if (r) return r;
    }
    Expr *r = expr_find_qual(e->case_arg, rel); if (r) return r;
    r = expr_find_qual(e->case_else, rel); if (r) return r;
    return expr_find_qual(e->testexpr, rel);
}

/* Is this base row one the view shows?  MERGE through a view may only match
 * rows the view's own qualification admits. */
static bool view_row_visible(Session *s, ExecCtx *c, Expr *qual, Table *t, Datum *vals)
{
    RowSet tmp; memset(&tmp, 0, sizeof tmp);
    tmp.ncols = t->ncols;
    tmp.cols = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(ColMeta));
    for (int i = 0; i < t->ncols; i++) {
        tmp.cols[i].rel = t->name; tmp.cols[i].name = t->cols[i].name;
        tmp.cols[i].type = t->cols[i].type; tmp.cols[i].typmod = t->cols[i].typmod;
        tmp.cols[i].hidden = false;
    }
    ExecCtx vc = *c; vc.rs = &tmp; vc.row = vals;
    bool ok = datum_true(to_bool(s, eval(&vc, qual)));
    free(tmp.cols);
    return ok;
}

/* WITH CHECK OPTION: a row written through the view has to satisfy the view's
 * own qualification, or PostgreSQL refuses it and prints the row. */
static void view_check_row(Session *s, ExecCtx *c, Expr *qual, const char *vname,
                           Table *t, Datum *vals)
{
    if (!qual) return;
    RowSet tmp; memset(&tmp, 0, sizeof tmp);
    tmp.ncols = t->ncols;
    tmp.cols = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(ColMeta));
    for (int i = 0; i < t->ncols; i++) {
        tmp.cols[i].rel = t->name; tmp.cols[i].name = t->cols[i].name;
        tmp.cols[i].type = t->cols[i].type; tmp.cols[i].typmod = t->cols[i].typmod;
        tmp.cols[i].hidden = false;
    }
    ExecCtx vc = *c; vc.rs = &tmp; vc.row = vals;
    if (datum_true(to_bool(s, eval(&vc, qual)))) { free(tmp.cols); return; }
    Buf b; buf_init(&b);
    for (int i = 0; i < t->ncols; i++) {
        if (i) buf_adds(&b, ", ");
        char *o = out_func(s, vals[i]);
        buf_adds(&b, vals[i].isnull ? "null" : o);
        free(o);
    }
    err_detail(s, "Failing row contains (%s).", b.data ? b.data : "");
    ereport(s, "44000", "new row violates check option for view \"%s\"", vname);
}

/* Rewrite a MERGE written against a view so that it names the base relation:
 * the join condition, every action's SET or INSERT list, every WHEN condition
 * and the RETURNING list. */
static void merge_map_stmt(MergeStmt *ns, ViewMap *vm)
{
    view_map_expr(ns->on, vm);
    for (int w = 0; w < ns->nwhens; w++) {
        MergeWhen *old = ns->whens[w];
        MergeWhen *mw = xmalloc(sizeof *mw); *mw = *old;
        view_map_expr(mw->cond, vm);
        /* a negative ncols means INSERT had no column list and |ncols| is the
         * number of values, so there is nothing to rename in that case */
        if (mw->ncols > 0 && old->cols) {
            mw->cols = xmalloc((size_t)mw->ncols * sizeof(char *));
            for (int i = 0; i < mw->ncols; i++)
                mw->cols[i] = view_map_col(vm, old->cols[i]);
        }
        int nv = mw->ncols < 0 ? -mw->ncols : mw->ncols;
        for (int i = 0; i < nv && mw->vals; i++) view_map_expr(mw->vals[i], vm);
        ns->whens[w] = mw;
    }
    for (int i = 0; i < ns->nret; i++) view_map_expr(ns->returning[i], vm);
    /* the view's own qualification is applied to the target rows in exec_merge;
     * it is already written in the base relation's names after this */
    if (vm->where) view_map_expr(vm->where, vm);
}

/* MERGE through a view whose INSTEAD OF row triggers do the writing.  The
 * target rows come from the view itself, and each action fires the trigger for
 * its event with the OLD and NEW rows PostgreSQL would pass. */
static QueryResult *exec_merge_instead(Session *s, MergeStmt *st, Table *v, ExecCtx *base)
{
    ExecCtx c; memset(&c, 0, sizeof c);
    c.s = s;
    if (base) { c.params = base->params; c.nparams = base->nparams; }
    if (st->nctes) {
        SelectStmt fake; memset(&fake, 0, sizeof fake);
        fake.ctes = st->ctes; fake.nctes = st->nctes;
        run_ctes(&c, &fake, &c);
    }
    RowSet *srcrs = exec_from(&c, st->source);
    RangeTbl rt; memset(&rt, 0, sizeof rt);
    rt.kind = 0; rt.schema = st->schema; rt.relname = st->name; rt.alias = st->alias;
    RowSet *tgtrs = exec_from(&c, &rt);

    RowSet probe; memset(&probe, 0, sizeof probe);
    probe.ncols = tgtrs->ncols + srcrs->ncols;
    probe.cols = xmalloc((size_t)(probe.ncols ? probe.ncols : 1) * sizeof(ColMeta));
    memcpy(probe.cols, tgtrs->cols, (size_t)tgtrs->ncols * sizeof(ColMeta));
    memcpy(probe.cols + tgtrs->ncols, srcrs->cols, (size_t)srcrs->ncols * sizeof(ColMeta));
    Datum *jrow = xmalloc((size_t)(probe.ncols ? probe.ncols : 1) * sizeof(Datum));

    int ins = 0, upd = 0, del = 0;
    for (int si = 0; si < srcrs->nrows; si++) {
        Datum *hit = NULL;
        for (int r = 0; r < tgtrs->nrows && !hit; r++) {
            memcpy(jrow, tgtrs->rows[r], (size_t)tgtrs->ncols * sizeof(Datum));
            memcpy(jrow + tgtrs->ncols, srcrs->rows[si], (size_t)srcrs->ncols * sizeof(Datum));
            ExecCtx mc = c; mc.rs = &probe; mc.row = jrow;
            if (datum_true(to_bool(s, eval(&mc, st->on)))) hit = tgtrs->rows[r];
        }
        for (int w = 0; w < st->nwhens; w++) {
            MergeWhen *mw = st->whens[w];
            if ((mw->kind == 0) != (hit != NULL)) continue;
            if (mw->cond) {
                if (hit) memcpy(jrow, hit, (size_t)tgtrs->ncols * sizeof(Datum));
                else for (int k = 0; k < tgtrs->ncols; k++) jrow[k] = d_null(tgtrs->cols[k].type);
                memcpy(jrow + tgtrs->ncols, srcrs->rows[si], (size_t)srcrs->ncols * sizeof(Datum));
                ExecCtx mc = c; mc.rs = &probe; mc.row = jrow;
                if (!datum_true(to_bool(s, eval(&mc, mw->cond)))) continue;
            }
            if (mw->action == 3) break;                    /* DO NOTHING */
            int event = mw->action == 0 ? 2 : mw->action == 1 ? 4 : 1;
            int nio = 0;
            Trigger **io = triggers_for(s->db, v->oid, event, 2, true, &nio);
            if (!nio) { free(io); break; }
            Datum *newrow = NULL;
            if (mw->action == 0) {                         /* UPDATE */
                newrow = xmalloc((size_t)(v->ncols ? v->ncols : 1) * sizeof(Datum));
                for (int k = 0; k < v->ncols; k++) newrow[k] = hit[k];
                ExecCtx mc = c; mc.rs = &probe; mc.row = jrow;
                for (int k = 0; k < mw->ncols; k++) {
                    int ci = col_index(v, mw->cols[k]);
                    if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" "
                                        "does not exist", mw->cols[k], v->name);
                    newrow[ci] = coerce_to(s, eval(&mc, mw->vals[k]),
                                           v->cols[ci].type, v->cols[ci].typmod, 1);
                }
                upd++;
            } else if (mw->action == 2) {                  /* INSERT */
                newrow = xmalloc((size_t)(v->ncols ? v->ncols : 1) * sizeof(Datum));
                for (int k = 0; k < v->ncols; k++) newrow[k] = d_null(v->cols[k].type);
                ExecCtx mc = c; mc.rs = &probe; mc.row = jrow;
                int nv = mw->ncols < 0 ? -mw->ncols : mw->ncols;
                for (int k = 0; k < nv; k++) {
                    int ci = mw->ncols < 0 ? k : col_index(v, mw->cols[k]);
                    if (ci < 0 || ci >= v->ncols) continue;
                    newrow[ci] = coerce_to(s, eval(&mc, mw->vals[k]),
                                           v->cols[ci].type, v->cols[ci].typmod, 1);
                }
                ins++;
            } else del++;
            for (int i = 0; i < nio; i++)
                fire_row_trigger(s, io[i], v, event, hit, newrow);
            free(io);
            break;
        }
    }
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "MERGE %d", ins + upd + del);
    return q;
}

static QueryResult *exec_merge(Session *s, Node *n, ExecCtx *base)
{
    MergeStmt *st = n->x;
    if (!tbl_find(s->db, st->schema, st->name))
        ereport_pos(s, "42P01", st->relpos, "relation \"%s\" does not exist", st->name);
    Table *t = resolve_table(s, st->schema, st->name, true);
    Expr *view_qual = NULL;     /* the view's own WHERE, when writing through one */
    Expr *view_check = NULL;    /* the same, when the view has WITH CHECK OPTION */
    const char *view_cname = NULL;
#if PGVC_VN >= 170
    /* PostgreSQL 17 is the release that let MERGE write through a view; 15 and
     * 16 refuse it, so this whole rewrite is gated on the release.  An INSTEAD
     * OF row trigger on the view takes the place of the write and makes the
     * view writable whether or not it is automatically updatable.  Otherwise
     * the view has to be automatically updatable, and the statement is
     * rewritten onto the base relation by the same rewrite INSERT, UPDATE and
     * DELETE use, so the actions, the join and the RETURNING list all see the
     * base relation's names. */
    if (t->relkind == 'v') {
        int want = 0;
        for (int w = 0; w < st->nwhens; w++) {
            int a = st->whens[w]->action;
            want |= a == 0 ? 2 : a == 1 ? 4 : a == 2 ? 1 : 0;
        }
        int nio = 0;
        Trigger **io = triggers_for(s->db, t->oid, want, 2, true, &nio);
        free(io);
        if (nio > 0) return exec_merge_instead(s, st, t, base);
    }
    for (int guard = 0; t->relkind == 'v' && guard < 10; guard++) {
        ViewMap vm;
        if (!view_updatable(s, t, &vm)) {
            /* the verb PostgreSQL names is the first action's */
            const char *verb = "update", *trig = "UPDATE";
            for (int w = 0; w < st->nwhens; w++) {
                int a = st->whens[w]->action;   /* 0 update 1 delete 2 insert */
                if (a == 1) { verb = "delete from"; trig = "DELETE"; break; }
                if (a == 2) { verb = "insert into"; trig = "INSERT"; break; }
                if (a == 0) { verb = "update"; trig = "UPDATE"; break; }
            }
            view_not_updatable(s, t, &vm, verb, trig, true);
        }
        MergeStmt *ns = xmalloc(sizeof *ns); *ns = *st;
        ns->schema = vm.base->schema; ns->name = vm.base->name;
        if (!ns->alias) ns->alias = t->name;
        merge_map_stmt(ns, &vm);
        st = ns;
        view_qual = vm.where;
        if (t->check_option) { view_check = vm.where; view_cname = t->name; }
        t = vm.base;
    }
#endif
    /* Still reached for a materialized view at every release, and for a plain
     * view before 17.  A view handled above has become its base relation. */
    if (t->relkind == 'v' || t->relkind == 'm') {
        err_detail(s, "This operation is not supported for %s.",
                   t->relkind == 'v' ? "views" : "materialized views");
        ereport(s, "0A000", "cannot execute MERGE on relation \"%s\"", t->name);
    }
    /* An UPDATE action's target columns are resolved at parse-analysis time, so
     * an unknown one is reported whether or not any row matches.  The execution
     * paths below resolve them inside the matched-row loop instead, which lets
     * the statement succeed silently when nothing matches.  MERGE arrived in 15
     * and has rejected the statement outright ever since. */
#if PGVC_VN >= 150
    for (int w = 0; w < st->nwhens; w++) {
        MergeWhen *mw = st->whens[w];
        if (mw->action != 0) continue;      /* only UPDATE actions name columns */
        for (int k = 0; k < mw->ncols; k++)
            if (col_index(t, mw->cols[k]) < 0)
                ereport_pos(s, "42703", mw->colpos ? mw->colpos[k] : 0,
                            "column \"%s\" of relation \"%s\" does not exist",
                            mw->cols[k], t->name);
    }
#endif
    ExecCtx c; memset(&c, 0, sizeof c);
    c.s = s;
    if (base) { c.params = base->params; c.nparams = base->nparams; }
    if (st->nctes) {
        SelectStmt fake; memset(&fake, 0, sizeof fake);
        fake.ctes = st->ctes; fake.nctes = st->nctes;
        run_ctes(&c, &fake, &c);
    }
    RowSet *srcrs = exec_from(&c, st->source);
    RangeTbl rt; memset(&rt, 0, sizeof rt);
    rt.kind = 0; rt.schema = st->schema; rt.relname = st->name; rt.alias = st->alias;
    RowSet *tgtrs = exec_from(&c, &rt);

    RowSet probe; memset(&probe, 0, sizeof probe);
    probe.ncols = tgtrs->ncols + srcrs->ncols;
    probe.cols = xmalloc((size_t)(probe.ncols?probe.ncols:1)*sizeof(ColMeta));
    memcpy(probe.cols, tgtrs->cols, (size_t)tgtrs->ncols*sizeof(ColMeta));
    memcpy(probe.cols + tgtrs->ncols, srcrs->cols, (size_t)srcrs->ncols*sizeof(ColMeta));
    Datum *jrow = xmalloc((size_t)(probe.ncols?probe.ncols:1)*sizeof(Datum));

    int ins = 0, upd = 0, del = 0;
    Datum **retrows = NULL; Datum **retold = NULL; Datum **retsrc = NULL; int nret = 0;
    int *actions = NULL;
    /* The target as it stood when the command started: MERGE's own INSERTs
     * append here, and they are neither matchable by a later source row nor
     * indexable in tgt_matched. */
    Table **tparts = NULL; int ntparts = 0;
    collect_partitions(s->db, t, &tparts, &ntparts);
    Row **tgt_rows = NULL; Table **tgt_tab = NULL; int tgt_n = 0;
    for (int q = 0; q < ntparts; q++) {
        Table *pt = tparts[q];
        if (pt->relkind == 'p') continue;
        for (int r = 0; r < pt->nrows; r++) {
            tgt_rows = xrealloc(tgt_rows, (size_t)(tgt_n + 1) * sizeof(Row *));
            tgt_tab  = xrealloc(tgt_tab,  (size_t)(tgt_n + 1) * sizeof(Table *));
            tgt_rows[tgt_n] = pt->rows[r]; tgt_tab[tgt_n] = pt; tgt_n++;
        }
    }
    bool *tgt_matched = xmalloc((size_t)(tgt_n ? tgt_n : 1) * sizeof(bool));
    memset(tgt_matched, 0, (size_t)(tgt_n ? tgt_n : 1) * sizeof(bool));
    /* the row versions an UPDATE action produces are held back until the whole
     * statement has run, so an error later on leaves the table untouched */
    Row **updrow = NULL; Datum **updvals = NULL; int nupd = 0;
    Row **delrow = NULL; int ndel = 0;

#if PGVC_VN >= 170
    /* a WHEN NOT MATCHED BY SOURCE clause cannot see the source relation */
    {
        const char *sa = st->source && st->source->alias ? st->source->alias
                         : (srcrs->ncols ? srcrs->cols[0].rel : NULL);
        for (int w = 0; sa && w < st->nwhens; w++) {
            MergeWhen *mw = st->whens[w];
            if (mw->kind != 2 || !mw->cond) continue;
            Expr *bad = expr_find_qual(mw->cond, sa);
            if (bad) {
                err_detail(s, "There is an entry for table \"%s\", but it cannot be "
                              "referenced from this part of the query.", sa);
                ereport_pos(s, "42P01", bad->pos,
                            "invalid reference to FROM-clause entry for table \"%s\"", sa);
            }
        }
    }
#endif
    lock_table_write(s, t);
    for (int q = 0; q < ntparts; q++)
        if (tparts[q]->relkind != 'p') lock_table_write(s, tparts[q]);
    for (int si = 0; si < srcrs->nrows; si++) {
        bool matched = false;
        for (int r = 0; r < tgt_n; r++) {
            Row *row = tgt_rows[r];
            Table *rt2 = tgt_tab[r];
            if (!row_visible(s, row)) continue;
            if (view_qual && !view_row_visible(s, &c, view_qual, rt2, row->vals)) continue;
            int q = 0;
            for (int k = 0; k < rt2->ncols; k++) if (!rt2->cols[k].dropped) jrow[q++] = row->vals[k];
            memcpy(jrow + tgtrs->ncols, srcrs->rows[si], (size_t)srcrs->ncols * sizeof(Datum));
            ExecCtx mc = c; mc.rs = &probe; mc.row = jrow;
            if (!datum_true(to_bool(s, eval(&mc, st->on)))) continue;
            if (tgt_matched[r]) {
                err_hint(s, "Ensure that not more than one source row matches any one "
                            "target row.");
                ereport(s, "21000", "MERGE command cannot affect row a second time");
            }
            matched = true; tgt_matched[r] = true;
            /* a matched action takes the row lock, so a concurrent writer waits */
            {
                bool acts = false;
                for (int w = 0; w < st->nwhens; w++)
                    if (st->whens[w]->kind == 0 && st->whens[w]->action != 3) acts = true;
                if (acts) { row_lock_wait(s, row); row_mark_locked(s, row); }
            }
            for (int w = 0; w < st->nwhens; w++) {
                MergeWhen *mw = st->whens[w];
                if (mw->kind != 0) continue;
                if (mw->cond && !datum_true(to_bool(s, eval(&mc, mw->cond)))) continue;
                if (mw->action == 3) break;
                if (mw->action == 1) {
                    fk_check_delete(s, rt2, row);
                    delrow = xrealloc(delrow, (size_t)(ndel+1)*sizeof(Row*));
                    delrow[ndel++] = row;
                    del++;
                    retrows = xrealloc(retrows, (size_t)(nret+1)*sizeof(Datum*));
                    retold = xrealloc(retold, (size_t)(nret+1)*sizeof(Datum*));
                    retsrc = xrealloc(retsrc, (size_t)(nret+1)*sizeof(Datum*));
                    actions = xrealloc(actions, (size_t)(nret+1)*sizeof(int));
                    retrows[nret] = row->vals; retold[nret] = row->vals;
                    retsrc[nret] = srcrs->rows[si]; actions[nret] = 2; nret++;
                } else if (mw->action == 0) {
                    Datum *oldv = xmalloc((size_t)(rt2->ncols?rt2->ncols:1)*sizeof(Datum));
                    memcpy(oldv, row->vals, (size_t)rt2->ncols*sizeof(Datum));
                    Datum *nv = xmalloc((size_t)(rt2->ncols?rt2->ncols:1)*sizeof(Datum));
                    memcpy(nv, row->vals, (size_t)rt2->ncols*sizeof(Datum));
                    for (int k = 0; k < mw->ncols; k++) {
                        int ci = col_index(rt2, mw->cols[k]);
                        if (ci < 0)
                            ereport_pos(s, "42703", mw->colpos ? mw->colpos[k] : 0,
                                        "column \"%s\" of relation \"%s\" does not exist",
                                        mw->cols[k], t->name);
                        if (mw->vals[k]->tag == X_CONST && mw->vals[k]->alias &&
                            !strcmp(mw->vals[k]->alias, "__default__"))
                            nv[ci] = eval_default(s, rt2, &rt2->cols[ci], nv);
                        else
                            nv[ci] = coerce_to(s, eval(&mc, mw->vals[k]), rt2->cols[ci].type,
                                               rt2->cols[ci].typmod, 1);
                    }
                    apply_generated(s, rt2, nv);
                    check_constraints(s, rt2, nv, row);
                    view_check_row(s, &c, view_check, view_cname, rt2, nv);
                    updrow = xrealloc(updrow, (size_t)(nupd+1)*sizeof(Row*));
                    updvals = xrealloc(updvals, (size_t)(nupd+1)*sizeof(Datum*));
                    updrow[nupd] = row; updvals[nupd] = nv; nupd++;
                    upd++;
                    retrows = xrealloc(retrows, (size_t)(nret+1)*sizeof(Datum*));
                    retold = xrealloc(retold, (size_t)(nret+1)*sizeof(Datum*));
                    retsrc = xrealloc(retsrc, (size_t)(nret+1)*sizeof(Datum*));
                    actions = xrealloc(actions, (size_t)(nret+1)*sizeof(int));
                    retrows[nret] = nv; retold[nret] = oldv;
                    retsrc[nret] = srcrs->rows[si]; actions[nret] = 1; nret++;
                }
                break;
            }
            break;
        }
        if (matched) continue;
        for (int w = 0; w < st->nwhens; w++) {
            MergeWhen *mw = st->whens[w];
            if (mw->kind != 1) continue;
            int q = 0;
            for (int k = 0; k < tgtrs->ncols; k++) jrow[q++] = d_null(tgtrs->cols[k].type);
            memcpy(jrow + tgtrs->ncols, srcrs->rows[si], (size_t)srcrs->ncols * sizeof(Datum));
            ExecCtx mc = c; mc.rs = &probe; mc.row = jrow;
            if (mw->cond && !datum_true(to_bool(s, eval(&mc, mw->cond)))) continue;
            if (mw->action == 3) break;
            Datum *vals = row_new(t->ncols);
            bool *given = xmalloc((size_t)(t->ncols?t->ncols:1)*sizeof(bool));
            memset(given, 0, (size_t)(t->ncols?t->ncols:1)*sizeof(bool));
            int ncols = mw->ins_default_values ? 0 : (mw->ncols < 0 ? -mw->ncols : mw->ncols);
            for (int k = 0; k < ncols; k++) {
                int ci = (mw->ncols < 0) ? k : col_index(t, mw->cols[k]);
                if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                    mw->cols[k], t->name);
                if (!mw->vals) break;
                vals[ci] = coerce_to(s, eval(&mc, mw->vals[k]), t->cols[ci].type, t->cols[ci].typmod, 1);
                given[ci] = true;
            }
            for (int k = 0; k < t->ncols; k++)
                if (!given[k] && !t->cols[k].generated) vals[k] = eval_default(s, t, &t->cols[k], vals);
            apply_generated(s, t, vals);
            view_check_row(s, &c, view_check, view_cname, t, vals);
            Table *landed = NULL;
            insert_row_into(s, t, vals, &landed);
            ins++;
            retrows = xrealloc(retrows, (size_t)(nret+1)*sizeof(Datum*));
            retold = xrealloc(retold, (size_t)(nret+1)*sizeof(Datum*));
            retsrc = xrealloc(retsrc, (size_t)(nret+1)*sizeof(Datum*));
            actions = xrealloc(actions, (size_t)(nret+1)*sizeof(int));
            retrows[nret] = landed->rows[landed->nrows-1]->vals; retold[nret] = NULL;
            retsrc[nret] = srcrs->rows[si]; actions[nret] = 0; nret++;
            free(given);
            break;
        }
    }
#if PGVC_VN >= 170
    /* WHEN NOT MATCHED BY SOURCE */
    for (int r = 0; r < tgt_n; r++) {
        Row *row = t->rows[r];
        if (!row_visible(s, row) || tgt_matched[r]) continue;
        for (int w = 0; w < st->nwhens; w++) {
            MergeWhen *mw = st->whens[w];
            if (mw->kind != 2) continue;
            int q = 0;
            for (int k = 0; k < t->ncols; k++) if (!t->cols[k].dropped) jrow[q++] = row->vals[k];
            for (int k = 0; k < srcrs->ncols; k++) jrow[tgtrs->ncols + k] = d_null(srcrs->cols[k].type);
            ExecCtx mc = c; mc.rs = &probe; mc.row = jrow;
            if (mw->cond && !datum_true(to_bool(s, eval(&mc, mw->cond)))) continue;
            if (mw->action == 3) break;
            if (mw->action == 1) {
                fk_check_delete(s, t, row);
                row->xmax = xact_xid(s);
                del++;
                retrows = xrealloc(retrows, (size_t)(nret+1)*sizeof(Datum*));
                retold = xrealloc(retold, (size_t)(nret+1)*sizeof(Datum*));
                retsrc = xrealloc(retsrc, (size_t)(nret+1)*sizeof(Datum*));
                actions = xrealloc(actions, (size_t)(nret+1)*sizeof(int));
                retrows[nret] = row->vals; retold[nret] = row->vals;
                retsrc[nret] = NULL; actions[nret] = 2; nret++;
            } else if (mw->action == 0) {
                Datum *oldv = xmalloc((size_t)(t->ncols?t->ncols:1)*sizeof(Datum));
                memcpy(oldv, row->vals, (size_t)t->ncols*sizeof(Datum));
                Datum *nv = xmalloc((size_t)(t->ncols?t->ncols:1)*sizeof(Datum));
                memcpy(nv, row->vals, (size_t)t->ncols*sizeof(Datum));
                for (int k = 0; k < mw->ncols; k++) {
                    int ci = col_index(t, mw->cols[k]);
                    if (ci >= 0) nv[ci] = coerce_to(s, eval(&mc, mw->vals[k]),
                                                    t->cols[ci].type, t->cols[ci].typmod, 1);
                }
                check_constraints(s, t, nv, row);
                row->vals = nv;
                upd++;
                retrows = xrealloc(retrows, (size_t)(nret+1)*sizeof(Datum*));
                retold = xrealloc(retold, (size_t)(nret+1)*sizeof(Datum*));
                retsrc = xrealloc(retsrc, (size_t)(nret+1)*sizeof(Datum*));
                actions = xrealloc(actions, (size_t)(nret+1)*sizeof(int));
                retrows[nret] = nv; retold[nret] = oldv;
                retsrc[nret] = NULL; actions[nret] = 1; nret++;
            }
            break;
        }
    }
#endif
    /* now that nothing can fail, install the new row versions */
    for (int i = 0; i < nupd; i++) updrow[i]->vals = updvals[i];
    for (int i = 0; i < ndel; i++) delrow[i]->xmax = xact_xid(s);
    free(updrow); free(updvals); free(delrow);
    xact_touch(s, t);
    QueryResult *q;
    if (st->nret) {
        RowSet *proj = rs_new();
        RowSet tmp; memset(&tmp, 0, sizeof tmp);
        tmp.ncols = t->ncols + srcrs->ncols;
        tmp.cols = xmalloc((size_t)(tmp.ncols?tmp.ncols:1)*sizeof(ColMeta));
        for (int k = 0; k < t->ncols; k++) {
            tmp.cols[k].rel = st->alias ? st->alias : t->name; tmp.cols[k].name = t->cols[k].name;
            tmp.cols[k].type = t->cols[k].type; tmp.cols[k].typmod = t->cols[k].typmod;
            tmp.cols[k].hidden = false;
        }
        /* RETURNING may also name the source relation's columns */
        memcpy(tmp.cols + t->ncols, srcrs->cols, (size_t)srcrs->ncols * sizeof(ColMeta));
        RowSet tgtonly = tmp;
        tgtonly.ncols = t->ncols;
        for (int i = 0; i < st->nret; i++)
            rs_addcol(proj, NULL, expr_label(st->returning[i]), TEXTOID, -1);
        for (int r = 0; r < nret; r++) {
            Datum *joined = xmalloc((size_t)(tmp.ncols?tmp.ncols:1)*sizeof(Datum));
            memcpy(joined, retrows[r], (size_t)t->ncols * sizeof(Datum));
            for (int k = 0; k < srcrs->ncols; k++)
                joined[t->ncols + k] = retsrc[r] ? retsrc[r][k] : d_null(srcrs->cols[k].type);
            ExecCtx c2 = c; c2.rs = &tmp; c2.row = joined;
            c2.oldnew_rs = &tgtonly; c2.oldrow = retold[r];
            c2.newrow = (actions[r] == 2) ? NULL : retrows[r];
            c2.merge_action = actions[r] + 1;
            Datum *out = row_new(st->nret);
            for (int i = 0; i < st->nret; i++) out[i] = eval(&c2, st->returning[i]);
            rs_addrow(proj, out);
        }
        for (int i = 0; i < proj->ncols; i++) {
            Oid ty = 0;
            for (int k = 0; k < proj->nrows; k++) ty = common_type(s, ty, proj->rows[k][i].type);
            proj->cols[i].type = (!ty || ty == UNKNOWNOID) ? TEXTOID : ty;
        }
        q = rs_to_qr(s, proj);
    } else q = qr_new();
    snprintf(q->tag, sizeof q->tag, "MERGE %d", ins + upd + del);
    return q;
}

/* ====================================================================== DDL */
Oid new_oid(void);
void tbl_add(Database *db, Table *t);
void tbl_drop(Database *db, Table *t);
Schema *sch_create(Database *db, const char *name, const char *owner);

static Table *mk_table(Database *db, const char *schema, const char *name, char kind)
{
    Table *t = xmalloc(sizeof *t);
    memset(t, 0, sizeof *t);
    t->oid = new_oid();
    t->schema = xstrdup(schema); t->name = xstrdup(name);
    t->relkind = kind; t->owner = xstrdup("postgres");
    pthread_mutex_init(&t->lock, NULL);
    return t;
}

static void add_col(Table *t, const char *name, Oid type, int32 typmod)
{
    t->cols = xrealloc(t->cols, (size_t)(t->ncols + 1) * sizeof(Column));
    Column *c = &t->cols[t->ncols];
    memset(c, 0, sizeof *c);
    c->name = xstrdup(name); c->type = type; c->typmod = typmod;
    c->attnum = t->ncols + 1;
    t->ncols++;
}

static void add_cons(Table *t, Constraint *c)
{
    t->cons = xrealloc(t->cons, (size_t)(t->ncons + 1) * sizeof(Constraint *));
    t->cons[t->ncons++] = c;
}

static Table *make_sequence(Session *s, const char *schema, const char *name,
                            int64 start, int64 inc, int64 minv, int64 maxv, bool cycle)
{
    Table *q = mk_table(s->db, schema, name, 'S');
    add_col(q, "last_value", INT8OID, -1);
    q->seq_start = start; q->seq_inc = inc; q->seq_min = minv; q->seq_max = maxv;
    q->seq_cache = 1; q->seq_cycle = cycle; q->seq_last = start; q->seq_called = false;
    tbl_add(s->db, q);
    return q;
}

static int64 seq_nextval(Session *s, Table *seq)
{
    pthread_mutex_lock(&seq->lock);
    int64 v;
    if (!seq->seq_called) { v = seq->seq_start; seq->seq_called = true; }
    else {
        v = seq->seq_last + seq->seq_inc;
        if (seq->seq_inc > 0 && v > seq->seq_max) {
            if (!seq->seq_cycle) { pthread_mutex_unlock(&seq->lock);
                ereport(s, "2200H", "nextval: reached maximum value of sequence \"%s\" (%lld)",
                        seq->name, (long long)seq->seq_max); }
            v = seq->seq_min;
        }
        if (seq->seq_inc < 0 && v < seq->seq_min) {
            if (!seq->seq_cycle) { pthread_mutex_unlock(&seq->lock);
                ereport(s, "2200H", "nextval: reached minimum value of sequence \"%s\" (%lld)",
                        seq->name, (long long)seq->seq_min); }
            v = seq->seq_max;
        }
    }
    seq->seq_last = v;
    pthread_mutex_unlock(&seq->lock);
    return v;
}

int64 seq_call(Session *s, const char *name, int which, int64 arg, bool called)
{
    Table *seq = tbl_find(s->db, NULL, name);
    if (!seq || seq->relkind != 'S')
        ereport(s, "42P01", "relation \"%s\" does not exist", name);
    if (which == 0) return seq_nextval(s, seq);
    if (which == 1) {
        if (!seq->seq_called)
            ereport(s, "55000", "currval of sequence \"%s\" is not yet defined in this session", name);
        return seq->seq_last;
    }
    seq->seq_last = arg; seq->seq_called = called;
    return arg;
}

char *index_def_text(Database *db, Index *ix)
{
    Table *t = tbl_find_oid(db, ix->table);
    const char *am = ix->am == 405 ? "hash" : ix->am == 783 ? "gist" :
                     ix->am == 2742 ? "gin" : ix->am == 3580 ? "brin" :
                     ix->am == 4000 ? "spgist" : "btree";
    Buf b; buf_init(&b);
    buf_printf(&b, "CREATE %sINDEX %s ON %s.%s USING %s (",
               ix->unique ? "UNIQUE " : "", ix->name,
               t ? t->schema : "public", t ? t->name : "?", am);
    for (int i = 0; i < ix->nkeys; i++) {
        if (i) buf_adds(&b, ", ");
        /* an expression key is deparsed inside its own parentheses */
        if (ix->exprs && ix->exprs[i]) buf_printf(&b, "(%s)", ix->exprs[i]);
        else if (t) buf_adds(&b, t->cols[ix->keys[i]-1].name);
    }
    buf_addc(&b, ')');
    if (ix->nincl) {
        buf_adds(&b, " INCLUDE (");
        for (int i = 0; i < ix->nincl; i++) {
            if (i) buf_adds(&b, ", ");
            if (t) buf_adds(&b, t->cols[ix->incl[i]-1].name);
        }
        buf_addc(&b, ')');
    }
#if PGVC_VN >= 150
    if (ix->nulls_nd) buf_adds(&b, " NULLS NOT DISTINCT");
#endif
    if (ix->nopts) {
        buf_adds(&b, " WITH (");
        for (int i = 0; i < ix->nopts; i++) {
            if (i) buf_adds(&b, ", ");
            buf_printf(&b, "%s=%s", ix->optname[i], ix->optval[i]);
        }
        buf_addc(&b, ')');
    }
    if (ix->predicate) buf_printf(&b, " WHERE %s", ix->predicate);
    return buf_steal(&b);
}

static void add_index(Database *db, Table *t, const char *name, int *keys, int nkeys,
                      int *incl, int nincl, bool unique, bool primary, const char *pred,
                      char **exprs)
{
    Index *ix = xmalloc(sizeof *ix);
    memset(ix, 0, sizeof *ix);
    ix->oid = new_oid(); ix->name = xstrdup(name); ix->table = t->oid;
    ix->keys = keys; ix->nkeys = nkeys; ix->incl = incl; ix->nincl = nincl;
    ix->unique = unique; ix->primary = primary;
    ix->predicate = pred ? xstrdup(pred) : NULL;
    ix->exprs = exprs;
    ix->partitioned = (t->relkind == 'p');
    ix->am = 403;                       /* btree unless the caller says otherwise */
    ix->valid = true;
    db->indexes = xrealloc(db->indexes, (size_t)(db->nindexes + 1) * sizeof(Index *));
    db->indexes[db->nindexes++] = ix;
    catalog_note(3 /* U_ADD_INDEX */, db, ix);
}

static void attach_partition(Session *s, Table *parent, Table *child,
                             CreateTableStmt *bnd, bool attaching);
static void clone_part_indexes(Session *s, Database *db, Table *parent, Table *child);
static void clone_index_to(Session *s, Database *db, Index *parent, Table *child);

static QueryResult *exec_create_table(Session *s, Node *n)
{
    CreateTableStmt *st = n->x;
    Database *db = s->db;
    const char *schema = st->schema ? st->schema : "public";
    if (!sch_find(db, schema)) ereport(s, "3F000", "schema \"%s\" does not exist", schema);
    if (!st->temporary && !schema_priv(s, sch_find(db, schema), NULL, 'C'))
        ereport_pos(s, "42501", st->name_pos, "permission denied for schema %s", schema);
    if (tbl_find(db, schema, st->name)) {
        if (st->if_not_exists) {
            notice(s, "42P07", "relation \"%s\" already exists, skipping", st->name);
            QueryResult *q = qr_new();
            snprintf(q->tag, sizeof q->tag, "CREATE TABLE");
            return q;
        }
        ereport(s, "42P07", "relation \"%s\" already exists", st->name);
    }
#if PGVC_VN >= 120
    if (st->with_oids)
        ereport_pos(s, "42601", st->oids_pos, "syntax error at or near \"OIDS\"");
#endif
    /* the WITH list of a table is checked against the table reloptions */
    for (int i = 0; i < st->nreloptions; i++) {
        if (!strcmp(st->reloptions[i], "oids")) continue;
        char *v = rel_opt_check(s, 'r', st->reloptions[i],
                                st->reloptvals ? st->reloptvals[i] : "true");
        free(v);
    }
    if (st->using_am) {
#if PGVC_VN < 120
        ereport_pos(s, "42601", st->using_pos, "syntax error at or near \"USING\"");
#else
        static const char *idx[] = {"btree","hash","gist","gin","spgist","brin",NULL};
        bool isidx = false;
        for (int i = 0; idx[i]; i++) if (!strcasecmp(idx[i], st->using_am)) isidx = true;
        if (strcasecmp(st->using_am, "heap") && !isidx)
            ereport(s, "42704", "access method \"%s\" does not exist", st->using_am);
        if (isidx)
            ereport(s, "42809", "access method \"%s\" is not of type TABLE", st->using_am);
#endif
    }
    Table *t = mk_table(db, schema, st->name, st->partstrat ? 'p' : 'r');
    free(t->owner); t->owner = xstrdup(session_role(s));

    Table *parent = NULL;
    if (st->parent_name) {
        parent = tbl_find(db, st->parent_schema, st->parent_name);
        if (!parent) ereport(s, "42P01", "relation \"%s\" does not exist", st->parent_name);
        if (!parent->partstrat)
            ereport(s, "42809", "\"%s\" is not partitioned", parent->name);
        for (int i = 0; i < parent->ncols; i++) {
            add_col(t, parent->cols[i].name, parent->cols[i].type, parent->cols[i].typmod);
            t->cols[t->ncols-1].notnull = parent->cols[i].notnull;
            t->cols[t->ncols-1].defexpr = parent->cols[i].defexpr ? xstrdup(parent->cols[i].defexpr) : NULL;
            t->cols[t->ncols-1].generated = parent->cols[i].generated;
            t->cols[t->ncols-1].genexpr = parent->cols[i].genexpr ? xstrdup(parent->cols[i].genexpr) : NULL;
            t->cols[t->ncols-1].identity = parent->cols[i].identity;
            t->cols[t->ncols-1].identity_seq = parent->cols[i].identity_seq ?
                                               xstrdup(parent->cols[i].identity_seq) : NULL;
        }
#if PGVC_VN >= 180
        /* A partition carries its parent's not-null constraints under the
         * parent's names, exactly as an INHERITS child does.  Without this the
         * generic pass below names them after the partition instead. */
        for (int k = 0; k < parent->ncons; k++) {
            if (parent->cons[k]->contype != 'n' || parent->cons[k]->noinherit) continue;
            Constraint *cc = xmalloc(sizeof *cc);
            *cc = *parent->cons[k];
            cc->oid = 0;
            cc->name = xstrdup(parent->cons[k]->name);
            cc->inhcount = 1; cc->islocal = false;
            add_cons(t, cc);
        }
#endif
        if (st->partstrat) t->relkind = 'p';
    }
    if (st->like_table) {
        Table *lk = tbl_find(db, NULL, st->like_table);
        if (!lk) ereport(s, "42P01", "relation \"%s\" does not exist", st->like_table);
        for (int i = 0; i < lk->ncols; i++)
            add_col(t, lk->cols[i].name, lk->cols[i].type, lk->cols[i].typmod);
    }
    for (int i = 0; i < st->ninherits; i++) {
        Table *ih = tbl_find(db, NULL, st->inherits[i]);
        if (!ih) ereport(s, "42P01", "relation \"%s\" does not exist", st->inherits[i]);
        for (int k = 0; k < ih->ncols; k++)
            if (col_index(t, ih->cols[k].name) < 0) {
                add_col(t, ih->cols[k].name, ih->cols[k].type, ih->cols[k].typmod);
                t->cols[t->ncols-1].notnull = ih->cols[k].notnull;
            }
#if PGVC_VN >= 180
        /* a child carries its parents' not-null constraints under their names */
        for (int k = 0; k < ih->ncons; k++) {
            if (ih->cons[k]->contype != 'n' || ih->cons[k]->noinherit) continue;
            Constraint *cc = xmalloc(sizeof *cc);
            *cc = *ih->cons[k];
            cc->oid = 0;
            cc->name = xstrdup(ih->cons[k]->name);
            cc->inhcount = 1; cc->islocal = false;
            add_cons(t, cc);
        }
#endif
    }
    if (st->as_query) {
        ExecCtx c; memset(&c, 0, sizeof c); c.s = s;
        RowSet *rs = exec_select(&c, st->as_query->select);
        for (int i = 0; i < rs->ncols; i++)
            add_col(t, (i < st->ncols && st->cols[i]) ? st->cols[i]->name : rs->cols[i].name,
                    rs->cols[i].type == UNKNOWNOID ? TEXTOID : rs->cols[i].type, rs->cols[i].typmod);
        tbl_add(db, t);
        for (int i = 0; i < rs->nrows; i++) {
            Datum *vals = row_new(t->ncols);
            for (int k = 0; k < t->ncols; k++) vals[k] = coerce_to(s, rs->rows[i][k], t->cols[k].type, -1, 1);
            insert_row_into(s, t, vals, NULL);
        }
        QueryResult *q = qr_new();
        snprintf(q->tag, sizeof q->tag, "SELECT %d", rs->nrows);
        return q;
    }
    for (int i = 0; i < st->ncols; i++) {
        ColDef *cd = st->cols[i];
        if (!cd->typename) {            /* PARTITION OF column options */
            int ci = col_index(t, cd->name);
            if (ci < 0) ereport(s, "42703", "column \"%s\" does not exist", cd->name);
            if (cd->defexpr) t->cols[ci].defexpr = xstrdup(cd->defexpr);
            if (cd->notnull) t->cols[ci].notnull = true;
            continue;
        }
        if (!cd->type) ereport_pos(s, "42704", cd->typepos, "type \"%s\" does not exist", cd->typename);
        if (col_index(t, cd->name) >= 0)
            ereport(s, "42701", "column \"%s\" specified more than once", cd->name);
        add_col(t, cd->name, cd->type, cd->typmod);
        Column *col = &t->cols[t->ncols - 1];
        col->notnull = cd->notnull;
        col->defexpr = cd->defexpr ? xstrdup(cd->defexpr) : NULL;
        col->collation = cd->collation ? xstrdup(cd->collation) : NULL;
#if PGVC_VN >= 140
        if (cd->compression) {
            if (!strcasecmp(cd->compression, "default")) col->compression = 0;
            else if (!strcasecmp(cd->compression, "pglz")) col->compression = 'p';
            else if (!strcasecmp(cd->compression, "lz4")) col->compression = 'l';
            else ereport(s, "42704", "invalid compression method \"%s\"", cd->compression);
        }
#endif
        if (cd->generated) {
            char g = cd->generated;
            if (g == 'd') {
#if PGVC_VN >= 180
                g = 'v';                     /* VIRTUAL is the default from 18 */
#else
                ereport(s, "42601", "syntax error at or near \")\"");
#endif
            }
#if PGVC_VN < 180
            if (g == 'v')
                ereport_pos(s, "42601", cd->virtual_pos, "syntax error at or near \"VIRTUAL\"");
#endif
            if (cd->defexpr)
                ereport_pos(s, "42601", cd->default_pos,
                            "both default and generation expression specified for column "
                            "\"%s\" of table \"%s\"", cd->name, st->name);
            if (cd->genexpr_e) {
                Expr *bad = NULL;
                if (expr_has_subquery(cd->genexpr_e)) {
                    bad = gen_find_sublink(cd->genexpr_e);
                    ereport_pos(s, "0A000", bad ? bad->pos : cd->identity_pos,
                                "cannot use subquery in column generation expression");
                }
                bad = gen_find_gencol(cd->genexpr_e, st);
                if (bad) {
                    err_detail(s, "A generated column cannot reference another generated column.");
                    ereport_pos(s, "0A000", bad->pos,
                                "cannot use generated column \"%s\" in column generation expression",
                                bad->colname);
                }
                if (expr_is_volatile(s, cd->genexpr))
                    ereport(s, "42P17", "generation expression is not immutable");
            }
            col->generated = g;
            col->genexpr = xstrdup(cd->genexpr);
            col->notnull = cd->notnull;
        }
        if (cd->identity) {
#if PGVC_VN < 100
            ereport(s, "42601", "syntax error at or near \"GENERATED\"");
#endif
            if (col->type != INT2OID && col->type != INT4OID && col->type != INT8OID)
                ereport(s, "42601", "identity column type must be smallint, integer, or bigint");
            if (cd->defexpr)
                ereport_pos(s, "42601",
                            cd->default_pos > cd->identity_pos ? cd->default_pos
                                                               : cd->identity_pos,
                            "both default and identity specified for column \"%s\" of table \"%s\"",
                            cd->name, st->name);
            col->identity = cd->identity;
            Buf sn; buf_init(&sn);
            buf_printf(&sn, "%s_%s_seq", st->name, cd->name);
            col->identity_seq = buf_steal(&sn);
            col->notnull = true;
        }
        if (!strcmp(cd->typename, "serial") || !strcmp(cd->typename, "bigserial") ||
            !strcmp(cd->typename, "smallserial") || !strcmp(cd->typename, "serial4") ||
            !strcmp(cd->typename, "serial8") || !strcmp(cd->typename, "serial2")) {
            Buf sn; buf_init(&sn);
            buf_printf(&sn, "%s_%s_seq", st->name, cd->name);
            char *seqname = buf_steal(&sn);
            make_sequence(s, schema, seqname, 1, 1, 1, INT64_MAX, false)->seq_owner = t->oid;
            Buf de; buf_init(&de);
            buf_printf(&de, "nextval('%s'::regclass)", seqname);
            col->defexpr = buf_steal(&de);
            col->notnull = true;
        }
#if PGVC_VN < 110
        /* PostgreSQL 11 is the first release that allows a partitioned table
         * to carry a primary key or unique constraint. */
        if ((cd->primary || cd->unique) && st->partstrat)
            ereport_pos(s, "0A000", cd->cons_pos, "%s constraints are not supported on "
                        "partitioned tables", cd->primary ? "primary key" : "unique");
        if (cd->fk_table && st->partstrat)
            ereport_pos(s, "0A000", cd->cons_pos,
                        "foreign key constraints are not supported on partitioned tables");
#endif
        if (cd->primary) {
            Constraint *c = xmalloc(sizeof *c); memset(c, 0, sizeof *c);
            c->contype = 'p'; c->name = default_con_name(s, t, NULL, 'p');
            c->keys = xmalloc(sizeof(int)); c->keys[0] = col->attnum; c->nkeys = 1;
            add_cons(t, c);
        }
        if (cd->unique) {
            Constraint *c = xmalloc(sizeof *c); memset(c, 0, sizeof *c);
            c->contype = 'u'; c->name = default_con_name(s, t, cd->name, 'u');
            c->keys = xmalloc(sizeof(int)); c->keys[0] = col->attnum; c->nkeys = 1;
            c->nulls_nd = cd->nulls_nd;
            add_cons(t, c);
        }
        if (cd->checksrc) {
            Constraint *c = xmalloc(sizeof *c); memset(c, 0, sizeof *c);
            c->contype = 'c'; c->name = default_con_name(s, t, cd->name, 'c');
            c->checksrc = xstrdup(cd->checksrc);
            add_cons(t, c);
        }
        if (cd->fk_table) {
            Table *ref = tbl_find(db, NULL, cd->fk_table);
            if (!ref) ereport(s, "42P01", "relation \"%s\" does not exist", cd->fk_table);
#if PGVC_VN < 120
            if (ref->relkind == 'p')
                ereport(s, "0A000", "cannot reference partitioned table \"%s\"", ref->name);
#else
            if (ref->relkind == 'p') {
                bool haspk = false;
                for (int k = 0; k < ref->ncons; k++)
                    if (ref->cons[k]->contype == 'p' || ref->cons[k]->contype == 'u') haspk = true;
                if (!haspk)
                    ereport(s, "42830", "there is no unique constraint matching given keys "
                            "for referenced table \"%s\"", ref->name);
            }
#endif
            Constraint *c = xmalloc(sizeof *c); memset(c, 0, sizeof *c);
            c->contype = 'f'; c->name = default_con_name(s, t, cd->name, 'f');
            c->keys = xmalloc(sizeof(int)); c->keys[0] = col->attnum; c->nkeys = 1;
            c->fk_reltable = ref->oid;
            c->fkeys = xmalloc(sizeof(int));
            if (cd->nfk_cols) { int ci = col_index(ref, cd->fk_cols[0]);
                                if (ci < 0) ereport(s, "42703", "column \"%s\" referenced in foreign key constraint does not exist", cd->fk_cols[0]);
                                c->fkeys[0] = ci + 1; }
            else {
                c->fkeys[0] = 1;
                for (int k = 0; k < ref->ncons; k++)
                    if (ref->cons[k]->contype == 'p') c->fkeys[0] = ref->cons[k]->keys[0];
            }
            c->nfkeys = 1; c->fk_upd = cd->fk_upd; c->fk_del = cd->fk_del;
            add_cons(t, c);
        }
    }
    for (int i = 0; i < st->ncons; i++) {
        TblCons *tc = st->cons[i];
        Constraint *c = xmalloc(sizeof *c); memset(c, 0, sizeof *c);
        c->contype = tc->contype;
        c->without_overlaps = tc->without_overlaps;
        c->not_valid = tc->not_valid; c->not_enforced = tc->not_enforced;
#if PGVC_VN < 180
        if (tc->without_overlaps) ereport(s, "42601", "syntax error at or near \"WITHOUT\"");
#else
        if (tc->without_overlaps) check_without_overlaps(s, t, tc);
#endif
#if PGVC_VN < 110
        if (st->partstrat && (tc->contype == 'p' || tc->contype == 'u'))
            ereport_pos(s, "0A000", tc->pos, "%s constraints are not supported on "
                        "partitioned tables", tc->contype == 'p' ? "primary key" : "unique");
        if (st->partstrat && tc->contype == 'f')
            ereport_pos(s, "0A000", tc->pos,
                        "foreign key constraints are not supported on partitioned tables");
#endif
        if (tc->contype == 'c') {
            c->name = tc->name ? xstrdup(tc->name)
                               : default_con_name(s, t, check_first_col(t, tc->checksrc), 'c');
            c->checksrc = xstrdup(tc->checksrc);
            c->not_valid = tc->not_valid; c->not_enforced = tc->not_enforced;
            add_cons(t, c);
            continue;
        }
#if PGVC_VN >= 180
        if (tc->contype == 'n') {
            int ci = col_index(t, tc->cols[0]);
            if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                tc->cols[0], t->name);
            for (int k = 0; k < t->ncons; k++)
                if (t->cons[k]->contype == 'n' && t->cons[k]->nn_attnum == ci + 1) {
                    err_detail(s, "A not-null constraint named \"%s\" already exists "
                                  "for this column.", t->cons[k]->name);
                    ereport(s, "42710", "cannot create not-null constraint \"%s\" on "
                            "column \"%s\" of table \"%s\"",
                            tc->name ? tc->name : "", tc->cols[0], t->name);
                }
            c->nn_attnum = ci + 1;
            c->name = tc->name ? xstrdup(tc->name)
                               : default_con_name(s, t, tc->cols[0], 'n');
            c->not_valid = tc->not_valid;
            c->noinherit = tc->noinherit;
            if (!tc->not_valid) t->cols[ci].notnull = true;
            add_cons(t, c);
            continue;
        }
#endif
        c->keys = xmalloc((size_t)(tc->ncols ? tc->ncols : 1) * sizeof(int));
        for (int k = 0; k < tc->ncols; k++) {
            int ci = col_index(t, tc->cols[k]);
            if (ci < 0) ereport(s, "42703", "column \"%s\" named in key does not exist", tc->cols[k]);
            c->keys[k] = ci + 1;
            if (tc->contype == 'p') t->cols[ci].notnull = true;
        }
        c->nkeys = tc->ncols;
        { char *keynames = cols_join(t, c->keys, c->nkeys);
          c->name = tc->name ? xstrdup(tc->name)
                             : default_con_name(s, t, keynames, tc->contype);
          free(keynames); }
        if (tc->contype == 'f') {
            Table *ref = tbl_find(db, NULL, tc->fk_table);
            if (!ref && !strcmp(tc->fk_table, t->name)) ref = t;   /* self-reference */
            if (!ref) ereport(s, "42P01", "relation \"%s\" does not exist", tc->fk_table);
            c->fk_reltable = ref->oid;
            c->nfkeys = tc->nfk_cols ? tc->nfk_cols : 1;
            c->fkeys = xmalloc((size_t)c->nfkeys * sizeof(int));
            for (int k = 0; k < tc->nfk_cols; k++) {
                int ci = col_index(ref, tc->fk_cols[k]);
                if (ci < 0) ereport(s, "42703", "column \"%s\" referenced in foreign key constraint does not exist", tc->fk_cols[k]);
                c->fkeys[k] = ci + 1;
            }
            if (!tc->nfk_cols) {
                c->fkeys[0] = 1;
                for (int k = 0; k < ref->ncons; k++)
                    if (ref->cons[k]->contype == 'p') c->fkeys[0] = ref->cons[k]->keys[0];
            }
            c->fk_upd = tc->fk_upd; c->fk_del = tc->fk_del;
            c->fk_period = tc->fk_period;
#if PGVC_VN >= 180
            if (tc->fk_period) {
                if (tc->fk_del && tc->fk_del != 'a')
                    ereport(s, "0A000", "unsupported ON DELETE action for foreign key "
                                        "constraint using PERIOD");
                if (tc->fk_upd && tc->fk_upd != 'a')
                    ereport(s, "0A000", "unsupported ON UPDATE action for foreign key "
                                        "constraint using PERIOD");
                bool okref = false;
                for (int k = 0; k < ref->ncons; k++)
                    if ((ref->cons[k]->contype == 'p' || ref->cons[k]->contype == 'u') &&
                        ref->cons[k]->without_overlaps) okref = true;
                if (!okref)
                    ereport(s, "42830", "there is no unique constraint matching given keys "
                            "for referenced table \"%s\"", ref->name);
            }
#endif
        }
        add_cons(t, c);
        if (tc->contype == 'p' || tc->contype == 'u') {
            int *keys = xmalloc((size_t)c->nkeys * sizeof(int));
            memcpy(keys, c->keys, (size_t)c->nkeys * sizeof(int));
            int *incl = NULL;
            if (tc->nincl) {
                incl = xmalloc((size_t)tc->nincl * sizeof(int));
                for (int k = 0; k < tc->nincl; k++) {
                    int ci = col_index(t, tc->incl[k]);
                    if (ci < 0) ereport(s, "42703", "column \"%s\" does not exist", tc->incl[k]);
                    incl[k] = ci + 1;
                }
            }
            add_index(db, t, c->name, keys, c->nkeys, incl, tc->nincl, true, tc->contype=='p', NULL, NULL);
            db->indexes[db->nindexes-1]->conkind = tc->contype;
#if PGVC_VN >= 180
            if (tc->without_overlaps) db->indexes[db->nindexes-1]->am = 783;   /* gist */
#endif
        }
    }
    /* implicit indexes for column-level PK/UNIQUE */
    for (int i = 0; i < t->ncons; i++) {
        Constraint *c = t->cons[i];
        if ((c->contype != 'p' && c->contype != 'u') || idx_find(db, c->name)) continue;
        int *keys = xmalloc((size_t)c->nkeys * sizeof(int));
        memcpy(keys, c->keys, (size_t)c->nkeys * sizeof(int));
        add_index(db, t, c->name, keys, c->nkeys, NULL, 0, true, c->contype == 'p', NULL, NULL);
        db->indexes[db->nindexes-1]->conkind = c->contype;
#if PGVC_VN >= 180
        if (c->without_overlaps) db->indexes[db->nindexes-1]->am = 783;
#endif
    }
    /* NOT NULL constraints become catalog entries in PostgreSQL 18 */
    for (int i = 0; i < t->ncols; i++)
        if (t->cols[i].notnull) {
            bool covered = false;
            for (int k = 0; k < t->ncons; k++)
                if (t->cons[k]->contype == 'n' && t->cons[k]->nn_attnum == t->cols[i].attnum) covered = true;
            if (covered) continue;
            Constraint *c = xmalloc(sizeof *c); memset(c, 0, sizeof *c);
            c->contype = 'n'; c->nn_attnum = t->cols[i].attnum;
            c->name = default_con_name(s, t, t->cols[i].name, 'n');
            add_cons(t, c);
        }
    if (st->partstrat) {
        t->partstrat = st->partstrat;
        t->partkey = xmalloc((size_t)(st->npartkey ? st->npartkey : 1) * sizeof(int));
        t->partkeyexpr = xmalloc((size_t)(st->npartkey ? st->npartkey : 1) * sizeof(char *));
        for (int i = 0; i < st->npartkey; i++) {
            if (st->partkeyexpr && st->partkeyexpr[i]) {
                t->partkey[i] = 0;
                t->partkeyexpr[i] = xstrdup(st->partkeyexpr[i]);
                continue;
            }
            t->partkeyexpr[i] = NULL;
            int ci = col_index(t, st->partkey[i]);
            if (ci < 0) ereport(s, "42703", "column \"%s\" named in partition key does not exist",
                                st->partkey[i]);
            t->partkey[i] = ci + 1;
        }
        t->npartkey = st->npartkey;
#if PGVC_VN < 100
        ereport(s, "42601", "syntax error at or near \"PARTITION\"");
#endif
    }
    tbl_add(db, t);
    if (parent) {
        attach_partition(s, parent, t, st, false);
#if PGVC_VN >= 110
        clone_part_indexes(s, db, parent, t);
#endif
    }
    for (int i = 0; i < t->ncols; i++)
        if (t->cols[i].identity) {
            /* the identity's implicit sequence honours the options it was given */
            ColDef *cd = NULL;
            for (int k = 0; k < st->ncols; k++)
                if (st->cols[k]->name && !strcmp(st->cols[k]->name, t->cols[i].name))
                    cd = st->cols[k];
            int64 inc = (cd && cd->has_seq_inc) ? cd->seq_inc : 1;
            int64 minv = (cd && cd->has_seq_min) ? cd->seq_min : (inc > 0 ? 1 : INT64_MIN);
            int64 maxv = (cd && cd->has_seq_max) ? cd->seq_max : (inc > 0 ? INT64_MAX : -1);
            int64 start = (cd && cd->has_seq_start) ? cd->seq_start : (inc > 0 ? minv : maxv);
            Table *sq = make_sequence(s, schema, t->cols[i].identity_seq,
                                      start, inc, minv, maxv, cd ? cd->seq_cycle : false);
            sq->seq_owner = t->oid;
            if (cd && cd->seq_cache > 0) sq->seq_cache = cd->seq_cache;
        }
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "CREATE TABLE");
    return q;
}

/* The declared type of partition key column k of a partitioned table. */
static Oid partkey_type(Session *s, Table *t, int k)
{
    if (t->partkeyexpr && t->partkeyexpr[k]) {
        Expr *e = parse_expr_string(s, t->partkeyexpr[k]);
        RowSet rs; memset(&rs, 0, sizeof rs);
        rs.ncols = t->ncols;
        rs.cols = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(ColMeta));
        Datum *vals = row_new(t->ncols);
        for (int q = 0; q < t->ncols; q++) {
            rs.cols[q].rel = t->name; rs.cols[q].name = t->cols[q].name;
            rs.cols[q].type = t->cols[q].type; rs.cols[q].typmod = t->cols[q].typmod;
            rs.cols[q].hidden = false;
            vals[q] = d_null(t->cols[q].type);
        }
        ExecCtx c2; memset(&c2, 0, sizeof c2);
        c2.s = s; c2.rs = &rs; c2.row = vals;
        Datum d = eval(&c2, e);
        free(rs.cols); free(vals);
        return d.type ? d.type : TEXTOID;
    }
    return t->cols[t->partkey[k]-1].type;
}

/* Compare two range bounds of the same partitioned table.  A bound element is
 * -infinity, a value, or +infinity, and the first difference decides. */
static int bound_cmp(Session *s, const Datum *a, const signed char *ak, int na,
                     const Datum *b, const signed char *bk, int nb)
{
    int n = na < nb ? na : nb;
    for (int i = 0; i < n; i++) {
        int ai = ak ? ak[i] : 0, bi = bk ? bk[i] : 0;
        if (ai != bi) return ai < bi ? -1 : 1;
        if (ai) return 0;                    /* both the same infinity */
        int c = d_cmp(s, a[i], b[i]);
        if (c) return c;
    }
    return na == nb ? 0 : (na < nb ? -1 : 1);
}

static char *bound_text_of(Session *s, const Datum *a, const signed char *ak, int n)
{
    Buf b; buf_init(&b);
    for (int i = 0; i < n; i++) {
        if (i) buf_adds(&b, ", ");
        if (ak && ak[i] < 0) { buf_adds(&b, "MINVALUE"); continue; }
        if (ak && ak[i] > 0) { buf_adds(&b, "MAXVALUE"); continue; }
        char *o = out_func(s, a[i]);
        buf_adds(&b, a[i].isnull ? "null" : o);
        free(o);
    }
    return buf_steal(&b);
}

/* PostgreSQL validates a bound against the parent's strategy, against the
 * bounds already in use, and - when attaching - against the rows the table
 * already holds. */
static void attach_partition(Session *s, Table *parent, Table *child,
                             CreateTableStmt *bnd, bool attaching)
{
    Database *db = s->db;
    ExecCtx c; memset(&c, 0, sizeof c); c.s = s;
    if (attaching) {
        if (child == parent)
            ereport(s, "42P16", "cannot attach table \"%s\" to itself", child->name);
        if (child->parent)
            ereport(s, "42P16", "\"%s\" is already a partition", child->name);
        for (int i = 0; i < child->ncols; i++) {
            if (child->cols[i].dropped) continue;
            if (col_index(parent, child->cols[i].name) < 0) {
                err_detail(s, "The new partition may contain only the columns present in parent.");
                ereport(s, "42P10", "table \"%s\" contains column \"%s\" not found in parent \"%s\"",
                        child->name, child->cols[i].name, parent->name);
            }
        }
        for (int i = 0; i < parent->ncols; i++) {
            if (parent->cols[i].dropped) continue;
            int ci = col_index(child, parent->cols[i].name);
            if (ci < 0)
                ereport(s, "42P10", "child table is missing column \"%s\"", parent->cols[i].name);
            if (child->cols[ci].type != parent->cols[i].type)
                ereport(s, "42804", "child table \"%s\" has different type for column \"%s\"",
                        child->name, parent->cols[i].name);
        }
    }
    memset(&child->pbound, 0, sizeof child->pbound);
    child->pbound.is_default = bnd->bound_default;
    if (bnd->bound_default) {
#if PGVC_VN < 110
        ereport(s, "42601", "syntax error at or near \"DEFAULT\"");
#else
        if (parent->partstrat == 'h')
            ereport(s, "0A000", "a hash-partitioned table may not have a default partition");
        for (int i = 0; i < db->ntables; i++)
            if (db->tables[i]->parent == parent->oid && db->tables[i] != child &&
                db->tables[i]->pbound.is_default)
                ereport_pos(s, "42P17", PGVC_VN >= 140 ? bnd->bound_pos : 0,
                            "partition \"%s\" conflicts with existing default partition \"%s\"",
                            child->name, db->tables[i]->name);

#endif
    } else if (parent->partstrat == 'r') {
        if (!bnd->nbound_from)
            ereport_pos(s, "42601", bnd->bound_pos,
                        "invalid bound specification for a range partition");
        if (bnd->nbound_from != parent->npartkey || bnd->nbound_to != parent->npartkey)
            ereport_pos(s, "42601", bnd->bound_pos,
                        "FROM must specify exactly one value per partitioning column");
        child->pbound.nfrom = bnd->nbound_from;
        child->pbound.nto = bnd->nbound_to;
        child->pbound.from = xmalloc((size_t)bnd->nbound_from * sizeof(Datum));
        child->pbound.to = xmalloc((size_t)bnd->nbound_to * sizeof(Datum));
        child->pbound.fromkind = xmalloc((size_t)bnd->nbound_from);
        child->pbound.tokind = xmalloc((size_t)bnd->nbound_to);
        bool inf = false;
        for (int i = 0; i < bnd->nbound_from; i++) {
            Expr *e = bnd->bound_from[0][i];
            Oid kt = partkey_type(s, parent, i);
            signed char k = 0;
            if (e->alias && !strcmp(e->alias, "MINVALUE")) k = -1;
            else if (e->alias && !strcmp(e->alias, "MAXVALUE")) k = 1;
            if (inf && !k) k = child->pbound.fromkind[i-1];
            if (k) inf = true;
            child->pbound.fromkind[i] = k;
            child->pbound.from[i] = k ? d_null(kt) : coerce_to(s, eval(&c, e), kt, -1, 1);
        }
        inf = false;
        for (int i = 0; i < bnd->nbound_to; i++) {
            Expr *e = bnd->bound_to[0][i];
            Oid kt = partkey_type(s, parent, i);
            signed char k = 0;
            if (e->alias && !strcmp(e->alias, "MINVALUE")) k = -1;
            else if (e->alias && !strcmp(e->alias, "MAXVALUE")) k = 1;
            if (inf && !k) k = child->pbound.tokind[i-1];
            if (k) inf = true;
            child->pbound.tokind[i] = k;
            child->pbound.to[i] = k ? d_null(kt) : coerce_to(s, eval(&c, e), kt, -1, 1);
        }
        if (bound_cmp(s, child->pbound.from, child->pbound.fromkind, child->pbound.nfrom,
                      child->pbound.to, child->pbound.tokind, child->pbound.nto) >= 0) {
            char *lo = bound_text_of(s, child->pbound.from, child->pbound.fromkind, child->pbound.nfrom);
            char *hi = bound_text_of(s, child->pbound.to, child->pbound.tokind, child->pbound.nto);
            err_detail(s, "Specified lower bound (%s) is greater than or equal to upper bound (%s).",
                       lo, hi);
            ereport_pos(s, "42P17",
                        PGVC_VN >= 140 && bnd->nbound_from ? bnd->bound_from[0][0]->pos : 0,
                        "empty range bound specified for partition \"%s\"", child->name);
        }
        for (int i = 0; i < db->ntables; i++) {
            Table *p = db->tables[i];
            if (p->parent != parent->oid || p == child || p->pbound.is_default) continue;
            if (bound_cmp(s, child->pbound.from, child->pbound.fromkind, child->pbound.nfrom,
                          p->pbound.to, p->pbound.tokind, p->pbound.nto) < 0 &&
                bound_cmp(s, p->pbound.from, p->pbound.fromkind, p->pbound.nfrom,
                          child->pbound.to, child->pbound.tokind, child->pbound.nto) < 0)
                ereport_pos(s, "42P17",
                            PGVC_VN >= 140 && bnd->nbound_from ? bnd->bound_from[0][0]->pos : 0,
                            "partition \"%s\" would overlap partition \"%s\"",
                            child->name, p->name);
        }
    } else if (parent->partstrat == 'l') {
        if (!bnd->nbound_in)
            ereport_pos(s, "42601", bnd->bound_pos,
                        "invalid bound specification for a list partition");
        child->pbound.nin = bnd->nbound_in;
        child->pbound.in = xmalloc((size_t)bnd->nbound_in * sizeof(Datum));
        Oid kt = partkey_type(s, parent, 0);
        for (int i = 0; i < bnd->nbound_in; i++) {
            Datum v = eval(&c, bnd->bound_in[i]);
            child->pbound.in[i] = v.isnull ? d_null(kt) : coerce_to(s, v, kt, -1, 1);
        }
        for (int i = 0; i < db->ntables; i++) {
            Table *p = db->tables[i];
            if (p->parent != parent->oid || p == child || p->pbound.is_default) continue;
            for (int a = 0; a < child->pbound.nin; a++)
                for (int b2 = 0; b2 < p->pbound.nin; b2++) {
                    Datum x = child->pbound.in[a], y = p->pbound.in[b2];
                    if (x.isnull != y.isnull) continue;
                    if (!x.isnull && d_cmp(s, x, y) != 0) continue;
                    ereport_pos(s, "42P17",
                                PGVC_VN >= 140 ? bnd->bound_in[a]->pos : 0,
                                "partition \"%s\" would overlap partition \"%s\"",
                                child->name, p->name);
                }
        }
    } else if (parent->partstrat == 'h') {
#if PGVC_VN < 110
        ereport(s, "42601", "syntax error at or near \"WITH\"");
#else
        if (!bnd->nbound_from && !bnd->nbound_in && !bnd->bound_modulus &&
            !bnd->bound_remainder && !bnd->bound_text)
            ereport_pos(s, "42601", bnd->bound_pos,
                        "invalid bound specification for a hash partition");
        if (bnd->nbound_from || bnd->nbound_in ||
            (!bnd->bound_modulus && !bnd->has_modulus))
            ereport_pos(s, "42601", bnd->bound_pos,
                        "invalid bound specification for a hash partition");
        if (bnd->bound_modulus <= 0)
            ereport(s, "22023", "modulus for hash partition must be an integer value greater than zero");
        if (bnd->bound_remainder < 0 || bnd->bound_remainder >= bnd->bound_modulus)
            ereport(s, "22023", "remainder for hash partition must be less than modulus");
        for (int i = 0; i < db->ntables; i++) {
            Table *p = db->tables[i];
            if (p->parent != parent->oid || p == child || !p->pbound.is_hash) continue;
            int m1 = bnd->bound_modulus, m2 = p->pbound.modulus;
            int lo = m1 < m2 ? m1 : m2, hi2 = m1 < m2 ? m2 : m1;
            if (lo > 0 && hi2 % lo != 0) {
#if PGVC_VN >= 140
                /* PostgreSQL 14 names the partition whose modulus it clashes with */
                err_detail(s, "The new modulus %d is not a factor of %d, the modulus of "
                              "existing partition \"%s\".", m1, m2, p->name);
#endif
                ereport(s, "42P17", "every hash partition modulus must be a factor of "
                                    "the next larger modulus");
            }
        }
        child->pbound.is_hash = true;
        child->pbound.modulus = bnd->bound_modulus;
        child->pbound.remainder = bnd->bound_remainder;
        for (int i = 0; i < db->ntables; i++) {
            Table *p = db->tables[i];
            if (p->parent != parent->oid || p == child || !p->pbound.is_hash) continue;
            int m1 = child->pbound.modulus, m2 = p->pbound.modulus;
#if PGVC_VN >= 140
            if (m1 != m2 && (m1 % m2) && (m2 % m1)) {
                err_detail(s, "The new modulus %d is not a factor of %d, the modulus of "
                              "existing partition \"%s\".", m1, m2, p->name);
                ereport(s, "42P17", "every hash partition modulus must be a factor of the "
                                    "next larger modulus");
            }
#endif
            int g = m1 < m2 ? m1 : m2;
            if (g > 0 && (child->pbound.remainder % g) == (p->pbound.remainder % g))
                ereport_pos(s, "42P17", PGVC_VN >= 140 ? bnd->bound_pos : 0,
                            "partition \"%s\" would overlap partition \"%s\"",
                            child->name, p->name);
        }
#endif
    }
#if PGVC_VN >= 110
    if (!bnd->bound_default) {
        /* rows already in the default partition must not fall into the new one */
        Table *deflt = NULL;
        for (int i = 0; i < db->ntables; i++)
            if (db->tables[i]->parent == parent->oid && db->tables[i] != child &&
                db->tables[i]->pbound.is_default) deflt = db->tables[i];
        if (deflt)
            for (int r = 0; r < deflt->nrows; r++) {
                if (!row_visible(s, deflt->rows[r])) continue;
                Datum *pv = row_new(parent->ncols);
                for (int i = 0; i < parent->ncols; i++) {
                    int src = col_index(deflt, parent->cols[i].name);
                    pv[i] = (src >= 0 && src < deflt->rows[r]->n) ? deflt->rows[r]->vals[src]
                                                                  : d_null(parent->cols[i].type);
                }
                bool clash = partition_accepts(s, parent, child, pv);
                free(pv);
                if (clash)
                    ereport(s, "23514", "updated partition constraint for default partition "
                            "\"%s\" would be violated by some row", deflt->name);
            }
    }
#endif
    if (attaching) {
        /* every existing row must satisfy the bound we are about to impose */
        Table **parts = NULL; int nparts = 0;
        collect_partitions(db, child, &parts, &nparts);
        for (int p = 0; p < nparts; p++) {
            Table *pt = parts[p];
            if (pt->relkind == 'p') continue;
            for (int r = 0; r < pt->nrows; r++) {
                if (!row_visible(s, pt->rows[r])) continue;
                Datum *pv = row_new(parent->ncols);
                for (int i = 0; i < parent->ncols; i++) {
                    int src = col_index(pt, parent->cols[i].name);
                    pv[i] = (src >= 0 && src < pt->rows[r]->n) ? pt->rows[r]->vals[src]
                                                              : d_null(parent->cols[i].type);
                }
                bool ok = child->pbound.is_default ? true
                                                   : partition_accepts(s, parent, child, pv);
                free(pv);
                if (!ok) { free(parts);
#if PGVC_VN >= 130
                    ereport(s, "23514", "partition constraint of relation \"%s\" is violated "
                            "by some row", child->name);
#else
                    ereport(s, "23514", "partition constraint is violated by some row");
#endif
                }
            }
        }
        free(parts);
    }
    child->parent = parent->oid;
    if (bnd->bound_default) child->partbound = xstrdup("DEFAULT");
    else if (parent->partstrat == 'h') {
        /* PostgreSQL deparses a hash bound in lower case */
        Buf hb; buf_init(&hb);
        buf_printf(&hb, "FOR VALUES WITH (modulus %d, remainder %d)",
                   child->pbound.modulus, child->pbound.remainder);
        child->partbound = buf_steal(&hb);
    } else child->partbound = bnd->bound_text ? xstrdup(bnd->bound_text) : NULL;
#if PGVC_VN >= 110
    /* the parent's row triggers are cloned onto a partition as it joins */
    {
        int base = db->ntriggers;
        for (int i = 0; i < base; i++) {
            Trigger *tg = db->triggers[i];
            if (tg->table != parent->oid || !tg->forrow || tg->isclone) continue;
            bool have = false;
            for (int k = 0; k < db->ntriggers; k++)
                if (db->triggers[k]->table == child->oid &&
                    !strcmp(db->triggers[k]->name, tg->name)) have = true;
            if (have) continue;
            Trigger *cl = xmalloc(sizeof *cl);
            *cl = *tg;
            cl->oid = new_oid();
            cl->name = xstrdup(tg->name);
            cl->table = child->oid;
            cl->funcname = xstrdup(tg->funcname);
            cl->whensrc = tg->whensrc ? xstrdup(tg->whensrc) : NULL;
            cl->oldtable = tg->oldtable ? xstrdup(tg->oldtable) : NULL;
            cl->newtable = tg->newtable ? xstrdup(tg->newtable) : NULL;
            cl->isclone = true;
            cl->parent_trig = tg->oid;
            db->triggers = xrealloc(db->triggers,
                                    (size_t)(db->ntriggers + 1) * sizeof(Trigger *));
            db->triggers[db->ntriggers++] = cl;
        }
    }
#endif
}

static char *idx_opt_check(Session *s, Oid am, const char *name, const char *val, int pos);
static void idx_set_opt(Index *ix, const char *name, const char *val);

static QueryResult *exec_alter_table(Session *s, Node *n)
{
    AlterTableStmt *st = n->x;
    Database *db = s->db;
    if (st->objtype && strcmp(st->objtype, "index") && st->ncmds &&
        (st->cmds[0]->kind == 31 || st->cmds[0]->kind == 32)) {
        Table *rt = tbl_find(db, st->schema, st->name);
        if (!rt) {
            if (st->if_exists) {
                notice(s, "00000", "relation \"%s\" does not exist, skipping", st->name);
                QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "ALTER"); return q;
            }
            ereport(s, "42P01", "relation \"%s\" does not exist", st->name);
        }
        for (int i = 0; i < st->ncmds; i++) {
            AlterTableCmd *c = st->cmds[i];
            if (c->kind == 31) {
                char **nv = xmalloc((size_t)(c->nopts ? c->nopts : 1) * sizeof(char *));
                for (int k = 0; k < c->nopts; k++)
                    nv[k] = rel_opt_check(s, rt->relkind, c->optname[k], c->optval[k]);
                for (int k = 0; k < c->nopts; k++) {
                    rel_set_opt(rt, c->optname[k], nv[k]);
                    if (!strcmp(c->optname[k], "security_invoker"))
                        rt->security_invoker = !strcmp(nv[k], "true");
                    free(nv[k]);
                }
                free(nv);
            } else {
                for (int k = 0; k < c->nopts; k++)
                    for (int j = 0; j < rt->nopts; j++)
                        if (!strcmp(rt->optname[j], c->optname[k])) {
                            free(rt->optname[j]); free(rt->optval[j]);
                            memmove(&rt->optname[j], &rt->optname[j+1],
                                    (size_t)(rt->nopts-j-1)*sizeof(char*));
                            memmove(&rt->optval[j], &rt->optval[j+1],
                                    (size_t)(rt->nopts-j-1)*sizeof(char*));
                            rt->nopts--;
                            if (!strcmp(c->optname[k], "security_invoker"))
                                rt->security_invoker = false;
                            break;
                        }
            }
        }
        QueryResult *q = qr_new();
        snprintf(q->tag, sizeof q->tag, "ALTER %s",
                 rt->relkind == 'v' ? "VIEW" : rt->relkind == 'm' ? "MATERIALIZED VIEW" : "TABLE");
        return q;
    }
    if (st->objtype && !strcmp(st->objtype, "index") && st->ncmds &&
        (st->cmds[0]->kind == 31 || st->cmds[0]->kind == 32)) {
        Index *ix = idx_find(db, st->name);
        if (!ix) {
            if (st->if_exists) {
                notice(s, "00000", "relation \"%s\" does not exist, skipping", st->name);
                QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "ALTER INDEX"); return q;
            }
            ereport(s, "42P01", "relation \"%s\" does not exist", st->name);
        }
        for (int i = 0; i < st->ncmds; i++) {
            AlterTableCmd *c = st->cmds[i];
            if (c->kind == 31) {
                char **nv = xmalloc((size_t)(c->nopts ? c->nopts : 1) * sizeof(char *));
                for (int k = 0; k < c->nopts; k++)
                    nv[k] = idx_opt_check(s, ix->am, c->optname[k], c->optval[k], c->optpos[k]);
                for (int k = 0; k < c->nopts; k++) {
                    idx_set_opt(ix, c->optname[k], nv[k]);
                    free(nv[k]);
                }
                free(nv);
            } else if (c->kind == 32) {
                for (int k = 0; k < c->nopts; k++)
                    for (int j = 0; j < ix->nopts; j++)
                        if (!strcmp(ix->optname[j], c->optname[k])) {
                            free(ix->optname[j]); free(ix->optval[j]);
                            memmove(&ix->optname[j], &ix->optname[j+1],
                                    (size_t)(ix->nopts-j-1)*sizeof(char*));
                            memmove(&ix->optval[j], &ix->optval[j+1],
                                    (size_t)(ix->nopts-j-1)*sizeof(char*));
                            ix->nopts--;
                            break;
                        }
            }
        }
        QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "ALTER INDEX"); return q;
    }
    Table *t = tbl_find(db, st->schema, st->name);
    if (!t && st->objtype && !strcmp(st->objtype, "index")) {
        /* ALTER INDEX touches an index relation, not a table */
        Index *ix = idx_find(db, st->name);
        if (!ix) {
            if (st->if_exists) { notice(s, "00000", "relation \"%s\" does not exist, skipping", st->name);
                                 QueryResult *q = qr_new();
                                 snprintf(q->tag, sizeof q->tag, "ALTER INDEX"); return q; }
            ereport(s, "42P01", "relation \"%s\" does not exist", st->name);
        }
        for (int i = 0; i < st->ncmds; i++) {
            AlterTableCmd *c = st->cmds[i];
            if (c->kind == 24) {
                Index *ch = idx_find(db, c->name);
                if (!ch) ereport(s, "42704", "index \"%s\" does not exist", c->name);
                ch->parent_index = ix->oid;
                ix->valid = true;
            } else if (c->kind == 12) { free(ix->name); ix->name = xstrdup(c->name); }
            else if (c->kind == 25) {
                if (c->seq_arg < 1 || c->seq_arg > ix->nkeys + ix->nincl)
                    ereport(s, "42703", "column number %lld of relation \"%s\" does not exist",
                            (long long)c->seq_arg, ix->name);
                if (!ix->stattarget)
                    ix->stattarget = xmalloc((size_t)(ix->nkeys + ix->nincl) * sizeof(int));
                for (int k = 0; k < ix->nkeys + ix->nincl; k++)
                    if (!ix->statset) ix->stattarget[k] = -1;
                ix->statset = true;
                ix->stattarget[c->seq_arg - 1] = (int)c->seq_what;
            }
        }
        QueryResult *q = qr_new();
        snprintf(q->tag, sizeof q->tag, "ALTER INDEX");
        return q;
    }
    if (!t) {
        if (st->if_exists) {
            notice(s, "00000", "relation \"%s\" does not exist, skipping", st->name);
            QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "ALTER TABLE"); return q;
        }
        ereport(s, "42P01", "relation \"%s\" does not exist", st->name);
    }
    catalog_note_table_state(db, t);
    for (int i = 0; i < st->ncmds; i++) {
        AlterTableCmd *c = st->cmds[i];
        switch (c->kind) {
        case 1: {
            if (col_index(t, c->col->name) >= 0) {
                if (c->missing_ok) { notice(s, "00000", "column \"%s\" of relation \"%s\" already exists, skipping",
                                            c->col->name, t->name); break; }
                ereport(s, "42701", "column \"%s\" of relation \"%s\" already exists", c->col->name, t->name);
            }
            add_col(t, c->col->name, c->col->type, c->col->typmod);
            Column *col = &t->cols[t->ncols-1];
            col->notnull = c->col->notnull;
            col->defexpr = c->col->defexpr ? xstrdup(c->col->defexpr) : NULL;
            if (c->col->generated) { col->generated = c->col->generated == 'd' ?
                                     (PGVC_VN >= 180 ? 'v' : 's') : c->col->generated;
                                     col->genexpr = xstrdup(c->col->genexpr); }
            for (int r = 0; r < t->nrows; r++) {
                Row *row = t->rows[r];
                row->vals = xrealloc(row->vals, (size_t)t->ncols * sizeof(Datum));
                row->vals[t->ncols-1] = d_null(col->type);
                row->n = t->ncols;
                if (col->defexpr) row->vals[t->ncols-1] = eval_default(s, t, col, row->vals);
                if (col->generated) apply_generated(s, t, row->vals);
                if (col->notnull && row->vals[t->ncols-1].isnull) {
#if PGVC_VN >= 130
                    ereport(s, "23502", "column \"%s\" of relation \"%s\" contains null values",
                            col->name, t->name);
#else
                    ereport(s, "23502", "column \"%s\" contains null values", col->name);
#endif
                }
            }
            /* PostgreSQL 11 stores a constant default for the existing rows
             * instead of rewriting the table. */
            if (col->defexpr && PGVC_VN >= 110 && !expr_is_volatile(s, col->defexpr)) {
                Datum d = eval_default(s, t, col, NULL);
                if (!d.isnull) {
                    char *tx = out_func(s, d);
                    Buf mb; buf_init(&mb);
                    buf_printf(&mb, "{%s}", tx);
                    col->missingval = buf_steal(&mb);
                    free(tx);
                }
            }
            if (c->col->primary || c->col->unique) {
                Constraint *cc = xmalloc(sizeof *cc); memset(cc, 0, sizeof *cc);
                cc->contype = c->col->primary ? 'p' : 'u';
                cc->name = default_con_name(s, t, c->col->primary ? NULL : c->col->name, cc->contype);
                cc->keys = xmalloc(sizeof(int)); cc->keys[0] = col->attnum; cc->nkeys = 1;
                cc->nulls_nd = c->col->nulls_nd;
                add_cons(t, cc);
                int *keys = xmalloc(sizeof(int)); keys[0] = col->attnum;
                add_index(db, t, cc->name, keys, 1, NULL, 0, true, cc->contype=='p', NULL, NULL);
                db->indexes[db->nindexes-1]->conkind = cc->contype;
                db->indexes[db->nindexes-1]->nulls_nd = cc->nulls_nd;
            }
            break;
        }
        case 2: {
            int ci = col_index(t, c->name);
            if (ci < 0) {
                if (c->if_exists) { notice(s, "00000", "column \"%s\" of relation \"%s\" does not exist, skipping",
                                           c->name, t->name); break; }
                ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist", c->name, t->name);
            }
            for (int k = 0; k < t->npartkey; k++)
                if (t->partkey[k] == ci + 1)
                    ereport(s, "0A000", "cannot drop column \"%s\" because it is part of "
                            "the partition key of relation \"%s\"", c->name, t->name);
            if (t->parent) {
                Table *par = tbl_find_oid(db, t->parent);
                if (par && col_index(par, c->name) >= 0)
                    ereport(s, "0A000", "cannot drop inherited column \"%s\"", c->name);
            }
            t->cols[ci].dropped = true;
            for (int k = t->ncons - 1; k >= 0; k--) {
                Constraint *cc = t->cons[k];
                bool hit = (cc->contype == 'n' && cc->nn_attnum == ci + 1);
                for (int q = 0; q < cc->nkeys; q++) if (cc->keys[q] == ci + 1) hit = true;
                if (hit) { memmove(&t->cons[k], &t->cons[k+1], (size_t)(t->ncons-k-1)*sizeof(Constraint*)); t->ncons--; }
            }
#if PGVC_VN >= 150
            /* a generated column that reads this one depends on it */
            for (int k = 0; k < t->ncols; k++) {
                if (k == ci || !t->cols[k].generated || !t->cols[k].genexpr) continue;
                Expr *ge = parse_expression_text(s, t->cols[k].genexpr);
                Expr **cols = NULL; int ncols2 = 0;
                if (ge) collect_bare_cols(ge, &cols, &ncols2);
                bool dep = false;
                for (int q = 0; q < ncols2; q++)
                    if (cols[q]->colname && !strcmp(cols[q]->colname, t->cols[ci].name)) dep = true;
                free(cols);
                if (dep && !c->cascade) {
                    t->cols[ci].dropped = false;
                    err_detail(s, "column %s of table %s depends on column %s of table %s",
                               t->cols[k].name, t->name, t->cols[ci].name, t->name);
                    err_hint(s, "Use DROP ... CASCADE to drop the dependent objects too.");
                    ereport(s, "2BP01", "cannot drop column %s of table %s because other "
                            "objects depend on it", t->cols[ci].name, t->name);
                }
            }
#endif
#if PGVC_VN >= 150
            for (int k = 0; k < db->npubs && !c->cascade; k++) {
                Publication *pu = db->pubs[k];
                for (int q = 0; q < pu->ntables; q++) {
                    if (pu->tables[q] != t->oid || !pu->ncollist || pu->ncollist[q] <= 0) continue;
                    for (int w = 0; w < pu->ncollist[q]; w++)
                        if (!strcmp(pu->collist[q][w], t->cols[ci].name)) {
                            t->cols[ci].dropped = false;
                            err_detail(s, "publication of table %s in publication %s depends "
                                       "on column %s of table %s",
                                       t->name, pu->name, t->cols[ci].name, t->name);
                            err_hint(s, "Use DROP ... CASCADE to drop the dependent objects too.");
                            ereport(s, "2BP01", "cannot drop column %s of table %s because "
                                    "other objects depend on it", t->cols[ci].name, t->name);
                        }
                }
            }
#endif
            /* an extended statistics object that named this column goes too */
            for (int k = db->nstats - 1; k >= 0; k--) {
                ExtStat *e = db->stats[k];
                if (e->table != t->oid) continue;
                bool hit = false;
                for (int q = 0; q < e->nkeys; q++) if (e->keys[q] == ci + 1) hit = true;
                if (!hit) continue;
                memmove(&db->stats[k], &db->stats[k+1],
                        (size_t)(db->nstats-k-1)*sizeof(ExtStat*));
                db->nstats--;
            }
            break;
        }
        case 3: {
            int ci = col_index(t, c->name);
            if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist", c->name, t->name);
#if PGVC_VN >= 120
            /* a generated column pins the type of every column it reads */
            for (int k = 0; k < t->ncols; k++) {
                if (!t->cols[k].generated || !t->cols[k].genexpr || k == ci) continue;
                Expr *ge = parse_expr_string(s, t->cols[k].genexpr);
                if (expr_names_column(ge, t->cols[ci].name)) {
                    err_detail(s, "Column \"%s\" is used by generated column \"%s\".",
                               t->cols[ci].name, t->cols[k].name);
                    ereport(s, "0A000", "cannot alter type of a column used by a generated column");
                }
            }
#endif
            for (int r = 0; r < t->nrows; r++)
                t->rows[r]->vals[ci] = coerce_to(s, t->rows[r]->vals[ci], c->type, c->typmod, 2);
            t->cols[ci].type = c->type; t->cols[ci].typmod = c->typmod;
            /* a type change rewrites the table, so the fast default goes away */
            t->cols[ci].missingval = NULL;
            break;
        }
        case 4: { int ci = col_index(t, c->name);
                  if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist", c->name, t->name);
                  if (t->cols[ci].generated) {
#if PGVC_VN >= 170
                      /* 17 gave generated columns a SET EXPRESSION of their own
                       * and points SET DEFAULT at it. */
                      err_hint(s, "Use ALTER TABLE ... ALTER COLUMN ... SET EXPRESSION instead.");
#endif
                      ereport(s, "42P16", "column \"%s\" of relation \"%s\" is a generated column",
                              c->name, t->name);
                  }
                  t->cols[ci].defexpr = xstrdup(c->name2); break; }
#if PGVC_VN >= 170
        case 26: {                        /* ALTER COLUMN ... SET EXPRESSION AS */
            int ci = col_index(t, c->name);
            if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                c->name, t->name);
            if (!t->cols[ci].generated)
                ereport(s, "42P16", "column \"%s\" of relation \"%s\" is not a generated column",
                        c->name, t->name);
            if (expr_is_volatile(s, c->name2))
                ereport(s, "42P17", "generation expression is not immutable");
            char *old = t->cols[ci].genexpr;
            t->cols[ci].genexpr = xstrdup(c->name2);
            /* the new expression must produce the column's type */
            { Datum *probe = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(Datum));
              for (int k = 0; k < t->ncols; k++) probe[k] = d_null(t->cols[k].type);
              apply_generated(s, t, probe);
              free(probe); }
            /* a stored column is recomputed for every existing row */
            for (int r = 0; r < t->nrows; r++) {
                Row *row = t->rows[r];
                if (!row_visible(s, row)) continue;
                Datum *nv = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(Datum));
                memcpy(nv, row->vals, (size_t)t->ncols * sizeof(Datum));
                apply_generated(s, t, nv);
                row->vals = nv;
            }
            if (t->cols[ci].generated != 'v') { }
            free(old);
            break;
        }
#endif
        case 5: { int ci = col_index(t, c->name);
                  if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist", c->name, t->name);
                  if (t->cols[ci].generated) {
#if PGVC_VN >= 130
                      err_hint(s, "Use ALTER TABLE ... ALTER COLUMN ... DROP EXPRESSION instead.");
#endif
                      ereport(s, "42P16", "column \"%s\" of relation \"%s\" is a generated column",
                              c->name, t->name);
                  }
                  t->cols[ci].defexpr = NULL; break; }
        case 6: { int ci = col_index(t, c->name);
                  if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist", c->name, t->name);
                  for (int r = 0; r < t->nrows; r++)
                      if (row_visible(s, t->rows[r]) && t->rows[r]->vals[ci].isnull) {
#if PGVC_VN >= 130
                          ereport(s, "23502", "column \"%s\" of relation \"%s\" contains null values",
                                  c->name, t->name);
#else
                          ereport(s, "23502", "column \"%s\" contains null values", c->name);
#endif
                      }
                  t->cols[ci].notnull = true;
#if PGVC_VN >= 180
                  { Constraint *cc = xmalloc(sizeof *cc); memset(cc, 0, sizeof *cc);
                    cc->contype = 'n'; cc->nn_attnum = ci + 1;
                    cc->name = default_con_name(s, t, c->name, 'n'); add_cons(t, cc); }
#endif
                  break; }
        case 7: { int ci = col_index(t, c->name);
                  if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist", c->name, t->name);
                  t->cols[ci].notnull = false;
                  for (int k = t->ncons - 1; k >= 0; k--)
                      if (t->cons[k]->contype == 'n' && t->cons[k]->nn_attnum == ci + 1) {
                          memmove(&t->cons[k], &t->cons[k+1], (size_t)(t->ncons-k-1)*sizeof(Constraint*)); t->ncons--; }
                  break; }
        case 8: {
            TblCons *tc = c->cons;
            Constraint *cc = xmalloc(sizeof *cc); memset(cc, 0, sizeof *cc);
            cc->contype = tc->contype;
            cc->without_overlaps = tc->without_overlaps;
            cc->not_valid = tc->not_valid; cc->not_enforced = tc->not_enforced;
#if PGVC_VN < 110
            if (t->relkind == 'p' && (tc->contype == 'p' || tc->contype == 'u'))
                ereport_pos(s, "0A000", tc->pos, "%s constraints are not supported on "
                            "partitioned tables", tc->contype == 'p' ? "primary key" : "unique");
            if (t->relkind == 'p' && tc->contype == 'f')
                ereport_pos(s, "0A000", tc->pos,
                            "foreign key constraints are not supported on partitioned tables");
#endif
#if PGVC_VN < 180
            if (tc->without_overlaps) ereport(s, "42601", "syntax error at or near \"WITHOUT\"");
#else
            if (tc->without_overlaps) check_without_overlaps(s, t, tc);
            if (tc->contype == 'p' || tc->contype == 'u')
                for (int k = 0; k < tc->ncols; k++) {
                    int vci = col_index(t, tc->cols[k]);
                    if (vci >= 0 && t->cols[vci].generated == 'v')
                        ereport(s, "0A000", "%s on virtual generated columns are not supported",
                                tc->contype == 'p' ? "primary keys" : "unique constraints");
                }
#endif
#if PGVC_VN >= 180
            if (tc->contype == 'n') {
                int ci = col_index(t, tc->cols[0]);
                if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                    tc->cols[0], t->name);
                for (int k = 0; k < t->ncons; k++)
                    if (t->cons[k]->contype == 'n' && t->cons[k]->nn_attnum == ci + 1) {
                        err_detail(s, "A not-null constraint named \"%s\" already exists "
                                      "for this column.", t->cons[k]->name);
                        ereport(s, "42710", "cannot create not-null constraint \"%s\" on "
                                "column \"%s\" of table \"%s\"",
                                tc->name ? tc->name : "", tc->cols[0], t->name);
                    }
                cc->nn_attnum = ci + 1;
                cc->name = tc->name ? xstrdup(tc->name)
                                    : default_con_name(s, t, tc->cols[0], 'n');
                cc->not_valid = tc->not_valid;
                cc->noinherit = tc->noinherit;
                if (!tc->not_valid)
                    for (int r = 0; r < t->nrows; r++)
                        if (row_visible(s, t->rows[r]) && t->rows[r]->vals[ci].isnull)
                            ereport(s, "23502", "column \"%s\" of relation \"%s\" contains null values",
                                    tc->cols[0], t->name);
                t->cols[ci].notnull = true;
                add_cons(t, cc);
                break;
            }
#endif
            if (tc->contype == 'c') {
                cc->name = tc->name ? xstrdup(tc->name)
                                    : default_con_name(s, t, check_first_col(t, tc->checksrc), 'c');
                cc->checksrc = xstrdup(tc->checksrc);
                cc->not_valid = tc->not_valid; cc->not_enforced = tc->not_enforced;
                add_cons(t, cc);
                for (int r = 0; r < t->nrows; r++)
                    if (row_visible(s, t->rows[r])) check_constraints(s, t, t->rows[r]->vals, t->rows[r]);
                break;
            }
            cc->keys = xmalloc((size_t)(tc->ncols?tc->ncols:1)*sizeof(int));
            for (int k = 0; k < tc->ncols; k++) {
                int ci = col_index(t, tc->cols[k]);
                if (ci < 0) ereport(s, "42703", "column \"%s\" named in key does not exist", tc->cols[k]);
                cc->keys[k] = ci + 1;
                if (tc->contype == 'p') t->cols[ci].notnull = true;
            }
            cc->nkeys = tc->ncols;
            cc->nulls_nd = tc->nulls_nd;
            { char *cj = cols_join(t, cc->keys, cc->nkeys);
              cc->name = tc->name ? xstrdup(tc->name)
                                  : default_con_name(s, t, tc->ncols ? cj : NULL, tc->contype);
              free(cj); }
#if PGVC_VN >= 110
            if (t->relkind == 'p' && (tc->contype == 'p' || tc->contype == 'u'))
                for (int q2 = 0; q2 < t->npartkey; q2++) {
                    bool have = false;
                    for (int z = 0; z < cc->nkeys; z++)
                        if (cc->keys[z] == t->partkey[q2]) have = true;
                    if (!have) {
                        err_detail(s, "%s constraint on table \"%s\" lacks column \"%s\" which "
                                      "is part of the partition key.",
                                   tc->contype == 'p' ? "PRIMARY KEY" : "UNIQUE",
                                   t->name, t->cols[t->partkey[q2]-1].name);
                        ereport(s, "0A000", "unique constraint on partitioned table must include "
                                "all partitioning columns");
                    }
                }
#endif
            if (tc->contype == 'f') {
                Table *ref = tbl_find(db, NULL, tc->fk_table);
                if (!ref) ereport(s, "42P01", "relation \"%s\" does not exist", tc->fk_table);
#if PGVC_VN < 120
                if (ref->relkind == 'p')
                    ereport(s, "0A000", "cannot reference partitioned table \"%s\"", ref->name);
#endif
                cc->fk_reltable = ref->oid;
                cc->nfkeys = tc->nfk_cols ? tc->nfk_cols : 1;
                cc->fkeys = xmalloc((size_t)cc->nfkeys * sizeof(int));
                for (int k = 0; k < tc->nfk_cols; k++) {
                    int ci = col_index(ref, tc->fk_cols[k]);
                    if (ci < 0) ereport(s, "42703", "column \"%s\" referenced in foreign key constraint does not exist", tc->fk_cols[k]);
                    cc->fkeys[k] = ci + 1;
                }
                if (!tc->nfk_cols) { cc->fkeys[0] = 1;
                    for (int k = 0; k < ref->ncons; k++)
                        if (ref->cons[k]->contype == 'p') cc->fkeys[0] = ref->cons[k]->keys[0]; }
                cc->fk_upd = tc->fk_upd; cc->fk_del = tc->fk_del;
                cc->fk_period = tc->fk_period;
#if PGVC_VN >= 180
                if (tc->fk_period) {
                    if (tc->fk_del && tc->fk_del != 'a')
                        ereport(s, "0A000", "unsupported ON DELETE action for foreign key "
                                            "constraint using PERIOD");
                    if (tc->fk_upd && tc->fk_upd != 'a')
                        ereport(s, "0A000", "unsupported ON UPDATE action for foreign key "
                                            "constraint using PERIOD");
                    bool okref = false;
                    for (int k = 0; k < ref->ncons; k++)
                        if ((ref->cons[k]->contype == 'p' || ref->cons[k]->contype == 'u') &&
                            ref->cons[k]->without_overlaps) okref = true;
                    if (!okref)
                        ereport(s, "42830", "there is no unique constraint matching given keys "
                                "for referenced table \"%s\"", ref->name);
                }
#endif
            }
            add_cons(t, cc);
#if PGVC_VN >= 180
            if (cc->without_overlaps && cc->nkeys >= 2) {
                int pk = cc->keys[cc->nkeys-1] - 1;
                for (int r = 0; r < t->nrows; r++) {
                    if (!row_visible(s, t->rows[r])) continue;
                    for (int r2 = r + 1; r2 < t->nrows; r2++) {
                        if (!row_visible(s, t->rows[r2])) continue;
                        bool same = true;
                        for (int k = 0; k + 1 < cc->nkeys && same; k++) {
                            Datum a = t->rows[r]->vals[cc->keys[k]-1];
                            Datum b2 = t->rows[r2]->vals[cc->keys[k]-1];
                            if (a.isnull || b2.isnull || d_cmp(s, a, b2) != 0) same = false;
                        }
                        if (!same) continue;
                        Datum ov[2]; ov[0] = t->rows[r]->vals[pk]; ov[1] = t->rows[r2]->vals[pk];
                        if (ov[0].isnull || ov[1].isnull) continue;
                        bool f3 = false;
                        Datum rr = call_builtin(s, "&&", ov, 2, &f3);
                        if (!f3 || rr.isnull || !rr.v.i) continue;
                        Buf kb; buf_init(&kb); Buf vb; buf_init(&vb); Buf ob; buf_init(&ob);
                        for (int k = 0; k < cc->nkeys; k++) {
                            if (k) { buf_adds(&kb, ", "); buf_adds(&vb, ", "); buf_adds(&ob, ", "); }
                            buf_adds(&kb, t->cols[cc->keys[k]-1].name);
                            char *o1 = out_func(s, t->rows[r]->vals[cc->keys[k]-1]);
                            char *o2 = out_func(s, t->rows[r2]->vals[cc->keys[k]-1]);
                            buf_adds(&vb, o1); buf_adds(&ob, o2);
                            free(o1); free(o2);
                        }
                        err_detail(s, "Key (%s)=(%s) conflicts with key (%s)=(%s).",
                                   kb.data, vb.data, kb.data, ob.data);
                        ereport(s, "23P01", "could not create exclusion constraint \"%s\"",
                                cc->name);
                    }
                }
            }
#endif
            if (tc->contype == 'p' || tc->contype == 'u') {
                int *keys = xmalloc((size_t)cc->nkeys*sizeof(int));
                memcpy(keys, cc->keys, (size_t)cc->nkeys*sizeof(int));
                int *incl = NULL;
                if (tc->nincl) {
                    incl = xmalloc((size_t)tc->nincl*sizeof(int));
                    for (int k = 0; k < tc->nincl; k++) { int ci = col_index(t, tc->incl[k]);
                        if (ci < 0) ereport(s, "42703", "column \"%s\" does not exist", tc->incl[k]);
                        incl[k] = ci+1; }
                }
                add_index(db, t, cc->name, keys, cc->nkeys, incl, tc->nincl, true, tc->contype=='p', NULL, NULL);
#if PGVC_VN >= 180
                if (tc->without_overlaps) db->indexes[db->nindexes-1]->am = 783;
                /* ALTER TABLE ADD PRIMARY KEY makes its columns not null */
                if (tc->contype == 'p')
                    for (int k = 0; k < cc->nkeys; k++) {
                        int pci = cc->keys[k] - 1;
                        bool have = false;
                        for (int q3 = 0; q3 < t->ncons; q3++)
                            if (t->cons[q3]->contype == 'n' && t->cons[q3]->nn_attnum == pci + 1)
                                have = true;
                        if (have) continue;
                        t->cols[pci].notnull = true;
                        Constraint *nc = xmalloc(sizeof *nc); memset(nc, 0, sizeof *nc);
                        nc->contype = 'n'; nc->nn_attnum = pci + 1;
                        nc->name = default_con_name(s, t, t->cols[pci].name, 'n');
                        add_cons(t, nc);
                    }
#endif
                db->indexes[db->nindexes-1]->conkind = tc->contype;
                if (t->relkind == 'p')
                    for (int q2 = 0; q2 < db->ntables; q2++)
                        if (db->tables[q2]->parent == t->oid)
                            clone_index_to(s, db, db->indexes[db->nindexes-1], db->tables[q2]);
            }
            for (int r = 0; r < t->nrows; r++)
                if (row_visible(s, t->rows[r])) check_constraints(s, t, t->rows[r]->vals, t->rows[r]);
            break;
        }
#if PGVC_VN >= 180
        case 28:                      /* ALTER TABLE ... ALTER CONSTRAINT */
            ereport(s, "0A000", "cannot alter enforceability of constraint \"%s\" of "
                                "relation \"%s\"", c->name, t->name);
#endif
        case 27: {                    /* ALTER TABLE ... VALIDATE CONSTRAINT */
            Constraint *vc = NULL;
            for (int k = 0; k < t->ncons; k++)
                if (!strcmp(t->cons[k]->name, c->name)) { vc = t->cons[k]; break; }
            if (!vc) ereport(s, "42704", "constraint \"%s\" of relation \"%s\" does not exist",
                             c->name, t->name);
            if (vc->contype == 'n') {
                int an = vc->nn_attnum;
                for (int r = 0; r < t->nrows; r++)
                    if (row_visible(s, t->rows[r]) && t->rows[r]->vals[an-1].isnull)
                        ereport(s, "23514", "column \"%s\" of relation \"%s\" contains null values",
                                t->cols[an-1].name, t->name);
            }
            vc->not_valid = false;
            break;
        }
        case 9: {
            bool found = false;
            for (int k = 0; k < t->ncons; k++)
                if (!strcmp(t->cons[k]->name, c->name)) {
                    if (t->cons[k]->contype == 'n' && t->cons[k]->nn_attnum >= 1 &&
                        t->cons[k]->nn_attnum <= t->ncols)
                        t->cols[t->cons[k]->nn_attnum - 1].notnull = false;
                    memmove(&t->cons[k], &t->cons[k+1], (size_t)(t->ncons-k-1)*sizeof(Constraint*));
                    t->ncons--; found = true; break;
                }
            for (int k = 0; k < db->nindexes; k++)
                if (!strcmp(db->indexes[k]->name, c->name) && db->indexes[k]->table == t->oid) {
                    memmove(&db->indexes[k], &db->indexes[k+1], (size_t)(db->nindexes-k-1)*sizeof(Index*));
                    db->nindexes--; break;
                }
            if (!found && !c->if_exists)
                ereport(s, "42704", "constraint \"%s\" of relation \"%s\" does not exist", c->name, t->name);
            break;
        }
        case 10: {
            Table *child = tbl_find(db, c->bound->schema, c->bound->name);
            if (!child) ereport(s, "42P01", "relation \"%s\" does not exist", c->bound->name);
            if (!t->partstrat) ereport(s, "42809", "table \"%s\" is not partitioned", t->name);
            catalog_note_table_state(db, child);
            attach_partition(s, t, child, c->bound, true);
#if PGVC_VN >= 110
            clone_part_indexes(s, db, t, child);
#endif
            break;
        }
        case 11: {
            Table *child = tbl_find(db, NULL, c->name);
            if (!child) ereport(s, "42P01", "relation \"%s\" does not exist", c->name);
#if PGVC_VN >= 140
            /* this server detaches immediately, so FINALIZE never has anything
             * pending to complete */
            if (c->finalize && child->parent == t->oid) {
                err_detail(s, "There's no pending concurrent detach.");
                ereport(s, "55000", "cannot complete detaching partition \"%s\"", child->name);
            }
#endif
            if (child->parent != t->oid)
                ereport(s, "42P01", "relation \"%s\" is not a partition of relation \"%s\"",
                        child->name, t->name);
            catalog_note_table_state(db, child);
            child->parent = 0;
            free(child->partbound); child->partbound = NULL;
            memset(&child->pbound, 0, sizeof child->pbound);
            break;
        }
        case 12: { free(t->name); t->name = xstrdup(c->name); break; }
        case 13: { int ci = col_index(t, c->name);
                   if (ci < 0) ereport(s, "42703", "column \"%s\" does not exist", c->name);
                   free(t->cols[ci].name); t->cols[ci].name = xstrdup(c->name2); break; }
        case 14: { free(t->owner); t->owner = xstrdup(c->name); break; }
        case 15: { free(t->schema); t->schema = xstrdup(c->name); break; }
        case 16: { int ci = col_index(t, c->name);
                   if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                       c->name, t->name);
                   Oid ct = t->cols[ci].type;
                   if (ct != INT2OID && ct != INT4OID && ct != INT8OID)
                       ereport(s, "42601", "identity column type must be smallint, integer, or bigint");
                   if (t->cols[ci].identity)
                       ereport(s, "42P16", "column \"%s\" of relation \"%s\" is already an identity column",
                               c->name, t->name);
                   t->cols[ci].identity = c->name2[0];
                   Buf sn; buf_init(&sn); buf_printf(&sn, "%s_%s_seq", t->name, c->name);
                   t->cols[ci].identity_seq = buf_steal(&sn);
                   t->cols[ci].notnull = true;
                   { ColDef *cd = c->col;
                     int64 inc = (cd && cd->has_seq_inc) ? cd->seq_inc : 1;
                     int64 minv = (cd && cd->has_seq_min) ? cd->seq_min : (inc > 0 ? 1 : INT64_MIN);
                     int64 maxv = (cd && cd->has_seq_max) ? cd->seq_max : (inc > 0 ? INT64_MAX : -1);
                     int64 start = (cd && cd->has_seq_start) ? cd->seq_start : (inc > 0 ? minv : maxv);
                     make_sequence(s, t->schema, t->cols[ci].identity_seq, start, inc, minv, maxv,
                                   cd ? cd->seq_cycle : false)->seq_owner = t->oid; }
                   break; }
        case 17: { int ci = col_index(t, c->name);
                   if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                       c->name, t->name);
                   if (!t->cols[ci].identity && !t->cols[ci].generated) {
                       if (c->if_exists) {
                           notice(s, "00000", "column \"%s\" of relation \"%s\" is not an "
                                  "identity column, skipping", c->name, t->name);
                           break;
                       }
                       ereport(s, "42P16", "column \"%s\" of relation \"%s\" is not an identity column",
                               c->name, t->name);
                   }
                   if (t->cols[ci].identity) {
                       Table *sq = tbl_find(db, t->schema, t->cols[ci].identity_seq);
                       if (sq) tbl_drop(db, sq);
                   }
                   t->cols[ci].identity = 0; t->cols[ci].generated = 0; break; }
#if PGVC_VN >= 140
        case 33: { /* ALTER COLUMN ... SET COMPRESSION */
                   int ci = col_index(t, c->name);
                   if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                       c->name, t->name);
                   const char *m = c->name2 ? c->name2 : "default";
                   char want;
                   if (!strcasecmp(m, "default")) want = 0;
                   else if (!strcasecmp(m, "pglz")) want = 'p';
                   else if (!strcasecmp(m, "lz4")) want = 'l';
                   else ereport(s, "42704", "invalid compression method \"%s\"", m);
                   Oid ty = t->cols[ci].type;
                   if (want && ty != TEXTOID && ty != VARCHAROID && ty != BPCHAROID &&
                       ty != BYTEAOID && ty != JSONBOID && ty != JSONOID && ty != XMLOID &&
                       !type_is_array(ty))
                       ereport(s, "42804", "column data type %s does not support compression",
                               format_type_str(ty, -1));
                   t->cols[ci].compression = want;
                   break; }
#endif
#if PGVC_VN >= 150
        case 34: { /* ALTER TABLE ... SET ACCESS METHOD */
                   static const char *ams[] = {"heap",NULL};
                   bool ok = false;
                   for (int q = 0; ams[q]; q++) if (!strcasecmp(ams[q], c->name)) ok = true;
                   if (!ok) ereport(s, "42704", "access method \"%s\" does not exist", c->name);
                   break; }
#endif
        case 30: { /* ALTER COLUMN ... DROP EXPRESSION */
                   int ci = col_index(t, c->name);
                   if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                       c->name, t->name);
                   /* PostgreSQL 18 drops a virtual generation expression too,
                    * and stops saying "stored" in the complaint. */
#if PGVC_VN >= 180
                   if (!t->cols[ci].generated) {
                       if (c->if_exists) {
                           notice(s, "00000", "column \"%s\" of relation \"%s\" is not a "
                                  "generated column, skipping", c->name, t->name);
                           break;
                       }
                       ereport(s, "42P16", "column \"%s\" of relation \"%s\" is not a "
                               "generated column", c->name, t->name);
                   }
                   /* A virtual column has no stored value to stop computing, so
                    * PostgreSQL 18 refuses rather than rewriting the table. */
                   if (t->cols[ci].generated == 'v') {
                       err_detail(s, "Column \"%s\" of relation \"%s\" is a virtual "
                                     "generated column.", c->name, t->name);
                       ereport(s, "0A000", "ALTER TABLE / DROP EXPRESSION is not supported "
                                           "for virtual generated columns");
                   }
#else
                   if (t->cols[ci].generated != 's') {
                       if (c->if_exists) {
                           notice(s, "00000", "column \"%s\" of relation \"%s\" is not a "
                                  "stored generated column, skipping", c->name, t->name);
                           break;
                       }
                       ereport(s, "42P16", "column \"%s\" of relation \"%s\" is not a stored "
                               "generated column", c->name, t->name);
                   }
#endif
                   t->cols[ci].generated = 0;
                   free(t->cols[ci].genexpr); t->cols[ci].genexpr = NULL;
                   break; }
        case 21: { int ci = col_index(t, c->name);
                   if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                       c->name, t->name);
                   if (!t->cols[ci].identity)
                       ereport(s, "42P16", "column \"%s\" of relation \"%s\" is not an identity column",
                               c->name, t->name);
                   t->cols[ci].identity = (c->name2 && tolower((unsigned char)c->name2[0]) == 'a')
                                          ? 'a' : 'd';
                   break; }
        case 24: {
            /* ALTER INDEX <partitioned> ATTACH PARTITION <child index> */
            Index *par = idx_find(db, st->name);
            Index *ch = idx_find(db, c->name);
            if (!par) ereport(s, "42704", "index \"%s\" does not exist", st->name);
            if (!ch) ereport(s, "42704", "index \"%s\" does not exist", c->name);
            ch->parent_index = par->oid;
            par->valid = true;
            break;
        }
        case 23: {
            if (t->relkind != 'S') break;
            if (c->sq_has_inc) t->seq_inc = c->sq_inc;
            if (c->sq_has_min) t->seq_min = c->sq_min;
            if (c->sq_has_max) t->seq_max = c->sq_max;
            if (c->sq_has_cache) t->seq_cache = c->sq_cache;
            if (c->sq_has_cycle) t->seq_cycle = c->sq_cycle;
            if (c->sq_has_start) t->seq_start = c->sq_start;
            if (c->sq_has_restart) {
                t->seq_last = (c->sq_restart == INT64_MIN) ? t->seq_start : c->sq_restart;
                t->seq_start = t->seq_last;
                t->seq_called = false;
            }
            break;
        }
        case 22: { int ci = col_index(t, c->name);
                   if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                       c->name, t->name);
                   if (!t->cols[ci].identity)
                       ereport(s, "42P16", "column \"%s\" of relation \"%s\" is not an identity column",
                               c->name, t->name);
                   Table *sq = tbl_find(db, t->schema, t->cols[ci].identity_seq);
                   if (sq) {
                       if (c->seq_what == 1) { sq->seq_start = c->seq_arg;
                                               sq->seq_last = c->seq_arg; sq->seq_called = false; }
                       else if (c->seq_what == 2) sq->seq_inc = c->seq_arg;
                       else if (c->seq_what == 3) sq->seq_start = c->seq_arg;
                   }
                   break; }
        default: break;
        }
    }
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "ALTER %s",
             st->objtype && !strcmp(st->objtype, "index") ? "INDEX" :
             st->objtype && !strcmp(st->objtype, "sequence") ? "SEQUENCE" :
             st->objtype && !strcmp(st->objtype, "view") ? "VIEW" : "TABLE");
    return q;
}

/* A table's identity and serial sequences are owned by it and go away with it. */
static void drop_owned_sequences(Session *s, Database *db, Table *t)
{
    (void)s;
    for (int i = db->ntables - 1; i >= 0; i--) {
        Table *sq = db->tables[i];
        if (sq->relkind == 'S' && sq->seq_owner == t->oid) tbl_drop(db, sq);
    }
}

static QueryResult *exec_drop(Session *s, Node *n)
{
    DropStmt *st = n->x;
    Database *db = s->db;
    char *ot = lower_ident(st->objtype);
    /* PostgreSQL resolves the whole object list before dropping anything, so
     * naming a partition next to its parent is not an error. */
    Table **targets = NULL;
    bool istbl = !strcmp(ot,"table") || !strcmp(ot,"view") || !strcmp(ot,"sequence") ||
                 !strcmp(ot,"materialized view");
    if (istbl && st->nnames) {
        targets = xmalloc((size_t)st->nnames * sizeof(Table *));
        for (int i = 0; i < st->nnames; i++)
            targets[i] = tbl_find(db, st->names[i][0], st->names[i][1]);
    }
    for (int i = 0; i < st->nnames; i++) {
        char *schema = st->names[i][0], *name = st->names[i][1];
        if (istbl) {
            Table *t = targets[i];
            if (t) {                      /* may already be gone with its parent */
                bool live = false;
                for (int k = 0; k < db->ntables; k++) if (db->tables[k] == t) live = true;
                if (!live) continue;
            }
            if (!t) {
                if (st->if_exists) { notice(s, "00000", "%s \"%s\" does not exist, skipping", ot, name); continue; }
                ereport(s, "42P01", "%s \"%s\" does not exist", ot, name);
            }
            (void)schema;
            for (int k = 0; k < db->ntables; k++)
                if (db->tables[k]->parent == t->oid) {
                    drop_owned_sequences(s, db, db->tables[k]);
                    tbl_drop(db, db->tables[k]); k--;
                }
            for (int k = db->nstats - 1; k >= 0; k--)
                if (db->stats[k]->table == t->oid) {
                    memmove(&db->stats[k], &db->stats[k+1],
                            (size_t)(db->nstats-k-1)*sizeof(ExtStat*));
                    db->nstats--;
                }
            if (!st->cascade)
                for (int k = 0; k < db->ntables; k++) {
                    Table *ch = db->tables[k];
                    if (ch == t) continue;
                    for (int q = 0; q < ch->ncons; q++)
                        if (ch->cons[q]->contype == 'f' && ch->cons[q]->fk_reltable == t->oid) {
                            err_detail(s, "constraint %s on table %s depends on table %s",
                                       ch->cons[q]->name, ch->name, t->name);
                            err_hint(s, "Use DROP ... CASCADE to drop the dependent objects too.");
                            ereport(s, "2BP01", "cannot drop table %s because other objects depend on it", t->name);
                        }
                }
            else
                for (int k = 0; k < db->ntables; k++) {
                    Table *ch = db->tables[k];
                    for (int q = ch->ncons - 1; q >= 0; q--)
                        if (ch->cons[q]->contype == 'f' && ch->cons[q]->fk_reltable == t->oid) {
                            memmove(&ch->cons[q], &ch->cons[q+1], (size_t)(ch->ncons-q-1)*sizeof(Constraint*));
                            ch->ncons--;
                        }
                }
            drop_owned_sequences(s, db, t);
            tbl_drop(db, t);
            continue;
        }
        if (!strcmp(ot,"index")) {
            Index *ix = idx_find(db, name);
            if (!ix) { if (st->if_exists) continue;
                       ereport(s, "42704", "index \"%s\" does not exist", name); }
            if (ix->parent_index) {
                Index *par = NULL;
                for (int k = 0; k < db->nindexes; k++)
                    if (db->indexes[k]->oid == ix->parent_index) par = db->indexes[k];
                if (par) {
                    err_hint(s, "You can drop index %s instead.", par->name);
                    ereport(s, "2BP01", "cannot drop index %s because index %s requires it",
                            ix->name, par->name);
                }
            }
            /* a partitioned index takes its per-partition copies with it */
            for (int k = db->nindexes - 1; k >= 0; k--)
                if (db->indexes[k]->parent_index == ix->oid) {
                    memmove(&db->indexes[k], &db->indexes[k+1],
                            (size_t)(db->nindexes-k-1)*sizeof(Index*));
                    db->nindexes--;
                }
            for (int k = 0; k < db->nindexes; k++)
                if (db->indexes[k] == ix) {
                    memmove(&db->indexes[k], &db->indexes[k+1], (size_t)(db->nindexes-k-1)*sizeof(Index*));
                    db->nindexes--; break;
                }
            continue;
        }
        if (!strcmp(ot,"schema")) {
            Schema *sc = sch_find(db, name);
            if (!sc) { if (st->if_exists) continue;
                       ereport(s, "3F000", "schema \"%s\" does not exist", name); }
            for (int k = db->ntables - 1; k >= 0; k--)
                if (!strcmp(db->tables[k]->schema, name)) {
                    if (!st->cascade) ereport(s, "2BP01",
                        "cannot drop schema %s because other objects depend on it", name);
                    notice(s, "00000", "drop cascades to %s %s.%s",
                           db->tables[k]->relkind == 'v' ? "view" :
                           db->tables[k]->relkind == 'S' ? "sequence" : "table",
                           name, db->tables[k]->name);
                    tbl_drop(db, db->tables[k]);
                }
            for (int k = 0; k < db->nschemas; k++)
                if (db->schemas[k] == sc) {
                    memmove(&db->schemas[k], &db->schemas[k+1], (size_t)(db->nschemas-k-1)*sizeof(Schema*));
                    db->nschemas--; break;
                }
            continue;
        }
        if (!strcmp(ot,"role") || !strcmp(ot,"user") || !strcmp(ot,"group")) {
            bool found = false;
            for (int k = 0; k < G->nroles; k++)
                if (!strcmp(G->roles[k]->name, name)) {
                    memmove(&G->roles[k], &G->roles[k+1], (size_t)(G->nroles-k-1)*sizeof(RoleEntry*));
                    G->nroles--; found = true; break;
                }
            if (!found) {
                if (st->if_exists) { notice(s, "00000", "role \"%s\" does not exist, skipping", name);
                                     continue; }
                ereport(s, "42704", "role \"%s\" does not exist", name);
            }
            continue;
        }
        if (!strcmp(ot,"domain") || !strcmp(ot,"type")) {
            if (!domain_drop(name)) {
                if (st->if_exists) { notice(s, "00000", "type \"%s\" does not exist, skipping", name);
                                     continue; }
                ereport(s, "42704", "type \"%s\" does not exist", name);
            }
            continue;
        }
        if (!strcmp(ot,"publication")) {
            bool found = false;
            for (int k = 0; k < db->npubs; k++)
                if (!strcmp(db->pubs[k]->name, name)) {
                    memmove(&db->pubs[k], &db->pubs[k+1], (size_t)(db->npubs-k-1)*sizeof(Publication*));
                    db->npubs--; found = true; break;
                }
            if (!found) {
                if (st->if_exists) { notice(s, "00000", "publication \"%s\" does not exist, skipping", name);
                                     continue; }
                ereport(s, "42704", "publication \"%s\" does not exist", name);
            }
            continue;
        }
        if (!strcmp(ot,"function") || !strcmp(ot,"procedure") || !strcmp(ot,"routine")) {
            char kind = !strcmp(ot,"procedure") ? 'p' : !strcmp(ot,"function") ? 'f' : 0;
            int want = (st->nargcount && i < st->nnames) ? st->nargcount[i] : -1;
            const char *sig = (st->argsig && i < st->nnames && st->argsig[i]) ? st->argsig[i] : NULL;
            SqlRoutine *hit = NULL, *other = NULL;
            for (int k = 0; k < db->nroutines; k++) {
                SqlRoutine *p = db->routines[k];
                if (strcmp(p->name, name)) continue;
                if (want >= 0 && p->nargs != want) continue;
                if (kind && p->prokind != kind) { if (!other) other = p; continue; }
                hit = p; break;
            }
            if (!hit && other) {
                /* PostgreSQL insists on the right DROP verb for the routine kind */
                ereport(s, "42809", "%s(%s) is not a %s", name, sig ? sig : "",
                        kind == 'p' ? "procedure" : "function");
            }
            if (!hit) {
                if (st->if_exists) {
                    Buf ib; buf_init(&ib);
                    if (sig) {
                        /* the skipping notice spells the types the internal way */
                        for (const char *q2 = sig; *q2; ) {
                            const char *e2 = strstr(q2, ", ");
                            char *one = e2 ? xstrndup(q2, (size_t)(e2 - q2)) : xstrdup(q2);
                            if (ib.len) buf_adds(&ib, ", ");
                            buf_printf(&ib, "pg_catalog.%s", type_name(type_by_name(one)));
                            free(one);
                            if (!e2) break;
                            q2 = e2 + 2;
                        }
                    }
                    char *isig = buf_steal(&ib);
                    notice(s, "00000", "%s %s(%s) does not exist, skipping", ot, name, isig);
                    free(isig);
                    continue;
                }
                if (!sig)
                    ereport(s, "42883", "could not find a %s named \"%s\"", ot, name);
                ereport(s, "42883", "%s %s(%s) does not exist", ot, name, sig);
            }
            for (int k = 0; k < db->nroutines; k++)
                if (db->routines[k] == hit) {
                    memmove(&db->routines[k], &db->routines[k+1], (size_t)(db->nroutines-k-1)*sizeof(SqlRoutine*));
                    db->nroutines--; break;
                }
            continue;
        }
        if (!strcmp(ot,"trigger")) {
            bool found = false;
            for (int k = 0; k < db->ntriggers; k++)
                if (!strcmp(db->triggers[k]->name, name)) {
                    memmove(&db->triggers[k], &db->triggers[k+1],
                            (size_t)(db->ntriggers-k-1)*sizeof(Trigger*));
                    db->ntriggers--; found = true; break;
                }
            if (!found) {
                if (st->if_exists) { notice(s, "00000", "trigger \"%s\" for relation \"%s\" does not exist, skipping",
                                            name, st->onrel ? st->onrel : ""); continue; }
                ereport(s, "42704", "trigger \"%s\" for table \"%s\" does not exist",
                        name, st->onrel ? st->onrel : "");
            }
            continue;
        }
        if (!strcmp(ot,"statistics")) {
            bool found = false;
            for (int k = 0; k < db->nstats; k++)
                if (!strcmp(db->stats[k]->name, name)) {
                    memmove(&db->stats[k], &db->stats[k+1],
                            (size_t)(db->nstats-k-1)*sizeof(ExtStat*));
                    db->nstats--; found = true; break;
                }
            if (!found) {
                if (st->if_exists) { notice(s, "00000", "statistics object \"%s\" does not exist, skipping", name);
                                     continue; }
                ereport(s, "42704", "statistics object \"%s\" does not exist", name);
            }
            continue;
        }
        if (!strcmp(ot,"database")) {
            bool gone = true;
            for (int k = 0; k < G->ndbs; k++)
                if (!strcmp(G->dbs[k]->name, name)) {
                    memmove(&G->dbs[k], &G->dbs[k+1], (size_t)(G->ndbs-k-1)*sizeof(Database*));
                    G->ndbs--; gone = false; break;
                }
            if (gone) {
                if (st->if_exists) { notice(s, "00000", "database \"%s\" does not exist, skipping", name);
                                     continue; }
                ereport(s, "42704", "database \"%s\" does not exist", name);
            }
            continue;
        }
        if (!strcmp(ot,"subscription")) {
            bool found = false;
            for (int k = 0; k < db->nsubs; k++)
                if (!strcmp(db->subs[k]->name, name)) {
                    catalog_note(14 /* U_DEL_SUB */, db, db->subs[k]);
                    memmove(&db->subs[k], &db->subs[k+1],
                            (size_t)(db->nsubs-k-1)*sizeof(Subscription*));
                    db->nsubs--; found = true; break;
                }
            if (!found) {
                if (st->if_exists) { notice(s, "00000", "subscription \"%s\" does not exist, skipping", name);
                                     continue; }
                ereport(s, "42704", "subscription \"%s\" does not exist", name);
            }
            continue;
        }
        /* types and triggers: accepted */
    }
    QueryResult *q = qr_new();
    char up[64]; snprintf(up, sizeof up, "%s", ot);
    for (char *p = up; *p; p++) *p = (char)toupper((unsigned char)*p);
    snprintf(q->tag, sizeof q->tag, "DROP %s", up);
    return q;
}

/* ================================================= remaining statements */
void xact_begin(Session *s, int isolation, int readonly);
void catalog_undo_begin(void);
void catalog_undo_apply(void);
void catalog_undo_clear(void);
void catalog_note(int kind, Database *db, void *obj);
void catalog_note_table_state(Database *db, Table *t);
void xact_commit(Session *s);
void xact_rollback(Session *s);
void xact_savepoint(Session *s, const char *name);
void xact_rollback_to(Session *s, const char *name);
void xact_release(Session *s, const char *name);

QueryResult *exec_copy(Session *s, Node *n);
QueryResult *exec_repl(Session *s, Node *n);

static QueryResult *one_text(const char *colname, const char *val, Oid type)
{
    QueryResult *q = qr_new();
    qr_addcol(q, colname, type, -1);
    Datum *row = xmalloc(sizeof(Datum));
    row[0] = val ? d_text(type, val) : d_null(type);
    qr_addrow(q, row);
    snprintf(q->tag, sizeof q->tag, "SHOW");
    return q;
}

static QueryResult *exec_explain(Session *s, Node *n)
{
    ExplainStmt *st = n->x;
    if (st->errkind == 1)
        ereport_pos(s, "42601", st->errpos, "unrecognized EXPLAIN option \"%s\"", st->errarg);
    if (st->errkind == 2)
        ereport(s, "42601", "%s requires a Boolean value", st->errarg);
    if (st->buffers && !st->analyze_)
        ereport(s, "22023", "EXPLAIN option BUFFERS requires ANALYZE");
#if PGVC_VN >= 130
    if (st->wal && !st->analyze_)
        ereport(s, "22023", "EXPLAIN option WAL requires ANALYZE");
#endif
    QueryResult *q = qr_new();
    qr_addcol(q, "QUERY PLAN", TEXTOID, -1);
    Node *inner = st->query;
    Buf b; buf_init(&b);
    if (inner->tag == N_SELECT) {
        SelectStmt *sel = inner->select;
        if (sel->nfrom == 0) buf_adds(&b, "Result");
        else if (sel->nfrom == 1 && sel->from[0]->kind == 0)
            buf_printf(&b, "Seq Scan on %s", sel->from[0]->relname);
        else buf_adds(&b, "Nested Loop");
    } else buf_adds(&b, "Result");
    if (st->costs) buf_adds(&b, "  (cost=0.00..0.00 rows=0 width=0)");
    Datum *row = xmalloc(sizeof(Datum));
    row[0] = d_text(TEXTOID, b.data);
    qr_addrow(q, row);
    buf_free(&b);
    if (st->analyze_) {
        /* running the statement is part of EXPLAIN ANALYZE */
        exec_node(s, inner);
    }
    snprintf(q->tag, sizeof q->tag, "EXPLAIN");
    return q;
}

/* The reloptions a relation of each kind takes. */
static char *rel_opt_check(Session *s, char relkind, const char *name, const char *val)
{
    static const struct { const char *nm; char kind; int minvn; bool viewonly; } o[] = {
        {"fillfactor",'i',0,false}, {"autovacuum_enabled",'b',0,false},
        {"toast_tuple_target",'i',0,false}, {"parallel_workers",'i',0,false},
        {"user_catalog_table",'b',0,false},
        {"vacuum_index_cleanup",'e',0,false}, {"vacuum_truncate",'b',0,false},
        {"security_barrier",'b',0,true}, {"check_option",'e',0,true},
        {"security_invoker",'b',150,true},
        {NULL,0,0,false}
    };
    bool isview = (relkind == 'v' || relkind == 'm');
    for (int i = 0; o[i].nm; i++) {
        if (strcmp(o[i].nm, name)) continue;
        if (o[i].minvn && PGVC_VN < o[i].minvn) break;
        if (o[i].viewonly && !isview) break;
        if (!o[i].viewonly && isview) break;
        if (o[i].kind == 'b') {
            if (!strcasecmp(val,"on")||!strcasecmp(val,"true")||!strcasecmp(val,"yes")||
                !strcmp(val,"1")) return xstrdup("true");
            if (!strcasecmp(val,"off")||!strcasecmp(val,"false")||!strcasecmp(val,"no")||
                !strcmp(val,"0")) return xstrdup("false");
            ereport(s, "22023", "invalid value for boolean option \"%s\": %s", name, val);
        }
        return xstrdup(val);
    }
    ereport(s, "22023", "unrecognized parameter \"%s\"", name);
}

static void rel_set_opt(Table *t, const char *name, const char *val)
{
    for (int i = 0; i < t->nopts; i++)
        if (!strcmp(t->optname[i], name)) { free(t->optval[i]); t->optval[i] = xstrdup(val); return; }
    t->optname = xrealloc(t->optname, (size_t)(t->nopts+1)*sizeof(char*));
    t->optval  = xrealloc(t->optval,  (size_t)(t->nopts+1)*sizeof(char*));
    t->optname[t->nopts] = xstrdup(name);
    t->optval[t->nopts] = xstrdup(val);
    t->nopts++;
}

static QueryResult *exec_create_view(Session *s, Node *n)
{
    CreateViewStmt *st = n->x;
    Database *db = s->db;
    const char *schema = st->schema ? st->schema : "public";
    Table *old = tbl_find(db, schema, st->name);
    if (old && !st->replace) ereport(s, "42P07", "relation \"%s\" already exists", st->name);
    if (old) tbl_drop(db, old);
#if PGVC_VN < 150
    if (st->security_invoker) ereport(s, "42601", "unrecognized parameter \"security_invoker\"");
#endif
    ExecCtx c; memset(&c, 0, sizeof c); c.s = s;
    RowSet *rs = exec_select(&c, st->query->select);
    Table *t = mk_table(db, schema, st->name, st->materialized ? 'm' : 'v');
    for (int i = 0; i < rs->ncols; i++)
        add_col(t, (i < st->ncols) ? st->cols[i] : rs->cols[i].name,
                rs->cols[i].type == UNKNOWNOID ? TEXTOID : rs->cols[i].type, rs->cols[i].typmod);
    t->viewdef = xstrdup(st->querysrc);
    t->check_option = st->check_option;
    t->security_invoker = st->security_invoker;
    for (int i = 0; i < st->nopts; i++) {
        char *v = rel_opt_check(s, st->materialized ? 'm' : 'v', st->optname[i], st->optval[i]);
        rel_set_opt(t, st->optname[i], v);
        free(v);
    }
    tbl_add(db, t);
    if (st->materialized) {
        t->relkind = 'm';
        for (int i = 0; i < rs->nrows; i++) {
            Row *r = xmalloc(sizeof *r); memset(r, 0, sizeof *r);
            r->xmin = 1; r->n = t->ncols; r->vals = rs->rows[i];
            if (t->nrows == t->caprows) { t->caprows = t->caprows ? t->caprows*2 : 16;
                                          t->rows = xrealloc(t->rows, (size_t)t->caprows*sizeof(Row*)); }
            t->rows[t->nrows++] = r;
        }
    }
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, st->materialized ? "SELECT %d" : "CREATE VIEW", rs->nrows);
    return q;
}

/* The name PostgreSQL gives an index it creates for a partition. */
static char *child_index_name(Database *db, Table *child, Index *parent)
{
    Buf b; buf_init(&b);
    buf_adds(&b, child->name);
    if (parent->conkind == 'p') buf_adds(&b, "_pkey");
    else {
        Table *pt = tbl_find_oid(db, parent->table);
        for (int i = 0; i < parent->nkeys + parent->nincl; i++) {
            int attn = (i < parent->nkeys) ? parent->keys[i] : parent->incl[i - parent->nkeys];
            const char *cn = (attn > 0 && pt && attn <= pt->ncols) ? pt->cols[attn-1].name : "expr";
            buf_printf(&b, "_%s", cn);
        }
        buf_adds(&b, parent->conkind == 'u' ? "_key" : "_idx");
    }
    char *base = buf_steal(&b);
    if (!idx_find(db, base)) return base;
    for (int k = 1; k < 100; k++) {
        Buf c2; buf_init(&c2);
        buf_printf(&c2, "%s%d", base, k);
        char *cand = buf_steal(&c2);
        if (!idx_find(db, cand)) { free(base); return cand; }
        free(cand);
    }
    return base;
}

/* PostgreSQL creates a matching index on every partition and keeps them
 * attached to the partitioned index. */
static void clone_index_to(Session *s, Database *db, Index *parent, Table *child)
{
    char *nm = child_index_name(db, child, parent);
    int *keys = xmalloc((size_t)(parent->nkeys ? parent->nkeys : 1) * sizeof(int));
    for (int i = 0; i < parent->nkeys; i++) {
        Table *pt = tbl_find_oid(db, parent->table);
        int ci = (parent->keys[i] > 0 && pt) ? col_index(child, pt->cols[parent->keys[i]-1].name) : -1;
        keys[i] = ci >= 0 ? ci + 1 : parent->keys[i];
    }
    int *incl = NULL;
    if (parent->nincl) {
        incl = xmalloc((size_t)parent->nincl * sizeof(int));
        for (int i = 0; i < parent->nincl; i++) incl[i] = parent->incl[i];
    }
    add_index(db, child, nm, keys, parent->nkeys, incl, parent->nincl,
              parent->unique, false, parent->predicate, NULL);
    Index *ix = db->indexes[db->nindexes-1];
    ix->parent_index = parent->oid;
    ix->am = parent->am;
    ix->conkind = parent->conkind;
    ix->primary = parent->primary;
    if (parent->unique) {
        Constraint *c = xmalloc(sizeof *c); memset(c, 0, sizeof *c);
        c->contype = parent->conkind ? parent->conkind : 'u'; c->name = xstrdup(nm);
        c->keys = xmalloc((size_t)(parent->nkeys ? parent->nkeys : 1) * sizeof(int));
        memcpy(c->keys, keys, (size_t)parent->nkeys * sizeof(int));
        c->nkeys = parent->nkeys;
        add_cons(child, c);
    }
    if (child->relkind == 'p') {
        for (int i = 0; i < db->ntables; i++)
            if (db->tables[i]->parent == child->oid) clone_index_to(s, db, ix, db->tables[i]);
    }
    free(nm);
}

/* Give a freshly attached partition a copy of every partitioned index. */
static void clone_part_indexes(Session *s, Database *db, Table *parent, Table *child)
{
    int n = db->nindexes;
    for (int i = 0; i < n; i++)
        if (db->indexes[i]->table == parent->oid && db->indexes[i]->partitioned &&
            db->indexes[i]->valid)
            clone_index_to(s, db, db->indexes[i], child);
}

/* The reloptions each index access method takes, and the release each one
 * appeared in.  PostgreSQL rejects an option the method does not know before
 * it looks at the value. */
typedef struct { Oid am; const char *name; char kind; int minvn; } IdxOptDef;
static const IdxOptDef idx_opts[] = {
    {403,"fillfactor",'i',0}, {403,"vacuum_cleanup_index_scale_factor",'r',110},
    {403,"deduplicate_items",'b',130},
    {405,"fillfactor",'i',0},
    {783,"fillfactor",'i',0}, {783,"buffering",'e',0},
    {2742,"fastupdate",'b',0}, {2742,"gin_pending_list_limit",'i',0},
    {4000,"fillfactor",'i',0},
    {3580,"pages_per_range",'i',0}, {3580,"autosummarize",'b',0},
    {0,NULL,0,0}
};

/* Normalise one index reloption, raising what PostgreSQL raises. */
static char *idx_opt_check(Session *s, Oid am, const char *name, const char *val, int pos)
{
    const IdxOptDef *d = NULL;
    for (int i = 0; idx_opts[i].name; i++)
        if (idx_opts[i].am == am && !strcmp(idx_opts[i].name, name) &&
            (!idx_opts[i].minvn || PGVC_VN >= idx_opts[i].minvn)) d = &idx_opts[i];
    (void) pos;
    if (!d) ereport(s, "22023", "unrecognized parameter \"%s\"", name);
    if (d->kind == 'b') {
        if (!strcasecmp(val,"on") || !strcasecmp(val,"true") || !strcasecmp(val,"yes") ||
            !strcmp(val,"1")) return xstrdup("on");
        if (!strcasecmp(val,"off") || !strcasecmp(val,"false") || !strcasecmp(val,"no") ||
            !strcmp(val,"0")) return xstrdup("off");
        ereport(s, "22023", "invalid value for boolean option \"%s\": %s", name, val);
    }
    if (d->kind == 'i' || d->kind == 'r') {
        char *end = NULL;
        strtod(val, &end);
        if (!end || *end || end == val)
            ereport(s, "22023", "invalid value for %s option \"%s\": %s",
                    d->kind == 'i' ? "integer" : "floating point", name, val);
    }
    return xstrdup(val);
}

static void idx_set_opt(Index *ix, const char *name, const char *val)
{
    for (int i = 0; i < ix->nopts; i++)
        if (!strcmp(ix->optname[i], name)) {
            free(ix->optval[i]); ix->optval[i] = xstrdup(val);
            return;
        }
    ix->optname = xrealloc(ix->optname, (size_t)(ix->nopts+1)*sizeof(char*));
    ix->optval  = xrealloc(ix->optval,  (size_t)(ix->nopts+1)*sizeof(char*));
    ix->optname[ix->nopts] = xstrdup(name);
    ix->optval[ix->nopts] = xstrdup(val);
    ix->nopts++;
}

static QueryResult *exec_create_index(Session *s, Node *n)
{
    CreateIndexStmt *st = n->x;
    Database *db = s->db;
    Table *t = tbl_find(db, st->schema, st->table);
    if (!t) ereport(s, "42P01", "relation \"%s\" does not exist", st->table);
#if PGVC_VN < 110
    if (t->relkind == 'p')
        ereport(s, "42809", "cannot create index on partitioned table \"%s\"", t->name);
#else
    if (t->relkind == 'p' && st->unique)
        for (int k = 0; k < t->npartkey; k++) {
            bool have = false;
            for (int i = 0; i < st->ncols; i++) {
                int ci = st->colexprs[i] ? -1 : col_index(t, st->cols[i]);
                if (ci >= 0 && ci + 1 == t->partkey[k]) have = true;
            }
            if (!have) {
                err_detail(s, "UNIQUE constraint on table \"%s\" lacks column \"%s\" which is "
                              "part of the partition key.", t->name, t->cols[t->partkey[k]-1].name);
                ereport(s, "0A000", "unique constraint on partitioned table must include "
                                    "all partitioning columns");
            }
            (void)0;
        }
#endif
    /* Only the access methods PostgreSQL ships, with the limits each imposes. */
    Oid amoid = 403;
    if (st->method) {
        static const struct { const char *nm; Oid oid; } am[] = {
            {"btree",403},{"hash",405},{"gist",783},{"gin",2742},
            {"spgist",4000},{"brin",3580},{NULL,0}
        };
        amoid = 0;
        for (int i = 0; am[i].nm; i++) if (!strcasecmp(am[i].nm, st->method)) amoid = am[i].oid;
        if (!amoid) ereport(s, "42704", "access method \"%s\" does not exist", st->method);
        if (amoid == 405) {
            if (st->unique)
                ereport(s, "0A000", "access method \"hash\" does not support unique indexes");
            if (st->ncols > 1)
                ereport(s, "0A000", "access method \"hash\" does not support multicolumn indexes");
            if (st->nincl)
                ereport(s, "0A000", "access method \"hash\" does not support included columns");
        }
        if (amoid != 403 && amoid != 783 && st->nincl)
            ereport(s, "0A000", "access method \"%s\" does not support included columns", st->method);
    }
    char *name = st->name ? xstrdup(st->name) : NULL;
    if (!name) {
        Buf b; buf_init(&b);
        buf_printf(&b, "%s", t->name);
        for (int i = 0; i < st->ncols; i++) buf_printf(&b, "_%s", st->cols[i]);
        buf_adds(&b, st->unique ? "_key" : "_idx");
        name = buf_steal(&b);
    }
    if (idx_find(db, name)) {
        if (st->if_not_exists) { notice(s, "42P07", "relation \"%s\" already exists, skipping", name);
                                 QueryResult *q = qr_new(); snprintf(q->tag,sizeof q->tag,"CREATE INDEX"); return q; }
        ereport(s, "42P07", "relation \"%s\" already exists", name);
    }
    int *keys = xmalloc((size_t)(st->ncols?st->ncols:1)*sizeof(int));
    char **exprs = NULL;
    for (int i = 0; i < st->ncols; i++) {
        if (st->colexprs[i]) {
            keys[i] = 0;
            exprs = xrealloc(exprs, (size_t)st->ncols * sizeof(char *));
            for (int k = 0; k < st->ncols; k++) if (!exprs[k]) exprs[k] = NULL;
            exprs[i] = xstrdup(st->cols[i]);
            continue;
        }
        int ci = col_index(t, st->cols[i]);
        if (ci < 0) ereport(s, "42703", "column \"%s\" does not exist", st->cols[i]);
#if PGVC_VN >= 180
        if (t->cols[ci].generated == 'v')
            ereport(s, "0A000", "indexes on virtual generated columns are not supported");
#endif
        keys[i] = ci + 1;
    }
    int *incl = NULL;
    if (st->nincl) {
#if PGVC_VN < 110
        ereport(s, "42601", "syntax error at or near \"INCLUDE\"");
#endif
        incl = xmalloc((size_t)st->nincl*sizeof(int));
        for (int i = 0; i < st->nincl; i++) {
            if (st->incl[i] && st->incl[i][0] == '(')
                ereport(s, "0A000", "expressions are not supported in included columns");
            int ci = col_index(t, st->incl[i]);
            if (ci < 0) ereport(s, "42703", "column \"%s\" does not exist", st->incl[i]);
            incl[i] = ci + 1;
        }
    }
#if PGVC_VN < 150
    if (st->nulls_nd) ereport(s, "42601", "syntax error at or near \"NULLS\"");
#endif
    if (st->unique)
        for (int r = 0; r < t->nrows; r++) {
            if (!row_visible(s, t->rows[r])) continue;
            for (int r2 = r + 1; r2 < t->nrows; r2++) {
                if (!row_visible(s, t->rows[r2])) continue;
                bool same = true;
                for (int k = 0; k < st->ncols && same; k++) {
                    if (!keys[k]) { same = false; break; }
                    Datum a = t->rows[r]->vals[keys[k]-1], b2 = t->rows[r2]->vals[keys[k]-1];
                    if (a.isnull || b2.isnull || d_cmp(s, a, b2) != 0) same = false;
                }
                if (same) {
                    Buf vb; buf_init(&vb);
                    for (int k = 0; k < st->ncols; k++) { if (k) buf_adds(&vb, ", ");
                        char *o = out_func(s, t->rows[r]->vals[keys[k]-1]); buf_adds(&vb, o); free(o); }
                    err_detail(s, "Key (%s)=(%s) is duplicated.", st->cols[0], vb.data);
                    ereport(s, "23505", "could not create unique index \"%s\"", name);
                }
            }
        }
    /* the options are checked before the index is created, so a bad one leaves
     * nothing behind */
    char **optv = NULL;
    if (st->nopts) {
        optv = xmalloc((size_t)st->nopts * sizeof(char *));
        for (int i = 0; i < st->nopts; i++)
            optv[i] = idx_opt_check(s, amoid, st->optname[i], st->optval[i], st->optpos[i]);
    }
    add_index(db, t, name, keys, st->ncols, incl, st->nincl, st->unique, false, st->predicate, exprs);
    db->indexes[db->nindexes-1]->am = amoid;
    db->indexes[db->nindexes-1]->nulls_nd = st->nulls_nd;
    if (st->unique && st->nulls_nd) {
        /* the index carries a unique constraint, which is where the check lives */
        for (int i = 0; i < t->ncons; i++)
            if (t->cons[i]->contype == 'u' && t->cons[i]->name &&
                !strcmp(t->cons[i]->name, name)) t->cons[i]->nulls_nd = true;
    }
    for (int i = 0; i < st->nopts; i++) {
        idx_set_opt(db->indexes[db->nindexes-1], st->optname[i], optv[i]);
        free(optv[i]);
    }
    free(optv);
#if PGVC_VN >= 110
    if (t->relkind == 'p') {
        Index *par = db->indexes[db->nindexes-1];
        par->valid = !st->only;
        if (!st->only)
            for (int i = 0; i < db->ntables; i++)
                if (db->tables[i]->parent == t->oid) clone_index_to(s, db, par, db->tables[i]);
    }
#endif
    if (st->unique) {
        Constraint *c = xmalloc(sizeof *c); memset(c, 0, sizeof *c);
        c->contype = 'u'; c->name = xstrdup(name);
        c->keys = xmalloc((size_t)st->ncols*sizeof(int));
        memcpy(c->keys, keys, (size_t)st->ncols*sizeof(int));
        c->nkeys = st->ncols;
        c->nulls_nd = st->nulls_nd;
        add_cons(t, c);
    }
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "CREATE INDEX");
    return q;
}

/* password_encryption decides the verifier format; before PostgreSQL 10 the
 * parameter was a boolean and md5 was the only hashed form. */
static char *build_password(Session *s, const char *user, const char *pass)
{
    const char *enc = guc_get(s, "password_encryption");
    char *verifier;
#if PGVC_VN >= 100
    if (enc && !strcasecmp(enc, "scram-sha-256")) {
        unsigned char salt[16];
        for (int i = 0; i < 16; i++) salt[i] = (unsigned char)(i * 17 + 3);
        verifier = scram_build_verifier(pass, strlen(pass), salt, 16, 4096);
    } else {
        verifier = md5_build_verifier(pass, user);
    }
#else
    (void)enc;
    verifier = md5_build_verifier(pass, user);
#endif
#if PGVC_VN >= 180
    /* PostgreSQL 18 warns whenever the verifier it ends up storing is MD5,
     * whichever statement produced it, unless md5_password_warnings is off.
     * The test is on the stored verifier rather than on password_encryption,
     * so a pre-hashed md5 password warns too. */
    if (verifier && !strncmp(verifier, "md5", 3)) {
        const char *w = guc_get(s, "md5_password_warnings");
        bool on = !(w && (!strcasecmp(w, "off") || !strcasecmp(w, "false") ||
                          !strcasecmp(w, "no") || !strcmp(w, "0")));
        if (on)
            warning_detail_hint(s, "00000",
                "MD5 password support is deprecated and will be removed in a "
                "future release of PostgreSQL.",
                "Refer to the PostgreSQL documentation for details about "
                "migrating to another password type.",
                "setting an MD5-encrypted password");
    }
#endif
    return verifier;
}

/* CREATE TRIGGER.  The trigger itself lives on the relation; PL/pgSQL runs
 * the function when the executor fires it. */
static QueryResult *exec_create_trigger(Session *s, Node *n)
{
    CreateTrigStmt *st = n->x;
    Database *db = s->db;
    if (!st) { QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "CREATE TRIGGER"); return q; }
    Table *t = tbl_find(db, st->schema, st->table);
    if (!t) ereport(s, "42P01", "relation \"%s\" does not exist", st->table);
    for (int i = 0; i < db->ntriggers; i++)
        if (db->triggers[i]->table == t->oid && !strcmp(db->triggers[i]->name, st->name))
            ereport(s, "42710", "trigger \"%s\" for relation \"%s\" already exists",
                    st->name, t->name);
    if (st->oldtable || st->newtable) {
        if (st->timing != 1)
            ereport(s, "0A000", "transition table name can only be specified for an AFTER trigger");
        if (st->oldtable && !(st->events & (2 | 4)))
            ereport(s, "0A000", "OLD TABLE can only be specified for a DELETE or UPDATE trigger");
        if (st->newtable && !(st->events & (1 | 2)))
            ereport(s, "0A000", "NEW TABLE can only be specified for an INSERT or UPDATE trigger");
        if (st->nold > 1) ereport(s, "42601", "OLD TABLE cannot be specified multiple times");
        if (st->nnew > 1) ereport(s, "42601", "NEW TABLE cannot be specified multiple times");
    }
    if (t->relkind == 'p') {
        /* PostgreSQL 11 allows AFTER row triggers on a partitioned table by
         * cloning them to the partitions; 10 allows no row trigger at all;
         * 13 allows BEFORE row triggers too. */
#if PGVC_VN < 110
        if (st->forrow) {
            err_detail(s, "Partitioned tables cannot have ROW triggers.");
            ereport(s, "42809", "\"%s\" is a partitioned table", t->name);
        }
#elif PGVC_VN < 130
        if (st->forrow && st->timing == 0) {
            err_detail(s, "Partitioned tables cannot have BEFORE / FOR EACH ROW triggers.");
            ereport(s, "42809", "\"%s\" is a partitioned table", t->name);
        }
#endif
#if PGVC_VN >= 130
        if (st->forrow && st->timing == 2)
            ereport(s, "42809", "\"%s\" is a partitioned table", t->name);
#endif
    }
    /* the trigger function has to exist */
    if (st->funcname && *st->funcname) {
        bool found = false;
        for (int i = 0; i < db->nroutines; i++)
            if (!strcmp(db->routines[i]->name, st->funcname)) found = true;
        if (!found)
            ereport(s, "42883", "function %s() does not exist", st->funcname);
    }
    Trigger *tg = xmalloc(sizeof *tg);
    memset(tg, 0, sizeof *tg);
    tg->oid = new_oid();
    tg->name = xstrdup(st->name);
    tg->table = t->oid;
    tg->timing = st->timing;
    tg->events = st->events;
    tg->forrow = st->forrow;
    tg->funcname = xstrdup(st->funcname ? st->funcname : "");
    tg->whensrc = st->whensrc ? xstrdup(st->whensrc) : NULL;
    tg->oldtable = st->oldtable ? xstrdup(st->oldtable) : NULL;
    tg->newtable = st->newtable ? xstrdup(st->newtable) : NULL;
    db->triggers = xrealloc(db->triggers, (size_t)(db->ntriggers + 1) * sizeof(Trigger *));
    db->triggers[db->ntriggers++] = tg;
#if PGVC_VN >= 110
    /* A row trigger on a partitioned table is cloned onto every partition;
     * before PostgreSQL 14 the clones are marked internal. */
    if (t->relkind == 'p' && st->forrow) {
        int base = db->ntriggers;
        for (int i = 0; i < db->ntables; i++) {
            if (db->tables[i]->parent != t->oid) continue;
            Trigger *cl = xmalloc(sizeof *cl);
            *cl = *tg;
            cl->oid = new_oid();
            cl->name = xstrdup(tg->name);
            cl->table = db->tables[i]->oid;
            cl->funcname = xstrdup(tg->funcname);
            cl->whensrc = tg->whensrc ? xstrdup(tg->whensrc) : NULL;
            cl->oldtable = tg->oldtable ? xstrdup(tg->oldtable) : NULL;
            cl->newtable = tg->newtable ? xstrdup(tg->newtable) : NULL;
            cl->isclone = true;
            cl->parent_trig = tg->oid;
            db->triggers = xrealloc(db->triggers,
                                    (size_t)(db->ntriggers + 1) * sizeof(Trigger *));
            db->triggers[db->ntriggers++] = cl;
        }
        (void) base;
    }
#endif
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "CREATE TRIGGER");
    return q;
}

/* CREATE STATISTICS (PostgreSQL 10). */
static QueryResult *exec_create_stats(Session *s, Node *n)
{
    CreateIndexStmt *st = n->x;
    Database *db = s->db;
#if PGVC_VN < 100
    ereport(s, "42601", "syntax error at or near \"STATISTICS\"");
#else
    const char *schema = st->schema ? st->schema : "public";
    if (st->stats_alter) {
        ExtStat *e = NULL;
        for (int i = 0; i < db->nstats; i++)
            if (!strcmp(db->stats[i]->name, st->name)) e = db->stats[i];
        if (!e) {
            if (st->if_not_exists) {
                notice(s, "00000", "statistics object \"%s\" does not exist, skipping", st->name);
                QueryResult *q = qr_new();
                snprintf(q->tag, sizeof q->tag, "ALTER STATISTICS"); return q;
            }
            ereport(s, "42704", "statistics object \"%s\" does not exist", st->name);
        }
        if (st->stats_alter == 1) {
            if (st->stattarget < -1)
                ereport(s, "22023", "statistics target %d is too low", st->stattarget);
            if (st->stattarget > 10000)
                ereport(s, "22023", "lowering statistics target to %d", 10000);
            e->stattarget = st->stattarget;
        } else if (st->stats_alter == 2) {
            free(e->name); e->name = xstrdup(st->newname);
        }
        QueryResult *q = qr_new();
        snprintf(q->tag, sizeof q->tag, "ALTER STATISTICS");
        return q;
    }
    for (int i = 0; i < db->nstats; i++)
        if (!strcmp(db->stats[i]->name, st->name)) {
            if (st->if_not_exists) {
                notice(s, "42710", "statistics object \"%s\" already exists, skipping", st->name);
                QueryResult *q = qr_new();
                snprintf(q->tag, sizeof q->tag, "CREATE STATISTICS");
                return q;
            }
            ereport(s, "42710", "statistics object \"%s\" already exists", st->name);
        }
    Table *t = tbl_find(db, NULL, st->table);
    if (!t) ereport(s, "42P01", "relation \"%s\" does not exist", st->table);
    /* PostgreSQL stores the kinds sorted: d ndistinct, f dependencies,
     * m mcv (PostgreSQL 12), e expressions (PostgreSQL 14). */
    char kinds[8]; int nk = 0;
    bool want_d = false, want_f = false, want_m = false;
    if (!st->nincl) { want_d = want_f = true; if (PGVC_VN >= 120) want_m = true; }
    for (int i = 0; i < st->nincl; i++) {
        if (!strcasecmp(st->incl[i], "ndistinct")) want_d = true;
        else if (!strcasecmp(st->incl[i], "dependencies")) want_f = true;
        else if (!strcasecmp(st->incl[i], "mcv") && PGVC_VN >= 120) want_m = true;
        else ereport(s, "22023", "unrecognized statistics kind \"%s\"", st->incl[i]);
    }
#if PGVC_VN >= 140
    /* an object built only on expressions carries the "e" kind alone */
    if (st->nexprs && !st->nincl) {
        want_d = want_f = want_m = false;
        if (st->ncols + st->nexprs >= 2) { want_d = want_f = true; want_m = true; }
    }
#endif
    if (want_d) kinds[nk++] = 'd';
    if (want_f) kinds[nk++] = 'f';
    if (want_m) kinds[nk++] = 'm';
#if PGVC_VN >= 140
    if (st->nexprs) kinds[nk++] = 'e';
#endif
    kinds[nk] = 0;
    if (st->ncols + st->nexprs < 2 && !st->nexprs)
        ereport(s, "22023", "extended statistics require at least 2 columns");
    ExtStat *e = xmalloc(sizeof *e);
    memset(e, 0, sizeof *e);
    e->oid = new_oid();
    e->schema = xstrdup(schema);
    e->name = xstrdup(st->name);
    e->table = t->oid;
    e->keys = xmalloc((size_t)st->ncols * sizeof(int));
    for (int i = 0; i < st->ncols; i++) {
        int ci = col_index(t, st->cols[i]);
        if (ci < 0) ereport(s, "42703", "column \"%s\" does not exist", st->cols[i]);
        e->keys[i] = ci + 1;
    }
    e->nkeys = st->ncols;
    e->kinds = xstrdup(kinds);
    e->stattarget = -1;
    if (st->nexprs) {
        e->exprs = xmalloc((size_t)st->nexprs * sizeof(char *));
        for (int i = 0; i < st->nexprs; i++) e->exprs[i] = xstrdup(st->exprs[i]);
        e->nexprs = st->nexprs;
    }
    db->stats = xrealloc(db->stats, (size_t)(db->nstats + 1) * sizeof(ExtStat *));
    db->stats[db->nstats++] = e;
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "CREATE STATISTICS");
    return q;
#endif
}

static QueryResult *exec_create_role(Session *s, Node *n)
{
    CreateRoleStmt *st = n->x;
    if (role_find(st->name)) ereport(s, "42710", "role \"%s\" already exists", st->name);
    RoleEntry *e = xmalloc(sizeof *e);
    memset(e, 0, sizeof *e);
    e->oid = new_oid(); e->name = xstrdup(st->name);
    e->canlogin = st->login; e->superuser = st->superuser; e->createdb = st->createdb;
    e->createrole = st->createrole; e->replication = st->replication; e->inherit = st->inherit;
    if (st->has_password && st->password) {
        if (!*st->password)
            notice(s, "00000", "empty string is not a valid password, clearing password");
        else e->password = build_password(s, st->name, st->password);
    }
    G->roles = xrealloc(G->roles, (size_t)(G->nroles + 1) * sizeof(RoleEntry *));
    G->roles[G->nroles++] = e;
    catalog_note(10 /* U_ADD_ROLE */, G->dbs[0], e);
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "CREATE ROLE");
    return q;
}

#if PGVC_VN >= 150
/* A publication row filter may only use columns, constants and immutable
 * built-ins; PostgreSQL checks that when the publication is defined. */
static void check_pub_where(Session *s, Table *t, const char *src, int pos)
{
    static const char *mutable_fns[] = {"random","gen_random_uuid","now","clock_timestamp",
        "timeofday","statement_timestamp","transaction_timestamp","currval","nextval",
        "setval","pg_backend_pid","current_setting","txid_current","pg_current_xact_id", NULL};
    Expr *e = parse_expression_text(s, src);
    if (!e) return;
    /* walk the tree */
    Expr *stack[64]; int sp = 0;
    stack[sp++] = e;
    bool sublink = false, mutfn = false;
    int subpos = 0;
    while (sp) {
        Expr *x = stack[--sp];
        if (!x) continue;
        if (x->tag == X_SUBLINK || x->tag == X_EXISTS || x->tag == X_ANYALL) {
            sublink = true;
            /* positions inside the fragment are relative to it */
            if (x->pos > 1) subpos = x->pos - 1;
        }
        if ((x->tag == X_FUNC || x->tag == X_AGG) && x->name)
            for (int i = 0; mutable_fns[i]; i++)
                if (!strcasecmp(mutable_fns[i], x->name)) mutfn = true;
        if (x->tag == X_COLREF && x->colname && col_index(t, x->colname) < 0)
            ereport_pos(s, "42703", pos, "column \"%s\" does not exist", x->colname);
        for (int i = 0; i < x->nargs && sp < 60; i++) stack[sp++] = x->args[i];
        if (x->testexpr && sp < 60) stack[sp++] = x->testexpr;
        if (x->case_arg && sp < 60) stack[sp++] = x->case_arg;
        if (x->case_else && sp < 60) stack[sp++] = x->case_else;
        for (int i = 0; i < x->nwhen && sp < 58; i++) {
            stack[sp++] = x->when_cond[i];
            stack[sp++] = x->when_res[i];
        }
    }
    if (mutfn) {
        err_detail(s, "User-defined or built-in mutable functions are not allowed.");
        ereport_pos(s, "42P17", pos, "invalid publication WHERE expression");
    }
    if (sublink) {
        err_detail(s, "Only columns, constants, built-in operators, built-in data types, "
                      "built-in collations, and immutable built-in functions are allowed.");
        ereport_pos(s, "42P17", pos + subpos, "invalid publication WHERE expression");
    }
    if (e->tag == X_COLREF) {
        int ci = col_index(t, e->colname);
        if (ci >= 0 && t->cols[ci].type != BOOLOID)
            ereport_pos(s, "42804", pos,
                        "argument of PUBLICATION WHERE must be type boolean, not type %s",
                        format_type_str(t->cols[ci].type, -1));
    }
}
#endif

static QueryResult *exec_create_pub(Session *s, Node *n)
{
    CreatePubStmt *st = n->x;
    Database *db = s->db;
#if PGVC_VN < 100
    ereport(s, "42601", "syntax error at or near \"PUBLICATION\"");
#endif
#if PGVC_VN < 130
    if (st->viaroot) ereport(s, "22023", "unrecognized publication parameter: \"publish_via_partition_root\"");
#endif
    if (st->bad_bool)
        ereport(s, "42601", "%s requires a Boolean value", st->bad_bool);
    if (st->bad_publish)
#if PGVC_VN >= 150
        ereport(s, "22023", "unrecognized value for publication option \"publish\": \"%s\"",
                st->bad_publish);
#else
        ereport(s, "22023", "unrecognized \"publish\" value: \"%s\"", st->bad_publish);
#endif
    if (st->bad_param)
        ereport(s, "22023", PGVC_VN >= 120 ? "unrecognized publication parameter: \"%s\""
                                           : "unrecognized publication parameter: %s",
                st->bad_param);
    for (int i = 0; i < db->npubs; i++)
        if (!strcmp(db->pubs[i]->name, st->name))
            ereport(s, "42710", "publication \"%s\" already exists", st->name);
    Publication *p = xmalloc(sizeof *p);
    memset(p, 0, sizeof *p);
    p->oid = new_oid(); p->name = xstrdup(st->name);
    p->alltables = st->alltables;
    p->pub_insert = st->insert_; p->pub_update = st->update_;
    p->pub_delete = st->delete_; p->pub_truncate = st->truncate_;
    p->viaroot = st->viaroot;
    p->tables = xmalloc((size_t)(st->ntables?st->ntables:1)*sizeof(Oid));
    p->where = xmalloc((size_t)(st->ntables?st->ntables:1)*sizeof(char*));
    p->collist = xmalloc((size_t)(st->ntables?st->ntables:1)*sizeof(char**));
    p->ncollist = xmalloc((size_t)(st->ntables?st->ntables:1)*sizeof(int));
    for (int i = 0; i < st->ntables; i++) {
        Table *t = tbl_find(db, st->tables[i][0], st->tables[i][1]);
        if (!t) ereport(s, "42P01", "relation \"%s\" does not exist", st->tables[i][1]);
        if (t->relkind != 'r' && t->relkind != 'p') {
#if PGVC_VN >= 150
            err_detail(s, "This operation is not supported for %s.",
                       t->relkind == 'v' ? "views" : t->relkind == 'm' ? "materialized views"
                       : t->relkind == 'S' ? "sequences" : "indexes");
            ereport(s, "42809", "cannot add relation \"%s\" to publication", t->name);
#else
            err_detail(s, "Only tables can be added to publications.");
            ereport(s, "42809", "\"%s\" is not a table", t->name);
#endif
        }
#if PGVC_VN >= 150
        if (st->where && st->where[i])
            check_pub_where(s, t, st->where[i], st->wherepos ? st->wherepos[i] : 0);
        /* the column list is checked against the table */
        for (int k = 0; k < st->ncollist[i]; k++) {
            if (col_index(t, st->collist[i][k]) < 0)
                ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                        st->collist[i][k], t->name);
            for (int q = 0; q < k; q++)
                if (!strcmp(st->collist[i][q], st->collist[i][k]))
                    ereport(s, "42701", "duplicate column \"%s\" in publication column list",
                            st->collist[i][k]);
        }
        /* PostgreSQL stores the list in attribute-number order */
        if (st->ncollist[i] > 1) {
            for (int a = 0; a < st->ncollist[i]; a++)
                for (int b2 = a + 1; b2 < st->ncollist[i]; b2++)
                    if (col_index(t, st->collist[i][a]) > col_index(t, st->collist[i][b2])) {
                        char *x = st->collist[i][a];
                        st->collist[i][a] = st->collist[i][b2];
                        st->collist[i][b2] = x;
                    }
        }
#endif
        p->tables[p->ntables] = t->oid;
        p->where[p->ntables] = st->where[i] ? xstrdup(st->where[i]) : NULL;
        p->collist[p->ntables] = st->collist[i];
        p->ncollist[p->ntables] = st->ncollist[i];
#if PGVC_VN < 150
        if (st->where[i]) ereport(s, "42601", "syntax error at or near \"WHERE\"");
        if (st->ncollist[i]) ereport(s, "42601", "syntax error at or near \"(\"");
#endif
        p->ntables++;
    }
    db->pubs = xrealloc(db->pubs, (size_t)(db->npubs + 1) * sizeof(Publication *));
    db->pubs[db->npubs++] = p;
    catalog_note(6 /* U_ADD_PUB */, db, p);
#if PGVC_VN >= 130
    /* this server never runs at wal_level=logical, and PostgreSQL 13 warns
     * about that when a publication is created */
    {
        const char *wl = guc_get(s, "wal_level");
        /* The wording was tightened twice: 15 started quoting the value the
         * setting has to be given, and 17 put the GUC name itself in quotes
         * as part of that release's message-style sweep. */
        if (!wl || strcmp(wl, "logical"))
            warning_hint(s, "01000",
#if PGVC_VN >= 170
                         "Set \"wal_level\" to \"logical\" before creating subscriptions.",
                         "\"wal_level\" is insufficient to publish logical changes");
#elif PGVC_VN >= 150
                         "Set wal_level to \"logical\" before creating subscriptions.",
                         "wal_level is insufficient to publish logical changes");
#else
                         "Set wal_level to logical before creating subscriptions.",
                         "wal_level is insufficient to publish logical changes");
#endif
    }
#endif
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "CREATE PUBLICATION");
    return q;
}

/* ALTER PUBLICATION changes the option set or the table list in place. */
static QueryResult *exec_alter_pub(Session *s, Node *n)
{
    CreatePubStmt *st = n->x;
    Database *db = s->db;
    Publication *p = NULL;
    for (int i = 0; i < db->npubs; i++)
        if (!strcmp(db->pubs[i]->name, st->name)) p = db->pubs[i];
    if (!p) ereport(s, "42704", "publication \"%s\" does not exist", st->name);
    if (st->bad_bool)
        ereport(s, "42601", "%s requires a Boolean value", st->bad_bool);
    if (st->bad_publish)
#if PGVC_VN >= 150
        ereport(s, "22023", "unrecognized value for publication option \"publish\": \"%s\"",
                st->bad_publish);
#else
        ereport(s, "22023", "unrecognized \"publish\" value: \"%s\"", st->bad_publish);
#endif
    if (st->bad_param)
        ereport(s, "22023", PGVC_VN >= 120 ? "unrecognized publication parameter: \"%s\""
                                           : "unrecognized publication parameter: %s",
                st->bad_param);
    if (st->alter_kind == 0 && st->has_publish) {
        if (st->has_publish_opt) {
            p->pub_insert = st->insert_; p->pub_update = st->update_;
            p->pub_delete = st->delete_; p->pub_truncate = st->truncate_;
        }
#if PGVC_VN >= 150
        if (st->has_viaroot && !st->viaroot)
            for (int k = 0; k < p->ntables; k++) {
                Table *pt = tbl_find_oid(db, p->tables[k]);
                if (!pt || pt->relkind != 'p' || !p->where || !p->where[k]) continue;
                err_detail(s, "The publication contains a WHERE clause for partitioned table "
                              "\"%s\", which is not allowed when \"publish_via_partition_root\" "
                              "is false.", pt->name);
                ereport(s, "0A000", "cannot set parameter \"publish_via_partition_root\" to "
                        "false for publication \"%s\"", p->name);
            }
#endif
        if (st->has_viaroot) p->viaroot = st->viaroot;
    } else if (st->alter_kind == 5 && st->newname) {
        free(p->name); p->name = xstrdup(st->newname);
    } else if (st->alter_kind >= 1 && st->alter_kind <= 3) {
        if (p->alltables)
            ereport(s, "42809", "publication \"%s\" is defined as FOR ALL TABLES", p->name);
        if (st->alter_kind == 2) p->ntables = 0;
        for (int i = 0; i < st->ntables; i++) {
            Table *t = tbl_find(db, st->tables[i][0], st->tables[i][1]);
            if (!t) ereport(s, "42P01", "relation \"%s\" does not exist", st->tables[i][1]);
            if (t->relkind != 'r' && t->relkind != 'p') {
#if PGVC_VN >= 150
                err_detail(s, "This operation is not supported for %s.",
                           t->relkind == 'v' ? "views" : t->relkind == 'm' ? "materialized views"
                           : t->relkind == 'S' ? "sequences" : "indexes");
                ereport(s, "42809", "cannot add relation \"%s\" to publication", t->name);
#else
                err_detail(s, "Only tables can be added to publications.");
                ereport(s, "42809", "\"%s\" is not a table", t->name);
#endif
            }
            int at = -1;
            for (int k = 0; k < p->ntables; k++) if (p->tables[k] == t->oid) at = k;
            if (st->alter_kind == 3) {
                if (at < 0)
                    ereport(s, "42704", "relation \"%s\" is not part of the publication",
                            t->name);
                memmove(&p->tables[at], &p->tables[at+1],
                        (size_t)(p->ntables-at-1) * sizeof(Oid));
                p->ntables--;
                continue;
            }
            if (at >= 0) {
                if (st->alter_kind == 1)
                    ereport(s, "42710", "relation \"%s\" is already member of publication \"%s\"",
                            t->name, p->name);
                continue;
            }
#if PGVC_VN >= 150
            if (st->where && st->where[i])
                check_pub_where(s, t, st->where[i], st->wherepos ? st->wherepos[i] : 0);
            for (int k = 0; st->ncollist && k < st->ncollist[i]; k++) {
                if (col_index(t, st->collist[i][k]) < 0)
                    ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                            st->collist[i][k], t->name);
                for (int q = 0; q < k; q++)
                    if (!strcmp(st->collist[i][q], st->collist[i][k]))
                        ereport(s, "42701", "duplicate column \"%s\" in publication column list",
                                st->collist[i][k]);
            }
            if (st->ncollist && st->ncollist[i] > 1)
                for (int a = 0; a < st->ncollist[i]; a++)
                    for (int b2 = a + 1; b2 < st->ncollist[i]; b2++)
                        if (col_index(t, st->collist[i][a]) > col_index(t, st->collist[i][b2])) {
                            char *x = st->collist[i][a];
                            st->collist[i][a] = st->collist[i][b2];
                            st->collist[i][b2] = x;
                        }
#endif
            p->tables = xrealloc(p->tables, (size_t)(p->ntables + 1) * sizeof(Oid));
            p->where = xrealloc(p->where, (size_t)(p->ntables + 1) * sizeof(char *));
            p->collist = xrealloc(p->collist, (size_t)(p->ntables + 1) * sizeof(char **));
            p->ncollist = xrealloc(p->ncollist, (size_t)(p->ntables + 1) * sizeof(int));
            p->where[p->ntables] = (st->where && st->where[i]) ? xstrdup(st->where[i]) : NULL;
            p->collist[p->ntables] = st->collist ? st->collist[i] : NULL;
            p->ncollist[p->ntables] = st->ncollist ? st->ncollist[i] : 0;
            p->tables[p->ntables++] = t->oid;
        }
    }
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "ALTER PUBLICATION");
    return q;
}

/* ------------------------------------------------------------ subscriptions
 * A subscription is the subscriber side of logical replication.  This server
 * has no apply worker, so a subscription never connects and never replicates;
 * what has to be right is the catalog row it leaves behind and the errors
 * PostgreSQL raises before writing that row. */
static Subscription *sub_find(Database *db, const char *name)
{
    for (int i = 0; i < db->nsubs; i++)
        if (!strcmp(db->subs[i]->name, name)) return db->subs[i];
    return NULL;
}

/* origin decides whether changes that already carry a replication origin are
 * forwarded; only two words name that choice, and case does not matter. */
static void check_sub_origin(Session *s, const char *origin)
{
    if (!origin) return;
    if (strcasecmp(origin, "none") && strcasecmp(origin, "any"))
        ereport(s, "42601", "unrecognized origin value: \"%s\"", origin);
}

static void check_sub_options(Session *s, CreateSubStmt *st)
{
#if PGVC_VN < 170
    if (st->has_failover) ereport(s, "22023", "unrecognized subscription parameter: \"failover\"");
#endif
#if PGVC_VN < 160
    if (st->origin) ereport(s, "22023", "unrecognized subscription parameter: \"origin\"");
#endif
    if (st->bad_param)
        ereport(s, "22023", "unrecognized subscription parameter: \"%s\"", st->bad_param);
    if (st->bad_bool)
        ereport(s, "42601", "%s requires a Boolean value", st->bad_bool);
    check_sub_origin(s, st->origin);
}

static QueryResult *exec_create_sub(Session *s, Node *n)
{
    CreateSubStmt *st = n->x;
    Database *db = s->db;
#if PGVC_VN < 100
    ereport(s, "42601", "syntax error at or near \"SUBSCRIPTION\"");
#endif
    check_sub_options(s, st);
    /* connect = false is the only way to define a subscription that does not
     * reach out to the publisher, so the two spellings cannot be combined */
    if (st->has_connect && !st->connect && st->has_enabled && st->enabled)
        ereport(s, "42601", "connect = false and enabled = true are mutually exclusive options");
    if (sub_find(db, st->name))
        ereport(s, "42710", "subscription \"%s\" already exists", st->name);
    /* PostgreSQL dials the publisher at this point and reports whatever libpq
     * says; this server has no client library to dial with, so a subscription
     * can only be defined with connect = false. */
    if (st->connect)
        ereport(s, "08006", "could not connect to the publisher");

    Subscription *sub = xmalloc(sizeof *sub);
    memset(sub, 0, sizeof *sub);
    sub->oid = new_oid();
    sub->name = xstrdup(st->name);
    /* without a connection nothing can start, so the subscription is created
     * disabled unless the statement asked for something else */
    sub->enabled = st->has_enabled ? st->enabled : st->connect;
    sub->binary = st->binary;
    sub->disableonerr = st->disableonerr;
    sub->passwordrequired = true;
    sub->runasowner = false;
    sub->failover = st->failover;
    sub->stream = 'f';
    sub->twophase = 'd';
    sub->conninfo = xstrdup(st->conninfo ? st->conninfo : "");
    /* the slot takes the subscription's name unless the statement names one,
     * or NONE to leave the subscription without a slot */
    sub->slotname = st->has_slotname ? (st->slotname ? xstrdup(st->slotname) : NULL)
                                     : xstrdup(st->name);
    sub->synccommit = xstrdup("off");
    sub->origin = xstrdup(st->origin ? st->origin : "any");
    sub->npubs = st->npubs;
    sub->pubs = xmalloc((size_t)(st->npubs ? st->npubs : 1) * sizeof(char *));
    for (int i = 0; i < st->npubs; i++) sub->pubs[i] = xstrdup(st->pubs[i]);
    db->subs = xrealloc(db->subs, (size_t)(db->nsubs + 1) * sizeof(Subscription *));
    db->subs[db->nsubs++] = sub;
    catalog_note(13 /* U_ADD_SUB */, db, sub);

    warning_hint(s, "55000",
                 "To initiate replication, you must manually create the replication "
                 "slot, enable the subscription, and refresh the subscription.",
                 "subscription was created, but is not connected");
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "CREATE SUBSCRIPTION");
    return q;
}

static QueryResult *exec_alter_sub(Session *s, Node *n)
{
    CreateSubStmt *st = n->x;
    Database *db = s->db;
    Subscription *sub = sub_find(db, st->name);
    if (!sub) ereport(s, "42704", "subscription \"%s\" does not exist", st->name);
    if (st->nopts) {
        check_sub_options(s, st);
#if PGVC_VN >= 170
        /* failover is carried by the slot on the publisher, so there has to be
         * a slot for it to be carried by */
        if (st->has_failover && !sub->slotname)
            ereport(s, "0A000", "cannot set option \"failover\" for a subscription "
                    "that does not have a slot name");
#endif
        if (st->has_binary)   sub->binary = st->binary;
        if (st->has_failover) sub->failover = st->failover;
        if (st->has_slotname) {
            free(sub->slotname);
            sub->slotname = st->slotname ? xstrdup(st->slotname) : NULL;
        }
        if (st->origin) { free(sub->origin); sub->origin = xstrdup(st->origin); }
    }
    if (st->has_enabled) sub->enabled = st->enabled;
    if (st->conninfo) { free(sub->conninfo); sub->conninfo = xstrdup(st->conninfo); }
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "ALTER SUBSCRIPTION");
    return q;
}

static QueryResult *exec_create_func(Session *s, Node *n)
{
    CreateFuncStmt *st = n->x;
    Database *db = s->db;
#if PGVC_VN < 110
    if (st->prokind == 'p') ereport(s, "42601", "syntax error at or near \"PROCEDURE\"");
#endif
    if (st->prokind == 'e' || st->prokind == 'c' || st->prokind == 'r') {
        QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "CREATE TYPE"); return q;
    }
    SqlRoutine *p = xmalloc(sizeof *p);
    memset(p, 0, sizeof *p);
    p->oid = new_oid();
    p->schema = xstrdup(st->schema ? st->schema : "public");
    p->name = xstrdup(st->name);
    p->prokind = st->prokind;
    p->nargs = st->nargs;
    p->argnames = st->argnames;
    p->argmodes = st->argmodes;
    p->argtypes = xmalloc((size_t)(st->nargs?st->nargs:1)*sizeof(Oid));
    for (int i = 0; i < st->nargs; i++) p->argtypes[i] = type_by_name(st->argtypes[i]);
    p->rettype = st->rettype ? type_by_name(st->rettype) : VOIDOID;
    if (st->rettype && !strcasecmp(st->rettype, "trigger")) p->rettype = 2279;
    p->retset = st->retset;
    p->body = st->body ? xstrdup(st->body) : xstrdup("");
    p->lang = st->lang ? xstrdup(st->lang) : xstrdup("sql");
    p->volatility = st->volatility;
    if (strcasecmp(p->lang, "sql") && strcasecmp(p->lang, "plpgsql") &&
        strcasecmp(p->lang, "internal") && strcasecmp(p->lang, "c"))
        ereport(s, "42704", "language \"%s\" does not exist", p->lang);
    if (p->prokind == 'p' && !strcasecmp(p->lang, "plpgsql")) {
        extern int plpgsql_bad_proc_return(Session *s, const char *body);
        int off = plpgsql_bad_proc_return(s, p->body);
        if (off >= 0)
            ereport_pos(s, "42601", st->body_pos + off,
                        "RETURN cannot have a parameter in a procedure");
    }
    for (int i = 0; i < db->nroutines; i++)
        if (!strcmp(db->routines[i]->name, p->name) && db->routines[i]->nargs == p->nargs &&
            ({ bool same = true;
               for (int k = 0; k < p->nargs; k++)
                   if (db->routines[i]->argtypes[k] != p->argtypes[k]) same = false;
               same; })) {
            if (!st->replace) ereport(s, "42723", "%s \"%s\" already exists with same argument types",
                                      st->prokind == 'p' ? "procedure" : "function", p->name);
            db->routines[i] = p;
            QueryResult *q = qr_new();
            snprintf(q->tag, sizeof q->tag, st->prokind == 'p' ? "CREATE PROCEDURE" : "CREATE FUNCTION");
            return q;
        }
    db->routines = xrealloc(db->routines, (size_t)(db->nroutines + 1) * sizeof(SqlRoutine *));
    db->routines[db->nroutines++] = p;
    catalog_note(8 /* U_ADD_ROUTINE */, db, p);
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, st->prokind == 'p' ? "CREATE PROCEDURE" : "CREATE FUNCTION");
    return q;
}

static QueryResult *exec_call(Session *s, Node *n)
{
    CallStmt *st = n->x;
    ExecCtx c0; memset(&c0, 0, sizeof c0); c0.s = s;
    Datum *av = xmalloc((size_t)(st->nargs?st->nargs:1)*sizeof(Datum));
    Oid *at = xmalloc((size_t)(st->nargs?st->nargs:1)*sizeof(Oid));
    for (int i = 0; i < st->nargs; i++) { av[i] = eval(&c0, st->args[i]); at[i] = av[i].type; }
    char *sig = routine_sig(s, at, st->nargs, false);
    SqlRoutine *p = routine_resolve(s->db, st->schema, st->name, at, st->nargs, 'p');
    if (!p) {
        SqlRoutine *f = routine_resolve(s->db, st->schema, st->name, at, st->nargs, 'f');
        if (f) {
            err_hint(s, "To call a function, use SELECT.");
            ereport_pos(s, "42809", st->pos, "%s(%s) is not a procedure", st->name, sig);
        }
        err_hint(s, "No procedure matches the given name and argument types. "
                    "You might need to add explicit type casts.");
        ereport_pos(s, "42883", st->pos, "procedure %s(%s) does not exist", st->name, sig);
    }
    ExecCtx c; memset(&c, 0, sizeof c); c.s = s;
    Datum *args = xmalloc((size_t)(st->nargs?st->nargs:1)*sizeof(Datum));
    for (int i = 0; i < st->nargs; i++) {
        args[i] = av[i];
        if (i < p->nargs) args[i] = coerce_to(s, args[i], p->argtypes[i], -1, 1);
    }
    if (p->lang && !strcasecmp(p->lang, "plpgsql")) {
        Datum *outv = NULL; int nout = 0;
        plpgsql_run(s, p, args, st->nargs, NULL, &outv, &nout);
        QueryResult *q = qr_new();
        if (nout) {
            int k = 0;
            for (int i = 0; i < p->nargs; i++)
                if (p->argmodes && (p->argmodes[i] == 'o' || p->argmodes[i] == 'b'))
                    qr_addcol(q, (p->argnames && p->argnames[i] && *p->argnames[i])
                                 ? p->argnames[i] : "column", p->argtypes[i], -1);
            Datum *row = xmalloc((size_t)nout * sizeof(Datum));
            for (k = 0; k < nout; k++) row[k] = outv[k];
            qr_addrow(q, row);
        }
        snprintf(q->tag, sizeof q->tag, "CALL");
        return q;
    }
    /* run the body statement by statement, with named args as $n */
    Buf out; buf_init(&out);
    for (char *q = p->body; *q; ) {
        if (isalpha((unsigned char)*q) || *q == '_') {
            char *stt = q;
            while (isalnum((unsigned char)*q) || *q == '_') q++;
            char *id = xstrndup(stt, (size_t)(q - stt));
            char *lo = lower_ident(id);
            bool sub = false;
            for (int i = 0; i < p->nargs; i++)
                if (p->argnames && p->argnames[i] && *p->argnames[i] && !strcmp(p->argnames[i], lo))
                    { buf_printf(&out, "$%d", i + 1); sub = true; break; }
            if (!sub) buf_adds(&out, id);
            free(id); free(lo);
            continue;
        }
        buf_addc(&out, *q); q++;
    }
    char *text = buf_steal(&out);
    const char *rest = text;
    while (rest && *rest) {
        const char *next = NULL;
        Node *stmt = parse_sql(s, rest, &next);
        if (stmt->tag == N_EMPTY) break;
        ExecCtx ec; memset(&ec, 0, sizeof ec);
        ec.s = s; ec.params = args; ec.nparams = st->nargs;
        if (stmt->tag == N_SELECT) { exec_select(&ec, stmt->select); }
        else if (stmt->tag == N_INSERT) exec_insert(s, stmt, &ec);
        else if (stmt->tag == N_UPDATE) exec_update(s, stmt, &ec);
        else if (stmt->tag == N_DELETE) exec_delete(s, stmt, &ec);
        else exec_node(s, stmt);
        rest = next;
    }
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "CALL");
    return q;
}

QueryResult *exec_node(Session *s, Node *n)
{
    switch (n->tag) {
    case N_SELECT: {
        ExecCtx c; memset(&c, 0, sizeof c); c.s = s;
        RowSet *rs = exec_select(&c, n->select);
        QueryResult *q = rs_to_qr(s, rs);
        snprintf(q->tag, sizeof q->tag, "SELECT %d", q->nrows);
        return q;
    }
    case N_INSERT: return exec_insert(s, n, NULL);
    case N_UPDATE: return exec_update(s, n, NULL);
    case N_DELETE: return exec_delete(s, n, NULL);
    case N_MERGE:
#if PGVC_VN < 150
        ereport(s, "42601", "syntax error at or near \"MERGE\"");
#endif
        return exec_merge(s, n, NULL);
    case N_CREATE_TABLE: return exec_create_table(s, n);
    case N_CREATE_INDEX: return exec_create_index(s, n);
    case N_CREATE_VIEW:  return exec_create_view(s, n);
    case N_CREATE_ROLE:  return exec_create_role(s, n);
    case N_CREATE_PUB:   return exec_create_pub(s, n);
    case N_CREATE_STATS: return exec_create_stats(s, n);
    case N_VACUUM: {
        VacuumStmt *st = n->x;
        if (st && st->badopt)
            ereport_pos(s, "22023", st->badpos, "unrecognized VACUUM option \"%s\"", st->badopt);
        if (st && st->errkind == 2)
            ereport_pos(s, "42601", st->errpos2,
                        "parallel option requires a value between 0 and 1024");
        if (st && st->errkind == 3)
            ereport_pos(s, "42601", st->errpos2,
                        "parallel workers for vacuum must be between 0 and 1024");
        if (st && st->errkind == 4)
            ereport(s, "42601", "parallel requires an integer value");
        if (st && st->full && st->par_seen && st->parallel > 0)
            ereport(s, "42601", "VACUUM FULL cannot be performed in parallel");
        QueryResult *q = qr_new();
        snprintf(q->tag, sizeof q->tag, "VACUUM");
        return q;
    }
    case N_CREATE_TRIGGER: return exec_create_trigger(s, n);
    case N_DO: {
        DoStmt *st = n->x;
        if (st->lang && strcasecmp(st->lang, "plpgsql"))
            ereport(s, "42704", "language \"%s\" does not exist", st->lang);
        plpgsql_do(s, st->body ? st->body : "");
        QueryResult *q = qr_new();
        snprintf(q->tag, sizeof q->tag, "DO");
        return q;
    }
    case N_ALTER_PUB:    return exec_alter_pub(s, n);
    case N_CREATE_FUNC:  return exec_create_func(s, n);
    case N_CREATE_TYPE:  { QueryResult *q = qr_new(); snprintf(q->tag,sizeof q->tag,"CREATE TYPE"); return q; }
    case N_ALTER_TABLE:  return exec_alter_table(s, n);
    case N_DROP:         return exec_drop(s, n);
    case N_CALL:         return exec_call(s, n);
    case N_EXPLAIN:      return exec_explain(s, n);
    case N_COPY: {
        CopyStmt *cs = n->x;
#if PGVC_VN >= 150
        if (cs->bad_header)
            ereport(s, "22023", "header requires a Boolean value or \"match\"");
        if (cs->header_match && !cs->from)
            ereport(s, "0A000", "cannot use \"match\" with HEADER in COPY TO");
#else
        if (cs->bad_header) ereport(s, "22023", "header requires a Boolean value");
#endif
        return exec_copy(s, n);
    }
    case N_CREATE_SCHEMA: {
        CreateTableStmt *st = n->x;
        if (sch_find(s->db, st->name)) {
            if (st->if_not_exists) { notice(s, "42P06", "schema \"%s\" already exists, skipping", st->name);
                                     QueryResult *q = qr_new(); snprintf(q->tag,sizeof q->tag,"CREATE SCHEMA"); return q; }
            ereport(s, "42P06", "schema \"%s\" already exists", st->name);
        }
        sch_create(s->db, st->name, s->user);
        QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "CREATE SCHEMA"); return q;
    }
    case N_CREATE_SEQ: {
        CreateSeqStmt *st = n->x;
        const char *schema = st->schema ? st->schema : "public";
        if (tbl_find(s->db, schema, st->name)) {
            if (st->if_not_exists) { notice(s, "42P07", "relation \"%s\" already exists, skipping", st->name);
                                     QueryResult *q = qr_new(); snprintf(q->tag,sizeof q->tag,"CREATE SEQUENCE"); return q; }
            ereport(s, "42P07", "relation \"%s\" already exists", st->name);
        }
        int64 inc = st->inc ? st->inc : 1;
        int64 minv = st->has_min ? st->minv : (inc > 0 ? 1 : INT64_MIN);
        int64 maxv = st->has_max ? st->maxv : (inc > 0 ? INT64_MAX : -1);
        int64 start = st->has_start ? st->start : (inc > 0 ? minv : maxv);
        make_sequence(s, schema, st->name, start, inc, minv, maxv, st->cycle);
        QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "CREATE SEQUENCE"); return q;
    }
    case N_CREATE_DB: {
        CreateDbStmt *st = n->x;
        if (db_find(st->name)) ereport(s, "42P04", "database \"%s\" already exists", st->name);
        if (s->xact_state) ereport(s, "25001", "CREATE DATABASE cannot run inside a transaction block");
        db_create(st->name);
        QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "CREATE DATABASE"); return q;
    }
    case N_DROP_DB: { QueryResult *q = qr_new(); snprintf(q->tag,sizeof q->tag,"DROP DATABASE"); return q; }
    case N_TRUNCATE: {
        TruncateStmt *st = n->x;
        for (int i = 0; i < st->nnames; i++) {
            Table *t = tbl_find(s->db, st->names[i][0], st->names[i][1]);
            if (!t) ereport(s, "42P01", "relation \"%s\" does not exist", st->names[i][1]);
            Table **parts = NULL; int nparts = 0;
            collect_partitions(s->db, t, &parts, &nparts);
            for (int p = 0; p < nparts; p++) {
                lock_table_write(s, parts[p]);
                for (int r = 0; r < parts[p]->nrows; r++) parts[p]->rows[r]->xmax = xact_xid(s);
                parts[p]->nrows = 0;
                xact_touch(s, parts[p]);
            }
        }
        QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "TRUNCATE TABLE"); return q;
    }
    case N_TRANSACTION: {
        TransactionStmt *st = n->x;
        QueryResult *q = qr_new();
        switch (st->kind) {
        case 0: case 6:
            if (s->xact_state) warning(s, "25001", "there is already a transaction in progress");
            else { xact_begin(s, st->isolation < 0 ? 0 : st->isolation, st->readonly > 0);
                   catalog_undo_begin(); }
            snprintf(q->tag, sizeof q->tag, "BEGIN"); break;
        case 1:
            if (!s->xact_state) warning(s, "25P01", "there is no transaction in progress");
            if (s->xact_state == 2) { catalog_undo_apply(); xact_rollback(s);
                                       snprintf(q->tag, sizeof q->tag, "ROLLBACK"); break; }
            catalog_undo_clear();
            xact_commit(s);
            snprintf(q->tag, sizeof q->tag, "COMMIT"); break;
        case 2:
            if (!s->xact_state) warning(s, "25P01", "there is no transaction in progress");
            catalog_undo_apply();
            xact_rollback(s);
            snprintf(q->tag, sizeof q->tag, "ROLLBACK"); break;
        case 3:
            if (!s->xact_state) ereport(s, "25P01", "SAVEPOINT can only be used in transaction blocks");
            xact_savepoint(s, st->name);
            snprintf(q->tag, sizeof q->tag, "SAVEPOINT"); break;
        case 4:
            if (!s->xact_state) ereport(s, "25P01", "ROLLBACK TO SAVEPOINT can only be used in transaction blocks");
            xact_rollback_to(s, st->name);
            snprintf(q->tag, sizeof q->tag, "ROLLBACK"); break;
        case 5:
            if (!s->xact_state) ereport(s, "25P01", "RELEASE SAVEPOINT can only be used in transaction blocks");
            xact_release(s, st->name);
            snprintf(q->tag, sizeof q->tag, "RELEASE"); break;
        default:
            if (st->isolation >= 0) s->isolation = st->isolation;
            snprintf(q->tag, sizeof q->tag, "SET"); break;
        }
        return q;
    }
    case N_SET: {
        VarSetStmt *st = n->x;
        QueryResult *q = qr_new();
        if (st->is_local && s->xact_state == 0)
            warning(s, "25P01", "SET LOCAL can only be used in transaction blocks");
        if (st->name && !strcmp(st->name, "role")) {
            const char *v = st->is_default ? "none" : (st->value ? st->value : "none");
            if (!strcasecmp(v, "none")) { free(s->role); s->role = NULL; }
            else {
                if (!role_find(v)) ereport(s, "22023", "role \"%s\" does not exist", v);
                free(s->role); s->role = xstrdup(v);
            }
            snprintf(q->tag, sizeof q->tag, "SET");
            return q;
        }
        const char *sv = st->is_default ? guc_default(st->name)
                                        : (st->value ? st->value : "");
        if (st->is_local && s->xact_state) guc_set_local(s, st->name, sv);
        else guc_set(s, st->name, sv);
        snprintf(q->tag, sizeof q->tag, "SET");
        return q;
    }
    case N_RESET: {
        VarSetStmt *st = n->x;
        QueryResult *q = qr_new();
        if (st->is_all) { s->nguc = 0; free(s->role); s->role = NULL; }
        else if (st->name && !strcmp(st->name, "role")) { free(s->role); s->role = NULL; }
        else guc_set(s, st->name, guc_default(st->name));
        snprintf(q->tag, sizeof q->tag, "RESET");
        return q;
    }
    case N_SHOW: {
        VarSetStmt *st = n->x;
#if PGVC_VN >= 180
        /* PostgreSQL 18 renamed ssl_ecdh_curve; the old name still resolves */
        if (st->name && !strcasecmp(st->name, "ssl_ecdh_curve")) st->name = xstrdup("ssl_groups");
#endif
        if (st->is_all) {
            bool found = false;
            QueryResult *sv = sysview_scan(s, "pg_catalog", "pg_settings", &found);
            QueryResult *q = qr_new();
            qr_addcol(q, "name", TEXTOID, -1);
            qr_addcol(q, "setting", TEXTOID, -1);
            qr_addcol(q, "description", TEXTOID, -1);
            for (int i = 0; i < sv->nrows; i++) {
                Datum *row = xmalloc(3 * sizeof(Datum));
                row[0] = sv->rows[i][0]; row[1] = sv->rows[i][1]; row[2] = d_text(TEXTOID, "");
                qr_addrow(q, row);
            }
            snprintf(q->tag, sizeof q->tag, "SHOW");
            return q;
        }
        if (st->name && !strcmp(st->name, "role"))
            return one_text("role", s->role ? s->role : "none", TEXTOID);
        extern char *guc_show(Session *s, const char *name);
        char *v = guc_show(s, st->name);
        if (!v) ereport(s, "42704", "unrecognized configuration parameter \"%s\"", st->name);
        extern const char *guc_canonical(const char *name);
        QueryResult *q = one_text(guc_canonical(st->name), v, TEXTOID);
        free(v);
        return q;
    }
    case N_ALTER_SYSTEM: {
        QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "ALTER SYSTEM"); return q;
    }
    case N_PREPARE: {
        PrepareStmt *st = n->x;
        PreparedStmt *p = xmalloc(sizeof *p);
        memset(p, 0, sizeof *p);
        p->name = xstrdup(st->name); p->sql = xstrdup(st->src); p->tree = st->query;
        p->nparams = st->nargs;
        p->paramtypes = xmalloc((size_t)(st->nargs?st->nargs:1)*sizeof(Oid));
        for (int i = 0; i < st->nargs; i++) p->paramtypes[i] = type_by_name(st->argtypes[i]);
        for (int i = 0; i < s->nstmts; i++)
            if (!strcmp(s->stmts[i]->name, p->name))
                ereport(s, "42P05", "prepared statement \"%s\" already exists", p->name);
        if (s->nstmts < 64) s->stmts[s->nstmts++] = p;
        QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "PREPARE"); return q;
    }
    case N_EXECUTE: {
        ExecuteStmt *st = n->x;
        PreparedStmt *p = NULL;
        for (int i = 0; i < s->nstmts; i++)
            if (!strcmp(s->stmts[i]->name, st->name)) p = s->stmts[i];
        if (!p) ereport(s, "26000", "prepared statement \"%s\" does not exist", st->name);
        ExecCtx c; memset(&c, 0, sizeof c); c.s = s;
        Datum *args = xmalloc((size_t)(st->nargs?st->nargs:1)*sizeof(Datum));
        for (int i = 0; i < st->nargs; i++) {
            args[i] = eval(&c, st->args[i]);
            if (i < p->nparams && p->paramtypes[i]) args[i] = coerce_to(s, args[i], p->paramtypes[i], -1, 1);
        }
        ExecCtx ec; memset(&ec, 0, sizeof ec);
        ec.s = s; ec.params = args; ec.nparams = st->nargs;
        Node *inner = p->tree;
        if (inner->tag == N_SELECT) {
            RowSet *rs = exec_select(&ec, inner->select);
            QueryResult *q = rs_to_qr(s, rs);
            snprintf(q->tag, sizeof q->tag, "SELECT %d", q->nrows);
            return q;
        }
        if (inner->tag == N_INSERT) return exec_insert(s, inner, &ec);
        if (inner->tag == N_UPDATE) return exec_update(s, inner, &ec);
        if (inner->tag == N_DELETE) return exec_delete(s, inner, &ec);
        if (inner->tag == N_MERGE) return exec_merge(s, inner, &ec);
        return exec_node(s, inner);
    }
    case N_DEALLOCATE: {
        ExecuteStmt *st = n->x;
        if (!st->name) s->nstmts = 0;
        else for (int i = 0; i < s->nstmts; i++)
            if (!strcmp(s->stmts[i]->name, st->name)) {
                memmove(&s->stmts[i], &s->stmts[i+1], (size_t)(s->nstmts-i-1)*sizeof(PreparedStmt*));
                s->nstmts--; break;
            }
        QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "DEALLOCATE"); return q;
    }
    case N_REINDEX: {
        ReindexStmt *st = n->x;
        QueryResult *q = qr_new();
        if (st->concurrent) {
#if PGVC_VN < 120
            ereport(s, "42601", "syntax error at or near \"CONCURRENTLY\"");
#else
            bool sysschema = st->kind && !strcasecmp(st->kind, "schema") &&
                             st->name && !strcmp(st->name, "pg_catalog");
            if (sysschema) {
                warning(s, "0A000", "cannot reindex system catalogs concurrently, skipping all");
                snprintf(q->tag, sizeof q->tag, "REINDEX");
                return q;
            }
            if (st->name && !strncmp(st->name, "pg_", 3))
                ereport(s, "0A000", "cannot reindex system catalogs concurrently");
            if (s->xact_state == 1)
                ereport(s, "25001", "REINDEX CONCURRENTLY cannot run inside a transaction block");
            if (st->name && st->kind &&
                (!strcasecmp(st->kind, "index") || !strcasecmp(st->kind, "table")) &&
                !idx_find(s->db, st->name) && !tbl_find(s->db, NULL, st->name))
                ereport(s, "42P01", "relation \"%s\" does not exist", st->name);
#endif
        }
        if (st->name && st->kind && !strcmp(st->kind, "index") && !idx_find(s->db, st->name))
            ereport(s, "42704", "index \"%s\" does not exist", st->name);
        snprintf(q->tag, sizeof q->tag, "REINDEX");
        return q;
    }
    case N_GRANT: {
        GrantStmt *st = n->x;
        QueryResult *q = qr_new();
        if (st->objtype && !strcmp(st->objtype, "schema"))
            for (int i = 0; i < st->nobjs; i++) {
                Schema *sc = sch_find(s->db, st->objs[i]);
                if (!sc) ereport(s, "3F000", "schema \"%s\" does not exist", st->objs[i]);
                int nr = st->nroles;
                for (int k = 0; k < nr; k++) {
                    if (strcmp(st->roles[k], "public") && !role_find(st->roles[k]))
                        ereport(s, "42704", "role \"%s\" does not exist", st->roles[k]);
                    schema_grant(sc, st->roles[k], st->privs, st->is_grant);
                }
            }
        if (st->objtype && (!strcmp(st->objtype, "table") || !strcmp(st->objtype, "alltables"))) {
            int no = st->nobjs, nr = st->nroles;
            for (int k = 0; k < nr; k++)
                if (strcmp(st->roles[k], "public") && !role_find(st->roles[k]))
                    ereport(s, "42704", "role \"%s\" does not exist", st->roles[k]);
            if (!strcmp(st->objtype, "alltables")) {
                int nt = s->db->ntables;
                for (int j = 0; j < nt; j++) {
                    Table *t = s->db->tables[j];
                    if (t->relkind == 'i' || t->relkind == 'I') continue;
                    for (int k = 0; k < nr; k++) table_grant(t, st->roles[k], st->privs, st->is_grant);
                }
            } else for (int i = 0; i < no; i++) {
                Table *t = tbl_find(s->db, NULL, st->objs[i]);
                if (!t) ereport(s, "42P01", "relation \"%s\" does not exist", st->objs[i]);
                for (int k = 0; k < nr; k++) table_grant(t, st->roles[k], st->privs, st->is_grant);
            }
        }
        snprintf(q->tag, sizeof q->tag, st->is_grant ? "GRANT" : "REVOKE");
        return q;
    }
    case N_CREATE_DOMAIN: {
        CreateDomainStmt *st = n->x;
        if (!st->type) ereport(s, "42704", "type \"%s\" does not exist", st->typename);
        domain_create(s, st->name, st->type, st->typmod, st->checksrc, st->notnull);
        QueryResult *q = qr_new(); snprintf(q->tag, sizeof q->tag, "CREATE DOMAIN"); return q;
    }
    case N_CREATE_SUB:   return exec_create_sub(s, n);
    case N_ALTER_SUB:    return exec_alter_sub(s, n);
    case N_IDENT_SYS: case N_START_REPL: case N_CREATE_REPL_SLOT: case N_DROP_REPL_SLOT:
    case N_BASE_BACKUP: case N_TIMELINE_HISTORY: case N_SHOW_REPL: case N_READ_REPL_SLOT:
    case N_UPLOAD_MANIFEST:
        return exec_repl(s, n);
    case N_DECLARE: {
        /* A cursor and an extended-protocol portal are the same object in
         * PostgreSQL, so DECLARE registers a portal the Describe/Execute path
         * can also see. */
        CursorStmt *st = n->x;
        ExecCtx c; memset(&c, 0, sizeof c); c.s = s;
        RowSet *rs = exec_select(&c, st->query->select);
        Portal *po = xmalloc(sizeof *po);
        memset(po, 0, sizeof *po);
        po->name = xstrdup(st->name);
        po->res = rs_to_qr(s, rs);
        po->is_cursor = true;
        for (int i = 0; i < s->nportals; i++)
            if (!strcmp(s->portals[i]->name, st->name)) { s->portals[i] = po; goto declared; }
        if (s->nportals < 64) s->portals[s->nportals++] = po;
    declared:;
        QueryResult *q = qr_new();
        snprintf(q->tag, sizeof q->tag, "DECLARE CURSOR");
        return q;
    }
    case N_FETCH: {
        CursorStmt *st = n->x;
        Portal *po = NULL;
        for (int i = 0; i < s->nportals; i++)
            if (!strcmp(s->portals[i]->name, st->name)) po = s->portals[i];
        if (!po || !po->res) ereport(s, "34000", "cursor \"%s\" does not exist", st->name);
        QueryResult *src = po->res;
        QueryResult *q = qr_new();
        for (int i = 0; i < src->ncols; i++)
            qr_addcol(q, src->colnames[i], src->coltypes[i], src->coltypmods[i]);
        long want = (st->count < 0) ? src->nrows : st->count;
        long got = 0;
        if (st->seek) {
            /* FIRST / LAST / ABSOLUTE n / RELATIVE n reposition, then fetch one */
            long pos;
            if (st->seek == 1) pos = 0;
            else if (st->seek == 2) pos = src->nrows - 1;
            else if (st->seek == 3) pos = (st->seekn > 0) ? st->seekn - 1
                                        : (st->seekn < 0) ? src->nrows + st->seekn : -1;
            else pos = (long)po->next_row + st->seekn - (st->seekn > 0 ? 1 : 0);
            want = (st->seek == 3 && st->seekn == 0) ? 0 : 1;
            if (pos < 0) { pos = 0; if (st->seek == 3 && st->seekn < 0) want = 0; }
            if (pos >= src->nrows) { pos = src->nrows; want = 0; }
            po->next_row = (int)pos;
            /* once positioned, the fetch itself is always forward */
            while (got < want && po->next_row < src->nrows) {
                qr_addrow(q, src->rows[po->next_row++]); got++;
            }
            if (st->all) { q->ncols = 0; snprintf(q->tag, sizeof q->tag, "MOVE %ld", got); return q; }
            snprintf(q->tag, sizeof q->tag, "FETCH %ld", got);
            return q;
        }
        if (st->dir == 0) {
            while (got < want && po->next_row < src->nrows) { qr_addrow(q, src->rows[po->next_row++]); got++; }
        } else {
            while (got < want && po->next_row > 0) { qr_addrow(q, src->rows[--po->next_row]); got++; }
        }
        if (st->all) { q->ncols = 0; snprintf(q->tag, sizeof q->tag, "MOVE %ld", got); return q; }
        snprintf(q->tag, sizeof q->tag, "FETCH %ld", got);
        return q;
    }
    case N_CLOSE_CURSOR: {
        CursorStmt *st = n->x;
        if (st->all) s->nportals = 0;
        else {
            bool found = false;
            for (int i = 0; i < s->nportals; i++)
                if (!strcmp(s->portals[i]->name, st->name)) {
                    memmove(&s->portals[i], &s->portals[i+1],
                            (size_t)(s->nportals-i-1)*sizeof(Portal*));
                    s->nportals--; found = true; break;
                }
            if (!found) ereport(s, "34000", "cursor \"%s\" does not exist", st->name);
        }
        QueryResult *q = qr_new();
        snprintf(q->tag, sizeof q->tag, "CLOSE CURSOR");
        return q;
    }
    case N_CHECKPOINT: { store_save(s->db); QueryResult *q = qr_new();
                         snprintf(q->tag, sizeof q->tag, "CHECKPOINT"); return q; }
    case N_EMPTY: { QueryResult *q = qr_new(); q->tag[0] = '\0'; return q; }
    case N_ALTER_ROLE: {
        CreateRoleStmt *st = n->x;
        RoleEntry *e = st->name ? role_find(st->name) : NULL;
        if (st->name && !e && strcasecmp(st->name, "all"))
            ereport(s, "42704", "role \"%s\" does not exist", st->name);
        if (e && st->has_password) {
            if (!st->password) e->password = NULL;
            else if (!*st->password)
                notice(s, "00000", "empty string is not a valid password, clearing password");
            else e->password = build_password(s, st->name, st->password);
        }
        if (e) {
            if (st->login) e->canlogin = true;
            if (st->superuser) e->superuser = true;
            if (st->createdb) e->createdb = true;
            if (st->createrole) e->createrole = true;
            if (st->replication) e->replication = true;
        }
        QueryResult *q = qr_new();
        snprintf(q->tag, sizeof q->tag, "ALTER ROLE");
        return q;
    }
    case N_ANALYZE: case N_DISCARD: case N_LOCK: case N_COMMENT:
    case N_ALTER_SEQ: {
        QueryResult *q = qr_new();
        const char *kw = (const char *)n->x;
        if (n->tag == N_ANALYZE && kw) {
            char up[32]; snprintf(up, sizeof up, "%s", kw);
            for (char *p = up; *p; p++) *p = (char)toupper((unsigned char)*p);
            snprintf(q->tag, sizeof q->tag, "%s", up);
        } else if (n->tag == N_LOCK) snprintf(q->tag, sizeof q->tag, "LOCK TABLE");
        else if (n->tag == N_COMMENT) snprintf(q->tag, sizeof q->tag, "COMMENT");
        else if (n->tag == N_DISCARD) snprintf(q->tag, sizeof q->tag, "DISCARD ALL");
        else if (n->tag == N_ALTER_ROLE) snprintf(q->tag, sizeof q->tag, "ALTER ROLE");
        else if (n->tag == N_ALTER_PUB) snprintf(q->tag, sizeof q->tag, "ALTER PUBLICATION");
        else snprintf(q->tag, sizeof q->tag, "CREATE");
        return q;
    }
    default: {
        QueryResult *q = qr_new();
        snprintf(q->tag, sizeof q->tag, "OK");
        return q;
    }
    }
}

QueryResult *exec_sql(Session *s, const char *sql)
{
    s->eff_role = NULL;          /* a failed view body may have left it set */
    Node *n = parse_sql(s, sql, NULL);
    return exec_node(s, n);
}

/* helper used by the COPY implementation */
RowSet *copy_source_rows(Session *s, Node *q, int *ncols, char ***names, Oid **types)
{
    ExecCtx c; memset(&c, 0, sizeof c); c.s = s;
    RowSet *rs = exec_select(&c, q->select);
    *ncols = rs->ncols;
    *names = xmalloc((size_t)(rs->ncols?rs->ncols:1)*sizeof(char*));
    *types = xmalloc((size_t)(rs->ncols?rs->ncols:1)*sizeof(Oid));
    for (int i = 0; i < rs->ncols; i++) { (*names)[i] = rs->cols[i].name; (*types)[i] = rs->cols[i].type; }
    return rs;
}
/* COPY FROM ... WHERE: evaluate the condition against the row being read. */
bool copy_eval_where(Session *s, Table *t, Expr *w, Datum *vals)
{
    RowSet rs; memset(&rs, 0, sizeof rs);
    rs.ncols = t->ncols;
    rs.cols = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(ColMeta));
    for (int k = 0; k < t->ncols; k++) {
        rs.cols[k].rel = t->name; rs.cols[k].name = t->cols[k].name;
        rs.cols[k].type = t->cols[k].type; rs.cols[k].typmod = t->cols[k].typmod;
        rs.cols[k].hidden = false;
    }
    ExecCtx c; memset(&c, 0, sizeof c);
    c.s = s; c.rs = &rs; c.row = vals;
    Datum d = to_bool(s, eval(&c, w));
    free(rs.cols);
    return !d.isnull && d.v.i != 0;
}

/* The leftmost character position an expression covers. */
int expr_left_pos(Expr *e)
{
    if (!e) return 0;
    int best = e->pos;
    for (int i = 0; i < e->nargs; i++) {
        int p = expr_left_pos(e->args[i]);
        if (p && (!best || p < best)) best = p;
    }
    if (e->testexpr) { int p = expr_left_pos(e->testexpr); if (p && (!best || p < best)) best = p; }
    return best;
}

/* Every column the COPY FROM condition names must exist, and PostgreSQL says
 * so before it starts reading data. */
void copy_check_where_cols(Session *s, Table *t, Expr *w)
{
    if (!w) return;
    if (w->tag == X_COLREF) {
        if (col_index(t, w->colname) < 0)
            ereport_pos(s, "42703", w->pos, "column \"%s\" does not exist", w->colname);
        return;
    }
    for (int i = 0; i < w->nargs; i++) copy_check_where_cols(s, t, w->args[i]);
    for (int i = 0; i < w->nwhen; i++) {
        copy_check_where_cols(s, t, w->when_cond[i]);
        copy_check_where_cols(s, t, w->when_res[i]);
    }
    copy_check_where_cols(s, t, w->case_arg);
    copy_check_where_cols(s, t, w->case_else);
    copy_check_where_cols(s, t, w->testexpr);
}

bool expr_has_aggregate(Expr *e) { return expr_has_agg(e); }
bool expr_has_subquery(Expr *e)
{
    if (!e) return false;
    if (e->tag == X_SUBLINK || e->tag == X_EXISTS) return true;
    for (int i = 0; i < e->nargs; i++) if (expr_has_subquery(e->args[i])) return true;
    for (int i = 0; i < e->nwhen; i++)
        if (expr_has_subquery(e->when_cond[i]) || expr_has_subquery(e->when_res[i])) return true;
    if (expr_has_subquery(e->case_arg) || expr_has_subquery(e->case_else)) return true;
    if (expr_has_subquery(e->testexpr) || expr_has_subquery(e->filter)) return true;
    return false;
}

int rowset_nrows(RowSet *rs) { return rs->nrows; }
Datum *rowset_row(RowSet *rs, int i) { return rs->rows[i]; }
void insert_one_row(Session *s, Table *t, Datum *vals) { insert_row_into(s, t, vals, NULL); }
Datum table_default(Session *s, Table *t, int ci, Datum *row) { return eval_default(s, t, &t->cols[ci], row); }
void table_apply_generated(Session *s, Table *t, Datum *vals) { apply_generated(s, t, vals); }

/* -------------------------------------------------- persistence helpers */
Table *store_mk_table(Database *db, const char *schema, const char *name, char kind)
{
    Table *t = mk_table(db, schema, name, kind);
    tbl_add(db, t);
    return t;
}
void store_add_col(Table *t, const char *name, Oid type, int32 typmod, bool notnull, const char *def)
{
    add_col(t, name, type, typmod);
    t->cols[t->ncols-1].notnull = notnull;
    t->cols[t->ncols-1].defexpr = def ? xstrdup(def) : NULL;
}
void store_add_row(Session *s, Table *t, char **vals, int n)
{
    Row *r = xmalloc(sizeof *r);
    memset(r, 0, sizeof *r);
    r->xmin = 1; r->n = t->ncols;
    r->vals = xmalloc((size_t)(t->ncols?t->ncols:1) * sizeof(Datum));
    for (int i = 0; i < t->ncols; i++) {
        if (i >= n || !strcmp(vals[i], "\\N")) { r->vals[i] = d_null(t->cols[i].type); continue; }
        Buf b; buf_init(&b);
        for (char *q = vals[i]; *q; q++) {
            if (*q == '\\' && q[1] == 'n') { buf_addc(&b, '\n'); q++; }
            else if (*q == '\\' && q[1] == 't') { buf_addc(&b, '\t'); q++; }
            else if (*q == '\\' && q[1] == '\\') { buf_addc(&b, '\\'); q++; }
            else buf_addc(&b, *q);
        }
        r->vals[i] = in_func(s, t->cols[i].type, t->cols[i].typmod, b.data ? b.data : "", (int32)b.len);
        buf_free(&b);
    }
    if (t->nrows == t->caprows) { t->caprows = t->caprows ? t->caprows*2 : 16;
                                  t->rows = xrealloc(t->rows, (size_t)t->caprows*sizeof(Row*)); }
    t->rows[t->nrows++] = r;
}
void store_add_view(Session *s, Database *db, const char *sch, const char *nm, const char *def)
{
    Table *t = mk_table(db, sch, nm, 'v');
    t->viewdef = xstrdup(def);
    Node *n = parse_sql(s, def, NULL);
    ExecCtx c; memset(&c, 0, sizeof c); c.s = s;
    RowSet *rs = exec_select(&c, n->select);
    for (int i = 0; i < rs->ncols; i++)
        add_col(t, rs->cols[i].name, rs->cols[i].type, rs->cols[i].typmod);
    tbl_add(db, t);
}
void store_add_seq(Database *db, const char *sch, const char *nm, int64 last, int64 start,
                   int64 inc, int64 mx, bool called)
{
    Table *q = mk_table(db, sch, nm, 'S');
    add_col(q, "last_value", INT8OID, -1);
    q->seq_last = last; q->seq_start = start; q->seq_inc = inc;
    q->seq_max = mx; q->seq_min = 1; q->seq_cache = 1; q->seq_called = called;
    tbl_add(db, q);
}

QueryResult *exec_with_params(Session *s, Node *n, Datum *params, int nparams)
{
    ExecCtx c; memset(&c, 0, sizeof c);
    c.s = s; c.params = params; c.nparams = nparams;
    switch (n->tag) {
    case N_SELECT: {
        RowSet *rs = exec_select(&c, n->select);
        QueryResult *q = rs_to_qr(s, rs);
        snprintf(q->tag, sizeof q->tag, "SELECT %d", q->nrows);
        return q;
    }
    case N_INSERT: return exec_insert(s, n, &c);
    case N_UPDATE: return exec_update(s, n, &c);
    case N_DELETE: return exec_delete(s, n, &c);
    case N_MERGE:
#if PGVC_VN < 150
        ereport(s, "42601", "syntax error at or near \"MERGE\"");
#endif
        return exec_merge(s, n, &c);
    default: return exec_node(s, n);
    }
}

/* Lets support code evaluate an expression in the executor's context. */
Datum eval_thunk(void *ctx, Expr *e) { return eval((ExecCtx *)ctx, e); }

/* pg_get_viewdef(): PostgreSQL re-prints the view query from its parse tree,
 * one target-list entry to a line with the FROM clause below them. */
char *view_def_text(Session *s, const char *src, bool pretty)
{
    (void)s;
    if (!src) return NULL;
    const char *p = src;
    while (*p == ' ' || *p == '\n' || *p == '\t') p++;
    if (strncasecmp(p, "select", 6)) return xstrdup(src);
    p += 6;
    while (*p == ' ' || *p == '\n' || *p == '\t') p++;
    /* find the top-level FROM */
    const char *from = NULL;
    int depth = 0; bool inq = false;
    for (const char *q = p; *q; q++) {
        if (inq) { if (*q == '\'') inq = false; continue; }
        if (*q == '\'') { inq = true; continue; }
        if (*q == '(') depth++;
        else if (*q == ')') depth--;
        else if (depth == 0 && (q == p || !isalnum((unsigned char)q[-1])) &&
                 !strncasecmp(q, "from", 4) && !isalnum((unsigned char)q[4]) && q[4] != '_') {
            from = q; break;
        }
    }
    const char *tend = from ? from : src + strlen(src);
    Buf out; buf_init(&out);
    buf_adds(&out, " SELECT ");
    /* split the target list on top-level commas */
    depth = 0; inq = false;
    const char *st = p;
    int n = 0;
    for (const char *q = p; q <= tend; q++) {
        bool last = (q == tend);
        if (!last) {
            if (inq) { if (*q == '\'') inq = false; continue; }
            if (*q == '\'') { inq = true; continue; }
            if (*q == '(') { depth++; continue; }
            if (*q == ')') { depth--; continue; }
            if (!(depth == 0 && *q == ',')) continue;
        }
        const char *e = q;
        while (e > st && (e[-1] == ' ' || e[-1] == '\n' || e[-1] == '\t')) e--;
        char *item = xstrndup(st, (size_t)(e - st));
        char *norm = json_ctor_spacing(item);
        if (n) buf_adds(&out, ",\n    ");
        buf_adds(&out, norm ? norm : item);
        free(norm); free(item);
        n++;
        if (last) break;
        st = q + 1;
        while (*st == ' ' || *st == '\n' || *st == '\t') st++;
    }
    if (from) {
        const char *e = src + strlen(src);
        while (e > from && (e[-1] == ' ' || e[-1] == '\n' || e[-1] == '\t' || e[-1] == ';')) e--;
        buf_adds(&out, "\n   ");
        buf_add(&out, from, (size_t)(e - from));
    }
    buf_addc(&out, ';');
    (void)pretty;
    return buf_steal(&out);
}

/* Apply a domain's constraints to a value that already has the base type. */
void domain_check(Session *s, Oid oid, Datum v)
{
    for (int guard = 0; guard < 16; guard++) {
        Oid base = 0; const char *checksrc = NULL, *conname = NULL, *dname = NULL;
        bool notnull = false;
        if (!domain_step(oid, &base, &checksrc, &conname, &dname, &notnull)) return;
        if (notnull && v.isnull)
            ereport(s, "23502", "domain %s does not allow null values", dname);
        if (checksrc && !v.isnull) {
            Expr *e = parse_expr_string(s, checksrc);
            RowSet rs; memset(&rs, 0, sizeof rs);
            ColMeta cm; memset(&cm, 0, sizeof cm);
            cm.rel = NULL; cm.name = (char *)"value"; cm.type = v.type; cm.typmod = -1;
            rs.ncols = 1; rs.cols = &cm;
            Datum row[1]; row[0] = v;
            ExecCtx c; memset(&c, 0, sizeof c);
            c.s = s; c.rs = &rs; c.row = row;
            Datum r = to_bool(s, eval(&c, e));
            if (!r.isnull && !r.v.i)
                ereport(s, "23514", "value for domain %s violates check constraint \"%s\"",
                        dname, conname);
        }
        oid = base;
    }
}
