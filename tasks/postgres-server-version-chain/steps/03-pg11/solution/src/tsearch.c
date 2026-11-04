/* tsearch.c - the text-search surface this server implements: the `tsvector`
 * and `tsquery` types, the `simple` and `english` configurations, and the
 * functions and operator the scored set uses.
 *
 * Both types are carried as their canonical text, which is what PostgreSQL
 * prints and what every case compares, so building and matching work on that
 * spelling directly.
 *
 * The `english` configuration is PostgreSQL's: its stop-word list, then the
 * Snowball English stemmer (Porter2).  `simple` lower-cases and stops nothing.
 */
#include "engine.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- stop words */
/* PostgreSQL's english.stop, verbatim and in its own order. */
static const char *const EN_STOP[] = {
    "i","me","my","myself","we","our","ours","ourselves","you","your","yours",
    "yourself","yourselves","he","him","his","himself","she","her","hers",
    "herself","it","its","itself","they","them","their","theirs","themselves",
    "what","which","who","whom","this","that","these","those","am","is","are",
    "was","were","be","been","being","have","has","had","having","do","does",
    "did","doing","a","an","the","and","but","if","or","because","as","until",
    "while","of","at","by","for","with","about","against","between","into",
    "through","during","before","after","above","below","to","from","up","down",
    "in","out","on","off","over","under","again","further","then","once","here",
    "there","when","where","why","how","all","any","both","each","few","more",
    "most","other","some","such","no","nor","not","only","own","same","so",
    "than","too","very","s","t","can","will","just","don","should","now", NULL
};

static bool en_is_stop(const char *w)
{
    for (int i = 0; EN_STOP[i]; i++)
        if (!strcmp(EN_STOP[i], w)) return true;
    return false;
}

/* --------------------------------------------------------- Porter2 stemmer */
static bool is_vowel(char c)
{
    return c=='a'||c=='e'||c=='i'||c=='o'||c=='u'||c=='y';
}
static bool ends(const char *w, size_t n, const char *suf)
{
    size_t m = strlen(suf);
    return n >= m && !memcmp(w + n - m, suf, m);
}
static bool is_double(const char *w, size_t n)
{
    static const char *d[] = {"bb","dd","ff","gg","mm","nn","pp","rr","tt",NULL};
    for (int i = 0; d[i]; i++) if (ends(w, n, d[i])) return true;
    return false;
}
/* a short syllable: a vowel followed by a non-vowel other than w, x or Y and
 * preceded by a non-vowel, or a vowel at the beginning followed by a non-vowel */
static bool short_syllable_at(const char *w, size_t n, size_t i)
{
    if (i + 1 >= n) return false;
    if (i == 0) return is_vowel(w[0]) && !is_vowel(w[1]);
    if (i + 2 > n) return false;
    char a = w[i-1], b = w[i], c = w[i+1];
    return !is_vowel(a) && is_vowel(b) && !is_vowel(c) && c!='w' && c!='x' && c!='Y';
}
static bool is_short_word(const char *w, size_t n, size_t r1)
{
    return r1 >= n && n >= 2 && short_syllable_at(w, n, n - 2);
}
static size_t region_after(const char *w, size_t n, size_t from)
{
    size_t i = from;
    while (i < n && !is_vowel(w[i])) i++;
    while (i < n && is_vowel(w[i])) i++;
    return i < n ? i + 1 : n;
}
static void replace_suffix(char *w, size_t *n, const char *suf, const char *rep)
{
    size_t m = strlen(suf), r = strlen(rep);
    memcpy(w + *n - m, rep, r);
    *n = *n - m + r;
    w[*n] = 0;
}

struct pair { const char *suf, *rep; };

static bool try_replace(char *w, size_t *n, size_t region, const struct pair *tab)
{
    for (int i = 0; tab[i].suf; i++) {
        size_t m = strlen(tab[i].suf);
        if (!ends(w, *n, tab[i].suf)) continue;
        if (*n - m < region) return true;      /* matched but out of region */
        replace_suffix(w, n, tab[i].suf, tab[i].rep);
        return true;
    }
    return false;
}

