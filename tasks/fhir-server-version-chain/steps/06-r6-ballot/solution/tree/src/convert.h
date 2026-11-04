/* convert.h -- render a resource against another release's definitions. */
#ifndef FHIR_CONVERT_H
#define FHIR_CONVERT_H

#include "util.h"
#include "json.h"
#include "defs.h"

/* -> a copy of `res` carrying only the elements `to` defines, or NULL when `to`
   has no such resource type at all.  `*dropped` counts the elements left out. */
jval *render_as(arena *a, fhirdefs *to, jval *res, int *dropped);

/* The release a `fhirVersion` media-type parameter names: "4.0" -> "4.0.1".
   -> the server's base_path for it, or NULL. */
const char *version_param_base(const char *mm);

#endif
