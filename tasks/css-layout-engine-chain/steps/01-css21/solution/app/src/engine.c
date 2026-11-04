/* engine.c -- the integration layer.
 *
 * Turns the four modules into the engine that engine.h publishes:
 *
 *   frontend  dom_parse_file()   HTML -> dom_node tree (arena-owned)
 *   cascade   cascade_build()    winning declared value per property/element
 *   values    values_compute()   -> dom_node::style (a `computed`)
 *   layout    lay_run()          -> lay_box per node, absolute from the ICB
 *
 * Nothing here re-implements any of them; this file is wiring, the Ahem font
 * metrics the layout callbacks need, and the user-agent defaults and used-box
 * values documented below.
 *
 * C11, libc only.  One arena per document, released by eng_free().
 */
#include "engine.h"

#include "util.h"
#include "dom.h"
#include "css.h"
#include "selector.h"
#include "cascade.h"
#include "style.h"
#include "values.h"
#include "layout.h"

#include <stdlib.h>
#include <string.h>

/* ===========================================================================
 * How this file fits the other modules together.
 *
 * (1) No user-agent stylesheet exists anywhere in the engine, so `display`
 *     starts at its CSS initial value for every element: a bare div is
 *     inline.  Left alone, <head>, <style> and <title> generate boxes and the
 *     CSS source text inside <style> is laid out as visible text.
 *
 *     cascade_build()'s `extra` sheets cannot carry the UA rules: cascade.h
 *     states that they sort AFTER the document's own sheets and therefore WIN
 *     specificity ties.  A UA sheet must LOSE them -- UA `div{display:block}`
 *     beating author `div{display:flex}` would be exactly backwards.
 *
 *     So the defaults are applied here, after values_compute(), and only where
 *     cascade_value() reports no author winner for that property on that
 *     element.  "Author declaration wins, otherwise the UA default" is what a
 *     lowest-origin sheet computes to, reached through the API that exists.
 *
 * (2) lay_box publishes the used margin / padding / border / inset arrays
 *     that values_box asks for, so fill_box() copies them across instead of
 *     reconstructing them from the computed style.  Reconstructing them here
 *     would resolve percentages against the PARENT's content width, which is
 *     only correct for in-flow boxes; an absolutely positioned box measures
 *     its percentages against its containing block, which is not in general
 *     its parent.  Layout knows that box, so layout publishes it.
 * ===========================================================================
 */

struct eng_doc {
    arena   *a;          /* one arena per document; owns dom, cascade, styles */
    dom_doc *doc;
    cascade *cas;
    lay_doc *lay;        /* malloc'd by layout; released with lay_free()      */
    lay_env  env;        /* lay_run() STORES THIS POINTER -- must outlive lay */
    double   vw, vh;
    int      has_ahem;   /* the document loads Ahem with @font-face */
};

/* ------------------------------------------------------------ small utils */

static int ieq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    for (; *a && *b; a++, b++) {
        int x = (*a >= 'A' && *a <= 'Z') ? *a - 'A' + 'a' : *a;
        int y = (*b >= 'A' && *b <= 'Z') ? *b - 'A' + 'a' : *b;
        if (x != y) return 0;
    }
    return *a == '\0' && *b == '\0';
}

/* Did an author declaration win `prop` on `n`?  cascade_value() takes the
 * !important out-param; values.c always hands it a real int, so do the same
 * rather than rely on the header's "may be NULL". */
static int author_set(cascade *cas, dom_node *n, const char *prop)
{
    int imp = 0;
    const char *v;
    if (!cas || !n) return 0;
    v = cascade_value(cas, n, prop, &imp);
    return v != NULL && *v != '\0';  /* an empty winner reads as absent */
}

/* --------------------------------------------- which faces the document has
 *
 * A family name selects a face the document actually has.  Ahem is not a
 * system font in the reference environment: it exists only where the document
 * loads it with @font-face.  `font-family: Ahem` in a document that declares
 * none is a miss, and CSS 2.1 15.3 falls back to the UA's standard font --
 * which is why one fixture's "Ahem" text measures in the standard font.
 */
