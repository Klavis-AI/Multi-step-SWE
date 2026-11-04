/* lay_core.c - arena, box-tree construction (CSS 2.1 §9.2.1.1 anonymous block
   boxes, §17.2.1 anonymous table objects), length resolution, intrinsic widths
   (§10.3.5 shrink-to-fit inputs) and the public entry points. */
#include "lay_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

double lay_dmax(double a, double b) { return a > b ? a : b; }
double lay_dmin(double a, double b) { return a < b ? a : b; }

/* ------------------------------------------------------------------ arena */
struct ar_chunk { ar_chunk *next; size_t used, cap; };
#define AR_HDR  ((sizeof(ar_chunk) + 15u) & ~(size_t)15u)
#define AR_MIN  ((size_t)16384)

void *lay_alloc(arena *a, size_t n) {
    ar_chunk *c;
    void *p;
    n = (n + 15u) & ~(size_t)15u;
    if (n == 0) n = 16;
    c = a->head;
    if (!c || c->cap - c->used < n) {
        size_t cap = n + AR_HDR;
        if (cap < AR_MIN) cap = AR_MIN;
        c = (ar_chunk *)malloc(cap);
        if (!c) abort();
        c->next = a->head;
        c->used = AR_HDR;
        c->cap = cap;
        a->head = c;
    }
    p = (char *)c + c->used;
    c->used += n;
    memset(p, 0, n);
    return p;
}

void lay_arena_release(arena *a) {
    ar_chunk *c = a->head;
    while (c) { ar_chunk *n = c->next; free(c); c = n; }
    a->head = NULL;
}

/* ---------------------------------------------------------- margin struts */
void ms_clear(mstrut *m) { m->pos = 0; m->neg = 0; }
void ms_add(mstrut *m, double v) {
    if (v > m->pos) m->pos = v;
    if (v < m->neg) m->neg = v;
}
void ms_join(mstrut *m, const mstrut *o) {
    if (o->pos > m->pos) m->pos = o->pos;
    if (o->neg < m->neg) m->neg = o->neg;
}
/* §8.3.1: the collapsed margin is the largest positive plus the most negative. */
double ms_val(const mstrut *m) { return m->pos + m->neg; }

/* ------------------------------------------------------- float exclusions */
void fc_init(fctx *f, arena *a, double cl, double cr) {
    f->a = a; f->ex = NULL; f->n = 0; f->cap = 0; f->cl = cl; f->cr = cr;
}

void fc_add(fctx *f, double x0, double x1, double top, double bot, int right) {
    if (f->n == f->cap) {
        int nc = f->cap ? f->cap * 2 : 8;
        lay_excl *ne = (lay_excl *)lay_alloc(f->a, (size_t)nc * sizeof(lay_excl));
        if (f->n) memcpy(ne, f->ex, (size_t)f->n * sizeof(lay_excl));
        f->ex = ne; f->cap = nc;
    }
    f->ex[f->n].x0 = x0; f->ex[f->n].x1 = x1;
    f->ex[f->n].top = top; f->ex[f->n].bot = bot;
    f->ex[f->n].right = right;
    f->n++;
}

void fc_band(const fctx *f, double top, double bot, double *l, double *r) {
    int i;
    double lo = f->cl, hi = f->cr;
    if (bot <= top) bot = top + 1e-9;
    for (i = 0; i < f->n; i++) {
        const lay_excl *e = &f->ex[i];
        if (e->bot <= top + 1e-9 || e->top >= bot - 1e-9) continue;
        if (e->right) { if (e->x0 < hi) hi = e->x0; }
        else          { if (e->x1 > lo) lo = e->x1; }
    }
    if (hi < lo) hi = lo;
    *l = lo; *r = hi;
}

double fc_next_below(const fctx *f, double y) {
    int i, found = 0;
    double best = y;
    for (i = 0; i < f->n; i++) {
        double b = f->ex[i].bot;
        if (b > y + 1e-9 && (!found || b < best)) { best = b; found = 1; }
    }
    return found ? best : y;
}

/* §9.5.2: bottom outer edge of the lowest float that must be cleared. */
double fc_clear_y(const fctx *f, clear_t c) {
    int i, any = 0;
    double y = -1e300;
    for (i = 0; i < f->n; i++) {
        int r = f->ex[i].right;
        if (c == C_BOTH || (c == C_LEFT && !r) || (c == C_RIGHT && r)) {
            if (!any || f->ex[i].bot > y) { y = f->ex[i].bot; any = 1; }
        }
    }
    return any ? y : -1e300;
}