static char *porter2(const char *in)
{
    static const struct { const char *w, *s; } EXC[] = {
        {"skis","ski"},{"skies","sky"},{"dying","die"},{"lying","lie"},
        {"tying","tie"},{"idly","idl"},{"gently","gentl"},{"ugly","ugli"},
        {"early","earli"},{"only","onli"},{"singly","singl"},
        {"sky","sky"},{"news","news"},{"howe","howe"},{"atlas","atlas"},
        {"cosmos","cosmos"},{"bias","bias"},{"andes","andes"},
        {"inning","inning"},{"outing","outing"},{"canning","canning"},
        {"herring","herring"},{"earring","earring"},{"proceed","proceed"},
        {"exceed","exceed"},{"succeed","succeed"}, {NULL,NULL}
    };
    size_t n = strlen(in);
    for (int i = 0; EXC[i].w; i++)
        if (!strcmp(in, EXC[i].w)) return xstrdup(EXC[i].s);
    if (n <= 2) return xstrdup(in);

    char *w = xmalloc(n + 8);
    memcpy(w, in, n + 1);

    if (w[0] == '\'') { memmove(w, w + 1, n); n--; }
    if (w[0] == 'y') w[0] = 'Y';
    for (size_t i = 1; i < n; i++)
        if (w[i] == 'y' && is_vowel(w[i-1])) w[i] = 'Y';

    size_t r1;
    if (!strncmp(w, "gener", 5) || !strncmp(w, "arsen", 5)) r1 = 5;
    else if (!strncmp(w, "commun", 6)) r1 = 6;
    else r1 = region_after(w, n, 0);
    size_t r2 = region_after(w, n, r1);

    /* step 0 */
    if (ends(w, n, "'s'")) n -= 3;
    else if (ends(w, n, "'s")) n -= 2;
    else if (ends(w, n, "'")) n -= 1;
    w[n] = 0;

    /* step 1a */
    if (ends(w, n, "sses")) replace_suffix(w, &n, "sses", "ss");
    else if (ends(w, n, "ied") || ends(w, n, "ies"))
        replace_suffix(w, &n, w[n-3]=='i'?"ied":"ies", n > 4 ? "i" : "ie");
    else if (ends(w, n, "us") || ends(w, n, "ss")) { /* nothing */ }
    else if (ends(w, n, "s")) {
        bool v = false;
        for (size_t i = 0; i + 2 < n; i++) if (is_vowel(w[i])) { v = true; break; }
        if (v) { n--; w[n] = 0; }
    }

    /* step 1b */
    bool step1b_done = false;
    if (ends(w, n, "eedly") || ends(w, n, "eed")) {
        const char *suf = ends(w, n, "eedly") ? "eedly" : "eed";
        if (n - strlen(suf) >= r1) replace_suffix(w, &n, suf, "ee");
    } else {
        static const char *const s1b[] = {"ingly","edly","ing","ed",NULL};
        for (int i = 0; s1b[i]; i++) {
            if (!ends(w, n, s1b[i])) continue;
            size_t m = strlen(s1b[i]);
            bool v = false;
            for (size_t k = 0; k + m < n; k++) if (is_vowel(w[k])) { v = true; break; }
            if (!v) break;
            n -= m; w[n] = 0; step1b_done = true;
            break;
        }
        if (step1b_done) {
            if (ends(w, n, "at") || ends(w, n, "bl") || ends(w, n, "iz")) {
                w[n++] = 'e'; w[n] = 0;
            } else if (is_double(w, n)) {
                n--; w[n] = 0;
            } else if (is_short_word(w, n, r1)) {
                w[n++] = 'e'; w[n] = 0;
            }
        }
    }

    /* step 1c */
    if (n > 2 && (w[n-1] == 'y' || w[n-1] == 'Y') && !is_vowel(w[n-2]))
        w[n-1] = 'i';

    r1 = r1 > n ? n : r1;
    r2 = r2 > n ? n : r2;

    /* step 2 */
    static const struct pair S2[] = {
        {"ization","ize"},{"ational","ate"},{"fulness","ful"},{"ousness","ous"},
        {"iveness","ive"},{"tional","tion"},{"biliti","ble"},{"lessli","less"},
        {"entli","ent"},{"ation","ate"},{"alism","al"},{"aliti","al"},
        {"ousli","ous"},{"iviti","ive"},{"fulli","ful"},{"enci","ence"},
        {"anci","ance"},{"abli","able"},{"izer","ize"},{"ator","ate"},
        {"alli","al"},{"bli","ble"},{"ogi","og"},{"li",""},{NULL,NULL}
    };
    for (int i = 0; S2[i].suf; i++) {
        if (!ends(w, n, S2[i].suf)) continue;
        size_t m = strlen(S2[i].suf);
        if (n - m < r1) break;
        if (!strcmp(S2[i].suf, "ogi")) { if (n >= 4 && w[n-4] == 'l') replace_suffix(w, &n, "ogi", "og"); break; }
        if (!strcmp(S2[i].suf, "li")) {
            if (n >= 3 && strchr("cdeghkmnrt", w[n-3])) { n -= 2; w[n] = 0; }
            break;
        }
        replace_suffix(w, &n, S2[i].suf, S2[i].rep);
        break;
    }

    /* step 3 */
    static const struct pair S3[] = {
        {"ational","ate"},{"tional","tion"},{"alize","al"},{"icate","ic"},
        {"iciti","ic"},{"ical","ic"},{"ful",""},{"ness",""},{NULL,NULL}
    };
    for (int i = 0; S3[i].suf; i++) {
        if (!ends(w, n, S3[i].suf)) continue;
        size_t m = strlen(S3[i].suf);
        if (n - m < r1) break;
        replace_suffix(w, &n, S3[i].suf, S3[i].rep);
        break;
    }
    if (ends(w, n, "ative") && n - 5 >= r2) { n -= 5; w[n] = 0; }

    /* step 4 */
    static const char *const S4[] = {
        "ement","ance","ence","able","ible","ment","ant","ent","ism","ate",
        "iti","ous","ive","ize","al","er","ic",NULL
    };
    for (int i = 0; S4[i]; i++) {
        if (!ends(w, n, S4[i])) continue;
        size_t m = strlen(S4[i]);
        if (n - m < r2) break;
        n -= m; w[n] = 0;
        break;
    }
    if (ends(w, n, "sion") || ends(w, n, "tion")) {
        if (n - 3 >= r2) { n -= 3; w[n] = 0; }
    }

    /* step 5 */
    if (ends(w, n, "e")) {
        if (n - 1 >= r2 || (n - 1 >= r1 && !short_syllable_at(w, n - 1, n - 3)))
            { n--; w[n] = 0; }
    } else if (ends(w, n, "l") && n >= 2 && w[n-2] == 'l' && n - 1 >= r2) {
        n--; w[n] = 0;
    }

    for (size_t i = 0; i < n; i++) if (w[i] == 'Y') w[i] = 'y';
    return w;
}

