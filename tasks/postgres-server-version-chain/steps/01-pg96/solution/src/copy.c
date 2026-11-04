/* copy.c - the COPY command, text and CSV, over the COPY sub-protocol. */
#include "engine.h"
#include "stmt.h"

void proto_copy_out_start(Session *s, int ncols);
void proto_copy_out_row(Session *s, const char *line, size_t n);
void proto_copy_out_end(Session *s);
void proto_copy_in_start(Session *s, int ncols);
bool proto_copy_in_line(Session *s, char **line, size_t *n);
void proto_copy_in_end(void);

typedef struct RowSet RowSet;
RowSet *copy_source_rows(Session *s, Node *q, int *ncols, char ***names, Oid **types);
int     rowset_nrows(RowSet *rs);
Datum  *rowset_row(RowSet *rs, int i);
void    insert_one_row(Session *s, Table *t, Datum *vals);
Datum   table_default(Session *s, Table *t, int ci, Datum *row);
void    table_apply_generated(Session *s, Table *t, Datum *vals);

void datum_to_binary(Session *s, Datum d, Buf *b);

static void emit_binary_row(Session *s, Buf *b, Datum *row, int ncols)
{
    buf_addc(b, (char)((ncols >> 8) & 0xff));
    buf_addc(b, (char)(ncols & 0xff));
    for (int i = 0; i < ncols; i++) {
        if (row[i].isnull) {
            for (int k = 0; k < 4; k++) buf_addc(b, (char)0xff);
            continue;
        }
        Buf f; buf_init(&f);
        datum_to_binary(s, row[i], &f);
        uint32 n = (uint32)f.len;
        buf_addc(b, (char)(n >> 24)); buf_addc(b, (char)(n >> 16));
        buf_addc(b, (char)(n >> 8));  buf_addc(b, (char)n);
        buf_add(b, f.data, f.len);
        buf_free(&f);
    }
}

static void text_escape(Buf *b, const char *v, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        switch (v[i]) {
        case '\\': buf_adds(b, "\\\\"); break;
        case '\n': buf_adds(b, "\\n"); break;
        case '\r': buf_adds(b, "\\r"); break;
        case '\t': buf_adds(b, "\\t"); break;
        case '\b': buf_adds(b, "\\b"); break;
        case '\f': buf_adds(b, "\\f"); break;
        case '\v': buf_adds(b, "\\v"); break;
        default: buf_addc(b, v[i]);
        }
    }
}

static void csv_quote(Buf *b, const char *v, size_t n, char q, char esc, bool force)
{
    bool need = force;
    for (size_t i = 0; i < n && !need; i++)
        if (v[i] == q || v[i] == ',' || v[i] == '\n' || v[i] == '\r') need = true;
    if (!need) { buf_add(b, v, n); return; }
    buf_addc(b, q);
    for (size_t i = 0; i < n; i++) {
        if (v[i] == q || v[i] == esc) buf_addc(b, esc);
        buf_addc(b, v[i]);
    }
    buf_addc(b, q);
}

static char *unescape_text(const char *p, size_t n, size_t *outlen, bool *isnull, const char *nullstr)
{
    if (n == strlen(nullstr) && !memcmp(p, nullstr, n)) { *isnull = true; *outlen = 0; return xstrdup(""); }
    *isnull = false;
    Buf b; buf_init(&b);
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '\\' && i + 1 < n) {
            i++;
            switch (p[i]) {
            case 'n': buf_addc(&b, '\n'); break;
            case 't': buf_addc(&b, '\t'); break;
            case 'r': buf_addc(&b, '\r'); break;
            case 'b': buf_addc(&b, '\b'); break;
            case 'f': buf_addc(&b, '\f'); break;
            case 'v': buf_addc(&b, '\v'); break;
            case '\\': buf_addc(&b, '\\'); break;
            case 'N': buf_addc(&b, '\\'); buf_addc(&b, 'N'); break;
            default: buf_addc(&b, p[i]);
            }
            continue;
        }
        buf_addc(&b, p[i]);
    }
    *outlen = b.len;
    return buf_steal(&b);
}

