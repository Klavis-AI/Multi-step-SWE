# TEST_INTERFACE.md — milestone 05 (transaction actions)

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
| Package version | `0.6.0` |
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

Milestones 1–4 keep every export they already had, and the gate re-exercises
them. This milestone adds the commit path: a catalog that owns a table's
metadata pointer, an in-core catalog implementation, and a `Transaction` that
turns a set of *actions* into the `TableUpdate`/`TableRequirement` pair a
catalog applies. It also moves the manifest-list partition bounds to raw byte
buffers and drops the partition-type callback from the manifest-list reader
(section 5).

```rust
pub mod spec;        // milestones 1-4, extended below
pub mod io;          // milestones 1-2
pub mod expr;        // milestone 2
pub mod transform;   // milestone 2
pub mod scan;        // milestone 2, one field and one builder method removed (section 5)
pub mod table;       // milestone 2
pub mod arrow;       // milestone 2
pub mod writer;      // milestone 4
pub mod inspect;     // milestone 4
pub mod cache;       // milestone 4
pub mod puffin;      // milestone 4
pub mod transaction; // NEW — Transaction and the transaction actions
pub use error::{Error, ErrorKind, Result};

// Crate-root re-exports (milestone 3), now including the in-core catalog:
pub use catalog::{
    Catalog, MemoryCatalog, Namespace, NamespaceIdent, TableCommit, TableCreation,
    TableIdent, TableRequirement, TableUpdate,
};
```

`MemoryCatalog` may live in whatever submodule you like so long as it is
reachable at **`iceberg::MemoryCatalog`** — that is the path the drivers
import. Same for `Transaction`, `ApplyTransactionAction` and `ActionCommit`,
which the drivers import from **`iceberg::transaction`**.

**Standard traits on the value types.** The drivers compare values with
`assert_eq!`, format them into failure messages, sort them and clone them, so
every *value* type this section names must implement `std::fmt::Debug`,
`Clone` and `PartialEq` on top of whatever traits are listed beside it. At this
milestone those types are `TableUpdate`, `TableRequirement`, `TableMetadata`,
`ManifestFile`, `FieldSummary`, `StatisticsFile`, `BlobMetadata`, `Datum`,
`Literal`, `Struct`, `Schema`, `SortOrder`, `SortField`, `PartitionSpec`,
`Snapshot`, `SnapshotReference`, `Summary`, `Namespace`, `NamespaceIdent`,
`TableIdent`, `Type`, `StructType`, `PrimitiveType`, and the enums `FormatVersion`,
`Operation`, `DataContentType`, `ManifestContentType`, `ManifestStatus`,
`DataFileFormat`, `Transform`, `SortDirection` and `NullOrder`.
`NamespaceIdent` must additionally be `Ord`, because the drivers sort what
`list_namespaces` returns before comparing it (section 4.2). `TableCreation`
needs none of the three: the drivers build a fresh one for every call.

**`Table` and `FileIO` are `Debug + Clone` at this milestone.** The drivers
implement `Catalog` themselves; their implementation derives `Debug` and holds
a `FileIO` and a `Table`, and it clones a base `Table` in order to keep serving
the version one writer read while another writer's commit lands (section 4.6).
`MemoryCatalog` and `TableCommit` must be `Debug`, and `Transaction` must be
`Clone` (section 4.4). Section 8 excludes the *text* `Debug` and `Display`
produce; it never excludes the impls.

The drivers also round-trip values through `serde_json` in both directions, so
`TableMetadata`, `Schema`, `Type`, `TableUpdate`, `FieldSummary`,
`StatisticsFile` and `BlobMetadata` must be `Serialize + Deserialize` as well,
producing and accepting the JSON section 6 and the specification define. In
particular a catalog implementation writes a `TableMetadata` it built with
`serde_json::to_vec` and reads it back with `serde_json::from_slice`, and it
clones the metadata it is going to update — `TableMetadata` is
`Debug + Clone + PartialEq + Serialize + Deserialize`.

### 4.1 The `Catalog` trait — "Metastore Tables"

`Catalog` is the component that, per "Metastore Tables", holds a pointer to the
current metadata file for each table and swaps it atomically. **The drivers
implement `Catalog` themselves**, without an `async_trait`-style attribute
macro, so the desugared method shape is part of the contract. If you declare
the trait with such a macro the generated shape must be exactly this: each
method returns a boxed, pinned, `Send` future, one input lifetime per reference
argument, every input lifetime outliving the future's lifetime, and
`Self: 'async_trait`.

```rust
pub trait Catalog: std::fmt::Debug + Sync + Send {
    fn list_namespaces<'life0, 'life1, 'async_trait>(
        &'life0 self,
        parent: Option<&'life1 NamespaceIdent>,
    ) -> std::pin::Pin<Box<
            dyn std::future::Future<Output = Result<Vec<NamespaceIdent>>>
                + Send + 'async_trait>>
    where
        'life0: 'async_trait,
        'life1: 'async_trait,
        Self: 'async_trait;

    // `rename_table` is the one method with THREE input lifetimes:
    fn rename_table<'life0, 'life1, 'life2, 'async_trait>(
        &'life0 self,
        src: &'life1 TableIdent,
        dest: &'life2 TableIdent,
    ) -> std::pin::Pin<Box<
            dyn std::future::Future<Output = Result<()>> + Send + 'async_trait>>
    where
        'life0: 'async_trait,
        'life1: 'async_trait,
        'life2: 'async_trait,
        Self: 'async_trait;

    // `update_table` takes its argument by value, so it has only ONE input lifetime:
    fn update_table<'life0, 'async_trait>(
        &'life0 self,
        commit: TableCommit,
    ) -> std::pin::Pin<Box<
            dyn std::future::Future<Output = Result<Table>> + Send + 'async_trait>>
    where
        'life0: 'async_trait,
        Self: 'async_trait;

    // ... and the remaining methods, in the same shape.
}
```

Written as `async fn`s — which is how you should read the argument order and
the return types — the full method set is:

```rust
async fn list_namespaces(&self, parent: Option<&NamespaceIdent>) -> Result<Vec<NamespaceIdent>>;
async fn create_namespace(&self, namespace: &NamespaceIdent,
                          properties: HashMap<String, String>) -> Result<Namespace>;
async fn get_namespace(&self, namespace: &NamespaceIdent) -> Result<Namespace>;
async fn namespace_exists(&self, namespace: &NamespaceIdent) -> Result<bool>;
async fn update_namespace(&self, namespace: &NamespaceIdent,
                          properties: HashMap<String, String>) -> Result<()>;
async fn drop_namespace(&self, namespace: &NamespaceIdent) -> Result<()>;
async fn list_tables(&self, namespace: &NamespaceIdent) -> Result<Vec<TableIdent>>;
async fn create_table(&self, namespace: &NamespaceIdent,
                      creation: TableCreation) -> Result<Table>;
async fn load_table(&self, table: &TableIdent) -> Result<Table>;
async fn drop_table(&self, table: &TableIdent) -> Result<()>;
async fn table_exists(&self, table: &TableIdent) -> Result<bool>;
async fn rename_table(&self, src: &TableIdent, dest: &TableIdent) -> Result<()>;
async fn register_table(&self, table: &TableIdent,
                        metadata_location: String) -> Result<Table>;
async fn update_table(&self, commit: TableCommit) -> Result<Table>;
```

The trait must be object-safe: `Transaction::commit` takes `&dyn Catalog`, and
the drivers pass their own implementation through it. There must be no extra
required method beyond these fourteen, and none may have a default body that
the drivers would have to override.

### 4.2 `MemoryCatalog` — the in-core catalog — "Metastore Tables"

