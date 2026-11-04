# TEST_INTERFACE.md — milestone 01 (table format core)

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
| Package version | `0.2.0` |
| Manifest | `/app/Cargo.toml` |
| Lockfile | `/app/Cargo.lock` — present and used; the verifier reuses it |
| Crate root | `/app/src/lib.rs` |
| Crate type | Rust library (`lib`), edition `2021` |
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

Two public modules, plus the crate-root error type.

```rust
pub mod spec;   // the table-format value types
pub mod io;     // file access
pub use error::{Error, ErrorKind, Result};   // Result<T> = std::result::Result<T, Error>
```

Everything in `spec` below is re-exported at `iceberg::spec::<Name>` — the
drivers write `use iceberg::spec::*;`, so the names must be reachable from that
one path regardless of which file you define them in.

**Standard traits on the value types.** The drivers compare values with
`assert_eq!`, format them into failure messages, and clone them out of `Arc`s
(`schema.as_ref().clone()`), so every *value* type this section names must
implement `std::fmt::Debug`, `Clone` and `PartialEq`. Those types are:
`Type`, `PrimitiveType`, `StructType`, `NestedField`, `ListType`, `MapType`,
`Schema`, `PartitionSpec`, `PartitionField`, `Transform`, `SortOrder`,
`SortField`, `SortDirection`, `NullOrder`, `Operation`, `Summary`, `Snapshot`,
`SnapshotLog`, `MetadataLog`, `FormatVersion`, `TableMetadata`,
`ManifestContentType`, `ManifestStatus`, `DataContentType`, `DataFileFormat`,
`Manifest`, `ManifestMetadata`, `ManifestEntry`, `DataFile`,
`ManifestListEntry`, `ManifestList`, `FieldSummary`, `Literal`,
`PrimitiveLiteral` and `Struct`. The builders (`SchemaBuilder`, the typed
builders of 4.8), the writers (`ManifestWriter`, `ManifestListWriter`) and the
`io` handles are **not** in that list and need none of those traits, beyond the
`Clone` on `FileIO` that 4.10 states. `Error` implements `Debug` and `Display`
(section 3). Section 8 excludes the *text* `Debug` and `Display` produce, never
the impls.

### 4.1 Types — `iceberg-spec/spec.md` "Schemas and Data Types", Appendix C

```rust
pub enum Type { Primitive(PrimitiveType), Struct(StructType), List(ListType), Map(MapType) }
pub enum PrimitiveType {
    Boolean, Int, Long, Float, Double, Date, Time, Timestamp, Timestamptz,
    String, Uuid, Fixed(u64), Binary, Decimal { precision: u32, scale: u32 },
}
impl Type {
    pub fn is_primitive(&self) -> bool;
    pub fn is_struct(&self) -> bool;
    pub fn decimal(precision: u32, scale: u32) -> Result<Self>;
    pub fn decimal_required_bytes(precision: u32) -> Result<u32>;
}
```

`Type` is *serde*: it serialises to the JSON of Appendix C — a string for a
primitive, an object with `"type": "struct" | "list" | "map"` for the rest.
A primitive name the specification does not define must fail to deserialise.

- `decimal(P, S)` fails for a precision the specification does not allow
  (`P > 38`, "Primitive Types").
- `decimal_required_bytes(P)` returns Appendix A's `minBytesRequired(P)`: the
  minimum number of bytes needed to store an unscaled decimal of precision `P`
  as a two's-complement big-endian value — 4 for `P = 9`, 8 for `P = 18`,
  16 for `P = 38`. It fails for a precision outside `1..=38`.

