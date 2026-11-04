/* lay_block.c - block formatting contexts: CSS 2.1 §10.3.3 (block-level width),
   §10.6.3 (auto height), §8.3.1 (collapsing margins), §9.5 (floats and
   clearance), §9.3/§10.1/§10.3.7/§10.6.4 (relative and absolute positioning). */
#include "lay_internal.h"
#include <string.h>
#include <stdlib.h>

static direction_t icb_dir(lay_doc *d);

/* ------------------------------------------------------------ flow state -- */
void bfs_add_pending(bfs *s, box *b) {
    if (s->npend == s->cappend) {
        int nc = s->cappend ? s->cappend * 2 : 8;
        box **nv = (box **)lay_alloc(&s->d->a, (size_t)nc * sizeof(box *));
        if (s->npend) memcpy(nv, s->pend, (size_t)s->npend * sizeof(box *));
        s->pend = nv; s->cappend = nc;
    }
    s->pend[s->npend++] = b;
}

/* A box whose top margin still adjoins its parent's has no top border or
   padding, so its border-box top and content-box top coincide: sealing the
   chain at `y` fixes every unresolved ancestor at the same offset. */
void bfs_seal(bfs *s, double y) {
    int i;
    if (s->resolved) return;
    if (s->parent && !s->parent->resolved) bfs_seal(s->parent, y);
    s->resolved = 1;
    s->owner->pub.y = y;
    s->cy = y;
    s->flow = y;
    ms_clear(&s->st);
    s->sealed_inside = 1;
    for (i = 0; i < s->npend; i++) lay_set_subtree_y(s->pend[i], y);
    s->npend = 0;
}

static double resolve_here(bfs *s) {
    double y = s->flow + ms_val(&s->st);
    if (!s->resolved) bfs_seal(s, y);
    return y;
}

double fc_highest_top(const fctx *f) {
    int i; double y = -1e300; int any = 0;
    for (i = 0; i < f->n; i++)
        if (!any || f->ex[i].top > y) { y = f->ex[i].top; any = 1; }
    return any ? y : -1e300;
}

/* ------------------------------------------------------------- contents -- */
static void layout_contents(lay_doc *d, box *b, bfs *s) {
    lay_box *c;
    int has_block = 0, has_inline = 0;

    if (b->kind == BK_TABLE) {
        if (!s->resolved) bfs_seal(s, s->flow + ms_val(&s->st));
        lay_table_contents(d, b);
        s->flow = s->cy + b->pub.content_h;
        ms_clear(&s->st);
        s->sealed_inside = 1;
        return;
    }
    for (c = b->pub.first_child; c; c = c->next) {
        box *x = (box *)c;
        if (x->cs->display == D_NONE || x->is_abs || x->is_float) continue;
        if (!x->in_flow) continue;
        if (x->block_level) has_block = 1; else has_inline = 1;
    }
    if (has_inline && !has_block) {
        double bottom, bl = 0;
        int hb = 0;
        if (!s->resolved) bfs_seal(s, s->flow + ms_val(&s->st));
        bottom = lay_inline_content(s, s->cy, &bl, &hb);
        if (hb) { b->baseline = bl - b->pub.y; b->has_baseline = 1; }
        s->flow = bottom;
        ms_clear(&s->st);
        s->sealed_inside = 1;
        return;
    }
    c = b->pub.first_child;
    while (c) {
        box *x = (box *)c;
        c = c->next;
        lay_layout_inflow_child(s, x);
    }
}

/* Set up a flow state for `b` and lay out its contents.  `parent` is non-NULL
   only when b shares its parent's block formatting context. */
static void run_contents(lay_doc *d, box *b, bfs *s, fctx *own_fc,
                         bfs *parent, double x, int resolved, double y,
                         double cch, int cch_def) {
    memset(s, 0, sizeof(*s));
    s->d = d;
    s->owner = b;
    s->parent = parent;
    s->cx = x + b->bl + b->pl + (b->cs->direction == DIR_RTL ? b->sbw : 0.0);
    s->cw = b->pub.content_w;
    s->cch = cch;
    s->cch_def = cch_def;
    if (own_fc) {
        fc_init(own_fc, &d->a, s->cx, s->cx + s->cw);
        s->f = own_fc;
    } else {
        s->f = parent->f;
    }
    if (resolved) {
        s->resolved = 1;
        s->cy = y + b->bt + b->pt;
        s->flow = s->cy;
        ms_clear(&s->st);
    } else {
        s->resolved = 0;
        s->flow = parent->flow;
        s->st = parent->st;
    }
    layout_contents(d, b, s);
}