```rust
pub struct MemoryCatalog { /* private */ }   // Debug
impl MemoryCatalog {
    pub fn new(file_io: FileIO, warehouse_location: Option<String>) -> Self;
}
impl Catalog for MemoryCatalog { /* every method of 4.1 */ }
```

The catalog keeps namespaces, their properties and one metadata-file location
per table in memory; the metadata documents themselves go through the
`FileIO`. The drivers build it over a local-filesystem `FileIO` and assert:

- **Namespaces.** `create_namespace` returns a `Namespace` whose `name()` is
  the identifier passed in and whose `properties()` are the map passed in;
  `get_namespace` returns the same. Creating a namespace whose parent does not
  exist is an error, and so is creating one that already exists.
  `get_namespace`, `update_namespace` and `drop_namespace` naming a namespace
  that does not exist are errors too.
- **`list_namespaces` is single-level.** `list_namespaces(None)` returns the
  top-level namespaces. `list_namespaces(Some(parent))` returns each child by
  its own **one-element** identifier — for a namespace `["a", "child"]` under
  parent `["a"]` the returned identifier is `["child"]`, not the full path.
  The order is unspecified; the drivers sort before comparing.
- **`update_namespace` replaces the whole property map**, it does not merge:
  after updating `{k1, k2}` with `{k2}`, `get_namespace` reports exactly
  `{k2}`.
- **Table location derivation.** `create_table` with an explicit
  `TableCreation::location` uses it verbatim. With no location, the prefix is
  the namespace's `"location"` property if it has one, otherwise the
  catalog's `warehouse_location` joined with the namespace components by
  `/`; the table location is that prefix plus `/` plus the table name. So a
  catalog created with warehouse `W`, a namespace `["db"]` carrying no
  `location` property, and a table named `derived`, yields the table location
  `W/db/derived`. With no explicit location, no namespace `location` property
  and no warehouse location, `create_table` is an error. Creating a table that
  already exists is an error, and so is creating one under a namespace that
  does not exist.
- **The metadata a `TableCreation` produces.** A `TableCreation` carries no
  format version, and the metadata `create_table` writes is
  **`FormatVersion::V2`** — the highest version `PROFILE.md` P1 puts in scope.
  The schema it was given becomes the current schema; with no partition
  spec and no sort order it gets the unpartitioned spec and the unsorted order
  (section 4.9).