double fc_lowest(const fctx *f) {
    int i; double y = -1e300;
    for (i = 0; i < f->n; i++) if (f->ex[i].bot > y) y = f->ex[i].bot;
    return f->n ? y : 0.0;
}

/* ------------------------------------------------------- length utilities */
int lay_is_auto(length l) { return l.kind == LEN_AUTO; }

/* A used length is stored on a 1/64 px grid, so 30% of 1628px is used as
   31257/64 = 488.390625 rather than 488.4.  The tolerance is for conversions
   that are exact in decimal but land one ulp low in binary: 25.4mm is exactly
   96px, and flooring that would lose a whole 1/64 px. */
double lay_snap64(double v) {
    double t = v * 64.0 + 1e-6;
    double f = (double)(long long)t;
    if (f > t) f -= 1.0;                     /* floor, for either sign */
    return f / 64.0;
}

/* Round UP to the same 1/64px grid.  A shaped run of text is snapped up and
   never down, so a run that already lands on the grid keeps its width and one
   that does not gains at most 1/64px.  The inputs are exact multiples of
   1/(64*65536) px, so no tolerance is needed here. */
double lay_ceil64(double v) {
    double t = v * 64.0;
    double f = (double)(long long)t;         /* truncates toward zero */
    if (f < t) f += 1.0;                     /* ceil, for either sign */
    return f / 64.0;
}

int lay_res(length l, double base, int base_def, double *out) {
    switch (l.kind) {
        case LEN_PX:  *out = lay_snap64(l.v); return 1;
        case LEN_PCT: if (!base_def) return 0;
                      *out = lay_snap64(base * l.v / 100.0); return 1;
        /* calc(): the percentage part resolves against the same base as a
           plain percentage, then the px part is added. */
        case LEN_CALC: if (!base_def) return 0;
                      *out = lay_snap64(base * l.pct / 100.0 + l.v); return 1;
        default: return 0;
    }
}

double lay_res0(length l, double base, int base_def) {
    double v = 0;
    if (lay_res(l, base, base_def, &v)) return v;
    return 0;
}

/* §8.3/§8.4: percentage margins and padding resolve against the containing
   block WIDTH on all four sides. */
void lay_edges(box *b, double cbw, int cbw_def) {
    const computed *s = b->cs;
    double base = cbw_def ? cbw : 0.0;
    double m[4], p[4];
    int i;
    for (i = 0; i < 4; i++) {
        m[i] = lay_is_auto(s->margin[i]) ? 0.0 : lay_res0(s->margin[i], base, 1);
        p[i] = lay_res0(s->padding[i], base, 1);
        if (p[i] < 0) p[i] = 0;
    }
    b->mt = m[EDGE_TOP];  b->mr = m[EDGE_RIGHT];
    b->mb = m[EDGE_BOTTOM]; b->ml = m[EDGE_LEFT];
    b->pt = p[EDGE_TOP];  b->pr = p[EDGE_RIGHT];
    b->pb = p[EDGE_BOTTOM]; b->pl = p[EDGE_LEFT];
    b->bt = s->border[EDGE_TOP]    > 0 ? s->border[EDGE_TOP]    : 0;
    b->br = s->border[EDGE_RIGHT]  > 0 ? s->border[EDGE_RIGHT]  : 0;
    b->bb = s->border[EDGE_BOTTOM] > 0 ? s->border[EDGE_BOTTOM] : 0;
    b->bl = s->border[EDGE_LEFT]   > 0 ? s->border[EDGE_LEFT]   : 0;
}

void lay_scrollbars(box *b) {
    const computed *s = b->cs;
    b->sbw = (s->overflow_y == OV_SCROLL) ? LAY_SB : 0.0;
    b->sbh = (s->overflow_x == OV_SCROLL) ? LAY_SB : 0.0;
}

double lay_inner_from_border_w(const box *b, double bw) {
    double v = bw - b->bl - b->br - b->pl - b->pr;
    return v > 0 ? v : 0;
}
double lay_border_from_inner_w(const box *b, double iw) {
    return iw + b->bl + b->br + b->pl + b->pr;
}
double lay_inner_from_border_h(const box *b, double bh) {
    double v = bh - b->bt - b->bb - b->pt - b->pb;
    return v > 0 ? v : 0;
}
double lay_border_from_inner_h(const box *b, double ih) {
    return ih + b->bt + b->bb + b->pt + b->pb;
}

double lay_spec_inner_w(const box *b, double v) {
    if (b->cs->box_sizing == BS_BORDER) return lay_inner_from_border_w(b, v);
    return v > 0 ? v : 0;
}
double lay_spec_inner_h(const box *b, double v) {
    if (b->cs->box_sizing == BS_BORDER) return lay_inner_from_border_h(b, v);
    return v > 0 ? v : 0;
}

