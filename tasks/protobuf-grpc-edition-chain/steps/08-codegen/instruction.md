# Code generation: a `protoc` plugin, and the compiler that drives it

Your compiler handles proto2, proto3, ProtoJSON, editions 2023 feature
resolution, edition 2024 naming and visibility, text format across every era,
and gRPC over HTTP/2. This step is about the part of `protoc` that has no
equivalent in any earlier step: **generating code**.

The project remains your own implementation: it must not contain, link, vendor,
download or invoke a third-party Protocol Buffers implementation. The toolchain
available is GCC with the C standard library and zlib. Your binaries may link
only libc, libm, libz and libgcc_s. All compiled code in the project is C; no file anywhere under `/app` may be
C++ object code or carry the C++ runtime.

`make` at `/app` must now produce `/app/bin/protoc`, `/app/bin/conformance` and
`/app/bin/protoc-gen-py`. The plugin is held to the same rule as every other artifact: it must
be an ELF executable this build produced, not a script and not something the
tree shipped.

## What changes

Code generation is split across two programs that talk over a pipe, and **both
sides are yours**. There is no other `protoc` here to drive your plugin, and
`/app` is not on `PATH` when your programs are run, so the plugin is reachable only through
`--plugin=NAME=PATH`.

**The compiler side.** `--plugin=protoc-gen-NAME=PATH` registers a generator and
`--NAME_out=[OPTS:]OUT_DIR` invokes it. `protoc` builds a `CodeGeneratorRequest`,
writes it to the plugin's stdin, reads a `CodeGeneratorResponse` from its stdout,
and writes out the files the response names. The schema is
`SPEC/proto/google/protobuf/compiler/plugin.proto`; the flags are in
`SPEC/protoc-help.txt`.

Four properties of the request must be right, and each is the kind of thing that
looks right until it is compared:

- `file_to_generate` names exactly the files given on the command line, while
  `proto_file` carries the **whole import closure, dependencies before the files
  that import them**.
- `source_file_descriptors` carries **only the files to generate** — it is not a
  copy of `proto_file`. A single-file invocation cannot tell the two apart.
- `parameter` is sent only when `OPTS:` was given, and splits at the **first**
  colon: `--NAME_out=a,b:DIR` sends `a,b`, not `a,b:DIR` and not `DIR`.
- `compiler_version` is populated.

A plugin that answers with the `error` field set must make `protoc` fail with
that message; a plugin that cannot be found or that exits non-zero must also
fail, naming what went wrong.

**The generator side.** `protoc-gen-py` emits Python that is byte-identical to
what `protoc --python_out` produces. It is run as
`--plugin=protoc-gen-py=/app/bin/protoc-gen-py --py_out=DIR`, and plugins may
also be registered under other names, so `--NAME_out` must work for
an arbitrary NAME rather than for `py` alone. Its output must match byte for byte, so every
detail counts: the embedded descriptor blob is the `FileDescriptorProto` with
`json_name` **suppressed**; `_serialized_start`/`_serialized_end` are offsets of
each entity's content within that blob; and the order in which descriptors and
options are listed is its own specification, described in `SPEC/python-codegen.md`.

## What must keep working

When a generator cannot be run — no such plugin, or the plugin failed — the
diagnostic names the program protoc tried to execute, in its `protoc-gen-NAME`
form, not merely the `--NAME_out` flag that was typed. A plugin that fails with
an error response is reported with **that plugin's own message**.

Two details of writing the response out, which the plugin protocol implies but
does not spell out:

- A plugin that succeeds with nothing to say answers with an **empty**
  `CodeGeneratorResponse` — zero bytes on stdout. That is success, not a
  malformed reply.
- A response may name a file inside a directory that does not exist yet
  (`sub/thing_pb2.py`). Create the directories you need before writing.

Everything earlier. The encode, decode and `--decode_raw` behaviour
of every proto2, proto3 and editions file you already handle must keep working.

## Meta requirements

- You have **7,200 seconds** for this step. Run `bash /app/timer.sh` to see the
  remaining budget. The configured timeout is authoritative.
- Keep your source under `/app`; it is the deliverable.
- The project must build from source in a clean checkout with none of your
  build products present, in a fresh container with the toolchain in this
  image, and its programs must work through the command lines above.
  That build is plain `make` at `/app`, no arguments, under a **900-second**
  limit, and every program it must produce has to be an ELF executable that
  build wrote — not a script, and not a binary the tree shipped.
- Do not cheat by using online solutions or hints specific to this task.
- Work only from this workspace and the documentation named above; do not
  search the system for other implementations or answers.
- Check your own progress by running your programs and comparing against what
  the specifications above require.
