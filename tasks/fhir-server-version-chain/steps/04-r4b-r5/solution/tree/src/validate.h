/* validate.h -- StructureDefinition-driven structural validation. */
#ifndef FHIR_VALIDATE_H
#define FHIR_VALIDATE_H

#include "defs.h"

typedef struct {
    const char *severity;   /* "error" | "warning" | "information" */
    const char *code;       /* IssueType code */
    const char *expression; /* FHIRPath-ish location */
    const char *text;
} vissue;

typedef struct {
    arena  *a;
    vissue *v;
    size_t  n, cap;
    int     errors;
} vresult;

void vres_init(vresult *r, arena *a);
void vres_add(vresult *r, const char *sev, const char *code, const char *expr, const char *text);

/* Validate one resource against the base definitions of `d`.  When `profile` is
   non-NULL the instance is additionally validated against that profile. */
void validate_resource(arena *a, fhirdefs *d, jval *res, const char *profile, vresult *out);

#endif