/* §10.4: min/max applied to the used width; min wins over max. */
double lay_clampw(const box *b, double w, double cbw, int cbw_def) {
    double v;
    if (lay_res(b->cs->max_width, cbw, cbw_def, &v)) {
        v = lay_spec_inner_w(b, v);
        if (w > v) w = v;
    }
    if (lay_res(b->cs->min_width, cbw, cbw_def, &v)) {
        v = lay_spec_inner_w(b, v);
        if (w < v) w = v;
    }
    return w > 0 ? w : 0;
}

double lay_clamph(const box *b, double h, double cbh, int cbh_def) {
    double v;
    if (lay_res(b->cs->max_height, cbh, cbh_def, &v)) {
        v = lay_spec_inner_h(b, v);
        if (h > v) h = v;
    }
    if (lay_res(b->cs->min_height, cbh, cbh_def, &v)) {
        v = lay_spec_inner_h(b, v);
        if (h < v) h = v;
    }
    return h > 0 ? h : 0;
}

/* -------------------------------------------------------------- box basics */
box *lay_new_box(lay_doc *d, box_kind k) {
    box *b = (box *)lay_alloc(&d->a, sizeof(box));
    b->kind = k;
    b->in_flow = 1;
    b->col_span = 1;
    b->row_span = 1;
    b->col_index = -1;
    b->row_index = -1;
    return b;
}

void lay_append(box *parent, box *child) {
    child->pub.parent = &parent->pub;
    child->pub.prev = parent->pub.last_child;
    child->pub.next = NULL;
    if (parent->pub.last_child) parent->pub.last_child->next = &child->pub;
    else parent->pub.first_child = &child->pub;
    parent->pub.last_child = &child->pub;
}

static void detach_all(box *parent) {
    parent->pub.first_child = parent->pub.last_child = NULL;
}

int lay_is_inline_level(const box *b) {
    if (b->is_float || b->is_abs) return 0;
    if (b->kind == BK_TEXT || b->kind == BK_INLINE) return 1;
    return !b->block_level;
}

void lay_shift_subtree(box *b, double dx, double dy) {
    lay_box *c;
    if (dx == 0 && dy == 0) return;
    b->pub.x += dx; b->pub.y += dy;
    for (c = b->pub.first_child; c; c = c->next) lay_shift_subtree((box *)c, dx, dy);
}

void lay_set_subtree_y(box *b, double y) {
    lay_box *c;
    b->pub.y = y;
    for (c = b->pub.first_child; c; c = c->next) lay_set_subtree_y((box *)c, y);
}

double lay_text_w(lay_doc *d, const char *s, int len, const computed *st) {
    char *tmp;
    if (len <= 0) return 0;
    tmp = (char *)lay_alloc(&d->a, (size_t)len + 1);
    memcpy(tmp, s, (size_t)len);
    tmp[len] = '\0';
    return d->env->text_width(tmp, st);
}

/* ----------------------------------------------------- node -> box mapping */
static size_t hash_ptr(const void *p) {
    size_t x = (size_t)(uintptr_t)p;
    x ^= x >> 33;
    x *= (size_t)0xff51afd7ed558ccdULL;
    x ^= x >> 29;
    return x;
}

static void tab_grow(lay_doc *d) {
    int oldcap = d->tabcap, i, nc = oldcap ? oldcap * 2 : 64;
    lay_slot *old = d->tab;
    d->tab = lay_alloc(&d->a, (size_t)nc * sizeof(*d->tab));
    d->tabcap = nc;
    d->tabn = 0;
    for (i = 0; i < oldcap; i++) {
        size_t j;
        if (!old[i].k) continue;
        j = hash_ptr(old[i].k) & (size_t)(nc - 1);
        while (d->tab[j].k) j = (j + 1) & (size_t)(nc - 1);
        d->tab[j].k = old[i].k;
        d->tab[j].v = old[i].v;
        d->tabn++;
    }
}

static void tab_put(lay_doc *d, void *k, box *v) {
    size_t j;
    if (!k) return;
    if (d->tabn * 10 >= d->tabcap * 7) tab_grow(d);
    j = hash_ptr(k) & (size_t)(d->tabcap - 1);
    while (d->tab[j].k) {
        if (d->tab[j].k == k) { d->tab[j].v = v; return; }
        j = (j + 1) & (size_t)(d->tabcap - 1);
    }
    d->tab[j].k = k; d->tab[j].v = v; d->tabn++;
}