```rust
pub struct StructType { /* private */ }
impl StructType {
    pub fn new(fields: Vec<NestedFieldRef>) -> Self;
    pub fn fields(&self) -> &[NestedFieldRef];
    pub fn field_by_id(&self, id: i32) -> Option<&NestedFieldRef>;
    pub fn field_by_name(&self, name: &str) -> Option<&NestedFieldRef>;
}

pub type NestedFieldRef = std::sync::Arc<NestedField>;
pub struct NestedField {
    pub id: i32,
    pub name: String,
    pub required: bool,
    pub field_type: Box<Type>,      // deref to &Type via `.as_ref()`
    pub doc: Option<String>,
    // initial_default / write_default may be present; out of scope here
}
impl NestedField {
    pub fn required(id: i32, name: impl ToString, field_type: Type) -> Self;
    pub fn optional(id: i32, name: impl ToString, field_type: Type) -> Self;
    pub fn list_element(id: i32, field_type: Type, required: bool) -> Self;
    pub fn map_key_element(id: i32, field_type: Type) -> Self;
    pub fn map_value_element(id: i32, field_type: Type, required: bool) -> Self;
    pub fn with_doc(self, doc: impl ToString) -> Self;
}
```

`NestedField: Into<NestedFieldRef>` — the drivers write
`NestedField::required(..).into()`. `ListType` and `MapType` need only exist,
be constructible from Appendix C JSON, and be reachable as `Type::List` /
`Type::Map`.

The three nested-position helpers build the field Appendix C names for that
position: `list_element` a field named `element` with the given `required`,
`map_key_element` a **required** field named `key`, and `map_value_element` a
field named `value` with the given `required`. `with_doc` sets `doc`.

### 4.2 Schema — "Schemas and Data Types", "Identifier Field IDs", Appendix C

```rust
pub struct Schema { /* private */ }          // PartialEq, serde
pub type SchemaRef = std::sync::Arc<Schema>;
pub struct SchemaBuilder { /* private */ }
impl Schema {
    pub fn builder() -> SchemaBuilder;
    pub fn schema_id(&self) -> i32;
    pub fn as_struct(&self) -> &StructType;
    pub fn field_by_id(&self, field_id: i32) -> Option<&NestedFieldRef>;
    pub fn field_by_name(&self, field_name: &str) -> Option<&NestedFieldRef>;
    pub fn field_id_by_name(&self, name: &str) -> Option<i32>;
    pub fn name_by_field_id(&self, field_id: i32) -> Option<&str>;
    pub fn highest_field_id(&self) -> i32;
}
impl SchemaBuilder {                          // consuming builder: takes self
    pub fn with_schema_id(self, schema_id: i32) -> Self;
    pub fn with_fields(self, fields: impl IntoIterator<Item = NestedFieldRef>) -> Self;
    pub fn with_identifier_field_ids(self, ids: impl IntoIterator<Item = i32>) -> Self;
    pub fn build(self) -> Result<Schema>;
}
```

- Names are **dotted paths** for nested fields: a `lat` inside a struct field
  named `location` is `field_id_by_name("location.lat")`.
- The `SchemaBuilder` setters may be called in **any order** and none of them
  validates: the drivers set the identifier field ids before the fields those
  ids name. All validation happens in `build()`.
- `build()` must **fail** when an identifier field id names a field that is
  optional, or whose type is floating point, or that does not exist —
  "Identifier Field IDs".
- `highest_field_id()` is the **largest id in the schema's own field list** —
  the schema-level counterpart of `last-column-id`, "the highest assigned
  column ID" ("Table Metadata Fields"). Whether an id nested inside a `struct`,
  `list` or `map` field can also raise it is **out of scope** here (section 8);
  the drivers only ask this of a flat schema.
- Serde shape: `{"type":"struct","schema-id":N,"identifier-field-ids":[...],
  "fields":[{"id":..,"name":..,"required":..,"type":..,"doc":..}, ...]}`.
  `identifier-field-ids` is omitted when empty.

### 4.3 Partition specs — "Partitioning", "Partition Transforms", Appendix C

```rust
pub struct PartitionField {
    pub source_id: i32,
    pub field_id: i32,
    pub name: String,
    pub transform: Transform,
}
pub struct PartitionSpec {
    pub spec_id: i32,
    pub fields: Vec<PartitionField>,
}
pub type PartitionSpecRef = std::sync::Arc<PartitionSpec>;
impl PartitionSpec {
    pub fn is_unpartitioned(&self) -> bool;
    pub fn partition_type(&self, schema: &Schema) -> Result<StructType>;
}
```

