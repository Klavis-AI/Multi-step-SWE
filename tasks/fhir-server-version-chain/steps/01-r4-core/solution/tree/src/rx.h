/* rx.h -- the XML-Schema regular-expression subset FHIR uses for primitive
   types and for `constraint`/`pattern` regexes.  Whole-string match only. */
#ifndef FHIR_RX_H
#define FHIR_RX_H

#include "util.h"

typedef struct rx rx;

rx  *rx_compile(arena *a, const char *pattern);
int  rx_match(rx *r, const char *s);   /* 1 when the whole string matches */

#endif