/* ------------------------------------------------- independent (BFC) boxes */
void lay_layout_independent(lay_doc *d, box *b, double x, double y,
                            double avail_w, int mode, double cbh, int cbh_def) {
    fctx f;
    bfs s;
    double iw, bw, spec, ch, inner_h;
    int have_h;

    if (b->cs->display == D_NONE) return;
    lay_edges(b, avail_w, 1);
    lay_scrollbars(b);

    if (mode >= 2) {
        bw = b->pub.w;
        iw = lay_inner_from_border_w(b, bw);
    } else if (lay_res(b->cs->width, avail_w, 1, &spec)) {
        iw = lay_clampw(b, lay_spec_inner_w(b, spec), avail_w, 1);
        bw = lay_border_from_inner_w(b, iw);
    } else if (b->is_repl) {
        iw = lay_clampw(b, b->cs->intrinsic_w, avail_w, 1);
        bw = lay_border_from_inner_w(b, iw);
    } else if (mode == 1) {
        double mn = lay_intrinsic(d, b, 1, 0);
        double mx = lay_intrinsic(d, b, 0, 0);
        double av, sf;
        lay_edges(b, avail_w, 1);          /* lay_intrinsic re-resolved the edges */
        lay_scrollbars(b);
        av = avail_w - b->ml - b->mr;
        sf = lay_dmin(lay_dmax(mn, av), mx);
        if (sf < mn) sf = mn;
        iw = lay_clampw(b, lay_inner_from_border_w(b, sf), avail_w, 1);
        bw = lay_border_from_inner_w(b, iw);
    } else {
        iw = avail_w - b->ml - b->mr - b->bl - b->br - b->pl - b->pr;
        if (iw < 0) iw = 0;
        iw = lay_clampw(b, iw, avail_w, 1);
        bw = lay_border_from_inner_w(b, iw);
    }
    b->pub.x = x;
    b->pub.y = y;
    b->pub.w = bw;
    b->pub.content_w = iw - b->sbw;
    if (b->pub.content_w < 0) b->pub.content_w = 0;

    /* definite inner height, if any, so percentage children can resolve */
    if (mode == 3) {
        have_h = 1;
        inner_h = lay_inner_from_border_h(b, b->pub.h);
    } else if (lay_res(b->cs->height, cbh, cbh_def, &spec)) {
        have_h = 1;
        inner_h = lay_clamph(b, lay_spec_inner_h(b, spec), cbh, cbh_def);
    } else if (b->is_repl) {
        have_h = 1;
        inner_h = lay_clamph(b, b->cs->intrinsic_h, cbh, cbh_def);
    } else {
        have_h = 0;
        inner_h = 0;
    }
    b->pub.content_h = have_h ? lay_dmax(0.0, inner_h - b->sbh) : 0.0;
    b->h_definite = have_h ? 1u : 0u;

    run_contents(d, b, &s, &f, NULL, x, 1, y,
                 have_h ? b->pub.content_h : 0.0, have_h);
    ch = s.flow - s.cy + ms_val(&s.st);

    if (!have_h) {
        double lo = fc_lowest(&f);
        /* §10.6.3: a box that establishes a BFC grows to contain its floats. */
        if (f.n > 0 && lo - s.cy > ch) ch = lo - s.cy;
        if (ch < 0) ch = 0;
        inner_h = lay_clamph(b, ch + b->sbh, cbh, cbh_def);
    }
    b->pub.h = lay_border_from_inner_h(b, inner_h);
    b->pub.content_h = lay_dmax(0.0, inner_h - b->sbh);
}

