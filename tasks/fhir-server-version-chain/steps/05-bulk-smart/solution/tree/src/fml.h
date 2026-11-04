/* fml.h -- the FHIR Mapping Language (mapping-language.html) and the engine that
   runs a StructureMap over an instance. */
#ifndef FHIR_FML_H
#define FHIR_FML_H

#include "defs.h"

typedef struct fmlmap fmlmap;

/* Parse FML source text.  Returns NULL and fills `err` on a syntax error. */
fmlmap *fml_parse(arena *a, const char *text, char *err, size_t errlen);
/* Parse a StructureMap resource (the JSON form of the same thing). */
fmlmap *fml_from_resource(arena *a, jval *sm, char *err, size_t errlen);
/* The map's canonical url and name. */
const char *fml_url(fmlmap *m);
const char *fml_name(fmlmap *m);
/* The StructureMap resource form of a parsed map. */
jval *fml_to_resource(arena *a, fmlmap *m);
/* Run the map: `src` is the input resource, the result is the target resource. */
jval *fml_transform(arena *a, fhirdefs *d, fmlmap *m, jval *src, char *err, size_t errlen);

#endif