/* split one CSV record honouring quotes */
static int csv_split(const char *p, size_t n, char delim, char quote, char esc,
                     char **fields, size_t *lens, bool *quoted, int maxf)
{
    int nf = 0;
    size_t i = 0;
    while (nf < maxf) {
        Buf b; buf_init(&b);
        bool inq = false, wasq = false;
        while (i < n) {
            char c = p[i];
            if (inq) {
                if (c == esc && i + 1 < n && (p[i+1] == quote || p[i+1] == esc)) { buf_addc(&b, p[i+1]); i += 2; continue; }
                if (c == quote) { inq = false; i++; continue; }
                buf_addc(&b, c); i++; continue;
            }
            if (c == quote) { inq = true; wasq = true; i++; continue; }
            if (c == delim) { i++; break; }
            buf_addc(&b, c); i++;
        }
        fields[nf] = b.data ? b.data : xstrdup("");
        lens[nf] = b.len;
        quoted[nf] = wasq;
        nf++;
        if (i >= n) {
            if (n && p[n-1] == delim && nf < maxf) {
                fields[nf] = xstrdup(""); lens[nf] = 0; quoted[nf] = false; nf++;
            }
            break;
        }
    }
    return nf;
}

/* Evaluate a COPY FROM WHERE condition against one incoming row. */
static bool copy_where_ok(Session *s, Table *t, Expr *w, Datum *vals)
{
    extern bool copy_eval_where(Session *s, Table *t, Expr *w, Datum *vals);
    return copy_eval_where(s, t, w, vals);
}

