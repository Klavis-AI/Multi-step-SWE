# The GNU gold linker plugin API (LTO)

> What a linker must expose so that the GCC and LLVM LTO plugins can claim IR inputs, ask about symbol resolution, and hand back real object files.

## Provenance

Restated from the reference implementation's plugin host,
`libwild/src/linker_plugins.rs`, at tags `0.9.0` (step 8) and `0.10.0` (step 9),
cross-checked against `libwild/src/resolution.rs` (`load_symbols_in_redirect`)
and `libwild/src/args/elf.rs` (`--plugin`, `--plugin-opt`). The transfer-vector
contents are byte-identical between those two tags. Nothing is copied from
`plugin-api.h`; every number below was read out of the reference's own tag
enumeration and structure definitions, which are what the graded Oracle actually
sends to a plugin.

Divergences from the published API that the implementation makes normative here:

1. **The transfer vector is deliberately incomplete.** The reference never
   advertises `LDPT_SET_EXTRA_LIBRARY_PATH` (16), `LDPT_GNU_LD_VERSION` (17),
   `LDPT_GOLD_VERSION` (2), any `LDPT_GET_INPUT_SECTION_*` tag (19, 20, 21, 22,
   29, 30), `LDPT_UPDATE_SECTION_ORDER` (23),
   `LDPT_ALLOW_SECTION_ORDERING` (24), the unique-segment pair (26, 27),
   `LDPT_REGISTER_NEW_INPUT_HOOK` (31) or `LDPT_REGISTER_CLAIM_FILE_HOOK_V2`
   (35). Real GCC and LLVM plugins work without them. §3.
2. **`get_symbols` (9) and `get_symbols_v2` (25) are advertised but are stubs.**
   Both point at a function that records "the plugin uses an older, unsupported
   version of the API" and returns the error status, aborting the link. Only
   `LDPT_GET_SYMBOLS_V3` (28) is functional. §3, §7.
3. **The `--wrap`/`--defsym` preservation rule is implemented by flagging the
   affected symbols, not by a separate API call.** §8.
4. No `wild: error: `-style prefix is part of the diagnostic contract. A plugin
   failure MUST exit non-zero with at least one stderr line naming what failed;
   nothing about the wording is graded.

Not verified here: `LDPT_ADD_INPUT_LIBRARY` (14) against a real plugin (none
locally exercises it), and the `LDPT_REGISTER_CLAIM_FILE_HOOK_V2` protocol,
which the reference does not implement.

## 1. Model

LTO defers code generation to the compiler. Inputs holding compiler IR instead
of machine code are handed to a shared library (the *plugin*), which claims
them, states what symbols they define and reference, and — once the linker has
resolved every symbol — compiles them into ordinary ELF relocatable objects fed
back into the same link. The linker is the host: it loads the plugin and hands
it a callback table (§2, §3), offers it every input (§5), records the symbols it
pushes back (§6), answers its questions about how each was resolved (§7),
ingests the objects it produces and re-resolves (§9), and calls its cleanup hook
(§10).

## 2. Loading and `onload`

`--plugin PATH` names a shared object. `--plugin-opt OPT` may be repeated; each
value is passed to the plugin verbatim as a NUL-terminated string.

At the point the first input needs to be offered to the plugin, `dlopen` the
library and look up the exported symbol `onload`. Its C signature is

```
void onload(struct ld_plugin_tv *tv);
```

If `dlopen` fails, or `onload` is missing, the link MUST fail naming the plugin
path. A statically linked linker cannot `dlopen` at all and MUST fail with a
diagnostic saying so rather than silently skipping LTO.

Before calling `onload`, raise the soft file-descriptor limit to the hard limit
if possible. Claimed IR files must stay open for the whole link because the
plugin reads them back through the descriptor you gave it; a large LTO link
otherwise exhausts descriptors. Failing to raise the limit is not an error.

## 3. The transfer vector

`onload` receives a pointer to an array of entries, terminated by an entry whose
tag is 0. Each entry is:

a 4-byte `tag`, then a union of int / `const char *` / function pointer at the
next pointer-aligned offset — 16 bytes per entry on x86-64. Integer-valued tags
occupy the low bits of the value slot.

Complete tag numbering; you only *provide* the ones marked yes:

