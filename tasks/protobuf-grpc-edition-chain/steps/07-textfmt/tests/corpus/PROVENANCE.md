# Corpus provenance — step 01 (proto2)

The author writes the adapter and the inputs; the author never
writes the answers. Everything under `expect/` below was produced by the
official protoc release binary at authoring time. No expected descriptor,
encoded message or text rendering in this corpus was hand-written.

## Oracle

| | |
| --- | --- |
| binary | `tmp/protobuf-grpc-edition-chain/protoc/v32.1/bin/protoc` |
| version | `libprotoc 32.1` (official `protoc-32.1-linux-x86_64.zip`) |
| bundled includes | that release's own `include/google/protobuf/*.proto` |

protoc 32.1 is the pin because it is the first major with **edition 2024**
support (measured: v31.1 rejects `edition = "2024"`, v32.1 accepts).

## Inputs (authored here)

| set | count | what |
| --- | --- | --- |
| `proto2/` | 130 `.proto` files | twelve strata: scalars 15, extensions 15, imports 14, options 13, labels 12, groups 10, enums 10, nested 9, services 8, reserved 8, oneof 8, maps 8 |
| `reject/` | 67 `.proto` files | inputs protoc refuses; `reject/MANIFEST.tsv` records the observed exit code for each (all 67 exit 1) |

`proto2/MANIFEST.tsv` maps every file to its stratum and a one-line
description. Every one of the 130 compiles under v32.1 with exit 0.

## Recorded expectations

| family | count | exact command (cwd = `corpus/proto2`) |
| --- | --- | --- |
| `expect/desc/<rel>.bin` | 130 | `protoc -I. --descriptor_set_out=<out> <rel>` |
| `expect/descfull/<rel>.bin` | 130 | `protoc -I. --include_imports --include_source_info --retain_options --descriptor_set_out=<out> <rel>` |
| `expect/codec/*.enc.bin` | 48 | `protoc -I. --encode=<TYPE> <rel>` with the authored `.in.txt` on stdin |
| `expect/codec/*.dec.txt` | 48 | `protoc -I. --decode=<TYPE> <rel>` with the recorded `.enc.bin` on stdin |
| `expect/codec/*.raw.txt` | 48 | `protoc --decode_raw` with the recorded `.enc.bin` on stdin |

`expect/codec/MANIFEST.tsv` has one row per scored case:
`kind<TAB>relative_proto_path<TAB>message_type<TAB>stdin_file<TAB>expect_file`,
144 rows for 48 targets × 3 directions.

### How the codec inputs are derived

`tools/record_codec.py` reads each file's **own recorded descriptor**, renders
it to text with protoc, and derives a text-format input from the field list:
type-appropriate constants for scalars, the first declared value for an enum,
two elements for every repeated field (so packed and expanded encodings are
both exercised), and submessages to a bounded depth. That authored text is the
*input*. protoc then produces all three expected outputs from it. Targets are
taken round-robin across strata rather than in file order, so the 48 spread
over all twelve strata instead of clustering.

## Determinism

Byte-comparison is only a legitimate check if the oracle is deterministic, so
that is measured rather than assumed: five consecutive `--descriptor_set_out` runs over the same input yield
one distinct checksum, and five `--decode_raw` runs likewise.
