# Apache Iceberg specification corpus

Verbatim copies of the normative Apache Iceberg format documents, pinned at
one edition. **These files contain no implementation** — they are the
specification text and nothing else. They are read-only inputs: never edit,
trim, annotate or extend them.

| Edition | `apache/iceberg` tag `apache-iceberg-1.9.1`, commit `f40208ae6fb2f33e578c2637d3dea1db18739f31` |
| --- | --- |
| Source path | `format/` in that repository |
| Licence | Apache-2.0 — see `LICENSE` and `NOTICE` in this directory |
| Integrity | `SHA256SUMS` in this directory covers every document |

## The documents

| File | What it defines |
| --- | --- |
| `spec.md` | **The table specification.** Format versions, the type system, schemas and field IDs, partitioning and transforms, sorting, manifests, snapshots, manifest lists, scan planning, snapshot references and retention, table metadata, commit conflict resolution, delete formats, and Appendices A–G. |
| `view-spec.md` | The view metadata specification: view versions, representations, and view metadata JSON. |
| `puffin-spec.md` | The Puffin blob file format: magic, footer, blob metadata, compression codecs. |
| `gcm-stream-spec.md` | The AES-GCM stream format used for encrypted files. Background only. |

## Navigating `spec.md`

It is one long document; these are the sections most milestones need.

| Looking for | Section |
| --- | --- |
| What each format version adds and requires | "Format Versioning", "Appendix E: Format version changes" |
| Primitive and nested types, promotion rules | "Schemas and Data Types", "Primitive Types", "Schema Evolution" |
| Field IDs, identifier fields, reserved ids | "Identifier Field IDs", "Reserved Field IDs" |
| Partition transforms and their result types | "Partitioning", "Partition Transforms" |
| Bucket hashing and its test vectors | "Bucket Transform Details", "Appendix B: 32-bit Hash Requirements" |
| Truncation semantics | "Truncate Transform Details" |
| Evolving a partition spec | "Partition Evolution" |
| Sort orders | "Sorting" |
| Manifest file contents and entry fields | "Manifests", "Manifest Entry Fields", "Sequence Number Inheritance" |
| Snapshots, snapshot log, refs, retention | "Snapshots", "Snapshot References", "Snapshot Retention Policy" |
| Manifest list contents and field summaries | "Manifest Lists" |
| Planning a scan, pruning by partition and stats | "Scan Planning" |
| The table metadata document | "Table Metadata", "Table Metadata Fields" |
| Commits, retries and conflict resolution | "Optimistic Concurrency", "Commit Conflict Resolution and Retry" |
| Row-level deletes | "Row-level Deletes", "Delete Formats", "Position Delete Files", "Equality Delete Files", "Deletion Vectors" |
| Avro/Parquet/ORC mappings and field-id storage | "Appendix A: Format-specific Requirements" |
| JSON shapes of schemas, specs, orders, metadata | "Appendix C: JSON serialization" |
| Byte and JSON encodings of single values | "Appendix D: Single-value serialization" |

`PROFILE.md` beside this file records the benchmark's resolutions where the
specification leaves behaviour open.