| tag | name | provided? |
| --- | --- | --- |
| 0 | `LDPT_NULL` (terminator) | yes, last |
| 1 | `LDPT_API_VERSION` | yes, value `1` |
| 2 | `LDPT_GOLD_VERSION` | no |
| 3 | `LDPT_LINKER_OUTPUT` | yes, see §4 |
| 4 | `LDPT_OPTION` | yes, one per `--plugin-opt` |
| 5 | `LDPT_REGISTER_CLAIM_FILE_HOOK` | yes |
| 6 | `LDPT_REGISTER_ALL_SYMBOLS_READ_HOOK` | yes |
| 7 | `LDPT_REGISTER_CLEANUP_HOOK` | yes |
| 8 | `LDPT_ADD_SYMBOLS` | yes |
| 9 | `LDPT_GET_SYMBOLS` | yes, but stub — see below |
| 10 | `LDPT_ADD_INPUT_FILE` | yes |
| 11 | `LDPT_MESSAGE` | yes, **registered first** |
| 12 | `LDPT_GET_INPUT_FILE` | yes |
| 13 | `LDPT_RELEASE_INPUT_FILE` | yes |
| 14 | `LDPT_ADD_INPUT_LIBRARY` | yes |
| 15 | `LDPT_OUTPUT_NAME` | yes |
| 16 | `LDPT_SET_EXTRA_LIBRARY_PATH` | no |
| 17 | `LDPT_GNU_LD_VERSION` | no |
| 18 | `LDPT_GET_VIEW` | yes |
| 19 | `LDPT_GET_INPUT_SECTION_COUNT` | no |
| 20 | `LDPT_GET_INPUT_SECTION_TYPE` | no |
| 21 | `LDPT_GET_INPUT_SECTION_NAME` | no |
| 22 | `LDPT_GET_INPUT_SECTION_CONTENTS` | no |
| 23 | `LDPT_UPDATE_SECTION_ORDER` | no |
| 24 | `LDPT_ALLOW_SECTION_ORDERING` | no |
| 25 | `LDPT_GET_SYMBOLS_V2` | yes, but stub |
| 26 | `LDPT_ALLOW_UNIQUE_SEGMENT_FOR_SECTIONS` | no |
| 27 | `LDPT_UNIQUE_SEGMENT_FOR_SECTIONS` | no |
| 28 | `LDPT_GET_SYMBOLS_V3` | yes, functional |
| 29 | `LDPT_GET_INPUT_SECTION_ALIGNMENT` | no |
| 30 | `LDPT_GET_INPUT_SECTION_SIZE` | no |
| 31 | `LDPT_REGISTER_NEW_INPUT_HOOK` | no |
| 32 | `LDPT_GET_WRAP_SYMBOLS` | yes |
| 33 | `LDPT_ADD_SYMBOLS_V2` | yes, same function as tag 8 |
| 34 | `LDPT_GET_API_VERSION` | yes |
| 35 | `LDPT_REGISTER_CLAIM_FILE_HOOK_V2` | no |

**Order within the vector is load-bearing.** Plugins walk the array front to
back and act on entries as they see them. `LDPT_MESSAGE` MUST be the *first*
entry: a plugin that hits a bad `LDPT_OPTION` before it has a message callback
cannot report the problem and silently ignores it. The reference emits, in this
order: message (11); every `--plugin-opt` as `LDPT_OPTION` (4); api-version (1);
linker-output (3); output-name (15); register-claim-file (5);
register-cleanup (7); register-all-symbols-read (6); get-api-version (34);
add-symbols (8); add-symbols-v2 (33); get-symbols-v3 (28); add-input-file (10);
add-input-library (14); get-symbols (9, stub); get-symbols-v2 (25, stub);
get-view (18); get-wrap-symbols (32); get-input-file (12);
release-input-file (13); then `LDPT_NULL`.

Registering 9 and 25 as *stubs* rather than omitting them is deliberate: a
plugin finding no `GET_SYMBOLS` tag may fail obscurely, whereas a stub gives a
clear "unsupported API version" diagnostic and a failed link. Each stub takes no
arguments, records that message and returns the error status.

You MUST NOT assume `onload` registers all three hooks. Record which of
claim-file, all-symbols-read and cleanup were registered and call only those; a
missing claim-file hook MUST fail the link when a file is offered.

## 4. Statuses, enums and structures

Return status of every callback, and of the hooks the plugin registers:

