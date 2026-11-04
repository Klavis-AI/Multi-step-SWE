# corpus8 provenance

Every expectation in `expect/` and every request fixture in `req/` was recorded
from the **official protoc 32.1 release binary**, never from the reference
implementation in this repository. Recorded 2026-09-14.

## Identifying the recorder

Three binaries on the authoring host answer `--version` with `libprotoc 32.1`,
because the reference deliberately mimics the string. They are told apart by
their symbol tables, not their version output:

```
.pb/bin/protoc     10261736 B   google::protobuf symbols = 9995   <- upstream, used here
impl/bin/protoc       288160 B   google::protobuf symbols =    0   <- the reference
```

## expect/*_pb2.py — generated Python

```sh
protoc -I. -Iinclude --python_out=OUT <file>.proto
```

`include/` is the well-known-type directory shipped in the protoc release. It is
required for `c13_wkt.proto`: upstream ships `google/protobuf/*.proto` as files
on the import path, whereas the reference carries those descriptors internally.
A recording command that works against the reference is therefore NOT
automatically the one that works against upstream.

## req/*.req.bin — CodeGeneratorRequest fixtures

Captured by a plugin that writes its stdin to a file and answers with an empty
response, then **stripped of `source_code_info`** (FileDescriptorProto field 9)
in both `proto_file` (15) and `source_file_descriptors` (17). See `record.sh`.

The strip is part of the contract. `SPEC/python-codegen.md` does not make
`source_code_info` a candidate's problem, and `protoc-gen-reqparam` states that
omitting it "has not got the compiler side wrong". A fixture carrying field 9 would demand handling for
something the task leaves optional.

Sizes after the strip, and the generated content each produces:

```
c01_p2_basic     347 B -> 1458      c09_dep          172 B -> 1379
c02_p2_group     289 B -> 1508      c10_import_flat  349 B -> 1424
c03_p3_scalars   643 B -> 1794      c12_import_dir   303 B -> 1399
c04_p3_optional  324 B -> 1432      c14_options      272 B -> 2083
c05_maps         541 B -> 2135      c15_ext          400 B -> 1556
c06_oneof_enum   491 B -> 1946      c16_ed2023       231 B -> 1714
c07_nested3      332 B -> 1821
c08_service      416 B -> 1707      14 fixtures, all verified byte-exact
```

`c13_wkt` has no request fixture: capturing one needs the upstream include path,
and the resulting request would carry WKT descriptors the reference resolves
differently. Its `_pb2.py` expectation is recorded normally and is scored by the
`codegen` family.
