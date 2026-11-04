# A `protoc`-compatible compiler for proto2

## 1. Implement this milestone

Build a Protocol Buffers compiler, in C, that reproduces the behaviour of the
official **protoc 32.1** release for the **proto2** language.

`make`, run with `/app` as the working directory and no arguments, must produce
an executable at **`/app/bin/protoc`**. Every program this project provides is
built to `/app/bin/<name>`, named as its own published interface names it.

The project must be your own implementation: it must not contain, link, vendor,
download or invoke a third-party Protocol Buffers implementation. The toolchain
available is GCC with the C standard library and zlib; there is no protobuf
library in the environment. All compiled code in the project is C; no file anywhere under `/app` may be
C++ object code or carry the C++ runtime.

### The command line

Implement the following, exactly as `/app/SPEC/protoc-help.txt` documents them
(that file is the verbatim `protoc --help` output of the 32.1 release):

- `-I<dir>` / `--proto_path=<dir>`, repeatable, searched in order
- `-o<file>` / `--descriptor_set_out=<file>` — write a `FileDescriptorSet`
- `--include_imports` — include every transitive dependency in that set
- `--retain_options` — keep options that would otherwise be stripped
- `--encode=<MESSAGE_TYPE>` — read text format on stdin, write binary on stdout
- `--decode=<MESSAGE_TYPE>` — read binary on stdin, write text format on stdout
- `--decode_raw` — read binary on stdin, write raw tag/value text; takes no files
- `--print_free_field_numbers` — report each message's unused field numbers
- `--dependency_out=<file>` — write a make-format dependency rule
- `@<argfile>` — expand one argument per line, in place
- `--version` — print a version line and exit 0

Imports of `google/protobuf/*.proto` must resolve without any `-I` pointing at a
protobuf distribution: protoc carries those sources inside itself. The copies
this milestone must agree with are all in `/app/SPEC/proto/` — embed them.

### The language

Full proto2, as `/app/SPEC/proto2-spec.md` defines it: `syntax = "proto2"` and
an absent `syntax` statement; packages and C++-style name scoping; nested
messages and enums; `required` / `optional` / `repeated`; `[packed=true]`;
every scalar type and its `default`; `group`; `oneof`; `map<K,V>`; `enum` with
`allow_alias` and negative values; `extend`, `extensions <ranges>` including
`to max`, and custom options (extensions of `google.protobuf.*Options`)
including the aggregate `{ … }` form; `reserved` numbers, ranges and names;
`import`, `import public`, `import weak`; services with `rpc`, `stream` on
either side, and method option blocks; comments and the full string escape set.

Wire format is `/app/SPEC/encoding.md`; text format is
`/app/SPEC/textformat-spec.md`.

### What correct means

- For a file that compiles, the bytes written by `--descriptor_set_out` —
  alone, and with `--include_imports --retain_options` — must be **identical**
  to those protoc 32.1 writes for the same command line. protoc 32.1 leaves
  `FileDescriptorProto.syntax` (field 12) unset for a proto2 file, whether
  the file says `syntax = "proto2"` or has no `syntax` statement.
- `--encode`, `--decode` and `--decode_raw` output must be identical to
  protoc 32.1's for the same input.
- `--print_free_field_numbers`, `--dependency_out` and `@argfile` must produce
  what protoc 32.1 produces.
- A file the language does not permit must be **rejected**: the process must
  exit non-zero — not crash — and print a diagnostic on stderr that names the
  offending file. The wording of diagnostics is yours; only the non-zero exit,
  the absence of a crash, and the file name are required.

## 2. Read these local references

Normative, under `/app/SPEC/`:

- `proto2-spec.md` — the proto2 language specification
- `encoding.md` — the wire format
- `textformat-spec.md` — the text format specification
- `protoc-help.txt` — the protoc 32.1 command-line reference, verbatim
- `proto/google/protobuf/descriptor.proto` — the descriptor schema your output
  is an instance of, and the copy your compiler must agree with

## 3. Meta requirements

- You have **7,200 seconds** for this step. Run `bash /app/timer.sh` to see the
  remaining budget. The configured timeout is authoritative.
- Keep your source under `/app`; it is the deliverable.
- The project must build from source in a clean checkout with none of your
  build products present, in a fresh container with the toolchain in this
  image, and its programs must work through the command line above.
  That build is plain `make` at `/app`, no arguments, under a **900-second**
  limit, and `/app/bin/protoc` must be an ELF executable that build wrote — not
  a script, and not a binary the tree shipped.
- Do not cheat by using online solutions or hints specific to this task.
- Work only from this workspace and the documentation named above; do not
  search the system for other implementations or answers.
- Check your own progress by running your compiler and comparing against what
  the specifications above require.
