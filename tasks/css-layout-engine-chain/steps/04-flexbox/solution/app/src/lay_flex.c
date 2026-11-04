/* css-flexbox-1 §9: the flex layout algorithm.
   A flex container establishes an independent formatting context, so this
   file is entered from lay_layout_independent() in place of run_contents().
   Items are laid out by calling back into lay_layout_independent() with the
   main size already decided (mode 2), or both sizes decided (mode 3). */
#include "lay_internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  box *b;
  double base;          /* §9.2 flex base size, inner, main axis */
  double hypo;          /* §9.2 hypothetical main size, clamped */
  double target;        /* §9.7 resolved inner main size */
  double mm_start, mm_end;   /* main-axis margins */
  double cm_start, cm_end;   /* cross-axis margins */
  double main_edges, cross_edges;  /* border+padding on each axis */
  double grow, shrink, scaled;
  double cross;         /* inner cross size */
  int frozen, min_violation, max_violation;
  int auto_mm_start, auto_mm_end;
} fitem;

static int is_row(const computed *c) {
  return c->flex_direction == FD_ROW || c->flex_direction == FD_ROW_REVERSE;
}
static int is_reverse(const computed *c) {
  return c->flex_direction == FD_ROW_REVERSE ||
         c->flex_direction == FD_COLUMN_REVERSE;
}

/* §4.5 automatic minimum size: on a flex item whose main-axis min-* is auto,
   the used minimum is the content-based minimum size, clamped by a definite
   main size.  This is the behaviour that changed between drafts and is the
   reason `min-width: auto` fixtures flip. */
static double auto_min_main(lay_doc *d, fitem *it, int row, double avail) {
  double cbm;
  if (!row) return 0.0;   /* column: measured separately */
  cbm = lay_intrinsic(d, it->b, 1, avail);
  return lay_dmax(0.0, cbm - it->main_edges);
}

static void item_margins(fitem *it, int row) {
  box *b = it->b;
  if (row) {
    it->mm_start = b->ml; it->mm_end = b->mr;
    it->cm_start = b->mt; it->cm_end = b->mb;
    it->main_edges  = b->bl + b->br + b->pl + b->pr;
    it->cross_edges = b->bt + b->bb + b->pt + b->pb;
    it->auto_mm_start = lay_is_auto(b->cs->margin[EDGE_LEFT]);
    it->auto_mm_end   = lay_is_auto(b->cs->margin[EDGE_RIGHT]);
  } else {
    it->mm_start = b->mt; it->mm_end = b->mb;
    it->cm_start = b->ml; it->cm_end = b->mr;
    it->main_edges  = b->bt + b->bb + b->pt + b->pb;
    it->cross_edges = b->bl + b->br + b->pl + b->pr;
    it->auto_mm_start = lay_is_auto(b->cs->margin[EDGE_TOP]);
    it->auto_mm_end   = lay_is_auto(b->cs->margin[EDGE_BOTTOM]);
  }
  if (it->auto_mm_start) it->mm_start = 0;
  if (it->auto_mm_end)   it->mm_end = 0;
}

/* §9.2 step 3: the flex base size. */
static void base_size(lay_doc *d, fitem *it, int row, double main_avail,
                      int main_def) {
  const computed *st = it->b->cs;
  length fb = st->flex_basis;
  double v;

  if (fb.kind != LEN_AUTO) {
    if (lay_res(fb, main_avail, main_def, &v)) { it->base = v; return; }
    it->base = 0;
    return;
  }
  /* flex-basis: auto -> use the main size property, else content */
  if (lay_res(row ? st->width : st->height, main_avail, main_def, &v)) {
    it->base = row ? lay_spec_inner_w(it->b, v) : lay_spec_inner_h(it->b, v);
    return;
  }
  if (row) {
    double mx = lay_intrinsic(d, it->b, 0, main_avail);
    it->base = lay_dmax(0.0, mx - it->main_edges);
  } else {
    /* content height: lay the item out at its available width first */
    lay_layout_independent(d, it->b, 0, 0, main_avail, 0, 0, 0);
    it->base = lay_dmax(0.0, lay_inner_from_border_h(it->b, it->b->pub.h));
  }
}

static double clamp_main(fitem *it, int row, double v, double avail,
                         int avail_def, double automin) {
  double lo = automin, hi = -1, t;
  const computed *st = it->b->cs;
  if (lay_res(row ? st->min_width : st->min_height, avail, avail_def, &t))
    lo = row ? lay_spec_inner_w(it->b, t) : lay_spec_inner_h(it->b, t);
  if (lay_res(row ? st->max_width : st->max_height, avail, avail_def, &t))
    hi = row ? lay_spec_inner_w(it->b, t) : lay_spec_inner_h(it->b, t);
  if (hi >= 0 && v > hi) v = hi;
  if (v < lo) v = lo;
  return v;
}