/* ----------------------------------------------------- §10.3.3 used width */
static void block_width(box *c, double avail, direction_t cb_dir, double *out_iw) {
    const computed *st = c->cs;
    double spec, iw, edges = c->bl + c->br + c->pl + c->pr;
    int mla = lay_is_auto(st->margin[EDGE_LEFT]);
    int mra = lay_is_auto(st->margin[EDGE_RIGHT]);
    int fixed_w = 1;

    if (!lay_res(st->width, avail, 1, &spec)) {
        double tml = mla ? 0.0 : c->ml, tmr = mra ? 0.0 : c->mr, cl;
        iw = avail - tml - tmr - edges;
        if (iw < 0) iw = 0;
        cl = lay_clampw(c, iw, avail, 1);
        if (cl != iw) { iw = cl; }
        else { c->ml = tml; c->mr = tmr; fixed_w = 0; }
    } else {
        iw = lay_clampw(c, lay_spec_inner_w(c, spec), avail, 1);
    }
    if (fixed_w) {
        double nonauto = (mla ? 0.0 : c->ml) + (mra ? 0.0 : c->mr);
        double rest = avail - iw - edges - nonauto;
        /* §10.3.3: when the box is over-constrained, auto margins are zero. */
        if (rest < 0) {
            if (mla) { c->ml = 0; mla = 0; }
            if (mra) { c->mr = 0; mra = 0; }
            rest = avail - iw - edges - c->ml - c->mr;
        }
        if (mla && mra)      { c->ml = rest / 2.0; c->mr = rest - rest / 2.0; }
        else if (mla)        { c->ml = rest; }
        else if (mra)        { c->mr = rest; }
        else if (cb_dir == DIR_RTL) { c->ml += rest; }
        else                 { c->mr += rest; }
    }
    *out_iw = iw;
}

/* ------------------------------------------------------------ float boxes */
void lay_place_float(bfs *s, box *fl) {
    lay_doc *d = s->d;
    double y0, y, l, r, x, mw, mh;

    if (fl->cs->display == D_NONE) return;
    y0 = s->flow + ms_val(&s->st);
    if (!s->resolved) { bfs_seal(s, y0); y0 = s->cy; }

    /* Lay the float out at the origin: its own size does not depend on where
       it lands, and the margin-box size is needed to choose the band. */
    lay_layout_independent(d, fl, 0, 0, s->cw, 1, s->cch, s->cch_def);
    mw = fl->ml + fl->pub.w + fl->mr;
    mh = fl->mt + fl->pub.h + fl->mb;

    y = y0;
    if (fl->cs->clear != C_NONE) {
        double cy = fc_clear_y(s->f, fl->cs->clear);
        if (cy > y) y = cy;
    }
    {   /* §9.5.1 rule 5: never higher than an earlier float */
        double hi = fc_highest_top(s->f);
        if (hi > -1e299 && y < hi) y = hi;
    }
    for (;;) {
        double ny;
        fc_band(s->f, y, y + (mh > 0 ? mh : 1.0), &l, &r);
        if (l < s->cx) l = s->cx;
        if (r > s->cx + s->cw) r = s->cx + s->cw;
        if (r - l >= mw - 1e-6) break;
        ny = fc_next_below(s->f, y);
        if (ny <= y + 1e-9) break;
        y = ny;
    }
    if (fl->cs->cfloat == F_RIGHT) x = r - fl->pub.w - fl->mr;
    else                          x = l + fl->ml;

    lay_shift_subtree(fl, x - fl->pub.x, (y + fl->mt) - fl->pub.y);
    fc_add(s->f, x - fl->ml, x + fl->pub.w + fl->mr, y, y + mh,
           fl->cs->cfloat == F_RIGHT ? 1 : 0);
}

