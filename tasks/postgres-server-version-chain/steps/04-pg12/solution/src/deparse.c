/* deparse.c - render a parsed expression the way ruleutils.c does.
 *
 * pg_get_expr(), the generation_expression of information_schema.columns and
 * pg_get_statisticsobjdef() all show the parsed node tree, not the text the
 * user typed: operators come back spaced and parenthesised, identifiers come
 * back folded and quoted only where they have to be.  Everything here works on
 * the tree the ordinary parser builds, so the two cannot drift apart. */
#include "engine.h"
#include "stmt.h"

static void dp_expr(Session *s, Buf *b, Expr *e, bool qualify, const char *rel);

/* An identifier needs quoting unless it is all lower-case, starts with a
 * letter or underscore, and is not a reserved word. */
static void dp_ident(Buf *b, const char *id)
{
    extern bool kw_is_reserved(const char *lower);
    bool need = !id || !*id;
    if (!need) {
        if (!(islower((unsigned char)id[0]) || id[0] == '_')) need = true;
        for (const char *p = id; *p && !need; p++)
            if (!(islower((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '_'))
                need = true;
        if (!need && kw_is_reserved(id)) need = true;
    }
    if (!need) { buf_adds(b, id); return; }
    buf_addc(b, '"');
    for (const char *p = id; p && *p; p++) {
        if (*p == '"') buf_addc(b, '"');
        buf_addc(b, *p);
    }
    buf_addc(b, '"');
}

static void dp_literal(Buf *b, const char *t)
{
    buf_addc(b, '\'');
    for (const char *p = t; p && *p; p++) {
        if (*p == '\'') buf_addc(b, '\'');
        buf_addc(b, *p);
    }
    buf_addc(b, '\'');
}

static void dp_args(Session *s, Buf *b, Expr *e, bool qualify, const char *rel)
{
    for (int i = 0; i < e->nargs; i++) {
        if (i) buf_adds(b, ", ");
        dp_expr(s, b, e->args[i], qualify, rel);
    }
}

#if PGVC_VN >= 160
/* The trailing clauses of a SQL/JSON constructor.  ruleutils always spells the
 * RETURNING type out, even the json it would have defaulted to, and spells an
 * ON NULL clause only when it is not that constructor's own default. */
static void dp_json_tail(Buf *b, Expr *e, bool arrayish)
{
    if (arrayish) { if (!e->json_absent_on_null) buf_adds(b, " NULL ON NULL"); }
    else if (e->json_absent_on_null) buf_adds(b, " ABSENT ON NULL");
    if (e->isjson_unique) buf_adds(b, " WITH UNIQUE KEYS");
    char *tn = format_type_str(e->casttype ? e->casttype : JSONOID,
                               e->casttype ? e->casttypmod : -1);
    buf_printf(b, " RETURNING %s", tn ? tn : "json");
    free(tn);
}

/* JSON_OBJECT and JSON_OBJECTAGG print their pairs as "key : value". */
static void dp_json_pairs(Session *s, Buf *b, Expr *e, bool qualify, const char *rel)
{
    for (int i = 0; i + 1 < e->nargs; i += 2) {
        if (i) buf_adds(b, ", ");
        dp_expr(s, b, e->args[i], qualify, rel);
        buf_adds(b, " : ");
        dp_expr(s, b, e->args[i+1], qualify, rel);
    }
}
#endif

static void dp_expr(Session *s, Buf *b, Expr *e, bool qualify, const char *rel)
{
    if (!e) return;
    switch (e->tag) {
    case X_CONST:
        if (e->isnull_const) { buf_adds(b, "NULL"); return; }
        if (e->val.type == BOOLOID && !e->strval) { buf_adds(b, e->val.v.i ? "true" : "false"); return; }
        if (e->strval && (e->val.type == INT2OID || e->val.type == INT4OID ||
                          e->val.type == INT8OID || e->val.type == NUMERICOID ||
                          e->val.type == FLOAT8OID)) {
            buf_adds(b, e->strval);
            return;
        }
        if (e->strval) { dp_literal(b, e->strval); return; }
        buf_adds(b, "NULL");
        return;
    case X_PARAM: buf_printf(b, "$%d", e->paramno); return;
    case X_COLREF:
        if (e->qual2) { dp_ident(b, e->qual2); buf_addc(b, '.'); }
        if (e->qual) { dp_ident(b, e->qual); buf_addc(b, '.'); }
        else if (qualify && rel) { dp_ident(b, rel); buf_addc(b, '.'); }
        dp_ident(b, e->colname);
        return;
    case X_STAR: buf_addc(b, '*'); return;
    case X_OP:
    case X_FUNC: {
        const char *nm = e->name ? e->name : "";
        bool isop = nm[0] && !isalpha((unsigned char)nm[0]) && nm[0] != '_';
        if (isop && e->nargs == 2) {
            buf_addc(b, '(');
            dp_expr(s, b, e->args[0], qualify, rel);
            buf_printf(b, " %s ", nm);
            dp_expr(s, b, e->args[1], qualify, rel);
            buf_addc(b, ')');
            return;
        }
        if (isop && e->nargs == 1) {
            if (!strcmp(nm, "!post")) {
                dp_expr(s, b, e->args[0], qualify, rel);
                buf_adds(b, " !");
                return;
            }
            buf_addc(b, '(');
            buf_adds(b, nm);
            dp_expr(s, b, e->args[0], qualify, rel);
            buf_addc(b, ')');
            return;
        }
        if (e->nspname) { dp_ident(b, e->nspname); buf_addc(b, '.'); }
        dp_ident(b, nm);
        buf_addc(b, '(');
        if (e->agg_distinct) buf_adds(b, "DISTINCT ");
        if (e->agg_star) buf_addc(b, '*');
        else dp_args(s, b, e, qualify, rel);
        buf_addc(b, ')');
        return;
    }
    case X_AGG: {
#if PGVC_VN >= 160
        /* JSON_ARRAYAGG / JSON_OBJECTAGG are grammar productions, not ordinary
         * aggregates, so ruleutils prints them in their own spelling. */
        if (e->name && (!strcmp(e->name, "json_arrayagg") ||
                        !strcmp(e->name, "json_objectagg"))) {
            bool arr = !strcmp(e->name, "json_arrayagg");
            buf_adds(b, arr ? "JSON_ARRAYAGG(" : "JSON_OBJECTAGG(");
            if (arr) dp_args(s, b, e, qualify, rel);
            else dp_json_pairs(s, b, e, qualify, rel);
            for (int i = 0; i < e->agg_norder; i++) {
                buf_adds(b, i ? ", " : " ORDER BY ");
                dp_expr(s, b, e->agg_order[i].e, qualify, rel);
                if (e->agg_order[i].dir) buf_adds(b, " DESC");
            }
            dp_json_tail(b, e, arr);
            buf_addc(b, ')');
            return;
        }
#endif
        if (e->nspname) { dp_ident(b, e->nspname); buf_addc(b, '.'); }
        dp_ident(b, e->name ? e->name : "");
        buf_addc(b, '(');
        if (e->agg_distinct) buf_adds(b, "DISTINCT ");
        if (e->agg_star) buf_addc(b, '*');
        else dp_args(s, b, e, qualify, rel);
        buf_addc(b, ')');
        return;
    }
    case X_AND:
    case X_OR: {
        buf_addc(b, '(');
        for (int i = 0; i < e->nargs; i++) {
            if (i) buf_adds(b, e->tag == X_AND ? " AND " : " OR ");
            dp_expr(s, b, e->args[i], qualify, rel);
        }
        buf_addc(b, ')');
        return;
    }
    case X_NOT:
        buf_adds(b, "(NOT ");
        dp_expr(s, b, e->args[0], qualify, rel);
        buf_addc(b, ')');
        return;
    case X_IS: {
        buf_addc(b, '(');
        dp_expr(s, b, e->args[0], qualify, rel);
        static const char *w[] = {" IS NULL"," IS NOT NULL"," IS TRUE"," IS NOT TRUE",
                                  " IS FALSE"," IS NOT FALSE"," IS UNKNOWN"," IS NOT UNKNOWN"};
        if (e->is_kind >= 0 && e->is_kind <= 7) buf_adds(b, w[e->is_kind]);
        else {
            buf_adds(b, e->is_kind == 8 ? " IS DISTINCT FROM " : " IS NOT DISTINCT FROM ");
            if (e->nargs > 1) dp_expr(s, b, e->args[1], qualify, rel);
        }
        buf_addc(b, ')');
        return;
    }
    case X_ISJSON:
        buf_addc(b, '(');
        dp_expr(s, b, e->args[0], qualify, rel);
        buf_adds(b, e->isjson_not ? " IS NOT JSON" : " IS JSON");
        if (e->isjson_kind == 1) buf_adds(b, " ARRAY");
        else if (e->isjson_kind == 2) buf_adds(b, " OBJECT");
        else if (e->isjson_kind == 3) buf_adds(b, " SCALAR");
        if (e->isjson_unique) buf_adds(b, " WITH UNIQUE KEYS");
        buf_addc(b, ')');
        return;
#if PGVC_VN >= 160
    case X_JSONCTOR: {
        /* the SQL/JSON query functions of 17 are not modelled here; leaving
         * the buffer untouched sends a view body to the text re-flow instead
         * of printing a hole. */
        if (e->json_ctor >= 7) return;
        const char *jn = e->name ? e->name : "json_array";
        for (const char *jp = jn; *jp; jp++)
            buf_addc(b, (char)toupper((unsigned char)*jp));
        buf_addc(b, '(');
        if (e->json_ctor == 1) dp_json_pairs(s, b, e, qualify, rel);
        else dp_args(s, b, e, qualify, rel);
        dp_json_tail(b, e, e->json_ctor != 1);
        buf_addc(b, ')');
        return;
    }
#endif
    case X_CAST: {
        char *tn = format_type_str(e->casttype, e->casttypmod);
        dp_expr(s, b, e->args[0], qualify, rel);
        buf_printf(b, "::%s", tn ? tn : (e->casttypename ? e->casttypename : "text"));
        free(tn);
        return;
    }
    case X_CASE: {
        buf_adds(b, "CASE");
        if (e->case_arg) { buf_addc(b, ' '); dp_expr(s, b, e->case_arg, qualify, rel); }
        for (int i = 0; i < e->nwhen; i++) {
            buf_adds(b, " WHEN ");
            dp_expr(s, b, e->when_cond[i], qualify, rel);
            buf_adds(b, " THEN ");
            dp_expr(s, b, e->when_res[i], qualify, rel);
        }
        if (e->case_else) { buf_adds(b, " ELSE "); dp_expr(s, b, e->case_else, qualify, rel); }
        buf_adds(b, " END");
        return;
    }
    case X_COALESCE:
        buf_adds(b, "COALESCE(");
        dp_args(s, b, e, qualify, rel);
        buf_addc(b, ')');
        return;
    case X_NULLIF:
        buf_adds(b, "NULLIF(");
        dp_args(s, b, e, qualify, rel);
        buf_addc(b, ')');
        return;
    case X_ARRAY:
        buf_adds(b, "ARRAY[");
        dp_args(s, b, e, qualify, rel);
        buf_addc(b, ']');
        return;
    case X_ROW:
        buf_adds(b, "ROW(");
        dp_args(s, b, e, qualify, rel);
        buf_addc(b, ')');
        return;
    case X_LIST:
        dp_args(s, b, e, qualify, rel);
        return;
    case X_SUBSCRIPT:
        dp_expr(s, b, e->args[0], qualify, rel);
        buf_addc(b, '[');
        if (e->nargs > 1) dp_expr(s, b, e->args[1], qualify, rel);
        buf_addc(b, ']');
        return;
    case X_BETWEEN:
        buf_addc(b, '(');
        dp_expr(s, b, e->args[0], qualify, rel);
        buf_adds(b, e->between_not ? " NOT BETWEEN " : " BETWEEN ");
        if (e->between_sym) buf_adds(b, "SYMMETRIC ");
        dp_expr(s, b, e->args[1], qualify, rel);
        buf_adds(b, " AND ");
        dp_expr(s, b, e->args[2], qualify, rel);
        buf_addc(b, ')');
        return;
    case X_IN: {
        buf_addc(b, '(');
        dp_expr(s, b, e->testexpr, qualify, rel);
        buf_adds(b, e->between_not ? " <> ALL (ARRAY[" : " = ANY (ARRAY[");
        for (int i = 0; i < e->nargs; i++) {
            if (i) buf_adds(b, ", ");
            dp_expr(s, b, e->args[i], qualify, rel);
        }
        buf_adds(b, "]))");
        return;
    }
    case X_LIKE:
        buf_addc(b, '(');
        dp_expr(s, b, e->args[0], qualify, rel);
        buf_adds(b, e->like_similar ? (e->like_not ? " !~ " : " ~ ")
                 : e->like_ilike ? (e->like_not ? " !~~* " : " ~~* ")
                 : (e->like_not ? " !~~ " : " ~~ "));
        dp_expr(s, b, e->args[1], qualify, rel);
        buf_addc(b, ')');
        return;
    case X_COLLATE:
        dp_expr(s, b, e->args[0], qualify, rel);
        buf_adds(b, " COLLATE ");
        dp_ident(b, e->name ? e->name : "default");
        return;
    default:
        /* anything the renderer does not model is shown as it was written */
        if (e->strval) buf_adds(b, e->strval);
        return;
    }
}

/* Parse a fragment of SQL as an expression and render it back.  On any parse
 * trouble the original text is handed back unchanged. */
char *deparse_expr_src(Session *s, const char *src, const char *rel, bool qualify)
{
    extern Expr *parse_expression_text(Session *s, const char *src);
    if (!src) return NULL;
    Expr *e = parse_expression_text(s, src);
    if (!e) return xstrdup(src);
    Buf b; buf_init(&b);
    dp_expr(s, &b, e, qualify, rel);
    if (!b.len) { buf_free(&b); return xstrdup(src); }
    return buf_steal(&b);
}

/* ------------------------------------------------------------ view bodies
 * pg_get_viewdef() shows the rewritten query, not the text that was typed:
 * every column reference is qualified, the clauses are laid out on their own
 * lines, and in pretty mode the keywords are indented the way ruleutils.c
 * indents them. */
/* In pretty mode ruleutils drops the parentheses the plain form always adds
 * around the outermost operator of a clause. */
static void dp_clause(Session *s, Buf *b, Expr *e, const char *rel, bool pretty,
                      bool qualify)
{
    Buf t; buf_init(&t);
    dp_expr(s, &t, e, qualify, rel);
    const char *x = t.data ? t.data : "";
    size_t n = t.len;
    if (pretty && n >= 2 && x[0] == '(' && x[n-1] == ')') {
        int depth = 0; bool outer = true;
        for (size_t i = 0; i < n; i++) {
            if (x[i] == '(') depth++;
            else if (x[i] == ')') { depth--; if (!depth && i + 1 < n) { outer = false; break; } }
        }
        if (outer) { buf_add(b, x + 1, n - 2); buf_free(&t); return; }
    }
    buf_add(b, x, n);
    buf_free(&t);
}

char *deparse_view(Session *s, Table *t, bool pretty)
{
    if (!t || !t->viewdef) return NULL;
    Node *n = parse_sql(s, t->viewdef, NULL);
    if (!n || n->tag != N_SELECT || !n->select) return NULL;
    SelectStmt *sel = n->select;
    if (sel->setop || sel->nvalrows || sel->nfrom != 1 || sel->from[0]->kind != 0)
        return NULL;                        /* only the plain shapes are modelled */
    const char *rel = sel->from[0]->alias ? sel->from[0]->alias : sel->from[0]->relname;
#if PGVC_VN >= 160
    /* From PostgreSQL 16 the pretty form no longer prefixes the relation. */
    bool qual = !pretty;
#else
    bool qual = true;
#endif

    Buf b; buf_init(&b);
    buf_adds(&b, " SELECT ");
    if (sel->distinct) buf_adds(&b, "DISTINCT ");
    int printed = 0;
    for (int i = 0; i < sel->ntarget; i++) {
        Expr *e = sel->target[i];
        if (e->tag == X_STAR) {
            for (int k = 0; k < t->ncols; k++) {
                if (t->cols[k].dropped) continue;
                if (printed) buf_adds(&b, ",\n    ");
                if (qual) buf_printf(&b, "%s.", rel);
                dp_ident(&b, t->cols[k].name);
                printed++;
            }
            continue;
        }
        const char *label = e->alias ? e->alias
                          : (printed < t->ncols ? t->cols[printed].name : NULL);
        Buf tb; buf_init(&tb);
        dp_clause(s, &tb, e, rel, pretty, qual);
        if (tb.len && label &&
            !(e->tag == X_COLREF && e->colname && !strcmp(e->colname, label)))
            buf_printf(&tb, " AS %s", label);
        if (!tb.len) {
            /* a node the renderer does not model: leave the whole view to the
             * text re-flow rather than print a hole */
            buf_free(&tb); buf_free(&b);
            return NULL;
        }
        if (printed) buf_adds(&b, ",\n    ");
        buf_add(&b, tb.data, tb.len);
        buf_free(&tb);
        printed++;
    }
    buf_printf(&b, "\n   FROM %s", sel->from[0]->relname);
    if (sel->from[0]->alias) buf_printf(&b, " %s", sel->from[0]->alias);
    if (sel->where) {
        buf_adds(&b, "\n  WHERE ");
        dp_clause(s, &b, sel->where, rel, pretty, qual);
    }
    if (sel->ngroupby) {
        buf_adds(&b, "\n  GROUP BY ");
        for (int i = 0; i < sel->ngroupby; i++) {
            if (i) buf_adds(&b, ", ");
            dp_expr(s, &b, sel->groupby[i], qual, rel);
        }
    }
    if (sel->having) {
        buf_adds(&b, "\n HAVING ");
        dp_clause(s, &b, sel->having, rel, pretty, qual);
    }
    if (sel->norderby) {
        buf_adds(&b, "\n  ORDER BY ");
        for (int i = 0; i < sel->norderby; i++) {
            if (i) buf_adds(&b, ", ");
            Expr *e = sel->orderby[i].e;
            if (e->tag == X_COLREF) dp_expr(s, &b, e, qual, rel);
            else { buf_addc(&b, '('); dp_expr(s, &b, e, qual, rel); buf_addc(&b, ')'); }
            if (sel->orderby[i].dir) buf_adds(&b, " DESC");
            if (sel->orderby[i].nulls == 1 && !sel->orderby[i].dir) buf_adds(&b, " NULLS FIRST");
            if (sel->orderby[i].nulls == 2 && sel->orderby[i].dir) buf_adds(&b, " NULLS LAST");
        }
    }
    if (sel->offset) {
        buf_adds(&b, "\n OFFSET ");
        dp_expr(s, &b, sel->offset, qual, rel);
        buf_adds(&b, " ROWS");
    }
    if (sel->limit) {
        if (sel->with_ties || sel->fetch_first) {
            buf_adds(&b, "\n FETCH FIRST (");
            dp_expr(s, &b, sel->limit, qual, rel);
            buf_printf(&b, ") ROWS %s", sel->with_ties ? "WITH TIES" : "ONLY");
        } else {
            buf_adds(&b, "\n LIMIT ");
            dp_expr(s, &b, sel->limit, qual, rel);
        }
    }
    buf_addc(&b, ';');
    return buf_steal(&b);
}
