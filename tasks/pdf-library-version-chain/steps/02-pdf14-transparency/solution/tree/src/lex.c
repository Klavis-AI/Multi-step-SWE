/* lex.c -- tokenizer and object parser.
 *
 * ISO 32000-1 clause 7.2 (lexical conventions) and 7.3 (objects), plus the
 * malformations every shipping reader accepts. The rules that matter:
 *
 *   - the lexer never reads outside [buf, buf+len) whatever the input is;
 *   - every call to pdf_lex_next() either consumes at least one byte or
 *     reports EOF, so no caller loop can spin;
 *   - the parser is depth-capped, so adversarial nesting cannot blow the
 *     stack, and it returns pdf_null_obj() -- never NULL -- on junk.
 */

#include "pdfint.h"

#include <errno.h>
#include <limits.h>
#include <math.h>

#define LEX_MAX_DEPTH 256

/* ------------------------------------------------------- character classes */

static int is_ws(int c)
{
    return c == 0x00 || c == 0x09 || c == 0x0a ||
           c == 0x0c || c == 0x0d || c == 0x20;
}

static int is_delim(int c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' ||
           c == '[' || c == ']' || c == '{' || c == '}' ||
           c == '/' || c == '%';
}

static int is_reg(int c)
{
    return !is_ws(c) && !is_delim(c);
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Whitespace and comments are interchangeable everywhere outside strings,
 * including between array elements and dictionary entries. */
static void skip_ws(pdf_lexer *lx)
{
    for (;;) {
        while (lx->pos < lx->len && is_ws(lx->buf[lx->pos]))
            lx->pos++;
        if (lx->pos < lx->len && lx->buf[lx->pos] == '%') {
            while (lx->pos < lx->len &&
                   lx->buf[lx->pos] != '\n' && lx->buf[lx->pos] != '\r')
                lx->pos++;
            continue;               /* the EOL itself is eaten as whitespace */
        }
        break;
    }
}

/* ----------------------------------------------------------------- tokens */

static void tok_clear(pdf_token *tok)
{
    tok->type = PDF_TOK_EOF;
    tok->i = 0;
    tok->r = 0.0;
    tok->s = NULL;
    tok->slen = 0;
}

static int tok_bytes(pdf_doc *doc, pdf_token *tok, pdf_toktype type,
                     const char *p, size_t n)
{
    char *s = pdf_strdup_n(doc, p, n);
    if (!s) {
        tok->type = PDF_TOK_ERROR;
        return PDF_E_NOMEM;
    }
    tok->type = type;
    tok->s = s;
    tok->slen = n;
    return 0;
}

static int tok_is_kw(const pdf_token *tok, const char *kw)
{
    return tok->type == PDF_TOK_KEYWORD && tok->s && strcmp(tok->s, kw) == 0;
}

/* ------------------------------------------------------------ sub-lexers */

/* Literal string; lx->pos is just past the '('.
 * Balanced unescaped parens nest, \ escapes follow 7.3.4.2, and a raw CR or
 * CRLF inside the string is normalised to a single LF. */
static int lex_string(pdf_doc *doc, pdf_lexer *lx, pdf_token *tok)
{
    pdf_buf b;
    int depth = 1, rc = 0;

    pdf_buf_init(&b);
    while (lx->pos < lx->len) {
        int c = lx->buf[lx->pos++];

        if (c == '\\') {
            if (lx->pos >= lx->len)
                break;                          /* trailing backslash at EOF */
            c = lx->buf[lx->pos++];
            switch (c) {
            case 'n': rc = pdf_buf_putc(&b, '\n'); break;
            case 'r': rc = pdf_buf_putc(&b, '\r'); break;
            case 't': rc = pdf_buf_putc(&b, '\t'); break;
            case 'b': rc = pdf_buf_putc(&b, '\b'); break;
            case 'f': rc = pdf_buf_putc(&b, '\f'); break;
            case '\r':                          /* line continuation */
                if (lx->pos < lx->len && lx->buf[lx->pos] == '\n')
                    lx->pos++;
                break;
            case '\n':
                break;
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7': {
                int v = c - '0', k;
                for (k = 0; k < 2 && lx->pos < lx->len; k++) {
                    int d = lx->buf[lx->pos];
                    if (d < '0' || d > '7')
                        break;
                    v = v * 8 + (d - '0');
                    lx->pos++;
                }
                rc = pdf_buf_putc(&b, v & 0xFF);        /* truncated mod 256 */
                break;
            }
            default:
                rc = pdf_buf_putc(&b, c);   /* \( \) \\ and "backslash ignored" */
                break;
            }
        } else if (c == '(') {
            depth++;
            rc = pdf_buf_putc(&b, '(');
        } else if (c == ')') {
            if (--depth == 0)
                break;                                  /* string ends here */
            rc = pdf_buf_putc(&b, ')');
        } else if (c == '\r') {
            if (lx->pos < lx->len && lx->buf[lx->pos] == '\n')
                lx->pos++;
            rc = pdf_buf_putc(&b, '\n');
        } else {
            rc = pdf_buf_putc(&b, c);
        }

        if (rc != 0) {
            pdf_buf_free(&b);
            tok->type = PDF_TOK_ERROR;
            return rc;
        }
    }
    /* An unterminated string just ends at EOF; keep what we have. */
    rc = tok_bytes(doc, tok, PDF_TOK_STRING, (const char *)b.p, b.n);
    pdf_buf_free(&b);
    return rc;
}