static int value_is_ahem(const char *v)
{
    char buf[64];
    size_t k = 0;

    if (!v) return 0;
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r' || *v == '\f' ||
           *v == '"' || *v == '\'') v++;
    while (*v && k + 1 < sizeof buf && *v != '"' && *v != '\'' && *v != ',')
        buf[k++] = *v++;
    while (k > 0 && (buf[k - 1] == ' ' || buf[k - 1] == '\t' ||
                     buf[k - 1] == '\n' || buf[k - 1] == '\r' || buf[k - 1] == '\f'))
        k--;
    buf[k] = '\0';
    return ieq(buf, "ahem");
}

static int sheet_loads_ahem(const css_rule *r)
{
    for (; r; r = r->next) {
        if (r->kind == CSS_AT_RULE && r->at_name && ieq(r->at_name, "font-face")) {
            const css_decl *dc;
            for (dc = r->decls; dc; dc = dc->next)
                if (dc->prop && ieq(dc->prop, "font-family") &&
                    value_is_ahem(dc->value))
                    return 1;
        }
        if (r->children && sheet_loads_ahem(r->children)) return 1;
    }
    return 0;
}

static int doc_loads_ahem(arena *tmp, dom_node *n)
{
    dom_node *ch;

    if (!n) return 0;
    if (n->type == DOM_ELEMENT && ieq(n->tag, "style")) {
        for (ch = n->first_child; ch; ch = ch->next_sibling) {
            if (ch->type == DOM_TEXT && ch->text) {
                css_sheet *sh = css_parse_sheet(tmp, ch->text);
                if (sh && sheet_loads_ahem(sh->rules)) return 1;
            }
        }
    }
    for (ch = n->first_child; ch; ch = ch->next_sibling)
        if (doc_loads_ahem(tmp, ch)) return 1;
    return 0;
}

/* ================================================================== fonts
 *
 * Two faces are modelled, because the documents use exactly two.  The
 * fixtures pull in Ahem with @font-face: every glyph sits on a 1em advance,
 * the ascent is 0.8em and the descent 0.2em with no line gap.  Everything
 * else is rendered in the UA's standard font, whose metrics are those of
 * Times New Roman -- 2048 units per em, ascent 1825, descent 443, line gap
 * 87 -- with the per-glyph advances tabulated below for U+0020..U+007E.
 *
 * The ascent, the descent and the line gap are each ROUNDED to a whole pixel
 * before they are used, and then one pixel moves from the ascent to the
 * descent whenever the descent rounded DOWN (see eng_font_ad).  That is what
 * makes `line-height: normal` 18px at a 16px standard font (13 + 4 + 1) while
 * the content area of an inline box at that size is 17px (13 + 4).  CSS 2.1
 * 10.8.1 leaves both quantities to the UA; these are the ones the reference
 * recordings were made with.
 *
 * Advances are NOT the exact rational units * size / upem either: they are
 * scaled through a 16.16 fixed-point factor that is rounded once per face and
 * size (eng_scale_16_16), and a run of text is then rounded UP to the 1/64px
 * layout grid.  Both steps are observable: five Ahem "X" at 16px measure
 * 80 + 1/64 px, the same five at 20px exactly 100.
 *
 * Advances are counted in CODE POINTS: counting bytes would make every
 * U+200B three glyphs wide.  U+200B (ZERO WIDTH SPACE) advances 0 in every
 * face -- the fixtures build min-content/max-content sizes out of runs of
 * "X" separated by it.
 */
