/* xmltable.c - a small XML reader and the XMLTABLE table function.
 *
 * The XML subset is the one XMLTABLE needs: elements, attributes, text and
 * comments, with the usual five entity references.  The path language is the
 * subset of XPath the standard requires for a row and column expression:
 * an absolute or relative sequence of element names, "." for the context
 * node, and "text()" for its text.
 */
#include "engine.h"

struct XmlNode {
    char  *name;                  /* NULL for a text node */
    char  *text;                  /* text content of a text node */
    XmlNode **kids; int nkids;
    XmlNode *parent;
};

typedef struct {
    const char *p;
    const char *src;
    char *err;                    /* DETAIL text of a parse failure */
} XP;

static void xp_fail(XP *x, const char *what)
{
    if (x->err) return;
    Buf b; buf_init(&b);
    buf_printf(&b, "line 1: %s\n%s\n^", what, x->src);
    x->err = buf_steal(&b);
}

static XmlNode *xn_new(const char *name, XmlNode *parent)
{
    XmlNode *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->name = name ? xstrdup(name) : NULL;
    n->parent = parent;
    if (parent) {
        parent->kids = xrealloc(parent->kids, (size_t)(parent->nkids + 1) * sizeof(XmlNode *));
        parent->kids[parent->nkids++] = n;
    }
    return n;
}

static void xp_skip_space(XP *x) { while (*x->p && isspace((unsigned char)*x->p)) x->p++; }

static void xp_entity(Buf *b, const char **p)
{
    const char *q = *p + 1;
    const char *e = strchr(q, ';');
    if (!e || e - q > 10) { buf_addc(b, '&'); (*p)++; return; }
    size_t n = (size_t)(e - q);
    if (n == 2 && !strncmp(q, "lt", 2)) buf_addc(b, '<');
    else if (n == 2 && !strncmp(q, "gt", 2)) buf_addc(b, '>');
    else if (n == 3 && !strncmp(q, "amp", 3)) buf_addc(b, '&');
    else if (n == 4 && !strncmp(q, "quot", 4)) buf_addc(b, '"');
    else if (n == 4 && !strncmp(q, "apos", 4)) buf_addc(b, '\'');
    else if (*q == '#') {
        int cp = (q[1] == 'x' || q[1] == 'X') ? (int)strtol(q + 2, NULL, 16)
                                              : (int)strtol(q + 1, NULL, 10);
        if (cp < 0x80) buf_addc(b, (char)cp);
        else if (cp < 0x800) { buf_addc(b, (char)(0xC0 | (cp >> 6)));
                               buf_addc(b, (char)(0x80 | (cp & 0x3F))); }
        else { buf_addc(b, (char)(0xE0 | (cp >> 12)));
               buf_addc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
               buf_addc(b, (char)(0x80 | (cp & 0x3F))); }
    } else buf_add(b, q - 1, n + 2);
    *p = e + 1;
}

static bool xp_name_char(char c) { return isalnum((unsigned char)c) || c == '_' || c == '-' ||
                                          c == '.' || c == ':'; }

/* Parse one element and everything under it. */
static void xp_element(XP *x, XmlNode *parent)
{
    if (*x->p != '<') { xp_fail(x, "Start tag expected, '<' not found"); return; }
    x->p++;
    if (*x->p == '/') { xp_fail(x, "Extra content at the end of the document"); return; }
    const char *ns = x->p;
    while (xp_name_char(*x->p)) x->p++;
    if (x->p == ns) { xp_fail(x, "Start tag expected, '<' not found"); return; }
    char *name = xstrndup(ns, (size_t)(x->p - ns));
    XmlNode *e = xn_new(name, parent);
    free(name);
    /* attributes are accepted and discarded */
    for (;;) {
        xp_skip_space(x);
        if (*x->p == '/' && x->p[1] == '>') { x->p += 2; return; }
        if (*x->p == '>') { x->p++; break; }
        if (!*x->p) { xp_fail(x, "Premature end of data in tag"); return; }
        while (*x->p && *x->p != '=' && *x->p != '>' && !isspace((unsigned char)*x->p)) x->p++;
        xp_skip_space(x);
        if (*x->p == '=') {
            x->p++;
            xp_skip_space(x);
            char q = *x->p;
            if (q == '"' || q == '\'') { x->p++; while (*x->p && *x->p != q) x->p++;
                                         if (*x->p) x->p++; }
        }
    }
    /* content */
    for (;;) {
        if (!*x->p) { xp_fail(x, "Premature end of data in tag"); return; }
        if (x->p[0] == '<' && x->p[1] == '/') {
            x->p += 2;
            while (xp_name_char(*x->p)) x->p++;
            if (*x->p == '>') x->p++;
            return;
        }
        if (x->p[0] == '<' && x->p[1] == '!') {          /* comment or CDATA */
            const char *e2 = strstr(x->p, "-->");
            if (!strncmp(x->p, "<![CDATA[", 9)) {
                const char *c = strstr(x->p + 9, "]]>");
                if (!c) { xp_fail(x, "Premature end of data in tag"); return; }
                XmlNode *t = xn_new(NULL, e);
                t->text = xstrndup(x->p + 9, (size_t)(c - (x->p + 9)));
                x->p = c + 3;
                continue;
            }
            if (!e2) { xp_fail(x, "Premature end of data in tag"); return; }
            x->p = e2 + 3;
            continue;
        }
        if (x->p[0] == '<' && x->p[1] == '?') {
            const char *e2 = strstr(x->p, "?>");
            if (!e2) { xp_fail(x, "Premature end of data in tag"); return; }
            x->p = e2 + 2;
            continue;
        }
        if (*x->p == '<') { xp_element(x, e); if (x->err) return; continue; }
        Buf b; buf_init(&b);
        while (*x->p && *x->p != '<') {
            if (*x->p == '&') xp_entity(&b, &x->p);
            else buf_addc(&b, *x->p++);
        }
        XmlNode *t = xn_new(NULL, e);
        t->text = buf_steal(&b);
    }
}

