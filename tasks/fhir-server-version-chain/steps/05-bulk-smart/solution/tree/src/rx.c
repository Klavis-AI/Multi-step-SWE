#include "rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Node kinds: an alternation of sequences of repeated atoms. */
typedef enum { N_CHAR, N_CLASS, N_ANY, N_GROUP } atomkind;

typedef struct rxnode rxnode;
typedef struct rxalt rxalt;

typedef struct {
    unsigned char set[32];   /* bitmap over byte values */
    int           negate;
} rxclass;

struct rxnode {              /* one repeated atom */
    atomkind kind;
    unsigned char ch;
    rxclass  cls;
    rxalt   *group;
    int      min, max;       /* max < 0 means unbounded */
    rxnode  *next;
};

struct rxalt {               /* alternation: a list of branches */
    rxnode **branch;
    size_t   n, cap;
};

struct rx {
    arena *a;
    rxalt *top;
    int    ok;
};

typedef struct {
    const char *p;
    arena      *a;
    int         ok;
} rxp;

static rxalt *p_alt(rxp *P, char stop);

static void cls_add(rxclass *c, unsigned char b) { c->set[b >> 3] |= (unsigned char)(1u << (b & 7)); }
static int  cls_has(const rxclass *c, unsigned char b) { return (c->set[b >> 3] >> (b & 7)) & 1; }

static void cls_add_range(rxclass *c, unsigned char lo, unsigned char hi) {
    for (unsigned b = lo; b <= hi; b++) cls_add(c, (unsigned char)b);
}

static void cls_add_escape(rxclass *c, char e) {
    switch (e) {
        case 's': cls_add(c, ' '); cls_add(c, '\t'); cls_add(c, '\n'); cls_add(c, '\r'); cls_add(c, '\f'); cls_add(c, 0x0b); break;
        case 'S': for (unsigned b = 1; b < 256; b++)
                      if (b != ' ' && b != '\t' && b != '\n' && b != '\r' && b != '\f' && b != 0x0b) cls_add(c, (unsigned char)b);
                  break;
        case 'd': cls_add_range(c, '0', '9'); break;
        case 'D': for (unsigned b = 1; b < 256; b++) if (b < '0' || b > '9') cls_add(c, (unsigned char)b); break;
        case 'w': cls_add_range(c, 'a', 'z'); cls_add_range(c, 'A', 'Z'); cls_add_range(c, '0', '9'); cls_add(c, '_');
                  for (unsigned b = 0x80; b < 256; b++) cls_add(c, (unsigned char)b);
                  break;
        case 'W': for (unsigned b = 1; b < 128; b++)
                      if (!isalnum((int)b) && b != '_') cls_add(c, (unsigned char)b);
                  break;
        case 'n': cls_add(c, '\n'); break;
        case 'r': cls_add(c, '\r'); break;
        case 't': cls_add(c, '\t'); break;
        default:  cls_add(c, (unsigned char)e); break;
    }
}

static int esc_is_class(char e) { return strchr("sSdDwW", e) != NULL; }

static rxnode *p_atom(rxp *P) {
    rxnode *n = arena_calloc(P->a, sizeof(rxnode));
    n->min = n->max = 1;
    char c = *P->p;
    if (c == '(') {
        P->p++;
        if (P->p[0] == '?' && P->p[1] == ':') P->p += 2;
        n->kind = N_GROUP;
        n->group = p_alt(P, ')');
        if (*P->p == ')') P->p++;
        else P->ok = 0;
        return n;
    }
    if (c == '[') {
        P->p++;
        n->kind = N_CLASS;
        if (*P->p == '^') { n->cls.negate = 1; P->p++; }
        int first = 1;
        while (*P->p && (*P->p != ']' || first)) {
            first = 0;
            unsigned char lo;
            if (*P->p == '\\' && P->p[1]) {
                char e = P->p[1];
                P->p += 2;
                if (esc_is_class(e)) { cls_add_escape(&n->cls, e); continue; }
                lo = (unsigned char)(e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : e);
            } else {
                lo = (unsigned char)*P->p++;
            }
            if (*P->p == '-' && P->p[1] && P->p[1] != ']') {
                P->p++;
                unsigned char hi;
                if (*P->p == '\\' && P->p[1]) { hi = (unsigned char)P->p[1]; P->p += 2; }
                else hi = (unsigned char)*P->p++;
                cls_add_range(&n->cls, lo, hi);
            } else {
                cls_add(&n->cls, lo);
            }
        }
        if (*P->p == ']') P->p++;
        else P->ok = 0;
        return n;
    }
    if (c == '.') { P->p++; n->kind = N_ANY; return n; }
    if (c == '\\' && P->p[1]) {
        char e = P->p[1];
        P->p += 2;
        if (esc_is_class(e)) { n->kind = N_CLASS; cls_add_escape(&n->cls, e); return n; }
        n->kind = N_CHAR;
        n->ch = (unsigned char)(e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : e);
        return n;
    }
    P->p++;
    n->kind = N_CHAR;
    n->ch = (unsigned char)c;
    return n;
}