`PartitionSpec` and `PartitionField` are *serde* with the Appendix C
"Partition Specs" names `spec-id`, `fields`, `source-id`, `field-id`, `name`,
`transform`. Both structs are constructed **by struct literal** in the drivers,
so their fields are public and named exactly as above.

`is_unpartitioned()` is true when the spec partitions nothing: it has no
partition fields, or every one of its fields uses the `void` transform.

`partition_type` returns one `NestedField` per partition field, in spec order,
with `id` = the partition field id, `name` = the partition field name, type =
the transform's result type over the source field's type, and `required =
false`. It must **fail** when a `source_id` is not in the schema.

### 4.4 Transforms — "Partition Transforms", "Bucket/Truncate Transform Details"

```rust
pub enum Transform {
    Identity, Bucket(u32), Truncate(u32), Year, Month, Day, Hour, Void, Unknown,
}
impl Transform {
    pub fn result_type(&self, input_type: &Type) -> Result<Type>;
    pub fn preserves_order(&self) -> bool;
    pub fn satisfies_order_of(&self, other: &Self) -> bool;
    pub fn dedup_name(&self) -> String;
}
impl std::str::FromStr for Transform { type Err = Error; }
impl std::fmt::Display for Transform {}
```

- Textual form, used by `FromStr`, `Display` **and** serde: `identity`, `void`,
  `year`, `month`, `day`, `hour`, `bucket[N]`, `truncate[W]`, and `unknown` for
  the `Unknown` variant — the placeholder for a transform this milestone does
  not recognise. A name outside those nine forms must fail to parse.
- `result_type` follows the spec's result-type column: `int` for `bucket[N]`
  and for `year`/`month`/`day`/`hour`, the source type for `identity`,
  `truncate[W]` and `void`. The four temporal transforms produce `int` over
  **every** source type the spec's "Source types" column admits — `year`,
  `month` and `day` over `date`, `timestamp` and `timestamptz`, and `hour` over
  `timestamp` and `timestamptz`. `result_type` must **fail** for a source type
  that column excludes — `bucket` over `double`, `truncate` over `boolean`,
  `hour` over `date`, `year` over `string`. What `result_type` does for
  `Transform::Unknown` is out of scope (section 8).
- `preserves_order` is true for `identity`, `truncate`, `year`, `month`, `day`,
  `hour`, and false for `bucket` and `void`.
- `satisfies_order_of` is true when this transform's ordering refines the
  other's: `day` satisfies `year` and `month`, `hour` satisfies `day`,
  `identity` satisfies any order-preserving transform; `year` does not satisfy
  `day` and `bucket` satisfies nothing but itself.
- `dedup_name` is shared by the four time transforms (`year`, `month`, `day`,
  `hour` return the same string) so that a spec cannot carry two time
  transforms of one source field, and differs between `bucket` and `truncate`.

### 4.5 Sort orders — "Sorting", Appendix C

```rust
pub enum SortDirection { Ascending, Descending }   // serde "asc" / "desc"
pub enum NullOrder { First, Last }                 // serde "nulls-first" / "nulls-last"
pub struct SortField {
    pub source_id: i32,
    pub transform: Transform,
    pub direction: SortDirection,
    pub null_order: NullOrder,
}
pub struct SortOrder { pub order_id: i64, pub fields: Vec<SortField> }
pub type SortOrderRef = std::sync::Arc<SortOrder>;
impl SortOrder { pub fn is_unsorted(&self) -> bool; }
```

Serde names: `order-id`, `fields`, `source-id`, `transform`, `direction`,
`null-order`. Order id `0` with no fields is the unsorted order.

### 4.6 Snapshots — "Snapshots", "Snapshot References"

```rust
pub enum Operation { Append, Replace, Overwrite, Delete }   // serde lower-case
pub struct Summary { pub operation: Operation, /* other properties */ }
pub struct Snapshot { /* private */ }
pub type SnapshotRef = std::sync::Arc<Snapshot>;
impl Snapshot {
    pub fn snapshot_id(&self) -> i64;
    pub fn parent_snapshot_id(&self) -> Option<i64>;
    pub fn sequence_number(&self) -> i64;
    pub fn manifest_list_file_path(&self) -> Result<&str>;
    pub fn summary(&self) -> &Summary;
    pub fn timestamp(&self) -> chrono::DateTime<chrono::Utc>;
    pub fn schema_id(&self) -> Option<i32>;
    pub fn schema(&self, table_metadata: &TableMetadata) -> Result<SchemaRef>;
}
pub struct SnapshotLog { pub snapshot_id: i64, pub timestamp_ms: i64 }
pub struct MetadataLog { /* metadata-file, timestamp-ms */ }
```