/* ------------------------------------------------------------- the configs */
bool ts_config_known(const char *cfg)
{
    return cfg && (!strcmp(cfg, "simple") || !strcmp(cfg, "english") ||
                   !strcmp(cfg, "pg_catalog.simple") ||
                   !strcmp(cfg, "pg_catalog.english"));
}

static bool cfg_is_english(const char *cfg)
{
    return cfg && (!strcmp(cfg, "english") || !strcmp(cfg, "pg_catalog.english"));
}

/* lexeme for one token under a configuration; NULL when the token is stopped */
static char *ts_lexeme(const char *cfg, const char *tok)
{
    size_t n = strlen(tok);
    char *low = xmalloc(n + 1);
    for (size_t i = 0; i < n; i++) low[i] = (char)tolower((unsigned char)tok[i]);
    low[n] = 0;
    if (!cfg_is_english(cfg)) return low;
    if (en_is_stop(low)) { free(low); return NULL; }
    char *st = porter2(low);
    free(low);
    return st;
}

/* --------------------------------------------------------------- tokenizer */
/* PostgreSQL's default parser, as far as this server needs it: runs of letters
 * are words, runs of digits are numbers, everything else separates.  Every
 * token advances the position counter, stop words included, which is why
 * `to_tsvector('english','the quick brown fox')` numbers `quick` 2. */
typedef struct { char *tok; int pos; } TsTok;

static TsTok *ts_tokens(const char *text, int *ntok, int start_pos)
{
    TsTok *out = NULL; int n = 0, cap = 0;
    int pos = start_pos;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        if (!isalnum(*p) && *p < 0x80) { p++; continue; }
        const unsigned char *st = p;
        bool alpha = (isalpha(*p) || *p >= 0x80);
        while (*p && ((alpha && (isalpha(*p) || *p >= 0x80)) ||
                      (!alpha && isdigit(*p)))) p++;
        if (n == cap) { cap = cap ? cap * 2 : 16; out = xrealloc(out, (size_t)cap * sizeof *out); }
        out[n].tok = xstrndup((const char *)st, (size_t)(p - st));
        out[n].pos = ++pos;
        n++;
    }
    *ntok = n;
    return out;
}