static void p_quant(rxp *P, rxnode *n) {
    char c = *P->p;
    if (c == '*') { n->min = 0; n->max = -1; P->p++; }
    else if (c == '+') { n->min = 1; n->max = -1; P->p++; }
    else if (c == '?') { n->min = 0; n->max = 1; P->p++; }
    else if (c == '{') {
        const char *q = P->p + 1;
        char *end = NULL;
        long lo = strtol(q, &end, 10);
        if (end == q) return;
        long hi = lo;
        if (*end == ',') {
            const char *r = end + 1;
            if (*r == '}') hi = -1;
            else { char *e2 = NULL; hi = strtol(r, &e2, 10); end = e2; }
            if (*end != '}') end = strchr(end, '}');
        }
        if (!end || *end != '}') return;
        n->min = (int)lo;
        n->max = (int)hi;
        P->p = end + 1;
    }
    if (*P->p == '?') P->p++;   /* lazy quantifier: treated as greedy */
}

static rxnode *p_seq(rxp *P, char stop) {
    rxnode *head = NULL, *tail = NULL;
    while (*P->p && *P->p != '|' && *P->p != stop) {
        rxnode *n = p_atom(P);
        p_quant(P, n);
        if (tail) tail->next = n; else head = n;
        tail = n;
        if (!P->ok) break;
    }
    return head;
}

static rxalt *p_alt(rxp *P, char stop) {
    rxalt *a = arena_calloc(P->a, sizeof(rxalt));
    for (;;) {
        rxnode *b = p_seq(P, stop);
        if (a->n == a->cap) {
            size_t cap = a->cap ? a->cap * 2 : 4;
            rxnode **nb = arena_alloc(P->a, cap * sizeof(rxnode *));
            if (a->branch) memcpy(nb, a->branch, a->n * sizeof(rxnode *));
            a->branch = nb; a->cap = cap;
        }
        a->branch[a->n++] = b;
        if (*P->p == '|') { P->p++; continue; }
        break;
    }
    return a;
}

rx *rx_compile(arena *a, const char *pattern) {
    rx *r = arena_calloc(a, sizeof(rx));
    r->a = a;
    rxp P = { pattern ? pattern : "", a, 1 };
    r->top = p_alt(&P, 0);
    r->ok = P.ok && *P.p == 0;
    return r;
}

/* Backtracking matcher.  `k` is the continuation: the node list to match after
   this one, plus the enclosing continuation. */
typedef struct kont {
    rxnode      *node;
    struct kont *up;
} kont;

static int m_seq(rxnode *n, const char *s, const char *end, kont *up, const char **out);
static int m_alt(rxalt *a, const char *s, const char *end, kont *up, const char **out);

static int m_kont(kont *k, const char *s, const char *end, const char **out) {
    if (!k) { *out = s; return 1; }
    return m_seq(k->node, s, end, k->up, out);
}

static int atom_match1(rxnode *n, const char *s, const char *end) {
    if (s >= end) return 0;
    unsigned char b = (unsigned char)*s;
    switch (n->kind) {
        case N_CHAR:  return b == n->ch;
        case N_ANY:   return 1;
        case N_CLASS: return n->cls.negate ? !cls_has(&n->cls, b) : cls_has(&n->cls, b);
        default:      return 0;
    }
}

/* Repetition is iterative: recursion depth stays proportional to the pattern's
   nesting, never to the length of the subject.  A greedy count is taken first
   and then walked back, which is what the FHIR/XSD patterns need. */
static int m_rep(rxnode *n, const char *s, const char *end, kont *up, const char **out) {
    if (n->kind != N_GROUP) {
        long cnt = 0;
        const char *p = s;
        while ((n->max < 0 || cnt < n->max) && atom_match1(n, p, end)) { p++; cnt++; }
        while (cnt >= n->min) {
            if (m_kont(up, s + cnt, end, out)) return 1;
            if (cnt == 0) break;
            cnt--;
        }
        return 0;
    }
    size_t cap = 16, cnt = 0;
    const char **pos = malloc(cap * sizeof(char *));
    if (!pos) return 0;
    pos[0] = s;
    while (n->max < 0 || (long)cnt < n->max) {
        const char *q = NULL;
        if (!m_alt(n->group, pos[cnt], end, NULL, &q)) break;
        if (q == pos[cnt]) break;                 /* no progress: stop */
        if (cnt + 1 >= cap) {
            cap *= 2;
            const char **np = realloc(pos, cap * sizeof(char *));
            if (!np) break;
            pos = np;
        }
        pos[++cnt] = q;
    }
    int ok = 0;
    for (long c2 = (long)cnt; c2 >= n->min; c2--) {
        if (m_kont(up, pos[c2], end, out)) { ok = 1; break; }
    }
    free(pos);
    return ok;
}

static int m_seq(rxnode *n, const char *s, const char *end, kont *up, const char **out) {
    if (!n) return m_kont(up, s, end, out);
    kont k = { n->next, up };
    return m_rep(n, s, end, n->next ? &k : up, out);
}

static int m_alt(rxalt *a, const char *s, const char *end, kont *up, const char **out) {
    if (!a) return m_kont(up, s, end, out);
    for (size_t i = 0; i < a->n; i++) {
        const char *p;
        if (m_seq(a->branch[i], s, end, up, &p)) { *out = p; return 1; }
    }
    return 0;
}

int rx_match(rx *r, const char *s) {
    if (!r || !r->ok) return 1;      /* an unusable pattern never rejects */
    if (!s) return 0;
    const char *end = s + strlen(s);
    const char *out = NULL;
    if (!m_alt(r->top, s, end, NULL, &out)) return 0;
    return out == end;
}
