# TEST_INTERFACE.md — milestone 04 (row-level deletes)

This is the public contract the hidden verifier holds you to. Everything the
verifier names — crate identity, build command, module paths, exported symbols,
signatures, formats crossing the boundary, and the pinned crates it links
against — appears here or in a section of the specification corpus named from
`instruction.md`'s reading list. The verifier may combine these in ways this
document does not enumerate; it will not use an interface this document does
not name.

Section 8 lists what is **out of scope** at this milestone. Read it: several
things the Iceberg specification defines are deliberately outside this
milestone's contract.

## 1. Crate identity and build contract

| Item | Value |
| --- | --- |
| Package name | `iceberg` |
| Package version | `0.5.1` |
| Manifest | `/app/Cargo.toml` |
| Lockfile | `/app/Cargo.lock` — present and used; the verifier reuses it |
| Crate root | `/app/src/lib.rs` |
| Crate type | Rust library (`lib`), edition `2024` |
| Build command | `cargo build` run in `/app` |
| Toolchain | the `rustc`/`cargo` in the image (1.88.0); no nightly features |

The verifier runs `cargo build` in `/app` first. Then it creates a
**throw-away driver crate outside `/app`** that declares
`iceberg = { path = "/app" }` and compiles its own integration tests against
your crate. Your `Cargo.toml` is never modified, and no test of yours is ever
run or read as evidence.

The driver crate copies `/app/Cargo.lock` as its own starting resolution, so a
dependency you pin is the version the drivers link against. Keep `Cargo.lock`
in the workspace and keep it consistent with `Cargo.toml`. If your lockfile
cannot resolve the driver crate's own dependencies, the verifier retries once
with its own resolution instead of failing you — a rescue path, not a
guarantee: after it your pins are no longer what the drivers link against.

The verifier image carries a pre-populated Cargo registry cache for the
dependency set this milestone's own resolution needs, plus the driver crate's.
That is not necessarily the set the workspace arrives with: the workspace
carries the previous milestone's lockfile until you change it. A dependency
you add is fetched when the checker runs, so prefer the crates already resolved in
`Cargo.lock`.

## 2. Workspace layout

```text
/app/
  Cargo.toml            your crate manifest       (required, name = "iceberg")
  Cargo.lock            your resolved graph       (required)
  src/                  your implementation       (layout is yours)
  LICENSE  NOTICE       Apache-2.0 notices carried forward
  iceberg-spec/         the normative specification corpus (read-only)
  TEST_INTERFACE.md     this file
  WORKSPACE_HANDOFF.md  what to preserve when this milestone ends
  timer.sh              advisory countdown for this milestone's budget
```

Internal module organisation under `src/` is entirely yours **except** for the
module paths this document names in section 4: those are part of the public
API and the drivers use them verbatim (`iceberg::spec::Schema`, and so on).

## 3. Crates the verifier links against

The driver crate declares exactly these dependencies and no others:

```toml
iceberg     = { path = "/app" }
serde_json  = "1"
tempfile    = "3"
tokio       = { version = "1", features = ["rt-multi-thread", "macros"] }
uuid        = "1"
chrono      = "0.4"
```

Consequences you must design for:

- Types from `uuid` (`uuid::Uuid`) and `chrono` (`chrono::DateTime<chrono::Utc>`)
  cross the API boundary, so your crate must depend on **semver-compatible**
  versions of those crates (`uuid` 1.x, `chrono` 0.4.x). A different major
  version makes the types distinct and the drivers will not compile.
- Serialisation and deserialisation go through `serde`: every type this
  document marks *serde* must implement `serde::Serialize` and
  `serde::Deserialize` such that `serde_json` produces and accepts the JSON
  the specification defines.
- Async entry points are driven by `tokio`. Any `async fn` this document names
  must be executable on a plain tokio runtime, single- or multi-threaded; do
  not require a particular executor or flavour to be installed by the caller.
- The drivers use `tempfile::TempDir` for scratch directories and address the
  files inside them through your `FileIO`, so the local-filesystem scheme must
  accept plain absolute paths such as `/tmp/xyz/manifest.avro`.
- The drivers unwrap your `Result` and format the failure with `{}` and `{:?}`,
  so `Error` must implement `std::fmt::Debug` and `std::fmt::Display`. Where
  section 4 annotates a type with standard traits (`Debug`, `Clone`,
  `PartialEq`, …), those impls are required as well. Section 8 excludes the
  **text** `Debug` and `Display` produce; it never excludes the impls
  themselves.

## 4. Public API required at this milestone

Milestones 1–3 keep every export they already had, and the gate re-exercises
them. This milestone adds three public modules, one new public writer path for
manifests, and a set of delete-related fields on `DataFile`. Where it changes
an earlier signature the change is listed in section 5, and the new form is
what is required.

```rust
pub mod spec;      // milestones 1-3, extended below
pub mod io;        // milestones 1-2, unchanged for these drivers
pub mod expr;      // milestone 2
pub mod transform; // milestone 2
pub mod scan;      // milestone 2, extended below
pub mod table;     // milestone 2, extended below
pub mod arrow;     // milestone 2
pub mod writer;    // extended below
pub mod inspect;   // NEW — metadata inspection tables
pub mod cache;     // NEW — object-cache traits
pub mod puffin;    // NEW — Puffin blob files
pub use error::{Error, ErrorKind, Result};
```

**Standard traits on the value types.** The drivers compare values with
`assert_eq!`/`assert_ne!`, format them into failure messages, and clone them out
of `Arc`s, so every *value* type this section names must implement
`std::fmt::Debug`, `Clone` and `PartialEq` on top of whatever traits are listed
beside it. Those types are `DataContentType`, `DataFileFormat`,
`ManifestContentType`, `ManifestStatus`, `FormatVersion`, `DataFile`,
`Manifest`, `ManifestMetadata`, `ManifestEntry`, `ManifestFile`, `ManifestList`,
`FieldSummary`, `Schema`, `NestedField`, `Type`, `PrimitiveType`, `StructType`,
`Struct`, `Datum`, `PartitionSpec`, `PartitionField`, `Transform`, `Snapshot`,
`Summary`, `Operation`, `TableMetadata`, `TableIdent`, `Blob`, `BlobMetadata`,
`FileMetadata`, `CompressionCodec`, `FileScanTaskDeleteFile` and
`FileScanTask`.

Two inherited shapes the gate still relies on, restated because this
milestone's overlay is the only document a reader has: `PartitionSpec::fields()`
yields `PartitionField`s whose `field_id`, `name`, `source_id` and `transform`
are **public fields**, exactly as milestone 1 declared them; and `Type` keeps
its four variants, so a nested type is still reachable as `Type::List(_)` /
`Type::Map(_)` — one payload per variant, whose contents this milestone does not
name.

`TableRequirement` — the commit requirement milestone 3 introduced — gains
`Clone` at **this** milestone; it was `Debug + Serialize + Deserialize +
PartialEq` before. Milestone 5's catalog clones the requirement list it is
about to check, so the derive is required from here on. The builders
(`DataFileBuilder`, `ManifestWriterBuilder`, `Blob::builder()`'s typestate,
`TableBuilder`, `TableScanBuilder`), the writers (`ManifestWriter`,
`ManifestListWriter`, `PuffinWriter`), `PuffinReader`, `Table`, the `inspect`
tables and the `io` handles are **not** in that list and need none of those
traits, beyond the `Clone` on `FileIO` milestone 1 states.
`EqualityDeleteWriterConfig` needs `Clone` and `Debug` but not `PartialEq`, as
4.6 says. Section 8 excludes the *text* `Debug` and `Display` produce; it never
excludes the impls.