/* §9.7 resolving flexible lengths, on one line. */
static void resolve_flex(fitem *it, int n, int row, double inner_main,
                         double avail, int avail_def, const double *automin) {
  double hypo_total = 0, free_space, initial_free;
  int i, grow_mode, loops = 0;

  for (i = 0; i < n; i++)
    hypo_total += it[i].hypo + it[i].mm_start + it[i].mm_end + it[i].main_edges;
  grow_mode = hypo_total < inner_main;

  for (i = 0; i < n; i++) {
    it[i].frozen = 0;
    it[i].target = it[i].hypo;
    /* §9.7.1: freeze items that cannot flex in the needed direction */
    if (grow_mode ? it[i].grow == 0 : it[i].shrink == 0) it[i].frozen = 1;
    else if (grow_mode && it[i].base > it[i].hypo) it[i].frozen = 1;
    else if (!grow_mode && it[i].base < it[i].hypo) it[i].frozen = 1;
  }
  initial_free = inner_main;
  for (i = 0; i < n; i++)
    initial_free -= it[i].mm_start + it[i].mm_end + it[i].main_edges;

  for (;;) {
    double used = 0, sum_grow = 0, sum_scaled = 0, total_violation = 0;
    if (++loops > 64) break;
    for (i = 0; i < n; i++) {
      used += it[i].frozen ? it[i].target : it[i].base;
      if (!it[i].frozen) {
        sum_grow += it[i].grow;
        it[i].scaled = it[i].shrink * it[i].base;
        sum_scaled += it[i].scaled;
      }
    }
    free_space = initial_free - used;
    if (sum_grow == 0 && sum_scaled == 0) break;

    for (i = 0; i < n; i++) {
      if (it[i].frozen) continue;
      if (grow_mode) {
        if (sum_grow > 0)
          it[i].target = it[i].base + free_space * (it[i].grow / sum_grow);
      } else {
        if (sum_scaled > 0)
          it[i].target = it[i].base + free_space * (it[i].scaled / sum_scaled);
      }
    }
    /* §9.7.4 clamp and count violations */
    for (i = 0; i < n; i++) {
      double before;
      if (it[i].frozen) continue;
      before = it[i].target;
      it[i].target = clamp_main(&it[i], row, it[i].target, avail, avail_def,
                                automin[i]);
      it[i].min_violation = it[i].target > before;
      it[i].max_violation = it[i].target < before;
      total_violation += it[i].target - before;
    }
    if (total_violation > 1e-9) {
      for (i = 0; i < n; i++) if (it[i].min_violation) it[i].frozen = 1;
    } else if (total_violation < -1e-9) {
      for (i = 0; i < n; i++) if (it[i].max_violation) it[i].frozen = 1;
    } else {
      for (i = 0; i < n; i++) it[i].frozen = 1;
    }
    for (i = 0; i < n; i++) if (!it[i].frozen) break;
    if (i == n) break;
  }
  for (i = 0; i < n; i++)
    if (it[i].target < 0) it[i].target = 0;
}

/* §8.2 / §8.4 distribution along an axis. */
static void distribute(align_t how, double free_space, int n,
                       double *offset, double *between) {
  *offset = 0; *between = 0;
  if (free_space < 0) free_space = 0;
  switch (how) {
    case AL_FLEX_END:      *offset = free_space; break;
    case AL_CENTER:        *offset = free_space / 2.0; break;
    case AL_SPACE_BETWEEN:
      if (n > 1) *between = free_space / (n - 1); else *offset = 0;
      break;
    case AL_SPACE_AROUND:
      if (n > 0) { *between = free_space / n; *offset = *between / 2.0; }
      break;
    case AL_SPACE_EVENLY:
      if (n > 0) { *between = free_space / (n + 1); *offset = *between; }
      break;
    default: break;                      /* flex-start / stretch / normal */
  }
}

static int cmp_order(const void *a, const void *b) {
  const fitem *x = (const fitem *)a, *y = (const fitem *)b;
  if (x->b->cs->order != y->b->cs->order)
    return x->b->cs->order < y->b->cs->order ? -1 : 1;
  return 0;                              /* qsort is not stable; see below */
}

