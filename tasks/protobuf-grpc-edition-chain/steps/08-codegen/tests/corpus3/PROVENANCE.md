# Corpus provenance — step 02 (proto3 + ProtoJSON)

The author writes the inputs and the commands; protoc writes
every expected byte.

## Oracle
`tmp/protobuf-grpc-edition-chain/protoc/v32.1/bin/protoc` — `libprotoc 32.1`,
the official release binary. Same pin as step 1.

## Inputs (authored here)
| set | count | what |
| --- | --- | --- |
| `proto3/` | 40 | twelve strata: scalars, presence, packed, enums, maps, oneof, nested, json, services, reserved, imports, options |
| `refuse/` | 18 | constructs legal in proto2 and **illegal** in proto3 — `required`, explicit `default`, `group`, extension ranges, non-zero first enum value, plus proto3-era validation (duplicate default JSON names, map-in-oneof, float map key) |

Every `proto3/` file compiles under v32.1 with exit 0; every `refuse/` file
exits non-zero. Both verified.

## Recorded expectations
| family | command (cwd = `corpus3/proto3`) |
| --- | --- |
| `expect/desc/<rel>.bin` | `protoc -I. --descriptor_set_out=<out> <rel>` |
| `expect/descfull/<rel>.bin` | `protoc -I. --include_imports --retain_options --descriptor_set_out=<out> <rel>` |
| `expect/codec/*.enc.bin` | `protoc -I. --encode=<TYPE> <rel>` with the authored `.in.txt` on stdin |
| `expect/codec/*.dec.txt` | `protoc -I. --decode=<TYPE> <rel>` with the recorded `.enc.bin` on stdin |
| `expect/codec/*.raw.txt` | `protoc --decode_raw` with the recorded `.enc.bin` on stdin |

Codec inputs are derived from each file's **own recorded descriptor** — the
field list is read back out of the descriptor and turned into a text-format
body — so no expectation is hand-typed.

## Why this corpus is weighted the way it is
A floor check showed the step-1 reference already scores 40/40 on
proto3 descriptor parity and 17/18 on these refusals. Descriptor parity is
therefore kept at 0.08 as regression only, and the weight sits on the
conformance suite and ProtoJSON, which the floor scores zero on.