| value | name | meaning |
| --- | --- | --- |
| 0 | OK | success |
| 1 | no symbols | the handle has no symbols to report |
| 2 | bad handle | the handle the linker passed is not recognised |
| 3 | error | failure |

Anything other than OK from a plugin hook MUST fail the link, except that
`no symbols` from your own `get_symbols_v3` is a normal answer (§7).

`LDPT_LINKER_OUTPUT` value, chosen from the link's own output kind:

| value | kind |
| --- | --- |
| 0 | relocatable (`-r`) |
| 1 | non-PIE executable |
| 2 | shared object |
| 3 | PIE executable |

The input-file descriptor passed to the claim hook, and filled in by
`get_input_file`:

```
struct ld_plugin_input_file {
    const char *name;   /* NUL-terminated path of the containing file */
    int         fd;     /* open descriptor for that file */
    off_t       offset; /* byte offset of the member within the file */
    off_t       filesize;
    void       *handle; /* linker-owned cookie; opaque to the plugin */
};
```

The symbol record, exchanged in both directions, in declaration order:
`char *name` (may carry an `@version` or `@@version` suffix); `char *version`
(NULL if unversioned); `char def` (table below); `char symbol_type`;
`char section_kind`; `char unused`; `int visibility` (an `STV_*` value);
`uint64_t size`; `char *comdat_key` (NULL outside a COMDAT group);
`int resolution` (the linker writes this in `get_symbols_v3`). The four
single-byte fields pack into one 4-byte word; with natural LP64 padding the
struct is 48 bytes.

`def` values:

| value | meaning | counts as a definition? |
| --- | --- | --- |
| 0 | strong definition | yes |
| 1 | weak definition | yes |
| 2 | undefined | no |
| 3 | weak undefined | no |
| 4 | common | yes |

Any other value MUST be treated as "kind unknown", which is not a definition.

## 5. Offering files: the claim hook

For **every** input file — objects, archives and archive members — call the
registered claim-file hook:

```
enum ld_plugin_status claim_file(const struct ld_plugin_input_file *file, int *claimed);
```

Construct the descriptor as follows.

- `name` is the path of the file **on disk**. For an archive member it is the
  path of the *archive*, not a synthesised `archive(member.o)` string.
- `fd` is an open descriptor for that on-disk file.
- `offset` is 0 for a plain file, and for an archive member it is the byte
  offset of the member's **data** inside the archive — the offset just past the
  60-byte `ar` member header, exactly the offset you would `pread` from.
- `filesize` is the length of the member's data (equivalently, the length of the
  byte slice you would otherwise parse as ELF), not the length of the archive.
- `handle` is a cookie you own. It MUST remain valid and stable for the whole
  link, because the plugin passes it back later in `get_symbols_v3`,
  `get_view`, `get_input_file` and `release_input_file`. Store at least: the
  data slice, the descriptor, the offset, the name, and a mutable slot for the
  file id you will assign when the file becomes an input.

`*claimed` is an out parameter. Initialise it to 0 before the call and treat any
value other than 1 as "not claimed".

After the hook returns:

- If the plugin reported an error through `LDPT_MESSAGE` at error or fatal
  level, the link MUST fail even when the hook returned OK.
- If the file was **not** claimed and it has archive semantics (an archive, or a
  member of one, without `--whole-archive`), that is normal: carry on and treat
  the member by the ordinary rules.
- If the file was **not** claimed and it is a plain input, the link MUST fail
  naming the input path — a non-ELF, non-archive, non-script file that no plugin
  wants is an unusable input.
- If the file **was** claimed, do not parse it as ELF. Its symbols are exactly
  what the plugin pushed via `add_symbols` during the hook (§6), and the file
  becomes an "LTO input" whose sections are empty.

Claim order MUST be deterministic. Do not call the claim hook concurrently from
several threads: real plugins are not thread-safe (the LLVM plugin as of
clang 20 is not), and even with a mutex a non-deterministic *claim order* makes
the plugin's output non-deterministic.

## 6. `add_symbols`

Callable only from inside the claim hook:

```
enum ld_plugin_status add_symbols(void *handle, int nsyms, const struct ld_plugin_symbol *syms);
```

Copy everything you need out of `syms` before returning. The plugin does not
keep the array, the name strings or the version strings alive past the claim
hook; retaining pointers into them is a use-after-free. Copy at least name,
version, `def`, visibility and size into linker-owned storage.

