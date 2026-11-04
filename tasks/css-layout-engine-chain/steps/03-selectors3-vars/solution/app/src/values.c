/* values.c -- computed values (CSS 2.1 section 6 + the level-3 pieces this
 * milestone needs) and CSSOM resolved-value serialisation.
 *
 * C11, libc only.  <math.h> is deliberately NOT included: style.h defines a
 * `float_t` enum, which would collide with C99's math.h typedef of the same
 * name.
 */
#include "values.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strncasecmp, used by the calc() parser */
#include <ctype.h>

/* ------------------------------------------------------------------ misc */

#define VS_MAGIC 0x56414c55u            /* 'V','A','L','U' */

/* Border styles.  `computed` has nowhere to put these, but CSSOM has to
 * serialise them and the used border width depends on them. */
enum { BST_NONE = 0, BST_HIDDEN, BST_DOTTED, BST_DASHED, BST_SOLID,
       BST_DOUBLE, BST_GROOVE, BST_RIDGE, BST_INSET, BST_OUTSET };

static const char *const g_bstyle_name[] = {
    "none", "hidden", "dotted", "dashed", "solid",
    "double", "groove", "ridge", "inset", "outset"
};

/* The record values_compute() hangs off dom_node::style.  `c` MUST stay the
 * first member: node->style points straight at it, so a plain `computed *`
 * view of the node keeps working for layout, and values_resolved() recovers
 * the private tail by casting back. */
typedef struct vstyle {
    computed c;
    unsigned magic;
    const char *display_kw;     /* static string, or NULL */
    double bw_stated[4];        /* border width before style:none zeroing */
    unsigned char bstyle[4];
    int lh_factor_set;          /* line-height was a <number> */
    double lh_factor;
    int va_pct_set;             /* vertical-align was a <percentage> */
    double va_pct;
} vstyle;

typedef struct {
    arena *a;
    cascade *cas;
    double vw, vh;
    double root_fs;
    const struct cprop *cps;   /* custom properties in scope */
} vctx;

struct cprop;   /* css-variables-1; defined below */

/* Length resolution context for one element. */
typedef struct { double fs, root_fs, vw, vh, xh; } lenctx;

enum { LF_AUTO = 1u, LF_NONE = 2u, LF_NEG = 4u, LF_PCT = 8u,
       LF_STRETCH = 16u };

typedef struct { const char *s; size_t n; } vtok;

static int finite_d(double v) { return (v == v) && v < 1e308 && v > -1e308; }

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