void lay_flex_content(lay_doc *d, box *b, double inner_w, int have_h,
                      double inner_h) {
  const computed *cs = b->cs;
  int row = is_row(cs), rev = is_reverse(cs);
  double main_inner = row ? inner_w : (have_h ? inner_h : 0);
  int main_def = row ? 1 : have_h;
  double cross_inner = row ? (have_h ? inner_h : 0) : inner_w;
  int cross_def = row ? have_h : 1;
  lay_box *c;
  fitem *it;
  double *automin;
  int n = 0, i, li, lines;
  int *line_start, *line_count;
  double *line_cross;
  double content_cross = 0, main_pos, cross_pos;

  for (c = b->pub.first_child; c; c = c->next) {
    box *cb = (box *)c;
    if (cb->cs->display == D_NONE) continue;
    if (cb->is_abs) { lay_enqueue_abs(d, cb); continue; }
    n++;
  }
  if (n == 0) {
    b->pub.content_h = have_h ? inner_h : 0;
    return;
  }
  it      = (fitem *)lay_alloc(&d->a, (size_t)n * sizeof *it);
  automin = (double *)lay_alloc(&d->a, (size_t)n * sizeof *automin);
  memset(it, 0, (size_t)n * sizeof *it);

  i = 0;
  for (c = b->pub.first_child; c; c = c->next) {
    box *cb = (box *)c;
    if (cb->cs->display == D_NONE || cb->is_abs) continue;
    /* §4: a flex item is blockified and its float is ignored. */
    cb->is_float = 0u; cb->in_flow = 1u;
    it[i].b = cb;
    it[i].grow = cb->cs->flex_grow;
    it[i].shrink = cb->cs->flex_shrink;
    i++;
  }
  /* §5.4 order.  Insertion sort keeps equal orders in document order, which
     qsort() would not -- order is a reordering, not a shuffle. */
  for (i = 1; i < n; i++) {
    fitem tmp = it[i];
    int j = i - 1;
    while (j >= 0 && it[j].b->cs->order > tmp.b->cs->order) {
      it[j + 1] = it[j]; j--;
    }
    it[j + 1] = tmp;
  }
  (void)cmp_order;

  for (i = 0; i < n; i++) {
    lay_edges(it[i].b, row ? inner_w : cross_inner, 1);
    lay_scrollbars(it[i].b);
    item_margins(&it[i], row);
    base_size(d, &it[i], row, main_inner, main_def);
    automin[i] = 0.0;
    if (lay_is_auto(row ? it[i].b->cs->min_width : it[i].b->cs->min_height))
      automin[i] = auto_min_main(d, &it[i], row, main_inner);
    it[i].hypo = clamp_main(&it[i], row, it[i].base, main_inner, main_def,
                            automin[i]);
  }

  /* §9.3 line breaking */
  line_start = (int *)lay_alloc(&d->a, (size_t)(n + 1) * sizeof *line_start);
  line_count = (int *)lay_alloc(&d->a, (size_t)(n + 1) * sizeof *line_count);
  line_cross = (double *)lay_alloc(&d->a, (size_t)(n + 1) * sizeof *line_cross);
  lines = 0;
  if (cs->flex_wrap == FW_NOWRAP || !main_def) {
    line_start[0] = 0; line_count[0] = n; lines = 1;
  } else {
    double run = 0;
    int start = 0, cnt = 0;
    for (i = 0; i < n; i++) {
      double outer = it[i].hypo + it[i].mm_start + it[i].mm_end +
                     it[i].main_edges;
      if (cnt > 0 && run + outer > main_inner + 1e-9) {
        line_start[lines] = start; line_count[lines] = cnt; lines++;
        start = i; cnt = 0; run = 0;
      }
      run += outer; cnt++;
    }
    line_start[lines] = start; line_count[lines] = cnt; lines++;
  }

  /* §9.7 per line, then cross sizing */
  for (li = 0; li < lines; li++) {
    int s = line_start[li], cnt = line_count[li];
    double lc = 0;
    resolve_flex(it + s, cnt, row, main_def ? main_inner : 0, main_inner,
                 main_def, automin + s);
    for (i = s; i < s + cnt; i++) {
      box *cb = it[i].b;
      if (row) {
        cb->pub.w = lay_border_from_inner_w(cb, it[i].target);
        lay_layout_independent(d, cb, 0, 0, inner_w, 2,
                               cross_def ? cross_inner : 0, cross_def);
        it[i].cross = lay_inner_from_border_h(cb, cb->pub.h);
      } else {
        cb->pub.h = lay_border_from_inner_h(cb, it[i].target);
        lay_layout_independent(d, cb, 0, 0, cross_inner, 0,
                               main_def ? main_inner : 0, main_def);
        cb->pub.h = lay_border_from_inner_h(cb, it[i].target);
        it[i].cross = lay_inner_from_border_w(cb, cb->pub.w);
      }
      lc = lay_dmax(lc, it[i].cross + it[i].cm_start + it[i].cm_end +
                        it[i].cross_edges);
    }
    line_cross[li] = lc;
    content_cross += lc;
  }
  /* §9.4 step 8: a single-line container with a definite cross size uses it. */
  if (lines == 1 && cross_def) line_cross[0] = cross_inner;

  if (!cross_def) cross_inner = content_cross;

  /* §8.4 align-content across lines */
  {
    double off = 0, between = 0, total = 0;
    for (li = 0; li < lines; li++) total += line_cross[li];
    if (lines > 1)
      distribute(cs->align_content, cross_inner - total, lines, &off, &between);
    else if (cs->align_content == AL_STRETCH && cross_def && lines == 1)
      line_cross[0] = cross_inner;
    cross_pos = off;

    for (li = 0; li < lines; li++) {
      int s = line_start[li], cnt = line_count[li];
      double used = 0, free_space, moff = 0, mbetween = 0;
      int n_auto = 0;

      for (i = s; i < s + cnt; i++) {
        used += it[i].target + it[i].mm_start + it[i].mm_end + it[i].main_edges;
        n_auto += it[i].auto_mm_start + it[i].auto_mm_end;
      }
      free_space = (main_def ? main_inner : used) - used;
      if (free_space < 0) free_space = 0;
      if (n_auto > 0) {
        double share = free_space / n_auto;
        for (i = s; i < s + cnt; i++) {
          if (it[i].auto_mm_start) it[i].mm_start = share;
          if (it[i].auto_mm_end)   it[i].mm_end = share;
        }
        free_space = 0;
      }
      distribute(cs->justify_content, free_space, cnt, &moff, &mbetween);

      main_pos = moff;
      for (i = s; i < s + cnt; i++) {
        int k = rev ? (s + cnt - 1 - (i - s)) : i;
        box *cb = it[k].b;
        double outer_main = it[k].target + it[k].mm_start + it[k].mm_end +
                            it[k].main_edges;
        double outer_cross = it[k].cross + it[k].cm_start + it[k].cm_end +
                             it[k].cross_edges;
        align_t self = cb->cs->align_self == AL_AUTO ? cs->align_items
                                                     : cb->cs->align_self;
        double cfree = line_cross[li] - outer_cross;
        double cdelta = 0;

        if (cfree < 0) cfree = 0;
        switch (self) {
          case AL_FLEX_END: cdelta = cfree; break;
          case AL_CENTER:   cdelta = cfree / 2.0; break;
          case AL_STRETCH: {
            /* §8.3: stretch only applies when the cross size is auto. */
            int auto_cross = row ? lay_is_auto(cb->cs->height)
                                 : lay_is_auto(cb->cs->width);
            if (auto_cross) {
              double target_cross = line_cross[li] - it[k].cm_start -
                                    it[k].cm_end - it[k].cross_edges;
              if (target_cross < 0) target_cross = 0;
              if (row) {
                cb->pub.h = lay_border_from_inner_h(cb, target_cross);
                lay_layout_independent(d, cb, 0, 0, inner_w, 3,
                                       cross_def ? cross_inner : 0, cross_def);
              } else {
                cb->pub.w = lay_border_from_inner_w(cb, target_cross);
                lay_layout_independent(d, cb, 0, 0, target_cross, 2,
                                       main_def ? main_inner : 0, main_def);
                cb->pub.h = lay_border_from_inner_h(cb, it[k].target);
              }
              it[k].cross = target_cross;
            }
            break;
          }
          default: break;                /* flex-start, baseline (approximated) */
        }

        {
          double tx, ty;
          if (row) {
            tx = b->pub.x + b->bl + b->pl + main_pos + it[k].mm_start;
            ty = b->pub.y + b->bt + b->pt + cross_pos + cdelta + it[k].cm_start;
          } else {
            ty = b->pub.y + b->bt + b->pt + main_pos + it[k].mm_start;
            tx = b->pub.x + b->bl + b->pl + cross_pos + cdelta + it[k].cm_start;
          }
          /* the item was laid out at the origin; move the whole subtree */
          lay_shift_subtree(cb, tx - cb->pub.x, ty - cb->pub.y);
        }
        main_pos += outer_main + mbetween;
      }
      cross_pos += line_cross[li] + between;
    }
  }

  if (row) {
    b->pub.content_h = have_h ? inner_h : content_cross;
  } else {
    double used_main = 0;
    for (i = 0; i < n; i++)
      used_main = lay_dmax(used_main, 0.0);
    b->pub.content_h = have_h ? inner_h : 0;
    (void)used_main;
  }
}