/* ------------------------------------------------------------- tsvector */
typedef struct { char *lex; int *pos; int npos; } TsEntry;

static int lexcmp(const void *a, const void *b)
{
    const TsEntry *x = a, *y = b;
    return strcmp(x->lex, y->lex);
}

/* Build the canonical tsvector text for a sequence of (already tokenised)
 * strings.  Positions continue across the pieces, which is how
 * jsonb_to_tsvector numbers a document. */
static char *ts_vector_text(const char *cfg, char **parts, int nparts)
{
    TsEntry *ent = NULL; int nent = 0, cap = 0;
    int pos = 0;
    for (int k = 0; k < nparts; k++) {
        int nt = 0;
        TsTok *tk = ts_tokens(parts[k], &nt, pos);
        /* PostgreSQL leaves one position between two values of a document, so
         * `{"a":"fox","b":42}` gives 'fox':1 and '42':3. */
        pos = (nt ? tk[nt-1].pos : pos) + 1;
        for (int i = 0; i < nt; i++) {
            char *lx = ts_lexeme(cfg, tk[i].tok);
            free(tk[i].tok);
            if (!lx) continue;
            int at = -1;
            for (int j = 0; j < nent; j++) if (!strcmp(ent[j].lex, lx)) { at = j; break; }
            if (at < 0) {
                if (nent == cap) { cap = cap ? cap * 2 : 16; ent = xrealloc(ent, (size_t)cap * sizeof *ent); }
                ent[nent].lex = lx; ent[nent].pos = NULL; ent[nent].npos = 0;
                at = nent++;
            } else free(lx);
            ent[at].pos = xrealloc(ent[at].pos, (size_t)(ent[at].npos + 1) * sizeof(int));
            ent[at].pos[ent[at].npos++] = tk[i].pos;
        }
        free(tk);
    }
    qsort(ent, (size_t)nent, sizeof *ent, lexcmp);
    Buf b; buf_init(&b);
    for (int i = 0; i < nent; i++) {
        if (i) buf_addc(&b, ' ');
        buf_addc(&b, '\'');
        for (char *q = ent[i].lex; *q; q++) {
            if (*q == '\'' || *q == '\\') buf_addc(&b, '\\');
            buf_addc(&b, *q);
        }
        buf_addc(&b, '\'');
        for (int j = 0; j < ent[i].npos; j++)
            buf_printf(&b, "%s%d", j ? "," : ":", ent[i].pos[j]);
        free(ent[i].lex); free(ent[i].pos);
    }
    free(ent);
    return buf_steal(&b);
}

char *ts_to_tsvector(const char *cfg, const char *text)
{
    char *one = (char *)text;
    return ts_vector_text(cfg, &one, 1);
}

char *ts_to_tsvector_parts(const char *cfg, char **parts, int nparts)
{
    return ts_vector_text(cfg, parts, nparts);
}

/* --------------------------------------------------------------- tsquery */
/* websearch_to_tsquery's grammar: bare words are ANDed, `or` between two words
 * makes an OR, a leading `-` negates the word that follows, and a quoted run
 * becomes a phrase joined by <->.  Anything else is punctuation and is
 * dropped.  Stop words disappear, and a query left with nothing raises the
 * notice PostgreSQL raises. */
typedef struct { char *text; bool is_or_pending; } QBuf;

static void q_append(Buf *b, const char *piece, const char *op)
{
    if (b->len) buf_printf(b, " %s ", op);
    buf_adds(b, piece);
}

static char *q_lexstr(const char *lex, bool neg)
{
    Buf b; buf_init(&b);
    if (neg) buf_addc(&b, '!');
    buf_addc(&b, '\'');
    for (const char *q = lex; *q; q++) {
        if (*q == '\'' || *q == '\\') buf_addc(&b, '\\');
        buf_addc(&b, *q);
    }
    buf_addc(&b, '\'');
    return buf_steal(&b);
}