/* Hex string; lx->pos is just past the '<'. Whitespace and any other non-hex
 * byte are ignored, and a dangling final digit is padded with '0'. */
static int lex_hexstring(pdf_doc *doc, pdf_lexer *lx, pdf_token *tok)
{
    pdf_buf b;
    int hi = -1, rc = 0;

    pdf_buf_init(&b);
    while (lx->pos < lx->len) {
        int c = lx->buf[lx->pos++];
        int v;
        if (c == '>')
            break;
        v = hexval(c);
        if (v < 0)
            continue;
        if (hi < 0) {
            hi = v;
        } else {
            rc = pdf_buf_putc(&b, (hi << 4) | v);
            hi = -1;
            if (rc != 0)
                break;
        }
    }
    if (rc == 0 && hi >= 0)
        rc = pdf_buf_putc(&b, hi << 4);
    if (rc != 0) {
        pdf_buf_free(&b);
        tok->type = PDF_TOK_ERROR;
        return rc;
    }
    rc = tok_bytes(doc, tok, PDF_TOK_STRING, (const char *)b.p, b.n);
    pdf_buf_free(&b);
    return rc;
}

/* Name; lx->pos is just past the '/'. #xx escapes are decoded (7.3.5); "/" on
 * its own is a legal empty name; #00 is not legal, so it stays literal. */
static int lex_name(pdf_doc *doc, pdf_lexer *lx, pdf_token *tok)
{
    pdf_buf b;
    int rc = 0;

    pdf_buf_init(&b);
    while (lx->pos < lx->len) {
        int c = lx->buf[lx->pos];
        if (!is_reg(c))
            break;
        lx->pos++;
        if (c == '#') {
            int h1 = (lx->pos < lx->len) ? hexval(lx->buf[lx->pos]) : -1;
            int h2 = (lx->pos + 1 < lx->len) ? hexval(lx->buf[lx->pos + 1]) : -1;
            if (h1 >= 0 && h2 >= 0 && (h1 != 0 || h2 != 0)) {
                c = (h1 << 4) | h2;
                lx->pos += 2;
            }
            /* otherwise the '#' is kept as an ordinary character */
        }
        rc = pdf_buf_putc(&b, c);
        if (rc != 0)
            break;
    }
    if (rc != 0) {
        pdf_buf_free(&b);
        tok->type = PDF_TOK_ERROR;
        return rc;
    }
    rc = tok_bytes(doc, tok, PDF_TOK_NAME, (const char *)b.p, b.n);
    pdf_buf_free(&b);
    return rc;
}

