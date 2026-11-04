/* options.h -- interpreting `option` statements into an options submessage.
 *
 * protoc does not leave options uninterpreted: `option java_package = "x"`
 * becomes FileOptions.java_package, and `option (pkg.my_opt) = "tagged"`
 * becomes the extension field's own number carrying a value encoded per its
 * declared type. Both were measured; an implementation that emits
 * uninterpreted_option instead produces different bytes.
 */
#ifndef PB_OPTIONS_H
#define PB_OPTIONS_H

#include "pb.h"
#include "ast.h"
#include "desc.h"

/* Interprets `opts` against the descriptor for the relevant options message
 * (FileOptions, MessageOptions, FieldOptions, ...) and writes the result into
 * `out`. `scope` is the package of the file being compiled, used to resolve a
 * relative extension name. Problems are reported through `d`. */
/* `retain_source` mirrors protoc's --retain_options. Without it, an option
 * whose extension declares [retention = RETENTION_SOURCE] is stripped from the
 * descriptor -- measured: the default emits only the runtime-retained value,
 * and --retain_options brings the source-retained one back. */
void options_interpret(Arena *a, Diags *d, const Pool *pool, const Desc *opt_desc,
                       const char *scope, const AstOption *opts, Dyn *out,
                       bool retain_source, const char *file);

#endif /* PB_OPTIONS_H */