A `summary.operation` outside the four documented operations must fail to
deserialise.

### 4.7 Table metadata — "Table Metadata", "Table Metadata Fields", Appendix C/E

```rust
pub enum FormatVersion { V1 = 1, V2 = 2 }
pub struct TableMetadata { /* private */ }        // PartialEq, serde
impl TableMetadata {
    pub fn format_version(&self) -> FormatVersion;
    pub fn uuid(&self) -> uuid::Uuid;
    pub fn location(&self) -> &str;
    pub fn last_sequence_number(&self) -> i64;
    pub fn last_updated_ms(&self) -> chrono::DateTime<chrono::Utc>;
    pub fn schemas_iter(&self) -> impl Iterator<Item = &SchemaRef>;
    pub fn schema_by_id(&self, schema_id: i32) -> Option<&SchemaRef>;
    pub fn current_schema(&self) -> &SchemaRef;
    pub fn partition_specs_iter(&self) -> impl Iterator<Item = &PartitionSpecRef>;
    pub fn partition_spec_by_id(&self, spec_id: i32) -> Option<&PartitionSpecRef>;
    pub fn default_partition_spec(&self) -> Option<&PartitionSpecRef>;
    pub fn snapshots(&self) -> impl Iterator<Item = &SnapshotRef>;
    pub fn snapshot_by_id(&self, snapshot_id: i64) -> Option<&SnapshotRef>;
    pub fn current_snapshot(&self) -> Option<&SnapshotRef>;
    pub fn history(&self) -> &[SnapshotLog];
    pub fn sort_orders_iter(&self) -> impl Iterator<Item = &SortOrderRef>;
    pub fn sort_order_by_id(&self, sort_order_id: i64) -> Option<&SortOrderRef>;
    pub fn default_sort_order(&self) -> Option<&SortOrderRef>;
    pub fn properties(&self) -> &std::collections::HashMap<String, String>;
}
```

Deserialisation must accept **both** documented layouts:

- **v1** — a single `schema` and `partition-spec` (a bare field list), an
  optional `schemas`/`partition-specs`, `current-snapshot-id: -1` meaning "no
  current snapshot". The single v1 `schema` becomes the current schema and the
  single v1 `partition-spec` becomes the default spec.
- **v2** — `schemas` + `current-schema-id`, `partition-specs` +
  `default-spec-id`, `sort-orders` + `default-sort-order-id`,
  `last-sequence-number`, `refs`.

A `format-version` other than 1 or 2 must fail to deserialise. Round-tripping
a v2 document through `TableMetadata` and back to JSON must preserve
`format-version`, `last-sequence-number`, `current-schema-id`,
`default-spec-id`, `default-sort-order-id`, `current-snapshot-id` and `refs`,
and `TableMetadata: PartialEq` must hold across the round trip.

### 4.8 Manifests and manifest lists — "Manifests", "Manifest Lists", Appendix A

The drivers construct entries and data files with exactly the setter chains
shown below and then call `.build()`. **Any field those chains do not name must
carry a default and must not be required**: the entry's `snapshot_id`,
`sequence_number` and `file_sequence_number`, and the data file's
`sort_order_id`, are never set by a driver. The same holds of `SchemaBuilder`,
which the drivers use without ever calling `with_identifier_field_ids`. A
`.build()` that rejects one of these as missing fails to compile or panics in
the gate.

