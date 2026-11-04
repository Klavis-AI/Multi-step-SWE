/* cascade.h -- CSS 2.1 6.4 cascade order over a parsed document. */
#ifndef CSS_CASCADE_H
#define CSS_CASCADE_H

#include "util.h"
#include "dom.h"
#include "css.h"

typedef struct cascade cascade;

/* Collect every stylesheet in the document (<style> element text, in document
 * order) plus `extra` author sheets (which sort after the document's own, so
 * they win specificity ties), match them against every element, and record the
 * winning declaration per property per element.  `extra` may be NULL. */
cascade *cascade_build(arena *a, dom_doc *doc, css_sheet **extra, int n_extra);

/* Winning declared value for `prop` on `n`, or NULL if nothing won.
 * `*important` (may be NULL) is set to 1 when the winner carried !important. */
const char *cascade_value(cascade *c, dom_node *n, const char *prop, int *important);

/* NUL-terminated array of property names that have a winner on `n`.  Never
 * NULL; an element with no declarations yields an array whose first slot is
 * NULL.  Owned by the cascade's arena. */
const char **cascade_props(cascade *c, dom_node *n);

#endif /* CSS_CASCADE_H */
