/*-------------------------------------------------------------------------
 * jsonval.c
 *      PostgreSQL-compatible json / jsonb value type.
 *
 * Implements the JsonVal API declared in pgvc.h.  Semantics were derived by
 * probing real PostgreSQL 18 servers; see golden/tests/jsonval_cases.txt for
 * the recorded oracle answers.
 *
 * Highlights
 *   - json_parse() keeps the raw source span of every node, so
 *     json_to_text(v, false) reproduces PostgreSQL's text-preserving `json`
 *     output byte for byte (whitespace, key order and duplicate keys intact).
 *   - The value tree itself is canonical jsonb: object keys sorted by
 *     (length, memcmp), duplicates collapsed keeping the last, numbers stored
 *     in PostgreSQL `numeric` display form.
 *   - Parsing, freeing, rendering, copying and comparison are iterative, so
 *     arbitrarily deep documents are handled without stack growth.  (Real
 *     PostgreSQL survives ~10k-19k levels and then raises
 *     "stack depth limit exceeded".)
 *   - Errors are reported through *err as "<message>\n<detail>" (or just
 *     "<message>" when PostgreSQL emits no DETAIL line).  The buffer belongs
 *     to this module and is valid until the next failing json_parse() call.
 *
 * Build: gcc -std=gnu11 -O2 -Wall -c jsonval.c
 *-------------------------------------------------------------------------
 */
#include "pgvc.h"

/* ---------------------------------------------------------------- kinds */
#define JV_NULL  0
#define JV_BOOL  1
#define JV_NUM   2
#define JV_STR   3
#define JV_ARR   4
#define JV_OBJ   5

/*
 * PostgreSQL's jsonb type ordering.  jsonb.h numbers the value types
 *   jbvNull=0 < jbvString < jbvNumeric < jbvBool < jbvArray=0x10 < jbvObject
 * which yields  Object > Array > Boolean > Number > String > Null.
 */
static const int jv_type_rank[6] = {
    /* JV_NULL */ 0,
    /* JV_BOOL */ 3,
    /* JV_NUM  */ 2,
    /* JV_STR  */ 1,
    /* JV_ARR  */ 4,
    /* JV_OBJ  */ 5
};

/* PostgreSQL numeric limits, confirmed against the server:
 * at most 131072 integral digits and a display scale of at most 16383. */
#define JV_MAX_INT_DIGITS  131072
#define JV_MAX_DSCALE      16383

/* ------------------------------------------------------------- memory -- */
static void *
jv_alloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (p == NULL) {
        fputs("jsonval: out of memory\n", stderr);
        abort();
    }
    return p;
}

static void *
jv_realloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (q == NULL) {
        fputs("jsonval: out of memory\n", stderr);
        abort();
    }
    return q;
}

