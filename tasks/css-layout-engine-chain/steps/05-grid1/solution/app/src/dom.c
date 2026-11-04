/* dom.c -- HTML parser (a pragmatic subset of the WHATWG parsing algorithm).
 *
 * Tokenizer states implemented: data, tag open / end tag open, tag name,
 * attribute name / value (unquoted, single, double quoted), self-closing
 * start tag, markup declaration (comment, DOCTYPE, CDATA), RAWTEXT/RCDATA.
 *
 * Tree construction is a flattened version of the insertion modes that
 * static layout fixtures exercise: before head, in head, after head, in body.
 */

#include "dom.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- ascii bits */

static int h_is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

static int h_is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int h_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static char h_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int h_casecmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = h_lower(*a++), cb = h_lower(*b++);
        if (ca != cb)
            return (unsigned char)ca - (unsigned char)cb;
    }
    return (unsigned char)h_lower(*a) - (unsigned char)h_lower(*b);
}

static int tag_is(const dom_node *n, const char *t)
{
    return n->tag != NULL && strcmp(n->tag, t) == 0;
}

static int in_list(const char *tag, const char *const *list, size_t n)
{
    size_t i;
    if (!tag)
        return 0;
    for (i = 0; i < n; i++) {
        if (strcmp(tag, list[i]) == 0)
            return 1;
    }
    return 0;
}

#define NELEMS(x) (sizeof(x) / sizeof((x)[0]))

static const char *const void_elements[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr"
};

/* script/style are RAWTEXT (no character references); title/textarea are
 * RCDATA (character references are decoded).  Both consume to the matching
 * end tag without tokenizing markup. */
static const char *const rawtext_elements[] = { "script", "style" };
static const char *const rcdata_elements[]  = { "title", "textarea" };

static const char *const head_elements[] = {
    "base", "basefont", "bgsound", "link", "meta", "noscript",
    "script", "style", "template", "title"
};

static int is_void_element(const char *tag)
{
    return in_list(tag, void_elements, NELEMS(void_elements));
}

static int is_rawtext_element(const char *tag)
{
    return in_list(tag, rawtext_elements, NELEMS(rawtext_elements));
}

static int is_rcdata_element(const char *tag)
{
    return in_list(tag, rcdata_elements, NELEMS(rcdata_elements));
}

static int is_head_element(const char *tag)
{
    return in_list(tag, head_elements, NELEMS(head_elements));
}

/* ------------------------------------------------- character references */

/* A small named table.  Deliberately excludes non-standard names such as
 * "zwsp" -- "&zwsp;" stays literal text, while "&#8203;" decodes to U+200B. */
