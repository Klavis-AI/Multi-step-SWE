/* jsonpath.c - the SQL/JSON path language: the jsonpath type's input and
 * output functions and the executor behind jsonb_path_query() and friends.
 * PostgreSQL grew all of this in release 12, so the whole file compiles away
 * below that.
 *
 * The layout follows PostgreSQL's own: a scanner and a recursive-descent
 * parser build a tree of items linked into accessor chains, jsonpath_text()
 * prints that tree back in the canonical spelling, and jsonpath_run() walks
 * it over a jsonb document.  Values are the plain JsonVal nodes of
 * jsonval.c; new ones are made by printing JSON text and parsing it back,
 * which keeps every formatting and number decision in one place.
 */
#include "jsonpath.h"

#if PGVC_VN >= 120

#include <regex.h>

/* ==================================================================== items */

typedef enum {
    jpiNull, jpiString, jpiNumeric, jpiBool,
    jpiAnd, jpiOr, jpiNot, jpiIsUnknown,
    jpiEqual, jpiNotEqual, jpiLess, jpiGreater, jpiLessOrEqual, jpiGreaterOrEqual,
    jpiAdd, jpiSub, jpiMul, jpiDiv, jpiMod,
    jpiPlus, jpiMinus,
    jpiAnyArray, jpiAnyKey, jpiIndexArray, jpiAny,
    jpiKey, jpiCurrent, jpiRoot, jpiVariable, jpiFilter, jpiExists,
    jpiType, jpiSize, jpiAbs, jpiFloor, jpiDouble, jpiCeiling, jpiKeyValue,
    jpiDatetime,
    /* the type-conversion item methods PostgreSQL 17 added */
    jpiBigint, jpiInteger, jpiNumber, jpiDecimal, jpiBoolean, jpiStringM,
    jpiDate, jpiTime, jpiTimestamp,
    jpiStartsWith, jpiLikeRegex, jpiLast
} JpType;

/* like_regex flags, in the order PostgreSQL prints them */
#define JSP_REGEX_ICASE  0x01
#define JSP_REGEX_DOTALL 0x02
#define JSP_REGEX_MLINE  0x04
#define JSP_REGEX_WSPACE 0x08
#define JSP_REGEX_QUOTE  0x10

#define JP_ANY_LAST 0xFFFFFFFFu

typedef struct JpItem JpItem;
typedef struct { JpItem *from, *to; bool range; } JpSubscript;

struct JpItem {
    JpType   type;
    JpItem  *next;                 /* the rest of the accessor chain */
    JpItem  *left, *right;         /* binary operators */
    JpItem  *arg;                  /* unary operators, filter, exists */
    char    *str;                  /* key name, string, variable, numeric text */
    bool     bval;
    JpSubscript *subs; int nsubs;  /* jpiIndexArray */
    uint32   first, last;          /* jpiAny bounds */
    char    *pattern; uint32 rflags;
    regex_t *re;                   /* compiled like_regex */
    int      prec, scale; bool hasprec, hasscale;   /* .decimal(p[,s]) */
};

struct JsonPath {
    bool    strict;
    JpItem *expr;
};

/* ================================================================= scanner */

typedef enum {
    T_END = 0,
    T_STRING, T_NUMBER, T_IDENT, T_VARIABLE,
    T_LP, T_RP, T_LB, T_RB, T_LC, T_RC,
    T_COMMA, T_DOT, T_QUESTION, T_AT, T_DOLLAR, T_STAR, T_STARSTAR,
    T_PLUS, T_MINUS, T_SLASH, T_PERCENT,
    T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE,
    T_AND, T_OR, T_NOT,
    K_IS, K_TO, K_ABS, K_LAX, K_FLAG, K_LAST, K_NULL, K_SIZE, K_TRUE,
    K_TYPE, K_WITH, K_FALSE, K_FLOOR, K_DOUBLE, K_EXISTS, K_STARTS,
    K_STRICT, K_CEILING, K_UNKNOWN, K_KEYVALUE, K_DATETIME,
    K_BIGINT, K_INTEGER, K_NUMBER, K_DECIMAL, K_BOOLEAN, K_STRINGM,
    K_DATE, K_TIME, K_TIMESTAMP,
    K_LIKE_REGEX
} JpTok;

typedef struct {
    JpTok  kind;
    char  *text;          /* decoded string / number text / identifier */
    char  *errtext;       /* what PostgreSQL names in a syntax error, or NULL
                           * when it says "at end of jsonpath input" */
} JpToken;

typedef struct {
    Session *s;
    JpToken *tok; int ntok, captok;
    int      pos;                  /* index of the lookahead token */
} JpParser;

static void jp_syntax_error(JpParser *p) __attribute__((noreturn));
static void jp_syntax_error(JpParser *p)
{
    const char *t = p->tok[p->pos].errtext;
    if (t && *t)
        ereport(p->s, "42601", "syntax error at or near \"%s\" of jsonpath input", t);
    ereport(p->s, "42601", "syntax error at end of jsonpath input");
}

static const struct { const char *name; JpTok tok; bool lower_only; } jp_keywords[] = {
    {"is", K_IS, false}, {"to", K_TO, false}, {"abs", K_ABS, false},
    {"lax", K_LAX, false}, {"flag", K_FLAG, false}, {"last", K_LAST, false},
    {"null", K_NULL, true}, {"size", K_SIZE, false}, {"true", K_TRUE, true},
    {"type", K_TYPE, false}, {"with", K_WITH, false}, {"false", K_FALSE, true},
    {"floor", K_FLOOR, false}, {"double", K_DOUBLE, false},
    {"exists", K_EXISTS, false}, {"starts", K_STARTS, false},
    {"strict", K_STRICT, false}, {"ceiling", K_CEILING, false},
    {"unknown", K_UNKNOWN, false}, {"keyvalue", K_KEYVALUE, false},
    {"datetime", K_DATETIME, false},
#if PGVC_VN >= 170
    {"bigint", K_BIGINT, false}, {"integer", K_INTEGER, false},
    {"number", K_NUMBER, false}, {"decimal", K_DECIMAL, false},
    {"boolean", K_BOOLEAN, false}, {"string", K_STRINGM, false},
    {"date", K_DATE, false}, {"time", K_TIME, false},
    {"timestamp", K_TIMESTAMP, false},
#endif
    {"like_regex", K_LIKE_REGEX, false},
    {NULL, T_END, false}
};

/* PostgreSQL folds every jsonpath keyword case-insensitively except the three
 * JSON literals, which must be spelled in lower case. */
static JpTok jp_keyword_of(const char *word)
{
    for (int i = 0; jp_keywords[i].name; i++) {
        const char *k = jp_keywords[i].name;
        size_t n = strlen(k);
        if (strlen(word) != n) continue;
        size_t j = 0;
        for (; j < n; j++)
            if (tolower((unsigned char)word[j]) != k[j]) break;
        if (j != n) continue;
        if (jp_keywords[i].lower_only && strcmp(word, k)) return T_IDENT;
        return jp_keywords[i].tok;
    }
    return T_IDENT;
}

static bool jp_is_blank(int c) { return c == ' ' || c == '\t' || c == '\n' ||
                                        c == '\r' || c == '\f'; }
/* Exactly PostgreSQL's {special} set, plus the two bytes its {any} class also
 * excludes.  Every other byte joins an unquoted name. */
static bool jp_is_special(int c)
{
    return c != 0 && strchr("?%$.[]{}()|&!=<>@#,*:-+/\\\"", c) != NULL;
}

static void jp_push(JpParser *p, JpTok kind, char *text, char *errtext)
{
    if (p->ntok == p->captok) {
        p->captok = p->captok ? p->captok * 2 : 16;
        p->tok = xrealloc(p->tok, (size_t)p->captok * sizeof(JpToken));
    }
    p->tok[p->ntok].kind = kind;
    p->tok[p->ntok].text = text;
    p->tok[p->ntok].errtext = errtext;
    p->ntok++;
}

/* decode one "..." literal, leaving *pp after the closing quote */
static char *jp_scan_string(JpParser *p, const char **pp, const char *end)
{
    Buf b; buf_init(&b);
    const char *q = *pp + 1;
    while (q < end && *q != '"') {
        if (*q == '\\' && q + 1 < end) {
            q++;
            switch (*q) {
            case 'b': buf_addc(&b, '\b'); q++; break;
            case 'f': buf_addc(&b, '\f'); q++; break;
            case 'n': buf_addc(&b, '\n'); q++; break;
            case 'r': buf_addc(&b, '\r'); q++; break;
            case 't': buf_addc(&b, '\t'); q++; break;
            case 'v': buf_addc(&b, '\v'); q++; break;
            case 'u': {
                unsigned code = 0; int got = 0;
                q++;
                while (got < 4 && q < end && isxdigit((unsigned char)*q)) {
                    int d = isdigit((unsigned char)*q) ? *q - '0'
                          : tolower((unsigned char)*q) - 'a' + 10;
                    code = code * 16 + (unsigned)d; q++; got++;
                }
                if (got != 4) { buf_free(&b);
                    ereport(p->s, "42601", "invalid unicode sequence at or near "
                                           "\"\\u\" of jsonpath input"); }
                if (code < 0x80) buf_addc(&b, (char)code);
                else if (code < 0x800) {
                    buf_addc(&b, (char)(0xC0 | (code >> 6)));
                    buf_addc(&b, (char)(0x80 | (code & 0x3F)));
                } else {
                    buf_addc(&b, (char)(0xE0 | (code >> 12)));
                    buf_addc(&b, (char)(0x80 | ((code >> 6) & 0x3F)));
                    buf_addc(&b, (char)(0x80 | (code & 0x3F)));
                }
                break;
            }
            default: buf_addc(&b, *q); q++; break;
            }
            continue;
        }
        buf_addc(&b, *q); q++;
    }
    if (q >= end) {
        buf_free(&b);
#if PGVC_VN >= 160
        ereport(p->s, "42601", "unterminated quoted string at end of "
                               "jsonpath input");
#else
        ereport(p->s, "42601", "unexpected end of quoted string at end of "
                               "jsonpath input");
#endif
    }
    *pp = q + 1;
    return buf_steal(&b);
}

