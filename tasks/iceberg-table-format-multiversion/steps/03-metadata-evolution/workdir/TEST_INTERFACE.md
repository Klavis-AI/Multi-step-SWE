# TEST_INTERFACE.md — milestone 03 (metadata evolution)

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
| Package version | `0.4.0` |
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

Milestones 1 and 2's `spec`, `io`, `expr`, `transform`, `scan` and `table`
exports stay required and are re-exercised by the gate. This milestone adds the
metadata-update algebra — a builder that turns a `TableMetadata` into a new
`TableMetadata` while recording the updates it applied — plus the nanosecond
timestamp types and a bound/unbound split of the partition spec. Several
milestone-2 signatures change (section 5).

**Standard traits on the value types.** The drivers compare values with
`assert_eq!`/`assert_ne!`, format them into failure messages, and clone them, so
every *value* type this section names must implement `std::fmt::Debug`, `Clone`
and `PartialEq` on top of whatever traits are listed beside it. The gate depends
on this as much as the in-scope groups do: it formats a `TableMetadata` and a
`DataContentType` into its failure messages and compares them. The types are
`TableMetadata`, `TableMetadataBuilder`, `TableMetadataBuildResult`,
`TableUpdate`, `TableRequirement`, `TableCreation`, `MetadataLog`, `SnapshotLog`,
`Snapshot`, `SnapshotReference`, `SnapshotRetention`, `Summary`, `Operation`,
`Schema`, `NestedField`, `Type`, `PrimitiveType`, `StructType`, `Struct`,
`Datum`, `PrimitiveLiteral`, `Literal`, `PartitionSpec`, `PartitionField`,
`UnboundPartitionSpec`, `UnboundPartitionField`, `SortOrder`, `SortField`,
`SortDirection`, `NullOrder`, `FormatVersion`, `Manifest`, `ManifestMetadata`,
`ManifestEntry`, `ManifestList`, `ManifestFile`, `FieldSummary`,
`DataFile`, `DataContentType`, `DataFileFormat`, `ManifestContentType`,
`ManifestStatus`, `StatisticsFile`, `BlobMetadata`, `PartitionStatisticsFile`,
`TableIdent` and `NamespaceIdent`. `Snapshot` in particular must be `Clone`,
because the builder is handed one by value more than once.

Three named types are **exempt**, and the exemption is what this milestone
requires rather than an omission: `TableMetadataBuilder` is `Debug + Clone` and
needs no `PartialEq` (section 4.1); `TableRequirement` is `Debug + PartialEq`
and needs no `Clone` (section 4.5); and `TableCreation` needs only `Debug`
(section 4.6). Everything else outside the list — the builders that consume
`self`, the writers, `Table` and the `io` handles — needs none of those traits
either, beyond the `Clone` on `FileIO` milestone 1 states. Where a type below
carries its own trait comment, that comment is **additive**: it names the
traits this milestone adds *on top of* the three above, never a complete derive
list. Section 8 excludes the *text* `Debug` and `Display` produce; it never
excludes the impls.

```rust
pub mod spec;      // milestones 1-2, extended below
pub mod io;        // milestone 1
pub mod expr;      // milestone 2
pub mod transform; // milestone 2
pub mod scan;      // milestone 2, one signature changed (section 5)
pub mod table;     // milestone 2
pub mod arrow;     // milestone 2
pub use error::{Error, ErrorKind, Result};

// Crate-root re-exports. Note that these are NOT under `spec`:
pub use catalog::{
    Catalog, Namespace, NamespaceIdent, TableCommit, TableCreation, TableIdent,
    TableRequirement, TableUpdate, ViewCreation,
};
```

`TableMetadataBuilder` and `TableMetadataBuildResult` are reachable at
`iceberg::spec::TableMetadataBuilder` — they may live in a private submodule so
long as `spec` re-exports them. `TableUpdate`, `TableRequirement` and
`TableCreation` are reachable at the **crate root** (`iceberg::TableUpdate`),
not under `spec`; the drivers import them from there. `TableIdent` is also
imported from the crate root, by the gate. The remaining five names in that
re-export list — `Catalog`, `Namespace`, `NamespaceIdent`, `TableCommit` and
`ViewCreation` — are given for orientation only: nothing at this milestone
imports, constructs or checks them, and section 8 places catalogs, commits and
view updates at later milestones.

### 4.1 `TableMetadataBuilder` — "Table Metadata", "Table Metadata Fields"

Every method consumes `self` and returns an owned `Self` or `Result<Self>`;
none of them take `&mut self`. Order of calls matters: an item must be added
before it can be made current, and a partition spec or sort order binds
against the *current* schema, so a schema must be added first.

```rust
// iceberg::spec
pub struct TableMetadataBuilder { /* private */ }   // Debug + Clone

impl TableMetadataBuilder {
    /// Proxy id meaning "the item added by this set of updates".
    pub const LAST_ADDED: i32 = -1;

    pub fn new(schema: Schema,
               spec: impl Into<UnboundPartitionSpec>,
               sort_order: SortOrder,
               location: String,
               format_version: FormatVersion,
               properties: std::collections::HashMap<String, String>) -> Result<Self>;
    pub fn new_from_metadata(previous: TableMetadata,
                             current_file_location: Option<String>) -> Self;
    pub fn from_table_creation(table_creation: TableCreation) -> Result<Self>;

    // identity and location
    pub fn assign_uuid(self, uuid: uuid::Uuid) -> Self;                 // NOT Result
    pub fn set_location(self, location: String) -> Self;                // NOT Result
    pub fn upgrade_format_version(self, format_version: FormatVersion) -> Result<Self>;

    // properties
    pub fn set_properties(self, properties: std::collections::HashMap<String, String>) -> Result<Self>;
    pub fn remove_properties(self, properties: &[String]) -> Result<Self>;

    // schemas
    pub fn add_schema(self, schema: Schema) -> Self;                    // NOT Result
    pub fn set_current_schema(self, schema_id: i32) -> Result<Self>;
    pub fn add_current_schema(self, schema: Schema) -> Result<Self>;

    // partition specs
    pub fn add_partition_spec(self, unbound_spec: UnboundPartitionSpec) -> Result<Self>;
    pub fn set_default_partition_spec(self, spec_id: i32) -> Result<Self>;
    pub fn add_default_partition_spec(self, unbound_spec: UnboundPartitionSpec) -> Result<Self>;
    pub fn remove_partition_specs(self, spec_ids: &[i32]) -> Result<Self>;

    // sort orders  (note the i64 order id)
    pub fn add_sort_order(self, sort_order: SortOrder) -> Result<Self>;
    pub fn set_default_sort_order(self, sort_order_id: i64) -> Result<Self>;

    // snapshots and refs
    pub fn add_snapshot(self, snapshot: Snapshot) -> Result<Self>;
    pub fn set_branch_snapshot(self, snapshot: Snapshot, branch: &str) -> Result<Self>;
    pub fn remove_snapshots(self, snapshot_ids: &[i64]) -> Self;        // NOT Result
    pub fn set_ref(self, ref_name: &str, reference: SnapshotReference) -> Result<Self>;
    pub fn remove_ref(self, ref_name: &str) -> Self;                    // NOT Result

    // statistics files
    pub fn set_statistics(self, statistics: StatisticsFile) -> Self;
    pub fn remove_statistics(self, snapshot_id: i64) -> Self;
    pub fn set_partition_statistics(self, partition_statistics_file: PartitionStatisticsFile) -> Self;
    pub fn remove_partition_statistics(self, snapshot_id: i64) -> Self;

    pub fn build(self) -> Result<TableMetadataBuildResult>;
}

pub struct TableMetadataBuildResult {
    pub metadata: TableMetadata,
    pub changes: Vec<TableUpdate>,
    pub expired_metadata_logs: Vec<MetadataLog>,
}
impl From<TableMetadataBuildResult> for TableMetadata {}

impl TableMetadata {
    pub fn into_builder(self, current_file_location: Option<String>) -> TableMetadataBuilder;
}
```

