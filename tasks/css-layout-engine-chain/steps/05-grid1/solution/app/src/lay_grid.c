/* css-grid-1 §8 placement and §11 track sizing.
   A grid container establishes an independent formatting context, so this is
   entered from run_contents() in place of layout_contents() -- both callers
   funnel through that one line. */
#include "lay_internal.h"
#include <string.h>

enum { GMAXTRACK = 256 };

typedef struct {
  box *b;
  int c0, c1, r0, r1;          /* half-open track ranges, 0-based */
  double ml, mr, mt, mb;
} gitem;

/* §8.3: a line number is 1-based and may count from the end. */
static int line_index(gridpos p, int explicit_n, int *span_out)
{
  *span_out = 1;
  if (p.kind == GP_SPAN) { *span_out = p.v; return -1; }
  if (p.kind == GP_LINE) {
    if (p.v > 0) return p.v - 1;
    return explicit_n + 1 + p.v - 1;   /* -1 is the last line */
  }
  return -1;                            /* auto */
}

static void place_axis(gridpos s, gridpos e, int explicit_n, int *out0, int *out1)
{
  int sp_s, sp_e, a, b;
  a = line_index(s, explicit_n, &sp_s);
  b = line_index(e, explicit_n, &sp_e);
  if (a >= 0 && b >= 0)      { *out0 = a; *out1 = b > a ? b : a + 1; return; }
  if (a >= 0 && e.kind == GP_SPAN) { *out0 = a; *out1 = a + sp_e; return; }
  if (a >= 0)                { *out0 = a; *out1 = a + 1; return; }
  if (b >= 0 && s.kind == GP_SPAN) { *out1 = b; *out0 = b - sp_s > 0 ? b - sp_s : 0; return; }
  if (b >= 0)                { *out1 = b; *out0 = b - 1 > 0 ? b - 1 : 0; return; }
  *out0 = -1;                                     /* auto-placed */
  *out1 = (s.kind == GP_SPAN) ? sp_s : ((e.kind == GP_SPAN) ? sp_e : 1);
}

/* §11: resolve one track list into base sizes.  Intrinsic tracks take the
   largest contribution of the items that span only them, which is the common
   case the fixtures exercise; items spanning several intrinsic tracks
   contribute to none (§11.5 distributes those, and we do not). */
static void size_tracks(lay_doc *d, const track *tl, int n, double avail,
                        int avail_def, double gap, gitem *it, int ni,
                        int axis_col, double *base)
{
  int i, k;
  double used = 0, fr_sum = 0, leftover;

  for (k = 0; k < n; k++) {
    double v;
    base[k] = 0;
    switch (tl[k].kind) {
      case TK_LEN:
        if (lay_res(tl[k].len, avail, avail_def, &v)) base[k] = v;
        break;
      case TK_FR:
        fr_sum += tl[k].fr;
        break;
      default:                                  /* auto / min- / max-content */
        for (i = 0; i < ni; i++) {
          int s0 = axis_col ? it[i].c0 : it[i].r0;
          int s1 = axis_col ? it[i].c1 : it[i].r1;
          double c;
          if (s0 != k || s1 != k + 1) continue;   /* spans more: skip */
          c = axis_col
              ? lay_intrinsic(d, it[i].b, tl[k].kind == TK_MIN_CONTENT, avail)
              : 0.0;
          if (!axis_col) {
            lay_layout_independent(d, it[i].b, 0, 0,
                                   avail_def ? avail : 0, 0, 0, 0);
            c = it[i].b->pub.h;
          }
          if (c > base[k]) base[k] = c;
        }
        break;
    }
  }
  for (k = 0; k < n; k++) used += base[k];
  if (n > 1) used += gap * (n - 1);
  leftover = (avail_def ? avail : used) - used;
  if (leftover < 0) leftover = 0;
  if (fr_sum > 0)
    for (k = 0; k < n; k++)
      if (tl[k].kind == TK_FR) base[k] = leftover * (tl[k].fr / fr_sum);
}

