/* names.h — protoc's descriptor naming rules. See names.c for the measured
 * edge cases each of these reproduces. */
#ifndef PB_NAMES_H
#define PB_NAMES_H
#include "pb.h"
char *json_name_of(Arena *a, const char *name);
char *map_entry_name_of(Arena *a, const char *field_name);
char *group_field_name_of(Arena *a, const char *group_name);
#endif /* PB_NAMES_H */