lay_box *lay_box_for(lay_doc *d, void *node) {
    size_t j;
    if (!d || !d->tab || !node) return NULL;
    j = hash_ptr(node) & (size_t)(d->tabcap - 1);
    while (d->tab[j].k) {
        if (d->tab[j].k == node) return &d->tab[j].v->pub;
        j = (j + 1) & (size_t)(d->tabcap - 1);
    }
    return NULL;
}

/* --------------------------------------------------------- tree building */
static computed *anon_style(lay_doc *d, const computed *par, display_t disp) {
    computed *c = (computed *)lay_alloc(&d->a, sizeof(computed));
    int i;
    c->display = disp;
    c->width.kind = LEN_AUTO;     c->height.kind = LEN_AUTO;
    c->min_width.kind = LEN_AUTO; c->min_height.kind = LEN_AUTO;
    c->max_width.kind = LEN_NONE; c->max_height.kind = LEN_NONE;
    for (i = 0; i < 4; i++) {
        c->margin[i].kind = LEN_PX;  c->margin[i].v = 0;
        c->padding[i].kind = LEN_PX; c->padding[i].v = 0;
        c->inset[i].kind = LEN_AUTO;
        c->border[i] = 0;
    }
    c->z_index_auto = 1;
    if (par) {
        c->direction = par->direction;
        c->text_align = par->text_align;
        c->white_space = par->white_space;
        c->font_size = par->font_size;
        c->line_height = par->line_height;
        c->font_family = par->font_family;
        c->font_weight = par->font_weight;
    } else {
        c->font_size = 16.0;
        c->line_height = -1.0;
        c->font_weight = 400;
    }
    return c;
}

static box_kind kind_of(display_t disp) {
    switch (disp) {
        case D_INLINE:              return BK_INLINE;
        case D_TABLE:
        case D_INLINE_TABLE:        return BK_TABLE;
        case D_TABLE_ROW:           return BK_TROW;
        case D_TABLE_CELL:          return BK_TCELL;
        case D_TABLE_ROW_GROUP:
        case D_TABLE_HEADER_GROUP:
        case D_TABLE_FOOTER_GROUP:  return BK_TROWGROUP;
        case D_TABLE_COLUMN:        return BK_TCOL;
        case D_TABLE_COLUMN_GROUP:  return BK_TCOLGROUP;
        case D_TABLE_CAPTION:       return BK_TCAPTION;
        /* css-flexbox-1 §3: a flex container is its own formatting context. */
        case D_FLEX:
        case D_INLINE_FLEX:         return BK_FLEX;
        default:                    return BK_BLOCK;
    }
}

static int display_is_block_level(display_t d) {
    switch (d) {
        case D_BLOCK: case D_LIST_ITEM: case D_TABLE: case D_FLEX:
        case D_TABLE_ROW: case D_TABLE_CELL: case D_TABLE_ROW_GROUP:
        case D_TABLE_HEADER_GROUP: case D_TABLE_FOOTER_GROUP:
        case D_TABLE_COLUMN: case D_TABLE_COLUMN_GROUP: case D_TABLE_CAPTION:
            return 1;
        default: return 0;
    }
}

/* §9.7: 'float' and absolute positioning blockify the computed display. */
static display_t blockify(display_t d) {
    switch (d) {
        case D_INLINE: case D_INLINE_BLOCK: return D_BLOCK;
        case D_INLINE_TABLE:                return D_TABLE;
        default:                            return d;
    }
}

static void init_flags(box *b) {
    const computed *s = b->cs;
    display_t disp = s->display;
    int out_of_flow;

    b->is_float = (s->cfloat != F_NONE && s->position != P_ABSOLUTE &&
                   s->position != P_FIXED) ? 1u : 0u;
    b->is_abs = (s->position == P_ABSOLUTE || s->position == P_FIXED) ? 1u : 0u;
    out_of_flow = b->is_float || b->is_abs;
    if (out_of_flow) disp = blockify(disp);
    b->kind = kind_of(disp);
    b->block_level = display_is_block_level(disp) ? 1u : 0u;
    b->in_flow = out_of_flow ? 0u : 1u;
    b->is_repl = s->is_replaced ? 1u : 0u;

    /* §9.4.1: floats, absolutely positioned boxes, inline-blocks, table cells,
       captions, tables and block boxes with overflow != visible all establish
       new block formatting contexts. */
    b->is_bfc = 0;
    /* css-flexbox-1 §3: a flex container establishes an independent

       formatting context, so it is laid out through

       lay_layout_independent() rather than as a block in flow. */
    if (b->kind == BK_FLEX) b->is_bfc = 1u;
    if (out_of_flow) b->is_bfc = 1;
    else if (disp == D_INLINE_BLOCK || disp == D_TABLE_CELL ||
             disp == D_TABLE_CAPTION || disp == D_TABLE || disp == D_INLINE_TABLE)
        b->is_bfc = 1;
    else if (b->kind == BK_BLOCK &&
             (s->overflow_x != OV_VISIBLE || s->overflow_y != OV_VISIBLE))
        b->is_bfc = 1;
}

