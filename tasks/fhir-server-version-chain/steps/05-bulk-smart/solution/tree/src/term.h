#ifndef TERM_H
#define TERM_H
#include "defs.h"
#include "json.h"

/* The terminology operations of `terminology-service.html`.  Each returns the
   resource to send, or NULL with *status set when the request cannot be
   answered. */
jval *term_expand(arena *a, fhirdefs *d, const char *url, const char *filter,
                  int count, int offset, int *status, const char **msg);
jval *term_validate_code(arena *a, fhirdefs *d, const char *vsurl, const char *system,
                         const char *code, const char *display, jval *coding,
                         jval *cc, int *status, const char **msg);
jval *term_lookup(arena *a, fhirdefs *d, const char *system, const char *code,
                  int *status, const char **msg);
jval *term_subsumes(arena *a, fhirdefs *d, const char *system, const char *a_code,
                    const char *b_code, int *status, const char **msg);
jval *term_translate(arena *a, fhirdefs *d, const char *cmurl, const char *system,
                     const char *code, const char *target, int *status, const char **msg);

int vs_has_code(fhirdefs *d, const char *vsurl, const char *system, const char *code);

#endif