char *ts_websearch_to_tsquery(const char *cfg, const char *q, int *empty_reason)
{
    /* empty_reason: 0 none, 1 no lexemes at all, 2 only stop words */
    *empty_reason = 0;
    Buf out; buf_init(&out);
    const char *p = q;
    bool pending_or = false;
    bool saw_token = false, saw_lexeme = false;
    while (*p) {
        while (*p && !isalnum((unsigned char)*p) && *p != '"' && *p != '-') p++;
        if (!*p) break;
        bool neg = false;
        if (*p == '-') { neg = true; p++; if (!*p) break; }
        if (*p == '"') {                      /* a phrase */
            p++;
            const char *st = p;
            while (*p && *p != '"') p++;
            char *phrase = xstrndup(st, (size_t)(p - st));
            if (*p == '"') p++;
            int nt = 0;
            TsTok *tk = ts_tokens(phrase, &nt, 0);
            free(phrase);
            Buf ph; buf_init(&ph);
            for (int i = 0; i < nt; i++) {
                saw_token = true;
                char *lx = ts_lexeme(cfg, tk[i].tok);
                free(tk[i].tok);
                if (!lx) continue;
                saw_lexeme = true;
                char *one = q_lexstr(lx, false);
                free(lx);
                if (ph.len) buf_adds(&ph, " <-> ");
                buf_adds(&ph, one);
                free(one);
            }
            free(tk);
            if (ph.len) {
                char *piece = buf_steal(&ph);
                q_append(&out, piece, pending_or ? "|" : "&");
                free(piece);
                pending_or = false;
            } else buf_free(&ph);
            continue;
        }
        const char *st = p;
        while (*p && (isalnum((unsigned char)*p) || (unsigned char)*p >= 0x80)) p++;
        char *word = xstrndup(st, (size_t)(p - st));
        saw_token = true;
        if (!neg && (!strcasecmp(word, "or"))) { pending_or = true; free(word); continue; }
        char *lx = ts_lexeme(cfg, word);
        free(word);
        if (!lx) continue;                     /* a stop word */
        saw_lexeme = true;
        char *piece = q_lexstr(lx, neg);
        free(lx);
        q_append(&out, piece, pending_or ? "|" : "&");
        free(piece);
        pending_or = false;
    }
    if (!out.len) {
        *empty_reason = saw_token && !saw_lexeme ? 2 : 1;
        buf_free(&out);
        return xstrdup("");
    }
    return buf_steal(&out);
}

/* ------------------------------------------------------- matching @@ */
/* A parsed tsvector: lexemes with their positions. */
typedef struct { char *lex; int *pos; int npos; } VecEnt;
typedef struct { VecEnt *e; int n; } Vec;

static char *scan_quoted(const char **pp)
{
    const char *p = *pp;
    if (*p != '\'') return NULL;
    p++;
    Buf b; buf_init(&b);
    while (*p) {
        if (*p == '\\' && p[1]) { buf_addc(&b, p[1]); p += 2; continue; }
        if (*p == '\'') { p++; break; }
        buf_addc(&b, *p++);
    }
    *pp = p;
    return buf_steal(&b);
}

static Vec vec_parse(const char *text)
{
    Vec v; v.e = NULL; v.n = 0;
    const char *p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char *lex = scan_quoted(&p);
        if (!lex) break;
        v.e = xrealloc(v.e, (size_t)(v.n + 1) * sizeof(VecEnt));
        v.e[v.n].lex = lex; v.e[v.n].pos = NULL; v.e[v.n].npos = 0;
        if (*p == ':') {
            p++;
            for (;;) {
                int val = 0; bool any = false;
                while (isdigit((unsigned char)*p)) { val = val * 10 + (*p - '0'); p++; any = true; }
                while (*p && strchr("ABCD", *p)) p++;      /* weights */
                if (any) {
                    v.e[v.n].pos = xrealloc(v.e[v.n].pos,
                                            (size_t)(v.e[v.n].npos + 1) * sizeof(int));
                    v.e[v.n].pos[v.e[v.n].npos++] = val;
                }
                if (*p == ',') { p++; continue; }
                break;
            }
        }
        v.n++;
    }
    return v;
}

static void vec_free(Vec *v)
{
    for (int i = 0; i < v->n; i++) { free(v->e[i].lex); free(v->e[i].pos); }
    free(v->e);
}

static const VecEnt *vec_find(const Vec *v, const char *lex)
{
    for (int i = 0; i < v->n; i++) if (!strcmp(v->e[i].lex, lex)) return &v->e[i];
    return NULL;
}

/* The query is evaluated to a set of positions so that <-> can be checked.
 * A NULL set means "no match"; an empty-but-present set means "matches with no
 * position information", which is what a lexeme with no positions gives. */