/* ------------------------------------------- one in-flow block-level child */
void lay_layout_inflow_child(bfs *s, box *c) {
    lay_doc *d = s->d;
    bfs cs;
    fctx cf;
    double iw, y, spec;
    int sealed = 0, clearance = 0;
    double clear_y = 0;

    if (c->cs->display == D_NONE) return;
    if (c->is_abs) {
        double sy = s->flow + ms_val(&s->st);
        c->static_x = 0;
        c->static_y = s->resolved ? (sy - s->cy) : 0.0;
        c->static_rtl = (s->owner->cs->direction == DIR_RTL) ? 1u : 0u;
        lay_enqueue_abs(d, c);
        return;
    }
    if (c->is_float) { lay_place_float(s, c); return; }
    if (!c->in_flow) return;

    lay_edges(c, s->cw, 1);
    lay_scrollbars(c);
    block_width(c, s->cw, s->owner->cs->direction, &iw);
    c->pub.w = lay_border_from_inner_w(c, iw);
    c->pub.content_w = lay_dmax(0.0, iw - c->sbw);
    c->pub.x = s->cx + c->ml;

    /* §8.3.1: the child's top margin joins the pending collapsing set. */
    ms_add(&s->st, c->mt);

    /* §9.5.2 clearance.  Chrome introduces clearance whenever the lowest float
       to be cleared ends below the flow position the strut is measured from,
       and then discards the strut, placing the border top edge exactly at that
       float's bottom outer edge (the upstream fixture
       float_forced_clearance_adjoining_float documents this). */
    if (c->cs->clear != C_NONE && s->f->n > 0) {
        clear_y = fc_clear_y(s->f, c->cs->clear);
        if (clear_y > s->flow + 1e-9) clearance = 1;
    }
    if (clearance) {
        y = clear_y;
        if (!s->resolved) bfs_seal(s, y);
        else { ms_clear(&s->st); s->flow = y; }
        c->has_clearance = 1;
        sealed = 1;
    } else if (c->is_bfc || c->bt > 0 || c->pt > 0) {
        y = resolve_here(s);
        sealed = 1;
    } else {
        y = s->flow + ms_val(&s->st);
    }

    /* §9.5: a box that establishes a new BFC must not overlap a float. */
    if (c->is_bfc && s->f->n > 0) {
        double l, r, line_left, right_lim;
        int wauto = lay_is_auto(c->cs->width);
        for (;;) {
            double ny;
            int here;
            fc_band(s->f, y, y + 1.0, &l, &r);
            if (l < s->cx) l = s->cx;
            if (r > s->cx + s->cw) r = s->cx + s->cw;
            here = (l > s->cx + 1e-9) || (r < s->cx + s->cw - 1e-9);
            line_left = here ? lay_dmax(l, s->cx + c->ml) : s->cx + c->ml;
            right_lim = here ? r : s->cx + s->cw;
            if (!wauto) {
                if (line_left + c->pub.w <= right_lim + 1e-6) break;
            } else {
                double avail2 = right_lim - c->mr - line_left;
                double mn = lay_intrinsic(d, c, 1, 0);
                lay_edges(c, s->cw, 1);
                lay_scrollbars(c);
                if (avail2 >= mn - 1e-6 && c->ml + avail2 + c->mr >= -1e-9) break;
            }
            ny = fc_next_below(s->f, y);
            if (ny <= y + 1e-9) break;
            y = ny;
        }
        c->pub.x = line_left;
        if (wauto) {
            double avail2 = right_lim - c->mr - line_left;
            if (avail2 < 0) avail2 = 0;
            iw = lay_clampw(c, lay_inner_from_border_w(c, avail2), s->cw, 1);
            c->pub.w = lay_border_from_inner_w(c, iw);
            c->pub.content_w = lay_dmax(0.0, iw - c->sbw);
        }
        if (!s->resolved) bfs_seal(s, y);
        sealed = 1;
    }
    if (sealed) c->pub.y = y;

    /* definite height (percentage base for descendants) */
    {
        int have_h = lay_res(c->cs->height, s->cch, s->cch_def, &spec);
        double inner_h = 0;
        if (have_h) inner_h = lay_clamph(c, lay_spec_inner_h(c, spec), s->cch, s->cch_def);
        else if (c->is_repl) { have_h = 1; inner_h = lay_clamph(c, c->cs->intrinsic_h, s->cch, s->cch_def); }
        c->pub.content_h = have_h ? lay_dmax(0.0, inner_h - c->sbh) : 0.0;
        c->h_definite = have_h ? 1u : 0u;

        run_contents(d, c, &cs, c->is_bfc ? &cf : NULL, s,
                     c->pub.x, sealed, y,
                     have_h ? c->pub.content_h : 0.0, have_h);

        {
            double ch_collapsed, ch_full, min_h = 0;
            int collapse_bottom;
            mstrut trailing = cs.st;

            if (!cs.resolved) {
                /* nothing inside forced a position: is the box self-collapsing? */
                double used_h = have_h ? inner_h : 0.0;
                int zero_min = !lay_res(c->cs->min_height, s->cch, s->cch_def, &min_h)
                               || min_h <= 0;
                int self_collapsing = !sealed && !cs.sealed_inside && used_h == 0 &&
                                      zero_min && c->bb == 0 && c->pb == 0 &&
                                      !c->is_bfc && !c->has_clearance && c->sbh == 0;
                if (self_collapsing) {
                    /* §8.3.1: the margins collapse through this box. */
                    c->pub.h = 0;
                    c->pub.content_h = 0;
                    s->st = cs.st;
                    if (s->resolved) {
                        lay_set_subtree_y(c, s->flow + ms_val(&s->st));
                    } else {
                        bfs_add_pending(s, c);
                    }
                    ms_add(&s->st, c->mb);
                    lay_finish_scrollable(c);
                    return;
                }
                y = cs.flow + ms_val(&cs.st);
                bfs_seal(&cs, y);
                trailing = cs.st;                 /* now empty: consumed above */
            }

            ch_collapsed = cs.flow - cs.cy;
            ch_full = ch_collapsed + ms_val(&trailing);
            min_h = 0;
            lay_res(c->cs->min_height, s->cch, s->cch_def, &min_h);
            if (min_h > 0) min_h = lay_spec_inner_h(c, min_h);

            collapse_bottom = !have_h && c->bb == 0 && c->pb == 0 &&
                              !c->is_bfc && c->sbh == 0;
            /* A min-height that actually binds separates the margins. */
            if (collapse_bottom && ch_collapsed < min_h) collapse_bottom = 0;

            if (!have_h) {
                inner_h = collapse_bottom ? ch_collapsed : ch_full;
                if (inner_h < 0) inner_h = 0;
                if (c->is_bfc) {
                    double lo = fc_lowest(&cf);
                    if (cf.n > 0 && lo - cs.cy > inner_h) inner_h = lo - cs.cy;
                }
                inner_h = lay_clamph(c, inner_h + c->sbh, s->cch, s->cch_def);
            }
            c->pub.y = cs.cy - c->bt - c->pt;
            c->pub.h = lay_border_from_inner_h(c, inner_h);
            c->pub.content_h = lay_dmax(0.0, inner_h - c->sbh);

            s->flow = c->pub.y + c->pub.h;
            if (collapse_bottom) { s->st = trailing; }
            else                 { ms_clear(&s->st); }
            ms_add(&s->st, c->mb);
        }
    }
    lay_finish_scrollable(c);
}