XmlNode *xml_parse_doc(const char *s, char **err)
{
    XP x; memset(&x, 0, sizeof x);
    x.p = s; x.src = s;
    XmlNode *root = xn_new(NULL, NULL);       /* the document node */
    root->name = xstrdup("#document");
    xp_skip_space(&x);
    while (x.p[0] == '<' && (x.p[1] == '?' || x.p[1] == '!')) {
        const char *e2 = (x.p[1] == '?') ? strstr(x.p, "?>") : strchr(x.p, '>');
        if (!e2) break;
        x.p = e2 + (x.p[1] == '?' ? 2 : 1);
        xp_skip_space(&x);
    }
    xp_element(&x, root);
    if (x.err) { *err = x.err; return NULL; }
    *err = NULL;
    return root;
}

/* All descendant text of a node, concatenated. */
static void xn_text(XmlNode *n, Buf *b)
{
    if (!n->name && n->text) { buf_adds(b, n->text); return; }
    for (int i = 0; i < n->nkids; i++) xn_text(n->kids[i], b);
}

char *xml_node_text(XmlNode *n)
{
    Buf b; buf_init(&b);
    xn_text(n, &b);
    return buf_steal(&b);
}

/* Element children of n whose name matches. */
static void xn_children(XmlNode *n, const char *name, XmlNode ***out, int *nout)
{
    for (int i = 0; i < n->nkids; i++) {
        XmlNode *k = n->kids[i];
        if (!k->name) continue;
        if (strcmp(k->name, name)) continue;
        *out = xrealloc(*out, (size_t)(*nout + 1) * sizeof(XmlNode *));
        (*out)[(*nout)++] = k;
    }
}

/* Evaluate the supported XPath subset.  Returns false for a malformed path. */
bool xml_path(XmlNode *root, XmlNode *ctx, const char *path,
              XmlNode ***out, int *nout)
{
    *out = NULL; *nout = 0;
    if (!path || !*path) return false;
    XmlNode **cur = NULL; int ncur = 0;
    bool absolute = (*path == '/');
    const char *p = path;
    if (absolute) {
        p++;
        cur = xmalloc(sizeof(XmlNode *)); cur[0] = root; ncur = 1;
    } else {
        cur = xmalloc(sizeof(XmlNode *)); cur[0] = ctx; ncur = 1;
    }
    while (*p) {
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n == 0) { free(cur); return false; }
        char *step = xstrndup(p, n);
        XmlNode **next = NULL; int nnext = 0;
        if (!strcmp(step, ".")) {
            next = xmalloc((size_t)(ncur ? ncur : 1) * sizeof(XmlNode *));
            for (int i = 0; i < ncur; i++) next[nnext++] = cur[i];
        } else if (!strcmp(step, "text()")) {
            next = xmalloc((size_t)(ncur ? ncur : 1) * sizeof(XmlNode *));
            for (int i = 0; i < ncur; i++) next[nnext++] = cur[i];
        } else {
            for (int i = 0; i < ncur; i++) xn_children(cur[i], step, &next, &nnext);
        }
        free(step);
        free(cur);
        cur = next; ncur = nnext;
        if (!e) break;
        p = e + 1;
        if (!*p) { free(cur); return false; }
    }
    *out = cur; *nout = ncur;
    return true;
}