static int text_all_space(const char *t) {
    if (!t) return 1;
    for (; *t; t++)
        if (*t != ' ' && *t != '\t' && *t != '\n' && *t != '\r' && *t != '\f')
            return 0;
    return 1;
}

static box *build_node(lay_doc *d, void *node, const computed *pstyle);

static void build_children(lay_doc *d, box *parent, void *node, const computed *st) {
    void *c;
    for (c = d->env->first_child(node); c; c = d->env->next_sibling(c)) {
        box *cb = build_node(d, c, st);
        if (cb) lay_append(parent, cb);
    }
}

static box *build_node(lay_doc *d, void *node, const computed *pstyle) {
    const computed *st = d->env->style_of(node);
    box *b;
    if (!st) {
        const char *t = d->env->text_of(node);
        if (!t || !pstyle) return NULL;
        b = lay_new_box(d, BK_TEXT);
        b->cs = pstyle;
        b->fs = pstyle;
        b->pub.st = pstyle;
        b->text = t;
        b->pub.node = node;
        tab_put(d, node, b);
        return b;
    }
    b = lay_new_box(d, BK_BLOCK);
    b->cs = st;
    b->fs = st;
    b->pub.st = st;
    b->pub.node = node;
    tab_put(d, node, b);
    init_flags(b);
    build_children(d, b, node, st);
    return b;
}

/* ------------------------------ §9.2.1.1 anonymous block box generation -- */
static void relink(box *p, box *first, box *last) {
    box *it = first;
    while (it) {
        box *nx = (it == last) ? NULL : (box *)it->pub.next;
        it->pub.parent = NULL; it->pub.prev = NULL; it->pub.next = NULL;
        lay_append(p, it);
        it = nx;
    }
}

static box *wrap_run(lay_doc *d, box *parent, box *first, box *last) {
    box *anon = lay_new_box(d, BK_BLOCK);
    anon->cs = anon_style(d, parent->cs, D_BLOCK);
    anon->fs = parent->fs ? parent->fs : parent->cs;
    anon->pub.st = anon->cs;
    anon->is_anon = 1;
    anon->block_level = 1;
    anon->in_flow = 1;
    relink(anon, first, last);
    return anon;
}

static void fixup_anon_blocks(lay_doc *d, box *p) {
    lay_box *c;
    int has_block = 0, has_inline = 0;
    box *cur, *run_first = NULL, *run_last = NULL;
    int run_has_inline = 0;

    for (c = p->pub.first_child; c; c = c->next) {
        box *b = (box *)c;
        if (b->cs->display == D_NONE || !b->in_flow) continue;
        if (b->block_level) has_block = 1; else has_inline = 1;
    }
    if (!has_block || !has_inline) return;

    cur = (box *)p->pub.first_child;
    detach_all(p);
    while (cur) {
        box *nx = (box *)cur->pub.next;
        int inline_level;
        if (cur->kind == BK_TEXT && cur->cs->white_space != WS_PRE &&
            cur->cs->white_space != WS_PRE_WRAP && text_all_space(cur->text)) {
            cur->in_flow = 0;                   /* white space between blocks */
            cur->pub.parent = &p->pub;
            cur = nx;
            continue;
        }
        inline_level = (cur->cs->display != D_NONE) && !cur->is_abs &&
                       !cur->is_float && !cur->block_level;
        if (inline_level || (cur->is_float && run_first)) {
            if (!run_first) run_first = cur;
            run_last = cur;
            if (inline_level) run_has_inline = 1;
        } else {
            if (run_first) {
                if (run_has_inline) lay_append(p, wrap_run(d, p, run_first, run_last));
                else relink(p, run_first, run_last);
                run_first = run_last = NULL;
                run_has_inline = 0;
            }
            lay_append(p, cur);
        }
        cur = nx;
    }
    if (run_first) {
        if (run_has_inline) lay_append(p, wrap_run(d, p, run_first, run_last));
        else relink(p, run_first, run_last);
    }
}

