/* json.h -- the ProtoJSON mapping.
 *
 * Normative reference: protobuf.dev "ProtoJSON Format". The rules that decide
 * bytes, and that a naive JSON writer gets wrong:
 *   - field names are the descriptor's json_name (lowerCamelCase) by default;
 *   - 64-bit integers are JSON STRINGS, not numbers;
 *   - bytes are base64 (standard alphabet, padded);
 *   - enums print the value's name, or the number when unknown;
 *   - non-finite doubles print as "Infinity" / "-Infinity" / "NaN" strings;
 *   - implicit-presence fields at their default value are omitted entirely.
 */
#ifndef PB_JSON_H
#define PB_JSON_H
#include "pb.h"
#include "wire.h"
#include "desc.h"

/* Renders a message as ProtoJSON. */
Str json_write(Arena *a, const Dyn *m, const Desc *d);
/* Same, reporting a serialize-side refusal: ProtoJSON can decline to write a
 * FieldMask path that does not round-trip, or a Value holding Inf/NaN. */
Str json_write_opts(Arena *a, const Dyn *m, const Desc *d, const Pool *pool,
                    const char **err);

/* Parses ProtoJSON into a message. Returns NULL and sets *err on malformed
 * JSON, an unknown field, or a value that does not fit its field's type. */
Dyn *json_parse(Arena *a, Str in, const Desc *d, const char **err);
/* Same, with the conformance suite's JSON_IGNORE_UNKNOWN_PARSING_TEST mode:
 * unknown members and unknown enum value names are skipped, not refused. */
Dyn *json_parse_opts(Arena *a, Str in, const Desc *d, bool ignore_unknown,
                     const Pool *pool, const char **err);

#endif /* PB_JSON_H */