static void jp_scan(JpParser *p, const char *str, int len)
{
    const char *q = str, *end = str + len;
    while (q < end) {
        if (jp_is_blank((unsigned char)*q)) { q++; continue; }
        if (*q == '/' && q + 1 < end && q[1] == '*') {         /* comment */
            const char *e2 = q + 2;
            while (e2 + 1 < end && !(e2[0] == '*' && e2[1] == '/')) e2++;
            if (e2 + 1 >= end)
                ereport(p->s, "42601", "unexpected end of comment at end of "
                                       "jsonpath input");
            q = e2 + 2;
            continue;
        }
        if (*q == '"') {
            const char *st = q;
            char *v = jp_scan_string(p, &q, end);
            jp_push(p, T_STRING, v, xstrndup(st, (size_t)(q - st)));
            continue;
        }
        if (*q == '$' && q + 1 < end && !jp_is_special((unsigned char)q[1]) &&
            !jp_is_blank((unsigned char)q[1])) {
            const char *st = q;
            q++;
            while (q < end && !jp_is_special((unsigned char)*q) &&
                   !jp_is_blank((unsigned char)*q)) q++;
            jp_push(p, T_VARIABLE, xstrndup(st + 1, (size_t)(q - st - 1)),
                    xstrndup(st, (size_t)(q - st)));
            continue;
        }
        if (*q == '$' && q + 1 < end && q[1] == '"') {
            q++;
            const char *st = q;
            char *v = jp_scan_string(p, &q, end);
            jp_push(p, T_VARIABLE, v, xstrndup(st, (size_t)(q - st)));
            continue;
        }
        if (isdigit((unsigned char)*q)) {
            const char *st = q;
            while (q < end && isdigit((unsigned char)*q)) q++;
            if (q + 1 < end && *q == '.' && isdigit((unsigned char)q[1])) {
                q++;
                while (q < end && isdigit((unsigned char)*q)) q++;
            }
            if (q < end && (*q == 'e' || *q == 'E')) {
                const char *save = q;
                q++;
                if (q < end && (*q == '+' || *q == '-')) q++;
                if (q < end && isdigit((unsigned char)*q)) {
                    while (q < end && isdigit((unsigned char)*q)) q++;
                } else q = save;
            }
            char *t = xstrndup(st, (size_t)(q - st));
            jp_push(p, T_NUMBER, t, xstrdup(t));
            continue;
        }
        if (jp_is_special((unsigned char)*q)) {
            JpTok k = T_END;
            int n = 1;
            switch (*q) {
            case '(': k = T_LP; break;
            case ')': k = T_RP; break;
            case '[': k = T_LB; break;
            case ']': k = T_RB; break;
            case '{': k = T_LC; break;
            case '}': k = T_RC; break;
            case ',': k = T_COMMA; break;
            case '.': k = T_DOT; break;
            case '?': k = T_QUESTION; break;
            case '@': k = T_AT; break;
            case '$': k = T_DOLLAR; break;
            case '+': k = T_PLUS; break;
            case '-': k = T_MINUS; break;
            case '/': k = T_SLASH; break;
            case '%': k = T_PERCENT; break;
            case '*':
                if (q + 1 < end && q[1] == '*') { k = T_STARSTAR; n = 2; }
                else k = T_STAR;
                break;
            case '=':
                if (q + 1 < end && q[1] == '=') { k = T_EQ; n = 2; }
                break;
            case '!':
                if (q + 1 < end && q[1] == '=') { k = T_NE; n = 2; }
                else k = T_NOT;
                break;
            case '<':
                if (q + 1 < end && q[1] == '=') { k = T_LE; n = 2; }
                else if (q + 1 < end && q[1] == '>') { k = T_NE; n = 2; }
                else k = T_LT;
                break;
            case '>':
                if (q + 1 < end && q[1] == '=') { k = T_GE; n = 2; }
                else k = T_GT;
                break;
            case '&':
                if (q + 1 < end && q[1] == '&') { k = T_AND; n = 2; }
                break;
            case '|':
                if (q + 1 < end && q[1] == '|') { k = T_OR; n = 2; }
                break;
            default: break;
            }
            if (k == T_END) {
                /* PostgreSQL's scanner has no rule for this byte and reports
                 * it straight away. */
                char one[2] = { *q, 0 };
                ereport(p->s, "42601",
                        "syntax error at or near \"%s\" of jsonpath input", one);
            }
            char *t = xstrndup(q, (size_t)n);
            jp_push(p, k, t, xstrdup(t));
            q += n;
            continue;
        }
        {   /* an unquoted name or keyword */
            const char *st = q;
            while (q < end && !jp_is_special((unsigned char)*q) &&
                   !jp_is_blank((unsigned char)*q)) q++;
            char *word = xstrndup(st, (size_t)(q - st));
            /* PostgreSQL only returns the token once it knows what ends it,
             * and names the run of blanks that did in any syntax error. */
            char *et = NULL;
            if (q < end && jp_is_blank((unsigned char)*q)) {
                const char *bs = q;
                while (q < end && jp_is_blank((unsigned char)*q)) q++;
                et = xstrndup(bs, (size_t)(q - bs));
            }
            jp_push(p, jp_keyword_of(word), word, et);
        }
    }
    jp_push(p, T_END, xstrdup(""), NULL);
}

/* ================================================================== parser */

static JpItem *jp_new(JpType t)
{
    JpItem *i = xmalloc(sizeof *i);
    memset(i, 0, sizeof *i);
    i->type = t;
    return i;
}

static JpTok jp_cur(JpParser *p) { return p->tok[p->pos].kind; }
static const char *jp_curtext(JpParser *p) { return p->tok[p->pos].text; }
static bool jp_accept(JpParser *p, JpTok k)
{
    if (p->tok[p->pos].kind != k) return false;
    p->pos++;
    return true;
}
static void jp_expect(JpParser *p, JpTok k)
{
    if (!jp_accept(p, k)) jp_syntax_error(p);
}

static JpItem *jp_parse_or(JpParser *p);

/* ?( ) and !( ) take a predicate, never a plain path expression */
static bool jp_is_predicate(const JpItem *it)
{
    if (it->next) return false;
    switch (it->type) {
    case jpiAnd: case jpiOr: case jpiNot: case jpiIsUnknown:
    case jpiEqual: case jpiNotEqual: case jpiLess: case jpiGreater:
    case jpiLessOrEqual: case jpiGreaterOrEqual:
    case jpiExists: case jpiStartsWith: case jpiLikeRegex:
        return true;
    default:
        return false;
    }
}

/* every keyword doubles as a member name */
static bool jp_is_key_name(JpTok k)
{
    return k == T_IDENT || k == T_STRING || (k >= K_IS && k <= K_LIKE_REGEX);
}

static JpType jp_method_of(JpTok k)
{
    switch (k) {
    case K_ABS:      return jpiAbs;
    case K_SIZE:     return jpiSize;
    case K_TYPE:     return jpiType;
    case K_FLOOR:    return jpiFloor;
    case K_DOUBLE:   return jpiDouble;
    case K_CEILING:  return jpiCeiling;
    case K_KEYVALUE: return jpiKeyValue;
#if PGVC_VN >= 170
    case K_BIGINT:    return jpiBigint;
    case K_INTEGER:   return jpiInteger;
    case K_NUMBER:    return jpiNumber;
    case K_DECIMAL:   return jpiDecimal;
    case K_BOOLEAN:   return jpiBoolean;
    case K_STRINGM:   return jpiStringM;
    case K_DATE:      return jpiDate;
    case K_TIME:      return jpiTime;
    case K_TIMESTAMP: return jpiTimestamp;
#endif
    default:         return jpiNull;
    }
}

/* PostgreSQL stores the numeric, so the printed form is numeric_out's. */
static JpItem *jp_make_numeric(JpParser *p, const char *text)
{
    bool ok = false;
    Numeric *n = num_from_str(text, &ok);
    if (!ok) jp_syntax_error(p);
    JpItem *i = jp_new(jpiNumeric);
    char *t = num_to_str(n);
    i->str = xstrdup(t);
    free(t);
    num_free(n);
    return i;
}

static void jp_chain(JpItem *head, JpItem *tail)
{
    JpItem *e = head;
    while (e->next) e = e->next;
    e->next = tail;
}

static uint32 jp_any_level(JpParser *p)
{
    if (jp_accept(p, K_LAST)) return JP_ANY_LAST;
    if (jp_cur(p) != T_NUMBER) jp_syntax_error(p);
    const char *t = jp_curtext(p);
    for (const char *q = t; *q; q++) if (!isdigit((unsigned char)*q)) jp_syntax_error(p);
    unsigned long v = strtoul(t, NULL, 10);
    p->pos++;
    if (v >= JP_ANY_LAST) jp_syntax_error(p);
    return (uint32)v;
}

/* build the compiled regex and validate the flags, as the parser does */
static void jp_compile_regex(JpParser *p, JpItem *it)
{
    int cflags = REG_EXTENDED;
    bool sline = false, mline = false;

    if (it->rflags & JSP_REGEX_ICASE)  cflags |= REG_ICASE;
    if (it->rflags & JSP_REGEX_DOTALL) sline = true;
    if (it->rflags & JSP_REGEX_MLINE)  mline = true;
    if (it->rflags & JSP_REGEX_WSPACE) {
#if PGVC_VN < 130
        ereport(p->s, "0A000", "XQuery \"x\" flag (expanded regular "
                               "expressions) is not implemented");
#endif
    }
    if (mline && !sline) cflags |= REG_NEWLINE;

    /* PostgreSQL's own regex engine reports unbalanced brackets and
     * parentheses in words the C library does not use, so check for them
     * before handing the pattern over. */
    {
        const char *q = it->pattern;
        int depth = 0;
        bool quote = (it->rflags & JSP_REGEX_QUOTE) != 0;
        while (!quote && *q) {
            if (*q == '\\' && q[1]) { q += 2; continue; }
            if (*q == '[') {
                q++;
                if (*q == '^') q++;
                if (*q == ']') q++;
                while (*q && *q != ']') {
                    if (*q == '[' && (q[1] == ':' || q[1] == '.' || q[1] == '=')) {
                        char kind = q[1];
                        q += 2;
                        while (*q && !(*q == kind && q[1] == ']')) q++;
                        if (!*q) break;
                        q += 2;
                        continue;
                    }
                    q++;
                }
                if (!*q)
                    ereport(p->s, "2201B", "invalid regular expression: "
                                           "brackets [] not balanced");
                q++;
                continue;
            }
            if (*q == '(') depth++;
            else if (*q == ')') depth--;
            if (depth < 0)
                ereport(p->s, "2201B", "invalid regular expression: "
                                       "parentheses () not balanced");
            q++;
        }
        if (depth != 0)
            ereport(p->s, "2201B", "invalid regular expression: "
                                   "parentheses () not balanced");
    }

    it->re = xmalloc(sizeof(regex_t));
    memset(it->re, 0, sizeof(regex_t));
    if (it->rflags & JSP_REGEX_QUOTE) {
        Buf b; buf_init(&b);
        for (const char *q = it->pattern; *q; q++) {
            if (strchr(".[]()*+?{}|^$\\", *q)) buf_addc(&b, '\\');
            buf_addc(&b, *q);
        }
        char *lit = buf_steal(&b);
        int rc = regcomp(it->re, lit, cflags);
        free(lit);
        if (rc) ereport(p->s, "2201B", "invalid regular expression");
        return;
    }
    int rc = regcomp(it->re, it->pattern, cflags);
    if (rc) {
        char msg[256];
        regerror(rc, it->re, msg, sizeof msg);
        const char *pg = NULL;
        switch (rc) {
        case REG_BADBR:    pg = "invalid repetition count(s)"; break;
        case REG_BADRPT:   pg = "quantifier operand invalid"; break;
        case REG_EBRACE:   pg = "braces {} not balanced"; break;
        case REG_EBRACK:   pg = "brackets [] not balanced"; break;
        case REG_ECOLLATE: pg = "invalid collating element"; break;
        case REG_ECTYPE:   pg = "invalid character class"; break;
        case REG_EESCAPE:  pg = "invalid escape \\ sequence"; break;
        case REG_EPAREN:   pg = "parentheses () not balanced"; break;
        case REG_ERANGE:   pg = "invalid character range"; break;
        case REG_ESUBREG:  pg = "invalid backreference number"; break;
        case REG_ESPACE:   pg = "out of memory"; break;
        default:           pg = "invalid regular expression"; break;
        }
        ereport(p->s, "2201B", "invalid regular expression: %s", pg);
    }
}

/* '[' ... ']' */
static JpItem *jp_parse_array_accessor(JpParser *p)
{
    jp_expect(p, T_LB);
    if (jp_accept(p, T_STAR)) {
        jp_expect(p, T_RB);
        return jp_new(jpiAnyArray);
    }
    JpItem *it = jp_new(jpiIndexArray);
    do {
        JpItem *from = jp_parse_or(p);
        JpItem *to = NULL;
        bool range = false;
        if (jp_accept(p, K_TO)) { to = jp_parse_or(p); range = true; }
        it->subs = xrealloc(it->subs, (size_t)(it->nsubs + 1) * sizeof(JpSubscript));
        it->subs[it->nsubs].from = from;
        it->subs[it->nsubs].to = to;
        it->subs[it->nsubs].range = range;
        it->nsubs++;
    } while (jp_accept(p, T_COMMA));
    jp_expect(p, T_RB);
    return it;
}