/* ----------------------------- §17.2.1 anonymous table object generation -- */
static box *new_anon(lay_doc *d, box *par, display_t disp, box_kind k) {
    box *a = lay_new_box(d, k);
    a->cs = anon_style(d, par->cs, disp);
    a->fs = par->fs ? par->fs : par->cs;
    a->pub.st = a->cs;
    a->is_anon = 1;
    a->block_level = display_is_block_level(disp) ? 1u : 0u;
    a->in_flow = 1;
    if (disp == D_TABLE_CELL || disp == D_TABLE) a->is_bfc = 1;
    return a;
}

static int child_ok_in(box_kind parent, box_kind child) {
    switch (parent) {
        case BK_TABLE:
            return child == BK_TROWGROUP || child == BK_TROW ||
                   child == BK_TCOL || child == BK_TCOLGROUP || child == BK_TCAPTION;
        case BK_TROWGROUP: return child == BK_TROW;
        case BK_TROW:      return child == BK_TCELL;
        default:           return 1;
    }
}

static void fixup_table(lay_doc *d, box *p) {
    box *cur, *run_first = NULL, *run_last = NULL;
    display_t wrap_disp;
    box_kind wrap_kind;
    if (p->kind == BK_TABLE || p->kind == BK_TROWGROUP) {
        wrap_disp = D_TABLE_ROW; wrap_kind = BK_TROW;
    } else if (p->kind == BK_TROW) {
        wrap_disp = D_TABLE_CELL; wrap_kind = BK_TCELL;
    } else return;

    cur = (box *)p->pub.first_child;
    detach_all(p);
    while (cur) {
        box *nx = (box *)cur->pub.next;
        int bad;
        if (cur->kind == BK_TEXT && text_all_space(cur->text)) {
            cur->in_flow = 0; cur->pub.parent = &p->pub; cur = nx; continue;
        }
        if (cur->cs->display == D_NONE || cur->is_abs) {
            if (run_first) {
                box *a = new_anon(d, p, wrap_disp, wrap_kind);
                relink(a, run_first, run_last);
                lay_append(p, a);
                run_first = run_last = NULL;
            }
            lay_append(p, cur);
            cur = nx; continue;
        }
        bad = !child_ok_in(p->kind, cur->kind);
        if (bad) {
            if (!run_first) run_first = cur;
            run_last = cur;
        } else {
            if (run_first) {
                box *a = new_anon(d, p, wrap_disp, wrap_kind);
                relink(a, run_first, run_last);
                lay_append(p, a);
                run_first = run_last = NULL;
            }
            lay_append(p, cur);
        }
        cur = nx;
    }
    if (run_first) {
        box *a = new_anon(d, p, wrap_disp, wrap_kind);
        relink(a, run_first, run_last);
        lay_append(p, a);
    }

    if (p->kind == BK_TABLE) {          /* bare rows get an anonymous row group */
        cur = (box *)p->pub.first_child;
        run_first = run_last = NULL;
        detach_all(p);
        while (cur) {
            box *nx = (box *)cur->pub.next;
                if (cur->kind == BK_TROW) {
                if (!run_first) run_first = cur;
                run_last = cur;
            } else {
                if (run_first) {
                    box *a = new_anon(d, p, D_TABLE_ROW_GROUP, BK_TROWGROUP);
                    relink(a, run_first, run_last);
                    lay_append(p, a);
                    run_first = run_last = NULL;
                }
                lay_append(p, cur);
            }
            cur = nx;
        }
        if (run_first) {
            box *a = new_anon(d, p, D_TABLE_ROW_GROUP, BK_TROWGROUP);
            relink(a, run_first, run_last);
            lay_append(p, a);
        }
    }
}

static void fixup_orphan_table_parts(lay_doc *d, box *p) {
    box *cur, *run_first = NULL, *run_last = NULL;
    int any = 0;
    for (cur = (box *)p->pub.first_child; cur; cur = (box *)cur->pub.next) {
        if (cur->kind == BK_TROW || cur->kind == BK_TCELL ||
            cur->kind == BK_TROWGROUP || cur->kind == BK_TCOL ||
            cur->kind == BK_TCOLGROUP) { any = 1; break; }
    }
    if (!any) return;
    cur = (box *)p->pub.first_child;
    detach_all(p);
    while (cur) {
        box *nx = (box *)cur->pub.next;
        int part;
        part = (cur->kind == BK_TROW || cur->kind == BK_TCELL ||
                cur->kind == BK_TROWGROUP || cur->kind == BK_TCOL ||
                cur->kind == BK_TCOLGROUP) && !cur->is_abs;
        if (part) {
            if (!run_first) run_first = cur;
            run_last = cur;
        } else {
            if (run_first) {
                box *t = new_anon(d, p, D_TABLE, BK_TABLE);
                relink(t, run_first, run_last);
                fixup_table(d, t);
                lay_append(p, t);
                run_first = run_last = NULL;
            }
            lay_append(p, cur);
        }
        cur = nx;
    }
    if (run_first) {
        box *t = new_anon(d, p, D_TABLE, BK_TABLE);
        relink(t, run_first, run_last);
        fixup_table(d, t);
        lay_append(p, t);
    }
}

