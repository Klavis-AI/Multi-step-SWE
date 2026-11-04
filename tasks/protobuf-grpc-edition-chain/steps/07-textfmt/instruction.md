# Mixed-era text format: extensions and `Any`

Your compiler handles proto2, proto3, ProtoJSON, editions 2023 feature
resolution and edition 2024 naming and visibility. This step is about the
**text format** — the syntax `protoc --encode` reads and `protoc --decode`
writes — across every era you already support.

The project remains your own implementation: it must not contain, link, vendor,
download or invoke a third-party Protocol Buffers implementation. The toolchain
available is GCC with the C standard library and zlib. Your binaries may link
only libc, libm, libz and libgcc_s. All compiled code in the project is C; no file anywhere under `/app` may be
C++ object code or carry the C++ runtime.

`make` at `/app` must still produce `/app/bin/protoc` and `/app/bin/conformance`.
(`bin/conformance` remains part of the build contract.)

## What changes

Two text-format behaviours matter here, and getting either one wrong is
invisible if you only ask whether the command succeeded.

**Bracketed names.** An extension field is written under its fully qualified
name in brackets — `[pkg.e_int]: 42` — and a message-valued extension takes the
brace form `[pkg.e_msg] { … }`. The same bracket syntax carries an `Any`
expansion, `[type.googleapis.com/pkg.Inner] { … }`, which `--encode` must
resolve to a type, encode, and store as `type_url` plus `value`.

**Field-number order.** Text output is emitted in field-number order, never
declaration order, and extensions interleave with declared fields by number.

`--encode` writes the wire bytes in ascending field number too, extensions
interleaved with declared fields by number, regardless of the order the text
input lists them in. `SPEC/encoding.md` says the wire format guarantees no
particular order in general — that is true of the format, but this task requires
byte-identity with protoc 32.1, which emits ascending field number. Match protoc.

A compiler that drops extensions from text output still exits
zero and still produces well-formed text. Only byte comparison against protoc
detects it, and byte-identity with protoc is what is required.

## What must keep working

Everything earlier. The encode, decode and `--decode_raw` behaviour of every
proto2, proto3 and editions file you already handle must keep working.

`SPEC/text-format.md` states the forms precisely.

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