### 4.1 Delete content kinds and file formats — "Manifests", "Manifest Entry Fields", "Delete Formats"

```rust
// iceberg::spec
#[derive(Debug, PartialEq, Eq, Clone, Copy, Serialize, Deserialize)]
pub enum DataContentType { Data = 0, PositionDeletes = 1, EqualityDeletes = 2 }
impl TryFrom<i32> for DataContentType { type Error = Error; }   // 0|1|2 only

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum DataFileFormat { Avro, Orc, Parquet, Puffin }
impl std::str::FromStr for DataFileFormat { type Err = Error; }
impl std::fmt::Display for DataFileFormat {}
// serde is Display/FromStr based: a `DataFileFormat` is the JSON string
// "avro" | "orc" | "parquet" | "puffin".

#[derive(Debug, PartialEq, Eq, Clone, Copy, Hash)]
pub enum ManifestContentType { Data = 0, Deletes = 1 }
impl std::str::FromStr for ManifestContentType { type Err = Error; }
impl std::fmt::Display for ManifestContentType {}
impl TryFrom<i32> for ManifestContentType { type Error = Error; }   // 0|1 only

pub const UNASSIGNED_SEQUENCE_NUMBER: i64 = -1;
pub const UNASSIGNED_SNAPSHOT_ID: i64 = -1;
pub const MAIN_BRANCH: &str = "main";
```

Contracts the drivers assert:

- The discriminants are load-bearing: `DataContentType::Data as i32 == 0`,
  `PositionDeletes as i32 == 1`, `EqualityDeletes as i32 == 2`, and
  `ManifestContentType::Data as i32 == 0`, `Deletes as i32 == 1`. Both
  `TryFrom<i32>` impls reject every other value, including negatives and
  `i32::MAX` ("Manifest Entry Fields", "Manifest Lists" field 517).
- `DataFileFormat::from_str` lower-cases its input before matching, so
  `"Parquet"` parses; any name outside the four is an error. `puffin` exists as
  a data file format because "Delete Formats" stores deletion vectors in Puffin
  files.
- `ManifestContentType::from_str` is **case-sensitive**: exactly `"data"` and
  `"deletes"`. `"Deletes"` and `"delete"` are errors. `Display` produces the
  same two lower-case spellings, and the manifest writer writes that spelling
  into the Avro `content` metadata key.

### 4.2 `DataFile` — the delete-file fields — "Position Delete Files", "Deletion Vectors", "Delete File Stats"

`DataFile` gains one required builder field and four optional ones. It is
constructed through a *derive*-style builder: there is **no** `DataFile::builder()`. Both drivers `.unwrap()` the `build()` result, so whatever error type it
returns must implement `std::fmt::Debug` — this is the one trait obligation the
builders are *not* exempt from in the §4 preamble.

```rust
// iceberg::spec
DataFileBuilder::default()
    // required — no defaults:
    .content(DataContentType)
    .file_path(String)
    .file_format(DataFileFormat)
    .partition(Struct)
    .record_count(u64)
    .file_size_in_bytes(u64)
    .partition_spec_id(i32)                  // NEW at this milestone, required
    // optional — every one has a default:
    .column_sizes(HashMap<i32, u64>)
    .value_counts(HashMap<i32, u64>)
    .null_value_counts(HashMap<i32, u64>)
    .nan_value_counts(HashMap<i32, u64>)
    .lower_bounds(HashMap<i32, Datum>)
    .upper_bounds(HashMap<i32, Datum>)
    .key_metadata(Option<Vec<u8>>)
    .split_offsets(Vec<i64>)
    .equality_ids(Vec<i32>)                  // field 135
    .sort_order_id(i32)                      // strip_option: takes i32, stores Option<i32>
    .first_row_id(Option<i64>)               // field 142, NEW
    .referenced_data_file(Option<String>)    // field 143, NEW
    .content_offset(Option<i64>)             // field 144, NEW
    .content_size_in_bytes(Option<i64>)      // field 145, NEW
    .build()                                 // -> Result<DataFile, DataFileBuilderError>
```

Every setter takes `&mut self` and returns `&mut Self`, so the calls chain off
the temporary; `build(&self)` is **fallible** and returns `Result`.

Each optional field's default is its own type's empty value: an `Option<…>`
field that was never set reads back as `None` — so a `DataFile` that never set
`key_metadata` reports `key_metadata() == None`, and one that never set
`sort_order_id` reports `None` — and a collection-typed one (`split_offsets`,
`equality_ids` and the six statistics maps) reads back **empty**.

```rust
impl DataFile {
    pub fn content_type(&self) -> DataContentType;
    pub fn file_path(&self) -> &str;
    pub fn file_format(&self) -> DataFileFormat;
    pub fn partition(&self) -> &Struct;
    pub fn record_count(&self) -> u64;
    pub fn file_size_in_bytes(&self) -> u64;
    pub fn column_sizes(&self) -> &HashMap<i32, u64>;
    pub fn value_counts(&self) -> &HashMap<i32, u64>;
    pub fn null_value_counts(&self) -> &HashMap<i32, u64>;
    pub fn nan_value_counts(&self) -> &HashMap<i32, u64>;
    pub fn lower_bounds(&self) -> &HashMap<i32, Datum>;
    pub fn upper_bounds(&self) -> &HashMap<i32, Datum>;
    pub fn key_metadata(&self) -> Option<&[u8]>;
    pub fn split_offsets(&self) -> &[i64];
    pub fn equality_ids(&self) -> &[i32];
    pub fn sort_order_id(&self) -> Option<i32>;
    pub fn first_row_id(&self) -> Option<i64>;
    pub fn referenced_data_file(&self) -> Option<String>;   // by VALUE, not Option<&str>
    pub fn content_offset(&self) -> Option<i64>;
    pub fn content_size_in_bytes(&self) -> Option<i64>;
}
```

All five delete-related fields (135, 142, 143, 144, 145) must be written into
and read back out of the format-2 manifest Avro schema; the drivers write a
delete manifest and read it back to check each one survives.

**This overrides the version columns of the specification's `data_file` table.**
`spec.md` "Manifests" lists `142 first_row_id`, `144 content_offset` and
`145 content_size_in_bytes` in its **v3 column only**, and "First Row ID
Inheritance" — a section the instruction's reading list does not send you to —
says *"The value of `first_row_id` for delete files is always `null`"*. At this
milestone all three are nevertheless part of the format-2 `data_file` schema,
under PROFILE P1's carve-out for format-v3 features "where a milestone's
`TEST_INTERFACE.md` section 4 names them". Nothing here requires the v3
*semantics*: `first_row_id` is in scope only as a value that survives a manifest
round trip, as section 8 says, and no inheritance or row-lineage rule is
exercised.