static char *
jv_strndup(const char *s, size_t n)
{
    char *p = jv_alloc(n + 1);
    if (n)
        memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ------------------------------------------------------------ buffers -- */
typedef struct {
    char  *data;
    size_t len, cap;
} JBuf;

static void
jb_init(JBuf *b)
{
    b->data = NULL;
    b->len = b->cap = 0;
}

static void
jb_free(JBuf *b)
{
    free(b->data);
    jb_init(b);
}

static void
jb_need(JBuf *b, size_t extra)
{
    if (b->len + extra + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 64;
        while (nc < b->len + extra + 1)
            nc *= 2;
        b->data = jv_realloc(b->data, nc);
        b->cap = nc;
    }
}

static void
jb_add(JBuf *b, const void *p, size_t n)
{
    jb_need(b, n);
    if (n)
        memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void
jb_addc(JBuf *b, char c)
{
    jb_need(b, 1);
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

static void
jb_adds(JBuf *b, const char *s)
{
    jb_add(b, s, strlen(s));
}

static void
jb_addn(JBuf *b, char c, size_t n)
{
    jb_need(b, n);
    memset(b->data + b->len, c, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static char *
jb_steal(JBuf *b)
{
    char *r;
    if (b->data == NULL) {
        b->data = jv_alloc(1);
        b->data[0] = '\0';
    }
    r = b->data;
    jb_init(b);
    return r;
}

/* -------------------------------------------------- shared source text -- */
/*
 * Every node points at the original document text so that json_to_text(v,
 * false) can hand back the exact bytes PostgreSQL's `json` type would print,
 * including for sub-values obtained through json_get_key()/json_get_idx()
 * (PostgreSQL's json -> 'k' preserves the raw slice too).
 */
typedef struct JsonSrc {
    size_t refcnt;
    size_t len;
    char   data[];
} JsonSrc;

static JsonSrc *
src_new(const char *s, size_t len)
{
    JsonSrc *src = jv_alloc(sizeof(JsonSrc) + len + 1);
    src->refcnt = 1;
    src->len = len;
    if (len)
        memcpy(src->data, s, len);
    src->data[len] = '\0';
    return src;
}

static JsonSrc *
src_ref(JsonSrc *src)
{
    if (src)
        src->refcnt++;
    return src;
}

static void
src_unref(JsonSrc *src)
{
    if (src && --src->refcnt == 0)
        free(src);
}

/* ------------------------------------------------------------- values -- */
typedef struct JsonPair {
    char    *key;
    size_t   keylen;
    JsonVal *val;
} JsonPair;

struct JsonVal {
    uint8    kind;
    bool     bval;              /* JV_BOOL */
    JsonSrc *src;               /* shared original text (may be NULL) */
    size_t   raw_off, raw_len;  /* this value's span inside src */
    char    *num;               /* JV_NUM: canonical numeric text */
    char    *str;               /* JV_STR: decoded bytes (NUL terminated) */
    size_t   slen;
    JsonVal **elems;            /* JV_ARR */
    int       nelems, capelems;
    JsonPair *pairs;            /* JV_OBJ, canonical order */
    int       npairs, cappairs;
};

static JsonVal *
val_new(uint8 kind)
{
    JsonVal *v = jv_alloc(sizeof(JsonVal));
    memset(v, 0, sizeof(*v));
    v->kind = kind;
    return v;
}

static void
arr_push(JsonVal *a, JsonVal *e)
{
    if (a->nelems == a->capelems) {
        a->capelems = a->capelems ? a->capelems * 2 : 8;
        a->elems = jv_realloc(a->elems, (size_t) a->capelems * sizeof(JsonVal *));
    }
    a->elems[a->nelems++] = e;
}

static void
obj_push(JsonVal *o, char *key, size_t keylen, JsonVal *val)
{
    if (o->npairs == o->cappairs) {
        o->cappairs = o->cappairs ? o->cappairs * 2 : 8;
        o->pairs = jv_realloc(o->pairs, (size_t) o->cappairs * sizeof(JsonPair));
    }
    o->pairs[o->npairs].key = key;
    o->pairs[o->npairs].keylen = keylen;
    o->pairs[o->npairs].val = val;
    o->npairs++;
}

/* ------------------------------------------------- key / string compare -- */
/*
 * PostgreSQL compares jsonb string *scalars* with varstr_cmp(); the reference
 * cluster runs the C collation, i.e. plain byte order with the shorter string
 * first on a common prefix.
 */
static int
str_cmp_bytes(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t n = alen < blen ? alen : blen;
    int    r = n ? memcmp(a, b, n) : 0;

    if (r != 0)
        return r < 0 ? -1 : 1;
    if (alen == blen)
        return 0;
    return alen < blen ? -1 : 1;
}

/*
 * Object *key* ordering inside a jsonb value is different: shorter keys sort
 * first and only then byte order decides (lengthCompareJsonbStringValue).
 */
static int
key_cmp(const char *a, size_t alen, const char *b, size_t blen)
{
    int r;

    if (alen != blen)
        return alen > blen ? 1 : -1;
    r = alen ? memcmp(a, b, alen) : 0;
    if (r == 0)
        return 0;
    return r < 0 ? -1 : 1;
}

/* Sort key used by uniqueifyJsonbObject(): ties keep the *later* pair first,
 * so that dropping everything but the first of each run keeps the last
 * occurrence of a duplicated key. */
typedef struct {
    JsonPair pair;
    int      order;
} KeySortItem;

static int
key_sort_cmp(const void *ap, const void *bp)
{
    const KeySortItem *a = ap;
    const KeySortItem *b = bp;
    int r = key_cmp(a->pair.key, a->pair.keylen, b->pair.key, b->pair.keylen);

    if (r != 0)
        return r;
    return a->order > b->order ? -1 : 1;
}

static bool obj_canonicalise(JsonVal *o);   /* forward; true if a key repeated */

/* ----------------------------------------------------------- free/copy -- */
/*
 * Iterative teardown: a 100000-level document must be freeable.
 */
void
json_free(JsonVal *v)
{
    JsonVal **stack;
    int sn = 0, scap;

    if (v == NULL)
        return;

    scap = 64;
    stack = jv_alloc((size_t) scap * sizeof(JsonVal *));
    stack[sn++] = v;

    while (sn > 0) {
        JsonVal *cur = stack[--sn];
        int i;

        if (cur == NULL)
            continue;

        if (cur->kind == JV_ARR || cur->kind == JV_OBJ) {
            int n = (cur->kind == JV_ARR) ? cur->nelems : cur->npairs;
            if (sn + n > scap) {
                while (sn + n > scap)
                    scap *= 2;
                stack = jv_realloc(stack, (size_t) scap * sizeof(JsonVal *));
            }
            if (cur->kind == JV_ARR) {
                for (i = 0; i < cur->nelems; i++)
                    stack[sn++] = cur->elems[i];
                free(cur->elems);
            } else {
                for (i = 0; i < cur->npairs; i++) {
                    free(cur->pairs[i].key);
                    stack[sn++] = cur->pairs[i].val;
                }
                free(cur->pairs);
            }
        }
        free(cur->num);
        free(cur->str);
        src_unref(cur->src);
        free(cur);
    }
    free(stack);
}

/* Shallow clone of one node (children are filled in by the caller). */
static JsonVal *
val_clone_shallow(const JsonVal *v)
{
    JsonVal *n = val_new(v->kind);

    n->bval = v->bval;
    n->src = src_ref(v->src);
    n->raw_off = v->raw_off;
    n->raw_len = v->raw_len;
    if (v->num)
        n->num = jv_strndup(v->num, strlen(v->num));
    if (v->str) {
        n->str = jv_strndup(v->str, v->slen);
        n->slen = v->slen;
    }
    return n;
}

typedef struct {
    const JsonVal *src;
    JsonVal       *dst;
    int            i;
} CopyFrame;

JsonVal *
json_copy(const JsonVal *v)
{
    CopyFrame *st;
    int sn = 0, scap = 64;
    JsonVal *root;

    if (v == NULL)
        return NULL;

    root = val_clone_shallow(v);
    if (v->kind != JV_ARR && v->kind != JV_OBJ)
        return root;

    st = jv_alloc((size_t) scap * sizeof(CopyFrame));
    st[sn].src = v;
    st[sn].dst = root;
    st[sn].i = 0;
    sn++;

    while (sn > 0) {
        CopyFrame *f = &st[sn - 1];
        const JsonVal *s = f->src;
        int n = (s->kind == JV_ARR) ? s->nelems : s->npairs;

        if (f->i >= n) {
            sn--;
            continue;
        }
        {
            const JsonVal *child = (s->kind == JV_ARR)
                ? s->elems[f->i] : s->pairs[f->i].val;
            JsonVal *copy = val_clone_shallow(child);

            if (s->kind == JV_ARR)
                arr_push(f->dst, copy);
            else
                obj_push(f->dst,
                         jv_strndup(s->pairs[f->i].key, s->pairs[f->i].keylen),
                         s->pairs[f->i].keylen, copy);
            f->i++;

            if (child->kind == JV_ARR || child->kind == JV_OBJ) {
                if (sn == scap) {
                    scap *= 2;
                    st = jv_realloc(st, (size_t) scap * sizeof(CopyFrame));
                }
                st[sn].src = child;
                st[sn].dst = copy;
                st[sn].i = 0;
                sn++;
            }
        }
    }
    free(st);
    return root;
}

/* --------------------------------------------- numeric canonicalisation -- */
/*
 * PostgreSQL stores JSON numbers as `numeric`, so the jsonb rendering is
 * numeric_out() of the literal.  numeric's display scale is
 *      dscale = max(0, (digits after '.') - exponent)
 * and numeric_out never uses scientific notation, so the whole job is a
 * textual shift of the decimal point:
 *      1.0000 -> 1.0000     1E2  -> 100      1e-3     -> 0.001
 *      12300e-2 -> 123.00   10e-1 -> 1.0     2.0e-5   -> 0.000020
 *      -0 -> 0              -0.0 -> 0.0      0.00e5   -> 0
 *
 * Returns a malloc'd canonical string.  *overflow is set when the result
 * would exceed numeric's limits (PostgreSQL then raises "value overflows
 * numeric format"); the canonical text is still produced for callers that
 * tolerate it (the text-preserving `json` type never converts numbers).
 */
static char *
num_canonical(const char *tok, size_t len, bool *overflow)
{
    size_t i = 0;
    bool   neg = false;
    const char *idig;
    size_t nint = 0;
    const char *fdig = NULL;
    size_t nfrac = 0;
    long long expval = 0;
    long long shift;
    JBuf digits, out;
    size_t d0, nd;
    bool   iszero;

    *overflow = false;

    if (i < len && (tok[i] == '-' || tok[i] == '+')) {
        neg = (tok[i] == '-');
        i++;
    }
    idig = tok + i;
    while (i < len && tok[i] >= '0' && tok[i] <= '9') {
        i++;
        nint++;
    }
    if (i < len && tok[i] == '.') {
        i++;
        fdig = tok + i;
        while (i < len && tok[i] >= '0' && tok[i] <= '9') {
            i++;
            nfrac++;
        }
    }
    if (i < len && (tok[i] == 'e' || tok[i] == 'E')) {
        bool eneg = false;
        i++;
        if (i < len && (tok[i] == '+' || tok[i] == '-')) {
            eneg = (tok[i] == '-');
            i++;
        }
        while (i < len && tok[i] >= '0' && tok[i] <= '9') {
            if (expval < 1000000000LL)
                expval = expval * 10 + (tok[i] - '0');
            i++;
        }
        if (eneg)
            expval = -expval;
    }

    /* Assemble the significand's digits, then locate the decimal point. */
    jb_init(&digits);
    jb_add(&digits, idig, nint);
    if (nfrac)
        jb_add(&digits, fdig, nfrac);

    shift = expval - (long long) nfrac;   /* decimal point moves right by this */

    /* Strip leading zeros of the significand. */
    d0 = 0;
    while (d0 < digits.len && digits.data[d0] == '0')
        d0++;
    nd = digits.len - d0;
    iszero = (nd == 0);

    /* Display scale == the number of fractional digits we end up printing. */
    {
        long long dscale = shift < 0 ? -shift : 0;
        if (dscale > JV_MAX_DSCALE)
            *overflow = true;
    }

    jb_init(&out);
    if (iszero) {
        long long dscale = shift < 0 ? -shift : 0;
        jb_addc(&out, '0');
        if (dscale > 0) {
            if (dscale > JV_MAX_DSCALE) {
                /* Do not materialise an absurd string for an overflow case. */
                jb_free(&digits);
                jb_free(&out);
                return jv_strndup("0", 1);
            }
            jb_addc(&out, '.');
            jb_addn(&out, '0', (size_t) dscale);
        }
        jb_free(&digits);
        return jb_steal(&out);
    }

    if (shift >= 0) {
        long long nintout = (long long) nd + shift;
        if (nintout > JV_MAX_INT_DIGITS) {
            *overflow = true;
            jb_free(&digits);
            jb_free(&out);
            return jv_strndup("0", 1);
        }
        if (neg)
            jb_addc(&out, '-');
        jb_add(&out, digits.data + d0, nd);
        jb_addn(&out, '0', (size_t) shift);
    } else {
        long long k = -shift;                 /* fractional digit count */
        if (k > JV_MAX_DSCALE || (long long) nd - k > JV_MAX_INT_DIGITS) {
            *overflow = true;
            jb_free(&digits);
            jb_free(&out);
            return jv_strndup("0", 1);
        }
        if (neg)
            jb_addc(&out, '-');
        if (k < (long long) nd) {
            jb_add(&out, digits.data + d0, (size_t) ((long long) nd - k));
            jb_addc(&out, '.');
            jb_add(&out, digits.data + d0 + (nd - (size_t) k), (size_t) k);
        } else {
            jb_addc(&out, '0');
            jb_addc(&out, '.');
            jb_addn(&out, '0', (size_t) (k - (long long) nd));
            jb_add(&out, digits.data + d0, nd);
        }
    }
    jb_free(&digits);
    return jb_steal(&out);
}

/* Compare two canonical numeric strings. */
static int
num_text_cmp(const char *a, const char *b)
{
    int  sa = (*a == '-'), sb = (*b == '-');
    const char *x, *y;
    const char *xd, *yd;
    size_t xi, yi, xf, yf, i, n;
    int r;

    if (sa != sb)
        return sa ? -1 : 1;
    x = a + sa;
    y = b + sb;

    xd = strchr(x, '.');
    yd = strchr(y, '.');
    xi = xd ? (size_t) (xd - x) : strlen(x);
    yi = yd ? (size_t) (yd - y) : strlen(y);
    xf = xd ? strlen(xd + 1) : 0;
    yf = yd ? strlen(yd + 1) : 0;

    r = 0;
    if (xi != yi)
        r = xi > yi ? 1 : -1;
    else {
        int m = memcmp(x, y, xi);
        if (m != 0)
            r = m < 0 ? -1 : 1;
    }
    if (r == 0) {
        n = xf > yf ? xf : yf;
        for (i = 0; i < n; i++) {
            char cx = i < xf ? xd[1 + i] : '0';
            char cy = i < yf ? yd[1 + i] : '0';
            if (cx != cy) {
                r = cx < cy ? -1 : 1;
                break;
            }
        }
    }
    return sa ? -r : r;
}

/* Control bytes we care about, spelled numerically so the source stays
 * grep-able and free of literal escapes. */
#define JV_C_BS   ((char) 0x08)   /* backspace */
#define JV_C_TAB  ((char) 0x09)
#define JV_C_LF   ((char) 0x0A)
#define JV_C_FF   ((char) 0x0C)   /* form feed */
#define JV_C_CR   ((char) 0x0D)

/* ============================== lexer ==================================== */
typedef enum {
    T_INVALID = 0,
    T_STRING, T_NUMBER, T_TRUE, T_FALSE, T_NULL,
    T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET, T_COMMA, T_COLON,
    T_END
} JTok;

/* Mirrors PostgreSQL's JsonParseErrorType, minus the codes we cannot hit. */
typedef enum {
    JE_OK = 0,
    JE_INVALID_TOKEN,
    JE_ESCAPING_INVALID,
    JE_ESCAPING_REQUIRED,
    JE_UNICODE_ESCAPE_FORMAT,
    JE_UNICODE_HIGH_SURROGATE,
    JE_UNICODE_LOW_SURROGATE,
    JE_UNICODE_CODE_POINT_ZERO,
    JE_EXPECTED_MORE,
    JE_EXPECTED_END,
    JE_EXPECTED_JSON,
    JE_EXPECTED_STRING,
    JE_EXPECTED_COLON,
    JE_EXPECTED_ARRAY_NEXT,
    JE_EXPECTED_OBJECT_FIRST,
    JE_EXPECTED_OBJECT_NEXT,
    JE_NUMERIC_OVERFLOW
} JErr;

typedef struct {
    const char *input;
    size_t      input_len;
    const char *tok_start;
    const char *tok_term;
    const char *prev_tok_term;
    JTok        tok;
    bool        decode;         /* jsonb strictness: validate \u escapes */
    bool        dup_key;        /* an object had the same key twice */
    JBuf        strval;         /* decoded payload of a T_STRING token */
} JLex;

/* PostgreSQL's JSON_ALPHANUMERIC_CHAR(): what gets swallowed into a bad token. */
static inline bool
jv_alnum_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || (c & 0x80) != 0;
}

/* pg_encoding_mblen() for UTF-8. */
static int
utf8_mblen(const char *s)
{
    unsigned char c = (unsigned char) *s;

    if ((c & 0x80) == 0)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 1;
}

static void
utf8_append(JBuf *b, unsigned int cp)
{
    if (cp < 0x80) {
        jb_addc(b, (char) cp);
    } else if (cp < 0x800) {
        jb_addc(b, (char) (0xC0 | (cp >> 6)));
        jb_addc(b, (char) (0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        jb_addc(b, (char) (0xE0 | (cp >> 12)));
        jb_addc(b, (char) (0x80 | ((cp >> 6) & 0x3F)));
        jb_addc(b, (char) (0x80 | (cp & 0x3F)));
    } else {
        jb_addc(b, (char) (0xF0 | (cp >> 18)));
        jb_addc(b, (char) (0x80 | ((cp >> 12) & 0x3F)));
        jb_addc(b, (char) (0x80 | ((cp >> 6) & 0x3F)));
        jb_addc(b, (char) (0x80 | (cp & 0x3F)));
    }
}

/* Clamp s + mblen(s) to the end of the input, as PostgreSQL does. */
static const char *
clamp_term(JLex *lex, const char *s)
{
    const char *end = lex->input + lex->input_len;
    const char *t;

    if (s >= end)
        return end;
    t = s + utf8_mblen(s);
    return t <= end ? t : s;
}

static JErr
json_lex_string(JLex *lex)
{
    const char *s = lex->tok_start;
    size_t      len = (size_t) (lex->tok_start - lex->input);
    int         hi_surrogate = -1;

    lex->strval.len = 0;
    if (lex->strval.data)
        lex->strval.data[0] = 0;

    for (;;) {
        s++;
        len++;
        if (len >= lex->input_len) {
            lex->tok_term = clamp_term(lex, s);
            return JE_INVALID_TOKEN;
        }
        if (*s == '"')
            break;
        if (*s == 0x5C) {                       /* backslash */
            s++;
            len++;
            if (len >= lex->input_len) {
                lex->tok_term = clamp_term(lex, s);
                return JE_INVALID_TOKEN;
            }
            if (*s == 'u') {
                int i;
                unsigned int ch = 0;

                for (i = 1; i <= 4; i++) {
                    s++;
                    len++;
                    if (len >= lex->input_len) {
                        lex->tok_term = clamp_term(lex, s);
                        return JE_INVALID_TOKEN;
                    }
                    if (*s >= '0' && *s <= '9')
                        ch = (ch * 16) + (unsigned) (*s - '0');
                    else if (*s >= 'a' && *s <= 'f')
                        ch = (ch * 16) + (unsigned) (*s - 'a') + 10;
                    else if (*s >= 'A' && *s <= 'F')
                        ch = (ch * 16) + (unsigned) (*s - 'A') + 10;
                    else {
                        lex->tok_term = clamp_term(lex, s);
                        return JE_UNICODE_ESCAPE_FORMAT;
                    }
                }
                /*
                 * PostgreSQL only validates surrogate pairing and the zero
                 * code point when it is actually materialising the string,
                 * i.e. for jsonb.  The `json` input function skips this
                 * block, which is why a lone surrogate is accepted there.
                 */
                if (ch >= 0xD800 && ch <= 0xDBFF) {
                    if (hi_surrogate != -1) {
                        if (lex->decode) {
                            lex->tok_term = clamp_term(lex, s);
                            return JE_UNICODE_HIGH_SURROGATE;
                        }
                        utf8_append(&lex->strval, (unsigned) hi_surrogate);
                    }
                    hi_surrogate = (int) ch;
                    continue;
                } else if (ch >= 0xDC00 && ch <= 0xDFFF) {
                    if (hi_surrogate == -1) {
                        if (lex->decode) {
                            lex->tok_term = clamp_term(lex, s);
                            return JE_UNICODE_LOW_SURROGATE;
                        }
                        utf8_append(&lex->strval, ch);
                        continue;
                    }
                    ch = 0x10000 + (((unsigned) hi_surrogate - 0xD800) << 10) +
                         (ch - 0xDC00);
                    hi_surrogate = -1;
                } else if (hi_surrogate != -1) {
                    if (lex->decode) {
                        lex->tok_term = clamp_term(lex, s);
                        return JE_UNICODE_LOW_SURROGATE;
                    }
                    utf8_append(&lex->strval, (unsigned) hi_surrogate);
                    hi_surrogate = -1;
                }
                if (ch == 0) {
                    if (lex->decode) {
                        lex->tok_term = clamp_term(lex, s);
                        return JE_UNICODE_CODE_POINT_ZERO;
                    }
                    jb_addc(&lex->strval, (char) 0);
                    continue;
                }
                utf8_append(&lex->strval, ch);
            } else {
                if (hi_surrogate != -1) {
                    if (lex->decode) {
                        lex->tok_term = clamp_term(lex, s);
                        return JE_UNICODE_LOW_SURROGATE;
                    }
                    utf8_append(&lex->strval, (unsigned) hi_surrogate);
                    hi_surrogate = -1;
                }
                switch (*s) {
                    case '"':
                    case 0x5C:
                    case '/':
                        jb_addc(&lex->strval, *s);
                        break;
                    case 'b': jb_addc(&lex->strval, JV_C_BS);  break;
                    case 'f': jb_addc(&lex->strval, JV_C_FF);  break;
                    case 'n': jb_addc(&lex->strval, JV_C_LF);  break;
                    case 'r': jb_addc(&lex->strval, JV_C_CR);  break;
                    case 't': jb_addc(&lex->strval, JV_C_TAB); break;
                    default:
                        /* The detail names just the offending escape char. */
                        lex->tok_start = s;
                        lex->tok_term = clamp_term(lex, s);
                        return JE_ESCAPING_INVALID;
                }
            }
        } else {
            /*
             * Ordinary character.  PostgreSQL 17+ checks the dangling high
             * surrogate here *before* the control-character rule, so
             * '"\\ud800' followed by a raw control byte reports the missing
             * low surrogate rather than the unescaped control character.
             */
            if (hi_surrogate != -1) {
                if (lex->decode) {
                    lex->tok_term = clamp_term(lex, s);
                    return JE_UNICODE_LOW_SURROGATE;
                }
                utf8_append(&lex->strval, (unsigned) hi_surrogate);
                hi_surrogate = -1;
            }
            if ((unsigned char) *s < 32) {
                /* Per RFC 4627 these MUST be escaped. */
                lex->tok_term = s;
                return JE_ESCAPING_REQUIRED;
            }
            jb_addc(&lex->strval, *s);
        }
    }

    if (hi_surrogate != -1) {
        if (lex->decode) {
            lex->tok_term = clamp_term(lex, s);
            return JE_UNICODE_LOW_SURROGATE;
        }
        utf8_append(&lex->strval, (unsigned) hi_surrogate);
    }

    lex->prev_tok_term = lex->tok_term;
    lex->tok_term = s + 1;
    lex->tok = T_STRING;
    return JE_OK;
}

/*
 * json_lex_number(): a faithful port.  The trailing-garbage loop is what makes
 * '0x10', '1_000' and '1e1e1' report the whole run as the bad token, while
 * '1.2.3' stops after '1.2' and the parser then blames the stray '.'.
 */
static JErr
json_lex_number(JLex *lex, const char *s)
{
    bool   error = false;
    size_t len = (size_t) (s - lex->input);

    if (len < lex->input_len && *s == '0') {
        s++;
        len++;
    } else if (len < lex->input_len && *s >= '1' && *s <= '9') {
        do {
            s++;
            len++;
        } while (len < lex->input_len && *s >= '0' && *s <= '9');
    } else {
        error = true;
    }

    if (len < lex->input_len && *s == '.') {
        s++;
        len++;
        if (len == lex->input_len || *s < '0' || *s > '9')
            error = true;
        else {
            do {
                s++;
                len++;
            } while (len < lex->input_len && *s >= '0' && *s <= '9');
        }
    }

    if (len < lex->input_len && (*s == 'e' || *s == 'E')) {
        s++;
        len++;
        if (len < lex->input_len && (*s == '+' || *s == '-')) {
            s++;
            len++;
        }
        if (len == lex->input_len || *s < '0' || *s > '9')
            error = true;
        else {
            do {
                s++;
                len++;
            } while (len < lex->input_len && *s >= '0' && *s <= '9');
        }
    }

    for (; len < lex->input_len && jv_alnum_char((unsigned char) *s); s++, len++)
        error = true;

    lex->prev_tok_term = lex->tok_term;
    lex->tok_term = s;
    lex->tok = T_NUMBER;
    return error ? JE_INVALID_TOKEN : JE_OK;
}

static JErr
json_lex(JLex *lex)
{
    const char *s = lex->tok_term;
    size_t      len = (size_t) (s - lex->input);

    while (len < lex->input_len &&
           (*s == ' ' || *s == JV_C_TAB || *s == JV_C_LF || *s == JV_C_CR)) {
        s++;
        len++;
    }
    lex->tok_start = s;

    if (len >= lex->input_len) {
        lex->prev_tok_term = lex->tok_term;
        lex->tok_term = s;
        lex->tok = T_END;
        return JE_OK;
    }

    switch (*s) {
        case '{': case '}': case '[': case ']': case ',': case ':':
            lex->prev_tok_term = lex->tok_term;
            lex->tok_term = s + 1;
            lex->tok = (*s == '{') ? T_LBRACE
                     : (*s == '}') ? T_RBRACE
                     : (*s == '[') ? T_LBRACKET
                     : (*s == ']') ? T_RBRACKET
                     : (*s == ',') ? T_COMMA : T_COLON;
            return JE_OK;
        case '"':
            return json_lex_string(lex);
        case '-':
            return json_lex_number(lex, s + 1);
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return json_lex_number(lex, s);
        default: {
            const char *p = s;

            while (len < lex->input_len && jv_alnum_char((unsigned char) *p)) {
                p++;
                len++;
            }
            if (p == s) {
                /* Unexpected punctuation: blame exactly one character. */
                lex->prev_tok_term = lex->tok_term;
                lex->tok_term = s + 1;
                lex->tok = T_INVALID;
                return JE_INVALID_TOKEN;
            }
            lex->prev_tok_term = lex->tok_term;
            lex->tok_term = p;
            lex->tok = T_INVALID;
            if (p - s == 4) {
                if (memcmp(s, "true", 4) == 0) {
                    lex->tok = T_TRUE;
                    return JE_OK;
                }
                if (memcmp(s, "null", 4) == 0) {
                    lex->tok = T_NULL;
                    return JE_OK;
                }
                return JE_INVALID_TOKEN;
            }
            if (p - s == 5 && memcmp(s, "false", 5) == 0) {
                lex->tok = T_FALSE;
                return JE_OK;
            }
            return JE_INVALID_TOKEN;
        }
    }
}

/* ========================= error reporting ============================== */
/*
 * PostgreSQL reports a primary message plus a DETAIL line.  We hand both back
 * through the single `err` pointer joined by a newline:
 *
 *      invalid input syntax for type json\nToken "x" is invalid.
 *
 * When PostgreSQL emits no DETAIL (numeric overflow) only the message is
 * returned, without a trailing newline.  The storage belongs to this module
 * and stays valid until the next failing json_parse().
 */
static char *jv_err_store = NULL;

static const char *
jv_err_message(JErr e)
{
    if (e == JE_UNICODE_CODE_POINT_ZERO)
        return "unsupported Unicode escape sequence";   /* SQLSTATE 22P05 */
    if (e == JE_NUMERIC_OVERFLOW)
        return "value overflows numeric format";        /* SQLSTATE 22003 */
    return "invalid input syntax for type json";        /* SQLSTATE 22P02 */
}

/* Byte offset just past the token the last parse error stopped on; the
 * "JSON data, line 1: ..." context line is cut there. */
static __thread size_t g_err_offset;
size_t json_last_error_offset(void) { return g_err_offset; }

static void
jv_set_error(const char **err, JErr e, JLex *lex)
{
    JBuf b;
    size_t tlen = 0;
    const char *tok = "";

    if (lex != NULL && lex->input != NULL && lex->tok_term >= lex->input)
        g_err_offset = (size_t) (lex->tok_term - lex->input);
    else
        g_err_offset = 0;
    if (err == NULL)
        return;
    if (lex != NULL && lex->tok_start != NULL && lex->tok_term >= lex->tok_start) {
        tok = lex->tok_start;
        tlen = (size_t) (lex->tok_term - lex->tok_start);
    }

    jb_init(&b);
    jb_adds(&b, jv_err_message(e));

    switch (e) {
        case JE_INVALID_TOKEN:
            jb_adds(&b, "\nToken \"");
            jb_add(&b, tok, tlen);
            jb_adds(&b, "\" is invalid.");
            break;
        case JE_ESCAPING_INVALID:
            jb_adds(&b, "\nEscape sequence \"\\");
            jb_add(&b, tok, tlen);
            jb_adds(&b, "\" is invalid.");
            break;
        case JE_ESCAPING_REQUIRED: {
            char tmp[64];
            unsigned char c = lex ? (unsigned char) *lex->tok_term : 0;
            snprintf(tmp, sizeof tmp,
                     "\nCharacter with value 0x%02x must be escaped.", c);
            jb_adds(&b, tmp);
            break;
        }
        case JE_UNICODE_ESCAPE_FORMAT:
            jb_adds(&b, "\n\"\\u\" must be followed by four hexadecimal digits.");
            break;
        case JE_UNICODE_HIGH_SURROGATE:
            jb_adds(&b, "\nUnicode high surrogate must not follow a high surrogate.");
            break;
        case JE_UNICODE_LOW_SURROGATE:
            jb_adds(&b, "\nUnicode low surrogate must follow a high surrogate.");
            break;
        case JE_UNICODE_CODE_POINT_ZERO:
            jb_adds(&b, "\n\\u0000 cannot be converted to text.");
            break;
        case JE_EXPECTED_MORE:
            jb_adds(&b, "\nThe input string ended unexpectedly.");
            break;
        case JE_EXPECTED_END:
            jb_adds(&b, "\nExpected end of input, but found \"");
            jb_add(&b, tok, tlen);
            jb_adds(&b, "\".");
            break;
        case JE_EXPECTED_JSON:
            jb_adds(&b, "\nExpected JSON value, but found \"");
            jb_add(&b, tok, tlen);
            jb_adds(&b, "\".");
            break;
        case JE_EXPECTED_STRING:
            jb_adds(&b, "\nExpected string, but found \"");
            jb_add(&b, tok, tlen);
            jb_adds(&b, "\".");
            break;
        case JE_EXPECTED_COLON:
            jb_adds(&b, "\nExpected \":\", but found \"");
            jb_add(&b, tok, tlen);
            jb_adds(&b, "\".");
            break;
        case JE_EXPECTED_ARRAY_NEXT:
            jb_adds(&b, "\nExpected \",\" or \"]\", but found \"");
            jb_add(&b, tok, tlen);
            jb_adds(&b, "\".");
            break;
        case JE_EXPECTED_OBJECT_FIRST:
            jb_adds(&b, "\nExpected string or \"}\", but found \"");
            jb_add(&b, tok, tlen);
            jb_adds(&b, "\".");
            break;
        case JE_EXPECTED_OBJECT_NEXT:
            jb_adds(&b, "\nExpected \",\" or \"}\", but found \"");
            jb_add(&b, tok, tlen);
            jb_adds(&b, "\".");
            break;
        case JE_NUMERIC_OVERFLOW:
        default:
            break;                              /* message only, no DETAIL */
    }

    free(jv_err_store);
    jv_err_store = jb_steal(&b);
    *err = jv_err_store;
}

/* PostgreSQL's report_parse_error(): running off the end always wins. */
static JErr
jv_report(JLex *lex, JErr ctx)
{
    if (lex->tok_start == NULL || lex->tok == T_END)
        return JE_EXPECTED_MORE;
    return ctx;
}

/* ============================== parser ================================== */
typedef struct {
    JsonVal *node;
    size_t   vstart;            /* offset of this container's opening token */
    char    *key;               /* pending object key */
    size_t   keylen;
} PFrame;

enum { ST_VALUE, ST_OBJ_FIELD, ST_AFTER_VALUE };

/*
 * Canonicalise an object in place: sort keys by (length, memcmp) and drop
 * duplicates keeping the last occurrence, exactly as uniqueifyJsonbObject().
 */
static bool
obj_canonicalise(JsonVal *o)
{
    KeySortItem *items;
    int i, w;

    if (o->npairs <= 1)
        return false;

    items = jv_alloc((size_t) o->npairs * sizeof(KeySortItem));
    for (i = 0; i < o->npairs; i++) {
        items[i].pair = o->pairs[i];
        items[i].order = i;
    }
    qsort(items, (size_t) o->npairs, sizeof(KeySortItem), key_sort_cmp);

    w = 0;
    for (i = 0; i < o->npairs; i++) {
        if (w > 0 &&
            key_cmp(items[i].pair.key, items[i].pair.keylen,
                    o->pairs[w - 1].key, o->pairs[w - 1].keylen) == 0) {
            /* Later duplicate sorted first, so this one loses. */
            free(items[i].pair.key);
            json_free(items[i].pair.val);
            continue;
        }
        o->pairs[w++] = items[i].pair;
    }
    bool dup = (w < o->npairs);
    o->npairs = w;
    free(items);
    return dup;
}

static JsonVal *
make_scalar(JLex *lex, JErr *e)
{
    JsonVal *v;

    *e = JE_OK;
    switch (lex->tok) {
        case T_NULL:
            return val_new(JV_NULL);
        case T_TRUE:
            v = val_new(JV_BOOL);
            v->bval = true;
            return v;
        case T_FALSE:
            v = val_new(JV_BOOL);
            v->bval = false;
            return v;
        case T_STRING:
            v = val_new(JV_STR);
            v->slen = lex->strval.len;
            v->str = jv_strndup(lex->strval.data ? lex->strval.data : "",
                                lex->strval.len);
            return v;
        case T_NUMBER: {
            bool ovf = false;
            v = val_new(JV_NUM);
            v->num = num_canonical(lex->tok_start,
                                   (size_t) (lex->tok_term - lex->tok_start),
                                   &ovf);
            if (ovf && lex->decode) {
                json_free(v);
                *e = JE_NUMERIC_OVERFLOW;
                return NULL;
            }
            return v;
        }
        default:
            return NULL;
    }
}

/*
 * Iterative recursive-descent equivalent of PostgreSQL's pg_parse_json().
 * The state transitions mirror parse_object()/parse_array()/parse_scalar()
 * one for one so that the error reported for any malformed input is the same.
 */
static JErr
parse_doc(JLex *lex, JsonVal **out)
{
    PFrame  *st = NULL;
    int      sn = 0, scap = 0;
    JsonVal *cur = NULL;
    JsonVal *result = NULL;
    int      state = ST_VALUE;
    JErr     e;

    *out = NULL;

    e = json_lex(lex);
    if (e != JE_OK)
        goto fail;

    for (;;) {
        if (state == ST_VALUE) {
            size_t vs = (size_t) (lex->tok_start - lex->input);

            if (lex->tok == T_LBRACE || lex->tok == T_LBRACKET) {
                bool isobj = (lex->tok == T_LBRACE);
                JsonVal *c = val_new(isobj ? JV_OBJ : JV_ARR);

                e = json_lex(lex);
                if (e != JE_OK) {
                    json_free(c);
                    goto fail;
                }
                if (sn == scap) {
                    scap = scap ? scap * 2 : 32;
                    st = jv_realloc(st, (size_t) scap * sizeof(PFrame));
                }
                st[sn].node = c;
                st[sn].vstart = vs;
                st[sn].key = NULL;
                st[sn].keylen = 0;
                sn++;

                if (isobj) {
                    if (lex->tok == T_STRING) {
                        state = ST_OBJ_FIELD;
                    } else if (lex->tok == T_RBRACE) {
                        e = json_lex(lex);
                        if (e != JE_OK)
                            goto fail;
                        goto close_container;
                    } else {
                        e = jv_report(lex, JE_EXPECTED_OBJECT_FIRST);
                        goto fail;
                    }
                } else {
                    if (lex->tok == T_RBRACKET) {
                        e = json_lex(lex);
                        if (e != JE_OK)
                            goto fail;
                        goto close_container;
                    }
                    state = ST_VALUE;
                }
                continue;
            }

            if (lex->tok == T_STRING || lex->tok == T_NUMBER ||
                lex->tok == T_TRUE || lex->tok == T_FALSE ||
                lex->tok == T_NULL) {
                cur = make_scalar(lex, &e);
                if (e != JE_OK)
                    goto fail;
                cur->raw_off = vs;
                e = json_lex(lex);
                if (e != JE_OK)
                    goto fail;
                cur->raw_len = (size_t) (lex->prev_tok_term - lex->input) - vs;
                state = ST_AFTER_VALUE;
                continue;
            }

            e = jv_report(lex, JE_EXPECTED_JSON);
            goto fail;
        }

        if (state == ST_OBJ_FIELD) {
            PFrame *f = &st[sn - 1];

            if (lex->tok != T_STRING) {
                e = jv_report(lex, JE_EXPECTED_STRING);
                goto fail;
            }
            free(f->key);
            f->keylen = lex->strval.len;
            f->key = jv_strndup(lex->strval.data ? lex->strval.data : "",
                                lex->strval.len);
            e = json_lex(lex);
            if (e != JE_OK)
                goto fail;
            if (lex->tok != T_COLON) {
                e = jv_report(lex, JE_EXPECTED_COLON);
                goto fail;
            }
            e = json_lex(lex);
            if (e != JE_OK)
                goto fail;
            state = ST_VALUE;
            continue;
        }

        /* ST_AFTER_VALUE: `cur` is a finished value. */
        if (sn == 0) {
            result = cur;
            cur = NULL;
            break;
        }
        {
            PFrame *f = &st[sn - 1];

            if (f->node->kind == JV_ARR) {
                arr_push(f->node, cur);
                cur = NULL;
                if (lex->tok == T_COMMA) {
                    e = json_lex(lex);
                    if (e != JE_OK)
                        goto fail;
                    state = ST_VALUE;
                    continue;
                }
                if (lex->tok != T_RBRACKET) {
                    e = jv_report(lex, JE_EXPECTED_ARRAY_NEXT);
                    goto fail;
                }
                e = json_lex(lex);
                if (e != JE_OK)
                    goto fail;
            } else {
                obj_push(f->node, f->key, f->keylen, cur);
                f->key = NULL;
                cur = NULL;
                if (lex->tok == T_COMMA) {
                    e = json_lex(lex);
                    if (e != JE_OK)
                        goto fail;
                    state = ST_OBJ_FIELD;
                    continue;
                }
                if (lex->tok != T_RBRACE) {
                    e = jv_report(lex, JE_EXPECTED_OBJECT_NEXT);
                    goto fail;
                }
                e = json_lex(lex);
                if (e != JE_OK)
                    goto fail;
            }
        }

    close_container:
        {
            PFrame *f = &st[--sn];

            cur = f->node;
            cur->raw_off = f->vstart;
            cur->raw_len = (size_t) (lex->prev_tok_term - lex->input) - f->vstart;
            if (cur->kind == JV_OBJ && obj_canonicalise(cur))
                lex->dup_key = true;
            free(f->key);
            f->key = NULL;
            state = ST_AFTER_VALUE;
            continue;
        }
    }

    /* lex_expect(JSON_PARSE_END, ...) */
    if (lex->tok != T_END) {
        e = jv_report(lex, JE_EXPECTED_END);
        cur = result;
        result = NULL;
        goto fail;
    }

    free(st);
    *out = result;
    return JE_OK;

fail:
    json_free(cur);
    json_free(result);
    while (sn > 0) {
        sn--;
        free(st[sn].key);
        json_free(st[sn].node);
    }
    free(st);
    return e;
}

/* ============================= rendering ================================ */
/*
 * PostgreSQL escapes only '"', the backslash and the C0 controls; the five
 * controls with short forms use them, everything else below 0x20 becomes
 * lower-case \u00xx.  0x7f and all non-ASCII UTF-8 pass through untouched.
 */
static void
escape_json_string(JBuf *b, const char *s, size_t len)
{
    size_t i;

    jb_addc(b, '"');
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char) s[i];

        switch (c) {
            case 0x08: jb_adds(b, "\\b");  break;
            case 0x09: jb_adds(b, "\\t");  break;
            case 0x0A: jb_adds(b, "\\n");  break;
            case 0x0C: jb_adds(b, "\\f");  break;
            case 0x0D: jb_adds(b, "\\r");  break;
            case '"':  jb_adds(b, "\\\""); break;
            case 0x5C: jb_adds(b, "\\\\"); break;
            default:
                if (c < 0x20) {
                    char tmp[8];
                    snprintf(tmp, sizeof tmp, "\\u%04x", (unsigned) c);
                    jb_adds(b, tmp);
                } else {
                    jb_addc(b, (char) c);
                }
        }
    }
    jb_addc(b, '"');
}

static void
emit_scalar(JBuf *b, const JsonVal *v)
{
    switch (v->kind) {
        case JV_NULL: jb_adds(b, "null"); break;
        case JV_BOOL: jb_adds(b, v->bval ? "true" : "false"); break;
        case JV_NUM:  jb_adds(b, v->num ? v->num : "0"); break;
        case JV_STR:  escape_json_string(b, v->str ? v->str : "", v->slen); break;
        default: break;
    }
}

typedef struct {
    const JsonVal *v;
    int            i;           /* -1 = opener not emitted yet */
    int            level;
} RFrame;

#define JV_INDENT 4             /* jsonb_pretty() indents by four spaces */

static char *
render(const JsonVal *v, bool pretty)
{
    JBuf    b;
    RFrame *st;
    int     sn = 0, scap = 64;

    jb_init(&b);
    if (v == NULL)
        return jb_steal(&b);

    if (v->kind != JV_ARR && v->kind != JV_OBJ) {
        emit_scalar(&b, v);
        return jb_steal(&b);
    }

    st = jv_alloc((size_t) scap * sizeof(RFrame));
    st[sn].v = v;
    st[sn].i = -1;
    st[sn].level = 0;
    sn++;

    while (sn > 0) {
        RFrame *f = &st[sn - 1];
        const JsonVal *c = f->v;
        bool isobj = (c->kind == JV_OBJ);
        int  n = isobj ? c->npairs : c->nelems;

        if (f->i < 0) {
            jb_addc(&b, isobj ? '{' : '[');
            f->i = 0;
            continue;
        }
        if (f->i < n) {
            const JsonVal *child;

            if (pretty) {
                if (f->i > 0)
                    jb_addc(&b, ',');
                jb_addc(&b, JV_C_LF);
                jb_addn(&b, ' ', (size_t) (f->level + 1) * JV_INDENT);
            } else if (f->i > 0) {
                jb_adds(&b, ", ");
            }
            if (isobj) {
                escape_json_string(&b, c->pairs[f->i].key, c->pairs[f->i].keylen);
                jb_adds(&b, ": ");
                child = c->pairs[f->i].val;
            } else {
                child = c->elems[f->i];
            }
            f->i++;
            if (child->kind == JV_ARR || child->kind == JV_OBJ) {
                int level = f->level;
                if (sn == scap) {
                    scap *= 2;
                    st = jv_realloc(st, (size_t) scap * sizeof(RFrame));
                }
                st[sn].v = child;
                st[sn].i = -1;
                st[sn].level = level + 1;
                sn++;
            } else {
                emit_scalar(&b, child);
            }
            continue;
        }
        if (pretty) {
            jb_addc(&b, JV_C_LF);
            jb_addn(&b, ' ', (size_t) f->level * JV_INDENT);
        }
        jb_addc(&b, isobj ? '}' : ']');
        sn--;
    }
    free(st);
    return jb_steal(&b);
}

char *
json_to_text(const JsonVal *v, bool jsonb)
{
    if (v == NULL)
        return jv_strndup("", 0);

    if (!jsonb) {
        /* The text-preserving `json` type hands back the original bytes. */
        if (v->src != NULL && v->raw_off + v->raw_len <= v->src->len)
            return jv_strndup(v->src->data + v->raw_off, v->raw_len);
        /* Synthesised value with no source text: fall back to canonical. */
    }
    return render(v, false);
}

char *
json_pretty(const JsonVal *v)
{
    return render(v, true);
}

/* ============================ comparison ================================ */
static int
cmp_scalar(const JsonVal *a, const JsonVal *b)
{
    switch (a->kind) {
        case JV_NULL:
            return 0;
        case JV_BOOL:
            if (a->bval == b->bval)
                return 0;
            return a->bval ? 1 : -1;
        case JV_NUM:
            return num_text_cmp(a->num ? a->num : "0", b->num ? b->num : "0");
        case JV_STR:
            return str_cmp_bytes(a->str ? a->str : "", a->slen,
                                 b->str ? b->str : "", b->slen);
        default:
            return 0;
    }
}

static inline bool
jv_is_scalar(const JsonVal *v)
{
    return v->kind <= JV_STR;
}

typedef struct {
    const JsonVal *a, *b;
    int            i;
} XFrame;

/*
 * compareJsonbContainers().  A top-level scalar is stored as a single-element
 * "raw scalar" pseudo array, so length wins first and the raw-scalar flag only
 * breaks ties; that is why '1' < '[0]' but 'null' > '[]'.
 */
int
json_cmp(const JsonVal *a, const JsonVal *b)
{
    XFrame *st;
    int     sn = 0, scap = 64;
    int     res = 0;
    bool    ra, rb;
    int     na, nb;

    if (a == NULL || b == NULL)
        return (a == b) ? 0 : (a == NULL ? -1 : 1);

    if ((a->kind == JV_OBJ) != (b->kind == JV_OBJ))
        return (a->kind == JV_OBJ) ? 1 : -1;

    if (a->kind == JV_OBJ) {
        if (a->npairs != b->npairs)
            return a->npairs > b->npairs ? 1 : -1;
        if (a->npairs == 0)
            return 0;
    } else {
        ra = jv_is_scalar(a);
        rb = jv_is_scalar(b);
        na = ra ? 1 : a->nelems;
        nb = rb ? 1 : b->nelems;
        res = 0;
        if (ra != rb)
            res = ra ? -1 : 1;
        if (na != nb)
            res = na > nb ? 1 : -1;      /* element count overrides */
        if (res != 0)
            return res;
        if (ra && rb) {
            int ta = jv_type_rank[a->kind], tb = jv_type_rank[b->kind];
            if (ta != tb)
                return ta < tb ? -1 : 1;
            return cmp_scalar(a, b);
        }
        if (na == 0)
            return 0;
    }

    st = jv_alloc((size_t) scap * sizeof(XFrame));
    st[sn].a = a;
    st[sn].b = b;
    st[sn].i = 0;
    sn++;

    while (sn > 0 && res == 0) {
        XFrame *f = &st[sn - 1];
        const JsonVal *ca = f->a, *cb = f->b;
        bool isobj = (ca->kind == JV_OBJ);
        int  n = isobj ? ca->npairs : ca->nelems;
        const JsonVal *x, *y;
        int ta, tb, i;

        if (f->i >= n) {
            sn--;
            continue;
        }
        i = f->i++;
        if (isobj) {
            /*
             * Keys are compared here as ordinary jbvString scalars (plain byte
             * order), NOT with the length-first rule that decides their
             * storage order: '{"aa":1}' < '{"b":1}'.
             */
            res = str_cmp_bytes(ca->pairs[i].key, ca->pairs[i].keylen,
                                cb->pairs[i].key, cb->pairs[i].keylen);
            if (res != 0)
                break;
            x = ca->pairs[i].val;
            y = cb->pairs[i].val;
        } else {
            x = ca->elems[i];
            y = cb->elems[i];
        }

        ta = jv_type_rank[x->kind];
        tb = jv_type_rank[y->kind];
        if (ta != tb) {
            res = ta < tb ? -1 : 1;
            break;
        }
        if (x->kind == JV_ARR) {
            if (x->nelems != y->nelems) {
                res = x->nelems > y->nelems ? 1 : -1;
                break;
            }
            if (x->nelems == 0)
                continue;
        } else if (x->kind == JV_OBJ) {
            if (x->npairs != y->npairs) {
                res = x->npairs > y->npairs ? 1 : -1;
                break;
            }
            if (x->npairs == 0)
                continue;
        } else {
            res = cmp_scalar(x, y);
            if (res != 0)
                break;
            continue;
        }
        if (sn == scap) {
            scap *= 2;
            st = jv_realloc(st, (size_t) scap * sizeof(XFrame));
        }
        st[sn].a = x;
        st[sn].b = y;
        st[sn].i = 0;
        sn++;
    }
    free(st);
    return res;
}

/* ============================ containment =============================== */
static const JsonVal *
obj_find(const JsonVal *o, const char *key, size_t keylen)
{
    int lo = 0, hi = o->npairs - 1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int r = key_cmp(key, keylen, o->pairs[mid].key, o->pairs[mid].keylen);

        if (r == 0)
            return o->pairs[mid].val;
        if (r < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return NULL;
}

static bool
scalar_equal(const JsonVal *a, const JsonVal *b)
{
    if (a->kind != b->kind)
        return false;
    return cmp_scalar(a, b) == 0;
}

/*
 * JsonbDeepContains(), unrolled onto an explicit stack so that containment of
 * deeply nested values cannot overflow the C stack.
 *
 * Two kinds of sub-goal appear:
 *   - conjunctive (or_child == false): an object member's value must contain
 *     the corresponding value on the right; failure fails the whole frame.
 *   - disjunctive (or_child == true): a container element on the right must be
 *     contained by *some* container element on the left, so a failure just
 *     advances the parent's candidate cursor.
 */
typedef struct {
    bool                  isobj;
    const JsonVal        *lobj, *robj;      /* isobj */
    const JsonVal *const *le;               /* !isobj */
    const JsonVal *const *re;
    int                   ln, rn;
    int                   i;                /* next right-hand child */
    int                   cand;             /* next left-hand candidate */
    bool                  or_child;
} DFrame;

bool
json_contains(const JsonVal *a, const JsonVal *b)
{
    const JsonVal *la[1], *rb[1];
    DFrame *st;
    int     sn = 0, scap = 64;
    bool    result = false;

    if (a == NULL || b == NULL)
        return false;
    if ((a->kind == JV_OBJ) != (b->kind == JV_OBJ))
        return false;

    st = jv_alloc((size_t) scap * sizeof(DFrame));
    memset(&st[0], 0, sizeof(DFrame));
    if (a->kind == JV_OBJ) {
        st[0].isobj = true;
        st[0].lobj = a;
        st[0].robj = b;
    } else {
        bool araw = jv_is_scalar(a);
        bool braw = jv_is_scalar(b);

        /* A raw scalar may contain a raw scalar, but never a real array. */
        if (araw && !braw) {
            free(st);
            return false;
        }
        la[0] = a;
        rb[0] = b;
        st[0].isobj = false;
        st[0].le = araw ? la : (const JsonVal *const *) a->elems;
        st[0].ln = araw ? 1 : a->nelems;
        st[0].re = braw ? rb : (const JsonVal *const *) b->elems;
        st[0].rn = braw ? 1 : b->nelems;
    }
    sn = 1;

    for (;;) {
        DFrame *f = &st[sn - 1];
        const JsonVal *childL = NULL, *childR = NULL;
        bool child_is_or = false;
        int  outcome = -1;

        if (f->isobj) {
            const JsonVal *L = f->lobj, *R = f->robj;

            /* Keys are de-duplicated, so a smaller lhs can never contain. */
            if (L->npairs < R->npairs) {
                outcome = 0;
            } else if (f->i >= R->npairs) {
                outcome = 1;
            } else {
                const JsonVal *lv = obj_find(L, R->pairs[f->i].key,
                                             R->pairs[f->i].keylen);
                const JsonVal *rv = R->pairs[f->i].val;

                f->i++;
                if (lv == NULL) {
                    outcome = 0;
                } else if (jv_is_scalar(lv) != jv_is_scalar(rv)) {
                    /* PostgreSQL compares jbv types; containers are jbvBinary. */
                    outcome = 0;
                } else if (jv_is_scalar(lv)) {
                    if (!scalar_equal(lv, rv))
                        outcome = 0;
                    else
                        continue;
                } else if ((lv->kind == JV_OBJ) != (rv->kind == JV_OBJ)) {
                    outcome = 0;        /* array vs object: never contained */
                } else {
                    childL = lv;
                    childR = rv;
                }
            }
        } else if (f->i >= f->rn) {
            outcome = 1;
        } else {
            const JsonVal *r = f->re[f->i];

            if (jv_is_scalar(r)) {
                bool found = false;
                int  j;

                for (j = 0; j < f->ln; j++) {
                    if (jv_is_scalar(f->le[j]) && scalar_equal(f->le[j], r)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    outcome = 0;
                } else {
                    f->i++;
                    continue;
                }
            } else {
                int j = f->cand;

                /*
                 * Candidates are the left-hand *container* elements; one whose
                 * kind differs from r's can never contain it, so skipping it
                 * is the same as letting the attempt fail.
                 */
                while (j < f->ln &&
                       (jv_is_scalar(f->le[j]) ||
                        (f->le[j]->kind == JV_OBJ) != (r->kind == JV_OBJ)))
                    j++;
                if (j >= f->ln) {
                    outcome = 0;    /* no (more) container elements to try */
                } else {
                    f->cand = j + 1;
                    childL = f->le[j];
                    childR = r;
                    child_is_or = true;
                }
            }
        }

        if (outcome < 0) {
            /* Descend into the sub-goal. */
            if (sn == scap) {
                scap *= 2;
                st = jv_realloc(st, (size_t) scap * sizeof(DFrame));
            }
            memset(&st[sn], 0, sizeof(DFrame));
            st[sn].or_child = child_is_or;
            if (childR->kind == JV_OBJ) {
                st[sn].isobj = true;
                st[sn].lobj = childL;
                st[sn].robj = childR;
            } else {
                st[sn].isobj = false;
                st[sn].le = (const JsonVal *const *) childL->elems;
                st[sn].ln = childL->nelems;
                st[sn].re = (const JsonVal *const *) childR->elems;
                st[sn].rn = childR->nelems;
            }
            sn++;
            continue;
        }

        /* Propagate the verdict up through the frames. */
        for (;;) {
            bool was_or = st[sn - 1].or_child;

            sn--;
            if (sn == 0) {
                result = (outcome == 1);
                goto done;
            }
            if (outcome == 1) {
                if (was_or) {
                    st[sn - 1].i++;         /* this right-hand element matched */
                    st[sn - 1].cand = 0;
                }
                break;
            }
            if (was_or)
                break;                      /* parent retries the next candidate */
            /* conjunctive sub-goal failed: the parent fails as well */
        }
    }

done:
    free(st);
    return result;
}

/* ============================== accessors =============================== */
int
json_kind(const JsonVal *v)
{
    return v ? (int) v->kind : -1;
}

int
json_length(const JsonVal *v)
{
    if (v == NULL)
        return -1;
    if (v->kind == JV_ARR)
        return v->nelems;
    if (v->kind == JV_OBJ)
        return v->npairs;
    return -1;
}

const char *
json_str_value(const JsonVal *v)
{
    if (v == NULL || v->kind != JV_STR)
        return NULL;
    return v->str ? v->str : "";
}

/* Canonical numeric text of a number node (PostgreSQL numeric display form). */
const char *
json_num_text(const JsonVal *v)
{
    if (v == NULL || v->kind != JV_NUM)
        return NULL;
    return v->num ? v->num : "0";
}

/*
 * Positional access to an object's pairs, in the canonical (stored) order --
 * the same order json_to_text(v, true) renders them and the same order
 * jsonb_each() / jsonb_each_text() / jsonb_object_keys() return rows in.
 * Duplicates are already collapsed last-wins, so indices 0 .. json_length(v)-1
 * enumerate the object exactly once.
 *
 * Unlike json_get_idx(), these are plain iteration cursors: a negative index
 * is simply out of range rather than counting back from the end.
 */
const char *
json_key_at(const JsonVal *v, int i)
{
    if (v == NULL || v->kind != JV_OBJ)
        return NULL;
    if (i < 0 || i >= v->npairs)
        return NULL;
    return v->pairs[i].key;
}

JsonVal *
json_val_at(const JsonVal *v, int i)
{
    if (v == NULL || i < 0)
        return NULL;
    if (v->kind == JV_OBJ)
        return i < v->npairs ? v->pairs[i].val : NULL;
    if (v->kind == JV_ARR)
        return i < v->nelems ? v->elems[i] : NULL;
    return NULL;
}

JsonVal *
json_get_key(const JsonVal *v, const char *k)
{
    if (v == NULL || v->kind != JV_OBJ || k == NULL)
        return NULL;
    return (JsonVal *) obj_find(v, k, strlen(k));
}

JsonVal *
json_get_idx(const JsonVal *v, int i)
{
    if (v == NULL || v->kind != JV_ARR)
        return NULL;
    if (i < 0)
        i += v->nelems;             /* PostgreSQL counts back from the end */
    if (i < 0 || i >= v->nelems)
        return NULL;
    return v->elems[i];
}

/* ================================ entry ================================= */
/*
 * json_parse_ex(): jsonb == true reproduces jsonb_in() (strict unicode-escape
 * handling, numbers converted to numeric so overflow is caught); jsonb ==
 * false reproduces json_in(), which only validates syntax and therefore
 * accepts lone surrogates, the zero code point, and numbers outside the range
 * numeric can represent.
 */
static __thread bool *g_dupkey_out;

JsonVal *
json_parse_ex(const char *s, size_t len, bool jsonb, const char **err)
{
    JLex     lex;
    JsonVal *v = NULL;
    JErr     e;

    if (err)
        *err = NULL;
    if (s == NULL) {
        jv_set_error(err, JE_EXPECTED_MORE, NULL);
        return NULL;
    }

    memset(&lex, 0, sizeof lex);
    lex.input = s;
    lex.input_len = len;
    lex.tok_start = s;
    lex.tok_term = s;
    lex.prev_tok_term = s;
    lex.tok = T_INVALID;
    lex.decode = jsonb;
    jb_init(&lex.strval);

    e = parse_doc(&lex, &v);
    if (g_dupkey_out)
        *g_dupkey_out = lex.dup_key;
    jb_free(&lex.strval);

    if (e != JE_OK) {
        jv_set_error(err, e, &lex);
        return NULL;
    }

    /*
     * PostgreSQL's `json` output repeats the *entire* input, leading and
     * trailing whitespace included, so the root owns the whole span.
     */
    {
        JsonSrc *src = src_new(s, len);
        JsonVal **st;
        int sn = 0, scap = 64;

        st = jv_alloc((size_t) scap * sizeof(JsonVal *));
        st[sn++] = v;
        while (sn > 0) {
            JsonVal *cur = st[--sn];
            int i;

            cur->src = src_ref(src);
            if (cur->kind == JV_ARR || cur->kind == JV_OBJ) {
                int n = (cur->kind == JV_ARR) ? cur->nelems : cur->npairs;
                if (sn + n > scap) {
                    while (sn + n > scap)
                        scap *= 2;
                    st = jv_realloc(st, (size_t) scap * sizeof(JsonVal *));
                }
                for (i = 0; i < n; i++)
                    st[sn++] = (cur->kind == JV_ARR)
                        ? cur->elems[i] : cur->pairs[i].val;
            }
        }
        free(st);
        src_unref(src);
        v->raw_off = 0;
        v->raw_len = len;
    }
    return v;
}

JsonVal *
json_parse(const char *s, size_t len, const char **err)
{
    return json_parse_ex(s, len, true, err);
}

/*
 * json_parse_keys(): like json_parse_ex(), and additionally reports whether
 * any object in the document carried the same key twice.  IS JSON ... WITH
 * UNIQUE KEYS needs that, and the canonical form has already dropped it.
 */
JsonVal *
json_parse_keys(const char *s, size_t len, bool jsonb, const char **err, bool *dupkey)
{
    JsonVal *v;

    if (dupkey)
        *dupkey = false;
    g_dupkey_out = dupkey;
    v = json_parse_ex(s, len, jsonb, err);
    g_dupkey_out = NULL;
    return v;
}
