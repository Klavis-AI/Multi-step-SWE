/* Internal shared declarations for the CSS 2.1 visual formatting model.
   Not part of the published interface (style.h / layout.h). */
#ifndef LAY_INTERNAL_H
#define LAY_INTERNAL_H
#include "layout.h"
#include <stddef.h>

/* The reference fixtures pin ::-webkit-scrollbar to 15px on both axes; CSS 2.1
   §11.1.1 leaves the thickness UA-defined. */
#define LAY_SB 15.0

/* ------------------------------------------------------------------ arena */
typedef struct ar_chunk ar_chunk;
typedef struct { ar_chunk *head; } arena;
void *lay_alloc(arena *a, size_t n);
void  lay_arena_release(arena *a);

double lay_dmax(double a, double b);
double lay_dmin(double a, double b);

/* ------------------------------------------------------------------ boxes */
typedef enum {
  BK_BLOCK = 0,   /* block container (block-level, inline-block, cell, ...) */
  BK_INLINE,      /* non-replaced inline box */
  BK_TEXT,        /* text run */
  BK_TABLE, BK_TROWGROUP, BK_TROW, BK_TCELL, BK_TCOLGROUP, BK_TCOL, BK_TCAPTION,
  BK_FLEX         /* css-flexbox-1 §3 flex container */
} box_kind;

typedef struct box box;
struct box {
  lay_box pub;                /* MUST be first member: the published view */
  box_kind kind;
  const computed *cs;         /* style driving layout (synthesised for anon boxes) */
  const computed *fs;         /* style supplying font metrics */
  const char *text;           /* BK_TEXT only */
  double mt, mr, mb, ml;      /* used margins */
  double pt, pr, pb, pl;      /* used padding */
  double bt, br, bb, bl;      /* used border widths */
  double sbw, sbh;            /* scrollbar space reserved inside the padding box */
  double baseline;            /* border-box top -> baseline of the last line box */
  double static_x, static_y;  /* static position rel. to the parent's content box */
  double fx0, fy0, fy1;       /* the inline fragment currently being placed */
  double minc, maxc;          /* cached intrinsic border-box widths */
  unsigned intr_done : 1;
  unsigned in_flow : 1, is_float : 1, is_abs : 1, is_bfc : 1, is_anon : 1;
  unsigned is_repl : 1, block_level : 1, has_clearance : 1, has_baseline : 1;
  unsigned static_rtl : 1, h_definite : 1, abs_queued : 1, has_frag : 1;
  box *next_abs;
  int col_span, row_span, col_index, row_index;
};

typedef struct { void *k; box *v; } lay_slot;

struct lay_doc {
  arena a;
  const lay_env *env;
  box *root;
  double vw, vh;
  lay_slot *tab;
  int tabcap, tabn;
  box *abs_head, *abs_tail;
};

/* ------------------------------------------------- collapsing margin strut */
typedef struct { double pos, neg; } mstrut;
void   ms_clear(mstrut *m);
void   ms_add(mstrut *m, double v);
void   ms_join(mstrut *m, const mstrut *o);
double ms_val(const mstrut *m);

/* ----------------------------------------- float exclusions within one BFC */
typedef struct { double x0, x1, top, bot; int right; } lay_excl;
typedef struct {
  arena *a;
  lay_excl *ex;
  int n, cap;
  double cl, cr;              /* BFC root content-box left/right edges (absolute) */
} fctx;
void   fc_init(fctx *f, arena *a, double cl, double cr);
void   fc_add(fctx *f, double x0, double x1, double top, double bot, int right);
void   fc_band(const fctx *f, double top, double bot, double *l, double *r);
double fc_next_below(const fctx *f, double y);
double fc_clear_y(const fctx *f, clear_t c);
double fc_lowest(const fctx *f);
double fc_highest_top(const fctx *f);

/* ------------------------------------------------ per-container flow state */
typedef struct bfs bfs;
struct bfs {
  bfs      *parent;           /* enclosing container in the SAME formatting context */
  box      *owner;
  fctx     *f;
  lay_doc  *d;
  double    cx, cw;           /* content-box left edge / width (absolute) */
  double    cy;               /* content-box top edge (absolute); valid iff resolved */
  double    flow;             /* absolute y the pending strut is measured from */
  double    cch;              /* content height of the container, for % children */
  int       cch_def;
  mstrut    st;               /* pending collapsing margins */
  int       resolved;
  int       sealed_inside;
  box     **pend;
  int       npend, cappend;
};

void   bfs_seal(bfs *s, double y);
void   bfs_add_pending(bfs *s, box *b);

/* ------------------------------------------------------------ length helpers */
int    lay_is_auto(length l);
int    lay_res(length l, double base, int base_def, double *out);
double lay_res0(length l, double base, int base_def);
double lay_snap64(double v);
double lay_ceil64(double v);

/* ------------------------------------------------------------ shared layout */
void   lay_edges(box *b, double cbw, int cbw_def);
void   lay_scrollbars(box *b);
double lay_clampw(const box *b, double w, double cbw, int cbw_def);
double lay_clamph(const box *b, double h, double cbh, int cbh_def);
/* css-flexbox-1 §9, in lay_flex.c */
void   lay_flex_content(lay_doc *d, box *b, double inner_w, int have_h,
                        double inner_h);
double lay_spec_inner_w(const box *b, double v);
double lay_spec_inner_h(const box *b, double v);
double lay_intrinsic(lay_doc *d, box *b, int minimum, double avail);
double lay_inner_from_border_w(const box *b, double bw);
double lay_border_from_inner_w(const box *b, double iw);
double lay_border_from_inner_h(const box *b, double ih);
double lay_inner_from_border_h(const box *b, double bh);

/* mode: 0 = block-level auto width fills the available space,
         1 = shrink-to-fit (§10.3.5),
         2 = border-box width already stored in pub.w / pub.content_w,
         3 = width and height both already stored. */
void   lay_layout_independent(lay_doc *d, box *b, double x, double y,
                              double avail_w, int mode, double cbh, int cbh_def);
void   lay_layout_inflow_child(bfs *s, box *c);
void   lay_place_float(bfs *s, box *fl);
void   lay_run_abs_queue(lay_doc *d);
void   lay_enqueue_abs(lay_doc *d, box *b);
void   lay_queue_abs_at(bfs *s, box *b, double abs_x, double abs_y);
void   lay_publish_edges(box *b);
void   lay_finish_scrollable(box *b);
void   lay_shift_subtree(box *b, double dx, double dy);
void   lay_set_subtree_y(box *b, double y);

/* inline formatting (lay_inline.c) */
double lay_inline_content(bfs *s, double top, double *out_last_baseline,
                          int *out_has_baseline);
void   lay_text_metrics(lay_doc *d, box *t, double *minw, double *maxw);

/* tables (lay_table.c) */
void   lay_table_contents(lay_doc *d, box *t);
double lay_table_intrinsic(lay_doc *d, box *t, int minimum);

/* misc helpers */
double lay_text_w(lay_doc *d, const char *s, int len, const computed *st);
int    lay_is_inline_level(const box *b);
box   *lay_new_box(lay_doc *d, box_kind k);
void   lay_append(box *parent, box *child);
#endif