typedef struct { bool ok; int *pos; int npos; } QRes;

static QRes qres_none(void) { QRes r = {false, NULL, 0}; return r; }

static QRes q_or(QRes a, QRes b)
{
    if (!a.ok && !b.ok) { free(a.pos); free(b.pos); return qres_none(); }
    QRes r; r.ok = true; r.npos = 0; r.pos = NULL;
    for (int i = 0; i < a.npos; i++) {
        r.pos = xrealloc(r.pos, (size_t)(r.npos + 1) * sizeof(int)); r.pos[r.npos++] = a.pos[i];
    }
    for (int i = 0; i < b.npos; i++) {
        r.pos = xrealloc(r.pos, (size_t)(r.npos + 1) * sizeof(int)); r.pos[r.npos++] = b.pos[i];
    }
    free(a.pos); free(b.pos);
    return r;
}

static QRes q_and(QRes a, QRes b)
{
    if (!a.ok || !b.ok) { free(a.pos); free(b.pos); return qres_none(); }
    QRes r; r.ok = true; r.pos = b.pos; r.npos = b.npos;
    free(a.pos);
    return r;
}

/* a <-> b : a position of b must be exactly one past a position of a */
static QRes q_phrase(QRes a, QRes b, int dist)
{
    QRes r = qres_none();
    if (!a.ok || !b.ok) { free(a.pos); free(b.pos); return r; }
    for (int i = 0; i < a.npos; i++)
        for (int j = 0; j < b.npos; j++)
            if (b.pos[j] - a.pos[i] == dist) {
                r.ok = true;
                r.pos = xrealloc(r.pos, (size_t)(r.npos + 1) * sizeof(int));
                r.pos[r.npos++] = b.pos[j];
            }
    free(a.pos); free(b.pos);
    return r;
}

typedef struct { const char *p; const Vec *v; } QP;

static QRes q_expr(QP *q, int min_prec);

static QRes q_atom(QP *q)
{
    while (*q->p == ' ') q->p++;
    if (*q->p == '!') {
        q->p++;
        QRes a = q_atom(q);
        QRes r = qres_none();
        r.ok = !a.ok;
        free(a.pos);
        return r;
    }
    if (*q->p == '(') {
        q->p++;
        QRes r = q_expr(q, 0);
        while (*q->p == ' ') q->p++;
        if (*q->p == ')') q->p++;
        return r;
    }
    char *lex = scan_quoted(&q->p);
    if (!lex) return qres_none();
    const VecEnt *e = vec_find(q->v, lex);
    free(lex);
    QRes r = qres_none();
    if (!e) return r;
    r.ok = true;
    r.npos = e->npos;
    if (r.npos) {
        r.pos = xmalloc((size_t)r.npos * sizeof(int));
        memcpy(r.pos, e->pos, (size_t)r.npos * sizeof(int));
    }
    return r;
}

/* precedence: <-> (3) binds tighter than & (2), which binds tighter than | (1) */
static QRes q_expr(QP *q, int min_prec)
{
    QRes left = q_atom(q);
    for (;;) {
        while (*q->p == ' ') q->p++;
        int prec, dist = 1;
        const char *save = q->p;
        if (q->p[0] == '<' && q->p[1] == '-' && q->p[2] == '>') { prec = 3; q->p += 3; }
        else if (q->p[0] == '<' && isdigit((unsigned char)q->p[1])) {
            prec = 3; q->p++; dist = 0;
            while (isdigit((unsigned char)*q->p)) { dist = dist * 10 + (*q->p - '0'); q->p++; }
            if (*q->p == '>') q->p++;
        }
        else if (*q->p == '&') { prec = 2; q->p++; }
        else if (*q->p == '|') { prec = 1; q->p++; }
        else break;
        if (prec < min_prec) { q->p = save; break; }
        QRes right = q_expr(q, prec + 1);
        if (prec == 3) left = q_phrase(left, right, dist);
        else if (prec == 2) left = q_and(left, right);
        else left = q_or(left, right);
    }
    return left;
}

bool ts_match(const char *vector, const char *query)
{
    Vec v = vec_parse(vector);
    QP q; q.p = query; q.v = &v;
    QRes r = q_expr(&q, 0);
    bool ok = r.ok;
    free(r.pos);
    vec_free(&v);
    return ok;
}
