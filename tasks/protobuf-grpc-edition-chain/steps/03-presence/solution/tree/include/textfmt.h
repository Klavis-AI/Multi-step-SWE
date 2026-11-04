/* textfmt.h -- protobuf text format reader/writer. */
#ifndef PB_TEXTFMT_H
#define PB_TEXTFMT_H
#include "pb.h"
#include "wire.h"
#include "desc.h"

/* Renders a message in protoc's --decode shape. */
Str tf_write(Arena *a, const Dyn *m, const Desc *d);

/* Renders raw tag/value pairs in protoc's --decode_raw shape. */
Str tf_decode_raw(Arena *a, Str in);

/* Text-format string escaping, exposed for the option printer. */
void tf_quote(Buf *b, Str s);

/* Parses text format into a message. The --encode direction. */
Dyn *tf_parse(Arena *a, Diags *d, const char *file, Str in, const Desc *desc);

#endif /* PB_TEXTFMT_H */
