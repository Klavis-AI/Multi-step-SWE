/* fxml.h -- the FHIR XML representation, driven by the StructureDefinitions. */
#ifndef FHIR_XML_H
#define FHIR_XML_H

#include "defs.h"

void  fhir_to_xml(arena *a, fhirdefs *d, jval *res, buf *out);
jval *fhir_from_xml(arena *a, fhirdefs *d, const char *text, size_t len, char *err, size_t errlen);

#endif
