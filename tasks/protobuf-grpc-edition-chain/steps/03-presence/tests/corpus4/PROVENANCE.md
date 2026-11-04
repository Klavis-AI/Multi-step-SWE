# Corpus provenance — step 03 (presence, options, FieldMask)

The author writes the inputs and the commands; protoc writes every expected
byte.

## Oracle
`tmp/protobuf-grpc-edition-chain/protoc/v32.1/bin/protoc` — `libprotoc 32.1`.

## Floor
`protoc/v3.11.4/bin/protoc` — `libprotoc 3.11.4`, the last release before
proto3 `optional` arrived in 3.12.

| stratum | files | 32.1 | 3.11.4 | floors? |
| --- | ---: | --- | --- | --- |
| `presence/` | 18 | accepts 18/18 | refuses 18/18 | **yes** |
| `options/` | 3 | accepts | accepts | no |
| `fieldmask/` | 2 | accepts | accepts | no |

The options and FieldMask rows were **measured with every `optional` removed**:
each of those files contains a proto3 `optional`, which alone makes 3.11.4
refuse them for the presence reason rather than their own. Custom options and
`FieldMask` both predate 3.12.

Consequence: floor-discriminating weight belongs on `presence/`. The other two
strata are correctness families and carry small weight, the same structure
step 2 arrived at.

## Imports
protoc resolves `google/protobuf/*.proto` from its linked-in copies with `-I.`
alone, and so does the reference (which embeds them). No WKT sources are
shipped in the corpus, matching `corpus/proto2` and `corpus3`. Expectations are
recorded with `-I.` only — the exact flags the scorer passes — so a recording
cannot depend on an include path the verifier will not supply.

## Recorded expectations (cwd = `corpus4/p3`)
| family | command |
| --- | --- |
| `expect/desc/<rel>.bin` | `protoc -I. --descriptor_set_out=<out> <rel>` |
| `expect/descfull/<rel>.bin` | `protoc -I. --include_imports --retain_options --descriptor_set_out=<out> <rel>` |

Both recorded twice into separate directories and compared byte for byte.

## Refusals (`refuse/`, 3 files)
| file | what it actually tests |
| --- | --- |
| `01_two_labels_parse_error.proto` | two labels on one field — rejected at **parse** level, not by a proto3 label rule |
| `02_optional_in_oneof.proto` | semantic: fields in a oneof must not carry labels |
| `03_optional_map.proto` | semantic: labels are not allowed on map fields |

The first is named for what it does. `optional repeated` and `repeated
optional` both produce "Missing field number." — no phrasing of that construct
reaches a proto3 rule, so filing it as a semantics test would let any parser
that dislikes two labels pass the row.
