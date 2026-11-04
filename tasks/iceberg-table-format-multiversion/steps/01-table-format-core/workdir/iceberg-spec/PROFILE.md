# Benchmark compatibility profile

Task-owned resolutions where the Apache Iceberg specification leaves behaviour
open, or where this benchmark deliberately narrows it. The specification
documents beside this file are normative and unmodified; this file is the only
place the benchmark adds to them, and it never contradicts them.

## P1 — Format versions in scope

Table format versions **1 and 2** are in scope across the whole task. A
`format-version` outside `{1, 2}` must be rejected when parsing table
metadata. Format version 3 features (deletion vectors excepted, and only where
a milestone's `TEST_INTERFACE.md` section 4 names them) are out of scope; you
are not required to accept a v3 table.

## P2 — Catalog scope

The only catalogs in scope are the ones a milestone's `TEST_INTERFACE.md`
section 4 names. Hive, Glue, REST, JDBC/SQL and any other external catalog are
out of scope; nothing in this task requires a metastore, a network service or
a cloud account.

## P3 — Storage scope

The only storage scheme that must work is the **local filesystem**, reached
through `FileIO`. `file://` URIs and plain absolute paths must both be
accepted. S3, GCS, Azure and OSS backends are out of scope and are never
exercised.

## P4 — Data file formats

Iceberg's `data_file.file_format` enumerates `avro`, `orc` and `parquet`, and a
milestone whose `TEST_INTERFACE.md` section 4 says so adds `puffin`. This task
requires the **enum, its textual form and its round trip through manifests**;
it does not require you to read or write Parquet or ORC data files. Manifests and manifest lists are Avro object container files and *are*
read and written.

## P5 — Undefined ordering

Where the specification does not fix an order, none is required and none is
asserted: the order of entries in `schemas`, `partition-specs`, `sort-orders`
and `properties`, and the order of JSON object members anywhere. Where the
specification *does* fix an order — partition fields within a spec, sort
fields within an order, fields within a struct, entries within a manifest as
written — that order is required.

## P6 — Timestamps

`last-updated-ms` and `timestamp-ms` values you generate are never compared to
a wall clock. Where a document supplies a timestamp, it must survive a round
trip unchanged; where you generate one, only its ordering relative to other
generated timestamps is ever observed.

## P7 — Error reporting

Failures are reported through the crate's `Result`. The benchmark asserts only
that an operation fails, never which error variant, kind, message or source
chain it produces. `Error` and `ErrorKind` must exist and be public because
`Result<T>` names them; their contents are yours.

## P8 — Avro codec

Manifests and manifest lists are written **uncompressed** (`avro.codec` of
`null`) or with `deflate`; the benchmark checks the Avro object-container magic
and round-trips files through your own reader, and where a milestone's
`TEST_INTERFACE.md` section 4 says so it also decodes the Avro header, which is
uncompressed under either codec. No particular codec is required.

## P9 — Decimal type name spelling

Appendix C admits both `"decimal(9,2)"` and `"decimal(9, 2)"`. You must
**accept both** when deserialising; you may emit either, and your own output
must round-trip through your own parser.

## P10 — Reserved field IDs

Where the specification assigns a reserved field id — the position-delete
columns `file_path` (`2147483546`) and `pos` (`2147483545`), and the manifest
and manifest-list column ids in "Manifests" and "Manifest Lists" — those exact
ids are required wherever a milestone's `TEST_INTERFACE.md` section 4 puts those
columns in scope and its section 8 does not exclude them.