static const unsigned short g_serif_adv[95] = {
     512,  682,  836, 1024, 1024, 1706, 1593,  369,  682,  682, 1024, 1155,
     512,  682,  512,  569, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
    1024, 1024,  569,  569, 1155, 1155, 1155,  909, 1886, 1479, 1366, 1366,
    1479, 1251, 1139, 1479, 1479,  682,  797, 1479, 1251, 1821, 1479, 1479,
    1139, 1479, 1366, 1139, 1251, 1479, 1479, 1933, 1479, 1479, 1251,  682,
     569,  682,  961, 1024,  682,  909, 1024,  909, 1024,  909,  682, 1024,
    1024,  569,  569, 1024,  569, 1593, 1024, 1024, 1024, 1024,  682,  797,
     569, 1024, 1024, 1479, 1024, 1024,  909,  983,  410,  983, 1108
};

static const unsigned short g_serif_adv_bold[95] = {
     512,  682, 1137, 1024, 1024, 2048, 1706,  569,  682,  682, 1024, 1167,
     512,  682,  512,  569, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
    1024, 1024,  682,  682, 1167, 1167, 1167, 1024, 1905, 1479, 1366, 1479,
    1479, 1366, 1251, 1593, 1593,  797, 1024, 1593, 1366, 1933, 1479, 1593,
    1251, 1593, 1479, 1139, 1366, 1479, 1479, 2048, 1479, 1479, 1366,  682,
     569,  682, 1190, 1024,  682, 1024, 1139,  909, 1139,  909,  682, 1024,
    1139,  569,  682, 1139,  569, 1706, 1139, 1024, 1139, 1139,  909,  797,
     682, 1139, 1024, 1479, 1024, 1024,  909,  807,  451,  807, 1065
};

typedef struct {
    double upem, asc, desc, gap, fallback;
    const unsigned short *adv;   /* U+0020..U+007E, or NULL for a fixed advance */
} eng_face;

static const eng_face g_face_ahem  = { 1000.0,  800.0, 200.0,  0.0, 1000.0, NULL };
static const eng_face g_face_std   = { 2048.0, 1825.0, 443.0, 87.0, 1024.0, g_serif_adv };
static const eng_face g_face_std_b = { 2048.0, 1825.0, 443.0, 87.0, 1024.0, g_serif_adv_bold };

static const eng_face *eng_face_of(const computed *st)
{
    if (!st) return &g_face_std;
    if (st->font_family == FF_AHEM) return &g_face_ahem;
    return (st->font_weight >= 600) ? &g_face_std_b : &g_face_std;
}

static double eng_fs(const computed *st) { return st ? st->font_size : 16.0; }

/* Chrome rounds each vertical metric to a whole pixel before using it.
 * Spelled out rather than calling floor(): style.h declares a `float_t` of its
 * own, which collides with the C99 typedef <math.h> declares under that name. */
static double eng_round_px(double v)
{
    double t = (double)(long long)(v + 0.5);   /* truncates toward zero */
    return (t > v + 0.5) ? t - 1.0 : t;        /* -> floor, for either sign */
}

/* The 16.16 fixed-point factor a scaler builds once from the pixel size and
 * the em square.  It is rounded, so every advance it scales carries that one
 * rounding: at 16px over a 1000-unit em the factor is 67109/65536, a hair MORE
 * than 1.024, and an "X" whose advance is a full em measures 16 + 151/1000000
 * px rather than 16.  Rounding the RUN up to the layout grid (lay_ceil64) then
 * turns that hair into a visible 1/64px.  At 20px the same factor rounds the
 * other way and the run measures exactly. */
static double eng_scale_16_16(const eng_face *f, double size)
{
    long long ppem64 = (long long)(size * 64.0 + 0.5);   /* size in 26.6 px */
    long long upem   = (long long)f->upem;
    if (upem <= 0) return 0.0;
    return (double)((ppem64 * 65536 + upem / 2) / upem);
}