```rust
pub enum ManifestContentType { Data = 0, Deletes = 1 }   // FromStr + ToString
pub enum ManifestStatus { Existing = 0, Added = 1, Deleted = 2 }
pub enum DataContentType { Data = 0, PositionDeletes = 1, EqualityDeletes = 2 }
pub enum DataFileFormat { Avro, Orc, Parquet }           // FromStr + ToString

pub struct Manifest { /* private */ }
impl Manifest {
    pub fn new(metadata: ManifestMetadata, entries: Vec<ManifestEntry>) -> Self;
    pub fn parse_avro(bs: &[u8]) -> Result<Self>;
    pub fn entries(&self) -> &[ManifestEntryRef];
}

pub struct ManifestMetadata { /* private */ }
// typed builder, consuming, all five setters required:
//   ManifestMetadata::builder()
//       .schema(Schema).schema_id(i32).partition_spec(PartitionSpec)
//       .content(ManifestContentType).format_version(FormatVersion)
//       .build()   // -> ManifestMetadata

pub struct ManifestEntry { /* private */ }
pub type ManifestEntryRef = std::sync::Arc<ManifestEntry>;
// typed builder, consuming:
//   ManifestEntry::builder().status(ManifestStatus).data_file(DataFile)
//       .build()   // -> ManifestEntry
impl ManifestEntry {
    pub fn is_alive(&self) -> bool;
    pub fn content_type(&self) -> DataContentType;
    pub fn file_path(&self) -> &str;
    pub fn sequence_number(&self) -> Option<i64>;
    pub fn file_size_in_bytes(&self) -> u64;
}

pub struct DataFile { /* private */ }
// typed builder, consuming, with these setters:
//   DataFile::builder()
//   .content(DataContentType) .file_path(String) .file_format(DataFileFormat)
//   .partition(Struct) .record_count(u64) .file_size_in_bytes(u64)
//   .column_sizes(HashMap<i32,u64>) .value_counts(HashMap<i32,u64>)
//   .null_value_counts(HashMap<i32,u64>) .nan_value_counts(HashMap<i32,u64>)
//   .lower_bounds(HashMap<i32,Literal>) .upper_bounds(HashMap<i32,Literal>)
//   .key_metadata(Vec<u8>) .split_offsets(Vec<i64>) .equality_ids(Vec<i32>)
//   .build()   // -> DataFile

pub struct ManifestWriter { /* private */ }
impl ManifestWriter {
    pub fn new(output: OutputFile, snapshot_id: i64, key_metadata: Vec<u8>) -> Self;
    pub async fn write(self, manifest: Manifest) -> Result<ManifestListEntry>;
}

pub struct ManifestListEntry {          // public fields, struct-literal constructed
    pub manifest_path: String,
    pub manifest_length: i64,
    pub partition_spec_id: i32,
    pub content: ManifestContentType,
    pub sequence_number: i64,
    pub min_sequence_number: i64,
    pub added_snapshot_id: i64,
    pub added_data_files_count: Option<u32>,
    pub existing_data_files_count: Option<u32>,
    pub deleted_data_files_count: Option<u32>,
    pub added_rows_count: Option<u64>,
    pub existing_rows_count: Option<u64>,
    pub deleted_rows_count: Option<u64>,
    pub partitions: Vec<FieldSummary>,
    pub key_metadata: Vec<u8>,
}

pub struct FieldSummary {               // "Manifest Lists" `field_summary`
    pub contains_null: bool,
    pub contains_nan: Option<bool>,
    pub lower_bound: Option<Literal>,
    pub upper_bound: Option<Literal>,
}

pub struct ManifestList { /* private */ }
impl ManifestList {
    pub fn parse_with_version(
        bs: &[u8],
        version: FormatVersion,
        partition_type_provider: impl Fn(i32) -> Result<Option<StructType>>,
    ) -> Result<ManifestList>;
    pub fn entries(&self) -> &[ManifestListEntry];
}

pub struct ManifestListWriter { /* private */ }
impl ManifestListWriter {
    pub fn v1(output_file: OutputFile, snapshot_id: i64, parent_snapshot_id: i64) -> Self;
    pub fn v2(output_file: OutputFile, snapshot_id: i64, parent_snapshot_id: i64,
              sequence_number: i64) -> Self;
    pub fn add_manifest_entries(
        &mut self, entries: impl Iterator<Item = ManifestListEntry>) -> Result<()>;
    pub async fn close(self) -> Result<()>;
}
```