Tag 33 (`ADD_SYMBOLS_V2`) has the same signature and MUST be served by the same
function.

The symbols recorded here take part in ordinary symbol resolution as though
they came from a relocatable object:

- a symbol with `def` 0, 1 or 4 defines the name;
- `def` 2 and 3 are references, 3 being weak;
- `visibility` is an `STV_*` value and is applied normally — a hidden IR symbol
  does not become dynamic;
- if `version` is non-NULL it supplies the symbol's version, overriding any
  `@`-suffix parsed out of `name`;
- an LTO input reached only through archive semantics is *optional*: it joins
  the link only if it resolves an undefined symbol, exactly like a normal
  archive member, unless `--whole-archive` was in effect.

## 7. `get_symbols_v3`: reporting resolutions

```
enum ld_plugin_status get_symbols_v3(const void *handle, int nsyms, struct ld_plugin_symbol *syms);
```

Called by the plugin from inside the all-symbols-read hook, once per claimed
file. Recover your cookie from `handle`. If that file is an archive member you
decided **not** to load, return status 1 (no symbols) and write nothing. If
`nsyms` is 0, return OK. Otherwise fill in `syms[i].resolution` for each entry
and return OK. You MUST NOT modify any other field.

Resolution codes:

| value | name | meaning |
| --- | --- | --- |
| 0 | unknown | never written by the linker |
| 1 | undef | the name has no definition anywhere in the link |
| 2 | prevailing def | this IR definition wins **and** something outside IR needs it |
| 3 | prevailing def, IR-only | this IR definition wins and only IR refers to it |
| 4 | preempted by regular | a non-IR object's definition won |
| 5 | preempted by IR | another IR file's definition won |
| 6 | resolved from IR | this is a reference, satisfied by an IR definition |
| 7 | resolved from executable | reference satisfied by a regular object |
| 8 | resolved from dynamic | reference satisfied by a shared object |
| 9 | prevailing def, IR-only, exported | as 3, but the symbol must stay exported |

Code 3 is the licence the plugin needs to internalise or delete the symbol; code
2 and code 9 forbid that. Getting this wrong is how LTO silently drops symbols
that something outside the IR world depends on.

The decision procedure, given the symbol's name (and version, taken from the
`version` field when non-NULL, otherwise from an `@` suffix in the name):

```
if name is a --wrap target:
        return (symbol is undefined in this record) ? 7 : 4
look the name up in the symbol table and follow it to its definition
if not found:                     return 1
if the definition is undefined:   return 1
if this record is a reference (def is 2 or 3):
        defining file is an LTO input        -> 6
        defining file is a shared object     -> 8
        otherwise                            -> 7
# this record is a definition
if the winning definition belongs to this same file:
        symbol is flagged as having a non-IR reference -> 2
        else if the output is a shared object          -> 9
        else                                           -> 3
else:
        winning definition is in an LTO input -> 5
        otherwise                             -> 4
```

Two subtleties that are easy to get wrong:

- **`--wrap` is reported pre-wrap.** By the time this runs, the name table maps
  a wrapped name to `__wrap_<name>`. The plugin must be told the state *before*
  wrapping, so short-circuit on wrap targets as shown, before any lookup.
  (This short-circuit is relevant starting at step 9 / tag 0.10.0, when wrapped
  names can be defined in IR inputs; at step 8 / tag 0.9.0 the graded `--wrap`
  direction has the *reference* in bitcode and the *definition* in ordinary ELF.)
- **Code 9 versus 3 for shared-object output.** The reference does not compute
  per-symbol exportedness here; it reports 9 for every prevailing IR-only
  definition when linking a shared object. Match that: over-reporting 9 is safe
  (it only suppresses internalisation), under-reporting it is not.

`get_symbols_v3` MUST NOT panic across the FFI boundary. Catch panics, convert
them into the error status, and record a message.

## 8. Keeping `--wrap` and `--defsym` targets alive

Before invoking the all-symbols-read hook, mark as "referenced by non-IR code"
every symbol named by:

- each `--wrap NAME`: the names `NAME`, `__wrap_NAME` and `__real_NAME`, each
  only if it exists in the symbol table;
- each `--defsym SYM=TARGET` and each linker-script symbol redirect: the
  **target** name.