static double eng_text_width(const char *text, const computed *st)
{
    const eng_face *f = eng_face_of(st);
    const unsigned char *p = (const unsigned char *)text;
    double units = 0.0;

    if (!text) return 0.0;
    while (*p) {
        unsigned c = *p, cp;
        size_t len, k;

        if (c < 0x80)                len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else                         len = 1;   /* stray continuation byte */

        /* A truncated sequence ends the code point where it actually stops. */
        for (k = 1; k < len; k++) {
            if ((p[k] & 0xC0) != 0x80) { len = k; break; }
        }
        if (len == 1) {
            cp = c;
        } else {
            static const unsigned mask[5] = { 0u, 0x7Fu, 0x1Fu, 0x0Fu, 0x07u };
            cp = c & mask[len];
            for (k = 1; k < len; k++) cp = (cp << 6) | (unsigned)(p[k] & 0x3F);
        }

        if (cp == 0x200Bu)                                  ;    /* no advance */
        else if (f->adv && cp >= 0x20u && cp < 0x7Fu)  units += f->adv[cp - 0x20u];
        else                                           units += f->fallback;
        p += len;
    }
    /* 65536 and 64 are powers of two, so this is exact: the caller may sum
       several of these and round the total to the grid without drift. */
    return units * eng_scale_16_16(f, eng_fs(st)) / 65536.0 / 64.0;
}

/* The ascent and the descent are rounded to whole pixels INDEPENDENTLY, so
 * their sum need not be the rounded content height; a descent that rounded
 * DOWN would clip the lowest row of a glyph inside an `overflow: hidden` box,
 * so the ascent lends it one pixel back.  The content area keeps its height -
 * only the split across the baseline moves - but that split is exactly what a
 * line box made of an atomic inline plus a strut is measured by, because the
 * atomic supplies the ascent and the strut only the descent.  At 16px it makes
 * the standard face 13 + 4 rather than 14 + 3, and Ahem 12 + 4 rather than
 * 13 + 3; at 20px Ahem's descent is already exact (4.0) and nothing moves. */
static void eng_font_ad(const computed *st, double *asc, double *desc)
{
    const eng_face *f = eng_face_of(st);
    double size = eng_fs(st);
    double fa = f->asc  * size / f->upem;
    double fd = f->desc * size / f->upem;
    double a  = eng_round_px(fa);
    double d  = eng_round_px(fd);
    if (d < fd && a >= 1.0) { a -= 1.0; d += 1.0; }
    *asc = a; *desc = d;
}

static double eng_font_ascent(const computed *st)
{
    double a, d;
    eng_font_ad(st, &a, &d);
    return a;
}

static double eng_font_descent(const computed *st)
{
    double a, d;
    eng_font_ad(st, &a, &d);
    return d;
}

static double eng_font_line_gap(const computed *st)
{
    const eng_face *f = eng_face_of(st);
    return eng_round_px(f->gap * eng_fs(st) / f->upem);
}

/* ------------------------------------------------- DOM walk for lay_env
 * The callbacks carry no context pointer, so they must be pure functions of
 * the node.  dom_node supplies everything they need.
 */
static void *eng_first_child(void *node)
{
    return node ? ((dom_node *)node)->first_child : NULL;
}

static void *eng_next_sibling(void *node)
{
    return node ? ((dom_node *)node)->next_sibling : NULL;
}

/* NULL for text nodes -- that is how layout tells the two apart. */
static const computed *eng_style_of(void *node)
{
    dom_node *n = (dom_node *)node;
    if (!n || n->type != DOM_ELEMENT) return NULL;
    return (const computed *)n->style;
}

static const char *eng_text_of(void *node)
{
    dom_node *n = (dom_node *)node;
    if (!n || n->type != DOM_TEXT) return NULL;
    return n->text;
}

/* ==================================================== user-agent defaults */