/* one accessor after a path, or NULL when none is next */
static JpItem *jp_parse_accessor(JpParser *p)
{
    if (jp_cur(p) == T_LB) return jp_parse_array_accessor(p);
    if (jp_cur(p) == T_QUESTION) {
        p->pos++;
        jp_expect(p, T_LP);
        JpItem *it = jp_new(jpiFilter);
        it->arg = jp_parse_or(p);
        if (!jp_is_predicate(it->arg)) jp_syntax_error(p);
        jp_expect(p, T_RP);
        return it;
    }
    if (jp_cur(p) != T_DOT) return NULL;
    p->pos++;
    if (jp_accept(p, T_STAR)) return jp_new(jpiAnyKey);
    if (jp_accept(p, T_STARSTAR)) {
        JpItem *it = jp_new(jpiAny);
        it->first = 0; it->last = JP_ANY_LAST;
        if (jp_accept(p, T_LC)) {
            uint32 a = jp_any_level(p);
            uint32 b = a;
            if (jp_accept(p, K_TO)) b = jp_any_level(p);
            jp_expect(p, T_RC);
            it->first = a; it->last = b;
        }
        return it;
    }
    if (!jp_is_key_name(jp_cur(p))) jp_syntax_error(p);
    JpTok k = jp_cur(p);
    char *name = xstrdup(jp_curtext(p));
    p->pos++;
#if PGVC_VN >= 130
    /* .datetime() is the one method that takes an argument: the template. */
    if (k == K_DATETIME && jp_cur(p) == T_LP) {
        p->pos++;
        JpItem *dt = jp_new(jpiDatetime);
        if (jp_cur(p) == T_STRING) { dt->str = xstrdup(jp_curtext(p)); p->pos++; }
        jp_expect(p, T_RP);
        free(name);
        return dt;
    }
#endif
    /* a method only when it is one of the method names and '(' follows */
    if (jp_cur(p) == T_LP && jp_method_of(k) != jpiNull) {
        p->pos++;
        JpItem *m = jp_new(jp_method_of(k));
#if PGVC_VN >= 170
        /* .decimal() alone of the conversion methods takes arguments */
        if (m->type == jpiDecimal && jp_cur(p) == T_NUMBER) {
            m->prec = atoi(jp_curtext(p)); m->hasprec = true; p->pos++;
            if (jp_accept(p, T_COMMA)) {
                if (jp_cur(p) != T_NUMBER) jp_syntax_error(p);
                m->scale = atoi(jp_curtext(p)); m->hasscale = true; p->pos++;
            }
        }
#endif
        jp_expect(p, T_RP);
        free(name);
        return m;
    }
    JpItem *it = jp_new(jpiKey);
    it->str = name;
    return it;
}

static JpItem *jp_parse_primary(JpParser *p)
{
    switch (jp_cur(p)) {
    case T_DOLLAR:   p->pos++; return jp_new(jpiRoot);
    case T_AT:       p->pos++; return jp_new(jpiCurrent);
    case K_LAST:     p->pos++; return jp_new(jpiLast);
    case K_NULL:     p->pos++; return jp_new(jpiNull);
    case K_TRUE:  {  p->pos++; JpItem *i = jp_new(jpiBool); i->bval = true;  return i; }
    case K_FALSE: {  p->pos++; JpItem *i = jp_new(jpiBool); i->bval = false; return i; }
    case T_STRING: { JpItem *i = jp_new(jpiString); i->str = xstrdup(jp_curtext(p));
                     p->pos++; return i; }
    case T_VARIABLE: { JpItem *i = jp_new(jpiVariable); i->str = xstrdup(jp_curtext(p));
                       p->pos++; return i; }
    case T_NUMBER: { JpItem *i = jp_make_numeric(p, jp_curtext(p)); p->pos++; return i; }
    case K_EXISTS: {
        p->pos++;
        jp_expect(p, T_LP);
        JpItem *i = jp_new(jpiExists);
        i->arg = jp_parse_or(p);
        jp_expect(p, T_RP);
        return i;
    }
    case T_LP: {
        p->pos++;
        JpItem *i = jp_parse_or(p);
        jp_expect(p, T_RP);
        if (jp_accept(p, K_IS)) {
            jp_expect(p, K_UNKNOWN);
            JpItem *u = jp_new(jpiIsUnknown);
            u->arg = i;
            return u;
        }
        return i;
    }
    default:
        jp_syntax_error(p);
    }
}

static JpItem *jp_parse_accessor_expr(JpParser *p)
{
    JpItem *head = jp_parse_primary(p);
    for (;;) {
        JpItem *acc = jp_parse_accessor(p);
        if (!acc) break;
        jp_chain(head, acc);
    }
    return head;
}

static JpItem *jp_parse_unary(JpParser *p)
{
    if (jp_cur(p) == T_PLUS || jp_cur(p) == T_MINUS) {
        JpType t = (jp_cur(p) == T_PLUS) ? jpiPlus : jpiMinus;
        p->pos++;
        JpItem *i = jp_new(t);
        i->arg = jp_parse_unary(p);
        return i;
    }
    return jp_parse_accessor_expr(p);
}

static JpItem *jp_parse_mul(JpParser *p)
{
    JpItem *l = jp_parse_unary(p);
    for (;;) {
        JpType t;
        if (jp_cur(p) == T_STAR) t = jpiMul;
        else if (jp_cur(p) == T_SLASH) t = jpiDiv;
        else if (jp_cur(p) == T_PERCENT) t = jpiMod;
        else break;
        p->pos++;
        JpItem *i = jp_new(t);
        i->left = l; i->right = jp_parse_unary(p);
        l = i;
    }
    return l;
}

static JpItem *jp_parse_add(JpParser *p)
{
    JpItem *l = jp_parse_mul(p);
    for (;;) {
        JpType t;
        if (jp_cur(p) == T_PLUS) t = jpiAdd;
        else if (jp_cur(p) == T_MINUS) t = jpiSub;
        else break;
        p->pos++;
        JpItem *i = jp_new(t);
        i->left = l; i->right = jp_parse_mul(p);
        l = i;
    }
    return l;
}

static JpItem *jp_parse_cmp(JpParser *p)
{
    JpItem *l = jp_parse_add(p);
    JpType t;
    switch (jp_cur(p)) {
    case T_EQ: t = jpiEqual; break;
    case T_NE: t = jpiNotEqual; break;
    case T_LT: t = jpiLess; break;
    case T_LE: t = jpiLessOrEqual; break;
    case T_GT: t = jpiGreater; break;
    case T_GE: t = jpiGreaterOrEqual; break;
    case K_STARTS: {
        p->pos++;
        jp_expect(p, K_WITH);
        if (jp_cur(p) != T_STRING && jp_cur(p) != T_VARIABLE) jp_syntax_error(p);
        JpItem *r;
        if (jp_cur(p) == T_STRING) { r = jp_new(jpiString); r->str = xstrdup(jp_curtext(p)); }
        else { r = jp_new(jpiVariable); r->str = xstrdup(jp_curtext(p)); }
        p->pos++;
        JpItem *i = jp_new(jpiStartsWith);
        i->left = l; i->right = r;
        return i;
    }
    case K_LIKE_REGEX: {
        p->pos++;
        if (jp_cur(p) != T_STRING) jp_syntax_error(p);
        JpItem *i = jp_new(jpiLikeRegex);
        i->arg = l;
        i->pattern = xstrdup(jp_curtext(p));
        p->pos++;
        if (jp_accept(p, K_FLAG)) {
            if (jp_cur(p) != T_STRING) jp_syntax_error(p);
            for (const char *f = jp_curtext(p); *f; f++) {
                switch (*f) {
                case 'i': i->rflags |= JSP_REGEX_ICASE;  break;
                case 's': i->rflags |= JSP_REGEX_DOTALL; break;
                case 'm': i->rflags |= JSP_REGEX_MLINE;  break;
                case 'x': i->rflags |= JSP_REGEX_WSPACE; break;
                case 'q': i->rflags |= JSP_REGEX_QUOTE;  break;
                default:
#if PGVC_VN >= 150
                    err_detail(p->s, "Unrecognized flag character \"%c\" in "
                                     "LIKE_REGEX predicate.", *f);
#else
                    err_detail(p->s, "unrecognized flag character \"%c\" in "
                                     "LIKE_REGEX predicate", *f);
#endif
                    ereport(p->s, "22P02", "invalid input syntax for type %s", "jsonpath");
                }
            }
            p->pos++;
        }
        jp_compile_regex(p, i);
        return i;
    }
    default:
        return l;
    }
    p->pos++;
    JpItem *i = jp_new(t);
    i->left = l; i->right = jp_parse_add(p);
    return i;
}

static JpItem *jp_parse_not(JpParser *p)
{
    if (jp_cur(p) == T_NOT) {
        p->pos++;
        JpItem *i = jp_new(jpiNot);
        if (jp_cur(p) == T_LP) {
            p->pos++;
            i->arg = jp_parse_or(p);
            if (!jp_is_predicate(i->arg)) jp_syntax_error(p);
            jp_expect(p, T_RP);
        } else if (jp_cur(p) == K_EXISTS) {
            i->arg = jp_parse_primary(p);
        } else jp_syntax_error(p);
        return i;
    }
    return jp_parse_cmp(p);
}

static JpItem *jp_parse_and(JpParser *p)
{
    JpItem *l = jp_parse_not(p);
    while (jp_cur(p) == T_AND) {
        p->pos++;
        JpItem *i = jp_new(jpiAnd);
        i->left = l; i->right = jp_parse_not(p);
        l = i;
    }
    return l;
}

static JpItem *jp_parse_or(JpParser *p)
{
    JpItem *l = jp_parse_and(p);
    while (jp_cur(p) == T_OR) {
        p->pos++;
        JpItem *i = jp_new(jpiOr);
        i->left = l; i->right = jp_parse_and(p);
        l = i;
    }
    return l;
}

/* @ only makes sense inside a filter, and last only inside a subscript. */
static void jp_validate(JpParser *p, JpItem *it, int nesting, bool insub)
{
    for (; it; it = it->next) {
        switch (it->type) {
        case jpiCurrent:
            if (nesting <= 0)
                ereport(p->s, "42601", "@ is not allowed in root expressions");
            break;
        case jpiLast:
            if (!insub)
                ereport(p->s, "42601", "LAST is allowed only in array subscripts");
            break;
        case jpiFilter:
            jp_validate(p, it->arg, nesting + 1, insub);
            break;
        case jpiIndexArray:
            for (int i = 0; i < it->nsubs; i++) {
                jp_validate(p, it->subs[i].from, nesting, true);
                if (it->subs[i].to) jp_validate(p, it->subs[i].to, nesting, true);
            }
            break;
        default:
            if (it->left)  jp_validate(p, it->left, nesting, insub);
            if (it->right) jp_validate(p, it->right, nesting, insub);
            if (it->arg)   jp_validate(p, it->arg, nesting, insub);
            break;
        }
    }
}

