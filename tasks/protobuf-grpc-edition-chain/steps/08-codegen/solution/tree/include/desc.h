/* desc.h -- the resolved descriptor graph.
 *
 * build.c produces a FileDescriptorProto as raw bytes. Everything that has to
 * *interpret* a message -- text format, JSON, the wire codec, the conformance
 * shim -- needs those bytes turned back into a linked graph with resolved
 * type pointers. That is what a Pool does, and it is how the compiler
 * self-hosts: it parses its own bundled descriptor.proto, builds a
 * FileDescriptorProto from it, loads that into a Pool, and from then on can
 * parse any FileDescriptorSet including its own.
 */
#ifndef PB_DESC_H
#define PB_DESC_H

#include "pb.h"
#include "wire.h"

typedef struct EnumValD { const char *name; int32_t number; struct EnumValD *next; } EnumValD;

struct EnumD {
  const char *name, *full_name;
  EnumValD *values;
  const FileD *file;
  EnumD *next;
};

struct FieldD {
  const char *name, *json_name, *full_name;
  uint32_t number;
  int type;          /* T_*  */
  int label;         /* L_*  */
  const char *type_name;   /* fully qualified, no leading dot, or NULL */
  const Desc *msg_type;    /* resolved when type is message/group */
  const EnumD *enum_type;  /* resolved when type is enum */
  const char *default_value; /* as protoc stores it, or NULL */
  int oneof_index;           /* -1 when not in a oneof */
  bool proto3_optional;
  bool packed;               /* resolved encoding for repeated numerics */
  bool delimited;            /* resolved message_encoding == DELIMITED */
  bool is_extension;
  int retention;     /* FieldOptions.retention: 2 == RETENTION_SOURCE */
  const char *extendee;
  const Desc *parent;
  FieldD *next;
};

struct Desc {
  const char *name, *full_name;
  FieldD *fields;
  Desc *nested;
  EnumD *enums;
  FieldD *extensions;
  bool is_map_entry;
  bool is_group;
  const FileD *file;
  const Desc *parent;
  Desc *next;
};

struct FileD {
  const char *name, *package;
  int edition;          /* ED_PROTO2 | ED_PROTO3 | ED_2023 | ED_2024 */
  Features features;    /* resolved file-level features (editions) */
  Desc *messages;
  EnumD *enums;
  FieldD *extensions;
  FileD *next;
};

/* Loads one serialized FileDescriptorProto into the pool. Dependencies must
 * already be loaded: resolution looks them up by fully-qualified name. */
const FileD *pool_add_serialized(Pool *p, Str fdproto_bytes, const char **err);

/* Loads a whole serialized FileDescriptorSet, in order. */
int pool_add_set(Pool *p, Str set_bytes, const char **err);

const Desc   *pool_find_message(const Pool *p, const char *full_name);
const EnumD  *pool_find_enum(const Pool *p, const char *full_name);
/* Finds an extension of `extendee_full_name` whose own full name is
 * `ext_full_name`. Custom options resolve through this. */
const FieldD *pool_find_extension(const Pool *p, const char *extendee_full_name,
                                  const char *ext_full_name);
const FieldD *pool_find_extension_by_number(const Pool *p,
                                            const char *extendee_full_name,
                                            uint32_t number);

/* Checks every packed repeated numeric field's payload decodes exactly to its
 * end. A truncated final element must make the whole message fail to parse. */
bool dyn_validate_packed(const Dyn *m, const Desc *d);

/* Re-encodes a parsed message the way its schema says it must be written: a
 * repeated numeric field follows its declared packedness whatever form it
 * arrived in, and only the last-arrived member of a oneof survives. Unknown
 * fields are not in the Desc and are therefore left exactly as received. */
void dyn_normalize_wire(Dyn *m, const Desc *d);

const FieldD *desc_field_by_number(const Desc *d, uint32_t number);
const FieldD *desc_field_by_name(const Desc *d, const char *name);

#endif /* PB_DESC_H */