static const struct { const char *tag; display_t d; } g_ua_display[] = {
    /* Not rendered at all.  Without these the stylesheet text is laid out. */
    { "head", D_NONE }, { "style", D_NONE }, { "title", D_NONE },
    { "script", D_NONE }, { "meta", D_NONE }, { "link", D_NONE },
    { "base", D_NONE }, { "noscript", D_NONE }, { "template", D_NONE },
    { "datalist", D_NONE }, { "param", D_NONE }, { "source", D_NONE },
    { "track", D_NONE }, { "area", D_NONE },

    { "html", D_BLOCK }, { "body", D_BLOCK }, { "div", D_BLOCK },
    { "p", D_BLOCK }, { "h1", D_BLOCK }, { "h2", D_BLOCK }, { "h3", D_BLOCK },
    { "h4", D_BLOCK }, { "h5", D_BLOCK }, { "h6", D_BLOCK },
    { "ul", D_BLOCK }, { "ol", D_BLOCK }, { "dl", D_BLOCK }, { "dt", D_BLOCK },
    { "dd", D_BLOCK }, { "blockquote", D_BLOCK }, { "pre", D_BLOCK },
    { "form", D_BLOCK }, { "fieldset", D_BLOCK }, { "legend", D_BLOCK },
    { "hr", D_BLOCK }, { "address", D_BLOCK }, { "article", D_BLOCK },
    { "aside", D_BLOCK }, { "section", D_BLOCK }, { "nav", D_BLOCK },
    { "header", D_BLOCK }, { "footer", D_BLOCK }, { "main", D_BLOCK },
    { "figure", D_BLOCK }, { "figcaption", D_BLOCK }, { "center", D_BLOCK },
    { "dir", D_BLOCK }, { "menu", D_BLOCK }, { "details", D_BLOCK },
    { "summary", D_BLOCK }, { "optgroup", D_BLOCK }, { "option", D_BLOCK },

    { "li", D_LIST_ITEM },

    { "table", D_TABLE }, { "caption", D_TABLE_CAPTION },
    { "thead", D_TABLE_HEADER_GROUP }, { "tbody", D_TABLE_ROW_GROUP },
    { "tfoot", D_TABLE_FOOTER_GROUP }, { "tr", D_TABLE_ROW },
    { "td", D_TABLE_CELL }, { "th", D_TABLE_CELL },
    { "col", D_TABLE_COLUMN }, { "colgroup", D_TABLE_COLUMN_GROUP },

    { "button", D_INLINE_BLOCK }, { "input", D_INLINE_BLOCK },
    { "select", D_INLINE_BLOCK }, { "textarea", D_INLINE_BLOCK },
    /* <img> and the other replaced inlines keep the initial `inline`. */
    { NULL, D_INLINE }
};

static int ua_display_for(const char *tag, display_t *out)
{
    size_t i;
    if (!tag) return 0;
    for (i = 0; g_ua_display[i].tag; i++) {
        if (ieq(tag, g_ua_display[i].tag)) { *out = g_ua_display[i].d; return 1; }
    }
    return 0;
}

/* A UA margin, applied per side and only where the author declared neither
 * the shorthand nor that longhand.  A negative entry leaves the initial value.
 *
 * Only `body { margin: 8px }` and `p { margin: 1em 0 }` are modelled, because
 * they are the only UA margins the documents can show.  The rest of CSS 2.1
 * Appendix D is deliberately left out: h1's margin is stated in em of a
 * font-size this engine has no UA rule for either, and half a rule would be
 * worse than none. */
static void ua_margin(eng_doc *d, dom_node *n, computed *c, const double v[4])
{
    static const char *const mg[4] = { "margin-top", "margin-right",
                                       "margin-bottom", "margin-left" };
    int i;
    if (author_set(d->cas, n, "margin")) return;
    for (i = 0; i < 4; i++) {
        if (v[i] < 0.0) continue;
        if (author_set(d->cas, n, mg[i])) continue;
        c->margin[i].kind = LEN_PX;
        c->margin[i].v = v[i];
    }
}

/* Preorder, because `white-space` and `font-weight` inherit: values_compute()
 * has already run inheritance once, so a UA default set on <pre> or <strong>
 * here would not otherwise reach its descendants.  Recomputing the inherited
 * value down the tree makes the pass a no-op for a document with neither. */
