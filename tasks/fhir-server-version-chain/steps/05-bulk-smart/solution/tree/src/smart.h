/* smart.h -- SMART on FHIR: bearer tokens and scope enforcement. */
#ifndef FHIR_SMART_H
#define FHIR_SMART_H

#include "util.h"
#include "json.h"

typedef struct {
    int   present;        /* an Authorization: Bearer header was sent      */
    int   ok;             /* ... and it verified                           */
    const char *reason;   /* why not, when it did not                      */
    const char *patient;  /* the `patient` claim, when there is one        */
    vec   scopes;         /* const char* as written in the token           */
} smart_token;

/* Verify a bearer token against the JWK set, checking `exp` and `iss`. */
void smart_verify(arena *a, const char *authz, const char *issuer, smart_token *out);

/* May these scopes perform `op` (one of c r u d s) on `type`?
   `compartment_patient` receives the patient a `patient/` scope confines the
   request to, or NULL. */
int smart_allows(const smart_token *t, const char *type, char op,
                 const char **compartment_patient);

jval *smart_configuration(arena *a, const char *base_url);

#endif
