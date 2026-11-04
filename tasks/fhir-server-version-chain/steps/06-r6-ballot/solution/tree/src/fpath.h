/* fpath.h -- the FHIRPath subset used by SearchParameter.expression.
   Supports: path navigation, '|' union, parentheses, 'as'/'is'/ofType(),
   where(...) with '=', '!=', 'and', 'or', resolve(), extension(url),
   hasExtension(url), exists(), first(), [index], and string literals. */
#ifndef FHIR_FPATH_H
#define FHIR_FPATH_H

#include "defs.h"

typedef struct {
    jval       *v;
    const char *type;   /* FHIR type name of v, when known           */
    sdef       *sd;     /* structure whose element tree holds v's children */
    edef       *node;   /* element node for v inside sd               */
} fpnode;

typedef struct {
    fpnode *v;
    size_t  n, cap;
} fpcoll;

void fp_free(fpcoll *c);

/* Evaluate `expr` against `root` (a resource).  Results are borrowed pointers
   into `root`; `a` is used only for temporary strings. */
fpcoll fp_eval(arena *a, fhirdefs *d, jval *root, const char *expr);
/* Evaluate `expr` with `start` as the input collection (used by composite
   search parameters, whose components are relative to the composite's own
   expression). */
fpcoll fp_eval_in(arena *a, fhirdefs *d, const fpcoll *start, const char *expr);

#endif