/* Numbers. Real files hold "--5", "+3", ".", "6.-2" and worse, so follow what
 * shipping readers do: a '-' anywhere in the run only ever means "negative",
 * '+' is ignored, the first '.' starts the fraction and later ones are
 * dropped, and any other character ends the token (so "0R" lexes as 0 then
 * the keyword R, which some generators really do emit). */
static void lex_number(pdf_lexer *lx, pdf_token *tok)
{
    char num[64];
    size_t n = 0;
    int neg = 0, isreal = 0, ndig = 0;

    while (lx->pos < lx->len) {
        int c = lx->buf[lx->pos];
        if (c >= '0' && c <= '9') {
            if (n + 1 < sizeof num)
                num[n++] = (char)c;
            ndig++;
        } else if (c == '-') {
            neg = 1;
        } else if (c == '+') {
            /* ignored */
        } else if (c == '.') {
            if (!isreal) {
                isreal = 1;
                if (n + 1 < sizeof num)
                    num[n++] = '.';
            }
        } else {
            break;
        }
        lx->pos++;
    }
    num[n] = '\0';

    if (ndig == 0) {                    /* "-", "+", "." and friends */
        tok->type = isreal ? PDF_TOK_REAL : PDF_TOK_INT;
        tok->i = 0;
        tok->r = 0.0;
        return;
    }

    if (isreal) {
        double d = strtod(num, NULL);
        if (!isfinite(d))
            d = 0.0;
        if (neg)
            d = -d;
        tok->type = PDF_TOK_REAL;
        tok->r = d;
        if (d >= 9223372036854775808.0)         /* 2^63 */
            tok->i = LLONG_MAX;
        else if (d <= -9223372036854775808.0)
            tok->i = LLONG_MIN;
        else
            tok->i = (long long)d;
    } else {
        long long v;
        errno = 0;
        v = strtoll(num, NULL, 10);             /* num carries no sign */
        if (errno == ERANGE || v < 0)
            v = LLONG_MAX;
        tok->type = PDF_TOK_INT;
        tok->i = neg ? -v : v;
        tok->r = (double)tok->i;
    }
}

/* ------------------------------------------------------------- the lexer */

void pdf_lex_init(pdf_lexer *lx, const unsigned char *buf, size_t len, size_t pos)
{
    if (!lx)
        return;
    if (!buf)
        len = 0;
    lx->buf = buf;
    lx->len = len;
    lx->pos = pos > len ? len : pos;
}

/* Returns 0 whenever a token was produced -- including PDF_TOK_EOF and
 * PDF_TOK_ERROR -- and a negative PDF_E_* only when no token could be built
 * at all (allocation failure). Bad bytes surface as PDF_TOK_ERROR and are
 * always consumed, so callers keep making progress. */
