# Edition 2024: naming style and symbol visibility

Your compiler handles proto2, proto3, ProtoJSON and editions 2023 feature
resolution. This step moves to **edition 2024**, which adds two rules that
change what a valid file is and what its descriptor contains.

The project remains your own implementation: it must not contain, link, vendor,
download or invoke a third-party Protocol Buffers implementation. The toolchain
available is GCC with the C standard library and zlib. Your binaries may link
only libc, libm, libz and libgcc_s. All compiled code in the project is C; no file anywhere under `/app` may be
C++ object code or carry the C++ runtime.

`make` at `/app` must still produce `/app/bin/protoc` and `/app/bin/conformance`.

## Naming style is enforced

Edition 2024 defaults `features.enforce_naming_style` to `STYLE2024`, and a
file whose names violate it is **rejected**. Edition 2023 accepts the same
names, so this is a behaviour that changes with the edition, not a lint you may
skip.

The rules, as protoc applies them:

| entity | required shape |
| --- | --- |
| message, enum, service, method | TitleCase |
| field, oneof | lower_snake_case |
| enum value | UPPER_SNAKE_CASE |

Diagnostics must name the offending file and exit non-zero.

## Symbols have visibility

Edition 2024 adds the `local` and `export` keywords before `message` and
`enum`, and `SymbolVisibility` is recorded in the descriptor:
`DescriptorProto.visibility` is field 11, `EnumDescriptorProto.visibility` is
field 6, and the values are `VISIBILITY_UNSET = 0`, `VISIBILITY_LOCAL = 1`,
`VISIBILITY_EXPORT = 2`. See `SPEC/proto/google/protobuf/descriptor.proto`,
which is normative. protobuf.dev's edition 2024 language specification, which gives
the `export`/`local` grammar, is in the image (local copy:
`/opt/docs/protobuf/edition-2024-spec.md`).

Two details decide whether your bytes match:

- The field is emitted **only** when a keyword was written. An unmarked symbol
  carries no `visibility` field at all, rather than an explicit UNSET.
- It is serialised in ascending field-number order like everything else, so it
  follows `reserved_name` (10 on messages, 5 on enums) rather than sitting near
  the name.

`features.default_symbol_visibility` changes what unmarked symbols *mean*, but
it does not by itself change the bytes of a file that uses no keywords.

## What correct means

Descriptors for files using the visibility keywords must match protoc 32.1 byte
for byte, and naming-style violations must be rejected. Descriptor parity must
also hold for every file you already handle, and the previous step's
conformance behaviour must keep working: improving this step must not break an
earlier one.

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
