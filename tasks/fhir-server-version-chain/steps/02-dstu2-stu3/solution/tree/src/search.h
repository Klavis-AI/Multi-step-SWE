/* search.h -- FHIR search: parameter parsing, index-value extraction, matching. */
#ifndef FHIR_SEARCH_H
#define FHIR_SEARCH_H

#include "defs.h"
#include "store.h"

/* One parsed search value (after ',' splitting). */
typedef struct {
    const char *raw;
    const char *prefix;        /* eq ne gt lt ge le sa eb ap, or NULL */
    const char *v1, *v2, *v3;  /* token: system|code ; quantity: num|sys|code */
    int         has_bar;
} sval;

typedef struct {
    const char *name;          /* code without modifier            */
    const char *modifier;      /* after ':' , or NULL              */
    const char *chain;         /* after '.' , or NULL              */
    const char *chain_type;    /* "Patient" in subject:Patient.name */
    sval       *vals;
    size_t      nval;          /* OR within one parameter          */
    sparam     *sp;
    /* _has:Type:ref:code=value */
    const char *has_type, *has_ref, *has_code;
} sparamq;

typedef struct {
    sparamq    *params;
    size_t      nparam;
    /* result parameters */
    long        count;         /* -1 when absent */
    long        offset;
    const char *sort;
    vec         includes;      /* const char* "Type:code[:target]" */
    vec         revincludes;
    const char *summary;
    vec         elements;      /* const char* */
    const char *total_mode;
    const char *format;
    vec         types;         /* _type for system search */
    int         bad;           /* unknown parameter or malformed value */
    const char *bad_detail;
} squery;

/* Parse `qs` (raw query string) for resource `type` (NULL = system level). */
int  sq_parse(arena *a, fhirdefs *d, const char *type, const char *qs, squery *out);
/* Does `res` match every parameter of `q`?  `resolve` finds referenced resources. */
int  sq_match(arena *a, fhirdefs *d, store *st, jval *res, const squery *q);
/* Extract the index strings a parameter produces for one resource. */
void sq_values(arena *a, fhirdefs *d, jval *res, sparam *sp, vec *out /* char* */);
/* Comparable sort key for `_sort` on one parameter. */
char *sq_sortkey(arena *a, fhirdefs *d, jval *res, const char *code);

long long fhir_date_lo(const char *s);
long long fhir_date_hi(const char *s);

#endif