int pdf_lex_next(pdf_doc *doc, pdf_lexer *lx, pdf_token *tok)
{
    int c;

    if (!tok)
        return PDF_E_RANGE;
    tok_clear(tok);
    if (!lx || !lx->buf)
        return 0;

    skip_ws(lx);
    if (lx->pos >= lx->len)
        return 0;                                               /* EOF */

    c = lx->buf[lx->pos];
    switch (c) {
    case '[': lx->pos++; tok->type = PDF_TOK_ARRAY_OPEN;  return 0;
    case ']': lx->pos++; tok->type = PDF_TOK_ARRAY_CLOSE; return 0;
    case '{': lx->pos++; tok->type = PDF_TOK_BRACE_OPEN;  return 0;
    case '}': lx->pos++; tok->type = PDF_TOK_BRACE_CLOSE; return 0;
    case '(': lx->pos++; return lex_string(doc, lx, tok);
    case '/': lx->pos++; return lex_name(doc, lx, tok);
    case '<':
        if (lx->pos + 1 < lx->len && lx->buf[lx->pos + 1] == '<') {
            lx->pos += 2;
            tok->type = PDF_TOK_DICT_OPEN;
            return 0;
        }
        lx->pos++;
        return lex_hexstring(doc, lx, tok);
    case '>':
        if (lx->pos + 1 < lx->len && lx->buf[lx->pos + 1] == '>') {
            lx->pos += 2;
            tok->type = PDF_TOK_DICT_CLOSE;
            return 0;
        }
        lx->pos++;                              /* stray '>' */
        tok->type = PDF_TOK_ERROR;
        return 0;
    case ')':
        lx->pos++;                              /* stray ')' */
        tok->type = PDF_TOK_ERROR;
        return 0;
    default:
        break;
    }

    if (c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9')) {
        size_t start = lx->pos;

        lex_number(lx, tok);
        /* A real is the one token whose exact spelling outlives parsing: it is
         * echoed back verbatim when the object is printed, so the bytes the
         * file used are kept alongside the value.  An integer has only one
         * spelling that matters, and a failed copy just leaves s NULL. */
        if (tok->type == PDF_TOK_REAL && lx->pos > start) {
            tok->s = pdf_strdup_n(doc, (const char *)lx->buf + start,
                                  lx->pos - start);
            tok->slen = tok->s ? lx->pos - start : 0;
        }
        return 0;
    }

    {   /* keyword: obj endobj stream endstream R true false null xref ... */
        size_t start = lx->pos;
        while (lx->pos < lx->len && is_reg(lx->buf[lx->pos]))
            lx->pos++;
        if (lx->pos == start) {                 /* defensive: never happens */
            lx->pos++;
            tok->type = PDF_TOK_ERROR;
            return 0;
        }
        return tok_bytes(doc, tok, PDF_TOK_KEYWORD,
                         (const char *)lx->buf + start, lx->pos - start);
    }
}

/* ------------------------------------------------------------- the parser */

static pdf_obj *parse_value(pdf_doc *doc, pdf_lexer *lx, pdf_token *tok,
                            int depth, int allow_stream);

/* Offset of the next "endstream" at or after 'from', else lx->len. */
static size_t find_endstream(const pdf_lexer *lx, size_t from)
{
    static const char kw[] = "endstream";
    const size_t klen = sizeof kw - 1;
    size_t i = from;

    while (i + klen <= lx->len) {
        const unsigned char *hit = memchr(lx->buf + i, 'e', lx->len - i - klen + 1);
        if (!hit)
            break;
        i = (size_t)(hit - lx->buf);
        if (memcmp(lx->buf + i, kw, klen) == 0)
            return i;
        i++;
    }
    return lx->len;
}

/* Build a PDF_STREAM. lx->pos is just past the "stream" keyword. */
static pdf_obj *make_stream(pdf_doc *doc, pdf_lexer *lx, pdf_obj *dict)
{
    pdf_obj *stm, *lenobj;
    long long len = -1, off;
    size_t p = lx->pos, q;

    /* 7.3.8.1 requires CRLF or a bare LF here. Tolerate a lone CR, tolerate
     * the EOL missing entirely, and tolerate blanks before it. */
    q = p;
    while (q < lx->len && q < p + 8 && (lx->buf[q] == ' ' || lx->buf[q] == '\t'))
        q++;
    if (q < lx->len && (lx->buf[q] == '\r' || lx->buf[q] == '\n'))
        p = q;
    if (p < lx->len && lx->buf[p] == '\r') {
        p++;
        if (p < lx->len && lx->buf[p] == '\n')
            p++;
    } else if (p < lx->len && lx->buf[p] == '\n') {
        p++;
    }
    off = (long long)p;

    /* Only a direct integer counts here; an indirect /Length has to be
     * resolved by the caller, which also has to cope with it being wrong. */
    lenobj = pdf_dict_get_raw(dict, "Length");
    if (pdf_is_int(lenobj)) {
        len = pdf_to_int(lenobj);
        if (len < 0)
            len = -1;
    }

    stm = pdf_alloc(doc, sizeof *stm);
    if (!stm)
        return dict ? dict : pdf_null_obj();
    memset(stm, 0, sizeof *stm);
    stm->kind = PDF_STREAM;
    stm->u.stm.dict = pdf_is_dict(dict) ? dict : pdf_new_dict(doc);
    stm->u.stm.off = off;
    stm->u.stm.len = len;
    stm->u.stm.mem = NULL;
    stm->u.stm.memlen = 0;
    stm->u.stm.num = 0;
    stm->u.stm.gen = 0;

    /* Leave the lexer somewhere sane for a caller that keeps reading. */
    if (len >= 0 && (unsigned long long)off + (unsigned long long)len <= lx->len)
        lx->pos = (size_t)(off + len);
    else
        lx->pos = find_endstream(lx, p);

    {   /* swallow the matching endstream when it is right there */
        size_t save = lx->pos;
        pdf_token t;
        if (pdf_lex_next(doc, lx, &t) != 0 || !tok_is_kw(&t, "endstream"))
            lx->pos = save;
    }
    return stm;
}