/* -------------------------------------------- §9.3.2 relative positioning */
static void apply_relative(lay_doc *d, box *b) {
    lay_box *c;
    if (b->cs->display == D_NONE) return;
    for (c = b->pub.first_child; c; c = c->next) apply_relative(d, (box *)c);
    if (b->cs->position == P_RELATIVE) {
        box *p = (box *)b->pub.parent;
        double cbw = p ? p->pub.content_w : d->vw;
        double cbh = p ? p->pub.content_h : d->vh;
        int cbh_def = p ? (int)p->h_definite : 1;
        direction_t cbdir = p ? p->cs->direction : icb_dir(d);

        double dx = 0, dy = 0, v;
        int la = lay_is_auto(b->cs->inset[EDGE_LEFT]);
        int ra = lay_is_auto(b->cs->inset[EDGE_RIGHT]);
        int ta = lay_is_auto(b->cs->inset[EDGE_TOP]);
        int ba = lay_is_auto(b->cs->inset[EDGE_BOTTOM]);
        /* Percentages against an indefinite containing-block height behave as
           'auto' and therefore as zero. */
        if (!la && lay_res(b->cs->inset[EDGE_LEFT], cbw, 1, &v)) dx = v;
        else if (!ra && lay_res(b->cs->inset[EDGE_RIGHT], cbw, 1, &v)) dx = -v;
        if (!la && !ra && lay_res(b->cs->inset[EDGE_RIGHT], cbw, 1, &v)) {
            /* §9.4.3: over-constrained.  As in §10.3.3 it is the CONTAINING
               BLOCK's direction that says which offset wins, not the box's. */
            if (cbdir == DIR_RTL) dx = -v;
        }
        if (!ta && lay_res(b->cs->inset[EDGE_TOP], cbh, cbh_def, &v)) dy = v;
        else if (!ba && lay_res(b->cs->inset[EDGE_BOTTOM], cbh, cbh_def, &v)) dy = -v;
        /* §9.4.3: the offset that loses is the negation of the one that wins. */
        b->pub.inset[EDGE_LEFT] = dx; b->pub.inset[EDGE_RIGHT]  = -dx;
        b->pub.inset[EDGE_TOP]  = dy; b->pub.inset[EDGE_BOTTOM] = -dy;
        if (dx != 0 || dy != 0) lay_shift_subtree(b, dx, dy);
    }
}

/* ------------------------------------ §10.1/§10.3.7/§10.6.4 absolute boxes */

/* Append a box to the absolute-positioning queue.  Idempotent: a box that is
   measured twice (a shrink-to-fit pass, a float re-placed) must not be linked
   into the list twice, which would make next_abs cyclic. */
void lay_enqueue_abs(lay_doc *d, box *b) {
    if (b->abs_queued) return;
    b->abs_queued = 1;
    b->next_abs = NULL;
    if (d->abs_tail) d->abs_tail->next_abs = b; else d->abs_head = b;
    d->abs_tail = b;
}

