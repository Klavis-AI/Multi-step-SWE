/* lex.c - SQL tokenizer following PostgreSQL's lexical rules. */
#include "engine.h"
#include "lex.h"

static const char *kwlist[] = {
#define KW(x) #x,
#include "kwlist.h"
#undef KW
    NULL
};

bool kw_is_reserved(const char *lower)
{
    /* PostgreSQL's fully reserved key words: these can never be a bare column
     * label or identifier.  From the "SQL Key Words" appendix. */
    static const char *res[] = {
        "all","analyse","analyze","and","any","array","as","asc","asymmetric","both",
        "case","cast","check","collate","column","constraint","create","current_catalog",
        "current_date","current_role","current_time","current_timestamp","current_user",
        "default","deferrable","desc","distinct","do","else","end","except","false",
        "fetch","for","foreign","from","grant","group","having","in","initially",
        "intersect","into","lateral","leading","limit","localtime","localtimestamp",
        "not","null","offset","on","only","or","order","placing","primary","references",
        "returning","select","session_user","some","symmetric","table","then","to",
        "trailing","true","union","unique","user","using","variadic","when","where",
        "window","with", NULL };
    for (int i = 0; res[i]; i++)
        if (!strcmp(res[i], lower)) return true;
    return false;
}

bool kw_exists(const char *lower)
{
    for (int i = 0; kwlist[i]; i++)
        if (!strcmp(kwlist[i], lower)) return true;
    return false;
}

char *lower_ident(const char *s)
{
    size_t n = strlen(s);
    char *p = xmalloc(n + 1);
    for (size_t i = 0; i < n; i++)
        p[i] = (s[i] >= 'A' && s[i] <= 'Z') ? (char)(s[i] - 'A' + 'a') : s[i];
    p[n] = '\0';
    return p;
}