static pdf_obj *parse_array(pdf_doc *doc, pdf_lexer *lx, int depth)
{
    pdf_obj *arr = pdf_new_array(doc);
    pdf_token tok;

    for (;;) {
        size_t before = lx->pos;
        pdf_obj *v;

        if (pdf_lex_next(doc, lx, &tok) != 0)
            break;
        if (tok.type == PDF_TOK_EOF || tok.type == PDF_TOK_ARRAY_CLOSE)
            break;
        if (tok.type == PDF_TOK_DICT_CLOSE) {   /* ">>" inside "[": give up */
            lx->pos = before;
            break;
        }
        v = parse_value(doc, lx, &tok, depth, 0);
        if (pdf_array_push(doc, arr, v) != 0)
            break;                                          /* out of memory */
        if (lx->pos <= before)                  /* defensive: always advances */
            break;
    }
    return arr;
}

static pdf_obj *parse_dict(pdf_doc *doc, pdf_lexer *lx, int depth)
{
    pdf_obj *d = pdf_new_dict(doc);
    pdf_token tok;

    for (;;) {
        size_t before = lx->pos;

        if (pdf_lex_next(doc, lx, &tok) != 0)
            break;
        if (tok.type == PDF_TOK_EOF || tok.type == PDF_TOK_DICT_CLOSE)
            break;

        if (tok.type == PDF_TOK_NAME) {
            char *key = tok.s;                  /* arena memory, stays valid */
            pdf_token vt;
            size_t at = lx->pos;

            if (pdf_lex_next(doc, lx, &vt) != 0 || vt.type == PDF_TOK_EOF) {
                pdf_dict_put(doc, d, key ? key : "", pdf_null_obj());
                break;
            }
            if (vt.type == PDF_TOK_DICT_CLOSE) {        /* key with no value */
                pdf_dict_put(doc, d, key ? key : "", pdf_null_obj());
                break;
            }
            if (lx->pos <= at)
                break;
            pdf_dict_put(doc, d, key ? key : "",
                         parse_value(doc, lx, &vt, depth, 0));
        } else if (tok.type != PDF_TOK_ARRAY_CLOSE) {
            /* Junk where a key belongs: consume a whole value so the rest of
             * the dictionary stays aligned. */
            (void)parse_value(doc, lx, &tok, depth, 0);
        }

        if (lx->pos <= before)
            break;
    }
    return d;
}

/* Body of an "N G obj" definition. */
static pdf_obj *parse_indirect(pdf_doc *doc, pdf_lexer *lx,
                               int num, int gen, int depth)
{
    pdf_token tok;
    pdf_obj *body;
    size_t save = lx->pos;

    if (pdf_lex_next(doc, lx, &tok) != 0 || tok.type == PDF_TOK_EOF)
        return pdf_null_obj();
    if (tok_is_kw(&tok, "endobj")) {            /* empty body; leave endobj */
        lx->pos = save;
        return pdf_null_obj();
    }
    body = parse_value(doc, lx, &tok, depth, 1);
    if (body && body->kind == PDF_STREAM) {
        body->u.stm.num = num;
        body->u.stm.gen = gen;
    }
    return body;
}

