/* wkt.h -- the well-known-type sources this build embeds.
 *
 * protoc resolves the bundled google.protobuf sources from inside its own distribution.
 * Parity with a protoc release therefore means embedding that release's
 * copies: the sources change between majors, which is what makes the release
 * train visible in --include_imports output.
 */
#ifndef PB_WKT_H
#define PB_WKT_H

#include <stddef.h>

typedef struct { const char *path; const char *text; size_t len; } WktFile;
extern const WktFile wkt_files[];
extern const int wkt_files_count;

/* Embedded source for an import path such as
 * "google/protobuf/descriptor.proto", or NULL if this build does not bundle it. */
const WktFile *wkt_lookup(const char *path);

#endif /* PB_WKT_H */