static void ua_walk(eng_doc *d, dom_node *n, whitespace_t inherited_ws,
                    int inherited_weight)
{
    whitespace_t ws = inherited_ws;
    int weight = inherited_weight;
    dom_node *ch;

    if (n->type == DOM_ELEMENT && n->style) {
        /* values.h: `computed` is the first member of the record it stores,
         * so this pointer is the writable computed style of the element. */
        computed *c = (computed *)n->style;
        display_t ua;

        if (!author_set(d->cas, n, "display") && ua_display_for(n->tag, &ua))
            c->display = ua;

        if (author_set(d->cas, n, "white-space"))
            ws = c->white_space;
        else if (ieq(n->tag, "pre") || ieq(n->tag, "textarea"))
            ws = WS_PRE;
        c->white_space = ws;

        /* A family the document never loaded is a miss: fall back (15.3). */
        if (!d->has_ahem && c->font_family == FF_AHEM)
            c->font_family = FF_DEFAULT;

        if (author_set(d->cas, n, "font-weight") || author_set(d->cas, n, "font"))
            weight = c->font_weight;
        else if (ieq(n->tag, "strong") || ieq(n->tag, "b") ||
                 ieq(n->tag, "th") || ieq(n->tag, "caption"))
            weight = 700;
        c->font_weight = weight;

        if (ieq(n->tag, "body")) {
            static const double m[4] = { 8.0, 8.0, 8.0, 8.0 };
            ua_margin(d, n, c, m);
        } else if (ieq(n->tag, "p")) {
            /* `p { margin: 1em 0 }` -- 1em of the element's own font size. */
            double m[4];
            m[EDGE_TOP] = c->font_size;  m[EDGE_RIGHT] = 0.0;
            m[EDGE_BOTTOM] = c->font_size; m[EDGE_LEFT] = 0.0;
            ua_margin(d, n, c, m);
        }
    }
    for (ch = n->first_child; ch; ch = ch->next_sibling)
        ua_walk(d, ch, ws, weight);
}

/* ================================================================ loading */

eng_doc *eng_load_file(const char *path, double viewport_w, double viewport_h)
{
    arena *a;
    eng_doc *d;
    char *err = NULL;

    if (!path) return NULL;
    a = arena_new();
    if (!a) return NULL;

    d = (eng_doc *)arena_alloc(a, sizeof *d);   /* zeroed by the arena */
    if (!d) { arena_free(a); return NULL; }
    d->a = a;
    d->vw = viewport_w;
    d->vh = viewport_h;

    /* The only failure the contract admits: the file cannot be read. */
    d->doc = dom_parse_file(a, path, &err);
    if (!d->doc) { arena_free(a); return NULL; }

    d->cas = cascade_build(a, d->doc, NULL, 0);
    values_compute(a, d->doc, d->cas, viewport_w, viewport_h);
    {   /* a scratch arena: the re-parsed sheets are wanted only for this test */
        arena *tmp = arena_new();
        d->has_ahem = tmp ? doc_loads_ahem(tmp, d->doc->root) : 1;
        if (tmp) arena_free(tmp);
    }
    ua_walk(d, d->doc->root, WS_NORMAL, 400);

    /* env lives in the arena: lay_run() keeps the pointer, and the arena
     * outlives the lay_doc (eng_free releases them in that order). */
    d->env.root         = d->doc->root;
    d->env.first_child  = eng_first_child;
    d->env.next_sibling = eng_next_sibling;
    d->env.style_of     = eng_style_of;
    d->env.text_of      = eng_text_of;
    d->env.text_width   = eng_text_width;
    d->env.font_ascent  = eng_font_ascent;
    d->env.font_descent = eng_font_descent;
    d->env.font_line_gap = eng_font_line_gap;

    d->lay = lay_run(&d->env, viewport_w, viewport_h);
    return d;
}

void eng_free(eng_doc *d)
{
    arena *a;
    if (!d) return;
    lay_free(d->lay);        /* reads d->env, so it must precede arena_free */
    a = d->a;                /* d itself lives in the arena */
    arena_free(a);
}

/* ============================================================== selectors */