/* Turn one already-lexed token (plus whatever follows it) into an object. */
static pdf_obj *parse_value(pdf_doc *doc, pdf_lexer *lx, pdf_token *tok,
                            int depth, int allow_stream)
{
    switch (tok->type) {
    case PDF_TOK_INT: {
        /* "N G R" and "N G obj" need two tokens of lookahead; anything else
         * has to rewind so "[1 2 3]" stays three integers. */
        long long num = tok->i;
        size_t after_first = lx->pos;

        if (num >= 0 && num <= INT_MAX) {
            pdf_token t2;
            if (pdf_lex_next(doc, lx, &t2) == 0 && t2.type == PDF_TOK_INT &&
                t2.i >= 0 && t2.i <= INT_MAX) {
                long long gen = t2.i;
                size_t after_second = lx->pos;
                pdf_token t3;

                if (pdf_lex_next(doc, lx, &t3) == 0) {
                    if (tok_is_kw(&t3, "R"))
                        return pdf_new_ref(doc, (int)num, (int)gen);
                    if (allow_stream && tok_is_kw(&t3, "obj")) {
                        if (depth >= LEX_MAX_DEPTH)
                            return pdf_null_obj();
                        return parse_indirect(doc, lx, (int)num, (int)gen,
                                              depth + 1);
                    }
                }
                lx->pos = after_second;
            }
        }
        lx->pos = after_first;
        return pdf_new_int(doc, num);
    }

    case PDF_TOK_REAL:
        return pdf_new_real_lit(doc, tok->r, tok->s, tok->slen);

    case PDF_TOK_STRING:
        return pdf_new_string(doc, tok->s, tok->slen);

    case PDF_TOK_NAME:
        return pdf_new_name(doc, tok->s ? tok->s : "");

    case PDF_TOK_ARRAY_OPEN:
        if (depth >= LEX_MAX_DEPTH)
            return pdf_null_obj();      /* '[' already eaten: still progress */
        return parse_array(doc, lx, depth + 1);

    case PDF_TOK_DICT_OPEN: {
        pdf_obj *d;
        if (depth >= LEX_MAX_DEPTH)
            return pdf_null_obj();
        d = parse_dict(doc, lx, depth + 1);
        if (allow_stream) {
            size_t save = lx->pos;
            pdf_token t;
            if (pdf_lex_next(doc, lx, &t) == 0 && tok_is_kw(&t, "stream"))
                return make_stream(doc, lx, d);
            lx->pos = save;
        }
        return d;
    }

    case PDF_TOK_KEYWORD:
        if (tok_is_kw(tok, "true"))
            return pdf_new_bool(doc, 1);
        if (tok_is_kw(tok, "false"))
            return pdf_new_bool(doc, 0);
        /* null, and every keyword that cannot start a value (endobj, stream,
         * endstream, R, xref, trailer, startxref, ...) */
        return pdf_null_obj();

    case PDF_TOK_EOF:
    case PDF_TOK_ERROR:
    case PDF_TOK_ARRAY_CLOSE:
    case PDF_TOK_DICT_CLOSE:
    case PDF_TOK_BRACE_OPEN:
    case PDF_TOK_BRACE_CLOSE:
    default:
        return pdf_null_obj();
    }
}

/* Parse exactly one object, starting wherever the lexer is. */
pdf_obj *pdf_parse_obj(pdf_doc *doc, pdf_lexer *lx)
{
    pdf_token tok;

    if (!lx || !lx->buf)
        return pdf_null_obj();
    if (pdf_lex_next(doc, lx, &tok) != 0 || tok.type == PDF_TOK_EOF)
        return pdf_null_obj();
    return parse_value(doc, lx, &tok, 0, 1);
}