static int is_ws(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* s[0..n) against a NUL-terminated ASCII keyword, case-insensitively. */
static int tok_is(vtok t, const char *kw)
{
    size_t i;
    for (i = 0; i < t.n; i++) {
        if (kw[i] == '\0' || lc((unsigned char)t.s[i]) != lc((unsigned char)kw[i]))
            return 0;
    }
    return kw[t.n] == '\0';
}

static int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (lc((unsigned char)*a) != lc((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Split a declaration value into whitespace-separated tokens.  Returns 0 when
 * there are more than `max` of them (the caller treats that as invalid). */
static int split_tokens(const char *v, vtok *out, int max, int *count)
{
    const char *p = v;
    int k = 0;

    if (!v) return 0;
    for (;;) {
        while (*p && is_ws((unsigned char)*p)) p++;
        if (!*p) break;
        if (k >= max) return 0;
        out[k].s = p;
        while (*p && !is_ws((unsigned char)*p)) p++;
        out[k].n = (size_t)(p - out[k].s);
        k++;
    }
    *count = k;
    return 1;
}

/* ------------------------------------------------------------- numbers */

/* Length of a valid CSS <number> prefix of s[0..n), or 0 if there is none.
 * Deliberately hand-rolled: strtod() alone would accept "0x10" as hex and
 * "infinity" as a number, neither of which is a CSS number. */
static size_t scan_number(const char *s, size_t n)
{
    size_t i = 0, digits = 0, j, ed;

    if (i < n && (s[i] == '+' || s[i] == '-')) i++;
    while (i < n && s[i] >= '0' && s[i] <= '9') { i++; digits++; }
    if (i < n && s[i] == '.') {
        size_t k = i + 1, fd = 0;
        while (k < n && s[k] >= '0' && s[k] <= '9') { k++; fd++; }
        if (fd > 0) { i = k; digits += fd; }
    }
    if (digits == 0) return 0;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        j = i + 1;
        ed = 0;
        if (j < n && (s[j] == '+' || s[j] == '-')) j++;
        while (j < n && s[j] >= '0' && s[j] <= '9') { j++; ed++; }
        if (ed > 0) i = j;       /* otherwise the 'e' starts the unit */
    }
    return i;
}

/* Parse s[0..n) as a CSS number.  n has already been produced by
 * scan_number(), so strtod() can only see a valid decimal here. */
static int number_value(const char *s, size_t n, double *out)
{
    char tmp[80];
    char *end;
    double v;

    if (n == 0 || n >= sizeof tmp) return 0;   /* bounds check before the copy */
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    v = strtod(tmp, &end);
    if (end != tmp + n) return 0;
    if (!finite_d(v)) return 0;
    *out = v;
    return 1;
}

static int parse_num_tok(vtok t, double *out)
{
    size_t k = scan_number(t.s, t.n);
    if (k == 0 || k != t.n) return 0;
    return number_value(t.s, k, out);
}

static int parse_int_tok(vtok t, int *out)
{
    size_t i = 0;
    long sign = 1, v = 0;
    size_t digits = 0;

    if (t.n == 0) return 0;
    if (t.s[0] == '+' || t.s[0] == '-') { if (t.s[0] == '-') sign = -1; i = 1; }
    for (; i < t.n; i++) {
        if (t.s[i] < '0' || t.s[i] > '9') return 0;
        if (v < 100000000L) v = v * 10 + (t.s[i] - '0');
        digits++;
    }
    if (digits == 0) return 0;
    *out = (int)(sign * v);
    return 1;
}

/* Defined below, but called from apply_font_size / apply_line_height /
 * apply_vertical_align, which appear earlier. */
static int parse_calc(const char *v, const lenctx *L, unsigned flags, length *out);

/* ------------------------------------------------------------- lengths */

static double unit_px(vtok u, double v, const lenctx *L, int *ok)
{
    double vmin = (L->vw < L->vh) ? L->vw : L->vh;
    double vmax = (L->vw > L->vh) ? L->vw : L->vh;

    *ok = 1;
    if (tok_is(u, "px"))   return v;
    if (tok_is(u, "em"))   return v * L->fs;
    /* 4.3.2: `ex` is the x-height of the element's own first font. */
    if (tok_is(u, "ex"))   return v * L->xh;
    if (tok_is(u, "rem"))  return v * L->root_fs;
    if (tok_is(u, "vw"))   return v * L->vw / 100.0;
    if (tok_is(u, "vh"))   return v * L->vh / 100.0;
    if (tok_is(u, "vmin")) return v * vmin / 100.0;
    if (tok_is(u, "vmax")) return v * vmax / 100.0;
    /* CSS 2.1 absolute units, exact at the reference 96dpi. */
    if (tok_is(u, "in"))   return v * 96.0;
    if (tok_is(u, "cm"))   return v * 96.0 / 2.54;
    if (tok_is(u, "mm"))   return v * 96.0 / 25.4;
    if (tok_is(u, "q"))    return v * 96.0 / 101.6;
    if (tok_is(u, "pt"))   return v * 96.0 / 72.0;
    if (tok_is(u, "pc"))   return v * 16.0;
    *ok = 0;
    return 0;
}

/* One token -> a `length`.  `flags` says which non-length forms are legal. */
static int parse_length(vtok t, const lenctx *L, unsigned flags, length *out)
{
    size_t k;
    double v;
    vtok unit;
    int ok;

    if ((flags & LF_AUTO) && tok_is(t, "auto")) {
        out->kind = LEN_AUTO; out->v = 0; return 1;
    }
    if ((flags & LF_NONE) && tok_is(t, "none")) {
        out->kind = LEN_NONE; out->v = 0; return 1;
    }
    /* css-sizing-3: `stretch` fills the containing block; `-webkit-fill-
     * available` is the same thing under its older name. */
    if ((flags & LF_STRETCH) &&
        (tok_is(t, "stretch") || tok_is(t, "-webkit-fill-available"))) {
        out->kind = LEN_STRETCH; out->v = 0; return 1;
    }
    k = scan_number(t.s, t.n);
    if (k == 0) return 0;
    if (!number_value(t.s, k, &v)) return 0;
    unit.s = t.s + k;
    unit.n = t.n - k;

    if (unit.n == 0) {
        if (v != 0.0) return 0;             /* only 0 may be unitless */
        out->kind = LEN_PX; out->v = 0; return 1;
    }
    if (unit.n == 1 && unit.s[0] == '%') {
        if (!(flags & LF_PCT)) return 0;
        if (v < 0 && !(flags & LF_NEG)) return 0;
        out->kind = LEN_PCT; out->v = v; return 1;
    }
    v = unit_px(unit, v, L, &ok);
    if (!ok) return 0;
    if (v < 0 && !(flags & LF_NEG)) return 0;
    if (!finite_d(v)) return 0;
    out->kind = LEN_PX;
    out->v = v;
    return 1;
}

/* --------------------------------------------------------- keyword maps */

typedef struct { const char *kw; int v; } kwmap;

static int kw_find(const kwmap *m, vtok t, int *out)
{
    size_t i;
    for (i = 0; m[i].kw; i++) {
        if (tok_is(t, m[i].kw)) { *out = m[i].v; return 1; }
    }
    return 0;
}

static const kwmap g_position[] = {
    {"static", P_STATIC}, {"relative", P_RELATIVE},
    {"absolute", P_ABSOLUTE}, {"fixed", P_FIXED}, {NULL, 0}
};
static const kwmap g_float[] = {
    {"none", F_NONE}, {"left", F_LEFT}, {"right", F_RIGHT}, {NULL, 0}
};
static const kwmap g_clear[] = {
    {"none", C_NONE}, {"left", C_LEFT}, {"right", C_RIGHT},
    {"both", C_BOTH}, {NULL, 0}
};
static const kwmap g_overflow[] = {
    {"visible", OV_VISIBLE}, {"hidden", OV_HIDDEN},
    {"scroll", OV_SCROLL}, {"auto", OV_AUTO}, {NULL, 0}
};
static const kwmap g_boxsizing[] = {
    {"content-box", BS_CONTENT}, {"border-box", BS_BORDER}, {NULL, 0}
};
static const kwmap g_direction[] = {
    {"ltr", DIR_LTR}, {"rtl", DIR_RTL}, {NULL, 0}
};
/* `start`/`end` are CSS 3 keywords with no slot in textalign_t; they are
 * mapped to their LTR meaning and lose their keyword identity. */
static const kwmap g_textalign[] = {
    {"left", TA_LEFT}, {"right", TA_RIGHT}, {"center", TA_CENTER},
    {"justify", TA_JUSTIFY}, {"start", TA_LEFT}, {"end", TA_RIGHT}, {NULL, 0}
};
static const kwmap g_whitespace[] = {
    {"normal", WS_NORMAL}, {"pre", WS_PRE}, {"nowrap", WS_NOWRAP},
    {"pre-wrap", WS_PRE_WRAP}, {"pre-line", WS_PRE_LINE}, {NULL, 0}
};
static const kwmap g_valign[] = {
    {"baseline", VA_BASELINE}, {"top", VA_TOP}, {"bottom", VA_BOTTOM},
    {"middle", VA_MIDDLE}, {"sub", VA_SUB}, {"super", VA_SUPER},
    {"text-top", VA_TEXT_TOP}, {"text-bottom", VA_TEXT_BOTTOM}, {NULL, 0}
};
static const kwmap g_bstyle[] = {
    {"none", BST_NONE}, {"hidden", BST_HIDDEN}, {"dotted", BST_DOTTED},
    {"dashed", BST_DASHED}, {"solid", BST_SOLID}, {"double", BST_DOUBLE},
    {"groove", BST_GROOVE}, {"ridge", BST_RIDGE}, {"inset", BST_INSET},
    {"outset", BST_OUTSET}, {NULL, 0}
};

/* display: the keywords display_t can hold, then the level-3 outer/inner
 * types it cannot.  Those keep their spelling for CSSOM (`kw`) and take the
 * nearest representable box type so layout still does something sensible. */
static const struct { const char *kw; display_t d; int keep; } g_display[] = {
    {"none", D_NONE, 0}, {"block", D_BLOCK, 0}, {"inline", D_INLINE, 0},
    {"inline-block", D_INLINE_BLOCK, 0}, {"list-item", D_LIST_ITEM, 0},
    {"table", D_TABLE, 0}, {"inline-table", D_INLINE_TABLE, 0},
    {"table-row", D_TABLE_ROW, 0}, {"table-cell", D_TABLE_CELL, 0},
    {"table-row-group", D_TABLE_ROW_GROUP, 0},
    {"table-header-group", D_TABLE_HEADER_GROUP, 0},
    {"table-footer-group", D_TABLE_FOOTER_GROUP, 0},
    {"table-column", D_TABLE_COLUMN, 0},
    {"table-column-group", D_TABLE_COLUMN_GROUP, 0},
    {"table-caption", D_TABLE_CAPTION, 0},
    {"flex", D_BLOCK, 1}, {"grid", D_BLOCK, 1}, {"flow-root", D_BLOCK, 1},
    {"inline-flex", D_INLINE_BLOCK, 1}, {"inline-grid", D_INLINE_BLOCK, 1},
    {"contents", D_INLINE, 1},
    {NULL, D_INLINE, 0}
};

static const char *display_name(display_t d)
{
    size_t i;
    for (i = 0; g_display[i].kw; i++)
        if (!g_display[i].keep && g_display[i].d == d) return g_display[i].kw;
    return "inline";
}

static const char *kw_name(const kwmap *m, int v)
{
    size_t i;
    for (i = 0; m[i].kw; i++)
        if (m[i].v == v) return m[i].kw;
    return "";
}

/* ------------------------------------------------------- initial values */

static void init_style(vstyle *vs, const vstyle *par)
{
    computed *c = &vs->c;
    int i;

    memset(vs, 0, sizeof *vs);
    vs->magic = VS_MAGIC;

    c->display = D_INLINE;
    c->position = P_STATIC;
    c->cfloat = F_NONE;
    c->clear = C_NONE;
    c->overflow_x = c->overflow_y = OV_VISIBLE;
    c->box_sizing = BS_CONTENT;
    c->vertical_align = VA_BASELINE;
    c->vertical_align_px = 0;

    c->width.kind = c->height.kind = LEN_AUTO;
    /* CSS 2.1 initial min-width/min-height is 0, not `auto`; LEN_AUTO is only
     * the zero of the enum.  An explicit `min-width: auto` still lands on
     * LEN_AUTO below. */
    c->min_width.kind = c->min_height.kind = LEN_PX;
    c->min_width.v = c->min_height.v = 0;
    c->max_width.kind = c->max_height.kind = LEN_NONE;

    for (i = 0; i < 4; i++) {
        c->margin[i].kind = LEN_PX;  c->margin[i].v = 0;
        c->padding[i].kind = LEN_PX; c->padding[i].v = 0;
        c->inset[i].kind = LEN_AUTO; c->inset[i].v = 0;
        c->border[i] = 0;            /* border-style: none => used width 0 */
        vs->bw_stated[i] = 3.0;      /* border-width initial is `medium` */
        vs->bstyle[i] = BST_NONE;
    }

    c->z_index_auto = 1;
    c->z_index = 0;
    c->is_replaced = 0;
    c->intrinsic_w = c->intrinsic_h = 0;

    /* Inherited: direction, font-size, line-height, text-align, white-space
     * (CSS 2.1 Appendix F).  Nothing else here inherits. */
    if (par) {
        c->direction = par->c.direction;
        c->text_align = par->c.text_align;
        c->white_space = par->c.white_space;
        c->font_size = par->c.font_size;
        c->line_height = par->c.line_height;
        c->font_family = par->c.font_family;
        c->font_weight = par->c.font_weight;
        c->text_indent = par->c.text_indent;
        vs->lh_factor_set = par->lh_factor_set;
        vs->lh_factor = par->lh_factor;
    } else {
        c->direction = DIR_LTR;
        c->text_align = TA_LEFT;
        c->white_space = WS_NORMAL;
        c->font_size = 16.0;
        c->line_height = VALUES_LINE_HEIGHT_NORMAL;
        c->font_family = FF_DEFAULT;
        c->font_weight = 400;
        c->text_indent.kind = LEN_PX;   /* 16.1 initial value: 0 */
        c->text_indent.v = 0;
    }
}


/* ------------------------------------------------------- custom properties
 *
 * css-variables-1.  A custom property is any declaration whose name begins with
 * two dashes.  They inherit, so each element's list links to its parent's and a
 * lookup walks the chain; nothing is copied.
 */
typedef struct cprop { const char *name, *value; struct cprop *next; } cprop;

static const char *cp_lookup(const cprop *list, const char *name, size_t n)
{
    for (; list; list = list->next)
        if (strncmp(list->name, name, n) == 0 && list->name[n] == '\0')
            return list->value;
    return NULL;
}

/* Substitute every var(--name[, fallback]) in `v`.  Returns `v` unchanged when
 * it contains none, so the common path allocates nothing. */
static const char *subst_var(vctx *K, const char *v, const cprop *cps, int depth)
{
    const char *p; strbuf sb; int any = 0;

    if (!v || depth > 8 || !strstr(v, "var(")) return v;
    sb_init(&sb, K->a);
    for (p = v; *p; ) {
        if (strncmp(p, "var(", 4) != 0) { sb_putc(&sb, *p++); continue; }
        {
            const char *q = p + 4, *name, *fb = NULL;
            size_t nlen = 0, fblen = 0; int par = 1;
            while (*q == ' ') q++;
            name = q;
            while (*q && *q != ',' && *q != ')') { q++; nlen++; }
            while (nlen && name[nlen - 1] == ' ') nlen--;
            if (*q == ',') {
                fb = ++q;
                while (*q && par) {
                    if (*q == '(') par++;
                    else if (*q == ')') { if (--par == 0) break; }
                    q++; fblen++;
                }
            } else {
                while (*q && *q != ')') q++;
            }
            if (*q == ')') q++;
            {
                const char *rep = cp_lookup(cps, name, nlen);
                if (rep) sb_puts(&sb, rep);
                else if (fb) sb_putn(&sb, fb, fblen);
                any = 1;
            }
            p = q;
        }
    }
    if (!any) return v;
    /* a substituted value may itself contain var() */
    return subst_var(K, sb.p ? sb.p : "", cps, depth + 1);
}

static const cprop *collect_cprops(vctx *K, dom_node *n, const cprop *inherited)
{
    const char **names = cascade_props(K->cas, n);
    const cprop *head = inherited;
    int i;

    for (i = 0; names && names[i]; i++) {
        if (names[i][0] == '-' && names[i][1] == '-') {
            int imp = 0;
            const char *v = cascade_value(K->cas, n, names[i], &imp);
            if (v) {
                cprop *c = (cprop *)arena_alloc(K->a, sizeof *c);
                c->name = names[i];
                c->value = v;
                c->next = (cprop *)head;   /* nearest declaration wins */
                head = c;
            }
        }
    }
    return head;
}

/* ------------------------------------------------------ applying values */

static const char *cval(vctx *K, dom_node *n, const char *prop, int *imp)
{
    int dummy = 0;
    const char *v = cascade_value(K->cas, n, prop, imp ? imp : &dummy);
    if (v && *v && K->cps) v = subst_var(K, v, K->cps, 0);
    return (v && *v) ? v : NULL;
}

/* A declaration only applies in the pass matching its weight: this is how the
 * shorthand-before-longhand ordering below stays honest about !important. */
static const char *cval_lvl(vctx *K, dom_node *n, const char *prop, int lvl)
{
    int imp = 0;
    const char *v = cval(K, n, prop, &imp);
    return (v && imp == lvl) ? v : NULL;
}

/* x-height as a fraction of the em, per face: the OS/2 sxHeight of Ahem (800
 * of 1000) and of the standard font (940 of 2048).  CSS 2.1 4.3.2 allows
 * 0.5em where the x-height cannot be determined; it can be, so it is used. */
static double xheight_ratio(fontfamily_t f)
{
    return (f == FF_AHEM) ? 0.8 : 940.0 / 2048.0;
}

/* The CSS 2.1 absolute-size keywords, shared by `font-size` and `font`. */
static const kwmap g_abs_size[] = {
    {"xx-small", 9}, {"x-small", 10}, {"small", 13}, {"medium", 16},
    {"large", 18}, {"x-large", 24}, {"xx-large", 32}, {NULL, 0}
};

/* font-size: em/% against the PARENT's size, rem against the root's. */
static void apply_font_size(vctx *K, dom_node *n, vstyle *vs, const vstyle *par)
{
    const char *v = cval(K, n, "font-size", NULL);
    double parent_fs = par ? par->c.font_size : 16.0;
    vtok t[2];
    int cnt = 0, px = 0;
    lenctx L;
    length l;

    if (!v) return;
    if (!split_tokens(v, t, 2, &cnt) || cnt != 1) return;
    if (kw_find(g_abs_size, t[0], &px)) { vs->c.font_size = px; return; }

    L.fs = parent_fs;                        /* em/% resolve against the parent */
    L.root_fs = K->root_fs;
    L.vw = K->vw;
    L.vh = K->vh;
    L.xh = parent_fs * xheight_ratio(par ? par->c.font_family : FF_DEFAULT);
    if (!parse_length(t[0], &L, LF_PCT, &l)) return;   /* no auto/none/negative */
    if (l.kind == LEN_PCT) vs->c.font_size = parent_fs * l.v / 100.0;
    else if (l.kind == LEN_CALC) vs->c.font_size = parent_fs * l.pct / 100.0 + l.v;
    else if (l.kind == LEN_PX) vs->c.font_size = l.v;
}

/* ------------------------------------------------------------------ fonts */

/* A font-family list names faces in preference order.  This engine carries
 * metrics for Ahem and for the UA's standard font, so a list is read until a
 * name it knows appears; anything else resolves to the standard font, which is
 * also what an unmatched list falls back to. */
static fontfamily_t family_from_list(const char *v)
{
    const char *p = v;

    if (!v) return FF_DEFAULT;
    while (*p) {
        const char *s;
        size_t n;
        vtok t;

        while (*p && (is_ws((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            s = p;
            while (*p && *p != q) p++;
            n = (size_t)(p - s);
            if (*p) p++;
        } else {
            s = p;
            while (*p && *p != ',') p++;
            n = (size_t)(p - s);
            while (n > 0 && is_ws((unsigned char)s[n - 1])) n--;
        }
        t.s = s; t.n = n;
        if (tok_is(t, "ahem")) return FF_AHEM;
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return FF_DEFAULT;
}

/* font-weight: the absolute forms only.  `bolder`/`lighter` are relative to
 * the inherited weight (CSS 2.1 15.6) and are deliberately not guessed at. */
static int weight_from(const char *v)
{
    vtok t[2];
    int cnt = 0;
    double num;

    if (!v || !split_tokens(v, t, 2, &cnt) || cnt != 1) return -1;
    if (tok_is(t[0], "bold")) return 700;
    if (tok_is(t[0], "normal")) return 400;
    if (parse_num_tok(t[0], &num) && num >= 100 && num <= 900) return (int)num;
    return -1;
}

/* Split a `font` value on white space, with '/' delimited as a token of its
 * own so that "20px/1" reads as the three components it is. */
static int font_tokens(const char *v, vtok *out, int max, int *count)
{
    const char *p = v;
    int k = 0;

    if (!v) return 0;
    for (;;) {
        while (*p && is_ws((unsigned char)*p)) p++;
        if (!*p) break;
        if (k >= max) return 0;
        if (*p == '/') { out[k].s = p; out[k].n = 1; k++; p++; continue; }
        out[k].s = p;
        while (*p && !is_ws((unsigned char)*p) && *p != '/') p++;
        out[k].n = (size_t)(p - out[k].s);
        k++;
    }
    *count = k;
    return 1;
}

/* CSS 2.1 15.7 -- font: [ <style> || <variant> || <weight> ]? <size>
 * [ / <line-height> ]? <family>.  A shorthand sets every subproperty it can
 * carry, so the ones it does not name are reset to their initial values; that
 * is why it runs BEFORE the longhands, which then override it. */
static void apply_font_shorthand(vctx *K, dom_node *n, vstyle *vs,
                                 const vstyle *par)
{
    const char *v = cval(K, n, "font", NULL);
    double parent_fs = par ? par->c.font_size : 16.0;
    vtok t[24];
    int cnt = 0, i = 0, px = 0, weight = 400;
    lenctx L;
    length l;
    double num, fs;

    if (!v || !font_tokens(v, t, 24, &cnt) || cnt < 1) return;

    if (cnt == 1 && tok_is(t[0], "inherit")) {
        if (!par) return;
        vs->c.font_size = par->c.font_size;
        vs->c.line_height = par->c.line_height;
        vs->lh_factor_set = par->lh_factor_set;
        vs->lh_factor = par->lh_factor;
        vs->c.font_family = par->c.font_family;
        vs->c.font_weight = par->c.font_weight;
        return;
    }

    /* the optional style / variant / weight group, in any order */
    for (; i < cnt; i++) {
        if (tok_is(t[i], "normal") || tok_is(t[i], "italic") ||
            tok_is(t[i], "oblique") || tok_is(t[i], "small-caps")) continue;
        if (tok_is(t[i], "bold")) { weight = 700; continue; }
        if (parse_num_tok(t[i], &num) && num >= 100 && num <= 900) {
            weight = (int)num;
            continue;
        }
        break;
    }
    if (i >= cnt) return;                 /* <font-size> is not optional */

    L.fs = parent_fs;                     /* em/% resolve against the parent */
    L.root_fs = K->root_fs;
    L.vw = K->vw;
    L.vh = K->vh;
    L.xh = parent_fs * xheight_ratio(par ? par->c.font_family : FF_DEFAULT);
    if (kw_find(g_abs_size, t[i], &px)) {
        fs = px;
    } else if (parse_length(t[i], &L, LF_PCT, &l)) {
        if (l.kind == LEN_PCT) fs = parent_fs * l.v / 100.0;
        else if (l.kind == LEN_CALC) fs = parent_fs * l.pct / 100.0 + l.v;
        else if (l.kind == LEN_PX) fs = l.v;
        else return;
    } else {
        return;                           /* not a font-size: the whole value
                                             is invalid and drops */
    }
    i++;

    vs->c.font_size = fs;
    vs->c.font_weight = weight;
    vs->c.line_height = VALUES_LINE_HEIGHT_NORMAL;   /* reset by the shorthand */
    vs->lh_factor_set = 0;

    if (i + 1 < cnt && t[i].n == 1 && t[i].s[0] == '/') {
        i++;
        L.fs = fs;                        /* now the element's OWN size */
        L.xh = fs * xheight_ratio(vs->c.font_family);
        if (tok_is(t[i], "normal")) {
            /* already reset above */
        } else if (parse_num_tok(t[i], &num)) {
            if (num >= 0) {
                vs->lh_factor_set = 1;
                vs->lh_factor = num;
                vs->c.line_height = num * fs;
            }
        } else if (parse_length(t[i], &L, LF_PCT, &l)) {
            if (l.kind == LEN_PCT) vs->c.line_height = fs * l.v / 100.0;
            else if (l.kind == LEN_CALC) vs->c.line_height = fs * l.pct / 100.0 + l.v;
            else if (l.kind == LEN_PX) vs->c.line_height = l.v;
        }
        i++;
    }

    /* <font-family>: the rest of the declaration, comma separated. */
    if (i < cnt) vs->c.font_family = family_from_list(t[i].s);
}

static void apply_font_family(vctx *K, dom_node *n, vstyle *vs)
{
    const char *v = cval(K, n, "font-family", NULL);
    if (v) vs->c.font_family = family_from_list(v);
}

static void apply_font_weight(vctx *K, dom_node *n, vstyle *vs)
{
    int w = weight_from(cval(K, n, "font-weight", NULL));
    if (w > 0) vs->c.font_weight = w;
}

static void apply_line_height(vctx *K, dom_node *n, vstyle *vs, const lenctx *L)
{
    const char *v;
    vtok t[2];
    int cnt = 0;
    double num;
    length l;

    /* An inherited <number> is re-multiplied by THIS element's font-size. */
    if (vs->lh_factor_set) vs->c.line_height = vs->lh_factor * vs->c.font_size;

    v = cval(K, n, "line-height", NULL);
    if (!v) return;
    /* calc() is whitespace-separated, so it must be tried BEFORE split_tokens,
       which would make cnt != 1 and return. */
    if (parse_calc(v, L, LF_PCT, &l)) {
        if (l.kind == LEN_CALC)
            vs->c.line_height = vs->c.font_size * l.pct / 100.0 + l.v;
        else
            vs->c.line_height = l.v;
        vs->lh_factor_set = 0;
        return;
    }
    if (!split_tokens(v, t, 2, &cnt) || cnt != 1) return;

    if (tok_is(t[0], "normal")) {
        vs->c.line_height = VALUES_LINE_HEIGHT_NORMAL;
        vs->lh_factor_set = 0;
        return;
    }
    if (parse_num_tok(t[0], &num)) {         /* bare <number>: a factor */
        if (num < 0) return;
        vs->lh_factor_set = 1;
        vs->lh_factor = num;
        vs->c.line_height = num * vs->c.font_size;
        return;
    }
    /* A calc() is whitespace-separated, so split_tokens above would have made
       cnt != 1 and returned.  Try the RAW value first, as apply_len_single does. */
    if (!parse_length(t[0], L, LF_PCT, &l)) return;
    if (l.kind == LEN_PCT) {                 /* % of this element's font-size */
        vs->c.line_height = vs->c.font_size * l.v / 100.0;
        vs->lh_factor_set = 0;
    } else if (l.kind == LEN_CALC) {         /* calc(): % of font-size, plus px */
        vs->c.line_height = vs->c.font_size * l.pct / 100.0 + l.v;
        vs->lh_factor_set = 0;
    } else if (l.kind == LEN_PX) {
        vs->c.line_height = l.v;
        vs->lh_factor_set = 0;
    }
}

/* 1/2/3/4-value shorthand expansion -> top, right, bottom, left. */
static int expand_sides(const char *v, vtok out[4])
{
    vtok t[5];
    int cnt = 0;

    if (!split_tokens(v, t, 5, &cnt) || cnt < 1 || cnt > 4) return 0;
    switch (cnt) {
    case 1: out[0] = out[1] = out[2] = out[3] = t[0]; break;
    case 2: out[0] = out[2] = t[0]; out[1] = out[3] = t[1]; break;
    case 3: out[0] = t[0]; out[1] = out[3] = t[1]; out[2] = t[2]; break;
    default: out[0] = t[0]; out[1] = t[1]; out[2] = t[2]; out[3] = t[3]; break;
    }
    return 1;
}

/* A shorthand whose components do not all parse is invalid as a whole. */
static void apply_len_shorthand(const char *v, const lenctx *L, unsigned flags,
                                length dst[4])
{
    vtok t[4];
    length l[4];
    int i;

    if (!v || !expand_sides(v, t)) return;
    for (i = 0; i < 4; i++)
        if (!parse_length(t[i], L, flags, &l[i])) return;
    for (i = 0; i < 4; i++) dst[i] = l[i];
}


/* ------------------------------------------------------------------ calc()
 *
 * css-values-3 §8.  A calc() expression reduces to `v px + pct %`: the two
 * parts cannot be combined until the containing block is known, so both are
 * carried in `length` and resolved together in lay_res().
 *
 * The value arrives as a raw declaration string, not as tokens: split_tokens()
 * splits on whitespace, so `calc(100px + 50px)` would arrive as three tokens
 * and be rejected.  Parsing therefore happens before tokenization.
 */
/* `isnum` marks a bare <number>: css-values-3 allows multiplication only
 * by a number, and division only by a number, so the two must not be
 * confused with a length whose value happens to be unitless. */
typedef struct { double px, pct; int ok, isnum; } calcv;

static const char *cc_ws(const char *p) { while (*p && is_ws((unsigned char)*p)) p++; return p; }

static calcv cc_expr(const char **pp, const lenctx *L, int depth);

static calcv cc_factor(const char **pp, const lenctx *L, int depth)
{
    calcv r; const char *p = cc_ws(*pp); size_t k; double v; vtok u; int ok;
    r.px = r.pct = 0.0; r.ok = 0; r.isnum = 0;
    if (depth > 16) { *pp = p; return r; }
    if (*p == '(') {
        p++; r = cc_expr(&p, L, depth + 1); p = cc_ws(p);
        if (*p != ')') { r.ok = 0; *pp = p; return r; }
        p++; *pp = p; return r;
    }
    if (!strncasecmp(p, "calc(", 5)) {
        p += 5; r = cc_expr(&p, L, depth + 1); p = cc_ws(p);
        if (*p != ')') { r.ok = 0; *pp = p; return r; }
        p++; *pp = p; return r;
    }
    k = scan_number(p, strlen(p));
    if (k == 0) { *pp = p; return r; }
    if (!number_value(p, k, &v)) { *pp = p; return r; }
    p += k;
    if (*p == '%') { r.pct = v; r.ok = 1; r.isnum = 0; p++; *pp = p; return r; }
    u.s = p; u.n = 0;
    while (p[u.n] && (isalpha((unsigned char)p[u.n]))) u.n++;
    if (u.n == 0) { r.px = v; r.ok = 1; r.isnum = 1; *pp = p; return r; }  /* <number> */
    v = unit_px(u, v, L, &ok);
    if (!ok) { *pp = p + u.n; return r; }
    r.px = v; r.ok = 1; r.isnum = 0; *pp = p + u.n;
    return r;
}

static calcv cc_term(const char **pp, const lenctx *L, int depth)
{
    calcv a = cc_factor(pp, L, depth), b; const char *p;
    while (a.ok) {
        p = cc_ws(*pp);
        if (*p != '*' && *p != '/') break;
        {   char op = *p; p++; *pp = p;
            b = cc_factor(pp, L, depth);
            if (!b.ok) { a.ok = 0; break; }
            if (op == '*') {
                /* §8.1: at least one operand must be a <number>. */
                if (b.isnum)      { a.px *= b.px; a.pct *= b.px; }
                else if (a.isnum) { double k = a.px;
                                    a.px = b.px * k; a.pct = b.pct * k;
                                    a.isnum = b.isnum; }
                else              { a.ok = 0; break; }
            } else {
                /* §8.1: the right operand of / must be a <number>, non-zero. */
                if (!b.isnum || b.px == 0.0) { a.ok = 0; break; }
                a.px /= b.px; a.pct /= b.px;
            }
        }
    }
    return a;
}

static calcv cc_expr(const char **pp, const lenctx *L, int depth)
{
    calcv a = cc_term(pp, L, depth), b; const char *p;
    while (a.ok) {
        p = cc_ws(*pp);
        if (*p != '+' && *p != '-') break;
        {   char op = *p; p++; *pp = p;
            b = cc_term(pp, L, depth);
            if (!b.ok) { a.ok = 0; break; }
            if (op == '+') { a.px += b.px; a.pct += b.pct; }
            else           { a.px -= b.px; a.pct -= b.pct; }
        }
    }
    return a;
}

/* A whole declaration value that is a single calc().  Returns 0 when the value
 * is not a calc() at all, so the caller falls through to the token path. */
static int parse_calc(const char *v, const lenctx *L, unsigned flags, length *out)
{
    const char *p;
    calcv r;

    if (!v) return 0;
    p = cc_ws(v);
    if (strncasecmp(p, "calc(", 5) != 0) return 0;
    p += 5;
    r = cc_expr(&p, L, 0);
    p = cc_ws(p);
    if (!r.ok || *p != ')') return 0;
    p = cc_ws(p + 1);
    if (*p) return 0;                       /* trailing junk */
    if (!finite_d(r.px) || !finite_d(r.pct)) return 0;
    if (r.pct != 0.0) {
        if (!(flags & LF_PCT)) return 0;
        out->kind = LEN_CALC; out->v = r.px; out->pct = r.pct;
        return 1;
    }
    if (r.px < 0 && !(flags & LF_NEG)) return 0;
    out->kind = LEN_PX; out->v = r.px; out->pct = 0.0;
    return 1;
}

static void apply_len_single(const char *v, const lenctx *L, unsigned flags,
                             length *dst)
{
    vtok t[2];
    int cnt = 0;
    length l;

    if (!v) return;
    if (parse_calc(v, L, flags, &l)) { *dst = l; return; }
    if (!split_tokens(v, t, 2, &cnt) || cnt != 1) return;
    if (!parse_length(t[0], L, flags, &l)) return;
    *dst = l;
}

static void apply_kw(const char *v, const kwmap *m, int *dst)
{
    vtok t[2];
    int cnt = 0, out = 0;

    if (!v || !split_tokens(v, t, 2, &cnt) || cnt != 1) return;
    if (kw_find(m, t[0], &out)) *dst = out;
}

/* border-width component: thin/medium/thick or a non-negative length. */
static int parse_border_width(vtok t, const lenctx *L, double *out)
{
    length l;

    if (tok_is(t, "thin"))   { *out = 1; return 1; }
    if (tok_is(t, "medium")) { *out = 3; return 1; }
    if (tok_is(t, "thick"))  { *out = 5; return 1; }
    if (!parse_length(t, L, 0, &l)) return 0;   /* no auto/none/%/negative */
    if (l.kind != LEN_PX) return 0;
    *out = l.v;
    return 1;
}

/* `border` / `border-<edge>`: width || style || colour, in any order.  Colour
 * is not modelled, so any token that is neither a width nor a style is taken
 * to be part of the colour and is not validated. */
static int parse_border_shorthand(const char *v, const lenctx *L,
                                  double *w, int *st)
{
    vtok t[8];
    int cnt = 0, i, have_w = 0, have_s = 0, sv = 0;
    double wv = 0;

    if (!split_tokens(v, t, 8, &cnt) || cnt < 1) return 0;
    for (i = 0; i < cnt; i++) {
        double tw;
        int ts;
        if (parse_border_width(t[i], L, &tw)) {
            if (have_w) return 0;
            have_w = 1; wv = tw;
        } else if (kw_find(g_bstyle, t[i], &ts)) {
            if (have_s) return 0;
            have_s = 1; sv = ts;
        }
        /* else: colour -- accepted, unmodelled */
    }
    *w = have_w ? wv : 3.0;       /* omitted components reset to initial */
    *st = have_s ? sv : BST_NONE;
    return 1;
}

static void apply_border_group(vctx *K, dom_node *n, vstyle *vs,
                               const lenctx *L, int lvl)
{
    static const char *const edge_name[4] = { "top", "right", "bottom", "left" };
    const char *v;
    vtok t[4];
    int i, sv;
    double wv;
    char pbuf[40];

    /* Broadest first: border, border-width/-style, border-<edge>,
     * border-<edge>-width/-style.  The cascade records one winner per
     * property name and exposes no relative document order between two
     * different names, so this fixed order stands in for it. */
    v = cval_lvl(K, n, "border", lvl);
    if (v && parse_border_shorthand(v, L, &wv, &sv))
        for (i = 0; i < 4; i++) { vs->bw_stated[i] = wv; vs->bstyle[i] = (unsigned char)sv; }

    v = cval_lvl(K, n, "border-width", lvl);
    if (v && expand_sides(v, t)) {
        double w4[4];
        int ok = 1;
        for (i = 0; i < 4; i++)
            if (!parse_border_width(t[i], L, &w4[i])) { ok = 0; break; }
        if (ok) for (i = 0; i < 4; i++) vs->bw_stated[i] = w4[i];
    }

    v = cval_lvl(K, n, "border-style", lvl);
    if (v && expand_sides(v, t)) {
        int s4[4], ok = 1;
        for (i = 0; i < 4; i++)
            if (!kw_find(g_bstyle, t[i], &s4[i])) { ok = 0; break; }
        if (ok) for (i = 0; i < 4; i++) vs->bstyle[i] = (unsigned char)s4[i];
    }

    for (i = 0; i < 4; i++) {
        snprintf(pbuf, sizeof pbuf, "border-%s", edge_name[i]);
        v = cval_lvl(K, n, pbuf, lvl);
        if (v && parse_border_shorthand(v, L, &wv, &sv)) {
            vs->bw_stated[i] = wv;
            vs->bstyle[i] = (unsigned char)sv;
        }
    }
    for (i = 0; i < 4; i++) {
        snprintf(pbuf, sizeof pbuf, "border-%s-width", edge_name[i]);
        v = cval_lvl(K, n, pbuf, lvl);
        if (v) {
            vtok one[2];
            int cnt = 0;
            if (split_tokens(v, one, 2, &cnt) && cnt == 1 &&
                parse_border_width(one[0], L, &wv))
                vs->bw_stated[i] = wv;
        }
        snprintf(pbuf, sizeof pbuf, "border-%s-style", edge_name[i]);
        v = cval_lvl(K, n, pbuf, lvl);
        if (v) {
            vtok one[2];
            int cnt = 0;
            if (split_tokens(v, one, 2, &cnt) && cnt == 1 &&
                kw_find(g_bstyle, one[0], &sv))
                vs->bstyle[i] = (unsigned char)sv;
        }
    }
}

static void apply_overflow_group(vctx *K, dom_node *n, vstyle *vs, int lvl)
{
    const char *v = cval_lvl(K, n, "overflow", lvl);
    int x = 0, y = 0;

    if (v) {
        vtok t[3];
        int cnt = 0;
        if (split_tokens(v, t, 3, &cnt) && (cnt == 1 || cnt == 2) &&
            kw_find(g_overflow, t[0], &x) &&
            (cnt == 1 || kw_find(g_overflow, t[cnt - 1], &y))) {
            if (cnt == 1) y = x;
            vs->c.overflow_x = (overflow_t)x;
            vs->c.overflow_y = (overflow_t)y;
        }
    }
    x = (int)vs->c.overflow_x;
    y = (int)vs->c.overflow_y;
    apply_kw(cval_lvl(K, n, "overflow-x", lvl), g_overflow, &x);
    apply_kw(cval_lvl(K, n, "overflow-y", lvl), g_overflow, &y);
    vs->c.overflow_x = (overflow_t)x;
    vs->c.overflow_y = (overflow_t)y;
}

static void apply_display(vctx *K, dom_node *n, vstyle *vs)
{
    const char *v = cval(K, n, "display", NULL);
    vtok t[2];
    int cnt = 0;
    size_t i;

    if (!v || !split_tokens(v, t, 2, &cnt) || cnt != 1) return;
    for (i = 0; g_display[i].kw; i++) {
        if (tok_is(t[0], g_display[i].kw)) {
            vs->c.display = g_display[i].d;
            vs->display_kw = g_display[i].keep ? g_display[i].kw : NULL;
            return;
        }
    }
}

static void apply_vertical_align(vctx *K, dom_node *n, vstyle *vs,
                                 const lenctx *L)
{
    const char *v = cval(K, n, "vertical-align", NULL);
    vtok t[2];
    int cnt = 0, kv = 0;
    length l;

    if (!v) return;
    if (parse_calc(v, L, LF_PCT | LF_NEG, &l)) {
        vs->c.vertical_align = VA_LENGTH;
        vs->va_pct_set = 0;
        vs->c.vertical_align_px =
            ((l.kind == LEN_CALC && vs->c.line_height >= 0)
                 ? vs->c.line_height * l.pct / 100.0 : 0.0) + l.v;
        return;
    }
    if (!split_tokens(v, t, 2, &cnt) || cnt != 1) return;
    if (kw_find(g_valign, t[0], &kv)) {
        vs->c.vertical_align = (valign_t)kv;
        vs->c.vertical_align_px = 0;
        vs->va_pct_set = 0;
        return;
    }
    if (!parse_length(t[0], L, LF_PCT | LF_NEG, &l)) return;
    vs->c.vertical_align = VA_LENGTH;
    if (l.kind == LEN_PCT) {
        /* Refers to this element's line-height.  With `normal` there is no
         * length until layout picks a ratio, so only the percentage itself is
         * kept (for CSSOM) and the px stays 0. */
        vs->va_pct_set = 1;
        vs->va_pct = l.v;
        vs->c.vertical_align_px =
            (vs->c.line_height >= 0) ? vs->c.line_height * l.v / 100.0 : 0.0;
    } else if (l.kind == LEN_CALC) {
        /* calc() against the line-height, plus the px part. */
        vs->va_pct_set = 0;
        vs->c.vertical_align_px =
            ((vs->c.line_height >= 0) ? vs->c.line_height * l.pct / 100.0 : 0.0) + l.v;
    } else {
        vs->va_pct_set = 0;
        vs->c.vertical_align_px = l.v;
    }
}

static void apply_z_index(vctx *K, dom_node *n, vstyle *vs)
{
    const char *v = cval(K, n, "z-index", NULL);
    vtok t[2];
    int cnt = 0, iv = 0;

    if (!v || !split_tokens(v, t, 2, &cnt) || cnt != 1) return;
    if (tok_is(t[0], "auto")) { vs->c.z_index_auto = 1; vs->c.z_index = 0; return; }
    if (parse_int_tok(t[0], &iv)) { vs->c.z_index_auto = 0; vs->c.z_index = iv; }
}

/* Replaced elements: `computed` carries the flag and the intrinsic size, and
 * the width/height content attributes are the only intrinsic source this
 * engine has (no image decoding). */
static void apply_replaced(dom_node *n, computed *c)
{
    static const char *const tags[] = {
        "img", "canvas", "video", "object", "embed", "iframe",
        "input", "select", "textarea", NULL
    };
    const char *a;
    size_t i;
    vtok t;
    double v;

    if (!n->tag) return;
    for (i = 0; tags[i]; i++) {
        if (str_ieq(n->tag, tags[i])) { c->is_replaced = 1; break; }
    }
    if (!c->is_replaced) return;
    a = dom_attr_get(n, "width");
    if (a) { t.s = a; t.n = strlen(a); if (parse_num_tok(t, &v) && v >= 0) c->intrinsic_w = v; }
    a = dom_attr_get(n, "height");
    if (a) { t.s = a; t.n = strlen(a); if (parse_num_tok(t, &v) && v >= 0) c->intrinsic_h = v; }
}

static void compute_element(vctx *K, dom_node *n, const vstyle *par)
{
    static const char *const mg[4] = { "margin-top", "margin-right",
                                       "margin-bottom", "margin-left" };
    static const char *const pd[4] = { "padding-top", "padding-right",
                                       "padding-bottom", "padding-left" };
    static const char *const in[4] = { "top", "right", "bottom", "left" };
    vstyle *vs = (vstyle *)arena_alloc(K->a, sizeof *vs);
    computed *c = &vs->c;
    lenctx L;
    int lvl, i, kv;

    init_style(vs, par);

    apply_font_shorthand(K, n, vs, par);
    apply_font_size(K, n, vs, par);
    apply_font_family(K, n, vs);
    apply_font_weight(K, n, vs);
    if (!par) K->root_fs = c->font_size;   /* the root defines rem from here */

    /* Built after the font is settled: `ex` is a length in the element's own
     * first available font, so the family has to be known by now. */
    L.fs = c->font_size;
    L.root_fs = K->root_fs;
    L.vw = K->vw;
    L.vh = K->vh;
    L.xh = c->font_size * xheight_ratio(c->font_family);

    apply_line_height(K, n, vs, &L);

    /* Normal declarations first, then !important ones, and within each pass
     * shorthands before their longhands. */
    for (lvl = 0; lvl <= 1; lvl++) {
        apply_len_shorthand(cval_lvl(K, n, "margin", lvl), &L,
                            LF_AUTO | LF_NEG | LF_PCT, c->margin);
        for (i = 0; i < 4; i++)
            apply_len_single(cval_lvl(K, n, mg[i], lvl), &L,
                             LF_AUTO | LF_NEG | LF_PCT, &c->margin[i]);

        apply_len_shorthand(cval_lvl(K, n, "padding", lvl), &L, LF_PCT,
                            c->padding);
        for (i = 0; i < 4; i++)
            apply_len_single(cval_lvl(K, n, pd[i], lvl), &L, LF_PCT,
                             &c->padding[i]);

        apply_border_group(K, n, vs, &L, lvl);
        apply_overflow_group(K, n, vs, lvl);
    }

    apply_display(K, n, vs);
    kv = (int)c->position;   apply_kw(cval(K, n, "position", NULL), g_position, &kv);   c->position = (position_t)kv;
    kv = (int)c->cfloat;     apply_kw(cval(K, n, "float", NULL), g_float, &kv);         c->cfloat = (float_t)kv;
    kv = (int)c->clear;      apply_kw(cval(K, n, "clear", NULL), g_clear, &kv);         c->clear = (clear_t)kv;
    kv = (int)c->box_sizing; apply_kw(cval(K, n, "box-sizing", NULL), g_boxsizing, &kv); c->box_sizing = (boxsizing_t)kv;
    kv = (int)c->direction;  apply_kw(cval(K, n, "direction", NULL), g_direction, &kv);  c->direction = (direction_t)kv;
    kv = (int)c->text_align; apply_kw(cval(K, n, "text-align", NULL), g_textalign, &kv); c->text_align = (textalign_t)kv;
    kv = (int)c->white_space; apply_kw(cval(K, n, "white-space", NULL), g_whitespace, &kv); c->white_space = (whitespace_t)kv;

    apply_vertical_align(K, n, vs, &L);

    apply_len_single(cval(K, n, "width", NULL), &L, LF_AUTO | LF_PCT | LF_STRETCH, &c->width);
    apply_len_single(cval(K, n, "height", NULL), &L, LF_AUTO | LF_PCT | LF_STRETCH, &c->height);
    apply_len_single(cval(K, n, "min-width", NULL), &L, LF_AUTO | LF_PCT, &c->min_width);
    apply_len_single(cval(K, n, "min-height", NULL), &L, LF_AUTO | LF_PCT, &c->min_height);
    apply_len_single(cval(K, n, "max-width", NULL), &L, LF_NONE | LF_PCT, &c->max_width);
    apply_len_single(cval(K, n, "max-height", NULL), &L, LF_NONE | LF_PCT, &c->max_height);

    /* `inset` is the four-side shorthand for top/right/bottom/left; as with
     * every shorthand it is applied first so a longhand can override it. */
    apply_len_shorthand(cval(K, n, "inset", NULL), &L, LF_AUTO | LF_NEG | LF_PCT,
                        c->inset);
    for (i = 0; i < 4; i++)
        apply_len_single(cval(K, n, in[i], NULL), &L, LF_AUTO | LF_NEG | LF_PCT,
                         &c->inset[i]);

    /* 16.1: text-indent is inherited and may be negative. */
    apply_len_single(cval(K, n, "text-indent", NULL), &L,
                     LF_NEG | LF_PCT, &c->text_indent);

    apply_z_index(K, n, vs);
    apply_replaced(n, c);

    /* A border whose style is none or hidden has a used width of 0. */
    for (i = 0; i < 4; i++)
        c->border[i] = (vs->bstyle[i] == BST_NONE || vs->bstyle[i] == BST_HIDDEN)
                       ? 0.0 : vs->bw_stated[i];

    /* A computed `visible` paired with anything else becomes `auto`. */
    if (c->overflow_x == OV_VISIBLE && c->overflow_y != OV_VISIBLE)
        c->overflow_x = OV_AUTO;
    else if (c->overflow_y == OV_VISIBLE && c->overflow_x != OV_VISIBLE)
        c->overflow_y = OV_AUTO;

    n->style = c;          /* == vs: `computed` is the first member */
}

static void walk(vctx *K, dom_node *n, const vstyle *par, const cprop *cps)
{
    const vstyle *self = par;
    dom_node *ch;
    const cprop *mine = cps;

    if (n->type == DOM_ELEMENT) {
        mine = collect_cprops(K, n, cps);
        K->cps = mine;
        compute_element(K, n, par);
        self = (const vstyle *)n->style;
    }
    for (ch = n->first_child; ch; ch = ch->next_sibling)
        walk(K, ch, self, mine);
}

computed *values_compute(arena *a, dom_doc *doc, cascade *c, double vw, double vh)
{
    vctx K;

    if (!a || !doc || !doc->root) return NULL;
    K.a = a;
    K.cas = c;
    K.vw = vw;
    K.vh = vh;
    K.cps = NULL;
    K.root_fs = 16.0;
    walk(&K, doc->root, NULL, NULL);
    return (computed *)doc->root->style;
}

/* ================================================ resolved value (CSSOM) */

static int out_str(char *buf, size_t buflen, const char *s)
{
    size_t n = strlen(s);

    if (n >= buflen) n = buflen - 1;    /* buflen > 0 is checked by the caller */
    memcpy(buf, s, n);
    buf[n] = '\0';
    return 0;
}

/* Chrome serialises CSS numbers with 6 significant digits, trailing zeros and
 * a trailing '.' stripped, and switches to exponent form outside [1e-5, 1e6)
 * -- exactly what "%.6g" produces.  (Measured on Chrome 153; the recorded
 * corpus contains widths such as "1e+07px".) */
static int out_px(char *buf, size_t buflen, double v)
{
    if (!finite_d(v)) v = 0.0;
    if (v == 0.0) v = 0.0;              /* normalises -0 to 0 */
    snprintf(buf, buflen, "%.6gpx", v);
    return 0;
}

static int out_pct(char *buf, size_t buflen, double v)
{
    if (!finite_d(v)) v = 0.0;
    if (v == 0.0) v = 0.0;
    snprintf(buf, buflen, "%.6g%%", v);
    return 0;
}

static int out_length(char *buf, size_t buflen, length l)
{
    switch (l.kind) {
    case LEN_AUTO: return out_str(buf, buflen, "auto");
    case LEN_NONE: return out_str(buf, buflen, "none");
    case LEN_STRETCH: return out_str(buf, buflen, "stretch");
    case LEN_PCT:  return out_pct(buf, buflen, l.v);
    case LEN_CALC: {
        char a[64], b[64];
        out_pct(a, sizeof a, l.pct);
        out_px(b, sizeof b, l.v);
        snprintf(buf, buflen, "calc(%s + %s)", a, b);
        return 0;
    }
    default:       return out_px(buf, buflen, l.v);
    }
}

enum {
    PK_NONE = 0, PK_DISPLAY, PK_POSITION, PK_FLOAT, PK_CLEAR, PK_OVX, PK_OVY,
    PK_OVERFLOW, PK_BOXSIZING, PK_DIRECTION, PK_TEXTALIGN, PK_WHITESPACE,
    PK_VALIGN, PK_WIDTH, PK_HEIGHT, PK_MINW, PK_MINH, PK_MAXW, PK_MAXH,
    PK_MARGIN, PK_PADDING, PK_BORDERW, PK_BORDERS, PK_INSET, PK_FONTSIZE,
    PK_LINEHEIGHT, PK_ZINDEX, PK_MARGIN_SH, PK_PADDING_SH
};

static const struct { const char *name; int kind; int edge; } g_res[] = {
    { "display", PK_DISPLAY, 0 }, { "position", PK_POSITION, 0 },
    { "float", PK_FLOAT, 0 }, { "clear", PK_CLEAR, 0 },
    { "overflow-x", PK_OVX, 0 }, { "overflow-y", PK_OVY, 0 },
    { "overflow", PK_OVERFLOW, 0 }, { "box-sizing", PK_BOXSIZING, 0 },
    { "direction", PK_DIRECTION, 0 }, { "text-align", PK_TEXTALIGN, 0 },
    { "white-space", PK_WHITESPACE, 0 }, { "vertical-align", PK_VALIGN, 0 },
    { "width", PK_WIDTH, 0 }, { "height", PK_HEIGHT, 0 },
    { "min-width", PK_MINW, 0 }, { "min-height", PK_MINH, 0 },
    { "max-width", PK_MAXW, 0 }, { "max-height", PK_MAXH, 0 },
    { "margin-top", PK_MARGIN, EDGE_TOP }, { "margin-right", PK_MARGIN, EDGE_RIGHT },
    { "margin-bottom", PK_MARGIN, EDGE_BOTTOM }, { "margin-left", PK_MARGIN, EDGE_LEFT },
    { "padding-top", PK_PADDING, EDGE_TOP }, { "padding-right", PK_PADDING, EDGE_RIGHT },
    { "padding-bottom", PK_PADDING, EDGE_BOTTOM }, { "padding-left", PK_PADDING, EDGE_LEFT },
    { "border-top-width", PK_BORDERW, EDGE_TOP }, { "border-right-width", PK_BORDERW, EDGE_RIGHT },
    { "border-bottom-width", PK_BORDERW, EDGE_BOTTOM }, { "border-left-width", PK_BORDERW, EDGE_LEFT },
    { "border-top-style", PK_BORDERS, EDGE_TOP }, { "border-right-style", PK_BORDERS, EDGE_RIGHT },
    { "border-bottom-style", PK_BORDERS, EDGE_BOTTOM }, { "border-left-style", PK_BORDERS, EDGE_LEFT },
    { "top", PK_INSET, EDGE_TOP }, { "right", PK_INSET, EDGE_RIGHT },
    { "bottom", PK_INSET, EDGE_BOTTOM }, { "left", PK_INSET, EDGE_LEFT },
    { "font-size", PK_FONTSIZE, 0 }, { "line-height", PK_LINEHEIGHT, 0 },
    { "z-index", PK_ZINDEX, 0 },
    { "margin", PK_MARGIN_SH, 0 }, { "padding", PK_PADDING_SH, 0 },
    { NULL, PK_NONE, 0 }
};

/* width/height do not apply to a non-replaced inline box; CSSOM then reports
 * the computed value, which is what Chrome does. */
static int size_applies(const computed *c)
{
    return !(c->display == D_INLINE && !c->is_replaced);
}

/* Chrome reports the SIZING box: the border box under box-sizing: border-box,
 * the content box otherwise.  (Measured, and confirmed against the recorded
 * corpus: a border-box element with 10% padding reports its 100px border box
 * while its content box is 92px.) */
static double used_size(const computed *c, const values_box *b, int vertical)
{
    if (c->box_sizing == BS_BORDER) return vertical ? b->h : b->w;
    return vertical ? b->content_h : b->content_w;
}

/* Collapse 4 sides the way CSS shorthand serialisation does. */
static int out_sides(char *buf, size_t buflen, char s[4][48])
{
    int n = 4;

    if (strcmp(s[3], s[1]) == 0) {
        n = 3;
        if (strcmp(s[2], s[0]) == 0) {
            n = 2;
            if (strcmp(s[1], s[0]) == 0) n = 1;
        }
    }
    switch (n) {
    case 1: snprintf(buf, buflen, "%s", s[0]); break;
    case 2: snprintf(buf, buflen, "%s %s", s[0], s[1]); break;
    case 3: snprintf(buf, buflen, "%s %s %s", s[0], s[1], s[2]); break;
    default: snprintf(buf, buflen, "%s %s %s %s", s[0], s[1], s[2], s[3]); break;
    }
    return 0;
}

int values_resolved(dom_node *n, const values_box *box, const char *prop,
                    char *buf, size_t buflen)
{
    vstyle fallback;
    const vstyle *vs;
    const computed *c;
    const values_box *b;
    int kind = PK_NONE, edge = 0, i;

    if (!buf || buflen == 0) return -1;
    buf[0] = '\0';
    if (!prop) return -1;

    for (i = 0; g_res[i].name; i++) {
        if (str_ieq(prop, g_res[i].name)) {
            kind = g_res[i].kind;
            edge = g_res[i].edge;
            break;
        }
    }
    if (kind == PK_NONE) return -1;         /* unsupported: buf stays "" */

    if (n && n->style) {
        vs = (const vstyle *)n->style;      /* see the contract in values.h */
        if (vs->magic != VS_MAGIC) {
            /* Not one of ours: trust only the documented `computed` fields. */
            init_style(&fallback, NULL);
            fallback.c = *(const computed *)n->style;
            for (i = 0; i < 4; i++)
                fallback.bstyle[i] = (unsigned char)(fallback.c.border[i] > 0 ? BST_SOLID
                                                                             : BST_NONE);
            vs = &fallback;
        }
    } else {
        init_style(&fallback, NULL);
        vs = &fallback;
    }
    c = &vs->c;
    b = (box && box->has_box) ? box : NULL;

    switch (kind) {
    case PK_DISPLAY:
        return out_str(buf, buflen, vs->display_kw ? vs->display_kw
                                                  : display_name(c->display));
    case PK_POSITION:   return out_str(buf, buflen, kw_name(g_position, (int)c->position));
    case PK_FLOAT:      return out_str(buf, buflen, kw_name(g_float, (int)c->cfloat));
    case PK_CLEAR:      return out_str(buf, buflen, kw_name(g_clear, (int)c->clear));
    case PK_OVX:        return out_str(buf, buflen, kw_name(g_overflow, (int)c->overflow_x));
    case PK_OVY:        return out_str(buf, buflen, kw_name(g_overflow, (int)c->overflow_y));
    case PK_OVERFLOW: {
        const char *x = kw_name(g_overflow, (int)c->overflow_x);
        const char *y = kw_name(g_overflow, (int)c->overflow_y);
        if (strcmp(x, y) == 0) return out_str(buf, buflen, x);
        snprintf(buf, buflen, "%s %s", x, y);
        return 0;
    }
    case PK_BOXSIZING:  return out_str(buf, buflen, kw_name(g_boxsizing, (int)c->box_sizing));
    case PK_DIRECTION:  return out_str(buf, buflen, kw_name(g_direction, (int)c->direction));
    case PK_TEXTALIGN:  return out_str(buf, buflen, kw_name(g_textalign, (int)c->text_align));
    case PK_WHITESPACE: return out_str(buf, buflen, kw_name(g_whitespace, (int)c->white_space));

    case PK_VALIGN:
        if (c->vertical_align == VA_LENGTH) {
            if (vs->va_pct_set) return out_pct(buf, buflen, vs->va_pct);
            return out_px(buf, buflen, c->vertical_align_px);
        }
        return out_str(buf, buflen, kw_name(g_valign, (int)c->vertical_align));

    case PK_WIDTH:
        if (b && size_applies(c)) return out_px(buf, buflen, used_size(c, b, 0));
        return out_length(buf, buflen, c->width);
    case PK_HEIGHT:
        if (b && size_applies(c)) return out_px(buf, buflen, used_size(c, b, 1));
        return out_length(buf, buflen, c->height);

    /* The min and max pair keep the computed value even when a box exists. */
    case PK_MINW: return out_length(buf, buflen, c->min_width);
    case PK_MINH: return out_length(buf, buflen, c->min_height);
    case PK_MAXW: return out_length(buf, buflen, c->max_width);
    case PK_MAXH: return out_length(buf, buflen, c->max_height);

    case PK_MARGIN:
        if (b) return out_px(buf, buflen, b->margin[edge]);
        return out_length(buf, buflen, c->margin[edge]);
    case PK_PADDING:
        if (b) return out_px(buf, buflen, b->padding[edge]);
        return out_length(buf, buflen, c->padding[edge]);
    case PK_BORDERW:
        if (b) return out_px(buf, buflen, b->border[edge]);
        return out_px(buf, buflen, c->border[edge]);
    case PK_BORDERS:
        return out_str(buf, buflen, g_bstyle_name[vs->bstyle[edge]]);

    case PK_INSET:
        if (b && c->position != P_STATIC) return out_px(buf, buflen, b->inset[edge]);
        return out_length(buf, buflen, c->inset[edge]);

    case PK_FONTSIZE: return out_px(buf, buflen, c->font_size);
    case PK_LINEHEIGHT:
        if (c->line_height < 0) return out_str(buf, buflen, "normal");
        return out_px(buf, buflen, c->line_height);
    case PK_ZINDEX:
        if (c->z_index_auto) return out_str(buf, buflen, "auto");
        snprintf(buf, buflen, "%d", c->z_index);
        return 0;

    case PK_MARGIN_SH:
    case PK_PADDING_SH: {
        char s[4][48];
        for (i = 0; i < 4; i++) {
            if (b) out_px(s[i], sizeof s[i],
                          kind == PK_MARGIN_SH ? b->margin[i] : b->padding[i]);
            else out_length(s[i], sizeof s[i],
                            kind == PK_MARGIN_SH ? c->margin[i] : c->padding[i]);
        }
        return out_sides(buf, buflen, s);
    }
    default:
        return -1;
    }
}