`ManifestMetadata`, `ManifestEntry` and `DataFile` are built by **typed**
builders: every setter consumes and returns the builder, and `.build()` returns
the value itself — `ManifestMetadata`, `ManifestEntry` and `DataFile` — not a
`Result`. The drivers never unwrap it. (Contrast `SchemaBuilder::build()`,
which *is* `-> Result<Schema>`; see 4.2.)

`ManifestContentType` and `DataFileFormat` both implement `FromStr` (with
`type Err = Error`) and `ToString` over the textual forms the specification
uses — `data`/`deletes` for manifest content (the `content` property of the
"Manifests" key-value metadata table) and `avro`/`orc`/`parquet` for a data
file format ("Manifest Entry Fields", `file_format`; PROFILE.md P4 closes that
set at three) — and `to_string()` produces those lower-case forms. A string
that names none of them must **fail** to parse, as 4.4 requires of
`Transform`.

File-level obligations the drivers check. They decode the **Avro object
container header** — the `Obj\x01` magic, then the file's key-value metadata
map, whose `avro.schema` entry is the writer schema as JSON — and assert on the
record and column names they find there.

- Both files are **Avro object container files**: the first four bytes are
  `Obj\x01`, and the header is readable without decompressing the data blocks
  (PROFILE.md P8).
- The **manifest** file's writer schema is a record named `manifest_entry`
  ("Manifests") whose columns include `status`, `snapshot_id` and `data_file`,
  and whose `data_file` column is a nested record with the columns
  `content`, `file_path`, `file_format`, `partition`, `record_count` and
  `file_size_in_bytes` ("Manifest Entry Fields"). Whether the Avro schema also
  carries Appendix A `field-id` attributes is **out of scope** — see section 8.
- The **manifest** file's Avro key-value metadata carries the six properties
  the "Manifests" table lists: `schema` (the JSON of the table schema, which
  must parse back into a `Schema`), `schema-id`, `partition-spec`,
  `partition-spec-id`, `format-version` and `content` (`"data"` or
  `"deletes"`), all as strings.
- The **manifest list** file's writer schema is a record named `manifest_file`
  ("Manifest Lists") with these fifteen columns: `manifest_path`,
  `manifest_length`, `partition_spec_id`, `content`, `sequence_number`,
  `min_sequence_number`, `added_snapshot_id`, `added_data_files_count`,
  `existing_data_files_count`, `deleted_data_files_count`, `added_rows_count`,
  `existing_rows_count`, `deleted_rows_count`, `partitions`, `key_metadata`.
  Mind the spelling of the three count columns: this milestone spells them
  `added_data_files_count` / `existing_data_files_count` /
  `deleted_data_files_count`, **not** the "Manifest Lists" table's
  `added_files_count` / `existing_files_count` / `deleted_files_count`. That is
  a deliberate deviation from the specification table at milestone 1 — the
  names in this document are what is required, and they are also the
  `ManifestListEntry` field names above; milestone 2 renames all three to the
  specification's spelling. Every other column name is the table's own.
  In a **v1** list the three v2-only columns — `content`, `sequence_number`,
  `min_sequence_number` — are absent, as the "Manifest Lists" table's `v1`
  column shows.
- The **manifest list** file's Avro key-value metadata records, **all as
  strings**, the values the writer was constructed from: `snapshot-id` and
  `parent-snapshot-id` from both constructors, `sequence-number` from `v2`
  only, and `format-version` — `"1"` from `ManifestListWriter::v1` and `"2"`
  from `v2`. The Avro object container metadata map is `map<bytes>`, so "as a
  string" is the choice that matters: each value is the **decimal text** of the
  number in UTF-8 (`snapshot-id` 100 is the three bytes `"100"`), never its
  binary or zig-zag encoding — the same rule as the manifest's six properties
  above.
- `ManifestWriter::write` returns the `ManifestListEntry` describing the file
  it just wrote, with `added_snapshot_id` set to the writer's snapshot id and
  `added_data_files_count`, `existing_data_files_count` and
  `deleted_data_files_count` counting the entries with status `Added`,
  `Existing` and `Deleted` respectively.