There is deliberately **no** public `add_default_sort_order`; the path the
drivers use is `add_sort_order(order)?.set_default_sort_order(-1)?`.

`TableMetadataBuilder::new_from_metadata(md, loc)` and `md.into_builder(loc)`
are **the same operation** reached from two sides: the drivers use them
interchangeably and require the two to produce the same builder.

Behavioural contracts the drivers assert:

- **`LAST_ADDED` (`-1`)** in `set_current_schema`, `set_default_partition_spec`
  and `set_default_sort_order` means "the schema / spec / sort order added by
  this set of updates". If nothing of that kind was added, the call is an
  error. This is an API contract, not a specification rule.
- **Fresh metadata.** `from_table_creation` requires a location (a `TableCreation`
  with `location: None` is an error), defaults the format version to **2**, the
  partition spec to the empty spec and the sort order to
  `SortOrder::unsorted_order()`. `new` and `from_table_creation` reassign the
  schema's field ids depth-first from **1** ("Schema Evolution"), force the
  schema id to **0**, and remap the supplied partition-spec and sort-field
  `source_id`s **and the schema's identifier field ids** to the new ids of the
  same columns **by column name** ("Identifier Field IDs"), so the identifier
  fields still name the same columns after reassignment; a partition or sort
  field naming a column that is not in the schema is an error. `set_location`
  (and creation) strips **every** trailing `/` from the location, so
  `"…/orders//"` and `"…/orders"` give the same `location()`. That is an API
  contract, not a specification rule: the "Table Metadata Fields" `location`
  row says only what the field means. Creation is itself a set of updates: `new` and
  `from_table_creation` reach the fresh metadata by applying `set-location`,
  `add-schema`, `set-current-schema`, `add-spec`, `set-default-spec`,
  `add-sort-order` and `set-default-sort-order` in that order, so `build()`
  records all seven in `changes` (plus `set-properties` when the creation
  supplies a non-empty property map).
- **Schema evolution** ("Schema Evolution"). `add_schema` assigns
  `highest_schema_id + 1`, but reuses an existing schema's id when the new
  schema's struct and identifier field ids are equal to it — in that case the
  schema list does not grow. `last-column-id` is raised to the new schema's
  highest field id and **never decreases**, so dropping columns does not lower
  it. `set_current_schema` with an id that is not in `schemas` is an error.
  Older schemas are retained so that older snapshots stay readable.