static const struct { const char *name; const char *utf8; } named_refs[] = {
    { "amp",    "&" },        { "lt",     "<" },
    { "gt",     ">" },        { "quot",   "\"" },
    { "apos",   "'" },        { "nbsp",   "\xC2\xA0" },
    { "ensp",   "\xE2\x80\x82" }, { "emsp",   "\xE2\x80\x83" },
    { "thinsp", "\xE2\x80\x89" }, { "shy",    "\xC2\xAD" },
    { "copy",   "\xC2\xA9" }, { "reg",    "\xC2\xAE" },
    { "trade",  "\xE2\x84\xA2" }, { "hellip", "\xE2\x80\xA6" },
    { "mdash",  "\xE2\x80\x94" }, { "ndash",  "\xE2\x80\x93" },
    { "lsquo",  "\xE2\x80\x98" }, { "rsquo",  "\xE2\x80\x99" },
    { "ldquo",  "\xE2\x80\x9C" }, { "rdquo",  "\xE2\x80\x9D" },
    { "laquo",  "\xC2\xAB" }, { "raquo",  "\xC2\xBB" },
    { "bull",   "\xE2\x80\xA2" }, { "middot", "\xC2\xB7" },
    { "dagger", "\xE2\x80\xA0" }, { "Dagger", "\xE2\x80\xA1" },
    { "times",  "\xC3\x97" }, { "divide", "\xC3\xB7" },
    { "deg",    "\xC2\xB0" }, { "plusmn", "\xC2\xB1" },
    { "minus",  "\xE2\x88\x92" }, { "ne",     "\xE2\x89\xA0" },
    { "le",     "\xE2\x89\xA4" }, { "ge",     "\xE2\x89\xA5" },
    { "infin",  "\xE2\x88\x9E" }, { "euro",   "\xE2\x82\xAC" },
    { "pound",  "\xC2\xA3" }, { "yen",    "\xC2\xA5" },
    { "cent",   "\xC2\xA2" }, { "sect",   "\xC2\xA7" },
    { "para",   "\xC2\xB6" }, { "micro",  "\xC2\xB5" },
    { "sup1",   "\xC2\xB9" }, { "sup2",   "\xC2\xB2" },
    { "sup3",   "\xC2\xB3" }, { "frac12", "\xC2\xBD" },
    { "frac14", "\xC2\xBC" }, { "frac34", "\xC2\xBE" },
    { "larr",   "\xE2\x86\x90" }, { "uarr",   "\xE2\x86\x91" },
    { "rarr",   "\xE2\x86\x92" }, { "darr",   "\xE2\x86\x93" },
    { "harr",   "\xE2\x86\x94" }, { "alpha",  "\xCE\xB1" },
    { "beta",   "\xCE\xB2" }, { "gamma",  "\xCE\xB3" },
    { "lambda", "\xCE\xBB" }, { "mu",     "\xCE\xBC" },
    { "pi",     "\xCF\x80" }, { "sigma",  "\xCF\x83" },
    { "Omega",  "\xCE\xA9" }, { "szlig",  "\xC3\x9F" },
    { "agrave", "\xC3\xA0" }, { "aacute", "\xC3\xA1" },
    { "auml",   "\xC3\xA4" }, { "Auml",   "\xC3\x84" },
    { "ccedil", "\xC3\xA7" }, { "eacute", "\xC3\xA9" },
    { "egrave", "\xC3\xA8" }, { "iacute", "\xC3\xAD" },
    { "ntilde", "\xC3\xB1" }, { "oacute", "\xC3\xB3" },
    { "ouml",   "\xC3\xB6" }, { "Ouml",   "\xC3\x96" },
    { "uacute", "\xC3\xBA" }, { "uuml",   "\xC3\xBC" },
    { "Uuml",   "\xC3\x9C" }
};