static void fixup_tree(lay_doc *d, box *b) {
    lay_box *c;
    if (b->cs && b->cs->display == D_NONE) return;
    for (c = b->pub.first_child; c; c = c->next) fixup_tree(d, (box *)c);
    if (b->kind == BK_TABLE || b->kind == BK_TROW || b->kind == BK_TROWGROUP)
        fixup_table(d, b);
    else {
        fixup_orphan_table_parts(d, b);
        fixup_anon_blocks(d, b);
    }
}

/* ------------------------------------------------------- intrinsic widths */
double lay_intrinsic(lay_doc *d, box *b, int minimum, double avail) {
    double inner, v;
    (void)avail;
    if (b->cs->display == D_NONE) return 0;
    if (b->intr_done) return minimum ? b->minc : b->maxc;

    lay_edges(b, 0, 0);
    lay_scrollbars(b);

    if (b->kind == BK_TEXT) {
        double mn = 0, mx = 0;
        lay_text_metrics(d, b, &mn, &mx);
        b->minc = mn; b->maxc = mx;
        b->intr_done = 1;
        return minimum ? mn : mx;
    }
    if (b->is_repl) {
        inner = b->cs->intrinsic_w;
        if (lay_res(b->cs->width, 0, 0, &v)) inner = lay_spec_inner_w(b, v);
        inner = lay_clampw(b, inner, 0, 0);
        b->minc = b->maxc = lay_border_from_inner_w(b, inner);
        b->intr_done = 1;
        return b->minc;
    }
    if (b->kind == BK_INLINE) {
        lay_box *c;
        double mn = 0, mx = 0;
        for (c = b->pub.first_child; c; c = c->next) {
            box *cb = (box *)c;
            if (cb->cs->display == D_NONE || cb->is_abs) continue;
            mn = lay_dmax(mn, lay_intrinsic(d, cb, 1, 0));
            mx += lay_intrinsic(d, cb, 0, 0);
        }
        b->minc = mn + b->bl + b->br + b->pl + b->pr;
        b->maxc = mx + b->bl + b->br + b->pl + b->pr;
        b->intr_done = 1;
        return minimum ? b->minc : b->maxc;
    }
    if (b->kind == BK_TABLE) {
        b->minc = lay_table_intrinsic(d, b, 1);
        b->maxc = lay_table_intrinsic(d, b, 0);
        lay_edges(b, 0, 0);
        b->intr_done = 1;
        return minimum ? b->minc : b->maxc;
    }

    if (lay_res(b->cs->width, 0, 0, &v)) {
        inner = lay_clampw(b, lay_spec_inner_w(b, v), 0, 0);
        b->minc = b->maxc = lay_border_from_inner_w(b, inner) + b->sbw;
        b->intr_done = 1;
        return b->minc;
    } else {
        lay_box *c;
        double mn = 0, mx = 0, line = 0, floats = 0;
        double sbw = b->sbw, bl = b->bl, br = b->br, pl = b->pl, pr = b->pr;
        int had_inline = 0;
        for (c = b->pub.first_child; c; c = c->next) {
            box *cb = (box *)c;
            double cw_min, cw_max, mlr;
            if (cb->cs->display == D_NONE || cb->is_abs) continue;
            if (!cb->in_flow && !cb->is_float) continue;
            lay_edges(cb, 0, 0);
            mlr = cb->ml + cb->mr;
            cw_min = lay_intrinsic(d, cb, 1, 0);
            cw_max = lay_intrinsic(d, cb, 0, 0);
            if (cb->is_float) {
                if (cb->cs->clear != C_NONE) { mx = lay_dmax(mx, line + floats); floats = 0; }
                floats += cw_max + mlr;
                mn = lay_dmax(mn, cw_min + mlr);
                mx = lay_dmax(mx, line + floats);
            } else if (lay_is_inline_level(cb)) {
                had_inline = 1;
                line += cw_max;
                mn = lay_dmax(mn, cw_min);
                mx = lay_dmax(mx, line + floats);
            } else {
                mx = lay_dmax(mx, line + floats);
                line = 0; floats = 0;
                mn = lay_dmax(mn, cw_min + mlr);
                mx = lay_dmax(mx, cw_max + mlr);
            }
        }
        mx = lay_dmax(mx, line + floats);
        /* 16.1: the first line's indent widens the content the box must hold. */
        if (had_inline) {
            double ind = lay_res0(b->cs->text_indent, 0, 0);
            if (ind > 0) { mn += ind; mx += ind; }
        }
        b->minc = lay_clampw(b, mn, 0, 0) + bl + br + pl + pr + sbw;
        b->maxc = lay_clampw(b, mx, 0, 0) + bl + br + pl + pr + sbw;
        b->intr_done = 1;
        return minimum ? b->minc : b->maxc;
    }
}

