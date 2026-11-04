# Explicit presence, and the dynamic well-known types

Your compiler and conformance program from the previous step handle proto3 and
the core of ProtoJSON. This step adds two things that arrived later and that
most implementations get wrong.

The project remains your own implementation: it must not contain, link, vendor,
download or invoke a third-party Protocol Buffers implementation. The toolchain
available is GCC with the C standard library and zlib. Your binaries may link
only libc, libm, libz and libgcc_s. All compiled code in the project is C; no file anywhere under `/app` may be
C++ object code or carry the C++ runtime.

`make` at `/app` must still produce `/app/bin/protoc` and
`/app/bin/conformance`.

## Explicit presence in proto3

A proto3 field may be declared `optional`. That is not a cosmetic change: the
field gains presence, and the descriptor gains a **synthetic oneof** containing
only that field, named `_<field>` and marked `proto3_optional`.

The ordering rule is the part that is easy to get wrong: synthetic oneofs are
emitted **after every real oneof**, regardless of where the `optional` field
appears in the source. A file declaring `optional a`, then a real `oneof`, then
`optional b` produces the real oneof at index 0 and the two synthetic ones at
1 and 2. An implementation that assigns oneof indices in declaration order
produces different descriptor bytes.

`optional` is still not permitted on a repeated field, inside a oneof, or on a
map field.
protobuf.dev's application note on field presence is in the image as background
(local copy: `/opt/docs/protobuf/field_presence.md`).

## The dynamic well-known types

`SPEC/json-mapping.md` is normative. This step is about the types whose JSON
form is not an object of their fields:

- `Value` is any JSON value — null, number, string, bool, object or array —
  and its `null_value` member must be **present** when the input was `null`,
  not omitted as a default.
- `Struct` is a JSON object; `ListValue` is a JSON array.
- `Any` is `{"@type": "<url>", ...}`. An ordinary message inlines its fields
  beside `@type`. A well-known type with its own JSON form does **not**: it
  appears under a `value` member instead — and `Any` itself is such a type, so
  an `Any` nested in an `Any` uses the `value` form.
- `@type` may appear **after** the payload's members.

## Strict parsing

Input that is not valid must be refused rather than repaired: duplicate JSON
members, two members of the same oneof, `null` as an array element or map
value, an escape whose `\u` digits are not hex, an unpaired surrogate, and a
`Timestamp` or `Duration` outside its documented range.

When your compiler refuses a file, how it refuses matters, not only that it
refuses. A refusal must: exit **non-zero**; exit normally rather than dying on a
signal (a crash is not a refusal); write a diagnostic to **stderr**; and name the
offending file in that diagnostic, by its base name. A silent non-zero exit, or a
crash, is not a refusal.

ProtoJSON can also refuse to **write**: a `FieldMask` path that does not
round-trip through camelCase, and a `Value` holding infinity or NaN.

There is one exception to strictness, and it is signalled by the request:
when the conformance suite marks a case as an ignore-unknown parsing test, an
unrecognised member and an unrecognised enum value name are skipped rather
than refused.

## What correct means

The conformance program must answer the upstream conformance suite correctly
over the behaviours above. Descriptors for presence files must match protoc
32.1 byte for byte, refusals must behave as described above, and the previous
step's conformance behaviour must keep working: improving this step must not
break the last one.

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