static double span_size(const double *base, double gap, int a, int b)
{
  double t = 0;
  int k;
  for (k = a; k < b; k++) t += base[k];
  if (b - a > 1) t += gap * (b - a - 1);
  return t;
}

void lay_grid_content(lay_doc *d, box *b, double inner_w, int have_h,
                      double inner_h)
{
  const computed *cs = b->cs;
  lay_box *c;
  gitem *it;
  int n = 0, i, ncol, nrow, next_r = 0, next_c = 0;
  double *cbase, *rbase, *coff, *roff;
  double rgap = 0, cgap = 0, total_h = 0;

  lay_res(cs->row_gap, have_h ? inner_h : 0, have_h, &rgap);
  lay_res(cs->column_gap, inner_w, 1, &cgap);

  for (c = b->pub.first_child; c; c = c->next) {
    box *cb = (box *)c;
    if (cb->cs->display == D_NONE) continue;
    if (cb->is_abs) { lay_enqueue_abs(d, cb); continue; }
    n++;
  }
  if (n == 0) { b->pub.content_h = have_h ? inner_h : 0; return; }

  it = (gitem *)lay_alloc(&d->a, (size_t)n * sizeof *it);
  memset(it, 0, (size_t)n * sizeof *it);
  i = 0;
  for (c = b->pub.first_child; c; c = c->next) {
    box *cb = (box *)c;
    if (cb->cs->display == D_NONE || cb->is_abs) continue;
    cb->is_float = 0u; cb->in_flow = 1u;
    it[i].b = cb;
    i++;
  }

  ncol = cs->n_grid_cols > 0 ? cs->n_grid_cols : 1;
  nrow = cs->n_grid_rows > 0 ? cs->n_grid_rows : 0;

  /* §8.3 explicit placement, then §8.5 auto-placement in row-major order. */
  for (i = 0; i < n; i++) {
    lay_edges(it[i].b, inner_w, 1);
    lay_scrollbars(it[i].b);
    it[i].ml = it[i].b->ml; it[i].mr = it[i].b->mr;
    it[i].mt = it[i].b->mt; it[i].mb = it[i].b->mb;
    place_axis(it[i].b->cs->grid_col_start, it[i].b->cs->grid_col_end,
               cs->n_grid_cols, &it[i].c0, &it[i].c1);
    place_axis(it[i].b->cs->grid_row_start, it[i].b->cs->grid_row_end,
               cs->n_grid_rows, &it[i].r0, &it[i].r1);
  }
  for (i = 0; i < n; i++) {
    int span_c = it[i].c1 - (it[i].c0 < 0 ? 0 : it[i].c0);
    if (it[i].c0 < 0) {
      if (span_c < 1) span_c = 1;
      if (next_c + span_c > ncol) { next_c = 0; next_r++; }
      it[i].c0 = next_c; it[i].c1 = next_c + span_c;
      next_c += span_c;
    }
    if (it[i].r0 < 0) {
      int span_r = it[i].r1 < 1 ? 1 : it[i].r1;
      it[i].r0 = next_r; it[i].r1 = next_r + span_r;
    }
    if (it[i].c1 > ncol) ncol = it[i].c1;
    if (it[i].r1 > nrow) nrow = it[i].r1;
  }
  if (ncol < 1) ncol = 1;
  if (nrow < 1) nrow = 1;
  if (ncol > GMAXTRACK) ncol = GMAXTRACK;
  if (nrow > GMAXTRACK) nrow = GMAXTRACK;

  cbase = (double *)lay_alloc(&d->a, (size_t)(ncol + 1) * sizeof *cbase);
  rbase = (double *)lay_alloc(&d->a, (size_t)(nrow + 1) * sizeof *rbase);
  coff  = (double *)lay_alloc(&d->a, (size_t)(ncol + 2) * sizeof *coff);
  roff  = (double *)lay_alloc(&d->a, (size_t)(nrow + 2) * sizeof *roff);

  {
    /* implicit tracks are `auto`; build a full-length list */
    track *tl = (track *)lay_alloc(&d->a, (size_t)ncol * sizeof *tl);
    for (i = 0; i < ncol; i++) {
      if (i < cs->n_grid_cols) tl[i] = cs->grid_cols[i];
      else { tl[i].kind = TK_AUTO; tl[i].fr = 0; }
    }
    size_tracks(d, tl, ncol, inner_w, 1, cgap, it, n, 1, cbase);
  }
  {
    track *tl = (track *)lay_alloc(&d->a, (size_t)nrow * sizeof *tl);
    for (i = 0; i < nrow; i++) {
      if (i < cs->n_grid_rows) tl[i] = cs->grid_rows[i];
      else { tl[i].kind = TK_AUTO; tl[i].fr = 0; }
    }
    size_tracks(d, tl, nrow, have_h ? inner_h : 0, have_h, rgap, it, n, 0, rbase);
  }

  coff[0] = 0;
  for (i = 0; i < ncol; i++) coff[i + 1] = coff[i] + cbase[i] + cgap;
  roff[0] = 0;
  for (i = 0; i < nrow; i++) roff[i + 1] = roff[i] + rbase[i] + rgap;
  total_h = nrow > 0 ? roff[nrow] - rgap : 0;
  if (total_h < 0) total_h = 0;

  for (i = 0; i < n; i++) {
    box *cb = it[i].b;
    double aw = span_size(cbase, cgap, it[i].c0, it[i].c1);
    double ah = span_size(rbase, rgap, it[i].r0, it[i].r1);
    align_t js = cb->cs->justify_self == AL_AUTO ? cs->justify_items
                                                 : cb->cs->justify_self;
    align_t as = cb->cs->align_self == AL_AUTO ? cs->align_items
                                               : cb->cs->align_self;
    double iw, ih, dx = 0, dy = 0, tx, ty;

    /* §10.1: stretch is the default and applies only to an auto size. */
    if (js == AL_STRETCH && lay_is_auto(cb->cs->width))
      iw = aw - it[i].ml - it[i].mr;
    else {
      double spec;
      iw = lay_res(cb->cs->width, aw, 1, &spec) ? lay_spec_inner_w(cb, spec)
                                                : lay_intrinsic(d, cb, 0, aw);
      if (lay_is_auto(cb->cs->width)) iw = lay_inner_from_border_w(cb, iw);
    }
    if (iw < 0) iw = 0;
    cb->pub.w = lay_border_from_inner_w(cb, iw);

    if (as == AL_STRETCH && lay_is_auto(cb->cs->height)) {
      ih = ah - it[i].mt - it[i].mb;
      if (ih < 0) ih = 0;
      cb->pub.h = lay_border_from_inner_h(cb, ih);
      lay_layout_independent(d, cb, 0, 0, aw, 3, ah, 1);
    } else {
      lay_layout_independent(d, cb, 0, 0, aw, 2, ah, 1);
    }

    if (js == AL_CENTER)        dx = (aw - cb->pub.w - it[i].ml - it[i].mr) / 2.0;
    else if (js == AL_FLEX_END) dx =  aw - cb->pub.w - it[i].ml - it[i].mr;
    if (as == AL_CENTER)        dy = (ah - cb->pub.h - it[i].mt - it[i].mb) / 2.0;
    else if (as == AL_FLEX_END) dy =  ah - cb->pub.h - it[i].mt - it[i].mb;
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;

    tx = b->pub.x + b->bl + b->pl + coff[it[i].c0] + it[i].ml + dx;
    ty = b->pub.y + b->bt + b->pt + roff[it[i].r0] + it[i].mt + dy;
    lay_shift_subtree(cb, tx - cb->pub.x, ty - cb->pub.y);
  }

  b->pub.content_h = have_h ? inner_h : total_h;
}
