#ifndef KCP_CEL_H
#define KCP_CEL_H
#include "json.h"
/* Evaluate a CEL expression against a map of activation variables.
 * Returns a freshly allocated json value, or NULL with *err set (caller frees). */
json *cel_eval(const char *expr, json *activation, char **err);
/* Compile-check only: NULL on success, an error string otherwise. */
char *cel_check(const char *expr);
#endif