static bool ident_start(int c) { return isalpha((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }
static bool ident_cont(int c)  { return ident_start(c) || isdigit((unsigned char)c) || c == '$'; }

/* Operator characters PostgreSQL allows in a user operator name. */
static bool opchar(int c) { return strchr("+-*/<>=~!@#%^&|`?", c) != NULL; }

void lex_init(Lexer *L, Session *s, const char *src)
{
    L->s = s; L->src = src; L->p = src; L->ntok = 0; L->cap = 0; L->toks = NULL;
}

static void push(Lexer *L, Tok t)
{
    if (L->ntok == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 64;
        L->toks = xrealloc(L->toks, L->cap * sizeof(Tok));
    }
    L->toks[L->ntok++] = t;
}

static void lexfail(Lexer *L, const char *p, const char *fmt, ...)
{
    char msg[512];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    ereport_pos(L->s, "42601", (int)(p - L->src) + 1, "%s", msg);
}

void lex_all(Lexer *L)
{
    const char *p = L->src;
    for (;;) {
        while (*p) {
            if (isspace((unsigned char)*p)) { p++; continue; }
            if (p[0] == '-' && p[1] == '-') { while (*p && *p != '\n') p++; continue; }
            if (p[0] == '/' && p[1] == '*') {
                int depth = 1; p += 2;
                while (*p && depth) {
                    if (p[0] == '/' && p[1] == '*') { depth++; p += 2; }
                    else if (p[0] == '*' && p[1] == '/') { depth--; p += 2; }
                    else p++;
                }
                continue;
            }
            break;
        }
        if (!*p) break;
        Tok t; memset(&t, 0, sizeof t);
        t.pos = (int)(p - L->src) + 1;
        t.start = p;

        if (*p == ';') { t.kind = T_SEMI; p++; t.end = p; push(L, t); continue; }
        if (*p == '(') { t.kind = T_LPAREN; p++; t.end = p; push(L, t); continue; }
        if (*p == ')') { t.kind = T_RPAREN; p++; t.end = p; push(L, t); continue; }
        if (*p == '[') { t.kind = T_LBRACK; p++; t.end = p; push(L, t); continue; }
        if (*p == ']') { t.kind = T_RBRACK; p++; t.end = p; push(L, t); continue; }
        if (*p == ',') { t.kind = T_COMMA;  p++; t.end = p; push(L, t); continue; }

        /* parameter */
        if (*p == '$' && isdigit((unsigned char)p[1])) {
            const char *q = p + 1;
            while (isdigit((unsigned char)*q)) q++;
            t.kind = T_PARAM; t.ival = atoi(p + 1); p = q; t.end = p; push(L, t); continue;
        }
        /* dollar-quoted string */
        if (*p == '$') {
            const char *q = p + 1;
            while (ident_cont(*q) && *q != '$') q++;
            if (*q == '$') {
                size_t taglen = (size_t)(q - p) + 1;      /* includes both $ */
                char tag[128];
                if (taglen >= sizeof tag) lexfail(L, p, "dollar quote tag too long");
                memcpy(tag, p, taglen); tag[taglen] = '\0';
                const char *body = q + 1;
                const char *e = strstr(body, tag);
                if (!e) lexfail(L, p, "unterminated dollar-quoted string at or near \"%s\"", tag);
                t.kind = T_STRING;
                t.sval = xstrndup(body, (size_t)(e - body));
                p = e + taglen; t.end = p; push(L, t); continue;
            }
        }
        /* string literals */
        bool ustr = false;
        if ((*p == 'u' || *p == 'U') && p[1] == '&' && p[2] == '\'') { p += 2; ustr = true; }
        if (*p == '\'' || ((*p == 'e' || *p == 'E') && p[1] == '\'') ||
            ((*p == 'b' || *p == 'B' || *p == 'x' || *p == 'X') && p[1] == '\'')) {
            bool escape = (*p == 'e' || *p == 'E');
            bool bitstr = (*p == 'b' || *p == 'B' || *p == 'x' || *p == 'X');
            char bitkind = bitstr ? (char)tolower((unsigned char)*p) : 0;
            if (escape || bitstr) p++;
            Buf b; buf_init(&b);
            for (;;) {
                if (*p != '\'') lexfail(L, t.start, "unterminated quoted string");
                p++;
                while (*p) {
                    if (*p == '\'') {
                        if (p[1] == '\'') { buf_addc(&b, '\''); p += 2; continue; }
                        p++; break;
                    }
                    if (escape && *p == '\\') {
                        p++;
                        switch (*p) {
                        case 'n': buf_addc(&b, '\n'); p++; break;
                        case 't': buf_addc(&b, '\t'); p++; break;
                        case 'r': buf_addc(&b, '\r'); p++; break;
                        case 'b': buf_addc(&b, '\b'); p++; break;
                        case 'f': buf_addc(&b, '\f'); p++; break;
                        case 'v': buf_addc(&b, '\v'); p++; break;
                        case '\\': buf_addc(&b, '\\'); p++; break;
                        case '\'': buf_addc(&b, '\''); p++; break;
                        case 'u': case 'U': {
                            int nd = (*p == 'u') ? 4 : 8; p++;
                            unsigned long cp = 0; int got = 0;
                            while (got < nd && isxdigit((unsigned char)*p)) {
                                cp = cp * 16 + (unsigned long)(isdigit((unsigned char)*p) ? *p - '0'
                                     : (tolower((unsigned char)*p) - 'a' + 10));
                                p++; got++;
                            }
                            /* encode as UTF-8 */
                            if (cp < 0x80) buf_addc(&b, (char)cp);
                            else if (cp < 0x800) { buf_addc(&b, (char)(0xC0 | (cp >> 6)));
                                                   buf_addc(&b, (char)(0x80 | (cp & 0x3F))); }
                            else if (cp < 0x10000) { buf_addc(&b, (char)(0xE0 | (cp >> 12)));
                                                     buf_addc(&b, (char)(0x80 | ((cp >> 6) & 0x3F)));
                                                     buf_addc(&b, (char)(0x80 | (cp & 0x3F))); }
                            else { buf_addc(&b, (char)(0xF0 | (cp >> 18)));
                                   buf_addc(&b, (char)(0x80 | ((cp >> 12) & 0x3F)));
                                   buf_addc(&b, (char)(0x80 | ((cp >> 6) & 0x3F)));
                                   buf_addc(&b, (char)(0x80 | (cp & 0x3F))); }
                            break;
                        }
                        case 'x': case 'X': {
                            p++; unsigned v = 0; int got = 0;
                            while (got < 2 && isxdigit((unsigned char)*p)) {
                                v = v * 16 + (unsigned)(isdigit((unsigned char)*p) ? *p - '0'
                                    : (tolower((unsigned char)*p) - 'a' + 10));
                                p++; got++;
                            }
                            buf_addc(&b, (char)v); break;
                        }
                        default:
                            if (*p >= '0' && *p <= '7') {
                                unsigned v = 0; int got = 0;
                                while (got < 3 && *p >= '0' && *p <= '7') { v = v*8 + (unsigned)(*p-'0'); p++; got++; }
                                buf_addc(&b, (char)v);
                            } else { buf_addc(&b, *p); p++; }
                        }
                        continue;
                    }
                    buf_addc(&b, *p); p++;
                }
                /* string continuation across a newline */
                const char *q = p;
                while (isspace((unsigned char)*q)) {
                    if (*q == '\n') {
                        const char *r = q + 1;
                        while (isspace((unsigned char)*r)) r++;
                        if (*r == '\'') { p = r; goto cont; }
                    }
                    q++;
                }
                break;
            cont: ;
            }
            if (bitstr) {
                /* B'101' / X'ff' become bit strings; we expose them as text of the
                 * expanded binary form, matching PostgreSQL's `bit` output. */
                Buf o; buf_init(&o);
                if (bitkind == 'x') {
                    for (size_t i = 0; i < b.len; i++) {
                        int v = isdigit((unsigned char)b.data[i]) ? b.data[i]-'0'
                                : (tolower((unsigned char)b.data[i]) - 'a' + 10);
                        for (int k = 3; k >= 0; k--) buf_addc(&o, (char)('0' + ((v >> k) & 1)));
                    }
                } else buf_adds(&o, b.data ? b.data : "");
                buf_free(&b); b = o;
                t.bitstr = true;
            }
            if (ustr) {
                /* U&'...' : \XXXX, \+XXXXXX and \\ inside the literal.  An
                 * optional UESCAPE clause may pick another escape character. */
                char esc = '\\';
                const char *q2 = p;
                while (isspace((unsigned char)*q2)) q2++;
                if ((q2[0]=='u'||q2[0]=='U') && (q2[1]=='e'||q2[1]=='E') &&
                    (q2[2]=='s'||q2[2]=='S') && (q2[3]=='c'||q2[3]=='C') &&
                    (q2[4]=='a'||q2[4]=='A') && (q2[5]=='p'||q2[5]=='P') &&
                    (q2[6]=='e'||q2[6]=='E')) {
                    const char *q3 = q2 + 7;
                    while (isspace((unsigned char)*q3)) q3++;
                    if (*q3 == '\'' && q3[1] && q3[2] == '\'') { esc = q3[1]; p = q3 + 3; }
                }
                Buf o; buf_init(&o);
                for (size_t i = 0; i < b.len; ) {
                    if (b.data[i] == esc && i + 1 < b.len && b.data[i+1] == esc) {
                        buf_addc(&o, esc); i += 2; continue;
                    }
                    int nd = 0;
                    if (b.data[i] == esc && i + 1 < b.len && b.data[i+1] == '+') nd = 6;
                    else if (b.data[i] == esc) nd = 4;
                    if (nd) {
                        size_t j = i + 1 + (nd == 6 ? 1 : 0);
                        unsigned long cp = 0; int got = 0;
                        while (got < nd && j < b.len && isxdigit((unsigned char)b.data[j])) {
                            char ch2 = b.data[j];
                            cp = cp * 16 + (unsigned long)(isdigit((unsigned char)ch2) ? ch2 - '0'
                                 : (tolower((unsigned char)ch2) - 'a' + 10));
                            j++; got++;
                        }
                        if (got == nd) {
                            if (cp < 0x80) buf_addc(&o, (char)cp);
                            else if (cp < 0x800) { buf_addc(&o, (char)(0xC0 | (cp >> 6)));
                                                   buf_addc(&o, (char)(0x80 | (cp & 0x3F))); }
                            else if (cp < 0x10000) { buf_addc(&o, (char)(0xE0 | (cp >> 12)));
                                                     buf_addc(&o, (char)(0x80 | ((cp >> 6) & 0x3F)));
                                                     buf_addc(&o, (char)(0x80 | (cp & 0x3F))); }
                            else { buf_addc(&o, (char)(0xF0 | (cp >> 18)));
                                   buf_addc(&o, (char)(0x80 | ((cp >> 12) & 0x3F)));
                                   buf_addc(&o, (char)(0x80 | ((cp >> 6) & 0x3F)));
                                   buf_addc(&o, (char)(0x80 | (cp & 0x3F))); }
                            i = j; continue;
                        }
                    }
                    buf_addc(&o, b.data[i]); i++;
                }
                buf_free(&b); b = o;
            }
            t.kind = T_STRING;
            t.sval = b.data ? b.data : xstrdup("");
            t.slen = (int)b.len;
            t.end = p; push(L, t); continue;
        }
        /* quoted identifier */
        if (*p == '"') {
            Buf b; buf_init(&b);
            p++;
            while (*p) {
                if (*p == '"') { if (p[1] == '"') { buf_addc(&b, '"'); p += 2; continue; } p++; break; }
                buf_addc(&b, *p); p++;
            }
            t.kind = T_IDENT; t.sval = b.data ? b.data : xstrdup(""); t.quoted = true;
            t.end = p; push(L, t); continue;
        }
        /* number */
        if (isdigit((unsigned char)*p) ||
            (*p == '.' && isdigit((unsigned char)p[1]))) {
            const char *q = p;
            bool isfloat = false;
            if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X') && isxdigit((unsigned char)q[2])) {
                q += 2; while (isxdigit((unsigned char)*q) || *q=='_') q++;
                t.kind = T_NUMBER; t.sval = xstrndup(p, (size_t)(q - p)); t.isint = true;
                p = q; t.end = p; push(L, t); continue;
            }
            while (isdigit((unsigned char)*q)) q++;
            if (*q == '.' && q[1] != '.') { isfloat = true; q++; while (isdigit((unsigned char)*q)) q++; }
            if ((*q == 'e' || *q == 'E') &&
                (isdigit((unsigned char)q[1]) ||
                 ((q[1] == '+' || q[1] == '-') && isdigit((unsigned char)q[2])))) {
                isfloat = true; q += 2; while (isdigit((unsigned char)*q)) q++;
            }
            t.kind = T_NUMBER; t.sval = xstrndup(p, (size_t)(q - p)); t.isint = !isfloat;
            p = q; t.end = p; push(L, t); continue;
        }
        /* identifier or keyword */
        if (ident_start(*p)) {
            const char *q = p;
            while (ident_cont(*q)) q++;
            char *raw = xstrndup(p, (size_t)(q - p));
            char *lo = lower_ident(raw);
            free(raw);
            t.kind = T_IDENT; t.sval = lo; t.quoted = false;
            t.iskw = kw_exists(lo);
            p = q; t.end = p; push(L, t); continue;
        }
        /* operators */
        if (*p == '.') { t.kind = T_DOT; p++; t.end = p; push(L, t); continue; }
        if (*p == ':' && p[1] == ':') { t.kind = T_CAST; p += 2; t.end = p; push(L, t); continue; }
        if (*p == ':' && p[1] == '=') { t.kind = T_OP; t.sval = xstrdup(":="); p += 2; t.end = p; push(L, t); continue; }
        if (*p == ':') { t.kind = T_COLON; p++; t.end = p; push(L, t); continue; }
        if (opchar(*p)) {
            const char *q = p;
            while (opchar(*q)) q++;
            /* PostgreSQL trims trailing + or - unless the name contains a
             * character that makes it non-standard. */
            while (q - p > 1 && (q[-1] == '+' || q[-1] == '-')) {
                bool special = false;
                for (const char *r = p; r < q - 1; r++)
                    if (strchr("~!@#%^&|`?", *r)) { special = true; break; }
                if (special) break;
                q--;
            }
            t.kind = T_OP; t.sval = xstrndup(p, (size_t)(q - p));
            p = q; t.end = p; push(L, t); continue;
        }
        lexfail(L, p, "syntax error at or near \"%c\"", *p);
    }
    Tok e; memset(&e, 0, sizeof e);
    e.kind = T_EOF; e.pos = (int)(p - L->src) + 1; e.start = e.end = p;
    push(L, e);
}
