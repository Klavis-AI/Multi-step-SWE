/* lay_inline.c - inline formatting contexts: CSS 2.1 §9.4.2 (inline formatting),
   §16.6 (white-space processing), §10.8 (line height and baseline alignment),
   §16.2 (text-align).  All font metrics come from the lay_env callbacks. */
#include "lay_internal.h"
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------- white-space helpers */
static int ws_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}
static int ws_collapses(whitespace_t w) {
    return w == WS_NORMAL || w == WS_NOWRAP || w == WS_PRE_LINE;
}
static int ws_wraps(whitespace_t w) {
    return w == WS_NORMAL || w == WS_PRE_WRAP || w == WS_PRE_LINE;
}
static int ws_keeps_newline(whitespace_t w) {
    return w == WS_PRE || w == WS_PRE_WRAP || w == WS_PRE_LINE;
}
/* U+200B ZERO WIDTH SPACE, a soft wrap opportunity with no advance. */
static int is_zwsp(const char *p) {
    return (unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x80 &&
           (unsigned char)p[2] == 0x8B;
}

/* ------------------------------------------------------------- item stream */
enum { IT_TEXT = 0, IT_SPACE, IT_ATOMIC, IT_OPEN, IT_CLOSE, IT_FLOAT, IT_BR,
       IT_ABS };      /* an out-of-flow box sitting in the inline content */

typedef struct {
    int kind;
    box *b;                 /* owning box */
    const char *s;
    int len;
    double w;               /* advance */
    int brk;                /* a soft wrap opportunity exists before this item */
    double va_above;        /* baseline -> top of this fragment's own box */
    double va_half;         /* its half-leading, kept for the placement pass */
} iitem;

typedef struct {
    iitem *v;
    int n, cap;
    lay_doc *d;
} ilist;

static iitem *push(ilist *L, int kind, box *b) {
    iitem *it;
    if (L->n == L->cap) {
        int nc = L->cap ? L->cap * 2 : 32;
        iitem *nv = (iitem *)lay_alloc(&L->d->a, (size_t)nc * sizeof(iitem));
        if (L->n) memcpy(nv, L->v, (size_t)L->n * sizeof(iitem));
        L->v = nv; L->cap = nc;
    }
    it = &L->v[L->n++];
    memset(it, 0, sizeof(*it));
    it->kind = kind;
    it->b = b;
    return it;
}

/* Split a text run into words, collapsible space runs, forced breaks and
   zero-width break opportunities (§16.6.1 white-space processing model). */
static void segment_text(lay_doc *d, box *t, ilist *L, int *pending_space) {
    const char *s = t->text;
    whitespace_t ws = t->cs->white_space;
    int i = 0, n = (int)strlen(s ? s : "");
    while (i < n) {
        if (ws_is_space(s[i])) {
            int j = i;
            int had_nl = 0;
            while (j < n && ws_is_space(s[j])) {
                if (s[j] == '\n') had_nl = 1;
                if (!ws_collapses(ws)) break;   /* preserved: one char at a time */
                j++;
            }
            if (!ws_collapses(ws)) {
                if (s[i] == '\n' && ws_keeps_newline(ws)) {
                    push(L, IT_BR, t);
                } else {
                    iitem *it = push(L, IT_SPACE, t);
                    it->s = s + i; it->len = 1;
                    it->w = lay_text_w(d, s + i, 1, t->cs);
                    it->brk = ws_wraps(ws);
                }
                i++;
                continue;
            }
            if (had_nl && ws == WS_PRE_LINE) {
                push(L, IT_BR, t);
            } else {
                iitem *it = push(L, IT_SPACE, t);
                it->s = " "; it->len = 1;
                it->w = lay_text_w(d, " ", 1, t->cs);
                it->brk = ws_wraps(ws);
            }
            (void)pending_space;
            i = j;
            continue;
        }
        if (is_zwsp(s + i)) {
            iitem *it = push(L, IT_TEXT, t);
            it->s = s + i; it->len = 3; it->w = 0; it->brk = ws_wraps(ws);
            i += 3;
            continue;
        }
        {
            int j = i;
            iitem *it;
            while (j < n && !ws_is_space(s[j]) && !is_zwsp(s + j)) j++;
            it = push(L, IT_TEXT, t);
            it->s = s + i; it->len = j - i;
            it->w = lay_text_w(d, s + i, j - i, t->cs);
            it->brk = 0;
            i = j;
        }
    }
}

void lay_text_metrics(lay_doc *d, box *t, double *minw, double *maxw) {
    ilist L;
    int i;
    double line = 0, best = 0, word = 0, wbest = 0, pend = 0;
    memset(&L, 0, sizeof L);
    L.d = d;
    {
        int ps = 0;
        segment_text(d, t, &L, &ps);
    }
    for (i = 0; i < L.n; i++) {
        iitem *it = &L.v[i];
        if (it->kind == IT_BR) {
            if (line > best) best = line;
            line = 0; pend = 0;
            if (word > wbest) wbest = word;
            word = 0;
            continue;
        }
        if (it->kind == IT_SPACE) {
            /* collapsible white space at the edges of the content does not
               contribute to max-content (§16.6.1 removes it from the line) */
            if (line > 0 || !ws_collapses(it->b->cs->white_space)) pend += it->w;
            if (word > wbest) wbest = word;
            word = 0;
            continue;
        }
        line += pend + it->w;
        pend = 0;
        if (it->brk) { if (word > wbest) wbest = word; word = 0; }
        word += it->w;
    }
    if (line > best) best = line;
    if (word > wbest) wbest = word;
    /* Each of these is one run as Blink would shape it - the whole content for
       max-content, the widest unbreakable piece for min-content - so each is
       rounded up to the grid exactly once. */
    *maxw = lay_ceil64(best);
    *minw = lay_ceil64(wbest);
}

/* Walk the inline-level subtree of `cb` producing the item stream. */
static void collect(lay_doc *d, box *cb, box *b, ilist *L, double avail) {
    lay_box *c;
    for (c = b->pub.first_child; c; c = c->next) {
        box *x = (box *)c;
        if (x->cs->display == D_NONE) continue;
        if (x->is_abs) {
            /* §9.4.2: an absolutely positioned box among inline content is
               out of flow - it contributes nothing to any line box - but it
               still has to be LAID OUT, against its own containing block, and
               to take a static position from where it sits in the stream.
               Keep it in the item list; lay_inline_content() queues it once
               the line it belongs to has a position. */
            push(L, IT_ABS, x);
            continue;
        }
        if (x->is_float) { push(L, IT_FLOAT, x); continue; }
        if (x->kind == BK_TEXT) {
            int ps = 0;
            x->pub.w = 0; x->pub.h = 0;      /* fragments are re-accumulated */
            segment_text(d, x, L, &ps);
            continue;
        }
        if (x->kind == BK_INLINE) {
            lay_edges(x, avail, 1);
            x->has_frag = 0;
            push(L, IT_OPEN, x);
            collect(d, cb, x, L, avail);
            push(L, IT_CLOSE, x);
            continue;
        }
        /* atomic inline-level box: inline-block, inline-table, replaced */
        {
            iitem *it;
            lay_edges(x, avail, 1);
            lay_layout_independent(d, x, 0, 0, avail, 1, 0, 0);
            it = push(L, IT_ATOMIC, x);
            it->w = x->ml + x->pub.w + x->mr;
            it->brk = 1;
        }
    }
}

/* A collapsible white-space sequence collapses to a single space across the
   whole inline formatting context (§16.6.1), not within one text node:
   "XX </span><span> XX" has ONE space between the two words.  Segmentation is
   per text node, so the join is made here.  Inline box boundaries and
   out-of-flow boxes are transparent to it. */
/* Blink shapes and measures a RUN - one text node's worth of items, as far as
   the line takes it - and rounds THAT up to the 1/64px grid, so the grid step
   is paid once per run and not once per word: "XXXXX XXXXX" in Ahem at 16px is
   176 + 1/64 px wide, not 176 + 3/64.  Item widths arrive exact; rewrite each
   to the difference between the rounded prefixes, so every prefix of a run
   still sums to the rounded width of that prefix and a line that breaks inside
   a run is measured the same way Blink measures it. */
static void snap_runs(ilist *L) {
    int i = 0;
    while (i < L->n) {
        box *b = L->v[i].b;
        double acc = 0, prev = 0;
        int j = i;
        if (L->v[i].kind != IT_TEXT && L->v[i].kind != IT_SPACE) { i++; continue; }
        while (j < L->n && L->v[j].b == b &&
               (L->v[j].kind == IT_TEXT || L->v[j].kind == IT_SPACE)) {
            double snapped;
            acc += L->v[j].w;
            snapped = lay_ceil64(acc);
            L->v[j].w = snapped - prev;
            prev = snapped;
            j++;
        }
        i = j;
    }
}

static void collapse_spaces(ilist *L) {
    int i, k = 0, prev_space = 0;
    for (i = 0; i < L->n; i++) {
        iitem *it = &L->v[i];
        if (it->kind == IT_SPACE && ws_collapses(it->b->cs->white_space)) {
            if (prev_space) continue;          /* already collapsed into one */
            prev_space = 1;
        } else if (it->kind != IT_OPEN && it->kind != IT_CLOSE &&
                   it->kind != IT_ABS) {
            prev_space = 0;
        }
        L->v[k++] = *it;
    }
    L->n = k;
}

/* --------------------------------------------------------- line metrics -- */
typedef struct { double above, below; } vmet;

/* floor(), spelled out: style.h declares a `float_t` of its own, which collides
   with the C99 typedef <math.h> declares under that name, so this translation
   unit cannot include <math.h>. */
static double dfloor(double v) {
    double f = (double)(long long)v;         /* truncates toward zero */
    return (f > v) ? f - 1.0 : f;
}

/* `half` is the leading that goes ABOVE the baseline and `lh` the used
   line-height, so the box below the baseline is lh - (A + half) and not
   D + half. */
static void font_metrics(lay_doc *d, const computed *st, double *A, double *D,
                         double *half, double *lh) {
    double used;
    *A = d->env->font_ascent(st);
    *D = d->env->font_descent(st);
    /* 10.8.1: with 'normal' the used line-height is the font's own line
       spacing - ascent + descent + line gap - not just the content height. */
    used = st->line_height < 0 ? (*A + *D + d->env->font_line_gap(st))
                               : lay_snap64(st->line_height);
    /* 10.8.1 splits the leading in half above and below the content area but
       does not say the halves are whole pixels; they are not equal here.  The
       half above is FLOORED to a whole pixel and the descent takes whatever is
       left of the line box, so an ODD leading puts the baseline half a pixel
       lower than an exact split would.  It shows up wherever line-height and
       content height differ by an odd number of pixels - a 30px face on a 10px
       line box, or a 16px face with a 17px content area on an 18px 'normal'
       line box - and it is what the reference recordings show. */
    *half = dfloor((used - (*A + *D)) / 2.0);
    *lh   = used;
}

/* Distance from the top of an atomic inline's margin box to its baseline. */
static double atomic_baseline(const box *x) {
    if (x->has_baseline && x->cs->overflow_x == OV_VISIBLE &&
        x->cs->overflow_y == OV_VISIBLE && !x->is_repl)
        return x->mt + x->baseline;
    return x->mt + x->pub.h;            /* §10.8.1: bottom margin edge */
}

/* One fragment of an inline box: it opens at a line's start edge (or at the
   box's own start) and closes at the line's end edge (or at the box's own
   end).  The published box is the UNION of them (§9.4.2). */
static void frag_begin(box *b, double x, double top, double A, double D) {
    b->fx0 = x;
    b->fy0 = top - b->pt - b->bt;
    b->fy1 = b->fy0 + A + D + b->pt + b->pb + b->bt + b->bb;
    b->pub.content_h = A + D;
}

static void frag_end(box *b, double x1) {
    if (!b->has_frag) {
        b->pub.x = b->fx0;      b->pub.y = b->fy0;
        b->pub.w = x1 - b->fx0; b->pub.h = b->fy1 - b->fy0;
        b->has_frag = 1;
    } else {
        double x0 = lay_dmin(b->pub.x, b->fx0);
        double y0 = lay_dmin(b->pub.y, b->fy0);
        double xr = lay_dmax(b->pub.x + b->pub.w, x1);
        double yb = lay_dmax(b->pub.y + b->pub.h, b->fy1);
        b->pub.x = x0; b->pub.y = y0;
        b->pub.w = xr - x0; b->pub.h = yb - y0;
    }
    b->pub.content_w = b->pub.w - b->bl - b->pl - b->pr - b->br;
    if (b->pub.content_w < 0) b->pub.content_w = 0;
}

/* Top of this fragment's content area.  'top' and 'bottom' are measured from
   the line box's own edges; every other alignment is a fixed offset from the
   line's baseline (§10.8.1). */
static double frag_top(const iitem *it, double y, double base_y,
                       double above, double below, double lh) {
    switch (it->b->cs->vertical_align) {
        case VA_TOP:    return y + it->va_half;
        /* the box's bottom is the line's bottom; its content area starts
           lh - va_half - (A + D) above that, i.e. va_half below its own top. */
        case VA_BOTTOM: return y + above + below - lh + it->va_half;
        default:        return base_y + it->va_half - it->va_above;
    }
}

/* ------------------------------------------------------------ line layout */
double lay_inline_content(bfs *s, double top, double *out_baseline,
                          int *out_has_baseline) {
    lay_doc *d = s->d;
    box *cb = s->owner;
    ilist L;
    int i = 0;
    double y = top;
    double sA, sD, shalf, slh;
    int any_line = 0;
    int first_line = 1;
    double last_baseline = 0;
    box *ostack[64];            /* inline boxes still open across a line break */
    int nost = 0;

    memset(&L, 0, sizeof L);
    L.d = d;
    collect(d, cb, cb, &L, s->cw);
    collapse_spaces(&L);
    snap_runs(&L);
    font_metrics(d, cb->fs ? cb->fs : cb->cs, &sA, &sD, &shalf, &slh);

    while (i < L.n) {
        int start, end, j;
        double l, r, avail;
        int last_brk;
        double width = 0, pend_space = 0;
        int have_content = 0;
        double above = sA + shalf, below = slh - (sA + shalf);
        double base_y, x;
        int forced = 0;

        /* Nothing in this group puts a fragment on the line, and all three
           kinds interleave freely, so they are ONE loop rather than three
           passes.  A float is placed as soon as it is reached (§9.5); leading
           collapsible white space is removed (§16.6.1) and generates no line
           box (§9.4.2), so an out-of-flow box among it still takes its static
           position here.  Split into separate passes, a float that merely
           FOLLOWS white space would stop being a float that begins the line
           and would never be laid out at all. */
        for (;;) {
            if (i < L.n && L.v[i].kind == IT_FLOAT) {
                s->flow = y;
                ms_clear(&s->st);
                lay_place_float(s, L.v[i].b);
                i++;
                continue;
            }
            if (i < L.n && L.v[i].kind == IT_ABS) {
                lay_queue_abs_at(s, L.v[i].b, s->cx, y);
                i++;
                continue;
            }
            if (i < L.n && L.v[i].kind == IT_SPACE &&
                ws_collapses(L.v[i].b->cs->white_space)) { i++; continue; }
            break;
        }
        if (i >= L.n) break;

        fc_band(s->f, y, y + (above + below > 0 ? above + below : 1.0), &l, &r);
        if (l < s->cx) l = s->cx;
        if (r > s->cx + s->cw) r = s->cx + s->cw;
        /* 16.1: text-indent moves the start edge of the FIRST line box only. */
        if (first_line) l += lay_res0(cb->cs->text_indent, s->cw, 1);
        avail = r - l;
        if (avail < 0) avail = 0;

        start = i;
        end = i;
        last_brk = -1;
        for (j = i; j < L.n; j++) {
            iitem *it = &L.v[j];
            double adv;
            if (it->kind == IT_BR) { end = j; forced = 1; break; }
            if (it->kind == IT_FLOAT) { end = j; break; }
            /* Out of flow: no advance, no break opportunity, and it never
               makes the line non-empty. */
            if (it->kind == IT_ABS) { end = j + 1; continue; }
            if (it->kind == IT_OPEN)  { width += it->b->ml + it->b->bl + it->b->pl; end = j + 1; continue; }
            if (it->kind == IT_CLOSE) { width += it->b->pr + it->b->br + it->b->mr; end = j + 1; continue; }
            if (it->kind == IT_SPACE) {
                if (!have_content && ws_collapses(it->b->cs->white_space)) { end = j + 1; continue; }
                pend_space += it->w;
                end = j + 1;
                if (ws_wraps(it->b->cs->white_space)) last_brk = j + 1;
                continue;
            }
            /* U+200B, and the edges of atomic inlines, carry a break
               opportunity at their own position (§16.6 soft wrap points). */
            if (it->brk && have_content && ws_wraps(it->b->cs->white_space))
                last_brk = j;
            adv = pend_space + it->w;
            if (have_content && width + adv > avail + 1e-6 && last_brk > start) {
                end = last_brk;
                break;
            }
            width += adv;
            pend_space = 0;
            have_content = 1;
            end = j + 1;
        }
        if (end <= start) end = start + 1;

        /* Trailing collapsible spaces do not affect the line width (§16.6.1). */
        while (end > start && L.v[end - 1].kind == IT_SPACE &&
               ws_collapses(L.v[end - 1].b->cs->white_space)) end--;

        /* vertical metrics */
        for (j = start; j < end; j++) {
            iitem *it = &L.v[j];
            double A, D, half = 0, lh = 0, a, bl;
            const computed *st;
            int edge_aligned = 0;
            if (it->kind == IT_SPACE || it->kind == IT_TEXT ||
                it->kind == IT_OPEN) {
                st = it->b->cs;
                font_metrics(d, st, &A, &D, &half, &lh);
                a = A + half; bl = lh - a;
            } else if (it->kind == IT_ATOMIC) {
                double ab = atomic_baseline(it->b);
                double mh = it->b->mt + it->b->pub.h + it->b->mb;
                st = it->b->cs;
                a = ab; bl = mh - ab;
            } else continue;

            switch (st->vertical_align) {
                case VA_BASELINE: break;
                case VA_SUB:   a -= st->font_size / 5.0;  bl += st->font_size / 5.0; break;
                case VA_SUPER: a += st->font_size / 3.0;  bl -= st->font_size / 3.0; break;
                case VA_LENGTH: a += st->vertical_align_px; bl -= st->vertical_align_px; break;
                case VA_MIDDLE: {
                    /* §10.8.1: midpoint aligned with the baseline plus half the
                       parent's x-height; x-height is taken as 0.5em. */
                    double xh = (cb->fs ? cb->fs->font_size : st->font_size) * 0.5;
                    double h = a + bl;
                    a = xh / 2.0 + h / 2.0;
                    bl = h - a;
                    break;
                }
                case VA_TEXT_TOP: {
                    double h = a + bl;
                    a = sA; bl = h - a;
                    break;
                }
                case VA_TEXT_BOTTOM: {
                    double h = a + bl;
                    bl = sD; a = h - bl;
                    break;
                }
                case VA_TOP: case VA_BOTTOM:
                    edge_aligned = 1;   /* placed once the line box is closed */
                    break;
            }
            it->va_above = a;
            it->va_half = half;
            if (edge_aligned) continue;
            if (a > above) above = a;
            if (bl > below) below = bl;
        }
        /* 'top'/'bottom' aligned boxes may still stretch the line box. */
        for (j = start; j < end; j++) {
            iitem *it = &L.v[j];
            double h = 0;
            if (it->kind == IT_ATOMIC) h = it->b->mt + it->b->pub.h + it->b->mb;
            else if (it->kind == IT_OPEN || it->kind == IT_TEXT || it->kind == IT_SPACE) {
                double A, D, half, lh;
                font_metrics(d, it->b->cs, &A, &D, &half, &lh);
                h = lh;
            } else continue;
            if (it->b->cs->vertical_align == VA_TOP) {
                if (h > above + below) below = h - above;
            } else if (it->b->cs->vertical_align == VA_BOTTOM) {
                if (h > above + below) above = h - below;
            }
        }

        base_y = y + above;

        /* horizontal alignment (§16.2) */
        {
            double used = 0;
            for (j = start; j < end; j++) {
                iitem *it = &L.v[j];
                switch (it->kind) {
                    case IT_OPEN:  used += it->b->ml + it->b->bl + it->b->pl; break;
                    case IT_CLOSE: used += it->b->pr + it->b->br + it->b->mr; break;
                    case IT_TEXT: case IT_SPACE: case IT_ATOMIC: used += it->w; break;
                    default: break;
                }
            }
            {
                textalign_t ta = cb->cs->text_align;
                double free_sp = avail - used;
                if (free_sp < 0) free_sp = 0;
                if (ta == TA_LEFT && cb->cs->direction == DIR_RTL) ta = TA_RIGHT;
                switch (ta) {
                    case TA_RIGHT:  x = l + free_sp; break;
                    case TA_CENTER: x = l + free_sp / 2.0; break;
                    default:        x = l; break;
                }
            }
        }

        /* An inline box that was still open when the previous line ended
           resumes here, at this line's start edge, as a new fragment. */
        {
            int k;
            for (k = 0; k < nost; k++) {
                double A, D, half, lh;
                font_metrics(d, ostack[k]->cs, &A, &D, &half, &lh);
                frag_begin(ostack[k], x, base_y - A, A, D);
            }
        }

        /* place the fragments */
        for (j = start; j < end; j++) {
            iitem *it = &L.v[j];
            box *b = it->b;
            double A, D, half, lh;
            switch (it->kind) {
                case IT_OPEN:
                    x += b->ml;
                    font_metrics(d, b->cs, &A, &D, &half, &lh);
                    frag_begin(b, x, frag_top(it, y, base_y, above, below, lh),
                               A, D);
                    x += b->bl + b->pl;
                    if (nost < (int)(sizeof ostack / sizeof ostack[0]))
                        ostack[nost++] = b;
                    break;
                case IT_CLOSE:
                    x += b->pr + b->br;
                    frag_end(b, x);
                    x += b->mr;
                    if (nost > 0 && ostack[nost - 1] == b) nost--;
                    break;
                case IT_SPACE:
                case IT_TEXT:
                    font_metrics(d, b->cs, &A, &D, &half, &lh);
                    if (b->pub.w == 0 && b->pub.h == 0) {
                        b->pub.x = x;
                        b->pub.y = frag_top(it, y, base_y, above, below, lh);
                        b->pub.h = A + D;
                        b->pub.content_h = A + D;
                    }
                    b->pub.w += it->w;
                    b->pub.content_w = b->pub.w;
                    x += it->w;
                    break;
                case IT_ATOMIC: {
                    double ab = atomic_baseline(b);
                    double dy;
                    x += b->ml;
                    if (b->cs->vertical_align == VA_TOP) dy = y + b->mt;
                    else if (b->cs->vertical_align == VA_BOTTOM)
                        dy = y + above + below - b->pub.h - b->mb;
                    else dy = base_y - ab + b->mt;
                    lay_shift_subtree(b, x - b->pub.x, dy - b->pub.y);
                    x += b->pub.w + b->mr;
                    break;
                }
                case IT_ABS:
                    /* §10.1.4.3: the static position is where the hypothetical
                       box would have started on this line. */
                    lay_queue_abs_at(s, b, x, y);
                    break;
                default: break;
            }
        }

        {   /* the line ends: every box still open ends a fragment here */
            int k;
            for (k = nost - 1; k >= 0; k--) frag_end(ostack[k], x);
        }

        /* absolutely positioned descendants take their static position from the
           line they would have appeared on (§10.1.4.3). */
        y = y + above + below;
        last_baseline = base_y;
        any_line = 1;
        first_line = 0;
        i = end;
        if (forced && i < L.n && L.v[i].kind == IT_BR) i++;
        while (i < L.n && L.v[i].kind == IT_SPACE &&
               ws_collapses(L.v[i].b->cs->white_space)) i++;
    }

    if (out_baseline) *out_baseline = last_baseline;
    if (out_has_baseline) *out_has_baseline = any_line;
    return any_line ? y : top;
}

double lay_inline_intrinsic(lay_doc *d, box *cb, int minimum, double avail) {
    (void)d; (void)cb; (void)minimum; (void)avail;
    return 0;
}