/* §10.1.4.3 static position for a box discovered inside an inline formatting
   context.  (abs_x, abs_y) is where the hypothetical box's top-left corner
   would fall on the canvas; static_x/static_y are stored relative to the box
   tree parent's content box, which is the frame abs_layout() reads them in. */
void lay_queue_abs_at(bfs *s, box *b, double abs_x, double abs_y) {
    box *par = (box *)b->pub.parent;
    double pcx = 0.0, pcy = 0.0;

    if (b->abs_queued) return;
    if (par) {
        pcx = par->pub.x + par->bl + par->pl +
              (par->cs->direction == DIR_RTL ? par->sbw : 0.0);
        pcy = par->pub.y + par->bt + par->pt;
    }
    b->static_x = abs_x - pcx;
    b->static_y = abs_y - pcy;
    b->static_rtl = (s->owner->cs->direction == DIR_RTL) ? 1u : 0u;
    lay_enqueue_abs(s->d, b);
}

/* §10.1: the initial containing block has the dimensions of the viewport and
   is anchored at the canvas origin; its direction is the root element's. */
static direction_t icb_dir(lay_doc *d) {
    return (d->root && d->root->cs) ? d->root->cs->direction : DIR_LTR;
}

static void abs_containing_block(lay_doc *d, box *b, double *cbx, double *cby,
                                 double *cbw, double *cbh, direction_t *cbdir) {
    lay_box *p;
    if (b->cs->position == P_FIXED || !b->pub.parent) {
        *cbx = 0; *cby = 0; *cbw = d->vw; *cbh = d->vh; *cbdir = icb_dir(d);
        return;
    }
    for (p = b->pub.parent; p; p = p->parent) {
        box *pb = (box *)p;
        if (pb->cs->position != P_STATIC) {
            /* The containing block is the padding box less the scrollbar, and
               in a right-to-left box the scrollbar is on the LEFT, so it moves
               the left edge rather than the right one. */
            {   /* The gutter cannot take more room than the padding box has. */
                double pw = lay_dmax(0.0, pb->pub.w - pb->bl - pb->br);
                double sb = lay_dmin(pb->sbw, pw);
                *cbx = pb->pub.x + pb->bl +
                       (pb->cs->direction == DIR_RTL ? sb : 0.0);
            }
            *cby = pb->pub.y + pb->bt;
            *cbw = lay_dmax(0.0, pb->pub.w - pb->bl - pb->br - pb->sbw);
            *cbh = lay_dmax(0.0, pb->pub.h - pb->bt - pb->bb - pb->sbh);
            *cbdir = pb->cs->direction;
            return;
        }
    }
    /* §10.1 item 1: every ancestor is 'static', so the containing block is the
       initial containing block. */
    *cbx = 0; *cby = 0; *cbw = d->vw; *cbh = d->vh; *cbdir = icb_dir(d);
}