JsonPath *jsonpath_parse(Session *s, const char *str, int len)
{
    JpParser p;
    memset(&p, 0, sizeof p);
    p.s = s;
    jp_scan(&p, str, len);
    if (p.ntok == 1) {          /* nothing but whitespace and comments */
        ereport(s, "22P02", "invalid input syntax for type %s: \"%.*s\"",
                "jsonpath", len, str);
    }
    JsonPath *jp = xmalloc(sizeof *jp);
    memset(jp, 0, sizeof *jp);
    jp->strict = false;
    if (jp_cur(&p) == K_STRICT)   { jp->strict = true;  p.pos++; }
    else if (jp_cur(&p) == K_LAX) { jp->strict = false; p.pos++; }
    jp->expr = jp_parse_or(&p);
    if (jp_cur(&p) != T_END) jp_syntax_error(&p);
    jp_validate(&p, jp->expr, 0, false);
    for (int i = 0; i < p.ntok; i++) { free(p.tok[i].text); free(p.tok[i].errtext); }
    free(p.tok);
    return jp;
}

/* ================================================================= printer */

static void jp_escape_json(Buf *b, const char *s)
{
    buf_addc(b, '"');
    for (const unsigned char *q = (const unsigned char *)s; *q; q++) {
        switch (*q) {
        case '\b': buf_adds(b, "\\b"); break;
        case '\f': buf_adds(b, "\\f"); break;
        case '\n': buf_adds(b, "\\n"); break;
        case '\r': buf_adds(b, "\\r"); break;
        case '\t': buf_adds(b, "\\t"); break;
        case '"':  buf_adds(b, "\\\""); break;
        case '\\': buf_adds(b, "\\\\"); break;
        default:
            if (*q < ' ') buf_printf(b, "\\u%04x", (int)*q);
            else buf_addc(b, (char)*q);
            break;
        }
    }
    buf_addc(b, '"');
}

static int jp_priority(JpType t)
{
    switch (t) {
    case jpiOr:  return 0;
    case jpiAnd: return 1;
    case jpiEqual: case jpiNotEqual: case jpiLess: case jpiGreater:
    case jpiLessOrEqual: case jpiGreaterOrEqual:
    case jpiStartsWith: case jpiLikeRegex: return 2;
    case jpiAdd: case jpiSub: return 3;
    case jpiMul: case jpiDiv: case jpiMod: return 4;
    case jpiPlus: case jpiMinus: return 5;
    default: return 6;
    }
}

static const char *jp_op_name(JpType t)
{
    switch (t) {
    case jpiAnd: return "&&";
    case jpiOr: return "||";
    case jpiEqual: return "==";
    case jpiNotEqual: return "!=";
    case jpiLess: return "<";
    case jpiGreater: return ">";
    case jpiLessOrEqual: return "<=";
    case jpiGreaterOrEqual: return ">=";
    case jpiPlus: case jpiAdd: return "+";
    case jpiMinus: case jpiSub: return "-";
    case jpiMul: return "*";
    case jpiDiv: return "/";
    case jpiMod: return "%";
    case jpiStartsWith: return "starts with";
    case jpiLikeRegex: return "like_regex";
    case jpiType: return "type";
    case jpiSize: return "size";
    case jpiKeyValue: return "keyvalue";
    case jpiDouble: return "double";
    case jpiAbs: return "abs";
    case jpiFloor: return "floor";
    case jpiCeiling: return "ceiling";
    case jpiDatetime: return "datetime";
    case jpiBigint: return "bigint";
    case jpiInteger: return "integer";
    case jpiNumber: return "number";
    case jpiDecimal: return "decimal";
    case jpiBoolean: return "boolean";
    case jpiStringM: return "string";
    case jpiDate: return "date";
    case jpiTime: return "time";
    case jpiTimestamp: return "timestamp";
    default: return "?";
    }
}

static void jp_print(Buf *b, const JpItem *v, bool inkey, bool brackets)
{
    switch (v->type) {
    case jpiNull:    buf_adds(b, "null"); break;
    case jpiKey:     if (inkey) buf_addc(b, '.'); jp_escape_json(b, v->str); break;
    case jpiString:  jp_escape_json(b, v->str); break;
    case jpiVariable: buf_addc(b, '$'); jp_escape_json(b, v->str); break;
    case jpiNumeric: buf_adds(b, v->str); break;
    case jpiBool:    buf_adds(b, v->bval ? "true" : "false"); break;
    case jpiAnd: case jpiOr:
    case jpiEqual: case jpiNotEqual: case jpiLess: case jpiGreater:
    case jpiLessOrEqual: case jpiGreaterOrEqual:
    case jpiAdd: case jpiSub: case jpiMul: case jpiDiv: case jpiMod:
    case jpiStartsWith:
        if (brackets) buf_addc(b, '(');
        jp_print(b, v->left, false, jp_priority(v->left->type) <= jp_priority(v->type));
        buf_addc(b, ' ');
        buf_adds(b, jp_op_name(v->type));
        buf_addc(b, ' ');
        jp_print(b, v->right, false, jp_priority(v->right->type) <= jp_priority(v->type));
        if (brackets) buf_addc(b, ')');
        break;
    case jpiLikeRegex:
        if (brackets) buf_addc(b, '(');
        jp_print(b, v->arg, false, jp_priority(v->arg->type) <= jp_priority(v->type));
        buf_adds(b, " like_regex ");
        jp_escape_json(b, v->pattern);
        if (v->rflags) {
            buf_adds(b, " flag \"");
            if (v->rflags & JSP_REGEX_ICASE)  buf_addc(b, 'i');
            if (v->rflags & JSP_REGEX_DOTALL) buf_addc(b, 's');
            if (v->rflags & JSP_REGEX_MLINE)  buf_addc(b, 'm');
            if (v->rflags & JSP_REGEX_WSPACE) buf_addc(b, 'x');
            if (v->rflags & JSP_REGEX_QUOTE)  buf_addc(b, 'q');
            buf_addc(b, '"');
        }
        if (brackets) buf_addc(b, ')');
        break;
    case jpiPlus: case jpiMinus:
        if (brackets) buf_addc(b, '(');
        buf_addc(b, v->type == jpiPlus ? '+' : '-');
        jp_print(b, v->arg, false, jp_priority(v->arg->type) <= jp_priority(v->type));
        if (brackets) buf_addc(b, ')');
        break;
    case jpiFilter:
        buf_adds(b, "?(");
        jp_print(b, v->arg, false, false);
        buf_addc(b, ')');
        break;
    case jpiNot:
        buf_adds(b, "!(");
        jp_print(b, v->arg, false, false);
        buf_addc(b, ')');
        break;
    case jpiIsUnknown:
        buf_addc(b, '(');
        jp_print(b, v->arg, false, false);
        buf_adds(b, ") is unknown");
        break;
    case jpiExists:
        buf_adds(b, "exists (");
        jp_print(b, v->arg, false, false);
        buf_addc(b, ')');
        break;
    case jpiCurrent:  buf_addc(b, '@'); break;
    case jpiRoot:     buf_addc(b, '$'); break;
    case jpiLast:     buf_adds(b, "last"); break;
    case jpiAnyArray: buf_adds(b, "[*]"); break;
    case jpiAnyKey:   if (inkey) buf_addc(b, '.'); buf_addc(b, '*'); break;
    case jpiIndexArray:
        buf_addc(b, '[');
        for (int i = 0; i < v->nsubs; i++) {
            if (i) buf_addc(b, ',');
            jp_print(b, v->subs[i].from, false, false);
            if (v->subs[i].range) {
                buf_adds(b, " to ");
                jp_print(b, v->subs[i].to, false, false);
            }
        }
        buf_addc(b, ']');
        break;
    case jpiAny:
        if (inkey) buf_addc(b, '.');
        if (v->first == 0 && v->last == JP_ANY_LAST) buf_adds(b, "**");
        else if (v->first == v->last) {
            if (v->first == JP_ANY_LAST) buf_adds(b, "**{last}");
            else buf_printf(b, "**{%u}", v->first);
        }
        else if (v->first == JP_ANY_LAST) buf_printf(b, "**{last to %u}", v->last);
        else if (v->last == JP_ANY_LAST)  buf_printf(b, "**{%u to last}", v->first);
        else buf_printf(b, "**{%u to %u}", v->first, v->last);
        break;
    case jpiType:     buf_adds(b, ".type()"); break;
    case jpiSize:     buf_adds(b, ".size()"); break;
    case jpiAbs:      buf_adds(b, ".abs()"); break;
    case jpiFloor:    buf_adds(b, ".floor()"); break;
    case jpiCeiling:  buf_adds(b, ".ceiling()"); break;
    case jpiDouble:   buf_adds(b, ".double()"); break;
    case jpiKeyValue: buf_adds(b, ".keyvalue()"); break;
    case jpiDatetime:
        buf_adds(b, ".datetime(");
        if (v->str) jp_escape_json(b, v->str);
        buf_addc(b, ')');
        break;
    case jpiBigint: case jpiInteger: case jpiNumber: case jpiBoolean:
    case jpiStringM: case jpiDate: case jpiTime: case jpiTimestamp:
        buf_printf(b, ".%s()", jp_op_name(v->type));
        break;
    case jpiDecimal:
        buf_adds(b, ".decimal(");
        if (v->hasprec) {
            buf_printf(b, "%d", v->prec);
            if (v->hasscale) buf_printf(b, ",%d", v->scale);
        }
        buf_addc(b, ')');
        break;
    }
    if (v->next) jp_print(b, v->next, true, true);
}

char *jsonpath_text(const JsonPath *jp)
{
    Buf b; buf_init(&b);
    if (jp->strict) buf_adds(&b, "strict ");
    jp_print(&b, jp->expr, false, true);
    return buf_steal(&b);
}

static void jp_free_item(JpItem *it)
{
    while (it) {
        JpItem *nx = it->next;
        jp_free_item(it->left);
        jp_free_item(it->right);
        jp_free_item(it->arg);
        for (int i = 0; i < it->nsubs; i++) {
            jp_free_item(it->subs[i].from);
            jp_free_item(it->subs[i].to);
        }
        free(it->subs);
        free(it->str);
        free(it->pattern);
        if (it->re) { regfree(it->re); free(it->re); }
        free(it);
        it = nx;
    }
}

void jsonpath_free(JsonPath *jp)
{
    if (!jp) return;
    jp_free_item(jp->expr);
    free(jp);
}

/* ================================================================ executor */

#define JPER_OK       0
#define JPER_NOTFOUND 1
#define JPER_ERROR    2

#define JPB_FALSE   0
#define JPB_TRUE    1
#define JPB_UNKNOWN 2

/* Structural errors are the ones the silent argument swallows: when we are
 * not throwing we unwind with JPER_ERROR and keep whatever matched first. */
#define JP_ERR(c, state, ...) do { \
        if ((c)->throwerr) ereport((c)->s, state, __VA_ARGS__); \
        return JPER_ERROR; \
    } while (0)

typedef struct {
    Session *s;
    JsonVal *root;
    JsonVal *current;
    JsonVal *vars;
    bool     lax;
    bool     ignore_struct;
    bool     throwerr;
    bool     first_only;           /* stop as soon as one item is produced */
    bool     stop;
    JsonPathList *top;             /* the caller's result list */
    int      innermost_size;
    int      depth;
} JpCtx;

static void jl_add(JsonPathList *l, JsonVal *v)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = xrealloc(l->v, (size_t)l->cap * sizeof(JsonVal *));
    }
    l->v[l->n++] = v;
}

/* ---- building values.  Everything goes through jsonval.c's parser so the
 * canonical spelling of numbers and strings is decided in exactly one place. */