- **Partition evolution** ("Partition Evolution"). `add_partition_spec` binds
  the unbound spec against the current schema, assigning partition field ids
  from `last-partition-id + 1` — so the first spec of a fresh table gets
  **1000** and a later spec continues from where the previous one stopped. An
  unpartitioned table's `last-partition-id` is **999**. A new spec gets
  `highest_spec_id + 1`, unless a compatible spec already exists, in which case
  its id is reused. Older specs are retained. Errors: a spec that cannot bind
  to the current schema; `set_default_partition_spec` with an id that is not in
  `partition-specs`; `remove_partition_specs` naming the default spec; and, in
  a format-version-1 table, a spec whose field ids are not sequential from 1000
  ("Partition Evolution": v1 partition field ids "were assigned sequentially
  starting at 1000"; see also Appendix C, "Partition Specs").
- **Sort orders** ("Sorting"). Order id **0** is reserved for the unsorted
  order, so the first real sort order gets id 1; an unsorted order keeps 0 and
  does not grow the list. `add_sort_order` binds against the current schema and
  fails when a sort field's `source_id` is not in it. `set_default_sort_order`
  with an unknown id is an error.
- **Properties** ("Table Metadata Fields", `properties`). `set_properties`
  overwrites matching keys and leaves the rest; `remove_properties` ignores
  keys that are absent. Both **reject** the reserved keys, which are metadata
  fields rather than arbitrary properties and are never persisted as
  properties: `format-version`, `uuid`, `snapshot-count`,
  `current-snapshot-id`, `current-snapshot-summary`,
  `current-snapshot-timestamp-ms`, `current-schema`, `default-partition-spec`,
  `default-sort-order`.
- **Snapshots and refs** ("Snapshots", "Snapshot References").
  `set_branch_snapshot(snapshot, "main")` adds the snapshot, points the `main`
  reference at it and therefore sets `current-snapshot-id`, raises
  `last-sequence-number` to the snapshot's sequence number, and appends an entry
  to `snapshot-log`. Setting a **tag** reference (or any branch other than
  `main`) does not move `current-snapshot-id` and does not touch
  `snapshot-log`, and neither does `add_snapshot` on its own: adding a snapshot
  only puts it in `snapshots`, and only a `main`-branch reference makes it
  current. `remove_snapshots` drops the named snapshots and, with them, any
  reference that pointed at one of them; the `snapshot-log` entries that name a
  removed snapshot are dropped too, so the log's last entry is still the
  current snapshot. `remove_ref("main")` clears `current-snapshot-id` and
  empties `snapshot-log`. Errors: a snapshot id that
  already exists; in a format-version-2 table, a snapshot with a parent whose
  sequence number is not greater than `last-sequence-number`; `set_ref` naming
  a snapshot that is not in the table.
- **Format version** (Appendix E, "Format version changes"). Format versions
  move forward only: upgrading 1 → 2 succeeds and records the update;
  downgrading 2 → 1 is an error; upgrading to the version already in force is a
  no-op. `FormatVersion` is `Ord` with `V1 < V2`.
- **The `changes` log.** `build()` returns the ordered `TableUpdate`s that were
  actually applied. Three of the methods above are **composites** of two of the
  recorded actions, and a call that performs both records both, in this order:
  `add_current_schema` → `add-schema` then `set-current-schema`;
  `add_default_partition_spec` → `add-spec` then `set-default-spec`;
  `set_branch_snapshot` → `add-snapshot` then `set-snapshot-ref`. There is no
  combined `TableUpdate` variant for any of the three.
  An operation that changes nothing records **nothing** —
  assigning the uuid the table already has, setting the location it already
  has, upgrading to the current format version, `set_properties` with an empty
  map, and `remove_properties` with an empty slice all leave `changes` empty.
  Replaying `changes` on the metadata the commit started from, via
  `TableUpdate::apply`, must reproduce the same metadata.
- **The metadata log** ("Table Metadata Fields", `metadata-log`).
  `into_builder(Some(path))` appends
  `MetadataLog { metadata_file: path, timestamp_ms: <the previous metadata's last-updated-ms> }`
  to `metadata-log` when `build()` runs; `into_builder(None)` leaves the log
  untouched. Entries beyond `write.metadata.previous-versions-max` (default
  100) are dropped and returned in `expired_metadata_logs`.

### 4.2 `TableMetadata` accessors and its serialized form

```rust
impl TableMetadata {
    pub fn format_version(&self) -> FormatVersion;
    pub fn uuid(&self) -> uuid::Uuid;
    pub fn location(&self) -> &str;
    pub fn last_sequence_number(&self) -> i64;
    pub fn next_sequence_number(&self) -> i64;
    pub fn last_updated_ms(&self) -> i64;
    pub fn last_updated_timestamp(&self) -> Result<chrono::DateTime<chrono::Utc>>;

    pub fn schemas_iter(&self) -> impl ExactSizeIterator<Item = &SchemaRef>;
    pub fn schema_by_id(&self, schema_id: SchemaId) -> Option<&SchemaRef>;
    pub fn current_schema(&self) -> &SchemaRef;
    pub fn current_schema_id(&self) -> SchemaId;

    pub fn partition_specs_iter(&self) -> impl ExactSizeIterator<Item = &PartitionSpecRef>;
    pub fn partition_spec_by_id(&self, spec_id: i32) -> Option<&PartitionSpecRef>;
    pub fn default_partition_spec(&self) -> &PartitionSpecRef;   // NOT Option
    pub fn default_partition_spec_id(&self) -> i32;
    pub fn default_partition_type(&self) -> &StructType;

    pub fn snapshots(&self) -> impl ExactSizeIterator<Item = &SnapshotRef>;
    pub fn snapshot_by_id(&self, snapshot_id: i64) -> Option<&SnapshotRef>;
    pub fn current_snapshot(&self) -> Option<&SnapshotRef>;
    pub fn current_snapshot_id(&self) -> Option<i64>;
    pub fn snapshot_for_ref(&self, ref_name: &str) -> Option<&SnapshotRef>;
    pub fn history(&self) -> &[SnapshotLog];
    pub fn metadata_log(&self) -> &[MetadataLog];

    pub fn sort_orders_iter(&self) -> impl ExactSizeIterator<Item = &SortOrderRef>;
    pub fn sort_order_by_id(&self, sort_order_id: i64) -> Option<&SortOrderRef>;
    pub fn default_sort_order(&self) -> &SortOrderRef;
    pub fn default_sort_order_id(&self) -> i64;

    pub fn properties(&self) -> &std::collections::HashMap<String, String>;
    pub fn statistics_iter(&self) -> impl ExactSizeIterator<Item = &StatisticsFile>;
    pub fn partition_statistics_iter(&self) -> impl ExactSizeIterator<Item = &PartitionStatisticsFile>;
    pub fn statistics_for_snapshot(&self, snapshot_id: i64) -> Option<&StatisticsFile>;
    pub fn partition_statistics_for_snapshot(&self, snapshot_id: i64) -> Option<&PartitionStatisticsFile>;
}

pub struct SnapshotLog { pub snapshot_id: i64, pub timestamp_ms: i64 }
pub struct MetadataLog { pub metadata_file: String, pub timestamp_ms: i64 }
```

`next_sequence_number()` is the sequence number the **next** snapshot takes
("Sequence Numbers": `last-sequence-number` is the highest assigned sequence
number and each snapshot is assigned the next one). In a format-version-2
table it is `last_sequence_number() + 1`; a format-version-1 table does not
track sequence numbers, so it is **0** there.

There is **no** `last_column_id()` or `last_partition_id()` accessor. The
drivers observe those two fields through `serde_json::to_value(&metadata)`, so
the JSON key spellings **`"last-column-id"`** and **`"last-partition-id"`**
are part of the contract ("Table Metadata Fields"). `TableMetadata` remains
`Clone + Serialize + Deserialize + PartialEq` — the drivers clone a metadata to
run several independent updates from the same starting point — and metadata
produced by the builder must serialize to a metadata document that deserializes
back `==` to it.

### 4.3 Bound and unbound partition specs — "Partition Evolution", Appendix C

There are exactly **two** partition-spec types at this milestone: a bound
`PartitionSpec`, whose partition field ids are assigned, and an
`UnboundPartitionSpec`, whose field ids are optional. `PartitionSpec`'s fields
become private, and the builders lose their lifetime parameter.

```rust
pub struct PartitionSpec { /* private: spec_id, fields */ }
// section 4's standard traits (Debug + Clone + PartialEq), and additionally:
// Eq + Serialize + Deserialize
pub type PartitionSpecRef = std::sync::Arc<PartitionSpec>;

impl PartitionSpec {
    pub fn builder(schema: impl Into<SchemaRef>) -> PartitionSpecBuilder;
    pub fn spec_id(&self) -> i32;
    pub fn fields(&self) -> &[PartitionField];
    pub fn unpartition_spec() -> Self;                  // spec id 0, no fields
    pub fn is_unpartitioned(&self) -> bool;             // no fields, or all `void`
    pub fn partition_type(&self, schema: &Schema) -> Result<StructType>;
    pub fn into_unbound(self) -> UnboundPartitionSpec;
    pub fn with_spec_id(self, spec_id: i32) -> Self;    // consumes self
    pub fn has_sequential_ids(&self) -> bool;           // exactly 1000, 1001, 1002, ...
    pub fn highest_field_id(&self) -> Option<i32>;
    pub fn is_compatible_with(&self, other: &PartitionSpec) -> bool;
}

pub struct UnboundPartitionSpec { /* private: spec_id, fields */ }
// section 4's standard traits, and additionally:
// Eq + Default + Serialize + Deserialize
pub type UnboundPartitionSpecRef = std::sync::Arc<UnboundPartitionSpec>;

impl UnboundPartitionSpec {
    pub fn builder() -> UnboundPartitionSpecBuilder;
    pub fn bind(self, schema: impl Into<SchemaRef>) -> Result<PartitionSpec>;
    pub fn spec_id(&self) -> Option<i32>;
    pub fn fields(&self) -> &[UnboundPartitionField];
    pub fn with_spec_id(self, spec_id: i32) -> Self;    // consumes self
}

// Both field structs keep public fields.
pub struct PartitionField {
    pub source_id: i32, pub field_id: i32, pub name: String, pub transform: Transform,
}
impl PartitionField { pub fn into_unbound(self) -> UnboundPartitionField; }

pub struct UnboundPartitionField {
    pub source_id: i32, pub field_id: Option<i32>, pub name: String, pub transform: Transform,
}

impl UnboundPartitionSpecBuilder {          // schema-unaware; every method consumes self
    pub fn new() -> Self;                   // also Default
    pub fn with_spec_id(self, spec_id: i32) -> Self;
    /// First argument is the source **field id**.
    pub fn add_partition_field(self, source_id: i32, target_name: impl ToString,
                               transformation: Transform) -> Result<Self>;
    pub fn add_partition_fields(self,
        fields: impl IntoIterator<Item = UnboundPartitionField>) -> Result<Self>;
    pub fn build(self) -> UnboundPartitionSpec;         // infallible
}

impl PartitionSpecBuilder {                 // schema-aware; every method consumes self
    pub fn new(schema: impl Into<SchemaRef>) -> Self;
    pub fn new_from_unbound(unbound: UnboundPartitionSpec,
                            schema: impl Into<SchemaRef>) -> Result<Self>;
    pub fn with_last_assigned_field_id(self, last_assigned_field_id: i32) -> Self;
    pub fn with_spec_id(self, spec_id: i32) -> Self;
    /// First argument is the source **column name**.
    pub fn add_partition_field(self, source_name: impl AsRef<str>,
                               target_name: impl Into<String>,
                               transform: Transform) -> Result<Self>;
    pub fn add_unbound_field(self, field: UnboundPartitionField) -> Result<Self>;
    pub fn add_unbound_fields(self,
        fields: impl IntoIterator<Item = UnboundPartitionField>) -> Result<Self>;
    pub fn build(self) -> Result<PartitionSpec>;        // fallible
}
```

Note that the two `add_partition_field` methods take **different first
arguments**: the unbound builder takes a source field id, the bound builder
takes a source column name.

Behaviour from "Partition Evolution" and "Partition Transforms":

- Unassigned partition field ids are handed out from
  `with_last_assigned_field_id + 1`, whose default is **999**, so a fresh spec
  gets 1000, 1001, …. An explicitly supplied `field_id` is honoured and raises
  the running counter. `has_sequential_ids()` is true only for exactly
  1000, 1001, 1002, … from the start.
- `is_compatible_with` compares field **count, order, name, `source_id` and
  transform**, ignoring `spec_id` and `field_id`.
- Binding carries the spec id across: `UnboundPartitionSpec::bind` and
  `PartitionSpecBuilder::new_from_unbound` use the unbound spec's `spec_id`
  when it has one and **0** when it does not, and `into_unbound()` keeps the
  bound spec's `spec_id`, so it comes back as `Some(spec_id)`.
- `into_unbound()` also keeps each field's **assigned partition field id**:
  `PartitionField { field_id: 1000, .. }` becomes
  `UnboundPartitionField { field_id: Some(1000), .. }`. An unbound spec's field
  ids are `None` only when it was built without them — which is the usual case,
  because the unbound builder assigns none.
- `partition_type` produces a `StructType` whose fields are the partition field
  ids, named after the partition fields, typed by the transform's result type,
  and **always optional**.
- Both builders reject: an empty partition name; a duplicate partition name; a
  duplicate explicit partition `field_id`; and two fields on one source column
  whose `Transform::dedup_name()` values collide (`year`/`month` share one
  name, `day`/`hour` share another).
- The bound builder additionally rejects: a `source_id` that is not in the
  schema; a transform whose `result_type` is undefined for the source column's
  type; and a partition name equal to a schema column name, unless the field is
  an `identity` transform of that very column.
- Both builders reject **on the call that adds the field**, not at `build()`:
  `add_partition_field`, `add_partition_fields`, `add_unbound_field` and
  `add_unbound_fields` return the `Err` themselves, and the bound builder's
  `add_partition_field(source_name, ..)` is where an unknown column name
  surfaces. The bound `build()` is still `Result` because it hands out the
  remaining partition field ids, but none of the rejections above is deferred
  to it, and the unbound `build()` is infallible.

### 4.4 `TableUpdate` — the metadata-update documents

```rust
// iceberg::TableUpdate  (crate root)
#[derive(Debug, Serialize, Deserialize, PartialEq, Clone)]
#[serde(tag = "action", rename_all = "kebab-case")]
pub enum TableUpdate {
    UpgradeFormatVersion { format_version: FormatVersion },
    AssignUuid { uuid: uuid::Uuid },
    AddSchema { schema: Schema },
    SetCurrentSchema { schema_id: i32 },
    AddSpec { spec: UnboundPartitionSpec },
    SetDefaultSpec { spec_id: i32 },
    AddSortOrder { sort_order: SortOrder },
    SetDefaultSortOrder { sort_order_id: i64 },
    AddSnapshot { snapshot: Snapshot },
    SetSnapshotRef { ref_name: String, reference: SnapshotReference },  // reference is #[serde(flatten)]
    RemoveSnapshots { snapshot_ids: Vec<i64> },
    RemoveSnapshotRef { ref_name: String },
    SetLocation { location: String },
    SetProperties { updates: std::collections::HashMap<String, String> },
    RemoveProperties { removals: Vec<String> },
    RemovePartitionSpecs { spec_ids: Vec<i32> },
    SetStatistics { statistics: StatisticsFile },
    RemoveStatistics { snapshot_id: i64 },
    SetPartitionStatistics { partition_statistics: PartitionStatisticsFile },
    RemovePartitionStatistics { snapshot_id: i64 },
}

impl TableUpdate {
    /// Consumes both the update and the builder.
    pub fn apply(self, builder: TableMetadataBuilder) -> Result<TableMetadataBuilder>;
}
```

The serialized form is a JSON object with an `"action"` discriminator and
kebab-case keys, named after the "Table Metadata Fields" each update changes:

| `action` | payload keys |
| --- | --- |
| `upgrade-format-version` | `format-version` (integer 1 or 2) |
| `assign-uuid` | `uuid` |
| `add-schema` | `schema` |
| `set-current-schema` | `schema-id` (`-1` = last added) |
| `add-spec` | `spec` — the **unbound** spec the builder was handed, under the spec id it was assigned. Its `field-id`s are whatever that unbound spec carried: `null` for one the unbound builder produced, which is the form the drivers record and replay; `Some(..)` for one obtained from `into_unbound()` (section 4.3) |
| `set-default-spec` | `spec-id` (`-1` = last added) |
| `add-sort-order` | `sort-order` |
| `set-default-sort-order` | `sort-order-id` (`-1` = last added) |
| `add-snapshot` | `snapshot` |
| `set-snapshot-ref` | `ref-name`, plus the reference **flattened** into the same object: `snapshot-id`, `type` (`"branch"` or `"tag"`), and any retention keys (`min-snapshots-to-keep`, `max-snapshot-age-ms`, `max-ref-age-ms`) |
| `remove-snapshots` | `snapshot-ids` |
| `remove-snapshot-ref` | `ref-name` |
| `set-location` | `location` |
| `set-properties` | `updates` |
| `remove-properties` | `removals` |
| `remove-partition-specs` | `spec-ids` |
| `set-statistics` / `remove-statistics` | `statistics` / `snapshot-id` |
| `set-partition-statistics` / `remove-partition-statistics` | `partition-statistics` / `snapshot-id` |

Every variant must round-trip through `serde_json` unchanged. `apply` routes
each variant to the corresponding builder method, so an update the metadata
cannot accept (for example `set-current-schema` naming an unknown schema)
surfaces as an `Err` rather than producing invalid metadata.

### 4.5 `TableRequirement` — the commit requirements

```rust
// iceberg::TableRequirement  (crate root)
#[derive(Debug, Serialize, Deserialize, PartialEq)]
#[serde(tag = "type")]
pub enum TableRequirement {
    #[serde(rename = "assert-create")]
    NotExist,
    #[serde(rename = "assert-table-uuid")]
    UuidMatch { uuid: uuid::Uuid },                                  // key: "uuid"
    #[serde(rename = "assert-ref-snapshot-id")]
    RefSnapshotIdMatch { r#ref: String, snapshot_id: Option<i64> },  // keys: "ref", "snapshot-id"
    #[serde(rename = "assert-last-assigned-field-id")]
    LastAssignedFieldIdMatch { last_assigned_field_id: i32 },        // key: "last-assigned-field-id"
    #[serde(rename = "assert-current-schema-id")]
    CurrentSchemaIdMatch { current_schema_id: SchemaId },            // key: "current-schema-id"
    #[serde(rename = "assert-last-assigned-partition-id")]
    LastAssignedPartitionIdMatch { last_assigned_partition_id: i32 },// key: "last-assigned-partition-id"
    #[serde(rename = "assert-default-spec-id")]
    DefaultSpecIdMatch { default_spec_id: i32 },                     // key: "default-spec-id"
    #[serde(rename = "assert-default-sort-order-id")]
    DefaultSortOrderIdMatch { default_sort_order_id: i64 },          // key: "default-sort-order-id"
}

impl TableRequirement {
    /// Pass `None` when the table does not exist.
    pub fn check(&self, metadata: Option<&TableMetadata>) -> Result<()>;
}
```

The discriminator key is **`"type"`** (not `"action"`), and each variant's tag
is spelled out explicitly rather than derived from the variant name. Every
variant must round-trip through `serde_json` unchanged.

`check` semantics, from "Table Metadata Fields", "Schema Evolution",
"Partition Evolution" and "Snapshot References":

- `NotExist` is `Ok` only when `metadata` is `None`; every **other** variant is
  an `Err` when `metadata` is `None`.
- `UuidMatch`, `CurrentSchemaIdMatch`, `DefaultSpecIdMatch` and
  `DefaultSortOrderIdMatch` compare against `uuid()`, `current_schema_id()`,
  `default_partition_spec_id()` and `default_sort_order().order_id`.
- `LastAssignedFieldIdMatch` compares against the table's `last-column-id`;
  `LastAssignedPartitionIdMatch` against its `last-partition-id`. These are how
  a committer detects concurrent id assignment.
- `RefSnapshotIdMatch` with `snapshot_id: Some(id)` requires the named branch
  or tag to exist and to point at `id`. With `snapshot_id: None` it requires
  the reference **not** to exist, so it is an `Err` when the ref is present and
  `Ok` when it is absent.

### 4.6 Construction helpers the update algebra consumes

```rust
// iceberg::TableCreation  (crate root) — typed builder, all fields public
pub struct TableCreation {
    pub name: String,
    pub location: Option<String>,
    pub schema: Schema,
    pub partition_spec: Option<UnboundPartitionSpec>,
    pub sort_order: Option<SortOrder>,
    pub properties: std::collections::HashMap<String, String>,
}
// TableCreation::builder()
//   .name(String).location(String).schema(Schema)
//   .partition_spec(impl Into<UnboundPartitionSpec>).sort_order(SortOrder)
//   .properties(HashMap<String, String>)
//   .build()                                  // infallible
// The optional setters strip the Option (pass the bare value, not Some(..)).
// Only `.name` and `.schema` are required: `.location`, `.partition_spec`,
// `.sort_order` and `.properties` must all default when never called. Note
// that `properties` is a bare `HashMap`, not an `Option`, so "optional" here
// means "defaults to empty", not "strips an Option" — every construction in
// both drivers, the gate's included, omits it.

// iceberg::spec::Snapshot — typed builder, setters prefixed `with_`
// Snapshot::builder()
//   .with_snapshot_id(i64)
//   .with_parent_snapshot_id(Option<i64>)     // takes the Option itself
//   .with_sequence_number(i64)
//   .with_timestamp_ms(i64)
//   .with_manifest_list(impl Into<String>)
//   .with_schema_id(SchemaId)                 // bare id; the Option is stripped
//   .with_summary(Summary)
//   .build()                                  // infallible
impl Snapshot {
    pub fn snapshot_id(&self) -> i64;
    pub fn parent_snapshot_id(&self) -> Option<i64>;
    pub fn sequence_number(&self) -> i64;
    pub fn manifest_list(&self) -> &str;
    pub fn summary(&self) -> &Summary;
    pub fn timestamp(&self) -> Result<chrono::DateTime<chrono::Utc>>;
    pub fn timestamp_ms(&self) -> i64;
    pub fn schema_id(&self) -> Option<SchemaId>;
    pub fn schema(&self, table_metadata: &TableMetadata) -> Result<SchemaRef>;
    // No public `log()`: the `snapshot-log` entry a snapshot contributes is
    // read off `TableMetadata::history()` (section 8).
}
pub struct Summary {
    pub operation: Operation,
    pub additional_properties: std::collections::HashMap<String, String>,  // #[serde(flatten)]
}

pub const MAIN_BRANCH: &str = "main";
pub struct SnapshotReference { pub snapshot_id: i64, pub retention: SnapshotRetention }
impl SnapshotReference {
    pub fn new(snapshot_id: i64, retention: SnapshotRetention) -> Self;
    pub fn is_branch(&self) -> bool;
}
pub enum SnapshotRetention {
    Branch { min_snapshots_to_keep: Option<i32>,
             max_snapshot_age_ms: Option<i64>,
             max_ref_age_ms: Option<i64> },
    Tag { max_ref_age_ms: Option<i64> },
}
impl SnapshotRetention {
    pub fn branch(min_snapshots_to_keep: Option<i32>,
                  max_snapshot_age_ms: Option<i64>,
                  max_ref_age_ms: Option<i64>) -> Self;
    // No `tag` constructor is required; the drivers build the `Tag` variant
    // directly, as `SnapshotRetention::Tag { max_ref_age_ms }`.
}

// iceberg::spec::SortOrder — derive builder, setters prefixed `with`
pub struct SortOrder { pub order_id: i64, pub fields: Vec<SortField> }
impl SortOrder {
    pub fn builder() -> SortOrderBuilder;
    pub fn unsorted_order() -> SortOrder;
    pub fn is_unsorted(&self) -> bool;
    pub fn with_order_id(self, order_id: i64) -> SortOrder;   // consumes self
}
impl SortOrderBuilder {
    // .with_order_id(i64), .with_fields(Vec<SortField>), .with_sort_field(SortField)
    pub fn build_unbound(&self) -> Result<SortOrder>;         // takes &self
    pub fn build(&self, schema: &Schema) -> Result<SortOrder>;// takes &self
}
// SortField keeps the four public fields milestone 1 stated; the drivers read
// them directly off a built order. It is a typed builder with NO setter prefix.
pub enum SortDirection { Ascending, Descending }   // serde "asc" / "desc"
pub enum NullOrder { First, Last }                 // serde "nulls-first" / "nulls-last"
pub struct SortField {
    pub source_id: i32,
    pub transform: Transform,
    pub direction: SortDirection,
    pub null_order: NullOrder,
}
//   SortField::builder().source_id(i32).transform(Transform)
//       .direction(SortDirection).null_order(NullOrder).build()   // infallible
```

`SortOrder`'s reserved unsorted order id (`0`) need not be a public constant;
the drivers use `SortOrder::unsorted_order()` and the literal `0`.

The two statistics-file records the builder's statistics methods and the
`set-statistics` / `set-partition-statistics` updates carry, with the
kebab-case JSON keys "Table Metadata Fields" gives them (`statistics` and
`partition-statistics`):

```rust
// iceberg::spec
pub struct StatisticsFile {          // standard traits, plus Serialize + Deserialize
    pub snapshot_id: i64,
    pub statistics_path: String,
    pub file_size_in_bytes: i64,
    pub file_footer_size_in_bytes: i64,
    pub key_metadata: Option<String>,        // omitted from the JSON when None
    pub blob_metadata: Vec<BlobMetadata>,
}
pub struct BlobMetadata {            // standard traits, plus Serialize + Deserialize
    pub r#type: String,                     // JSON key "type"
    pub snapshot_id: i64,
    pub sequence_number: i64,
    pub fields: Vec<i32>,
    pub properties: std::collections::HashMap<String, String>,  // omitted when empty
}
pub struct PartitionStatisticsFile { // standard traits, plus Serialize + Deserialize
    pub snapshot_id: i64,
    pub statistics_path: String,
    pub file_size_in_bytes: i64,
}
```

Both records are keyed by `snapshot_id`: `set_statistics` replaces the entry
for that snapshot, `remove_statistics` drops it, and the two
`partition_statistics` methods do the same for the partition-statistics map.
Removing an entry that is not there records nothing. The **contents** of the
statistics files themselves are out of scope (section 8); only the records and
the maps they live in are.

### 4.7 Nanosecond timestamps — "Primitive Types", "Partition Transforms", Appendix D

```rust
pub enum PrimitiveType {
    Boolean, Int, Long, Float, Double,
    Decimal { precision: u32, scale: u32 },
    Date, Time, Timestamp, Timestamptz,
    TimestampNs,        // nanosecond precision, without zone
    TimestamptzNs,      // nanosecond precision, with zone
    String, Uuid, Fixed(u64), Binary,
}
impl PrimitiveType { pub fn compatible(&self, literal: &PrimitiveLiteral) -> bool; }

impl Datum {
    pub fn timestamp_nanos(value: i64) -> Self;      // -> TimestampNs
    pub fn timestamptz_nanos(value: i64) -> Self;    // -> TimestamptzNs
    pub fn try_from_bytes(bytes: &[u8], data_type: PrimitiveType) -> Result<Datum>;
    pub fn to_bytes(&self) -> Result<serde_bytes::ByteBuf>;   // CHANGED, see section 5
    // unchanged from milestone 2; the gate names `long` directly:
    pub fn long<T: Into<i64>>(t: T) -> Self;
    pub fn literal(&self) -> &PrimitiveLiteral;
    pub fn data_type(&self) -> &PrimitiveType;
}
```

`Display` for the two new variants must yield the specification's names,
**`"timestamp_ns"`** and **`"timestamptz_ns"`** ("Primitive Types"). They are
distinct types from `Timestamp` and `Timestamptz`, and
`Type::Primitive(..).is_primitive()` is true for both.

Result types from the "Partition Transforms" table, with a nanosecond
timestamp as the source: `year`, `month`, `hour` and `bucket[N]` all produce
`int`; `identity` and `void` keep the source type; `day` is a valid transform
over a nanosecond timestamp; `truncate[W]` is **not** defined over any
timestamp type and must be an error.

A nanosecond timestamp value is a **long** — `compatible` accepts
`PrimitiveLiteral::Long` and rejects `PrimitiveLiteral::Int`. Its Appendix D
"Binary single-value serialization" form is nanoseconds since the epoch in an
**8-byte little-endian long**, so `Datum::try_from_bytes(&nanos.to_le_bytes(),
PrimitiveType::TimestampNs)` yields a `Datum` whose `data_type()` is
`TimestampNs`, whose `literal()` is `PrimitiveLiteral::Long(nanos)`, and whose
`to_bytes()?` returns those same eight bytes. `to_bytes` is **fallible at this
milestone** — it returns `Result<serde_bytes::ByteBuf>`, not the bare
`ByteBuf` milestone 2 required (section 5).

A nanosecond timestamp column is a valid partition source: binding a spec that
applies `hour` to one assigns field id 1000 and yields a partition field of
type `int`.

### 4.8 Manifest, table and scan surface used by the gate

Milestone 1-2 signatures the gate still exercises, in the shapes required
here:

```rust
impl PartitionSpec { pub fn unpartition_spec() -> Self; }   // replaces struct-literal construction

impl ManifestMetadata {
    pub fn builder() -> ManifestMetadataBuilder;
    // .schema(SchemaRef)   <- an Arc<Schema>, not a Schema
    // .schema_id(SchemaId).partition_spec(PartitionSpec)
    // .content(ManifestContentType).format_version(FormatVersion).build()
}
impl ManifestWriter {
    pub fn new(output: OutputFile, snapshot_id: i64, key_metadata: Vec<u8>) -> Self;
    pub async fn write(self, manifest: Manifest) -> Result<ManifestFile>;
}
impl ManifestListWriter {
    pub fn v1(output_file: OutputFile, snapshot_id: i64,
              parent_snapshot_id: Option<i64>) -> Self;
    pub fn v2(output_file: OutputFile, snapshot_id: i64,
              parent_snapshot_id: Option<i64>, sequence_number: i64) -> Self;
    pub fn add_manifests(&mut self, manifests: impl Iterator<Item = ManifestFile>) -> Result<()>;
    pub async fn close(self) -> Result<()>;
}
pub struct ManifestFile {
    pub manifest_path: String,
    pub manifest_length: i64,
    pub partition_spec_id: i32,
    pub content: ManifestContentType,
    pub sequence_number: i64,
    pub min_sequence_number: i64,
    pub added_snapshot_id: i64,
    pub added_files_count: Option<u32>,
    pub existing_files_count: Option<u32>,
    pub deleted_files_count: Option<u32>,
    pub added_rows_count: Option<u64>,
    pub existing_rows_count: Option<u64>,
    pub deleted_rows_count: Option<u64>,
    pub partitions: Vec<FieldSummary>,
    pub key_metadata: Vec<u8>,
}
// DataFileBuilder::default(): required setters are content, file_path, file_format,
// partition, record_count, file_size_in_bytes; every other field defaults.
//   key_metadata is Option<Vec<u8>>   (pass None, not vec![])
//   build() -> Result<DataFile>

impl TableScan {
    pub fn column_names(&self) -> Option<&[String]>;   // None when nothing was selected
    pub fn snapshot(&self) -> &SnapshotRef;
    pub async fn plan_files(&self) -> Result<FileScanTaskStream>;
}

// iceberg::table::Table — the gate builds one and reads the location back.
// Table::builder()
//   .file_io(FileIO).metadata(impl Into<TableMetadataRef>).identifier(TableIdent)
//   .metadata_location(impl Into<String>)   // optional; the Option is stripped
//   .build() -> Result<Table>
impl Table {
    pub fn file_io(&self) -> &FileIO;
    pub fn metadata(&self) -> &TableMetadata;
    pub fn metadata_location(&self) -> Option<&str>;   // what was set above
    pub fn scan(&self) -> TableScanBuilder<'_>;        // unchanged from milestone 2
}
```

The gate builds a table end to end: metadata from `TableMetadataBuilder`, a
manifest and a manifest list written through the public writers into a
`FileIO`, the metadata document written through the same `FileIO`, then
`Table::builder()` and `table.scan()`. It requires that the metadata document
re-read from `FileIO` deserializes back `==` to the in-memory metadata, that
`ManifestList::parse_with_version` and `Manifest::parse_avro` read the written
files back with their entries and column bounds intact, and that `plan_files`
resolves and parses the current snapshot's manifest list **before** returning —
so a snapshot whose manifest list is missing is an error from `plan_files`
itself, and `TableScanBuilder::build` still rejects an unknown column, an
unbindable filter and an unknown `snapshot_id` ("Scan Planning").

Milestone 1-2 symbols the drivers and the gate name directly. Section 5 says
their form is unchanged; this roster is here because this document replaces the
earlier milestones' copy, so the names would otherwise be stated nowhere.

Both drivers open with `use iceberg::spec::*;`, so **every value type in the
block below is reached as `iceberg::spec::<Name>`** — a glob import of `spec`
must bring all of them into scope. Only the four crate-root names section 4
already calls out (`TableUpdate`, `TableRequirement`, `TableCreation`,
`TableIdent`) and the `io` / `expr` / `table` / `scan` items in the following
blocks live elsewhere.

```rust
// iceberg::spec
impl Schema {
    pub fn builder() -> SchemaBuilder;
    pub fn schema_id(&self) -> SchemaId;
    pub fn as_struct(&self) -> &StructType;
    pub fn highest_field_id(&self) -> i32;
    pub fn field_by_id(&self, field_id: i32) -> Option<&NestedFieldRef>;
    pub fn field_by_name(&self, field_name: &str) -> Option<&NestedFieldRef>;
    pub fn field_id_by_name(&self, name: &str) -> Option<i32>;   // dotted nested paths
    pub fn identifier_field_ids(&self) -> impl ExactSizeIterator<Item = i32> + '_;
}
// SchemaBuilder: .with_schema_id(i32), .with_fields(impl IntoIterator<Item = NestedFieldRef>),
//               .with_identifier_field_ids(impl IntoIterator<Item = i32>), .build() -> Result<Schema>
impl StructType { pub fn new(fields: Vec<NestedFieldRef>) -> Self;
                  pub fn fields(&self) -> &[NestedFieldRef]; }
pub struct NestedField {                // public fields, as at milestones 1-2
    pub id: i32,
    pub name: String,
    pub required: bool,
    pub field_type: Box<Type>,          // deref to &Type via `.as_ref()`
}
impl NestedField {
    pub fn required(id: i32, name: impl ToString, field_type: Type) -> Self;
    pub fn optional(id: i32, name: impl ToString, field_type: Type) -> Self;
}
impl Struct { pub fn empty() -> Self; }

pub enum FormatVersion { V1 = 1, V2 = 2 }   // serde: the bare integers 1 and 2
pub enum Type { Primitive(PrimitiveType), Struct(StructType), List(ListType), Map(MapType) }
pub enum Operation { Append, Replace, Overwrite, Delete }   // a snapshot Summary's operation
pub enum Transform { Identity, Bucket(u32), Truncate(u32), Year, Month, Day, Hour, Void, Unknown }
pub enum ManifestStatus      { Existing = 0, Added = 1, Deleted = 2 }
pub enum DataContentType     { Data = 0, PositionDeletes = 1, EqualityDeletes = 2 }
pub enum DataFileFormat      { Avro, Orc, Parquet }
pub enum ManifestContentType { Data = 0, Deletes = 1 }

impl ManifestEntry {                    // ManifestEntry::builder(): .status(..).data_file(..).build()
    pub fn is_alive(&self) -> bool;
    pub fn content_type(&self) -> DataContentType;
    pub fn file_path(&self) -> &str;
    pub fn data_file(&self) -> &DataFile;
}
impl DataFile {
    pub fn lower_bounds(&self) -> &std::collections::HashMap<i32, Datum>;
    pub fn upper_bounds(&self) -> &std::collections::HashMap<i32, Datum>;
}
// DataFileBuilder also takes the two bound maps, under the accessors' names:
//   .lower_bounds(HashMap<i32, Datum>).upper_bounds(HashMap<i32, Datum>)

pub type ManifestEntryRef = std::sync::Arc<ManifestEntry>;
impl Manifest      { pub fn entries(&self) -> &[ManifestEntryRef]; }
impl ManifestList  { pub fn entries(&self) -> &[ManifestFile];
    // the third argument is a partition-type provider, called per spec id:
    pub fn parse_with_version(bs: &[u8], version: FormatVersion,
        partition_type_provider: impl Fn(i32) -> Result<Option<StructType>>)
        -> Result<ManifestList>;
}

// iceberg::io — the gate writes and re-reads real files through these.
impl FileIO {                                       // FileIO: Clone
    pub fn from_path(path: impl AsRef<str>) -> Result<FileIOBuilder>;
    pub fn new_input(&self, path: impl AsRef<str>) -> Result<InputFile>;
    pub fn new_output(&self, path: impl AsRef<str>) -> Result<OutputFile>;
}
impl FileIOBuilder {
    pub fn new_fs_io() -> Self;
    pub fn build(self) -> Result<FileIO>;
}
impl InputFile  { pub async fn read(&self) -> Result<bytes::Bytes>; }
impl OutputFile { pub async fn write(&self, bs: bytes::Bytes) -> Result<()>; }

// iceberg::expr — the gate builds one predicate; milestone 2's other
// comparison constructors are unchanged and unused here.
impl Reference {
    pub fn new(name: impl Into<String>) -> Self;
    pub fn greater_than(self, datum: Datum) -> Predicate;
}
```

Neither `bytes` nor `serde_bytes` is among the crates section 3 lists, so the
drivers never name `bytes::Bytes` or `serde_bytes::ByteBuf`, and **neither
crate's version is part of this contract**. They rely on conversions only. The
gate writes with `out.write(serde_json::to_vec(..)?.into())`, so `Vec<u8>` must
convert into `OutputFile::write`'s argument type; it hands what
`InputFile::read` returns straight to `Manifest::parse_avro` and
`ManifestList::parse_with_version` as `&bytes`, so the returned value must
deref to a byte slice; and it reads what `Datum::to_bytes` returns through
`AsRef<[u8]>`, so that value need only deref to a byte slice too.

The roster above is complete for **names**: every symbol the drivers name is
either in it or in one of sections 4.1-4.7. The trait obligations those names
carry are stated separately, by section 4's standard-traits paragraph — the
roster's own comments never repeat them.

## 5. Migration from the previous milestone

Milestone 2's contract changes in the following ways. The new form is what is
required; the old form is not accepted.

| Milestone 2 | This milestone |
| --- | --- |
| `PartitionSpec::builder(schema: &Schema) -> PartitionSpecBuilder<'_>` | `PartitionSpec::builder(schema: impl Into<SchemaRef>) -> PartitionSpecBuilder` — the builder is owning and has no lifetime parameter |
| `PartitionSpecBuilder<'a>::new(schema: &'a Schema)` | `PartitionSpecBuilder::new(schema: impl Into<SchemaRef>)`, plus new `new_from_unbound(unbound, schema) -> Result<Self>` |
| `PartitionSpec::to_unbound(self)` | `PartitionSpec::into_unbound(self)` |
| `UnboundPartitionSpec::bind(self, schema: &Schema)` | `bind(self, schema: impl Into<SchemaRef>)` |
| `TableMetadata::default_partition_spec(&self) -> Option<&PartitionSpecRef>` | `-> &PartitionSpecRef` (the default spec is always present) |
| `TableMetadata::default_sort_order(&self) -> Option<&SortOrderRef>` | `-> &SortOrderRef` (the default sort order is always present) |
| `TableMetadata` exposes no id-only accessors | new: `current_schema_id`, `current_snapshot_id`, `default_partition_spec_id`, `default_partition_type`, `default_sort_order_id`, `metadata_log`, `snapshot_for_ref`, `next_sequence_number`; the `*_iter` methods are now `ExactSizeIterator` |
| `TableMetadata::last_updated_ms(&self) -> DateTime<Utc>` — the form milestone 1's contract stated, and milestone 2 restated `TableMetadata` as unchanged | `-> i64`, with `last_updated_timestamp(&self) -> Result<DateTime<Utc>>` for the `DateTime` form |
| `Datum::to_bytes(&self) -> serde_bytes::ByteBuf` | `-> Result<serde_bytes::ByteBuf>` — the Appendix D binary form is unchanged, but producing it is fallible |
| `Snapshot::timestamp(&self) -> DateTime<Utc>` | `-> Result<DateTime<Utc>>`, plus new `timestamp_ms(&self) -> i64` |
| `ManifestListWriter::v1(out, snapshot_id, parent_snapshot_id: i64)` | `v1(out, snapshot_id, parent_snapshot_id: Option<i64>)` |
| `ManifestListWriter::v2(out, snapshot_id, parent_snapshot_id: i64, sequence_number)` | `v2(out, snapshot_id, parent_snapshot_id: Option<i64>, sequence_number)` |
| `ManifestMetadata::builder().schema(Schema)` | `.schema(SchemaRef)` — an `Arc<Schema>` |
| `DataFile::key_metadata: Vec<u8>`, builder setter `.key_metadata(vec![])` | `Option<Vec<u8>>`, builder setter `.key_metadata(None)` |
| `TableScan::column_names(&self) -> &[String]` | `-> Option<&[String]>` (`None` when the scan selected nothing explicitly) |
| `PrimitiveType` has ten primitive names | adds `TimestampNs` and `TimestamptzNs` |

Three further forms are named here for a different reason: milestone 2's
contract never stated them, so they are not migrations, but a milestone-2
implementation may well carry them and it is worth saying where the required
form now lives.

- `TableMetadata` needs no `append_snapshot`; snapshots reach a table through
  `TableMetadataBuilder::add_snapshot` / `set_branch_snapshot` (section 4.1).
- `PartitionSpec` need not derive `Default`; the empty spec is
  `PartitionSpec::unpartition_spec()`, and `with_spec_id`,
  `has_sequential_ids`, `highest_field_id` and `is_compatible_with` are new at
  this milestone (section 4.3).
- `SortOrderBuilder::build` takes the schema **by reference** —
  `build(&self, schema: &Schema)` — alongside the new
  `build_unbound(&self) -> Result<SortOrder>` (section 4.6).

Unchanged from milestones 1 and 2 and still required in exactly the same form:
`Schema` and `SchemaBuilder`, `NestedField`, `StructType`, `Type` and the other
primitive names, `Transform` with its `FromStr`/`Display`/`result_type`/
`dedup_name`/`preserves_order`/`satisfies_order_of`, `Literal` (including the
Appendix D forms already required), `Datum`'s constructors, `literal()`,
`data_type()` and `try_from_bytes` — **but not `to_bytes`, whose return type
changes in the table above** — `SortDirection`/`NullOrder`/`SortField`,
`FormatVersion`, `Operation`,
`TableMetadata`'s serde form, `Manifest::{new, parse_avro, entries}`,
`ManifestEntry::builder()`, `ManifestWriter::{new, write}`,
`ManifestList::parse_with_version` (three arguments), `FileIO`/`FileIOBuilder`/
`InputFile`/`OutputFile`, `TableIdent::from_strs`, `iceberg::expr`'s
`Reference` and `Predicate`, and `TableScanBuilder::{select, with_filter,
snapshot_id, build}`.

`Table::builder()` keeps its required `file_io`, `metadata` and `identifier`
setters and its optional `metadata_location`, but its terminator changes:

| Milestone 2 | This milestone |
| --- | --- |
| `Table::builder()…build() -> Table` | `…build() -> Result<Table>` |
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

- Row-level delete files, delete vectors, Puffin files and blobs, metadata
  inspection tables and the object cache — milestone 4.
- Transactions, catalogs and commit conflict resolution — milestone 5. The
  update algebra is in scope, but nothing that *performs* a commit is: no
  `Catalog` implementation, no `TableCommit`, and no retry or conflict
  resolution. Section 4's crate-root re-export list therefore names `Catalog`,
  `Namespace`, `NamespaceIdent`, `TableCommit` and `ViewCreation` for
  orientation only; no requirement group and no gate case touches any of the five.
- The **JSON** type names of the nanosecond timestamp types: only their
  `Display` spellings (`timestamp_ns`, `timestamptz_ns`) are in scope, not how
  `PrimitiveType::TimestampNs`/`TimestamptzNs` serialise to or deserialise from
  a JSON type string.
- The Appendix D **JSON** single-value form of `timestamp_ns` and
  `timestamptz_ns` (`"2017-11-16T22:31:08.123456789"`): only the Appendix D
  **binary** form — an 8-byte little-endian long — is in scope.
- The result **type** of the `day` transform over a nanosecond timestamp: the
  drivers require only that `day` accepts a nanosecond timestamp as a source.
  The result types of `year`, `month`, `hour` and `bucket[N]` over those
  sources *are* in scope.
- Arrow and Parquet reading and writing of nanosecond timestamp columns, and
  every other format-version-3 addition (`variant`, `geometry`, `geography`,
  `unknown`, row lineage).
- View metadata **updates**: the read-only view metadata surface from
  milestone 2 stays required, but no view update algebra, no `ViewUpdate` type
  and no view metadata builder beyond what milestone 2 already covers.
- Schema field-id reassignment through anything other than the metadata
  builder's construction entry points: a standalone
  `Schema::reassign_field_ids` or a public reassigning `SchemaBuilder` setter
  is neither required nor in scope.
- Consuming the planned file-scan-task stream: the drivers assert that
  `plan_files` resolves and parses the snapshot's manifest list (and fails when
  it is missing), but they never collect the returned stream, so per-file
  pruning by partition summaries, partition data or column metrics is not
  in scope at this milestone.
