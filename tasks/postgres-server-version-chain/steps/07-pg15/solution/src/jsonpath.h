/* jsonpath.h - the SQL/JSON path language, new in PostgreSQL 12.
 *
 * Everything here is compiled away below release 12; callers guard their use
 * with #if PGVC_VN >= 120 so the linker never sees a reference. */
#ifndef PGVC_JSONPATH_H
#define PGVC_JSONPATH_H

#include "engine.h"

#if PGVC_VN >= 120

typedef struct JsonPath JsonPath;

/* A list of matched items.  Zero-initialise before use, free .v after. */
typedef struct { JsonVal **v; int n, cap; } JsonPathList;

/* Parse a jsonpath literal.  Raises PostgreSQL's error on bad input, so it
 * never returns NULL. */
JsonPath *jsonpath_parse(Session *s, const char *str, int len);
/* PostgreSQL's canonical spelling of a parsed path; malloc'd. */
char     *jsonpath_text(const JsonPath *jp);
void      jsonpath_free(JsonPath *jp);

/* Run a path over a document.  vars is the "vars" object or NULL.  When
 * throwerr is false a structural error stops the walk and returns false
 * instead of being raised; whatever matched before it stays in *out.
 * Other errors (an undefined variable, a bad "vars" argument) always raise.
 * first_only stops at the first match, which is what jsonb_path_exists()
 * does and is visible when a later item would have raised. */
bool jsonpath_run(Session *s, const JsonPath *jp, JsonVal *doc, JsonVal *vars,
                  bool throwerr, bool first_only, JsonPathList *out);

/* The SQL surface: the jsonb_path_* family and the @? and @@ operators.
 * Both leave *found / *handled false when the name is not one of theirs. */
Datum jsonpath_builtin(Session *s, const char *name, Datum *args, int nargs, bool *found);
Datum jsonpath_operator(Session *s, const char *name, Datum *args, int nargs, bool *handled);

#endif /* PGVC_VN >= 120 */
#endif /* PGVC_JSONPATH_H */
