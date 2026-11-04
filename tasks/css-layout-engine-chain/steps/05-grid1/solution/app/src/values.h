/* values.h -- computed values and CSSOM resolved values.
 *
 * This module sits between the cascade and layout.  It turns the winning
 * declared value of every property on every element into the `computed`
 * record that style.h defines (stored in dom_node::style), and it serialises
 * the CSSOM "resolved value" that WebDriver's Get Element CSS Value returns.
 *
 * C11, libc only.  Everything allocated lives in the arena handed to
 * values_compute() and stays valid until arena_free().
 *
 * ---------------------------------------------------------------------------
 * Contract with the rest of the engine
 *
 *   * values_compute() is the only writer of dom_node::style.  It allocates a
 *     record whose FIRST member is the `computed` that dom_node::style points
 *     at, followed by private fields for the handful of things CSSOM has to
 *     serialise but `computed` has no slot for (border styles, a level-3
 *     display keyword such as `flex`, the line-height <number> factor, and a
 *     <percentage> vertical-align).  values_resolved() recovers those private
 *     fields from the pointer and validates them with a magic number.
 *
 *     Consequence: do not assign a bare `computed` of your own to
 *     dom_node::style and then call values_resolved() on that node.  A NULL
 *     dom_node::style is fine -- it reads as the initial computed style.
 *
 *   * Layout owns dom_node::box and fills in `values_box` for the queries
 *     whose resolved value is the used value.
 */
#ifndef VALUES_H
#define VALUES_H

#include <stddef.h>

#include "util.h"
#include "dom.h"
#include "cascade.h"
#include "style.h"

/* computed.line_height carries a used px length, except for `normal`, which
 * has no length until layout picks a ratio.  `normal` is stored as this
 * negative sentinel; test with (style->line_height < 0). */
#define VALUES_LINE_HEIGHT_NORMAL (-1.0)

/* Used geometry for one element, supplied by layout. */
typedef struct {
  double x, y, w, h;              /* border box */
  double content_w, content_h;
  double margin[4], padding[4], border[4];
  double inset[4];                /* used offsets for positioned boxes */
  int has_box;
} values_box;

/* Compute the `computed` style of every element, store it in node->style, and
 * return the root's.  Inheritance flows parent -> child; `font_size` and
 * `line_height` are resolved before any em-relative length that depends on
 * them.  `vw`/`vh` are the viewport size in CSS pixels, needed for
 * vw/vh/vmin/vmax.  `c` may be NULL, which computes every element from the
 * initial and inherited values alone.  Returns NULL only when `a` or the
 * document is NULL. */
computed *values_compute(arena *a, dom_doc *doc, cascade *c, double vw, double vh);

/* CSSOM resolved value for `prop` on `n`.  `box` supplies the USED geometry
 * for the properties whose resolved value is the used value; pass NULL when
 * the element generates no box.  Returns 0 and writes a NUL-terminated string
 * into buf, or -1 for an unsupported property (and writes "").
 *
 * The output is always NUL-terminated when buflen > 0, and is truncated
 * rather than overflowed if the buffer is too small; 64 bytes is always
 * enough.  buflen == 0 returns -1 and writes nothing. */
int values_resolved(dom_node *n, const values_box *box, const char *prop,
                    char *buf, size_t buflen);

#endif /* VALUES_H */