static JsonVal *jv_text(const char *t)
{
    const char *err = NULL;
    return json_parse(t, strlen(t), &err);
}
static JsonVal *jv_null(void)      { return jv_text("null"); }
static JsonVal *jv_bool(bool b)    { return jv_text(b ? "true" : "false"); }
static JsonVal *jv_numeric(Numeric *n)
{
    char *t = num_to_str(n);
    JsonVal *v = jv_text(t);
    free(t);
    return v;
}
static JsonVal *jv_int(int i)
{
    char t[24];
    snprintf(t, sizeof t, "%d", i);
    return jv_text(t);
}
static JsonVal *jv_string(const char *s)
{
    Buf b; buf_init(&b);
    jp_escape_json(&b, s);
    char *t = buf_steal(&b);
    JsonVal *v = jv_text(t);
    free(t);
    return v;
}
static bool jv_boolval(const JsonVal *v)
{
    char *t = json_to_text(v, true);
    bool r = (t && t[0] == 't');
    free(t);
    return r;
}
static Numeric *jv_numval(const JsonVal *v)
{
    bool ok = false;
    return num_from_str(json_num_text(v), &ok);
}

static const char *jp_typename(const JsonVal *v)
{
    switch (json_kind(v)) {
    case 0:  return "null";
    case 1:  return "boolean";
    case 2:  return "number";
    case 3:  return "string";
    case 4:  return "array";
    default: return "object";
    }
}

static int jp_item(JpCtx *c, JpItem *jsp, JsonVal *jb, JsonPathList *found, bool unwrap);
static int jp_bool_item(JpCtx *c, JpItem *jsp, JsonVal *jb);

static int jp_exec(JpCtx *c, JpItem *jsp, JsonVal *jb, JsonPathList *found)
{
    return jp_item(c, jsp, jb, found, c->lax);
}

static int jp_next(JpCtx *c, JpItem *cur, JpItem *next, JsonVal *v, JsonPathList *found)
{
    if (!next && cur) next = cur->next;
    if (next) return jp_exec(c, next, v, found);
    jl_add(found, v);
    if (c->first_only && found == c->top) c->stop = true;
    return JPER_OK;
}

/* Walk the members of a container, applying jsp to those between the first
 * and last requested levels.  This is PostgreSQL's executeAnyItem, and it
 * backs [*], .* and .** alike. */
static int jp_any(JpCtx *c, JpItem *jsp, JsonVal *cont, JsonPathList *found,
                  uint32 level, uint32 first, uint32 last,
                  bool ignore_struct, bool unwrap_next)
{
    int res = JPER_NOTFOUND;
    if (level > last) return res;
    int k = json_kind(cont);
    if (k != 4 && k != 5) return res;
    int n = json_length(cont);
    for (int i = 0; i < n; i++) {
        JsonVal *v = json_val_at(cont, i);
        if (!v) continue;
        int vk = json_kind(v);
        bool container = (vk == 4 || vk == 5);
        if (level >= first ||
            (first == JP_ANY_LAST && last == JP_ANY_LAST && !container)) {
            if (jsp) {
                bool saved = c->ignore_struct;
                if (ignore_struct) c->ignore_struct = true;
                res = jp_item(c, jsp, v, found, unwrap_next);
                c->ignore_struct = saved;
                if (res == JPER_ERROR || c->stop) return res;
            } else {
                jl_add(found, v);
                if (c->first_only && found == c->top) c->stop = true;
                res = JPER_OK;
                if (c->stop) return res;
            }
        }
        if (level < last && container) {
            res = jp_any(c, jsp, v, found, level + 1, first, last,
                         ignore_struct, unwrap_next);
            if (res == JPER_ERROR || c->stop) return res;
        }
    }
    return res;
}

static int jp_unwrap_target(JpCtx *c, JpItem *jsp, JsonVal *jb,
                            JsonPathList *found, bool unwrap_elems)
{
    return jp_any(c, jsp, jb, found, 1, 1, 1, false, unwrap_elems);
}

/* Run jsp and, in lax mode, spread any array it yields into single items. */
static int jp_unwrap_result(JpCtx *c, JpItem *jsp, JsonVal *jb, bool unwrap,
                            JsonPathList *found)
{
    if (!(unwrap && c->lax)) return jp_exec(c, jsp, jb, found);

    JsonPathList seq;
    memset(&seq, 0, sizeof seq);
    int r = jp_exec(c, jsp, jb, &seq);
    if (r == JPER_ERROR) { free(seq.v); return r; }
    for (int i = 0; i < seq.n; i++) {
        if (json_kind(seq.v[i]) == 4)
            jp_unwrap_target(c, NULL, seq.v[i], found, false);
        else
            jl_add(found, seq.v[i]);
    }
    free(seq.v);
    return JPER_OK;
}

/* Operands of a predicate never raise: an error inside one makes the whole
 * predicate unknown. */
static int jp_unwrap_result_nothrow(JpCtx *c, JpItem *jsp, JsonVal *jb,
                                    bool unwrap, JsonPathList *found)
{
    bool saved = c->throwerr;
    c->throwerr = false;
    int r = jp_unwrap_result(c, jsp, jb, unwrap, found);
    c->throwerr = saved;
    return r;
}

static int jp_append_bool(JpCtx *c, JpItem *jsp, JsonPathList *found, int st)
{
    JsonVal *v = (st == JPB_UNKNOWN) ? jv_null() : jv_bool(st == JPB_TRUE);
    return jp_next(c, jsp, NULL, v, found);
}

static int jp_compare(JpType op, const JsonVal *a, const JsonVal *b)
{
    int ka = json_kind(a), kb = json_kind(b), cmp;
    if (ka != kb) {
        /* comparing null with anything else is false, except for != */
        if (ka == 0 || kb == 0) return op == jpiNotEqual ? JPB_TRUE : JPB_FALSE;
        return JPB_UNKNOWN;                 /* not comparable */
    }
    switch (ka) {
    case 0: cmp = 0; break;
    case 1: {
        bool x = jv_boolval(a), y = jv_boolval(b);
        cmp = (x == y) ? 0 : (x ? 1 : -1);
        break;
    }
    case 2: {
        Numeric *na = jv_numval(a), *nb = jv_numval(b);
        cmp = num_cmp(na, nb);
        num_free(na); num_free(nb);
        break;
    }
    case 3: {
        const char *x = json_str_value(a), *y = json_str_value(b);
        cmp = strcmp(x ? x : "", y ? y : "");
        break;
    }
    default:
        return JPB_UNKNOWN;                 /* objects and arrays */
    }
    bool r;
    switch (op) {
    case jpiEqual:          r = (cmp == 0); break;
    case jpiNotEqual:       r = (cmp != 0); break;
    case jpiLess:           r = (cmp <  0); break;
    case jpiGreater:        r = (cmp >  0); break;
    case jpiLessOrEqual:    r = (cmp <= 0); break;
    default:                r = (cmp >= 0); break;
    }
    return r ? JPB_TRUE : JPB_FALSE;
}

static int jp_starts_with(const JsonVal *whole, const JsonVal *initial)
{
    if (json_kind(whole) != 3 || json_kind(initial) != 3) return JPB_UNKNOWN;
    const char *w = json_str_value(whole), *i = json_str_value(initial);
    if (!w || !i) return JPB_UNKNOWN;
    size_t n = strlen(i);
    return (strlen(w) >= n && !memcmp(w, i, n)) ? JPB_TRUE : JPB_FALSE;
}

static int jp_like_regex(const JpItem *jsp, const JsonVal *str)
{
    if (json_kind(str) != 3) return JPB_UNKNOWN;
    const char *s = json_str_value(str);
    if (!s || !jsp->re) return JPB_UNKNOWN;
    return regexec(jsp->re, s, 0, NULL, 0) == 0 ? JPB_TRUE : JPB_FALSE;
}

static int jp_pred_eval(JpItem *pred, const JsonVal *l, const JsonVal *r)
{
    switch (pred->type) {
    case jpiStartsWith: return jp_starts_with(l, r);
    case jpiLikeRegex:  return jp_like_regex(pred, l);
    default:            return jp_compare(pred->type, l, r);
    }
}

/* Cross every left item with every right one.  In lax mode the first true
 * wins; in strict mode we must look at all of them so that an error anywhere
 * makes the answer unknown. */
static int jp_predicate(JpCtx *c, JpItem *pred, JpItem *larg, JpItem *rarg,
                        JsonVal *jb, bool unwrap_right)
{
    JsonPathList lseq, rseq;
    bool error = false, found = false;
    int ret;

    memset(&lseq, 0, sizeof lseq);
    memset(&rseq, 0, sizeof rseq);

    if (jp_unwrap_result_nothrow(c, larg, jb, true, &lseq) == JPER_ERROR) {
        ret = JPB_UNKNOWN;
        goto done;
    }
    if (rarg && jp_unwrap_result_nothrow(c, rarg, jb, unwrap_right, &rseq) == JPER_ERROR) {
        ret = JPB_UNKNOWN;
        goto done;
    }

    for (int i = 0; i < lseq.n; i++) {
        int j = 0;
        for (;;) {
            if (rarg && j >= rseq.n) break;
            int r = jp_pred_eval(pred, lseq.v[i], rarg ? rseq.v[j] : NULL);
            if (r == JPB_UNKNOWN) {
                if (!c->lax) { ret = JPB_UNKNOWN; goto done; }
                error = true;
            } else if (r == JPB_TRUE) {
                if (c->lax) { ret = JPB_TRUE; goto done; }
                found = true;
            }
            if (!rarg) break;
            j++;
        }
    }
    ret = found ? JPB_TRUE : (error ? JPB_UNKNOWN : JPB_FALSE);
done:
    free(lseq.v);
    free(rseq.v);
    return ret;
}

static int jp_bool_item(JpCtx *c, JpItem *jsp, JsonVal *jb)
{
    switch (jsp->type) {
    case jpiAnd: {
        int r = jp_bool_item(c, jsp->left, jb);
        if (r == JPB_FALSE) return JPB_FALSE;
        int r2 = jp_bool_item(c, jsp->right, jb);
        return r2 == JPB_TRUE ? r : r2;
    }
    case jpiOr: {
        int r = jp_bool_item(c, jsp->left, jb);
        if (r == JPB_TRUE) return JPB_TRUE;
        int r2 = jp_bool_item(c, jsp->right, jb);
        return r2 == JPB_FALSE ? r : r2;
    }
    case jpiNot: {
        int r = jp_bool_item(c, jsp->arg, jb);
        if (r == JPB_UNKNOWN) return JPB_UNKNOWN;
        return r == JPB_TRUE ? JPB_FALSE : JPB_TRUE;
    }
    case jpiIsUnknown:
        return jp_bool_item(c, jsp->arg, jb) == JPB_UNKNOWN ? JPB_TRUE : JPB_FALSE;
    case jpiEqual: case jpiNotEqual: case jpiLess: case jpiGreater:
    case jpiLessOrEqual: case jpiGreaterOrEqual:
        return jp_predicate(c, jsp, jsp->left, jsp->right, jb, true);
    case jpiStartsWith:
        return jp_predicate(c, jsp, jsp->left, jsp->right, jb, false);
    case jpiLikeRegex:
        return jp_predicate(c, jsp, jsp->arg, NULL, jb, false);
    case jpiExists: {
        JsonPathList vals;
        memset(&vals, 0, sizeof vals);
        int r = jp_unwrap_result_nothrow(c, jsp->arg, jb, false, &vals);
        int out = (r == JPER_ERROR) ? JPB_UNKNOWN : (vals.n ? JPB_TRUE : JPB_FALSE);
        free(vals.v);
        return out;
    }
    default:
        return JPB_UNKNOWN;
    }
}

static JsonVal *jp_variable(JpCtx *c, JpItem *jsp)
{
    if (!c->vars) return jv_null();
    JsonVal *v = json_get_key(c->vars, jsp->str);
    if (v) return v;
    /* Never suppressed: this is a mistake in the query, not in the data. */
    ereport(c->s, "42704", "could not find jsonpath variable \"%s\"", jsp->str);
}