/* HTML spec: numeric references in the C1 range are remapped. */
static const unsigned long win1252[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

static void put_utf8(strbuf *sb, unsigned long cp)
{
    if (cp == 0 || cp > 0x10FFFFUL || (cp >= 0xD800 && cp <= 0xDFFF))
        cp = 0xFFFD;
    if (cp < 0x80) {
        sb_putc(sb, (char)cp);
    } else if (cp < 0x800) {
        sb_putc(sb, (char)(0xC0 | (cp >> 6)));
        sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        sb_putc(sb, (char)(0xE0 | (cp >> 12)));
        sb_putc(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
    } else {
        sb_putc(sb, (char)(0xF0 | (cp >> 18)));
        sb_putc(sb, (char)(0x80 | ((cp >> 12) & 0x3F)));
        sb_putc(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
    }
}

/* s[i] is '&'.  Appends the decoded text (or a literal '&') to `out` and
 * returns the number of input bytes consumed (always >= 1). */
static size_t decode_ref(strbuf *out, const char *s, size_t n, size_t i)
{
    size_t j = i + 1;

    if (j < n && s[j] == '#') {
        unsigned long cp = 0;
        int digits = 0;
        size_t k = j + 1;
        int hex = 0;

        if (k < n && (s[k] == 'x' || s[k] == 'X')) {
            hex = 1;
            k++;
        }
        while (k < n && digits < 8) {
            char c = s[k];
            int d;
            if (h_is_digit(c))
                d = c - '0';
            else if (hex && c >= 'a' && c <= 'f')
                d = c - 'a' + 10;
            else if (hex && c >= 'A' && c <= 'F')
                d = c - 'A' + 10;
            else
                break;
            cp = cp * (hex ? 16UL : 10UL) + (unsigned long)d;
            digits++;
            k++;
        }
        /* Swallow any further digits of an absurdly long reference. */
        while (k < n && (h_is_digit(s[k]) ||
                         (hex && ((s[k] >= 'a' && s[k] <= 'f') ||
                                  (s[k] >= 'A' && s[k] <= 'F'))))) {
            cp = 0x110000UL;   /* forces U+FFFD */
            k++;
        }
        if (digits == 0) {
            sb_putc(out, '&');
            return 1;
        }
        if (k < n && s[k] == ';')
            k++;               /* the ';' is optional for numeric refs */
        if (cp >= 0x80 && cp <= 0x9F)
            cp = win1252[cp - 0x80];
        put_utf8(out, cp);
        return k - i;
    }

    /* Named reference: letters/digits terminated by ';'. */
    {
        size_t k = j;
        size_t m;
        while (k < n && k - j < 32 && (h_is_alpha(s[k]) || h_is_digit(s[k])))
            k++;
        if (k > j && k < n && s[k] == ';') {
            size_t len = k - j;
            for (m = 0; m < NELEMS(named_refs); m++) {
                if (strlen(named_refs[m].name) == len &&
                    memcmp(named_refs[m].name, s + j, len) == 0) {
                    sb_puts(out, named_refs[m].utf8);
                    return (k + 1) - i;
                }
            }
        }
    }
    sb_putc(out, '&');
    return 1;
}

/* Append s[0..n) to `out`, decoding character references. */
static void append_decoded(strbuf *out, const char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        if (s[i] == '&')
            i += decode_ref(out, s, n, i);
        else
            sb_putc(out, s[i++]);
    }
}

/* ------------------------------------------------------------ node basics */

static dom_node *node_new(arena *a, int type)
{
    dom_node *n = (dom_node *)arena_alloc(a, sizeof *n);
    n->type = type;
    n->elem_index = -1;
    return n;
}

static void append_child(dom_node *parent, dom_node *child)
{
    child->parent = parent;
    child->prev_sibling = parent->last_child;
    child->next_sibling = NULL;
    if (parent->last_child != NULL)
        parent->last_child->next_sibling = child;
    else
        parent->first_child = child;
    parent->last_child = child;
}

const char *dom_attr_get(const dom_node *n, const char *name)
{
    const dom_attr *at;

    if (!n || !name)
        return NULL;
    for (at = n->attrs; at != NULL; at = at->next) {
        if (h_casecmp(at->name, name) == 0)
            return at->value;
    }
    return NULL;
}

/* --------------------------------------------------------- parser context */

enum { M_BEFORE_HEAD = 0, M_IN_HEAD, M_AFTER_HEAD, M_IN_BODY };

typedef struct {
    const char *s;
    size_t      n, i;
    arena      *a;
    dom_doc    *doc;
    vec         stack;     /* open elements, stack.v[0] is <html> */
    int         mode;
} htmlp;

static dom_node *cur(htmlp *p)
{
    return (dom_node *)p->stack.v[p->stack.len - 1];
}

static void stack_push(htmlp *p, dom_node *n)
{
    vec_push(&p->stack, n);
}

static void stack_pop_to(htmlp *p, size_t len)
{
    if (len < 1)
        len = 1;                 /* never pop <html> */
    if (p->stack.len > len)
        p->stack.len = len;
}

/* Index of the innermost open element with this tag, or -1. */
static long stack_find(htmlp *p, const char *tag)
{
    size_t k = p->stack.len;
    while (k > 0) {
        k--;
        if (tag_is((dom_node *)p->stack.v[k], tag))
            return (long)k;
    }
    return -1;
}

static void ensure_head(htmlp *p)
{
    dom_node *h;

    if (p->doc->head != NULL)
        return;
    h = node_new(p->a, DOM_ELEMENT);
    h->tag = arena_strdup(p->a, "head");
    append_child(p->doc->html, h);
    p->doc->head = h;
    stack_pop_to(p, 1);
    stack_push(p, h);
    p->mode = M_IN_HEAD;
}

static void close_head(htmlp *p)
{
    if (p->mode == M_BEFORE_HEAD)
        ensure_head(p);
    if (p->mode == M_IN_HEAD) {
        long k = stack_find(p, "head");
        if (k >= 0)
            stack_pop_to(p, (size_t)k);
        p->mode = M_AFTER_HEAD;
    }
}

static void ensure_body(htmlp *p)
{
    if (p->mode == M_IN_BODY)
        return;
    close_head(p);
    if (p->doc->body == NULL) {
        dom_node *b = node_new(p->a, DOM_ELEMENT);
        b->tag = arena_strdup(p->a, "body");
        append_child(p->doc->html, b);
        p->doc->body = b;
    }
    stack_pop_to(p, 1);
    stack_push(p, p->doc->body);
    p->mode = M_IN_BODY;
}

/* ------------------------------------------------------------ text nodes */

static int all_ws(const char *t)
{
    while (*t != '\0') {
        if (!h_is_ws(*t))
            return 0;
        t++;
    }
    return 1;
}

/* `txt` must already be arena-owned and decoded. */
static void insert_text(htmlp *p, char *txt)
{
    dom_node *parent, *t;

    if (txt == NULL || txt[0] == '\0')
        return;
    if (all_ws(txt)) {
        /* Whitespace before <head> is ignored by the spec; everywhere else
         * whitespace-only text nodes are preserved verbatim. */
        if (p->mode == M_BEFORE_HEAD)
            return;
    } else if (p->mode != M_IN_BODY) {
        ensure_body(p);
    }
    parent = cur(p);
    if (parent->last_child != NULL && parent->last_child->type == DOM_TEXT) {
        strbuf sb;                       /* merge adjacent runs (e.g. split
                                          * by a comment or a stray end tag) */
        sb_init(&sb, p->a);
        sb_puts(&sb, parent->last_child->text);
        sb_puts(&sb, txt);
        parent->last_child->text = sb_done(&sb);
        return;
    }
    t = node_new(p->a, DOM_TEXT);
    t->text = txt;
    append_child(parent, t);
}

/* -------------------------------------------------------- implied closing */

/* Walk the open elements from the top: pop through the first element whose
 * tag is in `targets`, or give up on reaching a scope boundary in `stops`. */
static void auto_close(htmlp *p, const char *const *targets, size_t nt,
                       const char *const *stops, size_t ns)
{
    size_t k = p->stack.len;
    while (k > 1) {
        dom_node *e = (dom_node *)p->stack.v[k - 1];
        if (in_list(e->tag, targets, nt)) {
            stack_pop_to(p, k - 1);
            return;
        }
        if (in_list(e->tag, stops, ns))
            return;
        k--;
    }
}

static const char *const t_p[]  = { "p" };
static const char *const t_li[] = { "li" };
static const char *const t_dd[] = { "dd", "dt" };
static const char *const t_opt[] = { "option", "optgroup" };
static const char *const t_tr[] = { "tr" };
static const char *const t_td[] = { "td", "th" };
static const char *const t_tb[] = { "tbody", "thead", "tfoot" };

static const char *const s_button[] = {
    "html", "body", "table", "td", "th", "caption", "template",
    "button", "marquee", "object", "applet"
};
static const char *const s_list[] = {
    "html", "body", "ul", "ol", "menu", "table", "td", "th",
    "caption", "template"
};
static const char *const s_dl[] = {
    "html", "body", "dl", "table", "td", "th", "caption", "template"
};
static const char *const s_select[] = { "html", "body", "select", "datalist" };
static const char *const s_table[] = { "html", "body", "table", "template" };
static const char *const s_row[] = { "html", "body", "tr", "table", "template" };

/* Start tags that close an open <p>. */
static const char *const closes_p[] = {
    "address", "article", "aside", "blockquote", "center", "details",
    "dialog", "dir", "div", "dl", "fieldset", "figcaption", "figure",
    "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header",
    "hgroup", "hr", "main", "menu", "nav", "ol", "p", "pre", "section",
    "summary", "table", "ul", "li", "dd", "dt"
};

static void apply_implied_close(htmlp *p, const char *tag)
{
    if (in_list(tag, closes_p, NELEMS(closes_p)))
        auto_close(p, t_p, NELEMS(t_p), s_button, NELEMS(s_button));
    if (strcmp(tag, "li") == 0)
        auto_close(p, t_li, NELEMS(t_li), s_list, NELEMS(s_list));
    else if (strcmp(tag, "dd") == 0 || strcmp(tag, "dt") == 0)
        auto_close(p, t_dd, NELEMS(t_dd), s_dl, NELEMS(s_dl));
    else if (strcmp(tag, "option") == 0 || strcmp(tag, "optgroup") == 0)
        auto_close(p, t_opt, NELEMS(t_opt), s_select, NELEMS(s_select));
    else if (strcmp(tag, "tr") == 0)
        auto_close(p, t_tr, NELEMS(t_tr), s_table, NELEMS(s_table));
    else if (strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0)
        auto_close(p, t_td, NELEMS(t_td), s_row, NELEMS(s_row));
    else if (strcmp(tag, "tbody") == 0 || strcmp(tag, "thead") == 0 ||
             strcmp(tag, "tfoot") == 0)
        auto_close(p, t_tb, NELEMS(t_tb), s_table, NELEMS(s_table));
}

/* ------------------------------------------------------------- tokenizer */

static void skip_to_gt(htmlp *p)
{
    while (p->i < p->n && p->s[p->i] != '>')
        p->i++;
    if (p->i < p->n)
        p->i++;
}

static void skip_doctype(htmlp *p)
{
    /* Quoted sections may contain '>' (public/system identifiers). */
    while (p->i < p->n) {
        char c = p->s[p->i];
        if (c == '>') {
            p->i++;
            return;
        }
        if (c == '"' || c == '\'') {
            char q = c;
            p->i++;
            while (p->i < p->n && p->s[p->i] != q)
                p->i++;
            if (p->i < p->n)
                p->i++;
            continue;
        }
        p->i++;
    }
}

static void skip_until(htmlp *p, const char *marker)
{
    size_t mlen = strlen(marker);
    while (p->i < p->n) {
        if (p->n - p->i >= mlen && memcmp(p->s + p->i, marker, mlen) == 0) {
            p->i += mlen;
            return;
        }
        p->i++;
    }
    p->i = p->n;                 /* unterminated: closes at EOF */
}

/* Read a tag name at p->i; returns a lowercased arena string ("" if none). */
static char *read_tag_name(htmlp *p)
{
    size_t start = p->i;
    strbuf sb;

    while (p->i < p->n) {
        char c = p->s[p->i];
        if (h_is_ws(c) || c == '/' || c == '>')
            break;
        p->i++;
    }
    sb_init(&sb, p->a);
    {
        size_t k;
        for (k = start; k < p->i; k++)
            sb_putc(&sb, h_lower(p->s[k]));
    }
    return sb_done(&sb);
}

/* Parse attributes up to and including the '>' (or EOF). */
static dom_attr *read_attributes(htmlp *p, int *self_closing)
{
    dom_attr *head = NULL, *tail = NULL;

    *self_closing = 0;
    for (;;) {
        char *name;
        char *value;
        size_t nstart;

        while (p->i < p->n && h_is_ws(p->s[p->i]))
            p->i++;
        if (p->i >= p->n)
            break;
        if (p->s[p->i] == '>') {
            p->i++;
            break;
        }
        if (p->s[p->i] == '/') {
            p->i++;
            if (p->i < p->n && p->s[p->i] == '>') {
                *self_closing = 1;
                p->i++;
                break;
            }
            continue;                  /* stray '/', ignore */
        }

        nstart = p->i;
        while (p->i < p->n) {
            char c = p->s[p->i];
            if (h_is_ws(c) || c == '/' || c == '>' || c == '=')
                break;
            p->i++;
        }
        if (p->i == nstart) {          /* no progress possible */
            p->i++;
            continue;
        }
        {
            strbuf sb;
            size_t k;
            sb_init(&sb, p->a);
            for (k = nstart; k < p->i; k++)
                sb_putc(&sb, h_lower(p->s[k]));
            name = sb_done(&sb);
        }

        while (p->i < p->n && h_is_ws(p->s[p->i]))
            p->i++;
        value = NULL;
        if (p->i < p->n && p->s[p->i] == '=') {
            strbuf sb;
            size_t vs;
            p->i++;
            while (p->i < p->n && h_is_ws(p->s[p->i]))
                p->i++;
            sb_init(&sb, p->a);
            if (p->i < p->n && (p->s[p->i] == '"' || p->s[p->i] == '\'')) {
                char q = p->s[p->i++];
                vs = p->i;
                while (p->i < p->n && p->s[p->i] != q)
                    p->i++;
                append_decoded(&sb, p->s + vs, p->i - vs);
                if (p->i < p->n)
                    p->i++;            /* closing quote */
            } else {
                vs = p->i;
                while (p->i < p->n && !h_is_ws(p->s[p->i]) && p->s[p->i] != '>')
                    p->i++;
                append_decoded(&sb, p->s + vs, p->i - vs);
            }
            value = sb_done(&sb);
        }
        if (value == NULL)
            value = arena_strdup(p->a, "");

        if (name[0] != '\0') {
            dom_attr *at;
            int dup = 0;
            for (at = head; at != NULL; at = at->next) {
                if (strcmp(at->name, name) == 0) {
                    dup = 1;           /* spec: the first one wins */
                    break;
                }
            }
            if (!dup) {
                at = (dom_attr *)arena_alloc(p->a, sizeof *at);
                at->name = name;
                at->value = value;
                at->next = NULL;
                if (tail != NULL)
                    tail->next = at;
                else
                    head = at;
                tail = at;
            }
        }
    }
    return head;
}

/* Consume RAWTEXT/RCDATA up to "</tag" and attach it as one text node. */
static void consume_raw_text(htmlp *p, dom_node *elem, const char *tag,
                             int decode)
{
    size_t start = p->i;
    size_t end = p->n;
    size_t tlen = strlen(tag);
    strbuf sb;

    while (p->i < p->n) {
        if (p->s[p->i] == '<' && p->i + 1 < p->n && p->s[p->i + 1] == '/' &&
            p->n - (p->i + 2) >= tlen) {
            size_t k;
            int match = 1;
            for (k = 0; k < tlen; k++) {
                if (h_lower(p->s[p->i + 2 + k]) != tag[k]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                size_t after = p->i + 2 + tlen;
                if (after >= p->n || h_is_ws(p->s[after]) ||
                    p->s[after] == '/' || p->s[after] == '>') {
                    end = p->i;
                    p->i = after;
                    skip_to_gt(p);
                    break;
                }
            }
        }
        p->i++;
    }
    if (p->i >= p->n && end == p->n)
        end = p->n;                     /* unterminated: to EOF */

    if (end > start) {
        size_t s0 = start;
        /* <textarea> swallows a single leading newline. */
        if (strcmp(tag, "textarea") == 0 && s0 < end) {
            if (p->s[s0] == '\n')
                s0++;
            else if (p->s[s0] == '\r') {
                s0++;
                if (s0 < end && p->s[s0] == '\n')
                    s0++;
            }
        }
        if (end > s0) {
            dom_node *t = node_new(p->a, DOM_TEXT);
            sb_init(&sb, p->a);
            if (decode)
                append_decoded(&sb, p->s + s0, end - s0);
            else
                sb_putn(&sb, p->s + s0, end - s0);
            t->text = sb_done(&sb);
            append_child(elem, t);
        }
    }
}

/* --------------------------------------------------------- start/end tags */

static dom_node *make_element(htmlp *p, const char *tag, dom_attr *attrs)
{
    dom_node *e = node_new(p->a, DOM_ELEMENT);
    e->tag = arena_strdup(p->a, tag);
    e->attrs = attrs;
    return e;
}

static void merge_attrs(htmlp *p, dom_node *e, dom_attr *attrs)
{
    dom_attr *src;

    for (src = attrs; src != NULL; src = src->next) {
        if (dom_attr_get(e, src->name) == NULL) {
            dom_attr *at = (dom_attr *)arena_alloc(p->a, sizeof *at);
            dom_attr *t;
            at->name = src->name;
            at->value = src->value;
            at->next = NULL;
            if (e->attrs == NULL) {
                e->attrs = at;
            } else {
                for (t = e->attrs; t->next != NULL; t = t->next)
                    ;
                t->next = at;
            }
        }
    }
}

static void handle_start_tag(htmlp *p, char *tag, dom_attr *attrs,
                             int self_closing)
{
    dom_node *e;
    dom_node *parent;

    if (tag[0] == '\0')
        return;

    if (strcmp(tag, "html") == 0) {
        merge_attrs(p, p->doc->html, attrs);
        return;
    }
    if (strcmp(tag, "head") == 0) {
        if (p->mode == M_BEFORE_HEAD)
            ensure_head(p);
        else if (p->doc->head != NULL)
            merge_attrs(p, p->doc->head, attrs);
        return;
    }
    if (strcmp(tag, "body") == 0) {
        int existed = (p->doc->body != NULL);
        ensure_body(p);
        if (existed)
            merge_attrs(p, p->doc->body, attrs);
        else
            p->doc->body->attrs = attrs;
        return;
    }
    if (strcmp(tag, "frameset") == 0) {
        /* Not supported; treat as ordinary body content. */
        ensure_body(p);
    }

    if (is_head_element(tag)) {
        if (p->mode == M_BEFORE_HEAD)
            ensure_head(p);
    } else {
        ensure_body(p);
        apply_implied_close(p, tag);
    }

    /* In "after head", void and raw-text head elements belong to <head>. */
    parent = cur(p);
    if (p->mode == M_AFTER_HEAD && is_head_element(tag) &&
        (is_void_element(tag) || is_rawtext_element(tag) ||
         is_rcdata_element(tag))) {
        parent = p->doc->head;
    } else if (is_head_element(tag) && p->mode != M_IN_HEAD &&
               p->mode != M_IN_BODY) {
        ensure_body(p);
        parent = cur(p);
    }

    e = make_element(p, tag, attrs);
    append_child(parent, e);

    if (is_void_element(tag))
        return;                          /* void elements never get children */
    if (is_rawtext_element(tag)) {
        consume_raw_text(p, e, tag, 0);
        return;
    }
    if (is_rcdata_element(tag)) {
        consume_raw_text(p, e, tag, 1);
        return;
    }
    if (self_closing)
        return;                          /* tolerated on any element */
    if (parent == cur(p))
        stack_push(p, e);
}

static void handle_end_tag(htmlp *p, char *tag)
{
    long k;

    if (tag[0] == '\0')
        return;
    if (strcmp(tag, "head") == 0) {
        close_head(p);
        return;
    }
    if (strcmp(tag, "body") == 0 || strcmp(tag, "html") == 0) {
        /* Anything that follows still belongs to the body. */
        ensure_body(p);
        k = stack_find(p, "body");
        if (k >= 0)
            stack_pop_to(p, (size_t)k + 1);
        return;
    }
    k = stack_find(p, tag);
    if (k <= 0)
        return;                          /* no matching open element: ignore */
    stack_pop_to(p, (size_t)k);          /* closes any intervening elements */
}

/* ------------------------------------------------------------- main loop */

static void index_tree(dom_node *root)
{
    int order = 0;
    dom_node *n = root;

    root->child_index = 0;
    root->elem_index = 0;
    root->n_elem_siblings = 1;

    while (n != NULL) {
        dom_node *c;
        int nelem = 0, ci = 0, ei = 0;

        n->doc_order = order++;
        for (c = n->first_child; c != NULL; c = c->next_sibling) {
            if (c->type == DOM_ELEMENT)
                nelem++;
        }
        for (c = n->first_child; c != NULL; c = c->next_sibling) {
            c->child_index = ci++;
            c->n_elem_siblings = nelem;
            c->elem_index = (c->type == DOM_ELEMENT) ? ei++ : -1;
        }
        if (n->first_child != NULL) {
            n = n->first_child;
            continue;
        }
        while (n != NULL && n->next_sibling == NULL)
            n = n->parent;
        if (n != NULL)
            n = n->next_sibling;
    }
}

dom_doc *dom_parse_string(arena *a, const char *html, char **err)
{
    htmlp p;
    dom_doc *doc;
    dom_node *root;

    if (err != NULL)
        *err = NULL;
    if (html == NULL)
        html = "";

    doc = (dom_doc *)arena_alloc(a, sizeof *doc);
    doc->a = a;

    root = node_new(a, DOM_ELEMENT);
    root->tag = arena_strdup(a, "html");
    doc->root = root;
    doc->html = root;

    p.s = html;
    p.n = strlen(html);
    p.i = 0;
    p.a = a;
    p.doc = doc;
    p.mode = M_BEFORE_HEAD;
    vec_init(&p.stack, a);
    stack_push(&p, root);

    while (p.i < p.n) {
        char c = p.s[p.i];

        if (c != '<') {
            size_t start = p.i;
            strbuf sb;
            while (p.i < p.n && p.s[p.i] != '<')
                p.i++;
            sb_init(&sb, a);
            append_decoded(&sb, p.s + start, p.i - start);
            insert_text(&p, sb_done(&sb));
            continue;
        }

        /* c == '<' */
        if (p.n - p.i >= 4 && memcmp(p.s + p.i, "<!--", 4) == 0) {
            p.i += 4;
            skip_until(&p, "-->");
            continue;
        }
        if (p.i + 1 < p.n && p.s[p.i + 1] == '!') {
            if (p.n - p.i >= 9 && memcmp(p.s + p.i, "<![CDATA[", 9) == 0) {
                p.i += 9;
                skip_until(&p, "]]>");
                continue;
            }
            p.i += 2;
            skip_doctype(&p);            /* DOCTYPE or bogus comment */
            continue;
        }
        if (p.i + 1 < p.n && p.s[p.i + 1] == '?') {
            p.i += 2;
            skip_to_gt(&p);              /* bogus comment */
            continue;
        }
        if (p.i + 1 < p.n && p.s[p.i + 1] == '/') {
            if (p.i + 2 < p.n && h_is_alpha(p.s[p.i + 2])) {
                char *tag;
                int sc;
                p.i += 2;
                tag = read_tag_name(&p);
                (void)read_attributes(&p, &sc);   /* end tags: attrs ignored */
                handle_end_tag(&p, tag);
            } else {
                p.i += 2;
                skip_to_gt(&p);          /* "</>" and friends */
            }
            continue;
        }
        if (p.i + 1 < p.n && h_is_alpha(p.s[p.i + 1])) {
            char *tag;
            dom_attr *attrs;
            int sc = 0;
            p.i += 1;
            tag = read_tag_name(&p);
            attrs = read_attributes(&p, &sc);
            handle_start_tag(&p, tag, attrs, sc);
            continue;
        }
        /* A '<' that starts nothing is literal text. */
        {
            strbuf sb;
            sb_init(&sb, a);
            sb_putc(&sb, '<');
            p.i++;
            insert_text(&p, sb_done(&sb));
        }
    }

    /* Both <head> and <body> always exist in the finished tree. */
    if (doc->head == NULL)
        ensure_head(&p);
    if (doc->body == NULL)
        ensure_body(&p);

    index_tree(root);
    return doc;
}

dom_doc *dom_parse_file(arena *a, const char *path, char **err)
{
    FILE *f;
    strbuf sb;
    char buf[16384];
    size_t got;
    dom_doc *doc;

    if (err != NULL)
        *err = NULL;
    if (path == NULL) {
        if (err != NULL)
            *err = arena_strdup(a, "dom_parse_file: NULL path");
        return NULL;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        if (err != NULL)
            *err = arena_printf(a, "cannot open '%s': %s", path,
                                strerror(errno));
        return NULL;
    }
    sb_init(&sb, a);
    while ((got = fread(buf, 1, sizeof buf, f)) > 0)
        sb_putn(&sb, buf, got);
    if (ferror(f)) {
        if (err != NULL)
            *err = arena_printf(a, "error reading '%s'", path);
        fclose(f);
        return NULL;
    }
    fclose(f);
    doc = dom_parse_string(a, sb_done(&sb), err);
    return doc;
}