QueryResult *exec_copy(Session *s, Node *n)
{
    CopyStmt *st = n->x;
    bool csv = !strcmp(st->format, "csv");
    bool binary = !strcmp(st->format, "binary");
    if (st->filename)
        ereport(s, "42501", "must be superuser or a member of the pg_write_server_files role to COPY to a file");
    int on_error = 0;                       /* 0 stop, 1 ignore */
    bool verbose_log = false;
    if (st->on_error) {
#if PGVC_VN >= 170
        char *lo = lower_ident(st->on_error);
        if (!strcmp(lo, "stop")) on_error = 0;
        else if (!strcmp(lo, "ignore")) on_error = 1;
        else ereport_pos(s, "22023", st->on_error_pos,
                         "COPY ON_ERROR \"%s\" not recognized", st->on_error);
        free(lo);
        if (!st->from)
            ereport_pos(s, "0A000", st->on_error_pos,
                        "COPY ON_ERROR cannot be used with COPY TO");
#else
        ereport_pos(s, "42601", st->on_error_pos,
                    "option \"on_error\" not recognized");
#endif
    }
    if (st->log_verbosity) {
        char *lo = lower_ident(st->log_verbosity);
        verbose_log = !strcmp(lo, "verbose");
        free(lo);
    }

    if (!st->from) {
        if (st->where)
#if PGVC_VN >= 120
            ereport_pos(s, "42601", st->where_pos, "WHERE clause not allowed with COPY TO");
#else
            ereport_pos(s, "42601", st->where_pos, "syntax error at or near \"WHERE\"");
#endif
        int ncols = 0;
        char **names = NULL;
        Oid *types = NULL;
        RowSet *rs = NULL;
        Table *t = NULL;
        int *map = NULL;
        if (st->query) rs = copy_source_rows(s, st->query, &ncols, &names, &types);
        else {
            t = tbl_find(s->db, st->schema, st->name);
            if (!t) ereport(s, "42P01", "relation \"%s\" does not exist", st->name);
            if (st->ncols) {
                map = xmalloc((size_t)st->ncols * sizeof(int));
                for (int i = 0; i < st->ncols; i++) {
                    int ci = col_index(t, st->cols[i]);
                    if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist",
                                        st->cols[i], t->name);
                    map[i] = ci;
                }
                ncols = st->ncols;
            } else {
                map = xmalloc((size_t)(t->ncols?t->ncols:1) * sizeof(int));
                /* a generated column is not part of COPY's default column list */
                for (int i = 0; i < t->ncols; i++)
                    if (!t->cols[i].dropped && !t->cols[i].generated)
                        map[ncols++] = i;
            }
            names = xmalloc((size_t)(ncols?ncols:1) * sizeof(char*));
            for (int i = 0; i < ncols; i++) names[i] = t->cols[map[i]].name;
        }
        proto_copy_out_start(s, ncols);
        if (binary) {
            Buf hdr; buf_init(&hdr);
            buf_add(&hdr, "PGCOPY\n\377\r\n\0", 11);
            for (int i = 0; i < 8; i++) buf_addc(&hdr, 0);      /* flags + extension */
            proto_copy_out_row(s, hdr.data, hdr.len);
            buf_free(&hdr);
        }
        if (!binary && st->header) {
            Buf b; buf_init(&b);
            for (int i = 0; i < ncols; i++) {
                if (i) buf_addc(&b, st->delim);
                if (csv) csv_quote(&b, names[i], strlen(names[i]), st->quote, st->escape, false);
                else text_escape(&b, names[i], strlen(names[i]));
            }
            buf_addc(&b, '\n');
            proto_copy_out_row(s, b.data, b.len);
            buf_free(&b);
        }
        int nrows = 0;
        if (rs) {
            nrows = rowset_nrows(rs);
            for (int r = 0; r < nrows; r++) {
                Datum *row = rowset_row(rs, r);
                Buf b; buf_init(&b);
                if (binary) { emit_binary_row(s, &b, row, ncols); }
                else {
                    for (int i = 0; i < ncols; i++) {
                        if (i) buf_addc(&b, st->delim);
                        if (row[i].isnull) { buf_adds(&b, st->null_str); continue; }
                        char *v = out_func(s, row[i]);
                        if (csv) csv_quote(&b, v, strlen(v), st->quote, st->escape, false);
                        else text_escape(&b, v, strlen(v));
                        free(v);
                    }
                    buf_addc(&b, '\n');
                }
                proto_copy_out_row(s, b.data, b.len);
                buf_free(&b);
            }
        } else {
            for (int r = 0; r < t->nrows; r++) {
                Row *rw = t->rows[r];
                if (!row_visible(s, rw)) continue;
                nrows++;
                Buf b; buf_init(&b);
                if (binary) {
                    Datum *tmp = xmalloc((size_t)(ncols ? ncols : 1) * sizeof(Datum));
                    for (int i = 0; i < ncols; i++) tmp[i] = rw->vals[map[i]];
                    emit_binary_row(s, &b, tmp, ncols);
                    free(tmp);
                } else {
                    for (int i = 0; i < ncols; i++) {
                        if (i) buf_addc(&b, st->delim);
                        Datum d = rw->vals[map[i]];
                        if (d.isnull) { buf_adds(&b, st->null_str); continue; }
                        char *v = out_func(s, d);
                        if (csv) csv_quote(&b, v, strlen(v), st->quote, st->escape, false);
                        else text_escape(&b, v, strlen(v));
                        free(v);
                    }
                    buf_addc(&b, '\n');
                }
                proto_copy_out_row(s, b.data, b.len);
                buf_free(&b);
            }
        }
        if (binary) {
            Buf t2; buf_init(&t2);
            buf_addc(&t2, (char)0xff); buf_addc(&t2, (char)0xff);
            proto_copy_out_row(s, t2.data, t2.len);
            buf_free(&t2);
        }
        proto_copy_out_end(s);
        QueryResult *q = qr_new();
        snprintf(q->tag, sizeof q->tag, "COPY %d", nrows);
        return q;
    }

    /* COPY ... FROM STDIN */
    Table *t = tbl_find(s->db, st->schema, st->name);
    if (!t) ereport(s, "42P01", "relation \"%s\" does not exist", st->name);
    int ncols = 0;
    int *map = xmalloc((size_t)(t->ncols?t->ncols:1) * sizeof(int));
    if (st->ncols) {
        for (int i = 0; i < st->ncols; i++) {
            int ci = col_index(t, st->cols[i]);
            if (ci < 0) ereport(s, "42703", "column \"%s\" of relation \"%s\" does not exist", st->cols[i], t->name);
            map[ncols++] = ci;
        }
    } else for (int i = 0; i < t->ncols; i++) if (!t->cols[i].dropped && !t->cols[i].generated) map[ncols++] = i;

    if (binary) ereport(s, "0A000", "COPY from a binary file is not supported by this server");
    if (st->where) {
#if PGVC_VN < 120
        ereport_pos(s, "42601", st->where_pos, "syntax error at or near \"WHERE\"");
#else
        extern bool expr_has_aggregate(Expr *e);
        extern bool expr_has_subquery(Expr *e);
        extern void copy_check_where_cols(Session *s, Table *t, Expr *w);
        copy_check_where_cols(s, t, st->where);
        extern int expr_left_pos(Expr *e);
        int wpos = expr_left_pos(st->where);
        if (expr_has_aggregate(st->where))
            ereport_pos(s, "42803", wpos,
                        "aggregate functions are not allowed in COPY FROM WHERE conditions");
        if (expr_has_subquery(st->where))
            ereport_pos(s, "0A000", wpos,
                        "cannot use subquery in COPY FROM WHERE condition");
#endif
    }
    proto_copy_in_start(s, ncols);
    char *line; size_t len;
    int nrows = 0, nskipped = 0;
    bool first = true;
    while (proto_copy_in_line(s, &line, &len)) {
        if (len && line[len-1] == '\r') len--;
        if (!csv && len == 2 && line[0] == '\\' && line[1] == '.') break;
        if (first && st->header) {
            first = false;
#if PGVC_VN >= 150
            if (st->header_match) {
                char *hf[256]; size_t hl[256]; bool hq[256];
                int hn = csv ? csv_split(line, len, st->delim, st->quote, st->escape,
                                         hf, hl, hq, 256)
                             : 0;
                if (!csv) {
                    hn = 0;
                    size_t start = 0;
                    for (size_t i = 0; i <= len && hn < 256; i++)
                        if (i == len || line[i] == st->delim) {
                            hf[hn] = xstrndup(line + start, i - start);
                            hl[hn] = i - start; hq[hn] = false; hn++;
                            start = i + 1;
                        }
                }
                if (hn != ncols) {
                    err_context(s, "COPY %s, line 1: \"%.*s\"", t->name, (int)len, line);
                    ereport(s, "22P04", "wrong number of fields in header line: got %d, "
                            "expected %d", hn, ncols);
                }
                for (int q = 0; q < hn; q++) {
                    const char *want = t->cols[map[q]].name;
                    if (strlen(want) != hl[q] || memcmp(want, hf[q], hl[q])) {
                        err_context(s, "COPY %s, line 1: \"%.*s\"", t->name, (int)len, line);
                        ereport(s, "22P04", "column name mismatch in header line field %d: "
                                "got \"%.*s\", expected \"%s\"", q + 1,
                                (int)hl[q], hf[q], want);
                    }
                }
            }
#endif
            continue;
        }
        first = false;
        if (len == 0 && !csv) continue;
        char *fields[256]; size_t lens[256]; bool quoted[256];
        int nf;
        if (csv) nf = csv_split(line, len, st->delim, st->quote, st->escape, fields, lens, quoted, 256);
        else {
            nf = 0;
            size_t start = 0;
            for (size_t i = 0; i <= len && nf < 256; i++) {
                if (i == len || line[i] == st->delim) {
                    fields[nf] = xstrndup(line + start, i - start);
                    lens[nf] = i - start;
                    quoted[nf] = false;
                    nf++;
                    start = i + 1;
                }
            }
        }
        if (nf != ncols) {
            proto_copy_in_end();
            err_context(s, "COPY %s, line %d: \"%.*s\"", t->name, nrows + 1, (int)len, line);
            ereport(s, "22P04", nf < ncols ? "missing data for column \"%s\"" :
                    "extra data after last expected column",
                    nf < ncols ? t->cols[map[nf]].name : "");
        }
        Datum *vals = xmalloc((size_t)(t->ncols?t->ncols:1) * sizeof(Datum));
        for (int i = 0; i < t->ncols; i++) vals[i] = d_null(t->cols[i].type);
        bool *given = xmalloc((size_t)(t->ncols?t->ncols:1) * sizeof(bool));
        memset(given, 0, (size_t)(t->ncols?t->ncols:1) * sizeof(bool));
        if (on_error) {
            /* ON_ERROR ignore: a row whose input conversion fails is skipped */
            jmp_buf save;
            ErrState prev = s->err;
            volatile bool bad = false;
            volatile int badcol = 0;
            char badval[128]; badval[0] = '\0';
            memcpy(save, s->jmp, sizeof(jmp_buf));
            if (setjmp(s->jmp) == 0) {
                for (int i = 0; i < nf; i++) {
                    bool isnull = false;
                    size_t ol = lens[i];
                    char *v;
                    if (csv) {
                        v = fields[i];
                        if (!quoted[i] && ol == strlen(st->null_str) &&
                            !memcmp(v, st->null_str, ol)) isnull = true;
                    } else v = unescape_text(fields[i], lens[i], &ol, &isnull, st->null_str);
                    Column *col = &t->cols[map[i]];
                    badcol = i;
                    snprintf(badval, sizeof badval, "%.*s", (int)ol, v);
                    vals[map[i]] = isnull ? d_null(col->type)
                                          : in_func(s, col->type, col->typmod, v, (int32)ol);
                    given[map[i]] = true;
                }
            } else bad = true;
            memcpy(s->jmp, save, sizeof(jmp_buf));
            s->err = prev;
            if (bad) {
                nskipped++;
                if (verbose_log)
                    notice(s, "00000", "skipping row due to data type incompatibility "
                                       "at line %d for column \"%s\": \"%s\"",
                           nrows + nskipped, t->cols[map[badcol]].name, badval);
                free(given);
                continue;
            }
        } else
        for (int i = 0; i < nf; i++) {
            bool isnull = false;
            size_t ol = lens[i];
            char *v;
            if (csv) {
                v = fields[i];
                if (!quoted[i] && ol == strlen(st->null_str) && !memcmp(v, st->null_str, ol)) isnull = true;
            } else v = unescape_text(fields[i], lens[i], &ol, &isnull, st->null_str);
            Column *col = &t->cols[map[i]];
            if (!isnull) err_context(s, "COPY %s, line %d, column %s: \"%.*s\"",
                                     t->name, nrows + 1, col->name, (int)ol, v);
            vals[map[i]] = isnull ? d_null(col->type) : in_func(s, col->type, col->typmod, v, (int32)ol);
            s->err.context[0] = '\0';
            given[map[i]] = true;
        }
        for (int i = 0; i < t->ncols; i++)
            if (!given[i] && !t->cols[i].generated) vals[i] = table_default(s, t, i, vals);
        table_apply_generated(s, t, vals);
        if (st->where && !copy_where_ok(s, t, st->where, vals)) { nrows++; free(given); continue; }
        /* PostgreSQL names the line and its text when a row cannot be stored */
        err_context(s, "COPY %s, line %d: \"%.*s\"", t->name, nrows + 1, (int)len, line);
        insert_one_row(s, t, vals);
        s->err.context[0] = '\0';
        nrows++;
        free(given);
    }
    proto_copy_in_end();
    if (nskipped)
        notice(s, "00000", nskipped == 1
               ? "%d row was skipped due to data type incompatibility"
               : "%d rows were skipped due to data type incompatibility", nskipped);
    QueryResult *q = qr_new();
    snprintf(q->tag, sizeof q->tag, "COPY %d", nrows);
    return q;
}