That flag is what makes §7 report code 2 instead of code 3, which is what stops
the LTO compiler dead-code-eliminating or internalising the definition. Without
it, a `--defsym`/script redirect ends up pointing at a symbol the compiler
deleted, and the link either fails late or produces a wrong address. This rule
MUST hold even when the name is referenced from nowhere else.

**Implementation note.** The reference implements this marking only at tag
`0.10.0` (step 9): `libwild/src/linker_plugins.rs` gains
`mark_wrap_symbols_as_non_ir_ref` and `libwild/src/resolution.rs` gains the
`HAS_NON_IR_REF` assignment in `load_symbols_in_redirect` at that tag, and the
transfer-vector contents are unchanged — only which symbols the marking keeps
alive. A `--defsym` target that is defined in ordinary ELF already survives
garbage collection without this flag, so the rule is observable only on the
direction where the definition is in an IR input. The interface contract
beside this document states what each milestone grades, and is the authority
where it and this section disagree.


## 9. `all_symbols_read`, `add_input_file`, and the second resolution pass

If no claimed LTO input actually made it into the link, **skip the hook
entirely**. Invoking the GCC plugin with nothing to compile makes it run the
compiler with no input file and fail.

Otherwise, with resolution complete:

1. Apply the §8 marking.
2. Call the registered all-symbols-read hook. During it the plugin will call
   `get_symbols_v3` for each claimed file and then, for each object it
   generates, `add_input_file(const char *path)` — and possibly
   `add_input_library(const char *name)`.
3. `add_input_file` MUST append the path to a list of pending inputs, marked
   *temporary* (the plugin owns those files and will delete them in cleanup, so
   the linker must not keep them mapped past the link or try to delete them
   itself). `add_input_library` appends an `-lNAME` style input, resolved
   through the library search path, marked `as-needed`. Neither callback
   performs any I/O at the time it is called.
4. After the hook returns, load every pending input exactly as if it had been
   named on the command line, and add its symbols to the symbol table.
5. Re-apply `--wrap` overrides, so wrapped names now bind to the definitions
   that came out of LTO rather than to the IR inputs they were parsed from.
6. Re-run symbol resolution and archive-member selection over the whole link.
7. **Disable the LTO inputs.** Symbols from claimed IR files MUST now be ignored
   — their definitions have been superseded by the generated objects. Failing to
   do this produces duplicate definitions for everything LTO emitted.
8. Re-resolve alternative (for example versioned/aliased) definitions.

Errors reported by the plugin through `LDPT_MESSAGE` at error or fatal level MUST
fail the link even if every status code was OK; conversely, if one of your own
callbacks returned an error *and* the plugin then reported a message, your own
error is the more informative one and should be the one reported.

## 10. Remaining callbacks

**`message(int level, const char *format, ...)`** is variadic, printf-style.
Register a C-ABI variadic trampoline; you cannot express this as a plain Rust
`extern "C" fn` with a fixed arity. Format the arguments with `vsnprintf` and
route the result to stderr. Levels: 0 info, 1 warning, 2 error, 3 fatal. Levels
2 and 3 MUST also record the message so the link can be failed at the next
check point.

**`get_view(const void *handle, const void **view)`** writes a pointer to the
start of the claimed file's bytes. For an archive member that is a pointer to
the member's data, not to the archive.

**`get_input_file(const void *handle, struct ld_plugin_input_file *file)`**
refills the descriptor from the cookie: descriptor, offset, size, name; it
allocates nothing. **`release_input_file(const void *handle)`** pairs with it and
is therefore a no-op returning OK — but it MUST be provided, because plugins
call it.

**`get_wrap_symbols(uint64_t *count, const char *const **names)`** hands back
the array of `--wrap` names as NUL-terminated C strings. Build that array once,
at plugin-load time, with a lifetime covering the link. Only the LLVM plugin
uses it.

**`get_api_version(const char *plugin_id, const char *plugin_version, int min, int max, const char **linker_id, const char **linker_version)`**
returns an `int`: the API version you implement, which is **1**. Record
`plugin_id` and `plugin_version` when both are non-NULL — they are useful in
diagnostics. The reference ignores `min` and `max` and does not write the two
linker out-parameters.

**Cleanup.** When the plugin is finished with — at the latest, as the linker
unloads it — call the registered cleanup hook exactly once, then `dlclose`. The
hook is what deletes the temporary objects `add_input_file` named, so it MUST
run even when the link is failing. A non-OK status from cleanup does not need to
change the link's outcome.
