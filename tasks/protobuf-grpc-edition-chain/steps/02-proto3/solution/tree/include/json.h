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

/* Parses ProtoJSON into a message. Returns NULL and sets *err on malformed
 * JSON, an unknown field, or a value that does not fit its field's type. */
Dyn *json_parse(Arena *a, Str in, const Desc *d, const char **err);

#endif /* PB_JSON_H */