- "Sequence Number Inheritance": a newly added entry is written with a null
  data sequence number, so after `Manifest::parse_avro` — which has no manifest
  list to inherit from — `ManifestEntry::sequence_number()` is `None`.
- `ManifestList::parse_with_version` calls `partition_type_provider` **once per
  entry**, with that entry's own `partition_spec_id` — once per entry whatever
  the entry contains, not once per partition summary and not once per bound.
  The returned `StructType` types that entry's `partitions`: `FieldSummary`'s
  `lower_bound` and `upper_bound` are the Appendix D **binary** form of a value
  of the corresponding partition field's type, and are decoded into `Literal`s
  in spec order. An entry that carries partition summaries but for which the
  provider returns `None` must **fail** to parse, because the bounds cannot be
  typed.
- An entry whose `partitions` list is **empty** carries no partition summaries
  at all: it is written with the optional `partitions` column absent and read
  back as an empty list. Such an entry needs no partition type, so it parses
  even when the provider returns `None` for its `partition_spec_id`.

### 4.9 Values — Appendix D

```rust
pub enum PrimitiveLiteral { /* one variant per PrimitiveType */ }
pub enum Literal { Primitive(PrimitiveLiteral), Struct(Struct), List(..), Map(..) }
impl Literal {
    pub fn bool<T: Into<bool>>(t: T) -> Self;
    pub fn int<T: Into<i32>>(t: T) -> Self;
    pub fn long<T: Into<i64>>(t: T) -> Self;
    pub fn float<T: Into<f32>>(t: T) -> Self;
    pub fn double<T: Into<f64>>(t: T) -> Self;
    pub fn string<S: ToString>(s: S) -> Self;
    pub fn uuid(uuid: uuid::Uuid) -> Self;
    pub fn binary<I: IntoIterator<Item = u8>>(input: I) -> Self;
    pub fn fixed<I: IntoIterator<Item = u8>>(input: I) -> Self;
    pub fn decimal(decimal: i128) -> Self;
    pub fn try_from_bytes(bytes: &[u8], data_type: &Type) -> Result<Self>;
    pub fn try_from_json(value: serde_json::Value, data_type: &Type) -> Result<Option<Self>>;
    pub fn try_into_json(self, r#type: &Type) -> Result<serde_json::Value>;
}
pub struct Struct { /* private */ }
impl Struct { pub fn empty() -> Self; }
```

- `try_from_json(Value::Null, _)` returns `Ok(None)`.
- `decimal(v)` takes the **unscaled** value — Appendix D's "unscaled value".
  The scale belongs to the `Type`, not to the literal, so `decimal(1420)` is
  `14.20` against `decimal(9,2)` and `142.0` against `decimal(9,1)`, and two
  literals built from the same unscaled value compare **equal** whatever scale
  the type they are serialised against carries.
- JSON forms follow Appendix D "JSON single-value serialization": `true`,
  `34`, `1.0`, `"iceberg"`, `"2017-11-16"`, `"22:31:08.123456"`,
  `"2017-11-16T22:31:08.123456"`, `"2017-11-16T22:31:08.123456+00:00"`,
  `"f79c3e09-677c-4bbd-a479-3f349cb785e7"`, `"14.20"`; and round-trip through
  `try_into_json` unchanged.
- Binary forms follow Appendix D "Binary single-value serialization":
  boolean `0x00`/`0x01`, int/long/float/double little-endian, string as raw
  UTF-8 with no length prefix, uuid as 16 big-endian bytes, fixed and binary
  as the bytes themselves.

### 4.10 File access — `iceberg::io`

```rust
pub struct FileIOBuilder { /* private */ }
impl FileIOBuilder {
    pub fn new(scheme_str: impl ToString) -> Self;
    pub fn new_fs_io() -> Self;
    pub fn with_prop(self, key: impl ToString, value: impl ToString) -> Self;
    pub fn build(self) -> Result<FileIO>;
}
pub struct FileIO { /* private, Clone */ }
impl FileIO {
    pub fn new_input(&self, path: impl AsRef<str>) -> Result<InputFile>;
    pub fn new_output(&self, path: impl AsRef<str>) -> Result<OutputFile>;
    pub async fn is_exist(&self, path: impl AsRef<str>) -> Result<bool>;
    pub async fn delete(&self, path: impl AsRef<str>) -> Result<()>;
}
pub struct InputFile { /* private */ }
pub struct OutputFile { /* private */ }
impl OutputFile { pub fn location(&self) -> &str; }
impl InputFile  { pub fn location(&self) -> &str; }
```