/* -------------------------------------------------- scrollable overflow -- */
static void extent(box *b, double *r, double *bt) {
    lay_box *c;
    for (c = b->pub.first_child; c; c = c->next) {
        box *cb = (box *)c;
        double cr, cbo;
        if (cb->cs->display == D_NONE) continue;
        cr = cb->pub.x + cb->pub.w;
        cbo = cb->pub.y + cb->pub.h;
        if (cr > *r) *r = cr;
        if (cbo > *bt) *bt = cbo;
        if (cb->cs->overflow_x == OV_VISIBLE && cb->cs->overflow_y == OV_VISIBLE)
            extent(cb, r, bt);
    }
}

/* The used margin, padding and border of every box, published on lay_box so
   the CSSOM layer reads them instead of reconstructing them from the computed
   style.  Called once per box after layout has settled, so an 'auto' margin
   reads back as the length §10.3/§10.6 solved for. */
void lay_publish_edges(box *b) {
    b->pub.margin[EDGE_TOP]     = b->mt; b->pub.margin[EDGE_RIGHT]  = b->mr;
    b->pub.margin[EDGE_BOTTOM]  = b->mb; b->pub.margin[EDGE_LEFT]   = b->ml;
    b->pub.padding[EDGE_TOP]    = b->pt; b->pub.padding[EDGE_RIGHT] = b->pr;
    b->pub.padding[EDGE_BOTTOM] = b->pb; b->pub.padding[EDGE_LEFT]  = b->pl;
    b->pub.border[EDGE_TOP]     = b->bt; b->pub.border[EDGE_RIGHT]  = b->br;
    b->pub.border[EDGE_BOTTOM]  = b->bb; b->pub.border[EDGE_LEFT]   = b->bl;
}

void lay_finish_scrollable(box *b) {
    double pad_l = b->pub.x + b->bl;
    double pad_t = b->pub.y + b->bt;
    double client_w = lay_dmax(0.0, b->pub.w - b->bl - b->br - b->sbw);
    double client_h = lay_dmax(0.0, b->pub.h - b->bt - b->bb - b->sbh);
    double r = pad_l + client_w, bo = pad_t + client_h;
    extent(b, &r, &bo);
    b->pub.scroll_w = lay_dmax(client_w, r - pad_l);
    b->pub.scroll_h = lay_dmax(client_h, bo - pad_t);
}

static void finish_all(box *b) {
    lay_box *c;
    if (b->cs->display == D_NONE) return;
    for (c = b->pub.first_child; c; c = c->next) finish_all((box *)c);
    lay_finish_scrollable(b);
    lay_publish_edges(b);
}

/* ------------------------------------------------------------ public API */
lay_doc *lay_run(const lay_env *env, double viewport_w, double viewport_h) {
    lay_doc *d;
    box *r;
    if (!env || !env->root) return NULL;
    d = (lay_doc *)calloc(1, sizeof(lay_doc));
    if (!d) return NULL;
    d->env = env;
    d->vw = viewport_w;
    d->vh = viewport_h;
    tab_grow(d);
    r = build_node(d, env->root, NULL);
    d->root = r;
    if (!r || r->cs->display == D_NONE) return d;
    fixup_tree(d, r);

    /* §9.4.1: the root element establishes a block formatting context; its
       margins never collapse with the initial containing block. */
    r->is_bfc = 1;
    if (r->is_abs) {
        r->static_x = 0;
        r->static_y = 0;
        lay_enqueue_abs(d, r);
    } else {
        lay_edges(r, viewport_w, 1);
        lay_layout_independent(d, r, r->ml, r->mt, viewport_w, 0, viewport_h, 1);
    }
    lay_run_abs_queue(d);
    finish_all(r);
    return d;
}

void lay_free(lay_doc *d) {
    if (!d) return;
    lay_arena_release(&d->a);
    free(d);
}