static int jp_array_index(JpCtx *c, JpItem *jsp, JsonVal *jb, int *out)
{
    JsonPathList lst;
    memset(&lst, 0, sizeof lst);
    int r = jp_exec(c, jsp, jb, &lst);
    if (r == JPER_ERROR) { free(lst.v); return r; }
    if (lst.n != 1 || json_kind(lst.v[0]) != 2) {
        free(lst.v);
        JP_ERR(c, "22033", "jsonpath array subscript is not a single numeric value");
    }
    Numeric *n = jv_numval(lst.v[0]);
    free(lst.v);
    Numeric *t = num_trunc(n, 0);
    int64 iv = 0;
    bool ok = num_to_int64(t, &iv);
    num_free(n); num_free(t);
    if (!ok || iv > 2147483647LL || iv < -2147483648LL)
        JP_ERR(c, "22033", "jsonpath array subscript is out of integer range");
    *out = (int)iv;
    return JPER_OK;
}

static int jp_binary_arith(JpCtx *c, JpItem *jsp, JsonVal *jb, JsonPathList *found)
{
    JsonPathList lseq, rseq;
    memset(&lseq, 0, sizeof lseq);
    memset(&rseq, 0, sizeof rseq);

    int r = jp_unwrap_result(c, jsp->left, jb, true, &lseq);
    if (r == JPER_ERROR) { free(lseq.v); return r; }
    r = jp_unwrap_result(c, jsp->right, jb, true, &rseq);
    if (r == JPER_ERROR) { free(lseq.v); free(rseq.v); return r; }

    bool lbad = (lseq.n != 1 || json_kind(lseq.v[0]) != 2);
    bool rbad = (rseq.n != 1 || json_kind(rseq.v[0]) != 2);
    JsonVal *lv = lbad ? NULL : lseq.v[0];
    JsonVal *rv = rbad ? NULL : rseq.v[0];
    free(lseq.v);
    free(rseq.v);

    if (lbad)
        JP_ERR(c, "22038", "left operand of jsonpath operator %s is not a "
                           "single numeric value", jp_op_name(jsp->type));
    if (rbad)
        JP_ERR(c, "22038", "right operand of jsonpath operator %s is not a "
                           "single numeric value", jp_op_name(jsp->type));

    Numeric *a = jv_numval(lv), *b = jv_numval(rv), *res = NULL;
    bool divzero = false;
    switch (jsp->type) {
    case jpiAdd: res = num_add(a, b); break;
    case jpiSub: res = num_sub(a, b); break;
    case jpiMul: res = num_mul(a, b); break;
    case jpiDiv: res = num_div(a, b, &divzero); break;
    default:     res = num_mod(a, b, &divzero); break;
    }
    num_free(a); num_free(b);
    if (divzero) {
        if (res) num_free(res);
        JP_ERR(c, "22012", "division by zero");
    }
    JsonVal *out = jv_numeric(res);
    num_free(res);
    return jp_next(c, jsp, NULL, out, found);
}

static int jp_unary_arith(JpCtx *c, JpItem *jsp, JsonVal *jb, JsonPathList *found)
{
    JsonPathList seq;
    memset(&seq, 0, sizeof seq);
    int r = jp_unwrap_result(c, jsp->arg, jb, false, &seq);
    if (r == JPER_ERROR) { free(seq.v); return r; }

    int res = JPER_NOTFOUND;
    for (int i = 0; i < seq.n; i++) {
        JsonVal *v = seq.v[i];
        if (json_kind(v) != 2) {
            free(seq.v);
            JP_ERR(c, "2203B", "operand of unary jsonpath operator %s is not "
                               "a numeric value", jp_op_name(jsp->type));
        }
        if (jsp->type == jpiMinus) {
            Numeric *n = jv_numval(v);
            Numeric *m = num_neg(n);
            v = jv_numeric(m);
            num_free(n); num_free(m);
        }
        res = jp_next(c, jsp, NULL, v, found);
        if (res == JPER_ERROR || c->stop) break;
    }
    free(seq.v);
    return res;
}

static int jp_keyvalue(JpCtx *c, JpItem *jsp, JsonVal *jb, JsonPathList *found)
{
    JpItem *nx = jsp->next;
    int res = JPER_NOTFOUND;
    int n = json_length(jb);
    for (int i = 0; i < n; i++) {
        const char *key = json_key_at(jb, i);
        JsonVal *val = json_val_at(jb, i);
        if (!key || !val) continue;
        Buf b; buf_init(&b);
        buf_adds(&b, "{\"key\":");
        jp_escape_json(&b, key);
        buf_adds(&b, ",\"value\":");
        char *vt = json_to_text(val, true);
        buf_adds(&b, vt);
        free(vt);
        buf_adds(&b, ",\"id\":0}");
        char *t = buf_steal(&b);
        JsonVal *o = jv_text(t);
        free(t);
        res = jp_next(c, jsp, nx, o, found);
        if (res == JPER_ERROR || c->stop) break;
    }
    return res;
}

#if PGVC_VN >= 130
/* --------------------------------------------------------- .datetime()
 * PostgreSQL 13 added the standard's datetime() method.  The value it makes
 * is a date/time one; everything that reads it here - printing it back as
 * jsonb and comparing two of them - only needs the canonical ISO spelling,
 * so that is what is carried, as a JSON string.
 */
typedef struct {
    bool hasdate, hastime, hastz;
    int  y, mo, d, hh, mi, ss;
    int  tzh, tzm;
    char frac[8];              /* the fraction digits without the dot */
} JpDt;

static bool jp_dt_digits(const char **p, int n, int *out)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (!isdigit((unsigned char)(*p)[i])) return false;
        v = v * 10 + ((*p)[i] - '0');
    }
    *p += n;
    *out = v;
    return true;
}

static int jp_dt_mdays(int y, int m)
{
    static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1 || m > 12) return 0;
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m-1];
}

/* Fill in whichever of the fields the text carries; false when the text is
 * not one of the shapes PostgreSQL recognises without a template. */
static bool jp_dt_scan(const char *t, JpDt *o)
{
    const char *p = t;
    memset(o, 0, sizeof *o);
    if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) &&
        isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3]) && p[4] == '-') {
        if (!jp_dt_digits(&p, 4, &o->y)) return false;
        p++;
        if (!jp_dt_digits(&p, 2, &o->mo)) return false;
        if (*p++ != '-') return false;
        if (!jp_dt_digits(&p, 2, &o->d)) return false;
        o->hasdate = true;
        if (*p == ' ' || *p == 'T' || *p == 't') p++;
        else if (!*p)
            return o->mo >= 1 && o->mo <= 12 &&
                   o->d >= 1 && o->d <= jp_dt_mdays(o->y, o->mo);
        else return false;
    }
    if (!jp_dt_digits(&p, 2, &o->hh)) return false;
    if (*p++ != ':') return false;
    if (!jp_dt_digits(&p, 2, &o->mi)) return false;
    if (*p++ != ':') return false;
    if (!jp_dt_digits(&p, 2, &o->ss)) return false;
    o->hastime = true;
    if (*p == '.') {
        p++;
        int n = 0;
        while (isdigit((unsigned char)*p)) {
            if (n >= 6) return false;        /* microseconds is all there is */
            o->frac[n++] = *p++;
        }
        if (!n) return false;
        o->frac[n] = '\0';
        while (n > 0 && o->frac[n-1] == '0') o->frac[--n] = '\0';
    }
    if (*p == '+' || *p == '-') {
        int sign = (*p == '-') ? -1 : 1;
        p++;
        if (!jp_dt_digits(&p, 2, &o->tzh)) return false;
        if (*p == ':') { p++; if (!jp_dt_digits(&p, 2, &o->tzm)) return false; }
        o->tzh *= sign;
        if (sign < 0) o->tzm = -o->tzm;
        o->hastz = true;
    }
    if (*p) return false;
    if (o->hasdate && !(o->mo >= 1 && o->mo <= 12 &&
                        o->d >= 1 && o->d <= jp_dt_mdays(o->y, o->mo)))
        return false;
    if (o->hh > 24 || o->mi > 59 || o->ss > 60) return false;
    return true;
}

static JsonVal *jp_dt_render(const JpDt *o)
{
    char buf[80];
    int n = 0;
    if (o->hasdate)
        n += snprintf(buf + n, sizeof buf - (size_t)n, "%04d-%02d-%02d", o->y, o->mo, o->d);
    if (o->hasdate && o->hastime)
        n += snprintf(buf + n, sizeof buf - (size_t)n, "T");
    if (o->hastime) {
        n += snprintf(buf + n, sizeof buf - (size_t)n, "%02d:%02d:%02d", o->hh, o->mi, o->ss);
        if (o->frac[0])
            n += snprintf(buf + n, sizeof buf - (size_t)n, ".%s", o->frac);
    }
    if (o->hastz)
        snprintf(buf + n, sizeof buf - (size_t)n, "%c%02d:%02d",
                 (o->tzh < 0 || o->tzm < 0) ? '-' : '+',
                 o->tzh < 0 ? -o->tzh : o->tzh, o->tzm < 0 ? -o->tzm : o->tzm);
    return jv_string(buf);
}

/* A template argument, in the subset of to_timestamp()'s patterns the
 * standard's datetime() is given.  Its complaints come from the format
 * parser, so the silent argument does not swallow them. */
static JsonVal *jp_dt_template(JpCtx *c, const char *t, const char *fmt)
{
    static const struct { const char *kw; int n; char field; } kws[] = {
        {"YYYY",4,'Y'}, {"MM",2,'M'}, {"DD",2,'D'}, {"HH24",2,'h'}, {"HH12",2,'h'},
        {"HH",2,'h'}, {"MI",2,'m'}, {"SS",2,'s'}, {"TZH",2,'z'}, {"TZM",2,'Z'},
        {NULL,0,0}
    };
    JpDt o;
    memset(&o, 0, sizeof o);
    o.y = 2000; o.mo = 1; o.d = 1;
    const char *f = fmt, *p = t;
    while (*f) {
        bool hit = false;
        for (int i = 0; kws[i].kw && !hit; i++) {
            size_t L = strlen(kws[i].kw);
            if (strncasecmp(f, kws[i].kw, L)) continue;
            hit = true;
            f += L;
            int sign = 1;
            if (kws[i].field == 'z' && (*p == '+' || *p == '-')) {
                if (*p == '-') sign = -1;
                p++;
            }
            int v = 0, got = 0;
            while (got < kws[i].n && isdigit((unsigned char)*p)) { v = v*10 + (*p - '0'); p++; got++; }
            if (!got)
                ereport(c->s, "22007", "invalid value \"%s\" for \"%s\"", p, kws[i].kw);
            switch (kws[i].field) {
            case 'Y': o.y = v; o.hasdate = true; break;
            case 'M': o.mo = v; o.hasdate = true; break;
            case 'D': o.d = v; o.hasdate = true; break;
            case 'h': o.hh = v; o.hastime = true; break;
            case 'm': o.mi = v; o.hastime = true; break;
            case 's': o.ss = v; o.hastime = true; break;
            case 'z': o.tzh = v * sign; o.hastz = true; break;
            case 'Z': o.tzm = v * sign; o.hastz = true; break;
            default: break;
            }
        }
        if (hit) continue;
        if (strchr(" -./,';:", *f) == NULL)
            ereport(c->s, "22007", "invalid datetime format separator: \"%c\"", *f);
        if (*p == *f) p++;
        f++;
    }
    if (*p) ereport(c->s, "22007", "trailing characters remain in input string "
                                   "after datetime format");
    return jp_dt_render(&o);
}
#endif /* PGVC_VN >= 130 */

