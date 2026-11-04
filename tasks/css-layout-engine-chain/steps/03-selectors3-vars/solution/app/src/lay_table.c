/* lay_table.c - CSS 2.1 §17 tables, separated borders model only.
   'border-collapse: collapse' is not implemented.
   'border-spacing' is not expressible through the fixed `computed` struct, so
   the separated-borders geometry runs with the CSS 2.1 initial value, 0
   (§17.6.1); every use goes through bs_h/bs_v so a future property wires in. */
#include "lay_internal.h"
#include <string.h>
#include <stdlib.h>

#define MAXCOL 512

typedef struct {
    box **rows;  int nrows;
    box **cells; int ncells;      /* row-major, nrows * ncols, NULL when spanned */
    int ncols;
    double bs_h, bs_v;            /* border-spacing (§17.6.1 initial value: 0) */
} tgrid;

static void collect_rows(box *t, box **rows, int *n, int cap) {
    lay_box *c;
    for (c = t->pub.first_child; c; c = c->next) {
        box *b = (box *)c;
        if (b->cs->display == D_NONE || b->is_abs) continue;
        if (b->kind == BK_TROWGROUP) collect_rows(b, rows, n, cap);
        else if (b->kind == BK_TROW && *n < cap) rows[(*n)++] = b;
    }
}

static void build_grid(lay_doc *d, box *t, tgrid *g) {
    int r, ncols = 0;
    box **rows = (box **)lay_alloc(&d->a, sizeof(box *) * MAXCOL);
    g->nrows = 0;
    collect_rows(t, rows, &g->nrows, MAXCOL);
    g->rows = rows;
    g->bs_h = 0.0;
    g->bs_v = 0.0;
    for (r = 0; r < g->nrows; r++) {
        lay_box *c;
        int w = 0;
        for (c = g->rows[r]->pub.first_child; c; c = c->next) {
            box *cell = (box *)c;
            if (cell->cs->display == D_NONE || cell->is_abs) continue;
            if (cell->kind != BK_TCELL) continue;
            if (cell->col_span < 1) cell->col_span = 1;
            w += cell->col_span;
        }
        if (w > ncols) ncols = w;
    }
    if (ncols > MAXCOL) ncols = MAXCOL;
    g->ncols = ncols;
    g->cells = (box **)lay_alloc(&d->a, sizeof(box *) *
                                 (size_t)(g->nrows > 0 ? g->nrows : 1) *
                                 (size_t)(ncols > 0 ? ncols : 1));
    for (r = 0; r < g->nrows; r++) {
        lay_box *c;
        int col = 0;
        for (c = g->rows[r]->pub.first_child; c; c = c->next) {
            box *cell = (box *)c;
            int k;
            if (cell->cs->display == D_NONE || cell->is_abs) continue;
            if (cell->kind != BK_TCELL) continue;
            while (col < ncols && g->cells[r * ncols + col]) col++;
            if (col >= ncols) break;
            cell->col_index = col;
            cell->row_index = r;
            for (k = 0; k < cell->col_span && col + k < ncols; k++)
                g->cells[r * ncols + col + k] = (k == 0) ? cell : NULL;
            g->cells[r * ncols + col] = cell;
            col += cell->col_span;
        }
    }
}

/* §17.5.2.2 automatic table layout: per column min/max content widths. */
static void column_widths(lay_doc *d, box *t, tgrid *g, double *mins, double *maxs) {
    int r, c;
    for (c = 0; c < g->ncols; c++) { mins[c] = 0; maxs[c] = 0; }
    for (r = 0; r < g->nrows; r++) {
        for (c = 0; c < g->ncols; c++) {
            box *cell = g->cells[r * g->ncols + c];
            double mn, mx, v;
            if (!cell || cell->col_index != c) continue;
            lay_edges(cell, 0, 0);
            mn = lay_intrinsic(d, cell, 1, 0);
            mx = lay_intrinsic(d, cell, 0, 0);
            if (lay_res(cell->cs->width, 0, 0, &v)) {
                double sw = (cell->cs->box_sizing == BS_BORDER)
                          ? v : v + cell->bl + cell->br + cell->pl + cell->pr;
                if (sw > mn) mn = sw;
                if (sw > mx) mx = sw;
            }
            if (cell->col_span == 1) {
                if (mn > mins[c]) mins[c] = mn;
                if (mx > maxs[c]) maxs[c] = mx;
            } else {
                /* distribute a spanning cell over its columns (§17.5.2.2) */
                int k, ns = cell->col_span;
                double sm = 0, sx = 0;
                for (k = 0; k < ns && c + k < g->ncols; k++) { sm += mins[c + k]; sx += maxs[c + k]; }
                if (mn > sm) {
                    double extra = (mn - sm) / (double)ns;
                    for (k = 0; k < ns && c + k < g->ncols; k++) mins[c + k] += extra;
                }
                if (mx > sx) {
                    double extra = (mx - sx) / (double)ns;
                    for (k = 0; k < ns && c + k < g->ncols; k++) maxs[c + k] += extra;
                }
            }
        }
    }
    (void)t;
}