- **Metadata file placement** ("Metastore Tables": the catalog stores a
  pointer to the table's current metadata file). The metadata document is
  written under the table's location — the shape used is
  `{location}/metadata/{version}-{uuid}.metadata.json` — and the resulting
  `Table::metadata_location()` is `Some(that path)`. The drivers do not pin
  the file name; they require that `metadata_location()` is `Some`, that
  `load_table` reads that file back into a `TableMetadata` equal to the
  created one, and that `register_table` accepts such a path.
- **Rename, drop, register.** `rename_table` moves the metadata pointer: the
  source identifier stops existing, the destination exists, and the loaded
  table has the same `uuid()` and the same `metadata_location()`.
  `rename_table` onto an existing name is an error and leaves both tables
  intact. `drop_table` removes the pointer and the metadata file, so
  `table_exists` is false, `list_tables` no longer lists it and `load_table`
  is an error; dropping a table that does not exist is an error.
  `register_table` binds an existing metadata-file location to a new
  identifier and returns a table with that identifier and the original
  `uuid()`.
- **`update_table` is not supported on this catalog.** `MemoryCatalog` holds
  no lock and performs no atomic swap, so its `update_table` returns an
  `Err`. A `Transaction` committed against a bare `MemoryCatalog` therefore
  fails. This is deliberate: the metadata swap of "Metastore Tables" is what
  the drivers supply in their own `Catalog` implementation, and it is how the
  rest of this section is exercised.

### 4.3 `TableCommit` — applying a commit

```rust
pub struct TableCommit { /* private: ident, requirements, updates */ }  // Debug
impl TableCommit {
    pub fn identifier(&self) -> &TableIdent;
    pub fn take_requirements(&mut self) -> Vec<TableRequirement>;   // leaves the vec empty
    pub fn take_updates(&mut self) -> Vec<TableUpdate>;             // leaves the vec empty
    /// Checks every requirement against `table`'s metadata, then applies every
    /// update through `table.metadata().clone().into_builder(None)`.
    /// Consumes the commit.
    pub fn apply(self, table: Table) -> Result<Table>;
}
```

`TableCommit` has no public constructor: the builder is crate-private, and the
only way a caller obtains one is as the argument of `Catalog::update_table`.
That is how the drivers observe what an action emitted — they take the updates
and requirements out of the commit inside their own `update_table`.

`apply` returns a `Table` carrying the new metadata and the *old*
`metadata_location`; persisting the document and re-pointing the catalog is
the catalog's job. A failing requirement makes `apply` an `Err` and no update
is applied.

### 4.4 `Transaction` and the transaction-action model

```rust
// iceberg::transaction
pub struct Transaction { /* private: table, actions */ }   // Clone

impl Transaction {
    /// Takes the base table by reference and keeps its own copy of it, so the
    /// transaction has NO lifetime parameter and outlives the binding it was
    /// built from.
    pub fn new(table: &Table) -> Self;

    // Action constructors. Each takes `&self` and returns a fresh, unattached
    // action value; none of them mutates the transaction.
    pub fn fast_append(&self) -> FastAppendAction;
    pub fn update_table_properties(&self) -> UpdatePropertiesAction;
    pub fn update_location(&self) -> UpdateLocationAction;
    pub fn upgrade_table_version(&self) -> UpgradeFormatVersionAction;
    pub fn replace_sort_order(&self) -> ReplaceSortOrderAction;
    pub fn update_statistics(&self) -> UpdateStatisticsAction;

    /// Consumes the transaction. Returns the table the catalog produced.
    pub async fn commit(self, catalog: &dyn Catalog) -> Result<Table>;
}

/// Adds an action to a transaction. Blanket-implemented for every action type,
/// so it must be in scope: `use iceberg::transaction::ApplyTransactionAction;`
pub trait ApplyTransactionAction {
    /// Consumes BOTH the action and the transaction, and returns a new one.
    fn apply(self, tx: Transaction) -> Result<Transaction>;
}

/// What an action produces when the transaction commits.
pub struct ActionCommit { /* private */ }
impl ActionCommit {
    pub fn new(updates: Vec<TableUpdate>, requirements: Vec<TableRequirement>) -> Self;
    pub fn take_updates(&mut self) -> Vec<TableUpdate>;        // leaves the vec empty
    pub fn take_requirements(&mut self) -> Vec<TableRequirement>;  // same
}
```

**The action types are reachable only by method chaining.** They may be `pub`,
but they live in private modules and are never re-exported, so no caller can
name `FastAppendAction` or write it in a `let` binding's type. Every driver
usage has the shape

```rust
let tx = Transaction::new(&table);
let tx = tx.fast_append().add_data_files(files).apply(tx)?;
let table = tx.commit(&catalog).await?;
```

— build the action off `&tx`, configure it, hand it and the transaction to
`apply`, and get a new transaction back. The trait that actually commits an
action against a table is **crate-private**: nothing outside the crate can
implement a new action or invoke one directly.

Every configuration method consumes the action and returns it:

```rust
impl FastAppendAction {
    pub fn add_data_files(self, data_files: impl IntoIterator<Item = DataFile>) -> Self;
    pub fn set_snapshot_properties(self, props: HashMap<String, String>) -> Self; // replaces
    pub fn set_commit_uuid(self, commit_uuid: uuid::Uuid) -> Self;
    pub fn set_key_metadata(self, key_metadata: Vec<u8>) -> Self;
    pub fn with_check_duplicate(self, v: bool) -> Self;      // default true
}
impl UpdatePropertiesAction {
    pub fn set(self, key: String, value: String) -> Self;
    pub fn remove(self, key: String) -> Self;
}
impl UpdateLocationAction {
    pub fn set_location(self, location: String) -> Self;
}
impl UpgradeFormatVersionAction {
    pub fn set_format_version(self, format_version: FormatVersion) -> Self;
}
impl ReplaceSortOrderAction {
    /// Column NAME, resolved against the current schema at commit time.
    /// The transform is always `Transform::Identity`.
    pub fn asc(self, name: &str, null_order: NullOrder) -> Self;
    pub fn desc(self, name: &str, null_order: NullOrder) -> Self;
}
impl UpdateStatisticsAction {
    /// Keyed by `statistics_file.snapshot_id`.
    pub fn set_statistics(self, statistics_file: StatisticsFile) -> Self;
    pub fn remove_statistics(self, snapshot_id: i64) -> Self;
}
```

`set_key_metadata` hands its bytes to the manifest writer the append uses, so
they come back as the `key_metadata` of the manifest-list entry the commit
produced (section 4.8). `set_commit_uuid` fixes the uuid the commit uses when
it names the files it writes, so that uuid appears in the manifest-list path,
in the manifest path, or in both; the drivers accept any of the three.

An action is validated when the transaction commits, not when it is applied:
`apply` returns `Ok` for an incompletely configured action, and `commit`
returns the `Err`.

Documented rejections, each surfacing from `commit`:

- a fast append with **no** data files;
- a fast append whose data file is not `DataContentType::Data` ("Manifests": a
  data manifest tracks only data files);
- a fast append whose data file's `partition_spec_id` is not the table's
  default spec id ("Partitioning");
- a property key that is both `set` and `remove`d by one action;
- a location update with no location set;
- a format-version upgrade with no target version set;
- a format-version **downgrade** (Appendix E describes only forward changes);
- a property update naming a reserved property such as `format-version`
  ("Table Metadata Fields": these are metadata, not table properties);
- a sort field naming a column absent from the current schema ("Sorting").

Setting the format version a table **already has** is not a rejection: it is
accepted and changes nothing, because Appendix E describes only forward
changes and there is nothing to do.

A rejected commit must leave the catalog's table exactly as it was.

### 4.5 What each action emits — the update/requirement pairs

This is the table the drivers assert, element by element and **in order**, by
taking the updates and requirements out of the `TableCommit` their catalog
receives.

| action | `updates`, in order | `requirements`, in order |
| --- | --- | --- |
| fast append | `[AddSnapshot { snapshot }, SetSnapshotRef { ref_name: "main", reference: SnapshotReference::new(new_snapshot_id, SnapshotRetention::branch(None, None, None)) }]` | `[UuidMatch { uuid: base.uuid() }, RefSnapshotIdMatch { r#ref: "main", snapshot_id: base.current_snapshot_id() }]` — note the requirement's `snapshot_id` is the `Option<i64>` of the base table, so it is `None` on a table with no snapshot yet |
| update properties | `[SetProperties { updates }, RemoveProperties { removals }]` — **both are always emitted, even when one is empty** | `[]` |
| update location | `[SetLocation { location }]` | `[]` |
| upgrade format version | `[UpgradeFormatVersion { format_version }]` | `[]` |
| replace sort order | `[AddSortOrder { sort_order }, SetDefaultSortOrder { sort_order_id: -1 }]` — `-1` is the "last added" proxy id | `[CurrentSchemaIdMatch { current_schema_id }, DefaultSortOrderIdMatch { default_sort_order_id }]`, both read off the base table |
| update statistics | one `SetStatistics { statistics }` per file set, one `RemoveStatistics { snapshot_id }` per snapshot removed | `[]` |

The two property variants carry `SetProperties { updates: HashMap<String,
String> }` and `RemoveProperties { removals: Vec<String> }` — the drivers
construct both directly, so the container types are part of the contract.
`removals` is collected out of a set, so its **order** is unspecified when more
than one key is removed; the drivers only remove one key at a time.

Both enums are `Clone`, and their JSON form is tagged:

```rust
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "action", rename_all = "kebab-case")]
pub enum TableUpdate { /* the variants named above */ }

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum TableRequirement { /* the variants named above */ }
```

So a `TableUpdate` serialises as a tagged object whose `action` is the
variant's kebab-case name: `{"action": "set-properties", "updates": {..}}`,
`{"action": "remove-properties", "removals": [..]}`,
`{"action": "set-location", "location": ".."}`. The drivers assert those three.
`TableRequirement`'s own JSON is not asserted — only that it round-trips
through serde at all — but **`Clone` on both types is load-bearing beyond the
JSON**: a catalog implementation takes the two vectors out of the `TableCommit`
it is handed and still has to check and apply them, and the drivers record what
each action produced before applying it.

A replaced sort order lands as a *new* order rather than overwriting one:
"Sorting" reserves `sort-order-id` `0` for the unsorted order, so the committed
default order has a fresh non-zero id, and the unsorted order stays reachable
through `sort_order_by_id(0)`.

The new snapshot a fast append produces follows "Snapshots" and "Sequence
Numbers":

- `parent_snapshot_id()` is the base table's `current_snapshot_id()` — `None`
  for the first snapshot;
- `sequence_number()` is the base table's `next_sequence_number()`, i.e. one
  more than its `last_sequence_number()`, so the first commit's snapshot has
  sequence number 1 and the committed metadata's `last_sequence_number()`
  matches it;
- `schema_id()` is the base table's `current_schema_id()`;
- `manifest_list()` is a non-empty path, under the table location, of an Avro
  manifest list that the drivers read back;
- committing advances the `main` branch (`snapshot_for_ref("main")`) and
  appends one entry to the snapshot log, whose `snapshot_id` and
  `timestamp_ms` match the new snapshot;
- the manifest the commit wrote appears in the manifest list with
  `added_files_count == Some(1)`, `existing_files_count == Some(0)`,
  `deleted_files_count == Some(0)`, `added_rows_count == Some(<record count>)`,
  `content == ManifestContentType::Data`, `added_snapshot_id ==` the new
  snapshot id, and `sequence_number ==` the new snapshot's sequence number;
- a second fast append carries the earlier manifest forward: the new manifest
  list contains both manifest paths.

**Snapshot summary** ("Snapshots": `summary` is required and must carry
`operation`; Appendix F "Optional Snapshot Summary Fields"). A fast append's
summary has `operation == Operation::Append`, and serialises with
`"operation": "append"`. Every additional summary key **the implementation
itself generates** must be either one of the metric names Appendix F's
*Metrics* table defines — and every one of *those* values must be a decimal
string — or a `partitions.<partition-path>` per-partition summary, whose value
is Appendix F's `k=v,k=v` list of that partition's own metrics rather than a
single count, and which section 8 leaves out of scope. The running totals
`total-data-files`, `total-records` and `total-files-size` must be present.
Properties passed to `set_snapshot_properties` are the caller's, not the
implementation's: they must appear verbatim in the committed summary and the
key restriction above does not apply to them — Appendix F's *Other Fields*
(`engine-name`, `engine-version`) are how a caller labels a commit, and those
are what the drivers pass. (See section 8 for the per-append added-file metrics, which
are out of scope.)

### 4.6 Commit conflict resolution and retry — "Optimistic Concurrency", "Commit Conflict Resolution and Retry"

`Transaction::commit` implements the optimistic protocol:

- it re-loads the table from the catalog before running its actions, and if
  the metadata or the metadata location has changed it re-bases onto the
  refreshed table and derives its updates and requirements from that;
- it then hands the catalog a `TableCommit` whose requirements state the
  identity of the version it built on — for a fast append, the table's UUID
  and the snapshot `main` pointed at;
- it retries the whole commit while, and only while, the error reports itself
  as retryable, with exponential backoff bounded by the table properties
  below. An error that is not retryable fails the commit immediately.

```rust
// iceberg::spec
pub const PROPERTY_COMMIT_NUM_RETRIES: &str = "commit.retry.num-retries";
pub const PROPERTY_COMMIT_NUM_RETRIES_DEFAULT: usize = 4;
pub const PROPERTY_COMMIT_MIN_RETRY_WAIT_MS: &str = "commit.retry.min-wait-ms";
pub const PROPERTY_COMMIT_MIN_RETRY_WAIT_MS_DEFAULT: u64 = 100;
pub const PROPERTY_COMMIT_MAX_RETRY_WAIT_MS: &str = "commit.retry.max-wait-ms";
pub const PROPERTY_COMMIT_MAX_RETRY_WAIT_MS_DEFAULT: u64 = 60 * 1000;
pub const PROPERTY_COMMIT_TOTAL_RETRY_TIME_MS: &str = "commit.retry.total-timeout-ms";
pub const PROPERTY_COMMIT_TOTAL_RETRY_TIME_MS_DEFAULT: u64 = 30 * 60 * 1000;

// iceberg
impl Error {
    pub fn new(kind: ErrorKind, message: impl Into<String>) -> Self;
    pub fn retryable(&self) -> bool;              // false unless set
    pub fn with_retryable(self, retryable: bool) -> Self;
}
pub enum ErrorKind { /* ... */ CatalogCommitConflicts /* ... */ }
```

The drivers use `Error::new` to build an error of their **own** and return it
from their `Catalog::update_table`: a failure marked
`with_retryable(true)` must make `Transaction::commit` run the commit again —
the drivers count the attempts their catalog sees — and a failure left
non-retryable must fail the commit at the first attempt. That is the one place
the drivers *name* an `ErrorKind` variant; they still never assert which kind
your own code returns, and section 8 keeps the backoff schedule itself
out of scope.

These four values are read from the **table's** properties at commit time. A
value that does not parse as its type must make the commit an `Err` rather
than being silently replaced by the default — and note that the commit which
*writes* such a property still succeeds, because it read the properties as they
were before it ran.

`TableRequirement::check` is what makes a stale commit fail. The drivers assert
that a `RefSnapshotIdMatch` naming the snapshot `main` pointed at before a
concurrent append is `Ok` against the pre-append metadata and `Err` against the
post-append metadata, while `UuidMatch` stays `Ok` because a concurrent append
does not change the table's identity. They then reproduce the read-then-write
race end to end — a catalog that keeps serving the version a writer read while
another writer's commit has already landed — and require the late commit to
fail and the earlier one to remain current.

### 4.7 Manifest-list bounds are byte buffers — "Manifest Lists", Appendix D "Bound serialization"

"Manifest Lists" types fields 510 `lower_bound` and 511 `upper_bound` as
`bytes`. They are now carried as byte buffers rather than as decoded values.

```rust
// iceberg::spec — re-exported so callers can name it without another dependency
pub use serde_bytes::ByteBuf;

#[derive(Debug, Serialize, Deserialize, PartialEq, Eq, Clone, Default, Hash)]
pub struct FieldSummary {
    pub contains_null: bool,            // 509
    pub contains_nan: Option<bool>,     // 518
    pub lower_bound: Option<ByteBuf>,   // 510
    pub upper_bound: Option<ByteBuf>,   // 511
}
```

`FieldSummary` is now itself `Serialize + Deserialize`, with the field names
spelled exactly `contains_null`, `contains_nan`, `lower_bound`, `upper_bound`,
and the bounds serialising as JSON byte arrays.

The bytes are the Appendix D "Binary single-value serialization" form of the
partition value, which is also what `Datum::to_bytes` produces and what
`Datum::try_from_bytes` reads back. Both are **fallible**:

```rust
impl Datum {
    pub fn to_bytes(&self) -> Result<ByteBuf>;
    pub fn try_from_bytes(bytes: &[u8], data_type: PrimitiveType) -> Result<Datum>;
}
```

- `long` is eight bytes little-endian, `int` four bytes little-endian;
- `string` is raw UTF-8 with no length prefix.

The drivers write a partitioned manifest through `ManifestWriterBuilder`, read
its `ManifestFile` back out of a manifest list, and require that the summary
computed by the writer and the summary read back from Avro are equal, and
that for an identity-partitioned `long` column with values 100, 300 and 200
the bounds are the little-endian encodings of 100 and 300. Per field 509/510/511:
a null partition value sets `contains_null` and is excluded from the bounds; a
manifest all of whose partition values are null has `contains_null` set and
**both bounds `None`**. `contains_nan` is `Some(false)` for a manifest with no
NaN partition value.

### 4.8 Manifest and manifest-list surface, extended — "Manifest Lists", "Manifests"

```rust
impl ManifestList {
    /// TWO arguments now: the record is self-describing, so no partition-type
    /// callback is needed.
    pub fn parse_with_version(bs: &[u8], version: FormatVersion) -> Result<ManifestList>;
    pub fn entries(&self) -> &[ManifestFile];
    pub fn consume_entries(self) -> impl IntoIterator<Item = ManifestFile>;
}

pub struct ManifestFile {
    // ... unchanged through added_snapshot_id and the six counts ...
    pub partitions: Option<Vec<FieldSummary>>,   // 507 — now optional
    pub key_metadata: Option<Vec<u8>>,           // 519 — now optional
}

impl ManifestWriterBuilder {
    pub fn new(
        output: OutputFile,
        snapshot_id: Option<i64>,
        key_metadata: Option<Vec<u8>>,           // now optional
        schema: SchemaRef,
        partition_spec: PartitionSpec,
    ) -> Self;
    pub fn build_v1(self) -> ManifestWriter;
    pub fn build_v2_data(self) -> ManifestWriter;
    pub fn build_v2_deletes(self) -> ManifestWriter;
}

impl Manifest {
    pub fn parse_avro(bs: &[u8]) -> Result<Manifest>;
    pub fn entries(&self) -> &[ManifestEntryRef];
    /// NEW — the metadata the manifest file recorded about itself.
    pub fn metadata(&self) -> &ManifestMetadata;
}

impl ManifestMetadata {                     // every accessor returns a reference
    pub fn schema(&self) -> &SchemaRef;
    pub fn schema_id(&self) -> SchemaId;     // by value
    pub fn partition_spec(&self) -> &PartitionSpec;
    pub fn format_version(&self) -> &FormatVersion;
    pub fn content(&self) -> &ManifestContentType;
}
```

Observable obligations the drivers assert:

- a manifest written for an **unpartitioned** spec has
  `partitions == Some(vec![])` — present but empty — and, when no key metadata
  was given, `key_metadata == None`; a writer given `Some(bytes)` round-trips
  those bytes through the manifest list;
- `parse_with_version` needs only the bytes and the table format version, and
  both a data manifest and a delete manifest may sit in the same list, each
  keeping its `content`;
- a v1 manifest list round-trips with `sequence_number == 0` and
  `min_sequence_number == 0` ("Sequence Numbers": readers use 0 for v1);
- a delete manifest built with `build_v2_deletes()` reports
  `ManifestContentType::Deletes` both in the returned `ManifestFile` and in
  `Manifest::parse_avro(..).metadata().content()`, alongside its format
  version, schema id, schema and (unpartitioned) partition spec;
- a position delete file round-trips through such a manifest with
  `content_type() == DataContentType::PositionDeletes`, its record count, its
  data sequence number, `is_alive()`, empty `equality_ids()`, and its
  `referenced_data_file()` preserved ("Position Delete Files");
- the content check runs both ways: a data file added to a delete manifest is
  an error, and a delete file added to a data manifest is an error.

The `ManifestEntry` and `DataFile` accessors the drivers read, with their
return types — a reference-versus-value return here is a compile error, not a
failed assertion, and these are reached through `ManifestEntryRef`, which is
`Arc<ManifestEntry>`:

```rust
impl ManifestEntry {
    pub fn status(&self) -> ManifestStatus;          // by value
    pub fn content_type(&self) -> DataContentType;   // by value
    pub fn file_path(&self) -> &str;
    pub fn record_count(&self) -> u64;
    /// The entry's data sequence number; `None` when the entry carries none
    /// (`add_file` records the number it was given only when it is >= 0).
    pub fn sequence_number(&self) -> Option<i64>;
    pub fn file_size_in_bytes(&self) -> u64;
    pub fn data_file(&self) -> &DataFile;
    pub fn is_alive(&self) -> bool;                  // status is Added or Existing
}
impl DataFile {
    pub fn equality_ids(&self) -> &[i32];            // empty for a data or
                                                     // position-delete file
    pub fn referenced_data_file(&self) -> Option<String>;   // OWNED, not a ref
}
```

`DataFile`'s builder setter takes the `Option` itself
(`.referenced_data_file(Some(path))`), it does not strip it.

### 4.9 Table metadata, statistics and float ordering

```rust
impl TableMetadata {
    /// NEW at this milestone — previously only observable through the JSON.
    pub fn last_column_id(&self) -> i32;
    pub fn last_partition_id(&self) -> i32;
}
```

"Table Metadata Fields": for a table created from a schema with highest field
id 2 and no partition spec, `last_column_id()` is `2` and equals the current
schema's `highest_field_id()`, and `last_partition_id()` is **999** — the last
id assigned before the first partition field, since partition field ids start
at 1000 ("Partition Evolution"). Such a table also has no current snapshot,
`last_sequence_number() == 0`, an empty snapshot log, an unsorted default sort
order and an unpartitioned default spec.

"Table Statistics" — the statistics file the update-statistics action carries:

```rust
// iceberg::spec, kebab-case JSON keys
pub struct StatisticsFile {
    pub snapshot_id: i64,                    // "snapshot-id"
    pub statistics_path: String,             // "statistics-path"
    pub file_size_in_bytes: i64,             // "file-size-in-bytes"
    pub file_footer_size_in_bytes: i64,      // "file-footer-size-in-bytes"
    pub key_metadata: Option<String>,        // "key-metadata", omitted when None
    pub blob_metadata: Vec<BlobMetadata>,    // "blob-metadata"
}
pub struct BlobMetadata {
    pub r#type: String,                      // "type"
    pub snapshot_id: i64,                    // "snapshot-id"
    pub sequence_number: i64,                // "sequence-number"
    pub fields: Vec<i32>,                    // "fields"
    pub properties: HashMap<String, String>, // "properties", omitted when empty
}
```

Setting statistics makes `statistics_for_snapshot(id)` return the file and
`statistics_iter()` count it; removing them drops both. `StatisticsFile` must
round-trip through `serde_json` unchanged.

"Sorting" fixes the floating-point order as
`-NaN < -Infinity < -value < -0 < 0 < value < Infinity < NaN`. `Datum`'s
`PartialOrd` must implement it for `float` and `double`: NaN is **ordered, not
incomparable**, so `partial_cmp` is `Some` for every pair of same-typed float
values, a positive NaN sorts above every finite value and above `Infinity`, a
negative NaN sorts below every finite value and below `-Infinity`, and two NaNs
of the same sign compare `Equal`. (See section 8 for the sign of zero.)

### 4.10 Table, scan and IO surface the drivers use

Nothing here is new; these are the milestone-2 and milestone-4 shapes the
drivers rely on while exercising the commit path.

```rust
impl Table {
    pub fn builder() -> TableBuilder;   // .file_io / .metadata / .metadata_location
                                        // .identifier / .build() -> Result<Table>
    pub fn identifier(&self) -> &TableIdent;
    pub fn metadata(&self) -> &TableMetadata;
    pub fn metadata_ref(&self) -> TableMetadataRef;
    pub fn metadata_location(&self) -> Option<&str>;
    pub fn file_io(&self) -> &FileIO;
    pub fn scan(&self) -> TableScanBuilder<'_>;
}
impl TableScan {
    pub async fn plan_files(&self) -> Result<FileScanTaskStream>;
    pub fn column_names(&self) -> Option<&[String]>;
    pub fn snapshot(&self) -> Option<&SnapshotRef>;
}
impl TableMetadata {
    pub fn into_builder(self, current_file_location: Option<String>) -> TableMetadataBuilder;
}
impl FileIO { pub fn new_output(&self, path: impl AsRef<str>) -> Result<OutputFile>; }
impl OutputFile {
    pub fn location(&self) -> &str;
    pub async fn write(&self, bs: bytes::Bytes) -> Result<()>;
}
```

There is **no** public way to replace a `Table`'s metadata in place: whatever
method does that internally must stay crate-private. A caller changes a
table's metadata only by committing through a catalog, or by building a new
`Table` with `Table::builder()`.

`TableScanBuilder::build` must still reject a selected column the snapshot's
schema does not have and a `snapshot_id` naming no snapshot, and `plan_files`
must still resolve the snapshot's manifest list before it returns, so a
manifest list that is not readable is an `Err` from `plan_files` itself.

### 4.11 Inherited surface the drivers still build on

Nothing here is new. It is milestone-1-to-4 surface the drivers name while
exercising this milestone, restated because the documents that first gave it
are not in the workspace any more.

**Fixtures.** Every driver builds its schemas, partition specs and IO handles
through these:

```rust
// iceberg::spec
impl NestedField {
    pub fn required(id: i32, name: impl ToString, field_type: Type) -> Self;
    pub fn optional(id: i32, name: impl ToString, field_type: Type) -> Self;
}
// `NestedFieldRef` is `Arc<NestedField>`, so `NestedField::required(..).into()`
// is how a field reaches a schema builder; `SchemaRef` is `Arc<Schema>`.
impl Schema {
    pub fn builder() -> SchemaBuilder;
    //   .with_schema_id(i32)
    //   .with_fields(impl IntoIterator<Item = NestedFieldRef>)
    //   .build() -> Result<Schema>
}
impl PartitionSpec {
    pub fn builder(schema: impl Into<SchemaRef>) -> PartitionSpecBuilder;
    //   .with_spec_id(i32)
    //   .add_partition_field(source_name: impl AsRef<str>,
    //                        target_name: impl Into<String>,
    //                        transform: Transform) -> Result<Self>
    //   .build() -> Result<PartitionSpec>
}
impl Transform {
    /// The result-type column of "Partition Transforms": `int` for
    /// `bucket[N]` over any source type, the source type itself for
    /// `identity`.
    pub fn result_type(&self, input_type: &Type) -> Result<Type>;
}

// iceberg::io
pub struct FileIOBuilder { /* private */ }
impl FileIOBuilder {
    pub fn new_fs_io() -> Self;              // the local filesystem (PROFILE P3)
    pub fn build(self) -> Result<FileIO>;
}
```

**Identifiers and table creation.** The catalog cases name every table and
namespace through these, so their exact shapes matter:

```rust
// iceberg (crate root)
impl NamespaceIdent {
    /// One component per item; `["a", "child"]` is the namespace `a.child`.
    pub fn from_strs(iter: impl IntoIterator<Item = impl ToString>) -> Result<Self>;
}
impl TableIdent {
    /// The LAST item is the table name, the rest are its namespace, so
    /// `["db", "t"]` is the table `t` in namespace `db`.
    pub fn from_strs(iter: impl IntoIterator<Item = impl ToString>) -> Result<Self>;
    pub fn namespace(&self) -> &NamespaceIdent;
    pub fn name(&self) -> &str;
}

pub struct TableCreation { /* typed builder */ }   // Debug
TableCreation::builder()
    .name(String)                       // required
    .schema(Schema)                     // required
    .location(String)                   // OPTIONAL — a bare String, not an Option
    .partition_spec(UnboundPartitionSpec)   // optional
    .sort_order(SortOrder)                  // optional
    .properties(impl IntoIterator<Item = (String, String)>)  // optional
    .build();                           // infallible; the optional setters may
                                        // simply be left out
// The setters may be called in ANY order — the list above is not a sequence.
// The drivers call `.name().location().schema()` and `.name().schema()`.
```

`TableCreation` itself carries **no** format version; section 4.2 says what
version the metadata `create_table` writes gets.

**Writers, values and metadata the drivers build directly.** All milestone-1
to milestone-4 shapes, unchanged, with the arities the drivers call:

```rust
// iceberg::spec
impl ManifestListWriter {
    pub fn v1(output_file: OutputFile, snapshot_id: i64,
              parent_snapshot_id: Option<i64>) -> Self;
    pub fn v2(output_file: OutputFile, snapshot_id: i64,
              parent_snapshot_id: Option<i64>, sequence_number: i64) -> Self;
    // plus `add_manifests(impl Iterator<Item = ManifestFile>) -> Result<()>`
    // and `async close(self) -> Result<()>`
}
impl TableMetadataBuilder {
    pub fn new(schema: Schema, spec: impl Into<UnboundPartitionSpec>,
               sort_order: SortOrder, location: String,
               format_version: FormatVersion,
               properties: HashMap<String, String>) -> Result<Self>;
    // `build()` yields a value with `.metadata` and `.changes`
}
impl SortOrder { pub fn unsorted_order() -> SortOrder; }
impl Struct {
    pub fn empty() -> Self;
    // and `FromIterator<Option<Literal>>`, so
    // `Struct::from_iter([Some(Literal::long(1))])` builds a one-field value
}
impl Literal { pub fn long(i64) -> Self; pub fn string(impl ToString) -> Self; }
impl Datum {
    pub fn int(i32) -> Self;     pub fn long(i64) -> Self;
    pub fn float(f32) -> Self;   pub fn double(f64) -> Self;
    pub fn string(impl ToString) -> Self;
}
```

The drivers pass plain values to those constructors, so a plain-typed parameter
is enough; a more general one (`impl Into<i64>` and the like) compiles just as
well.

**Variant names the drivers write.** The section-4 preamble lists these enums
among the value types; the drivers also construct and compare their *variants*,
so the spellings and the one payload arity are given here. The variant sets
follow "Primitive Types", "Partition Transforms", "Manifest Entry Fields" and
Appendix A; only the members below are named by a driver.

```rust
// iceberg::spec
pub enum Type { Primitive(PrimitiveType), Struct(StructType), List(..), Map(..) }
// so a long column's type is `Type::Primitive(PrimitiveType::Long)`
pub enum PrimitiveType { /* .. */ Int, Long, String /* .. */ }
pub enum FormatVersion { V1 = 1, V2 = 2 }   // PROFILE P1: no other value parses
pub enum Transform { Identity, Bucket(u32), Truncate(u32),
                     Year, Month, Day, Hour, /* .. */ }
pub enum DataContentType { Data = 0, PositionDeletes = 1, EqualityDeletes = 2 }
pub enum DataFileFormat { Avro, Orc, Parquet, Puffin }
pub enum ManifestStatus { Existing = 0, Added = 1, Deleted = 2 }
```

**Accessors, public fields and arities the drivers read.** Still nothing new,
and section 5's closing paragraph already names these families as unchanged —
but it names them as families. The drivers name the members below, and for
these a reference-versus-value return, a public field's spelling or a wrong
arity is a compile error rather than a failed assertion, so they are spelled
out. `SnapshotRef`, `PartitionSpecRef`, `SortOrderRef`, `NestedFieldRef` and
`ManifestEntryRef` are `Arc<..>` of their types, exactly as `SchemaRef` is.

```rust
// iceberg::spec — schemas and partition specs
impl Schema {
    pub fn as_struct(&self) -> &StructType;      // the struct, not a field list
    pub fn field_by_name(&self, name: &str) -> Option<&NestedFieldRef>;
    pub fn highest_field_id(&self) -> i32;
}
impl StructType { pub fn fields(&self) -> &[NestedFieldRef]; }
pub struct NestedField { pub id: i32, pub name: String, pub required: bool,
                         pub field_type: Box<Type>, /* .. */ }
impl PartitionSpec {
    pub fn is_unpartitioned(&self) -> bool;
    pub fn fields(&self) -> &[PartitionField];
}
pub struct PartitionField { pub source_id: i32, pub field_id: i32,
                            pub name: String, pub transform: Transform }
impl UnboundPartitionSpec {
    pub fn builder() -> UnboundPartitionSpecBuilder;
    //   .with_spec_id(i32) -> Self
    //   .add_partition_field(source_id: i32, target_name: impl ToString,
    //                        Transform) -> Result<Self>
    //     — note: the UNBOUND builder takes the source field's ID, where the
    //       bound `PartitionSpec::builder(schema)` above takes its NAME
    //   .build() -> UnboundPartitionSpec        // INFALLIBLE — not a Result
}
impl FromStr for Transform { type Err = Error; }  // `"bucket[16]".parse()?`

// iceberg::spec — sort orders
pub struct SortOrder { pub order_id: i64, pub fields: Vec<SortField> }
impl SortOrder { pub fn is_unsorted(&self) -> bool; }   // no fields
pub struct SortField { pub source_id: i32, pub transform: Transform,
                       pub direction: SortDirection, pub null_order: NullOrder }
pub enum SortDirection { Ascending, Descending }   // "asc" / "desc" in JSON
pub enum NullOrder { First, Last }         // "nulls-first" / "nulls-last"

// iceberg::spec — snapshots and the snapshot log
pub const MAIN_BRANCH: &str = "main";   // the branch a fast append advances
pub struct Summary { pub operation: Operation,
                     pub additional_properties: HashMap<String, String> }
pub struct SnapshotLog { pub snapshot_id: i64, pub timestamp_ms: i64 }
Snapshot::builder()          // every setter is `with_`-prefixed and consuming
    .with_snapshot_id(i64)
    .with_parent_snapshot_id(Option<i64>)
    .with_sequence_number(i64)
    .with_timestamp_ms(i64)
    .with_manifest_list(impl Into<String>)
    .with_summary(Summary)
    .with_schema_id(SchemaId)      // the id itself, not an Option
    .build();                      // infallible
impl Snapshot {
    pub fn snapshot_id(&self) -> i64;
    pub fn parent_snapshot_id(&self) -> Option<i64>;
    pub fn sequence_number(&self) -> i64;
    pub fn timestamp_ms(&self) -> i64;
    pub fn manifest_list(&self) -> &str;
    pub fn summary(&self) -> &Summary;
    pub fn schema_id(&self) -> Option<SchemaId>;
}

// iceberg::spec — TableMetadata accessors; references unless marked by value
impl TableMetadata {
    pub fn format_version(&self) -> FormatVersion;              // by value
    pub fn uuid(&self) -> Uuid;                                 // by value
    pub fn location(&self) -> &str;
    pub fn properties(&self) -> &HashMap<String, String>;
    pub fn current_schema(&self) -> &SchemaRef;
    pub fn current_schema_id(&self) -> SchemaId;                // by value
    pub fn current_snapshot(&self) -> Option<&SnapshotRef>;
    pub fn current_snapshot_id(&self) -> Option<i64>;           // by value
    pub fn snapshot_by_id(&self, snapshot_id: i64) -> Option<&SnapshotRef>;
    pub fn snapshot_for_ref(&self, ref_name: &str) -> Option<&SnapshotRef>;
    pub fn snapshots(&self) -> impl ExactSizeIterator<Item = &SnapshotRef>;
    pub fn history(&self) -> &[SnapshotLog];                    // the snapshot log
    pub fn default_partition_spec(&self) -> &PartitionSpecRef;
    pub fn default_partition_spec_id(&self) -> i32;             // by value
    pub fn default_sort_order(&self) -> &SortOrderRef;
    pub fn default_sort_order_id(&self) -> i64;                 // by value
    pub fn sort_order_by_id(&self, sort_order_id: i64) -> Option<&SortOrderRef>;
    pub fn last_sequence_number(&self) -> i64;
    pub fn next_sequence_number(&self) -> i64;
    /// Epoch milliseconds, so positive for any metadata the builder produced.
    pub fn last_updated_ms(&self) -> i64;
    pub fn statistics_for_snapshot(&self, snapshot_id: i64) -> Option<&StatisticsFile>;
    pub fn statistics_iter(&self) -> impl ExactSizeIterator<Item = &StatisticsFile>;
}

// iceberg::spec — the update algebra of milestone 3
impl TableMetadataBuilder {
    pub fn add_snapshot(self, snapshot: Snapshot) -> Result<Self>;
    pub fn set_ref(self, ref_name: &str, reference: SnapshotReference) -> Result<Self>;
    pub fn build(self) -> Result<TableMetadataBuildResult>;
}
pub struct TableMetadataBuildResult {
    pub metadata: TableMetadata,
    /// The updates the builder recorded — non-empty whenever it changed
    /// anything, which is how a caller sees what a commit did.
    pub changes: Vec<TableUpdate>,
    /* .. */
}
impl TableUpdate {
    pub fn apply(self, builder: TableMetadataBuilder) -> Result<TableMetadataBuilder>;
}
impl TableRequirement {
    /// `None` means "the table does not exist"; the drivers always pass `Some`.
    pub fn check(&self, metadata: Option<&TableMetadata>) -> Result<()>;
}

// iceberg::spec — manifest writing (section 4.8 has the rest, including the
// typed `ManifestEntry` and `DataFile` accessors)
impl ManifestWriter {
    /// TWO arguments: the file, and the data sequence number recorded for it.
    pub fn add_file(&mut self, data_file: DataFile, sequence_number: i64) -> Result<()>;
    pub async fn write_manifest_file(self) -> Result<ManifestFile>;
}
DataFileBuilder::default()   // `&mut self` setters named for the fields
    .content(DataContentType)
    .file_path(String)
    .file_format(DataFileFormat)
    .partition(Struct)
    .partition_spec_id(i32)              // required — no default
    .record_count(u64)
    .file_size_in_bytes(u64)
    .equality_ids(Vec<i32>)
    .referenced_data_file(Option<String>)
    .build();                            // -> Result<DataFile>

// iceberg (crate root) — both accessors return REFERENCES
impl Namespace {
    pub fn name(&self) -> &NamespaceIdent;
    pub fn properties(&self) -> &HashMap<String, String>;
}

// iceberg::scan — milestone 2's builder, consuming setters
impl TableScanBuilder<'_> {
    pub fn select(self, column_names: impl IntoIterator<Item = impl ToString>) -> Self;
    pub fn snapshot_id(self, snapshot_id: i64) -> Self;
    pub fn build(self) -> Result<TableScan>;
}
```

**What the gate re-exercises from milestone 4.** Section 8 places the `inspect`
and `puffin` modules out of scope at this milestone, though they must keep
the surface the previous milestone required. The **gate** does re-exercise the
slice below. Nothing outside this slice is touched at this milestone.

```rust
// iceberg::inspect
pub struct MetadataTable<'a>(&'a Table);
impl<'a> MetadataTable<'a> {
    pub fn new(table: &'a Table) -> Self;
    pub fn snapshots(&self) -> SnapshotsTable<'_>;
    pub fn manifests(&self) -> ManifestsTable<'_>;
}
// each of the two tables has `schema(&self) -> iceberg::spec::Schema`
//   — an Iceberg schema, not an Arrow one
// iceberg::table
impl Table { pub fn inspect(&self) -> MetadataTable<'_>; }
```

The drivers use each view inside a single expression, so whether the returned
view borrows the `MetadataTable` or the `Table` behind it is yours.

- `SnapshotsTable::schema()` has **six** fields, in this order:
  `committed_at` (id 1), `snapshot_id` (2), `parent_id` (3), `operation` (4),
  `manifest_list` (5), `summary` (6).
- `ManifestsTable::schema()` has **twelve** fields; the first two are
  `content`, carrying id **14**, and `path`, carrying id 1.
- `MetadataTable::new(&table)` and `table.inspect()` produce the same schemas.

```rust
// iceberg::puffin
pub const DELETION_VECTOR_V1: &str = "deletion-vector-v1";

pub struct Blob { /* private */ }
Blob::builder()
    .r#type(String)                        // the setter is literally `r#type`
    .fields(Vec<i32>)
    .snapshot_id(i64)
    .sequence_number(i64)
    .data(Vec<u8>)
    .properties(HashMap<String, String>)
    .build();                              // all six required, infallible
impl Blob {
    pub fn blob_type(&self) -> &str;
    pub fn fields(&self) -> &[i32];
    pub fn data(&self) -> &[u8];
}

pub enum CompressionCodec { None, Lz4, Zstd }    // `Default` is `None`
```

`DataFileFormat` still carries `Puffin`, spelled `"puffin"` by `FromStr`.

## 5. Migration from the previous milestone

The previous milestone's contract changes in the following ways. The new form
is what is required; the old form is not accepted.

| Milestone 4 | This milestone |
| --- | --- |
| `ManifestList::parse_with_version(bs, version, partition_type_provider: impl Fn(i32) -> Result<Option<StructType>>)` | `ManifestList::parse_with_version(bs: &[u8], version: FormatVersion)` — two arguments; the manifest-list record is self-describing |
| `FieldSummary { lower_bound: Option<Datum>, upper_bound: Option<Datum>, .. }` | `Option<ByteBuf>` for both, holding the Appendix D binary form; `FieldSummary` also gains `Serialize`/`Deserialize` |
| `ManifestFile::partitions: Vec<FieldSummary>` | `Option<Vec<FieldSummary>>` |
| `ManifestFile::key_metadata: Vec<u8>` | `Option<Vec<u8>>` |
| `ManifestWriterBuilder::new(output, snapshot_id, key_metadata: Vec<u8>, schema, partition_spec)` | `key_metadata: Option<Vec<u8>>` |
| `Manifest` exposes only `parse_avro`, `entries`, `into_parts` | adds `metadata(&self) -> &ManifestMetadata`, with reference-returning accessors for schema, schema id, partition spec, format version and content |
| `TableMetadata` has no `last_column_id()`/`last_partition_id()`; the two fields are observable only through the metadata JSON | `last_column_id(&self) -> i32` and `last_partition_id(&self) -> i32` are public accessors (the JSON keys stay as they were) |
| `FileScanTask::data_file_content: DataContentType` | removed; a task's delete files are described by `FileScanTask::deletes` alone |
| `TableScanBuilder::with_delete_file_processing_enabled(bool)` | removed; delete files are always considered during planning |
| `TableCommit` exposes only `identifier`, `take_requirements`, `take_updates` | adds `apply(self, table: Table) -> Result<Table>`, which checks the requirements and applies the updates in one step |
| `Error` has no retry flag; `ErrorKind` has no commit-conflict kind | `Error::retryable()` / `Error::with_retryable(bool)`, and `ErrorKind::CatalogCommitConflicts` |
| no in-core catalog implementation | `MemoryCatalog`, reachable at `iceberg::MemoryCatalog` |
| no transaction module | `pub mod transaction` with `Transaction`, `ApplyTransactionAction`, `ActionCommit` and the six commit actions |
| no commit-retry property constants | `PROPERTY_COMMIT_NUM_RETRIES`, `PROPERTY_COMMIT_MIN_RETRY_WAIT_MS`, `PROPERTY_COMMIT_MAX_RETRY_WAIT_MS`, `PROPERTY_COMMIT_TOTAL_RETRY_TIME_MS` and their `_DEFAULT` values |

Two shapes are **restated** rather than changed, because the drivers depend on
the exact form and it is easy to miss: `TableRequirement` derives `Clone`
alongside `Debug`, `PartialEq`, `Serialize` and `Deserialize` (section 4.5),
and `Datum::to_bytes` is fallible, returning `Result<ByteBuf>` (section 4.7).
Neither changed at this milestone.

Unchanged from the previous milestone and still required in exactly the same
form: `ManifestFile`'s remaining public fields and its `has_added_files` /
`has_existing_files` / `has_deleted_files`; `ManifestListWriter::{v1, v2,
add_manifests, close}` and its sequence-number substitution;
`ManifestWriterBuilder::{build_v1, build_v2_data, build_v2_deletes}` and
`ManifestWriter::{add_file, add_delete_file, add_existing_file,
write_manifest_file}` with their data-versus-delete content check;
`ManifestEntry`'s accessors (typed in section 4.8);
`DataFileBuilder::default()` with its `&mut self` setters, its required
`partition_spec_id`, and its fallible `build()`; `DataContentType`,
`ManifestContentType` and `DataFileFormat`; `Datum` and its `to_bytes` /
`try_from_bytes`; `Schema`, `SortOrder`, `SortField`, `Struct` and `Literal`;
`PartitionSpec::builder(schema)` and `UnboundPartitionSpec::builder()` with
their consuming methods; the whole `TableMetadataBuilder` update algebra,
including `TableUpdate::apply` and `TableRequirement::check`; `TableMetadata`'s
other accessors; `Snapshot::builder()` with its `with_`-prefixed setters and
its accessors; `SnapshotReference`, `SnapshotRetention` and `MAIN_BRANCH`;
`TableCreation`, `TableIdent`, `NamespaceIdent` and `Namespace`;
`Table::builder()`; and milestone 2's `expr`, `transform` and `scan` surface.
Note in particular that `add_snapshot` still refuses a snapshot whose timestamp
precedes the last snapshot-log entry's timestamp — or the table's
`last-updated-ms` — by more than a minute, the tolerance it allows for clock
skew between the machines that commit. That is what orders the snapshot log.
No case in this milestone sits near that boundary.
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

- **Consuming the planned file-scan-task stream.** `plan_files` is inherited
  from milestone 2, so at this milestone the **gate** — not a requirement group —
  exercises it, for resolving and parsing the snapshot's manifest list and
  returning successfully, or failing when that manifest list is unreadable.
  The tasks it streams are never read. Applying positional deletes to a scanned
  row set, the delete-file index that attaches delete files to a data file's
  task, per-file and per-row-group pruning, and `to_arrow` are all outside the
  in-scope surface.
- **The per-append added-file snapshot-summary metrics.** A fast append's
  summary is in scope for carrying `operation` (`"append"`), for using only
  metric names the specification defines, for giving every metric a decimal
  string value, for carrying the running totals `total-data-files`,
  `total-records` and `total-files-size`, and for preserving verbatim any
  properties passed to `set_snapshot_properties`. Whether it also reports
  `added-data-files`, `added-records` or `added-files-size` for the files that
  commit added is **not** in scope — the per-manifest `added_files_count` and
  `added_rows_count` in the manifest list are what is checked instead. The
  `partitions.`-prefixed per-partition summaries are likewise out of scope.
- **The ordering of `-0.0` against `+0.0`.** The floating-point order of
  "Sorting" is in scope for NaN and infinity — a positive NaN above `Infinity`
  and every finite value, a negative NaN below `-Infinity` and every finite
  value, same-signed NaNs equal, and every same-typed pair comparable — but
  whether `-0.0` sorts strictly below `+0.0` is not.
- **Catalog builder traits.** A `Catalog` implementation is checked through the
  `Catalog` trait itself; a generic builder trait for constructing catalogs
  from a name and a property map is not.
- **Cloud storage backends.** Only the local filesystem `FileIO` is
  exercised. Object-store schemes, credential handling and region
  configuration are out of scope.
- **Metadata inspection tables and Puffin.** The `inspect` and `puffin`
  modules must keep the surface the previous milestone required. No requirement
  group exercises them; the **gate** re-exercises the slice section 4.11
  restates — the two metadata-table schemas and a Puffin `Blob` — and nothing
  beyond that slice is checked. Scanning a metadata table
  (`SnapshotsTable::scan`, `ManifestsTable::scan`), reading or writing a
  Puffin file, and the `cache`, `writer` and `arrow` modules' own surface are
  not exercised at this milestone at all.
- **Retry timing.** The four `commit.retry.*` property names, their default
  values, and the requirement that an unparseable value fails the commit are
  in scope; the actual backoff schedule, the number of sleeps performed, and any
  wall-clock behaviour are not.
- **Duplicate-file detection on append.** `with_check_duplicate` must exist and
  default to enabled, but no case adds the same data file twice.
- **Snapshot retention and expiry.** A fast append must set the `main` branch
  reference with a branch retention carrying no limits; enforcing
  `min-snapshots-to-keep`, `max-snapshot-age-ms` or `max-ref-age-ms`, and
  expiring snapshots, are out of scope. Branches or tags other than `main` are
  out of scope too.
- **The metadata file's name.** The catalog must record a metadata location
  that `load_table` and `register_table` can read back; the exact file name,
  the version counter in it and the metadata-log rotation are out of scope.
- **The rest of the crate manifest.** Section 1 pins the package `name`,
  `version`, `edition` and manifest path. Only the **version** is read out of
  `/app/Cargo.toml` and in scope here, because the move to `0.6.0` is this
  milestone's own change; the `name` and the `edition` are inherited, so
  `cargo build` and the gate are what depend on them and no requirement group asserts them.
  The `rust-version` key the release also sets is out of scope.
- **Views.** View metadata, view versions and view creation through a catalog
  are not exercised at this milestone.
- Reading or writing **data file contents** — Parquet, Avro or ORC row data —
  and any writer that produces a position or equality delete file.
- Format-version-3 additions: row lineage and `first_row_id` assignment,
  deletion vectors as a distinct manifest content kind, and the
  format-version-3 metadata fields.
- **The absence of an interface.** Where section 4 or section 5 says something
  is not public or is gone — `TableCommit` has no public constructor, there is
  no public way to replace a `Table`'s metadata in place,
  `FileScanTask::data_file_content` and
  `TableScanBuilder::with_delete_file_processing_enabled` are removed —
  nothing checks that it is really absent, because a compiled driver can only
  call interfaces that exist. Signatures that *changed* are a different
  matter: section 5's new forms are the ones the drivers call, and code
  written against an old form will not compile against them.