#if PGVC_VN >= 170
/* The type-conversion item methods PostgreSQL 17 added.  A conversion that
 * cannot be made is a structural error, so silent => true swallows it. */
static int jp_convert_method(JpCtx *c, JpItem *jsp, JsonVal *jb, JsonPathList *found)
{
    const char *m = jp_op_name(jsp->type);
    int k = json_kind(jb);

    if (jsp->type == jpiStringM) {
        const char *txt = NULL;
        if (k == 3) txt = json_str_value(jb);
        else if (k == 2) txt = json_num_text(jb);
        else if (k == 1) txt = jv_boolval(jb) ? "true" : "false";
        else
            JP_ERR(c, "2203F", "jsonpath item method .string() can only be applied "
                               "to a boolean, string, numeric, or datetime value");
        return jp_next(c, jsp, NULL, jv_string(txt), found);
    }

    if (jsp->type == jpiBoolean) {
        if (k == 1) return jp_next(c, jsp, NULL, jv_bool(jv_boolval(jb)), found);
        if (k == 2) {
            Numeric *n = jv_numval(jb);
            int64 iv = 0;
            bool ok = n && num_to_int64(n, &iv);
            if (n) num_free(n);
            if (!ok)
                JP_ERR(c, "2203F", "argument \"%s\" of jsonpath item method "
                                   ".boolean() is invalid for type boolean",
                                   json_num_text(jb));
            return jp_next(c, jsp, NULL, jv_bool(iv != 0), found);
        }
        if (k == 3) {
            const char *sv = json_str_value(jb);
            if (!sv) sv = "";
            char *lo = xstrdup(sv);
            for (char *q = lo; *q; q++) *q = (char)tolower((unsigned char)*q);
            bool tv = false, okb = true;
            if (!strcmp(lo,"true") || !strcmp(lo,"t") || !strcmp(lo,"yes") ||
                !strcmp(lo,"y") || !strcmp(lo,"on") || !strcmp(lo,"1")) tv = true;
            else if (!strcmp(lo,"false") || !strcmp(lo,"f") || !strcmp(lo,"no") ||
                     !strcmp(lo,"n") || !strcmp(lo,"off") || !strcmp(lo,"0")) tv = false;
            else okb = false;
            free(lo);
            if (!okb)
                JP_ERR(c, "2203F", "argument \"%s\" of jsonpath item method "
                                   ".boolean() is invalid for type boolean", sv);
            return jp_next(c, jsp, NULL, jv_bool(tv), found);
        }
        JP_ERR(c, "2203F", "jsonpath item method .boolean() can only be applied "
                           "to a boolean, string, or numeric value");
    }

    const char *txt = (k == 2) ? json_num_text(jb)
                    : (k == 3) ? json_str_value(jb) : NULL;
    if (!txt)
        JP_ERR(c, "2203F", "jsonpath item method .%s() can only be applied "
                           "to a string or numeric value", m);

    if (jsp->type == jpiDate || jsp->type == jpiTime || jsp->type == jpiTimestamp) {
        const char *err = NULL;
        bool ok;
        if (jsp->type == jpiDate)      { int32 d;  ok = dt_parse_date(txt, &d, &err); }
        else if (jsp->type == jpiTime) { int64 u;  ok = dt_parse_time(txt, &u, &err); }
        else                           { int64 u;  ok = dt_parse_timestamp(txt, &u, false, &err); }
        if (!ok)
            JP_ERR(c, "2203F", "argument \"%s\" of jsonpath item method .%s() is "
                               "invalid for type %s", txt, m, m);
        return jp_next(c, jsp, NULL, jv_string(txt), found);
    }

    bool ok = false;
    Numeric *n = num_from_str(txt, &ok);
    if (!ok) {
        if (n) num_free(n);
        JP_ERR(c, "2203F", "argument \"%s\" of jsonpath item method .%s() is "
                           "invalid for type %s", txt, m,
                           jsp->type == jpiBigint  ? "bigint" :
                           jsp->type == jpiInteger ? "integer" : "numeric");
    }
    if (jsp->type == jpiBigint || jsp->type == jpiInteger) {
        Numeric *r = num_round(n, 0);
        int64 iv = 0;
        char *canon = num_to_str(n);
        bool fits = r && num_to_int64(r, &iv);
        if (fits && jsp->type == jpiInteger &&
            (iv > 2147483647LL || iv < -2147483648LL)) fits = false;
        if (r) num_free(r);
        num_free(n);
        if (!fits) {
            if (c->throwerr)
                ereport(c->s, "2203F", "argument \"%s\" of jsonpath item method "
                        ".%s() is invalid for type %s", canon, m,
                        jsp->type == jpiBigint ? "bigint" : "integer");
            free(canon);
            return JPER_ERROR;
        }
        free(canon);
        char b[32]; snprintf(b, sizeof b, "%lld", (long long)iv);
        return jp_next(c, jsp, NULL, jv_text(b), found);
    }
    /* .number() and .decimal() */
    Numeric *r = n;
    if (jsp->type == jpiDecimal && jsp->hasprec) {
        r = num_round(n, jsp->hasscale ? jsp->scale : 0);
        num_free(n);
    }
    JsonVal *v = jv_numeric(r);
    num_free(r);
    return jp_next(c, jsp, NULL, v, found);
}
#endif

static int jp_item_1(JpCtx *c, JpItem *jsp, JsonVal *jb, JsonPathList *found, bool unwrap)
{
    int res = JPER_NOTFOUND;
    int k = json_kind(jb);

    switch (jsp->type) {
    /* ---- a predicate used where a value is wanted yields its own boolean */
    case jpiAnd: case jpiOr: case jpiNot: case jpiIsUnknown:
    case jpiEqual: case jpiNotEqual: case jpiLess: case jpiGreater:
    case jpiLessOrEqual: case jpiGreaterOrEqual:
    case jpiExists: case jpiStartsWith: case jpiLikeRegex:
        return jp_append_bool(c, jsp, found, jp_bool_item(c, jsp, jb));

    case jpiRoot:    return jp_next(c, jsp, NULL, c->root, found);
    case jpiCurrent: return jp_next(c, jsp, NULL, c->current, found);

    case jpiNull:    return jp_next(c, jsp, NULL, jv_null(), found);
    case jpiBool:    return jp_next(c, jsp, NULL, jv_bool(jsp->bval), found);
    case jpiString:  return jp_next(c, jsp, NULL, jv_string(jsp->str), found);
    case jpiNumeric: return jp_next(c, jsp, NULL, jv_text(jsp->str), found);
    case jpiVariable: return jp_next(c, jsp, NULL, jp_variable(c, jsp), found);

    case jpiKey:
        if (k == 5) {
            JsonVal *v = json_get_key(jb, jsp->str);
            if (v) return jp_next(c, jsp, NULL, v, found);
            if (!c->ignore_struct)
                JP_ERR(c, "2203A", "JSON object does not contain key \"%s\"", jsp->str);
        } else if (unwrap && k == 4) {
            return jp_unwrap_target(c, jsp, jb, found, false);
        } else if (!c->ignore_struct) {
            JP_ERR(c, "2203A", "jsonpath member accessor can only be applied "
                               "to an object");
        }
        break;

    case jpiAnyKey:
        if (k == 5)
            return jp_any(c, jsp->next, jb, found, 1, 1, 1, false, c->lax);
        if (unwrap && k == 4)
            return jp_unwrap_target(c, jsp, jb, found, false);
        if (!c->ignore_struct)
            JP_ERR(c, "2203C", "jsonpath wildcard member accessor can only be "
                               "applied to an object");
        break;

    case jpiAnyArray:
        if (k == 4)
            return jp_any(c, jsp->next, jb, found, 1, 1, 1, false, c->lax);
        if (c->lax)
            return jp_next(c, jsp, NULL, jb, found);
        if (!c->ignore_struct)
            JP_ERR(c, "22039", "jsonpath wildcard array accessor can only be "
                               "applied to an array");
        break;

    case jpiIndexArray:
        if (k == 4 || c->lax) {
            int saved = c->innermost_size;
            int size = (k == 4) ? json_length(jb) : -1;
            bool singleton = (size < 0);
            if (singleton) size = 1;
            c->innermost_size = size;
            for (int i = 0; i < jsp->nsubs; i++) {
                int ifrom = 0, ito = 0;
                res = jp_array_index(c, jsp->subs[i].from, jb, &ifrom);
                if (res == JPER_ERROR) break;
                if (jsp->subs[i].range) {
                    res = jp_array_index(c, jsp->subs[i].to, jb, &ito);
                    if (res == JPER_ERROR) break;
                } else ito = ifrom;

                if (!c->ignore_struct &&
                    (ifrom < 0 || ifrom > ito || ito >= size)) {
                    c->innermost_size = saved;
                    JP_ERR(c, "22033", "jsonpath array subscript is out of bounds");
                }
                if (ifrom < 0) ifrom = 0;
                if (ito >= size) ito = size - 1;

                res = JPER_NOTFOUND;
                for (int idx = ifrom; idx <= ito; idx++) {
                    JsonVal *v = singleton ? jb : json_val_at(jb, idx);
                    if (!v) continue;
                    res = jp_next(c, jsp, jsp->next, v, found);
                    if (res == JPER_ERROR || c->stop) break;
                }
                if (res == JPER_ERROR || c->stop) break;
            }
            c->innermost_size = saved;
        } else if (!c->ignore_struct) {
            JP_ERR(c, "22039", "jsonpath array accessor can only be applied "
                               "to an array");
        }
        break;

    case jpiLast:
        if (c->innermost_size < 0)
            ereport(c->s, "42601", "evaluating jsonpath LAST outside of array subscript");
        return jp_next(c, jsp, NULL, jv_int(c->innermost_size - 1), found);

    case jpiAny: {
        if (jsp->first == 0) {
            bool saved = c->ignore_struct;
            c->ignore_struct = true;
            res = jp_next(c, jsp, jsp->next, jb, found);
            c->ignore_struct = saved;
            if (res == JPER_ERROR || c->stop) break;
        }
        if (k == 4 || k == 5)
            res = jp_any(c, jsp->next, jb, found, 1, jsp->first, jsp->last,
                         true, c->lax);
        break;
    }

    case jpiFilter: {
        if (unwrap && k == 4)
            return jp_unwrap_target(c, jsp, jb, found, false);
        JsonVal *prev = c->current;
        c->current = jb;
        int st = jp_bool_item(c, jsp->arg, jb);
        c->current = prev;
        if (st != JPB_TRUE) return JPER_NOTFOUND;
        return jp_next(c, jsp, NULL, jb, found);
    }

    case jpiType:
        return jp_next(c, jsp, NULL, jv_string(jp_typename(jb)), found);

    case jpiSize: {
        int size = (k == 4) ? json_length(jb) : -1;
        if (size < 0) {
            if (!c->lax)
                JP_ERR(c, "22039", "jsonpath item method .size() can only be "
                                   "applied to an array");
            size = 1;
        }
        return jp_next(c, jsp, NULL, jv_int(size), found);
    }

    case jpiAbs: case jpiFloor: case jpiCeiling: {
        if (unwrap && k == 4)
            return jp_unwrap_target(c, jsp, jb, found, false);
        if (k != 2)
            JP_ERR(c, "2203B", "jsonpath item method .%s() can only be applied "
                               "to a numeric value", jp_op_name(jsp->type));
        Numeric *n = jv_numval(jb);
        Numeric *r = (jsp->type == jpiAbs)   ? num_abs(n)
                   : (jsp->type == jpiFloor) ? num_floor(n) : num_ceil(n);
        JsonVal *v = jv_numeric(r);
        num_free(n); num_free(r);
        return jp_next(c, jsp, NULL, v, found);
    }

    case jpiDouble: {
        if (unwrap && k == 4)
            return jp_unwrap_target(c, jsp, jb, found, false);
        double d;
        if (k == 2) {
            Numeric *n = jv_numval(jb);
            d = num_to_double(n);
            num_free(n);
            if (isnan(d) || isinf(d))
                JP_ERR(c, "22003", "numeric argument of jsonpath item method "
                                   ".double() is out of range for type double precision");
        } else if (k == 3) {
            const char *sv = json_str_value(jb);
            if (!sv) sv = "";
            char *end = NULL;
            errno = 0;
            d = strtod(sv, &end);
            bool bad = (end == sv);
            if (!bad) { while (*end && isspace((unsigned char)*end)) end++;
                        bad = (*end != '\0'); }
            if (!bad && (errno == ERANGE || isnan(d) || isinf(d))) bad = true;
            if (bad) {
#if PGVC_VN >= 170
                JP_ERR(c, "22P02", "argument \"%s\" of jsonpath item method "
                                   ".double() is invalid for type double precision", sv);
#else
                JP_ERR(c, "22P02", "string argument of jsonpath item method "
                                   ".double() is not a valid representation of "
                                   "a double precision number");
#endif
            }
        } else {
            JP_ERR(c, "22036", "jsonpath item method .double() can only be "
                               "applied to a string or numeric value");
        }
        /* float8_numeric() goes through the %.15g text, and so do we. */
        char buf[64];
        snprintf(buf, sizeof buf, "%.*g", 15, d);
        bool ok = false;
        Numeric *n = num_from_str(buf, &ok);
        JsonVal *v = jv_numeric(n);
        num_free(n);
        return jp_next(c, jsp, NULL, v, found);
    }

    case jpiKeyValue:
        if (unwrap && k == 4)
            return jp_unwrap_target(c, jsp, jb, found, false);
        if (k != 5)
            JP_ERR(c, "2203C", "jsonpath item method .keyvalue() can only be "
                               "applied to an object");
        return jp_keyvalue(c, jsp, jb, found);

    case jpiDatetime: {
#if PGVC_VN >= 130
        if (unwrap && k == 4)
            return jp_unwrap_target(c, jsp, jb, found, false);
        if (k != 3)
            JP_ERR(c, "22031", "jsonpath item method .datetime() can only be "
                               "applied to a string");
        const char *sv = json_str_value(jb);
        if (!sv) sv = "";
        JsonVal *dv;
        if (jsp->str) dv = jp_dt_template(c, sv, jsp->str);
        else {
            JpDt o;
            if (!jp_dt_scan(sv, &o)) {
                if (c->throwerr)
                    err_hint(c->s, "Use a datetime template argument to specify "
                                   "the input data format.");
                JP_ERR(c, "22031", "datetime format is not recognized: \"%s\"", sv);
            }
            dv = jp_dt_render(&o);
        }
        return jp_next(c, jsp, NULL, dv, found);
#else
        return JPER_NOTFOUND;
#endif
    }

    case jpiBigint: case jpiInteger: case jpiNumber: case jpiDecimal:
    case jpiBoolean: case jpiStringM: case jpiDate: case jpiTime:
    case jpiTimestamp:
#if PGVC_VN >= 170
        if (unwrap && k == 4)
            return jp_unwrap_target(c, jsp, jb, found, false);
        return jp_convert_method(c, jsp, jb, found);
#else
        return JPER_NOTFOUND;      /* the parser never builds these below 17 */
#endif

    case jpiAdd: case jpiSub: case jpiMul: case jpiDiv: case jpiMod:
        return jp_binary_arith(c, jsp, jb, found);
    case jpiPlus: case jpiMinus:
        return jp_unary_arith(c, jsp, jb, found);
    }
    return res;
}