"Delete File Stats" — *"Manifests hold the same statistics for delete files and
data files"* — so `column_sizes`, `value_counts`, `null_value_counts`,
`nan_value_counts`, `lower_bounds`, `upper_bounds` and `split_offsets` must
round-trip on a delete file exactly as they do on a data file.

"Position Delete Files" — a position delete file carries no `equality_ids` and
a **null** `sort_order_id` (*"Position deletes are required to be sorted by
file and position, not a table order, and should set sort order id to null"*),
while an equality delete file may carry a non-null one and it must round-trip.

"Deletion Vectors" — a delete entry that points at a blob inside a containing
file is tracked by `file_path`, `content_offset` and `content_size_in_bytes`,
and `referenced_data_file` names the single data file the deletes apply to.

### 4.3 Writing data and delete manifests — "Manifests", "Delete Formats"

The manifest write path is new. A manifest is no longer assembled as a
`Manifest` value and handed to a writer; the writer is built from the schema
and partition spec, entries are added to it, and it writes itself.

```rust
// iceberg::spec
pub enum ManifestStatus { Existing = 0, Added = 1, Deleted = 2 }   // milestone 1, unchanged

pub struct ManifestWriterBuilder { /* private */ }
impl ManifestWriterBuilder {
    pub fn new(
        output: OutputFile,
        snapshot_id: Option<i64>,
        key_metadata: Vec<u8>,
        schema: SchemaRef,
        partition_spec: PartitionSpec,
    ) -> Self;
    pub fn build_v1(self) -> ManifestWriter;         // format V1, content Data
    pub fn build_v2_data(self) -> ManifestWriter;    // format V2, content Data
    pub fn build_v2_deletes(self) -> ManifestWriter; // format V2, content Deletes
}

pub struct ManifestWriter { /* private */ }
impl ManifestWriter {
    pub fn add_file(&mut self, data_file: DataFile, sequence_number: i64) -> Result<()>;
    pub fn add_delete_file(&mut self, data_file: DataFile, sequence_number: i64,
                           file_sequence_number: Option<i64>) -> Result<()>;
    pub fn add_existing_file(&mut self, data_file: DataFile, snapshot_id: i64,
                             sequence_number: i64,
                             file_sequence_number: Option<i64>) -> Result<()>;
    pub async fn write_manifest_file(self) -> Result<ManifestFile>;   // consumes self
}
```

The three `build_*` methods **consume** the builder; `write_manifest_file`
**consumes** the writer; the three `add_*` methods take `&mut self`.

Behavioural contracts the drivers assert:

- **`add_delete_file` means "an entry whose status is `Deleted`"** — that is, a
  file being *removed* from the table — not "add a delete file to this
  manifest". Adding a position or equality delete file to a delete manifest is
  `add_file` on a writer produced by `build_v2_deletes()`. This follows
  "Manifest Entry Fields", where `status` 2 is DELETED and is orthogonal to
  `content`.
- **`add_file` sequence-number rule** ("Sequence Number Inheritance"): a
  `sequence_number` argument that is negative — use
  `UNASSIGNED_SEQUENCE_NUMBER` — writes a **null** data sequence number, so
  that it is inherited at read time; a non-negative argument is written
  verbatim and must not be overwritten later. The entry's status is set to
  `Added`, its snapshot id to the writer's snapshot id, and its file sequence
  number to null.
- **Data-vs-delete content check** ("Manifests": *"A separate set of manifests
  is used for delete files … but the same manifest schema is used for both"*,
  and "Delete Formats": *"Row-level deletes are added by v2 and are not
  supported in v1"*):
  - a `ManifestContentType::Data` manifest — which every v1 manifest is —
    rejects a `DataFile` whose content is `PositionDeletes` or
    `EqualityDeletes`;
  - a `ManifestContentType::Deletes` manifest rejects a `DataFile` whose
    content is `Data`;
  - so `build_v1()` followed by `add_file(<a delete file>)` is an error.
- An entry whose status is `Existing` or `Deleted` must carry **both** a
  `sequence_number` and a `file_sequence_number`; if either is `None` the add
  is an error ("Sequence Number Inheritance": *"When writing an existing file to
  a new manifest or marking an existing file as deleted, the data and file
  sequence numbers must be non-null"*).
- Each `add_*` method feeds one pair of the six counters, following the status
  it produces: `add_file` increments `added_files_count` by one and
  `added_rows_count` by the file's `record_count`; `add_existing_file`
  increments `existing_files_count` / `existing_rows_count`; `add_delete_file`
  increments `deleted_files_count` / `deleted_rows_count`. A rejected add — one
  that fails the content check or the both-sequence-numbers rule above —
  changes nothing: the entry is not recorded and no counter moves, so a writer
  whose adds were all rejected still writes a manifest with every count
  `Some(0)`.
- `write_manifest_file` returns a `ManifestFile` whose `sequence_number` is
  `UNASSIGNED_SEQUENCE_NUMBER`, whose `min_sequence_number` is the smallest
  sequence number among the live entries (or `UNASSIGNED_SEQUENCE_NUMBER` when
  there is none) — an entry whose data sequence number is **null**, because it
  was added with a negative `sequence_number` and is waiting to inherit one,
  does not contribute to the minimum — whose `added_snapshot_id` is the
  snapshot id the builder was given (or `UNASSIGNED_SNAPSHOT_ID` when it was
  given `None`) — "Manifest Lists" field 503, *"ID of the snapshot where the
  manifest file was added"* — whose `content` is the writer's content type, and
  whose `added_files_count` / `added_rows_count` / `existing_*` / `deleted_*`
  counts are the ones the previous bullet accumulated. All six counts are
  `Some(n)`, never `None`: a category with no entries reports `Some(0)`,
  because Appendix E makes those fields required from format version 2 onwards.
- The written Avro file carries these user-metadata keys: `schema`,
  `schema-id`, `partition-spec`, `partition-spec-id`, `format-version`, and —
  for format version 2 only — `content`, whose value is `"data"` or
  `"deletes"`. Reading the file back must recover the content type from it.

Reading side (unchanged in shape from milestone 1, listed because the drivers
use it on delete manifests):

```rust
impl Manifest {
    pub fn parse_avro(bs: &[u8]) -> Result<Self>;            // does NOT apply inheritance
    pub fn entries(&self) -> &[ManifestEntryRef];
    pub fn into_parts(self) -> (Vec<ManifestEntryRef>, ManifestMetadata);
}
pub struct ManifestMetadata {   // public fields
    pub schema: SchemaRef,
    pub schema_id: SchemaId,          // `pub type SchemaId = i32`
    pub partition_spec: PartitionSpec,
    pub format_version: FormatVersion,
    pub content: ManifestContentType,
}
impl ManifestEntry {
    pub fn status(&self) -> ManifestStatus;
    pub fn is_alive(&self) -> bool;
    pub fn content_type(&self) -> DataContentType;
    pub fn file_format(&self) -> DataFileFormat;
    pub fn file_path(&self) -> &str;
    pub fn record_count(&self) -> u64;
    pub fn file_size_in_bytes(&self) -> u64;
    pub fn snapshot_id(&self) -> Option<i64>;
    pub fn sequence_number(&self) -> Option<i64>;
    pub fn data_file(&self) -> &DataFile;
}
```

`into_parts` is the only public route to a parsed manifest's
`ManifestMetadata`, and it returns the entries **first**.

### 4.4 Manifest lists and sequence numbers — "Manifest Lists", "Sequence Numbers", "Sequence Number Inheritance"

```rust
// iceberg::spec
pub struct ManifestFile {                    // every field is public
    pub manifest_path: String,               // 500
    pub manifest_length: i64,                // 501
    pub partition_spec_id: i32,              // 502
    pub content: ManifestContentType,        // 517
    pub sequence_number: i64,                // 515
    pub min_sequence_number: i64,            // 516
    pub added_snapshot_id: i64,              // 503
    pub added_files_count: Option<u32>,      // 504
    pub existing_files_count: Option<u32>,   // 505
    pub deleted_files_count: Option<u32>,    // 506
    pub added_rows_count: Option<u64>,       // 512
    pub existing_rows_count: Option<u64>,    // 513
    pub deleted_rows_count: Option<u64>,     // 514
    pub partitions: Vec<FieldSummary>,       // 507
    pub key_metadata: Vec<u8>,               // 519
}
impl ManifestFile {
    pub async fn load_manifest(&self, file_io: &FileIO) -> Result<Manifest>;
    pub fn has_added_files(&self) -> bool;
    pub fn has_deleted_files(&self) -> bool;
    pub fn has_existing_files(&self) -> bool;
}
impl ManifestListWriter {
    pub fn v1(output_file: OutputFile, snapshot_id: i64,
              parent_snapshot_id: Option<i64>) -> Self;
    pub fn v2(output_file: OutputFile, snapshot_id: i64,
              parent_snapshot_id: Option<i64>, sequence_number: i64) -> Self;
    pub fn add_manifests(&mut self, manifests: impl Iterator<Item = ManifestFile>) -> Result<()>;
    pub async fn close(self) -> Result<()>;   // consumes self
}
```

- The three `has_*_files` predicates read the corresponding count. On a
  `ManifestFile` whose counts are present — which is every one
  `write_manifest_file` returns, as 4.3 says — `has_added_files()` is true
  exactly when `added_files_count` is `Some(n)` with `n > 0`, and likewise
  `has_existing_files()` for `existing_files_count` and `has_deleted_files()`
  for `deleted_files_count`. What they answer for an absent count is out of
  scope.
- **Manifest lists round-trip what they store.** `add_manifests` writes each
  `ManifestFile` into the list in the order given, and
  `ManifestList::parse_with_version` returns `manifest_path`,
  `manifest_length`, `partition_spec_id`, `added_snapshot_id`, `partitions`,
  `key_metadata` and the six counts exactly as they were written. That is the
  round trip only: what a writer *puts* in `partitions` and `key_metadata` is
  not constrained here, and key metadata's contents stay out of scope per
  section 8. `content`, `sequence_number` and `min_sequence_number` are
  format-version dependent, and for v2 the next bullet's substitution applies
  on the way in: a v1 manifest list has no column for any of the three, so a v1
  list reads back `content == ManifestContentType::Data` — "Manifest Lists"
  field 517, *"0 for all v1 manifests"* — with both sequence numbers `0`.
  Nothing here constrains how a writer *spells* a manifest's own location
  beyond field 500's own requirement that it be the *"Location of the manifest
  file"*: the `manifest_path` on the `ManifestFile` a writer returns must at
  least end with the name of the file that writer was given, and beyond that
  the drivers only compare the location the list gives back with the one the
  `ManifestFile` carried in.
- **Manifest-list sequence-number substitution** ("Sequence Numbers": *"All
  manifests, data files, and delete files created for a snapshot inherit the
  snapshot's sequence number. Manifest file metadata in the manifest list
  stores a manifest's sequence number."*). In `add_manifests`, for format
  version 2 only: a `ManifestFile` whose `sequence_number` or
  `min_sequence_number` is `UNASSIGNED_SEQUENCE_NUMBER` has the writer's
  `sequence_number` substituted — **but only when
  `manifest.added_snapshot_id == the writer's snapshot_id`**. A manifest
  carried forward from another snapshot must already carry its own numbers;
  substituting for it is an error, because only the current commit's manifests
  inherit the current commit's sequence number.
- **Entry-level inheritance** ("Sequence Number Inheritance"), applied by
  `ManifestFile::load_manifest` and *not* by `Manifest::parse_avro`: a null
  `snapshot_id` becomes the manifest list entry's `added_snapshot_id`; a null
  `sequence_number` and a null `file_sequence_number` become the manifest list
  entry's `sequence_number` when the entry's status is `Added` (or when the
  manifest's own sequence number is the initial one). A non-null sequence
  number is never overwritten, which is how *"a new file with data that
  logically belongs to an older sequence number"* keeps its number.
- A manifest list may hold data manifests and delete manifests together; each
  entry's `content` identifies which it is, and reading the list back must
  preserve it.

### 4.5 Position delete file layout — "Position Delete Files"

The two required columns of a `file_position_delete` record use the reserved
field ids the specification assigns. These are ordinary schema field ids, so
`Schema::builder()` must accept them, `field_by_id`/`field_id_by_name` must
find them, `highest_field_id` must report the larger of the two, and they must
survive a JSON round trip unchanged. `field_by_id` indexes **every** field, not
only the top-level ones: ids nested inside a list element, a map key or value,
or a struct member must each be findable by their own id.

| Field id | Name | Type | Required |
| --- | --- | --- | --- |
| `2147483546` | `file_path` | `string` | yes |
| `2147483545` | `pos` | `long` | yes |

(The optional `2147483544 row` column is not exercised.)

### 4.6 Equality delete files — "Equality Delete Files", "Identifier Field IDs"

An equality delete file names its delete columns in `DataFile::equality_ids`,
and those ids must round-trip through a delete manifest in the order given.

```rust
// iceberg::writer::base_writer::equality_delete_writer
pub struct EqualityDeleteWriterConfig { /* private */ }   // Clone + Debug
impl EqualityDeleteWriterConfig {
    pub fn new(
        equality_ids: Vec<i32>,
        original_schema: SchemaRef,
        partition_value: Option<Struct>,
        partition_spec_id: i32,               // NEW argument at this milestone
    ) -> Result<Self>;
    pub fn projected_arrow_schema_ref(&self) -> &arrow_schema::SchemaRef;
}
// Listed for orientation only. Nothing requires these two types or the
// `FileWriterBuilder` bound they are generic over — see section 8.
pub struct EqualityDeleteFileWriterBuilder<B: FileWriterBuilder> { /* private */ }
impl<B: FileWriterBuilder> EqualityDeleteFileWriterBuilder<B> {
    pub fn new(inner: B, config: EqualityDeleteWriterConfig) -> Self;
}
pub struct EqualityDeleteFileWriter<B: FileWriterBuilder> { /* private */ }
```

`EqualityDeleteWriterConfig::new` validates the delete columns against
"Equality Delete Files" and "Identifier Field IDs" and is an **error** when an
equality id names:

- a nested (non-primitive) column, or
- a floating-point column, or
- a field id that is not in `original_schema` — *"Fields with ids listed in
  this column must be present in the delete file"*.

For a valid config, `projected_arrow_schema_ref()` describes exactly the delete
columns, one Arrow field per equality id. The drivers only count its fields, so
your `arrow-schema` version is not part of the contract.

### 4.7 Puffin — `iceberg-spec/puffin-spec.md`

```rust
// iceberg::puffin
pub const APACHE_DATASKETCHES_THETA_V1: &str = "apache-datasketches-theta-v1";
pub const DELETION_VECTOR_V1: &str = "deletion-vector-v1";
pub const CREATED_BY_PROPERTY: &str = "created-by";

pub struct Blob { /* private */ }   // Debug + PartialEq + Clone; typed builder,
                                    // ALL SIX fields required, none has a default
Blob::builder()
    .r#type(String)                       // the setter is literally named `r#type`
    .fields(Vec<i32>)
    .snapshot_id(i64)
    .sequence_number(i64)
    .data(Vec<u8>)
    .properties(HashMap<String, String>)
    .build();                             // infallible
impl Blob {
    pub fn blob_type(&self) -> &str;
    pub fn fields(&self) -> &[i32];
    pub fn snapshot_id(&self) -> i64;
    pub fn sequence_number(&self) -> i64;
    pub fn data(&self) -> &[u8];
    pub fn properties(&self) -> &HashMap<String, String>;
}

#[derive(Debug, PartialEq, Eq, Clone, Copy, Default, Serialize, Deserialize)]
pub enum CompressionCodec { #[default] None, Lz4, Zstd }
// serde rename_all = "lowercase": the JSON strings are "none", "lz4", "zstd".

pub struct BlobMetadata { /* private */ }  // Debug + PartialEq + Eq + Clone + serde
impl BlobMetadata {
    pub fn blob_type(&self) -> &str;
    pub fn fields(&self) -> &[i32];
    pub fn snapshot_id(&self) -> i64;
    pub fn sequence_number(&self) -> i64;
    pub fn offset(&self) -> u64;
    pub fn length(&self) -> u64;
    pub fn compression_codec(&self) -> CompressionCodec;   // by value
    pub fn properties(&self) -> &HashMap<String, String>;
}

pub struct FileMetadata { /* private */ }  // Debug + PartialEq + Eq + Clone + serde
impl FileMetadata {
    pub fn new(blobs: Vec<BlobMetadata>, properties: HashMap<String, String>) -> Self;
    pub fn blobs(&self) -> &[BlobMetadata];
    pub fn properties(&self) -> &HashMap<String, String>;
}

pub struct PuffinWriter { /* private */ }
impl PuffinWriter {
    pub async fn new(output_file: &OutputFile,          // borrowed, not owned
                     properties: HashMap<String, String>,
                     compress_footer: bool) -> Result<Self>;
    pub async fn add(&mut self, blob: Blob, compression_codec: CompressionCodec) -> Result<()>;
    pub async fn close(self) -> Result<()>;             // consumes self
}

pub struct PuffinReader { /* private */ }
impl PuffinReader {
    pub fn new(input_file: InputFile) -> Self;          // owned, not borrowed
    pub async fn file_metadata(&self) -> Result<&FileMetadata>;   // memoised
    pub async fn blob(&self, blob_metadata: &BlobMetadata) -> Result<Blob>;
}
```

**Byte layout, asserted directly on the written file** — "Format
specification", "File structure", "Footer structure", "Footer Payload":

```text
file   = Magic  Blob₁ Blob₂ … Blobₙ  Footer
Footer = Magic  FooterPayload  FooterPayloadSize  Flags  Magic
Magic             = 0x50 0x46 0x41 0x31   ("PFA1", 4 bytes)
FooterPayloadSize = u32, little-endian, 4 bytes, length of FooterPayload
                    (after compression, if compressed)
Flags             = 4 bytes; byte 0 bit 0 = "FooterPayload is compressed";
                    every other bit is reserved and written as 0
FOOTER_STRUCT     = FooterPayloadSize ‖ Flags ‖ Magic  = 12 bytes
```

Consequences the drivers check on the raw bytes: the first four bytes and the
last four bytes of the file are both `PFA1`; with `compress_footer = false` the
four flag bytes are all zero; `FooterPayloadSize` read little-endian from
`len-12 .. len-8` gives the exact length of the JSON payload that begins four
bytes (the footer's leading magic) earlier; and the **first blob always starts
at offset 4**, immediately after the leading magic, with subsequent blobs laid
out back to back so `blobs[i+1].offset == blobs[i].offset + blobs[i].length`.

**FooterPayload JSON** — a single `FileMetadata` object, UTF-8, with these
exact key spellings ("FileMetadata", "BlobMetadata"):

```json
{
  "blobs": [
    {
      "type": "deletion-vector-v1",
      "fields": [2, 5],
      "snapshot-id": 11,
      "sequence-number": 4,
      "offset": 4,
      "length": 3,
      "compression-codec": "zstd",
      "properties": {"ndv": "3"}
    }
  ],
  "properties": {"created-by": "…"}
}
```

`compression-codec` is **omitted** when the codec is `None` (*"If omitted, the
data is assumed to be uncompressed"*), and `properties` is omitted when empty —
on both `BlobMetadata` and `FileMetadata`.

Reader behaviour ("Compression codecs" and "Footer structure"):

- `BlobMetadata::length` is the **stored** length, i.e. after compression;
  `PuffinReader::blob` decompresses and returns the original bytes, so a
  round-trip through `CompressionCodec::Zstd` yields a stored length smaller
  than the payload and a `Blob::data()` equal to it. With
  `CompressionCodec::None` the blob's bytes appear verbatim in the file at
  `offset .. offset + length`.
- `PuffinReader::blob` reads the payload out of the file, but the rest of the
  `Blob` it returns is carried over from the `BlobMetadata` argument: the
  returned `Blob`'s `blob_type`, `fields`, `snapshot_id`, `sequence_number` and
  `properties` are the ones recorded in that `BlobMetadata`. All six of `Blob`'s
  fields are required, and the footer is where the other five are stored, so
  there is nowhere else for them to come from.
- `PuffinReader::file_metadata` must **reject**: a file whose leading four
  bytes are not `PFA1`; a footer whose trailing four bytes are not `PFA1`; and
  a footer payload that is not a valid JSON `FileMetadata` object.
- A file written with no blobs is still a valid Puffin file: magic at both
  ends, an empty `blobs` array and — by the omission rule above — no
  `properties` member at all when the writer was given none. Reading it back
  gives a `FileMetadata` with no blobs and no properties, equal to
  `FileMetadata::new(vec![], HashMap::new())`.

### 4.8 Metadata inspection tables

```rust
// iceberg::inspect
pub struct MetadataTable<'a>(&'a Table);
impl<'a> MetadataTable<'a> {
    pub fn new(table: &'a Table) -> Self;
    pub fn snapshots(&self) -> SnapshotsTable<'a>;
    pub fn manifests(&self) -> ManifestsTable<'a>;
}
pub struct SnapshotsTable<'a> { /* private */ }
impl<'a> SnapshotsTable<'a> {
    pub fn new(table: &'a Table) -> Self;
    pub fn schema(&self) -> iceberg::spec::Schema;               // NOT an Arrow schema
    // `ArrowRecordBatchStream` is `iceberg::scan::ArrowRecordBatchStream`.
    // The scan methods are out of scope — see section 8.
    pub async fn scan(&self) -> Result<ArrowRecordBatchStream>;
}
pub struct ManifestsTable<'a> { /* private */ }
impl<'a> ManifestsTable<'a> {
    pub fn new(table: &'a Table) -> Self;
    pub fn schema(&self) -> iceberg::spec::Schema;
    pub async fn scan(&self) -> Result<ArrowRecordBatchStream>;
}
// iceberg::table
impl Table { pub fn inspect(&self) -> MetadataTable<'_>; }
```

The two schemas are fixed. Field order, names, ids and required-ness are all
asserted; the columns mirror the snapshot fields of "Snapshots" and the
`manifest_file` columns of "Manifest Lists".

`SnapshotsTable::schema()` — six fields, in this order:

| Id | Name | Type | Required |
| --- | --- | --- | --- |
| 1 | `committed_at` | `timestamptz` | yes |
| 2 | `snapshot_id` | `long` | yes |
| 3 | `parent_id` | `long` | no |
| 4 | `operation` | `string` | no |
| 5 | `manifest_list` | `string` | no |
| 6 | `summary` | `map<string, string>` | no |

The map's key element has field id **7** (`string`, required) and its value
element field id **8** (`string`, optional).

`ManifestsTable::schema()` — twelve fields, declared in this order (note that
the ids are *not* ascending: `content` is declared first but carries id 14):

| Id | Name | Type | Required |
| --- | --- | --- | --- |
| 14 | `content` | `int` | yes |
| 1 | `path` | `string` | yes |
| 2 | `length` | `long` | yes |
| 3 | `partition_spec_id` | `int` | yes |
| 4 | `added_snapshot_id` | `long` | yes |
| 5 | `added_data_files_count` | `int` | yes |
| 6 | `existing_data_files_count` | `int` | yes |
| 7 | `deleted_data_files_count` | `int` | yes |
| 15 | `added_delete_files_count` | `int` | yes |
| 16 | `existing_delete_files_count` | `int` | yes |
| 17 | `deleted_delete_files_count` | `int` | yes |
| 8 | `partition_summaries` | `list` | yes |

`partition_summaries`' element field has id **9**, is named `item`, is
required, and is a struct of:

| Id | Name | Type | Required |
| --- | --- | --- | --- |
| 10 | `contains_null` | `boolean` | yes |
| 11 | `contains_nan` | `boolean` | no |
| 12 | `lower_bound` | `string` | no |
| 13 | `upper_bound` | `string` | no |

`MetadataTable::new(&table)` and `table.inspect()` must produce the same
schemas: the metadata tables are views over the table they are built from and
hold no state of their own.

### 4.9 The object cache

```rust
// iceberg::cache
pub trait ObjectCache<K, V>: Send + Sync {
    fn get(&self, key: &K) -> Option<V>;
    fn set(&self, key: K, value: V);
}
pub trait ObjectCacheProvide: Send + Sync {
    fn manifest_cache(&self) -> &dyn ObjectCache<String, std::sync::Arc<Manifest>>;
    fn manifest_list_cache(&self) -> &dyn ObjectCache<String, std::sync::Arc<ManifestList>>;
}
pub type ObjectCacheProvider = std::sync::Arc<dyn ObjectCacheProvide>;

// iceberg::table
impl TableBuilder {                      // every setter takes `mut self` and returns Self
    pub fn file_io(self, file_io: FileIO) -> Self;                     // required
    pub fn metadata<T: Into<TableMetadataRef>>(self, metadata: T) -> Self;  // required
    pub fn identifier(self, identifier: TableIdent) -> Self;           // required
    pub fn metadata_location<T: Into<String>>(self, location: T) -> Self;
    pub fn readonly(self, readonly: bool) -> Self;
    pub fn disable_cache(self) -> Self;
    pub fn cache_size_bytes(self, cache_size_bytes: u64) -> Self;
    pub fn build(self) -> Result<Table>;
}
```

Both traits must be **dyn-compatible**: the drivers store them behind
`&dyn ObjectCache<…>` and `Arc<dyn ObjectCacheProvide>`, and `set` takes
`&self`, so an implementor supplies its own interior mutability. `get` returns
the value by clone, not by reference.

`TableBuilder::build` is an error when `file_io`, `metadata` or `identifier`
was not supplied. Cache configuration is invisible in the result: a table built
with the default cache, one built with `disable_cache()`, and one built with
`cache_size_bytes(n)` report identical metadata.

`ObjectCacheProvide` hands out its two caches as
`&dyn ObjectCache<String, Arc<Manifest>>` and
`&dyn ObjectCache<String, Arc<ManifestList>>`, and `ObjectCache` is itself a
`Send + Sync` trait, so **`Manifest` and `ManifestList` must both be
`Send + Sync`**: an `Arc<T>` is `Send + Sync` only when `T` is, and a cache
implementation that stores them cannot otherwise satisfy the supertrait.

Because a manifest file is immutable once written ("File System Operations":
*"Files are not moved or altered once they are written"*), loading the same
`ManifestFile` twice through `load_manifest` must produce equal `Manifest`
values — `Manifest`, `ManifestEntry` and `DataFile` are compared directly with
`assert_eq!`, as is the `TableMetadata` of three differently-cached tables, so
they carry the `Debug` + `PartialEq` obligation the section 4 preamble states.

### 4.10 Scan surface, extended — "Row-level Deletes", "Scan Planning"

```rust
// iceberg::scan
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct FileScanTaskDeleteFile {
    pub file_path: String,
    pub file_type: DataContentType,
    pub partition_spec_id: i32,
    pub equality_ids: Vec<i32>,     // empty for position deletes
}
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct FileScanTask {
    pub start: u64,
    pub length: u64,
    pub record_count: Option<u64>,
    pub data_file_path: String,
    pub data_file_content: DataContentType,
    pub data_file_format: DataFileFormat,
    pub schema: SchemaRef,
    pub project_field_ids: Vec<i32>,
    pub predicate: Option<BoundPredicate>,
    pub deletes: Vec<FileScanTaskDeleteFile>,   // NEW at this milestone
}
impl TableScanBuilder<'_> {
    pub fn with_delete_file_processing_enabled(self, enabled: bool) -> Self;  // consumes self
    // milestone 2's methods are unchanged
}
impl TableScan {
    pub async fn plan_files(&self) -> Result<FileScanTaskStream>;
    pub fn column_names(&self) -> Option<&[String]>;
    pub fn snapshot(&self) -> Option<&SnapshotRef>;
}
```

`FileScanTask` and `FileScanTaskDeleteFile` both serialise with their field
names verbatim — `data_file_path`, `deletes` and the rest of the names declared
above for the task, and `file_path`, `file_type`, `partition_spec_id`,
`equality_ids` for the delete file — and both round-trip through JSON.

Delete-file processing is **off** by default: a `TableScanBuilder` that never
calls `with_delete_file_processing_enabled` behaves as if it had been passed
`false`.

`plan_files` resolves and parses the snapshot's manifest list, and builds the
per-manifest planning contexts, **before** it returns — so an unreadable or
missing manifest list is an error from `plan_files` itself, and a manifest list
that mixes data and delete manifests must plan successfully with delete-file
processing both enabled and disabled. Toggling
`with_delete_file_processing_enabled` must not change the scan's snapshot or
its projected column names.

### 4.11 Inherited handles the drivers name directly

Milestones 1–3 keep every export they already had, and this milestone's
overlay is the only contract a reader has, so the inherited declarations most
easily lost when reading only this overlay are restated below. Nothing here is
new or changed, and the list is not exhaustive: everything those milestones
exported is still required, carried forward in the workspace you inherit and
summarised in section 5's unchanged list.

```rust
// iceberg::io — every case writes real files and reads them back.
impl FileIO {                                       // FileIO: Clone
    pub fn new_input(&self, path: impl AsRef<str>) -> Result<InputFile>;
    pub fn new_output(&self, path: impl AsRef<str>) -> Result<OutputFile>;
}
impl FileIOBuilder {
    pub fn new_fs_io() -> Self;                     // the local filesystem (PROFILE P3)
    pub fn build(self) -> Result<FileIO>;
}
impl InputFile { pub async fn read(&self) -> Result<bytes::Bytes>; }

// iceberg::spec — the third argument of the manifest-list reader is a
// partition-type provider, called once per partition-spec id; the drivers
// always pass a closure returning `Ok(None)`.
impl ManifestList {
    pub fn parse_with_version(bs: &[u8], version: FormatVersion,
        partition_type_provider: impl Fn(i32) -> Result<Option<StructType>>)
        -> Result<ManifestList>;
}

pub type NestedFieldRef = std::sync::Arc<NestedField>;
pub struct NestedField {
    pub id: i32,
    pub name: String,
    pub required: bool,
    pub field_type: Box<Type>,          // deref to &Type via `.as_ref()`
    // the doc / initial_default / write_default fields milestone 1 declared
    // are unchanged and not checked here
}
impl NestedField {
    pub fn required(id: i32, name: impl ToString, field_type: Type) -> Self;
    pub fn optional(id: i32, name: impl ToString, field_type: Type) -> Self;
}

// iceberg::spec — the schema and value constructors every fixture goes through.
impl Schema {
    pub fn builder() -> SchemaBuilder;
    pub fn as_struct(&self) -> &StructType;
    pub fn field_by_name(&self, field_name: &str) -> Option<&NestedFieldRef>;
}
impl SchemaBuilder {                     // every setter consumes and returns Self
    pub fn with_schema_id(self, schema_id: i32) -> Self;
    pub fn with_fields(self, fields: impl IntoIterator<Item = NestedFieldRef>) -> Self;
    pub fn with_identifier_field_ids(self, ids: impl IntoIterator<Item = i32>) -> Self;
    pub fn build(self) -> Result<Schema>;
}
// Milestone 1's spellings, unchanged: a field type is `Type::Primitive(
// PrimitiveType::{Boolean, Int, Long, Double, String, Timestamptz, …})` or
// `Type::Struct(StructType)`; the two nested variants are `List` and `Map`.
impl StructType { pub fn new(fields: Vec<NestedFieldRef>) -> Self; }
impl Struct     { pub fn empty() -> Self; }
impl Datum      { pub fn long<T: Into<i64>>(t: T) -> Self; }
impl PartitionSpec { pub fn spec_id(&self) -> i32; }

// iceberg::table
impl Table {
    pub fn metadata(&self) -> &TableMetadata;
    pub fn scan(&self) -> TableScanBuilder<'_>;
}

// iceberg — the CRATE ROOT, not `spec`: milestone 1's crate-root re-export of
// the catalog names keeps `TableIdent` reachable as `iceberg::TableIdent`, and
// the drivers and the gate import it from there. How the crate spells that
// re-export is its own business; the path is what is required.
pub use catalog::TableIdent;
impl TableIdent {
    pub fn from_strs(iter: impl IntoIterator<Item = impl ToString>) -> Result<TableIdent>;
}
```

`bytes` is not one of the crates section 3 lists, so the drivers never name
`InputFile::read`'s return type: they hand it straight to `Manifest::parse_avro`
or `ManifestList::parse_with_version` as a `&[u8]`, so whatever it returns must
deref to a byte slice.

## 5. Migration from the previous milestone

Milestone 3's contract changes in the following ways. The new form is what is
required; the old form is not accepted.

| Milestone 3 | This milestone |
| --- | --- |
| `ManifestWriter::new(output: OutputFile, snapshot_id: i64, key_metadata: Vec<u8>)`, then `Manifest::new(metadata, entries)` and `writer.write(manifest).await -> Result<ManifestFile>` | `ManifestWriterBuilder::new(output, Some(snapshot_id), key_metadata, schema, partition_spec)` → `build_v1()` / `build_v2_data()` / `build_v2_deletes()` → `add_file` / `add_delete_file` / `add_existing_file` → `write_manifest_file().await -> Result<ManifestFile>`. `ManifestMetadata` is derived by the builder and is no longer assembled by the caller on the write path |
| `DataFileBuilder` has no `partition_spec_id` | `partition_spec_id(i32)` is a **required** builder field |
| `DataFile` ends at `sort_order_id` | adds `first_row_id` (142), `referenced_data_file` (143), `content_offset` (144), `content_size_in_bytes` (145), each optional with a default and each round-tripping through the format-2 manifest |
| `DataFileFormat { Avro, Orc, Parquet }` | adds `Puffin`, spelled `"puffin"` by `FromStr`, `Display` and serde |
| `FileScanTask` has no delete files | `FileScanTask::deletes: Vec<FileScanTaskDeleteFile>`, and `TableScanBuilder::with_delete_file_processing_enabled(bool)` |
| `TableScan::snapshot(&self) -> &SnapshotRef` | `-> Option<&SnapshotRef>` (`None` when the table has no current snapshot). This is the only change to milestone 2's `scan` surface |
| `EqualityDeleteWriterConfig::new(equality_ids, original_schema, partition_value)` | `EqualityDeleteWriterConfig::new(equality_ids, original_schema, partition_value, partition_spec_id)` |
| `puffin` is private | `pub mod puffin`, exporting `Blob`, `BlobMetadata`, `FileMetadata`, `PuffinWriter`, `PuffinReader`, `CompressionCodec`, `DELETION_VECTOR_V1`, `APACHE_DATASKETCHES_THETA_V1`, `CREATED_BY_PROPERTY` |
| no metadata inspection tables | `pub mod inspect` with `MetadataTable`, `SnapshotsTable`, `ManifestsTable`, reached from `Table::inspect()` |
| no object-cache traits | `pub mod cache` with `ObjectCache`, `ObjectCacheProvide`, `ObjectCacheProvider` |
| crate edition 2021 | crate **edition 2024** (`rust-version = "1.85"`). This is a compile-time move, not an API change; it mainly affects `unsafe` attribute spelling, `gen` as a keyword, and the new match-ergonomics and temporary-scope rules |

Unchanged from the previous milestone and still required in exactly the same
form: `ManifestFile`'s public fields and `load_manifest`;
`ManifestList::parse_with_version` (three arguments) and `entries()`;
`ManifestListWriter::{v1, v2, add_manifests, close}`;
`Manifest::{parse_avro, entries, into_parts}`; `ManifestEntry`'s accessors;
`DataFileBuilder::default()` with `&mut self` setters and a fallible `build()`;
`DataContentType` and its `TryFrom<i32>`; `ManifestContentType` with its
`FromStr`, `Display` and `TryFrom<i32>`; `PartitionSpec::builder(schema)` with
its consuming methods; `TableMetadata`'s accessors, including
`default_partition_spec()` returning a reference rather than an `Option`;
`TableMetadataBuilder` and its update algebra; `Datum`; `Schema`;
`Snapshot::builder()` with its `with_`-prefixed setters; `Table::builder()` and
the setters it already had (`file_io`, `metadata`, `identifier`,
`metadata_location`, `readonly`, `build`); and milestone 2's `expr` and
`transform` surface together with the rest of its `scan` surface — everything
except `TableScan::snapshot`, whose new form is in the table above.
`TableBuilder::disable_cache()` and `cache_size_bytes()` are **new** at this
milestone, not carried forward; section 4.9 specifies them.
## 6. Formats crossing the boundary

- **Table metadata** is JSON, exactly as `iceberg-spec/spec.md` "Table Metadata",
  "Table Metadata Fields" and Appendix C define it. The drivers deserialise
  documents written by hand from those sections and re-serialise your value,
  comparing the documented field names and values. Field **order** in the
  emitted JSON is never asserted; field **names** and values are.
- **Schemas, partition specs and sort orders** are JSON per Appendix C.
- **Type names** are JSON strings per "Primitive Types" and Appendix C.
  Appendix C admits both `"decimal(9,2)"` and `"decimal(9, 2)"`; the drivers
  accept either spelling from you and require you to accept both.
- **Single values** (bounds, defaults, partition values) follow Appendix D —
  the binary form for bounds inside manifests, the JSON form inside metadata.
- **Manifests and manifest lists** are Avro object container files per
  "Manifests", "Manifest Lists" and Appendix A. The drivers check the Avro
  object-container magic `Obj\x01` and round-trip the file through your own
  reader. Where a milestone's section 4 says so, they also decode the
  uncompressed Avro header and check the writer schema and the file metadata
  it records.
- **Errors** are reported through your `Result` type. The drivers assert only
  that an operation **fails**; the error's variant, kind, message wording and
  source chain are never asserted.

## 7. Scope

- One **gate**: `cargo build` succeeds, one end-to-end path through this
  milestone's headline capability succeeds, and one end-to-end path through
  every milestone this milestone inherits still succeeds.

## 8. Out of scope at this milestone

Never in scope, at any milestone:

- Internal module layout below the paths section 4 names, private type and
  function names, file organisation under `src/`.
- Error message wording, error-variant identity, and the **text** that
  `Debug`/`Display` produce for any type this document does not explicitly name
  a format for. Whether those impls *exist* is not in this list: sections 3 and
  4 require them, and only what they print is out of scope.
- Iteration order the specification does not fix, allocation strategy, timing,
  memory use, throughput.
- Your own tests, benchmarks, examples, documentation and CI configuration.
- Any capability of the Iceberg specification not named in section 4 or in a
  specification section named by `instruction.md`'s reading list — in
  particular: catalogs other than the ones section 4 names, engine
  integrations, query execution beyond scan planning, and any format-version 3
  feature that section 4 does not itself name, unless this
  milestone's section 8 additions say otherwise.

Additionally, out of scope at **this** milestone:

- Transactions, catalogs, commit conflict resolution and transaction actions —
  milestone 5.
- A public **deletion-vector type**. The `deletion-vector-v1` blob type name,
  the Puffin blob that carries such a vector, and the delete-manifest fields
  that locate it (`referenced_data_file`, `content_offset`,
  `content_size_in_bytes`) are in scope; a public set-of-positions type with
  `set`/`contains`/iterate, and the roaring-bitmap serialised form of
  "Deletion Vectors", are not. Keep whatever internal representation you like.
- A **position-delete writer**. The reserved column ids `2147483546 file_path`
  and `2147483545 pos` are in scope as schema field ids, and position delete
  files are in scope as manifest content; a writer that produces the delete file
  itself is not. `writer::base_writer` need only carry the data-file writer and
  the equality-delete writer.
- Writing an **equality delete file end to end**. Only
  `EqualityDeleteWriterConfig::new`, its validation rules and
  `projected_arrow_schema_ref()` are in scope; the `IcebergWriter` write path over
  record batches is not, and neither are the
  `EqualityDeleteFileWriterBuilder` / `EqualityDeleteFileWriter` types that
  section 4.6 lists or the `FileWriterBuilder` bound they are generic over. No
  driver names them.
- The concrete **object-cache implementation**. Only the `cache` module's
  traits, the `TableBuilder` cache switches, and the observable requirement
  that loading the same manifest twice yields equal contents are in scope. The
  cache's eviction policy, capacity accounting, hit/miss counters and any
  public handle onto a live cache are not.
- The **row contents of the inspection tables**. Only
  `SnapshotsTable::schema()` and `ManifestsTable::schema()` are in scope; the
  `scan()` methods themselves, the record batches they produce, and the
  `ArrowRecordBatchStream` alias they return are not. No driver calls `scan()`
  on an inspection table.
- **Consuming the planned file-scan-task stream.** `plan_files` is in scope for
  resolving the manifest list and returning successfully — or failing when the
  manifest list is unreadable — but the tasks it streams are not read, and
  neither is the delete-file index that attaches delete files to a data file's
  task.
- The **LZ4 Puffin codec**, for blob data and for a compressed footer payload.
  `CompressionCodec::Lz4` must exist as a variant and spell itself `"lz4"`;
  whether it actually compresses is out of scope. Only `None` and `Zstd` are
  exercised end to end.
- Reading or writing **data file contents** — Parquet, Avro or ORC row data,
  and applying deletes to a scanned row set.
- The optional `2147483544 row` column of a position delete file, and the
  reconstruction of deleted row values.
- Format-version-3 additions: deletion vectors as a distinct manifest content
  kind, `first_row_id` inheritance and row lineage, and the rule that position
  delete files must not be added to v3 tables. `first_row_id` is in scope only as
  a field that survives a manifest round trip.
- Table statistics, partition statistics, encryption, key-metadata contents,
  and the `snapshots`/`manifests` inspection tables' Arrow-level column
  metadata.
- The `rust-version` value in `Cargo.toml`. The package `name`, `version` and
  `edition` from section 1 are read out of `/app/Cargo.toml` and are in scope; the
  minimum-supported-Rust-version key that the release also sets is not.
- The **`Clone` derive on `TableRequirement`** that section 4 requires. It is a
  forward requirement — milestone 5's catalog clones the requirement list it is
  about to check — and no driver at this milestone names the type, so nothing
  here observes it. Section 4 states it so that a candidate adds it in the
  commit where the rest of this milestone lands rather than discovering it a
  milestone later.
- The **contents of the `transform` and `arrow` modules**. Both appear in
  section 4's module block because milestone 2 required them and milestone 5
  requires them again, so they must stay public; no requirement group or gate case at this
  milestone reaches into either, and `create_transform_function`, the
  `TransformFunction` trait and every Arrow conversion are out of scope here.
  (`Transform` itself, which lives in `iceberg::spec`, *is* used by the gate.)
