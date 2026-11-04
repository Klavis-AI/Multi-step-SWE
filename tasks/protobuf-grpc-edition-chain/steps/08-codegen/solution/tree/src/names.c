/* names.c — the naming rules protoc applies when building descriptors.
 *
 * Every rule here was measured against protoc 32.1 rather than taken from the
 * documentation, because byte-identical descriptor output depends on the exact
 * edge cases and the docs do not spell them out. The tables in the comments
 * are the observations.
 */
#include "pb.h"
#include <ctype.h>
#include <string.h>

/* json_name: drop every '_', upper-case the character that followed one when
 * it is a letter, never force the first character's case.
 *
 *   _foo           -> Foo          foo_            -> foo
 *   foo__bar       -> fooBar       foo1bar         -> foo1bar
 *   FOO_BAR        -> FOOBAR       a_b_c           -> aBC
 *   x9_9y          -> x99y         __              -> ""
 *   ALLCAPS        -> ALLCAPS      mixedCase_here  -> mixedCaseHere
 *
 * A digit after '_' is appended unchanged and clears the pending capital,
 * which is why x9_9y keeps its second 9 rather than swallowing it.
 */
char *json_name_of(Arena *a, const char *name) {
  size_t n = strlen(name);
  char *out = arena_alloc(a, n + 1);
  size_t j = 0;
  bool cap = false;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)name[i];
    if (c == '_') { cap = true; continue; }
    if (cap && isalpha(c)) out[j++] = (char)toupper(c);
    else out[j++] = (char)c;
    cap = false;
  }
  out[j] = '\0';
  return out;
}

/* The nested message protoc synthesizes for a map field: capitalize the first
 * letter of every '_'-separated segment, drop the underscores, append "Entry".
 *
 *   m          -> MEntry
 *   my_map     -> MyMapEntry
 *   a_b_c_map  -> ABCMapEntry
 */
char *map_entry_name_of(Arena *a, const char *field_name) {
  size_t n = strlen(field_name);
  char *out = arena_alloc(a, n + 6); /* + "Entry" + NUL, underscores only shrink */
  size_t j = 0;
  bool cap = true;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)field_name[i];
    if (c == '_') { cap = true; continue; }
    if (cap && isalpha(c)) out[j++] = (char)toupper(c);
    else out[j++] = (char)c;
    cap = false;
  }
  memcpy(out + j, "Entry", 5);
  out[j + 5] = '\0';
  return out;
}

/* A group declares a nested message under the group's own name, and a field
 * whose name is that name **fully lower-cased**, underscores kept.
 *
 *   group Grp       -> field "grp",       type_name ".pkg.M.Grp"
 *   group My_Group  -> field "my_group",  type_name ".pkg.M.My_Group"
 *   group ALLCAPS   -> field "allcaps",   type_name ".pkg.M.ALLCAPS"
 */
char *group_field_name_of(Arena *a, const char *group_name) {
  size_t n = strlen(group_name);
  char *out = arena_alloc(a, n + 1);
  for (size_t i = 0; i < n; i++)
    out[i] = (char)tolower((unsigned char)group_name[i]);
  out[n] = '\0';
  return out;
}