- A third, schema-carrying partition-spec type: exactly two are required, the
  bound `PartitionSpec` and the `UnboundPartitionSpec`.
- Table statistics and partition statistics **files** themselves: the builder
  methods that record and remove them, and the `StatisticsFile`,
  `BlobMetadata` and `PartitionStatisticsFile` records section 4.6 defines,
  are part of the required surface, but no statistics file is ever read or
  written and no Puffin blob is parsed, so nothing about a file's *contents*
  is in scope.
- Snapshot expiry, metadata-log expiry beyond the
  `write.metadata.previous-versions-max` default, and snapshot retention policy
  enforcement. Section 4.1's `remove_snapshots` rule — the named snapshots go,
  any reference left pointing at one of them goes, and the `snapshot-log`
  entries naming a removed snapshot go with them — **is** required and in scope.
  What is out of scope is the harder shape of that repair: what a longer
  `snapshot-log` does when a removed snapshot's entry sits *between* two
  surviving ones, and whether `build()` then fails.
- A **public** `Snapshot::log()`. The `snapshot-log` entry a snapshot
  contributes is observed only through `TableMetadata::history()`, so whether
  the helper that builds one is part of your public surface is neither
  required nor in scope. Every other `Snapshot` accessor section 4.6 lists
  **is** required and is in scope.
- Encryption and key-metadata contents.
