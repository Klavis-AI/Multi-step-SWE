/* selector.h -- CSS 2.1 selector matching.
 *
 * Scope is CSS 2.1 (https://www.w3.org/TR/CSS21/selector.html) and nothing
 * more.  Selectors-3+ syntax (`~`, :nth-child(), :not(), ^=, $=, *=,
 * namespaces, ...) is rejected as invalid.
 */
#ifndef CSS_SELECTOR_H
#define CSS_SELECTOR_H

#include "util.h"
#include "dom.h"

typedef struct sel_list sel_list;

/* Parse a comma-separated selector list.  Returns NULL if the list is not
 * valid CSS 2.1; per CSS 2.1 4.1.7 one bad member invalidates the whole list. */
sel_list *sel_parse(arena *a, const char *text);

/* 1 if any complex selector in the list matches `n`.  A selector carrying a
 * pseudo-element never matches an element. */
int sel_matches(sel_list *s, dom_node *n);

/* Specificity of the highest-specificity complex selector in the list that
 * matches `n`, as CSS 2.1 a/b/c (a = #id, b = class/attr/pseudo-class,
 * c = type/pseudo-element).  Returns 1 if something matched, else 0 (and the
 * out-params are zeroed).  Any out-param may be NULL. */
int sel_specificity_for(sel_list *s, dom_node *n, int *a_, int *b_, int *c_);

/* Specificity of a single complex selector (no comma allowed), without
 * matching.  Returns 1 on success, 0 if the selector is not valid CSS 2.1. */
int sel_specificity(arena *a, const char *one_complex_selector, int *a_, int *b_, int *c_);

#endif /* CSS_SELECTOR_H */