static void query_walk(sel_list *s, dom_node *n, eng_node **out, int max,
                       int *cnt)
{
    dom_node *ch;

    if (!n || *cnt >= max) return;
    if (n->type == DOM_ELEMENT && sel_matches(s, n))
        out[(*cnt)++] = (eng_node *)n;      /* the handle IS the dom_node */
    for (ch = n->first_child; ch; ch = ch->next_sibling) {
        if (*cnt >= max) return;
        query_walk(s, ch, out, max, cnt);
    }
}

int eng_query(eng_doc *d, const char *selector, eng_node *root,
              eng_node **out, int max)
{
    /* A scratch arena, so repeated queries on one document do not accumulate
     * in the document's arena. */
    arena *tmp;
    sel_list *s;
    int cnt = 0;

    if (!d || !selector || !out) return -1;

    /* Validity is a property of the selector alone, so parse before honouring
     * `max` or an empty document: -1 must mean "not valid CSS" and nothing
     * else, or the front end reports `invalid selector` for a valid one. */
    tmp = arena_new();
    if (!tmp) return -1;
    s = sel_parse(tmp, selector);
    if (!s) {                       /* not valid CSS 2.1 -> invalid selector */
        arena_free(tmp);
        return -1;
    }
    if (max <= 0 || !d->doc || !d->doc->root) {
        arena_free(tmp);
        return 0;
    }

    if (root) {
        /* Find Elements From Element matches strict DESCENDANTS of root. */
        dom_node *r = (dom_node *)root, *ch;
        for (ch = r->first_child; ch; ch = ch->next_sibling)
            query_walk(s, ch, out, max, &cnt);
    } else {
        query_walk(s, d->doc->root, out, max, &cnt);
    }

    arena_free(tmp);
    return cnt;                     /* number WRITTEN, per engine.h */
}

/* ================================================================== rects */

void eng_rect(eng_doc *d, eng_node *n, double *x, double *y,
              double *w, double *h)
{
    lay_box *b = NULL;

    if (d && d->lay && n) b = lay_box_for(d->lay, (void *)n);
    /* An element that generates no box reports 0,0,0,0. */
    if (x) *x = b ? b->x : 0.0;
    if (y) *y = b ? b->y : 0.0;
    if (w) *w = b ? b->w : 0.0;
    if (h) *h = b ? b->h : 0.0;
}

/* ========================================================= resolved values */

/* Copy values_box out of the published lay_box.  Every field is a used value
 * layout solved for, so nothing here re-derives one.
 * Returns 1 when the element generates a box. */
static int fill_box(eng_doc *d, dom_node *n, values_box *vb)
{
    const lay_box *b;
    const computed *c;
    int i;

    memset(vb, 0, sizeof *vb);
    if (!d || !d->lay || !n) return 0;
    b = lay_box_for(d->lay, (void *)n);
    if (!b) return 0;

    c = (const computed *)n->style;
    if (!c) c = b->st;
    if (!c) return 0;

    /* display:none generates no box; CSSOM then reports computed values, so
     * report "no box" rather than a degenerate all-zero one. */
    if (c->display == D_NONE) return 0;

    vb->x = b->x;  vb->y = b->y;  vb->w = b->w;  vb->h = b->h;
    vb->content_w = b->content_w;
    vb->content_h = b->content_h;

    for (i = 0; i < 4; i++) {
        vb->margin[i]  = b->margin[i];
        vb->padding[i] = b->padding[i];
        vb->border[i]  = b->border[i];
        /* Zero for a static box; values_resolved() reads it only when the box
         * is positioned, and then it is the used offset (§9.4.3, §10.3.7). */
        vb->inset[i]   = b->inset[i];
    }

    vb->has_box = 1;
    return 1;
}

int eng_css(eng_doc *d, eng_node *n, const char *prop, char *buf, size_t buflen)
{
    values_box vb;

    if (!buf || buflen == 0) return -1;
    buf[0] = '\0';

    /* values_resolved() treats a box with has_box == 0 as "no box", which is
     * what an element that generates none must report. */
    (void)fill_box(d, (dom_node *)n, &vb);
    return values_resolved((dom_node *)n, &vb, prop, buf, buflen);
}