static int jp_item(JpCtx *c, JpItem *jsp, JsonVal *jb, JsonPathList *found, bool unwrap)
{
    if (c->depth >= 400)
        ereport(c->s, "54001", "stack depth limit exceeded");
    c->depth++;
    int r = jp_item_1(c, jsp, jb, found, unwrap);
    c->depth--;
    return r;
}

bool jsonpath_run(Session *s, const JsonPath *jp, JsonVal *doc, JsonVal *vars,
                  bool throwerr, bool first_only, JsonPathList *out)
{
    if (vars && json_kind(vars) != 5) {
        err_detail(s, "Jsonpath parameters should be encoded as key-value "
                      "pairs of \"vars\" object.");
        ereport(s, "22023", "\"vars\" argument is not an object");
    }
    JpCtx c;
    memset(&c, 0, sizeof c);
    c.s = s;
    c.root = doc;
    c.current = doc;
    c.vars = vars;
    c.lax = !jp->strict;
    c.ignore_struct = c.lax;
    c.throwerr = throwerr;
    c.first_only = first_only;
    c.top = out;
    c.innermost_size = -1;
    return jp_exec(&c, jp->expr, doc, out) != JPER_ERROR;
}

/* ======================================================== the SQL surface */

static Datum jp_jsonb(JsonVal *v)
{
    Datum d;
    memset(&d, 0, sizeof d);
    d.type = JSONBOID;
    d.v.jb = v;
    return d;
}

/* jsonb_path_query() is set-returning; the executor expands the array. */
static Datum jp_jsonb_array_datum(JsonPathList *l)
{
    ArrayVal *a = xmalloc(sizeof *a);
    memset(a, 0, sizeof *a);
    a->elemtype = JSONBOID;
    a->ndim = 1; a->dims[0] = l->n; a->lbound[0] = 1; a->nelem = l->n;
    a->elems = xmalloc((size_t)(l->n ? l->n : 1) * sizeof(Datum));
    for (int i = 0; i < l->n; i++) a->elems[i] = jp_jsonb(l->v[i]);
    Datum d;
    memset(&d, 0, sizeof d);
    d.type = JSONBARRAYOID;
    d.v.arr = a;
    return d;
}

/* wrap the matched items in one jsonb array */
static JsonVal *jp_wrap_array(JsonPathList *l)
{
    Buf b; buf_init(&b);
    buf_addc(&b, '[');
    for (int i = 0; i < l->n; i++) {
        if (i) buf_addc(&b, ',');
        char *t = json_to_text(l->v[i], true);
        buf_adds(&b, t);
        free(t);
    }
    buf_addc(&b, ']');
    char *t = buf_steal(&b);
    JsonVal *v = jv_text(t);
    free(t);
    return v;
}

/* Which of the five is this, and does it come from an operator?  Operators
 * pass no variables and never raise. */
typedef enum { JPF_QUERY, JPF_ARRAY, JPF_FIRST, JPF_EXISTS, JPF_MATCH } JpFunc;

static bool jp_func_of(const char *name, JpFunc *out, bool *opr)
{
    static const struct { const char *n; JpFunc f; bool o; } tab[] = {
        {"jsonb_path_query",           JPF_QUERY,  false},
        {"jsonb_path_query_array",     JPF_ARRAY,  false},
        {"jsonb_path_query_first",     JPF_FIRST,  false},
        {"jsonb_path_exists",          JPF_EXISTS, false},
        {"jsonb_path_match",           JPF_MATCH,  false},
        {"jsonb_path_exists_opr",      JPF_EXISTS, true},
        {"jsonb_path_match_opr",       JPF_MATCH,  true},
#if PGVC_VN >= 130
        {"jsonb_path_query_tz",        JPF_QUERY,  false},
        {"jsonb_path_query_array_tz",  JPF_ARRAY,  false},
        {"jsonb_path_query_first_tz",  JPF_FIRST,  false},
        {"jsonb_path_exists_tz",       JPF_EXISTS, false},
        {"jsonb_path_match_tz",        JPF_MATCH,  false},
#endif
        {NULL, JPF_QUERY, false}
    };
    for (int i = 0; tab[i].n; i++)
        if (!strcmp(name, tab[i].n)) { *out = tab[i].f; *opr = tab[i].o; return true; }
    return false;
}

static Datum jp_null_of(JpFunc f)
{
    if (f == JPF_EXISTS || f == JPF_MATCH) return d_null(BOOLOID);
    if (f == JPF_QUERY) return d_null(JSONBARRAYOID);
    return d_null(JSONBOID);
}

Datum jsonpath_builtin(Session *s, const char *name, Datum *args, int nargs, bool *found)
{
    JpFunc f;
    bool opr = false;

    *found = false;
    if (nargs < 2 || nargs > 4) return d_null(JSONBOID);
    if (!jp_func_of(name, &f, &opr)) return d_null(JSONBOID);
    *found = true;

    for (int i = 0; i < nargs; i++)
        if (args[i].isnull) return jp_null_of(f);

    Datum target = coerce_to(s, args[0], JSONBOID, -1, 1);
    char *ptext = out_func(s, args[1]);
    JsonPath *jp = jsonpath_parse(s, ptext, (int)strlen(ptext));
    free(ptext);

    /* The SQL functions default "vars" to an empty object, which is not the
     * same as no variables at all: an undefined $name is an error there but
     * simply JSON null for the operators. */
    JsonVal *vars = NULL;
    bool silent = opr;
    if (!opr) {
        vars = (nargs >= 3) ? coerce_to(s, args[2], JSONBOID, -1, 1).v.jb : jv_text("{}");
        if (nargs >= 4) silent = coerce_to(s, args[3], BOOLOID, -1, 1).v.i != 0;
    }
#if PGVC_VN >= 140
    /* Since release 14 the operators hand the executor an empty "vars"
     * object too, so an undefined $name is an error there as well. */
    else vars = jv_text("{}");
#endif

    /* Only jsonb_path_exists() asks a yes/no question, and only in lax mode:
     * strict mode has to look at every item to be sure none of them errors. */
    bool first_only = (f == JPF_EXISTS) && !jp->strict;
    JsonPathList out;
    memset(&out, 0, sizeof out);
    bool ok = jsonpath_run(s, jp, target.v.jb, vars, !silent, first_only, &out);
    jsonpath_free(jp);

    Datum r;
    switch (f) {
    case JPF_QUERY:
        r = jp_jsonb_array_datum(&out);
        break;
    case JPF_ARRAY:
        r = jp_jsonb(jp_wrap_array(&out));
        break;
    case JPF_FIRST:
        r = out.n ? jp_jsonb(out.v[0]) : d_null(JSONBOID);
        break;
    case JPF_EXISTS:
        r = ok ? d_bool(out.n > 0) : d_null(BOOLOID);
        break;
    default:
        if (out.n == 1 && json_kind(out.v[0]) == 1) r = d_bool(jv_boolval(out.v[0]));
        else if (out.n == 1 && json_kind(out.v[0]) == 0) r = d_null(BOOLOID);
        else if (!silent) ereport(s, "22038", "single boolean result is expected");
        else r = d_null(BOOLOID);
        break;
    }
    free(out.v);
    return r;
}

Datum jsonpath_operator(Session *s, const char *name, Datum *args, int nargs, bool *handled)
{
    *handled = false;
    if (nargs != 2) return d_null(BOOLOID);
    if (strcmp(name, "@?") && strcmp(name, "@@")) return d_null(BOOLOID);
    if (args[0].type != JSONBOID) return d_null(BOOLOID);
    if (args[1].type != JSONPATHOID && args[1].type != UNKNOWNOID)
        return d_null(BOOLOID);
    *handled = true;
    bool f2 = false;
    return jsonpath_builtin(s, !strcmp(name, "@?") ? "jsonb_path_exists_opr"
                                                   : "jsonb_path_match_opr",
                            args, 2, &f2);
}

#endif /* PGVC_VN >= 120 */
