# Editions 2023: resolving features, and acting on them

Your compiler already handles proto2, proto3 and the ProtoJSON mapping. This
step adds **editions**: a file declares `edition = "2023"` instead of a
`syntax`, and the behaviours that `syntax` used to imply are now individual
**features** that a file or a field can set.

The project remains your own implementation: it must not contain, link, vendor,
download or invoke a third-party Protocol Buffers implementation. The toolchain
available is GCC with the C standard library and zlib. Your binaries may link
only libc, libm, libz and libgcc_s. All compiled code in the project is C; no file anywhere under `/app` may be
C++ object code or carry the C++ runtime.

`make` at `/app` must still produce `/app/bin/protoc` and `/app/bin/conformance`.

## Where features come from

`SPEC/proto/google/protobuf/descriptor.proto` is normative. Its `FeatureSet`
message declares every feature, and each one carries two things that decide
what is legal and what a file inherits:

- `targets` — the entity types the feature may be set on. This is narrower than
  it looks: `field_presence`, `repeated_field_encoding`, `utf8_validation` and
  `message_encoding` target **FIELD and FILE only**, and `enum_type` targets
  **ENUM and FILE**. None of them may be set on a message. Setting one there is
  an error, not an inheritance level.
- `edition_defaults` — the value in force, given as entries for particular
  editions. The one that applies is the entry for the **greatest edition less
  than or equal to** this file's, not an exact match. So an edition 2023 file
  inherits its `repeated_field_encoding` from the `EDITION_PROTO3` entry when
  no `EDITION_2023` entry exists.

So resolution is two levels, not three: file then field, or file then enum.

protobuf.dev's edition 2023 language specification and its editions overview and
feature guide are in the image as background; `descriptor.proto` stays normative
(local copies: `/opt/docs/protobuf/edition-2023-spec.md`,
`/opt/docs/protobuf/overview.md`, `/opt/docs/protobuf/features.md`).

`field_presence` also takes `LEGACY_REQUIRED`, which brings required-field
semantics back to a field that is not declared `required`. It resolves and is
carried in the descriptor like any other feature, and at this step it is the
descriptor that must carry it correctly.

## Resolving is not enough

A feature that is resolved correctly into the descriptor and then ignored when
writing bytes is the failure to avoid here. For the two wire
behaviours below, `--descriptor_set_out` will look perfect either way, because
the descriptor simply carries the feature you were given — so a correct
descriptor says nothing about those two. The descriptor must still be correct
in its own right, `LEGACY_REQUIRED` included; both the descriptor and the
written bytes must be right.

What must actually change:

- `repeated_field_encoding = PACKED` writes a repeated numeric field as **one**
  length-delimited field holding the concatenated values; `EXPANDED` writes one
  tagged entry per value. Edition 2023 inherits PACKED where proto2 had
  EXPANDED.
- `message_encoding = DELIMITED` writes a submessage with group start/end tags
  rather than a length prefix — the same wire shape proto2 groups use, reached
  by feature rather than by field type.

**Text format for a DELIMITED field.** `SPEC/textformat-spec.md` describes the
*group* spelling — a capitalized type name instead of the field name — and that
rule applies only when the field is **group-like**: the message type is declared
in the same scope and its name is the field name capitalized. A `DELIMITED`
field whose type does not meet that test is printed and parsed by its ordinary
**field name**, exactly like any message field. Both shapes must be handled;
match protoc 32.1.

## What correct means

- Encoded bytes for editions files must be identical, byte for byte, to the
  official protoc 32.1 release's.
- A file that sets a feature on an entity its `targets` exclude must be
  refused: exit non-zero, without dying on a signal, with a diagnostic on
  stderr that names the file.
- Descriptor output, with and without `--include_imports --retain_options`,
  must match protoc 32.1 byte for byte, and every earlier behaviour must keep
  working: improving this step must not break an earlier one.

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