double lay_table_intrinsic(lay_doc *d, box *t, int minimum) {
    tgrid g;
    double *mins, *maxs, sum = 0;
    int c;
    memset(&g, 0, sizeof g);
    build_grid(d, t, &g);
    if (g.ncols == 0) return lay_border_from_inner_w(t, 0);
    mins = (double *)lay_alloc(&d->a, sizeof(double) * (size_t)g.ncols);
    maxs = (double *)lay_alloc(&d->a, sizeof(double) * (size_t)g.ncols);
    column_widths(d, t, &g, mins, maxs);
    for (c = 0; c < g.ncols; c++) sum += minimum ? mins[c] : maxs[c];
    sum += g.bs_h * (double)(g.ncols + 1);
    return lay_border_from_inner_w(t, sum);
}

/* Lay out the contents of a table whose border-box x/y and content width are
   already set.  Fills pub.content_h. */
void lay_table_contents(lay_doc *d, box *t) {
    tgrid g;
    double *mins, *maxs, *w;
    double avail, sum_min = 0, sum_max = 0, total = 0;
    double x0, y0, y;
    int r, c;
    int fixed = 0;

    memset(&g, 0, sizeof g);
    build_grid(d, t, &g);
    x0 = t->pub.x + t->bl + t->pl;
    y0 = t->pub.y + t->bt + t->pt;
    avail = t->pub.content_w;

    if (g.ncols == 0) {
        t->pub.content_h = 0;
        return;
    }
    mins = (double *)lay_alloc(&d->a, sizeof(double) * (size_t)g.ncols);
    maxs = (double *)lay_alloc(&d->a, sizeof(double) * (size_t)g.ncols);
    w    = (double *)lay_alloc(&d->a, sizeof(double) * (size_t)g.ncols);

    /* §17.5.2.1 fixed layout: the first row's cells (and any specified widths)
       decide the columns; used when the table has a definite width and the
       remaining columns simply share what is left. */
    if (!lay_is_auto(t->cs->width) && g.nrows > 0) {
        int known = 0;
        double used = 0;
        for (c = 0; c < g.ncols; c++) w[c] = -1;
        for (c = 0; c < g.ncols; c++) {
            box *cell = g.cells[0 * g.ncols + c];
            double v;
            if (!cell || cell->col_index != c) continue;
            lay_edges(cell, avail, 1);
            if (lay_res(cell->cs->width, avail, 1, &v)) {
                double bw = (cell->cs->box_sizing == BS_BORDER)
                          ? v : v + cell->bl + cell->br + cell->pl + cell->pr;
                int k;
                for (k = 0; k < cell->col_span && c + k < g.ncols; k++)
                    w[c + k] = bw / (double)cell->col_span;
                known += cell->col_span;
                used += bw;
            }
        }
        if (known > 0) {
            double rest = avail - used - g.bs_h * (double)(g.ncols + 1);
            int nunk = g.ncols - known;
            for (c = 0; c < g.ncols; c++)
                if (w[c] < 0) w[c] = nunk > 0 ? (rest > 0 ? rest / nunk : 0) : 0;
            fixed = 1;
        }
    }

    if (!fixed) {
        column_widths(d, t, &g, mins, maxs);
        for (c = 0; c < g.ncols; c++) { sum_min += mins[c]; sum_max += maxs[c]; }
        {
            double spacing = g.bs_h * (double)(g.ncols + 1);
            double target = avail - spacing;
            if (target < sum_min) target = sum_min;
            if (!lay_is_auto(t->cs->width)) {
                /* definite table width: grow columns to fill it */
            } else if (target > sum_max) {
                target = sum_max;
            }
            if (sum_max > sum_min && target > sum_min) {
                double extra = target - sum_min;
                double span = sum_max - sum_min;
                for (c = 0; c < g.ncols; c++)
                    w[c] = mins[c] + extra * (maxs[c] - mins[c]) / span;
            } else if (target > sum_min && g.ncols > 0) {
                double extra = (target - sum_min) / g.ncols;
                for (c = 0; c < g.ncols; c++) w[c] = mins[c] + extra;
            } else {
                for (c = 0; c < g.ncols; c++) w[c] = mins[c];
            }
        }
    }
    for (c = 0; c < g.ncols; c++) total += w[c];
    total += g.bs_h * (double)(g.ncols + 1);
    if (lay_is_auto(t->cs->width)) {
        t->pub.content_w = total;
        t->pub.w = lay_border_from_inner_w(t, total);
    }

    /* rows and cells */
    y = y0 + g.bs_v;
    for (r = 0; r < g.nrows; r++) {
        box *row = g.rows[r];
        double rh = 0, cx = x0 + g.bs_h;
        double specified_rh = 0;
        int have_rh;
        lay_edges(row, t->pub.content_w, 1);
        row->pub.x = x0;
        row->pub.y = y;
        row->pub.w = total - 2 * g.bs_h;
        have_rh = lay_res(row->cs->height, 0, 0, &specified_rh);

        for (c = 0; c < g.ncols; c++) {
            box *cell = g.cells[r * g.ncols + c];
            double cw = 0;
            int k;
            if (!cell || cell->col_index != c) {
                cx += w[c] + g.bs_h;
                continue;
            }
            for (k = 0; k < cell->col_span && c + k < g.ncols; k++)
                cw += w[c + k] + (k ? g.bs_h : 0);
            lay_edges(cell, cw, 1);
            lay_scrollbars(cell);
            cell->pub.w = cw;
            cell->pub.content_w = lay_inner_from_border_w(cell, cw) - cell->sbw;
            if (cell->pub.content_w < 0) cell->pub.content_w = 0;
            lay_layout_independent(d, cell, cx, y, cw, 2 /* width already fixed */,
                                   0, 0);
            if (cell->pub.h > rh) rh = cell->pub.h;
            cx += w[c] + g.bs_h;
        }
        if (have_rh && specified_rh > rh) rh = specified_rh;
        /* cells stretch to the row height (§17.5.3 vertical-align: top default) */
        for (c = 0; c < g.ncols; c++) {
            box *cell = g.cells[r * g.ncols + c];
            if (!cell || cell->col_index != c) continue;
            cell->pub.h = rh;
            cell->pub.content_h = lay_inner_from_border_h(cell, rh) - cell->sbh;
            if (cell->pub.content_h < 0) cell->pub.content_h = 0;
            lay_finish_scrollable(cell);
        }
        row->pub.h = rh;
        row->pub.content_h = rh;
        row->pub.content_w = lay_inner_from_border_w(row, row->pub.w);
        y += rh + g.bs_v;
    }
    /* §17.2: row groups and column groups generate boxes too.  A row group
       spans the rows it holds; a column group spans the columns it covers,
       over the full height of the table's content. */
    {
        lay_box *cc;
        double table_h = y - y0;
        double colx = x0 + g.bs_h;
        int col = 0;
        for (cc = t->pub.first_child; cc; cc = cc->next) {
            box *ch = (box *)cc;
            if (ch->cs->display == D_NONE || ch->is_abs) continue;
            if (ch->kind == BK_TROWGROUP) {
                box **gr = (box **)lay_alloc(&d->a, sizeof(box *) * MAXCOL);
                int n2 = 0, k;
                double gy0, gy1;
                collect_rows(ch, gr, &n2, MAXCOL);
                if (n2 == 0) continue;
                gy0 = gr[0]->pub.y;
                gy1 = gr[0]->pub.y + gr[0]->pub.h;
                for (k = 1; k < n2; k++) {
                    if (gr[k]->pub.y < gy0) gy0 = gr[k]->pub.y;
                    if (gr[k]->pub.y + gr[k]->pub.h > gy1)
                        gy1 = gr[k]->pub.y + gr[k]->pub.h;
                }
                lay_edges(ch, t->pub.content_w, 1);
                ch->pub.x = x0;
                ch->pub.y = gy0;
                ch->pub.w = total - 2 * g.bs_h;
                ch->pub.h = gy1 - gy0;
                ch->pub.content_w = lay_inner_from_border_w(ch, ch->pub.w);
                ch->pub.content_h = lay_inner_from_border_h(ch, ch->pub.h);
            } else if (ch->kind == BK_TCOLGROUP || ch->kind == BK_TCOL) {
                int span = 0, k;
                double cw2 = 0;
                if (ch->kind == BK_TCOLGROUP) {
                    lay_box *gc;
                    for (gc = ch->pub.first_child; gc; gc = gc->next) {
                        box *cb2 = (box *)gc;
                        if (cb2->kind == BK_TCOL)
                            span += cb2->col_span > 0 ? cb2->col_span : 1;
                    }
                }
                if (span <= 0) span = ch->col_span > 0 ? ch->col_span : 1;
                for (k = 0; k < span && col + k < g.ncols; k++)
                    cw2 += w[col + k] + (k ? g.bs_h : 0);
                lay_edges(ch, t->pub.content_w, 1);
                ch->pub.x = colx;
                ch->pub.y = y0 + g.bs_v;
                ch->pub.w = cw2;
                ch->pub.h = lay_dmax(0.0, table_h - 2 * g.bs_v);
                ch->pub.content_w = lay_inner_from_border_w(ch, ch->pub.w);
                ch->pub.content_h = lay_inner_from_border_h(ch, ch->pub.h);
                for (k = 0; k < span && col + k < g.ncols; k++)
                    colx += w[col + k] + g.bs_h;
                col += span;
            }
        }
    }
    t->pub.content_h = y - y0;
}
