# Proto3 and ProtoJSON, with a conformance test program

Your compiler from the previous step handles proto2. Extend it to **proto3**,
add the **ProtoJSON** mapping, and add a second program that the upstream
Protocol Buffers conformance suite can drive.

The project remains your own implementation: it must not contain, link, vendor,
download or invoke a third-party Protocol Buffers implementation. The toolchain
available is GCC with the C standard library and zlib; there is no protobuf
library in the environment. Your binaries may link only libc, libm, libz and
libgcc_s. All compiled code in the project is C; no file anywhere under `/app` may be
C++ object code or carry the C++ runtime.

## What is built

`make` at `/app` must produce two executables:

| path | what it does |
| --- | --- |
| `/app/bin/protoc` | the compiler, as before, now also accepting proto3 files |
| `/app/bin/conformance` | answers conformance requests on stdin/stdout |

## The language

proto3 differs from proto2 in ways that are mostly **removals**, and the
removals are enforced: no `required`, no explicit field defaults, no groups, no
extension ranges on ordinary messages, and the first enum value must be zero.
Field presence works differently: singular scalars have no presence unless
declared `optional`, and absent fields are omitted from JSON output rather than
written with a default. Repeated numeric fields are packed by default, where
proto2 leaves them unpacked.

`SPEC/proto3-spec.md` is the language reference.

## ProtoJSON

`SPEC/json-mapping.md` is the normative mapping between the binary form and
JSON. The parts that are easy to miss:

- 64-bit integers are **strings** in JSON; 32-bit ones are numbers.
- A quoted integer is parsed exactly. An unquoted one is a JSON number and
  therefore arrives through a double, which is a different rule at the extremes.
- A field may be written under its `json_name`, its lowerCamelCase default, or
  its original `.proto` name, and all three must be accepted.
- The well-known types do **not** use the ordinary object form: the wrappers
  are their bare value, `Timestamp` is an RFC 3339 string, `Duration` is a
  decimal count of seconds ending in `s`, `FieldMask` is a comma-separated list
  of camelCase paths, and `Struct`/`Value`/`ListValue` map onto plain JSON.
- Invalid input must be **rejected**, not repaired. Out-of-range numbers,
  fractional values in integer fields, empty strings where a number belongs and
  duplicate fields are all errors.

## The conformance program

`SPEC/conformance-protocol.md` describes the framing and the message exchange;
`SPEC/proto/conformance.proto` is the authority for the messages themselves.
The suite runs your program as a child process and sends one request at a time
until it closes stdin.

One distinction decides a large number of cases: answering `parse_error`
because you correctly refused malformed input is a **pass**, while
`runtime_error` is a failure. They are not interchangeable, and neither is
crashing.

## What correct means

- `/app/bin/conformance` must answer the upstream conformance suite correctly
  over **binary** input, and over **JSON** input.
- `--encode`, `--decode` and `--decode_raw` output for proto3 files must be
  identical, byte for byte, to the official protoc 32.1 release's.
- Files that are legal proto2 and illegal proto3 must be **rejected** with a
  diagnostic naming the offending file, and a non-zero exit.
- `--descriptor_set_out` output, with and without `--include_imports
  --retain_options`, must also be identical byte for byte.

Rejection also covers validation that is not about proto2 at all, and these
three are easy to miss: a field number must be **at least 1**; **two fields in one
message may not share a field number**; and **two fields may not resolve to the
same JSON name**, whether written with `json_name` or derived from the field
name (`a_b` and `aB` collide).

Descriptor and codec parity are the previous step's behaviour, and they must
keep holding: improving conformance must not come at the cost of it.

## Checking your own work

Nothing in this directory records an expected output. Compile the
files under `SPEC/proto/`, run your two programs against each other, and use
the spec documents as the authority. `SPEC/protoc-help.txt` is the CLI surface
you are reproducing.

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
