/* wire.h — wire codec and the dynamic message.
 *
 * Deliberately schema-free at the bottom: a Dyn can be built and serialized
 * with no descriptor at all, because serialization needs only (field number,
 * wire type, bytes). That is what lets the compiler bootstrap: it parses its
 * own bundled descriptor.proto, emits that file's FileDescriptorProto through
 * this API with no schema in hand, and only afterwards has descriptors with
 * which to parse descriptor sets, text format and JSON.
 *
 * Fields are kept in insertion order within a field number, and serialized in
 * ascending field-number order. protoc emits descriptors that way, and
 * byte-identical output is the whole game here.
 */
#ifndef PB_WIRE_H
#define PB_WIRE_H

#include "pb.h"

/* ---------------------------------------------------------------- encoding */
void wire_put_varint(Buf *b, uint64_t v);
void wire_put_tag(Buf *b, uint32_t field, int wt);
void wire_put_fixed32(Buf *b, uint32_t v);
void wire_put_fixed64(Buf *b, uint64_t v);
void wire_put_len(Buf *b, uint32_t field, Str payload);
size_t wire_varint_size(uint64_t v);

/* ---------------------------------------------------------------- decoding */
typedef struct { const uint8_t *p, *end; const char *err; } WRd;
void wrd_init(WRd *r, Str s);
bool wrd_tag(WRd *r, uint32_t *field, int *wt);
bool wrd_varint(WRd *r, uint64_t *v);
bool wrd_fixed32(WRd *r, uint32_t *v);
bool wrd_fixed64(WRd *r, uint64_t *v);
bool wrd_len(WRd *r, Str *s);
/* Skips one field of the given wire type, including a whole group for
 * WT_SGROUP, and appends the raw bytes (tag included) to `raw` when non-NULL. */
bool wrd_skip_field(WRd *r, uint32_t field, int wt, Buf *raw);
static inline bool wrd_eof(const WRd *r) { return r->p >= r->end; }

/* --------------------------------------------------------------------- Dyn */
typedef struct Dyn Dyn;

Dyn *dyn_new(Arena *a);
void dyn_clear(Dyn *m);

/* Low-level, schema-free putters. The caller states the encoding, so these
 * work before any descriptor exists. Repeated fields are built by calling the
 * putter more than once with the same field number. */
void dyn_put_varint(Dyn *m, uint32_t field, uint64_t v);
void dyn_put_fixed32(Dyn *m, uint32_t field, uint32_t v);
void dyn_put_fixed64(Dyn *m, uint32_t field, uint64_t v);
void dyn_put_bytes(Dyn *m, uint32_t field, Str s);
Dyn *dyn_put_msg(Dyn *m, uint32_t field);     /* length-prefixed submessage */
Dyn *dyn_put_group(Dyn *m, uint32_t field);   /* SGROUP/EGROUP delimited    */
/* Raw bytes already carrying their own tag(s): used for unknown-field
 * retention and for splicing a preserved extension back in at its number. */
void dyn_put_raw(Dyn *m, uint32_t field, Str tagged_bytes);

/* Convenience wrappers over the putters. */
void dyn_put_i32(Dyn *m, uint32_t field, int32_t v);
void dyn_put_i64(Dyn *m, uint32_t field, int64_t v);
void dyn_put_u32(Dyn *m, uint32_t field, uint32_t v);
void dyn_put_u64(Dyn *m, uint32_t field, uint64_t v);
void dyn_put_sint32(Dyn *m, uint32_t field, int32_t v);
void dyn_put_sint64(Dyn *m, uint32_t field, int64_t v);
void dyn_put_bool(Dyn *m, uint32_t field, bool v);
void dyn_put_float(Dyn *m, uint32_t field, float v);
void dyn_put_double(Dyn *m, uint32_t field, double v);
void dyn_put_str(Dyn *m, uint32_t field, const char *s);

/* Introspection, used by the text/JSON writers and by the option interpreter. */
int   dyn_count(const Dyn *m, uint32_t field);
bool  dyn_get_varint(const Dyn *m, uint32_t field, int idx, uint64_t *v);
bool  dyn_get_bytes(const Dyn *m, uint32_t field, int idx, Str *s);
Dyn  *dyn_get_msg(const Dyn *m, uint32_t field, int idx);
/* Iterate every present field number in ascending order. */
int   dyn_fields(const Dyn *m, uint32_t *out, int max);
/* Arrival order of one value, so a consumer can resolve oneof last-wins. */
uint64_t dyn_seq(const Dyn *m, uint32_t field, int idx);

/* Parses raw wire bytes with no schema. Length-delimited fields are kept as
 * bytes; dyn_get_msg re-parses one lazily when a caller asks for it as a
 * message and caches the result. That is the only honest thing a schema-free
 * parser can do -- on the wire a submessage and a string are the same shape --
 * and it is exactly what --decode_raw has to reckon with too. */
Dyn *dyn_parse_bytes(Arena *a, Str in, const char **err);

/* Folds every value of `field` into a single length-delimited value holding
 * their concatenated payloads: the packed encoding. */
void dyn_pack_field(Dyn *m, uint32_t field);

/* Serialize in ascending field-number order; within one field number, in
 * insertion order. */
enum { DYN_CANON_NONE = 0, DYN_CANON_I32, DYN_CANON_U32, DYN_CANON_BOOL };
void dyn_canon_field(Dyn *m, uint32_t field, int mode);
void dyn_keep_last(Dyn *m, uint32_t field);
void dyn_unpack_field(Dyn *m, uint32_t field, int wire_type);
void dyn_clear_field(Dyn *m, uint32_t field);
void dyn_drop_last(Dyn *m, uint32_t field);
bool dyn_promote_msg(Dyn *m, uint32_t field, int idx);
void dyn_make_group(Dyn *m, uint32_t field);
Str dyn_serialize(const Dyn *m, Arena *a);

#endif /* PB_WIRE_H */