static void abs_layout(lay_doc *d, box *b) {
    double cbx, cby, cbw, cbh;
    double edges_w, edges_h, iw, ih = 0, L = 0, R = 0, T = 0, B = 0, spec;
    int la, ra, wa, ta, ba, ha, mla, mra, mta, mba;
    double stat_l = 0, stat_r = 0, stat_t = 0;
    double used_ml, used_mr;
    direction_t cbdir = DIR_LTR;
    int have_h;

    if (b->cs->display == D_NONE) return;
    abs_containing_block(d, b, &cbx, &cby, &cbw, &cbh, &cbdir);
    lay_edges(b, cbw, 1);
    lay_scrollbars(b);
    edges_w = b->bl + b->br + b->pl + b->pr;
    edges_h = b->bt + b->bb + b->pt + b->pb;

    {   /* static position from the parent's content box (§10.1.4.3) */
        box *par = (box *)b->pub.parent;
        if (par) {
            double pcx = par->pub.x + par->bl + par->pl +
                         (par->cs->direction == DIR_RTL ? par->sbw : 0.0);
            double pcy = par->pub.y + par->bt + par->pt;
            stat_l = (pcx + b->static_x) - cbx;
            stat_t = (pcy + b->static_y) - cby;
            stat_r = (cbx + cbw) - (pcx + par->pub.content_w);
        }
    }

    la = lay_is_auto(b->cs->inset[EDGE_LEFT]);
    ra = lay_is_auto(b->cs->inset[EDGE_RIGHT]);
    ta = lay_is_auto(b->cs->inset[EDGE_TOP]);
    ba = lay_is_auto(b->cs->inset[EDGE_BOTTOM]);
    wa = !lay_res(b->cs->width, cbw, 1, &spec);
    /* `stretch` fills the containing block rather than shrinking to fit, so
       the offset that is 'auto' resolves to 0 and the width falls out of the
       10.3.7 equation instead of being measured. */
    if (b->cs->width.kind == LEN_STRETCH) {
        if (la && !ra)      { L = 0; la = 0; }
        else if (ra && !la) { R = 0; ra = 0; }
        else if (la && ra)  { L = b->static_rtl ? L : stat_l; R = 0; la = 0; ra = 0; }
    }
    if (b->cs->height.kind == LEN_STRETCH) {
        if (ta && !ba)      { T = 0; ta = 0; }
        else if (ba && !ta) { B = 0; ba = 0; }
    }
    mla = lay_is_auto(b->cs->margin[EDGE_LEFT]);
    mra = lay_is_auto(b->cs->margin[EDGE_RIGHT]);
    mta = lay_is_auto(b->cs->margin[EDGE_TOP]);
    mba = lay_is_auto(b->cs->margin[EDGE_BOTTOM]);
    if (!la) lay_res(b->cs->inset[EDGE_LEFT], cbw, 1, &L);
    if (!ra) lay_res(b->cs->inset[EDGE_RIGHT], cbw, 1, &R);
    if (!ta) lay_res(b->cs->inset[EDGE_TOP], cbh, 1, &T);
    if (!ba) lay_res(b->cs->inset[EDGE_BOTTOM], cbh, 1, &B);
    iw = wa ? 0 : lay_spec_inner_w(b, spec);

    {
        int pass;
        for (pass = 0; pass < 2; pass++) {
            double clamped;
            if (mla) b->ml = 0;
            if (mra) b->mr = 0;
            if (la && ra && wa) {
                double av;
                if (b->static_rtl) { R = stat_r; av = cbw - R - b->ml - b->mr - edges_w; }
                else               { L = stat_l; av = cbw - L - b->ml - b->mr - edges_w; }
                {
                    double mn = lay_intrinsic(d, b, 1, 0);
                    double mx = lay_intrinsic(d, b, 0, 0);
                    double sf;
                    lay_edges(b, cbw, 1);
                    lay_scrollbars(b);
                    if (mla) b->ml = 0;
                    if (mra) b->mr = 0;
                    sf = lay_dmin(lay_dmax(mn, av), mx);
                    if (sf < mn) sf = mn;
                    iw = lay_inner_from_border_w(b, sf);
                }
                if (b->static_rtl) L = cbw - R - b->ml - b->mr - edges_w - iw;
                else               R = cbw - L - b->ml - b->mr - edges_w - iw;
            } else if (!la && !ra && !wa) {
                double rest = cbw - L - R - edges_w - iw;
                if (mla && mra) {
                    if (rest < 0) { b->ml = 0; b->mr = rest; }
                    else { b->ml = rest / 2.0; b->mr = rest - rest / 2.0; }
                } else if (mla) b->ml = rest - b->mr;
                else if (mra)  b->mr = rest - b->ml;
                else {
                    /* §10.3.7: over-constrained.  It is the CONTAINING BLOCK's
                       direction that decides which offset is ignored - 'right'
                       when it is ltr, 'left' when it is rtl - not the box's. */
                    if (cbdir == DIR_RTL) L = cbw - R - b->ml - b->mr - edges_w - iw;
                    else                  R = cbw - L - b->ml - b->mr - edges_w - iw;
                }
            } else if (la && wa) {            /* solve width then left */
                double av = cbw - R - b->ml - b->mr - edges_w;
                double mn = lay_intrinsic(d, b, 1, 0);
                double mx = lay_intrinsic(d, b, 0, 0);
                double sf;
                lay_edges(b, cbw, 1); lay_scrollbars(b);
                if (mla) b->ml = 0;
                if (mra) b->mr = 0;
                sf = lay_dmin(lay_dmax(mn, av), mx);
                if (sf < mn) sf = mn;
                iw = lay_inner_from_border_w(b, sf);
                L = cbw - R - b->ml - b->mr - edges_w - iw;
            } else if (ra && wa) {            /* solve width then right */
                double av = cbw - L - b->ml - b->mr - edges_w;
                double mn = lay_intrinsic(d, b, 1, 0);
                double mx = lay_intrinsic(d, b, 0, 0);
                double sf;
                lay_edges(b, cbw, 1); lay_scrollbars(b);
                if (mla) b->ml = 0;
                if (mra) b->mr = 0;
                sf = lay_dmin(lay_dmax(mn, av), mx);
                if (sf < mn) sf = mn;
                iw = lay_inner_from_border_w(b, sf);
                R = cbw - L - b->ml - b->mr - edges_w - iw;
            } else if (la && ra) {            /* width known, both insets auto */
                if (b->static_rtl) { R = stat_r; L = cbw - R - b->ml - b->mr - edges_w - iw; }
                else               { L = stat_l; R = cbw - L - b->ml - b->mr - edges_w - iw; }
            } else if (la) {
                L = cbw - R - b->ml - b->mr - edges_w - iw;
            } else if (ra) {
                R = cbw - L - b->ml - b->mr - edges_w - iw;
            } else {                          /* width auto, both insets set */
                iw = cbw - L - R - b->ml - b->mr - edges_w;
                if (iw < 0) iw = 0;
            }
            clamped = lay_clampw(b, iw, cbw, 1);
            if (pass == 0 && clamped != iw) { iw = clamped; wa = 0; continue; }
            iw = clamped;
            break;
        }
    }

    b->pub.w = lay_border_from_inner_w(b, iw);
    b->pub.content_w = lay_dmax(0.0, iw - b->sbw);
    b->pub.x = cbx + L + b->ml;
    /* lay_layout_independent() below re-resolves the edges from the computed
       style, which would drop an 'auto' margin the width equation just solved
       for.  Keep the used values and put them back once the box is sized. */
    used_ml = b->ml; used_mr = b->mr;

    /* height (§10.6.4) */
    ha = !lay_res(b->cs->height, cbh, 1, &spec);
    have_h = 0;
    if (!ha) { ih = lay_spec_inner_h(b, spec); have_h = 1; }
    else if (b->is_repl) { ih = b->cs->intrinsic_h; have_h = 1; }
    else if (!ta && !ba) {
        ih = cbh - T - B - (mta ? 0 : b->mt) - (mba ? 0 : b->mb) - edges_h;
        if (ih < 0) ih = 0;
        have_h = 1;
    }
    if (have_h) {
        ih = lay_clamph(b, ih, cbh, 1);
        b->pub.content_h = lay_dmax(0.0, ih - b->sbh);
        b->pub.h = lay_border_from_inner_h(b, ih);
        lay_layout_independent(d, b, b->pub.x, cby, cbw, 3, cbh, 1);
    } else {
        lay_layout_independent(d, b, b->pub.x, cby, cbw, 2, cbh, 1);
        ih = lay_inner_from_border_h(b, b->pub.h);
    }

    if (mta) b->mt = 0;
    if (mba) b->mb = 0;
    if (ta && ba) {
        T = stat_t;
    } else if (ta) {
        T = cbh - B - b->mt - b->mb - edges_h - ih;
    } else if (ba) {
        /* T is known */
    } else {
        double rest = cbh - T - B - edges_h - ih;
        if (mta && mba) {
            if (rest < 0) { b->mt = 0; b->mb = rest; }
            else { b->mt = rest / 2.0; b->mb = rest - rest / 2.0; }
        } else if (lay_is_auto(b->cs->margin[EDGE_TOP])) {
            b->mt = rest - b->mb;
        } else if (lay_is_auto(b->cs->margin[EDGE_BOTTOM])) {
            b->mb = rest - b->mt;
        }
    }
    lay_shift_subtree(b, 0, (cby + T + b->mt) - b->pub.y);
    b->ml = used_ml; b->mr = used_mr;

    /* Used offsets (§10.3.7, §10.6.4).  The end offsets follow from the
       solved-for start offset, which is also the right answer when the box was
       over-constrained and one of them was ignored. */
    b->pub.inset[EDGE_LEFT]   = L;
    b->pub.inset[EDGE_RIGHT]  = cbw - L - b->ml - b->mr - edges_w - iw;
    b->pub.inset[EDGE_TOP]    = T;
    b->pub.inset[EDGE_BOTTOM] = cbh - T - b->mt - b->mb - edges_h - ih;
    lay_finish_scrollable(b);
}

void lay_run_abs_queue(lay_doc *d) {
    while (d->abs_head) {
        box *b = d->abs_head;
        d->abs_head = b->next_abs;
        if (!d->abs_head) d->abs_tail = NULL;
        b->next_abs = NULL;
        abs_layout(d, b);
    }
    if (d->root) apply_relative(d, d->root);
}