The only storage scheme that must work at this milestone is the **local
filesystem** (PROFILE.md P3), and there are three ways to name it, all of which
`build()` must accept: `FileIOBuilder::new_fs_io()`, `FileIOBuilder::new("file")`
and `FileIOBuilder::new("")` — the empty scheme string means the local
filesystem too. The drivers build a `FileIO` all three ways and expect the
three to address the same files. No other scheme string is required of you
(S3, GCS, Azure and OSS are out of scope, P3), and the drivers never pass one.

`with_prop` carries backend configuration. A property the backend it builds
does not recognise is **ignored**, not rejected: `build()` still succeeds. The
drivers pass one such property to check that.

The resulting `FileIO` accepts plain absolute paths (`/tmp/x/manifest.avro`)
**and** `file://` URIs, and both name the same file (P3). `InputFile::location()`
and `OutputFile::location()` report the location the handle was created with,
**unchanged** — a `file://` prefix is echoed back, not normalised away. The
drivers write files into a `tempfile::TempDir` through this `FileIO` and read
them back with `std::fs::read`, so the bytes on disk are the bytes the
specification defines.

## 5. Migration from the previous milestone

None — this is the first milestone. The workspace you start from contains no
implementation.
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

- Scan planning, predicate binding, expression evaluators and any `scan`
  module — milestone 2.
- Arrow or Parquet reading and writing of data files.
- `TableMetadataBuilder` and the `TableUpdate`/`TableRequirement` algebra —
  milestone 3.
- Row-level delete files, delete vectors and Puffin — milestone 4.
- Transactions, catalogs and commit conflict resolution — milestone 5.
- Nanosecond timestamp types (`timestamp_ns`, `timestamptz_ns`), the `variant`,
  `geometry`, `geography` and `unknown` types, and every other format-version-3
  addition.
- The **JSON** single-value form of `fixed` and `binary`, and the **binary**
  single-value form of `decimal`: out of scope at this milestone.
- Appendix A `field-id` attributes inside the Avro writer schema of **either**
  file. Both files' record and column names and their Avro key-value metadata
  are required (see 4.8); how the writer schema conveys Iceberg field ids is
  out of scope at this milestone. This also settles `PROFILE.md` P10 for milestone 1: P10
  requires the specification's reserved manifest and manifest-list column ids
  "wherever a milestone's contract puts those columns in scope", and the Avro
  writer schema is the only place those ids would appear, so **P10's manifest
  and manifest-list clause is not in scope here**. (P10's position-delete
  reserved ids belong to milestone 4.)
- Whether `Schema::highest_field_id()` (4.2) counts ids nested inside a
  `struct`, `list` or `map` field: only flat schemas are in scope at this
  milestone, and the two readings agree on those.
- The `Literal::Struct`, `Literal::List` and `Literal::Map` variants beyond
  their presence in the enum 4.9 declares: neither their construction nor
  their Appendix D JSON or binary forms is in scope at this milestone. Every
  value assertion in scope is over a **primitive** literal, and the only `Struct`
  a driver builds is `Struct::empty()`.
- Everything about the `Transform::Unknown` **variant** except its textual form
  (4.4): its `result_type`, `preserves_order`, `satisfies_order_of` and
  `dedup_name` are unspecified here, because the specification's "Partition
  Transforms" table has no row for it. This is the transform placeholder, not
  the format-version-3 `unknown` *type* excluded above.
- Every storage backend but the local filesystem, and every scheme string but
  the ones 4.10 names for it: `FileIO` need only reach local files at this
  milestone (PROFILE.md P3). Object-store schemes arrive at milestone 2.
- Row lineage, table statistics, partition statistics, encryption and key
  metadata contents.
