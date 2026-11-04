// Verifier-owned conformance driver for milestone 05 (transaction actions,
// in-core memory catalog, byte-buffer manifest-list bounds).
//
// Every assertion here traces to the pinned Apache Iceberg table specification
// shipped at /app/iceberg-spec/spec.md, or to /app/TEST_INTERFACE.md, which
// states the public Rust surface the milestone must expose.
//
// Group = the `gNN_<name>__` prefix of the test function name. The runner
// invokes one `cargo test` per group and asserts the executed count.

use std::collections::{HashMap, HashSet};
use std::future::Future;
use std::pin::Pin;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

use iceberg::io::{FileIO, FileIOBuilder};
use iceberg::spec::*;
use iceberg::table::Table;
use iceberg::transaction::{ActionCommit, ApplyTransactionAction, Transaction};
use iceberg::{
    Catalog, Error, ErrorKind, MemoryCatalog, Namespace, NamespaceIdent, Result, TableCommit,
    TableCreation, TableIdent, TableRequirement, TableUpdate,
};

// ---------------------------------------------------------------------------
// Verifier-owned harness pieces.
//
// spec.md "Metastore Tables" makes the catalog the component that atomically
// swaps one table-metadata pointer for another. `MemoryCatalog` at this
// milestone implements every catalog operation except that swap, so the
// verifier supplies the swap itself: `TxCatalog` delegates to a real
// `MemoryCatalog` and implements `Catalog::update_table` by checking the
// commit's `TableRequirement`s against the current metadata, applying its
// `TableUpdate`s, and re-pointing the catalog entry at the new metadata file.
//
// It also records every (updates, requirements) pair a `Transaction` produced,
// which is how the documented update/requirement pairs are observed, and can
// be told to serve a stale table from `load_table` in order to reproduce the
// read-then-write race described in spec.md "Optimistic Concurrency".
// ---------------------------------------------------------------------------

type Fut<'x, T> = Pin<Box<dyn Future<Output = Result<T>> + Send + 'x>>;

#[derive(Debug)]
struct TxCatalog {
    inner: MemoryCatalog,
    file_io: FileIO,
    /// When set, `load_table` returns this table instead of the stored one.
    stale: Mutex<Option<Table>>,
    recorded: Mutex<Vec<(Vec<TableUpdate>, Vec<TableRequirement>)>>,
    /// Injected `update_table` failures: how many are still to be served, and
    /// whether they report themselves as retryable.
    failures: Mutex<(usize, bool)>,
    /// When set, `update_table` hands the whole commit to
    /// `TableCommit::apply` against this table instead of checking and
    /// applying it by hand (TEST_INTERFACE.md section 4.3).
    apply_against: Mutex<Option<Table>>,
    /// `update_table` calls since the last `inject_failures`.
    attempts: AtomicUsize,
}

impl TxCatalog {
    fn new(file_io: FileIO, warehouse: String) -> Self {
        Self {
            inner: MemoryCatalog::new(file_io.clone(), Some(warehouse)),
            file_io,
            stale: Mutex::new(None),
            recorded: Mutex::new(vec![]),
            failures: Mutex::new((0, false)),
            apply_against: Mutex::new(None),
            attempts: AtomicUsize::new(0),
        }
    }

    fn serve_stale(&self, table: Table) {
        *self.stale.lock().unwrap() = Some(table);
    }

    /// Makes `update_table` apply the commit through `TableCommit::apply`
    /// against `table`, and return whatever that produced.
    fn apply_against(&self, table: Table) {
        *self.apply_against.lock().unwrap() = Some(table);
    }

    fn last_updates(&self) -> Vec<TableUpdate> {
        self.recorded.lock().unwrap().last().unwrap().0.clone()
    }

    fn last_requirements(&self) -> Vec<TableRequirement> {
        self.recorded.lock().unwrap().last().unwrap().1.clone()
    }

    /// Makes the next `count` `update_table` calls fail, and resets the
    /// attempt counter. This is how the drivers observe the retry rule of
    /// spec.md "Commit Conflict Resolution and Retry" without waiting on a
    /// real conflict.
    fn inject_failures(&self, count: usize, retryable: bool) {
        *self.failures.lock().unwrap() = (count, retryable);
        self.attempts.store(0, Ordering::SeqCst);
    }

    fn attempts(&self) -> usize {
        self.attempts.load(Ordering::SeqCst)
    }
}

impl Catalog for TxCatalog {
    fn list_namespaces<'a, 'b, 'c>(
        &'a self,
        parent: Option<&'b NamespaceIdent>,
    ) -> Fut<'c, Vec<NamespaceIdent>>
    where
        'a: 'c,
        'b: 'c,
        Self: 'c,
    {
        Box::pin(async move { self.inner.list_namespaces(parent).await })
    }

    fn create_namespace<'a, 'b, 'c>(
        &'a self,
        namespace: &'b NamespaceIdent,
        properties: HashMap<String, String>,
    ) -> Fut<'c, Namespace>
    where
        'a: 'c,
        'b: 'c,
        Self: 'c,
    {
        Box::pin(async move { self.inner.create_namespace(namespace, properties).await })
    }

    fn get_namespace<'a, 'b, 'c>(&'a self, namespace: &'b NamespaceIdent) -> Fut<'c, Namespace>
    where
        'a: 'c,
        'b: 'c,
        Self: 'c,
    {
        Box::pin(async move { self.inner.get_namespace(namespace).await })
    }

    fn namespace_exists<'a, 'b, 'c>(&'a self, namespace: &'b NamespaceIdent) -> Fut<'c, bool>
    where
        'a: 'c,
        'b: 'c,
        Self: 'c,
    {
        Box::pin(async move { self.inner.namespace_exists(namespace).await })
    }

    fn update_namespace<'a, 'b, 'c>(
        &'a self,
        namespace: &'b NamespaceIdent,
        properties: HashMap<String, String>,
    ) -> Fut<'c, ()>
    where
        'a: 'c,
        'b: 'c,
        Self: 'c,
    {
        Box::pin(async move { self.inner.update_namespace(namespace, properties).await })
    }

    fn drop_namespace<'a, 'b, 'c>(&'a self, namespace: &'b NamespaceIdent) -> Fut<'c, ()>
    where
        'a: 'c,
        'b: 'c,
        Self: 'c,
    {
        Box::pin(async move { self.inner.drop_namespace(namespace).await })
    }

    fn list_tables<'a, 'b, 'c>(&'a self, namespace: &'b NamespaceIdent) -> Fut<'c, Vec<TableIdent>>
    where
        'a: 'c,
        'b: 'c,
        Self: 'c,
    {
        Box::pin(async move { self.inner.list_tables(namespace).await })
    }

    fn create_table<'a, 'b, 'c>(
        &'a self,
        namespace: &'b NamespaceIdent,
        creation: TableCreation,
    ) -> Fut<'c, Table>
    where
        'a: 'c,
        'b: 'c,
        Self: 'c,
    {
        Box::pin(async move { self.inner.create_table(namespace, creation).await })
    }

    fn load_table<'a, 'b, 'c>(&'a self, table: &'b TableIdent) -> Fut<'c, Table>
    where
        'a: 'c,
        'b: 'c,
        Self: 'c,
    {
        Box::pin(async move {
            if let Some(stale) = self.stale.lock().unwrap().clone() {
                return Ok(stale);
            }
            self.inner.load_table(table).await
        })
    }

    fn drop_table<'a, 'b, 'c>(&'a self, table: &'b TableIdent) -> Fut<'c, ()>
    where
        'a: 'c,
        'b: 'c,
        Self: 'c,
    {
        Box::pin(async move { self.inner.drop_table(table).await })
    }

    fn table_exists<'a, 'b, 'c>(&'a self, table: &'b TableIdent) -> Fut<'c, bool>
    where
        'a: 'c,
        'b: 'c,
        Self: 'c,
    {
        Box::pin(async move { self.inner.table_exists(table).await })
    }

    fn rename_table<'a, 'b, 'd, 'c>(
        &'a self,
        src: &'b TableIdent,
        dest: &'d TableIdent,
    ) -> Fut<'c, ()>
    where
        'a: 'c,
        'b: 'c,
        'd: 'c,
        Self: 'c,
    {
        Box::pin(async move { self.inner.rename_table(src, dest).await })
    }

    fn register_table<'a, 'b, 'c>(
        &'a self,
        table: &'b TableIdent,
        metadata_location: String,
    ) -> Fut<'c, Table>
    where
        'a: 'c,
        'b: 'c,
        Self: 'c,
    {
        Box::pin(async move { self.inner.register_table(table, metadata_location).await })
    }

    fn update_table<'a, 'c>(&'a self, commit: TableCommit) -> Fut<'c, Table>
    where
        'a: 'c,
        Self: 'c,
    {
        Box::pin(async move {
            self.attempts.fetch_add(1, Ordering::SeqCst);
            {
                let mut failures = self.failures.lock().unwrap();
                if failures.0 > 0 {
                    failures.0 -= 1;
                    let retryable = failures.1;
                    return Err(Error::new(
                        ErrorKind::CatalogCommitConflicts,
                        "verifier-injected commit conflict",
                    )
                    .with_retryable(retryable));
                }
            }

            // TEST_INTERFACE.md section 4.3: `apply` checks every requirement
            // against the table it is given and then applies every update in
            // one step, returning the new metadata with the OLD metadata
            // location. A failing requirement makes it an `Err`.
            let against = self.apply_against.lock().unwrap().clone();
            if let Some(against) = against {
                return commit.apply(against);
            }

            let ident = commit.identifier().clone();
            let current = self.inner.load_table(&ident).await?;

            let mut commit = commit;
            let updates = commit.take_updates();
            let requirements = commit.take_requirements();
            self.recorded
                .lock()
                .unwrap()
                .push((updates.clone(), requirements.clone()));

            // spec.md "Optimistic Concurrency": the swap only happens when the
            // base the writer used is still current.
            for requirement in &requirements {
                requirement.check(Some(current.metadata()))?;
            }

            let mut builder = current.metadata().clone().into_builder(None);
            for update in updates {
                builder = update.apply(builder)?;
            }
            let metadata = builder.build()?.metadata;

            let new_location = format!(
                "{}/metadata/{}.metadata.json",
                metadata.location(),
                next_metadata_uuid()
            );
            self.file_io
                .new_output(&new_location)?
                .write(serde_json::to_vec(&metadata).expect("serialise table metadata").into())
                .await?;
            self.inner.drop_table(&ident).await?;
            self.inner.register_table(&ident, new_location).await
        })
    }
}

fn next_metadata_uuid() -> uuid::Uuid {
    // `Uuid::new_v4()` is behind uuid's non-default `v4` feature; the driver
    // manifest does not enable it and must not depend on a candidate's graph
    // enabling it transitively. `from_u128` is feature-free.
    static N: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);
    uuid::Uuid::from_u128(N.fetch_add(1, std::sync::atomic::Ordering::Relaxed) as u128)
}

fn test_schema() -> Schema {
    Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "name", Type::Primitive(PrimitiveType::String)).into(),
        ])
        .build()
        .unwrap()
}

fn temp_catalog() -> (tempfile::TempDir, TxCatalog) {
    let dir = tempfile::TempDir::new().unwrap();
    let warehouse = dir.path().to_str().unwrap().to_string();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let catalog = TxCatalog::new(io, warehouse);
    (dir, catalog)
}

fn db() -> NamespaceIdent {
    NamespaceIdent::from_strs(["db"]).unwrap()
}

/// Creates namespace `db` and an unpartitioned v2 table `db.<name>`.
async fn fresh_table(catalog: &TxCatalog, name: &str) -> Table {
    catalog.create_namespace(&db(), HashMap::new()).await.unwrap();
    catalog
        .create_table(
            &db(),
            TableCreation::builder()
                .name(name.to_string())
                .schema(test_schema())
                .build(),
        )
        .await
        .unwrap()
}

fn a_data_file(path: &str, spec_id: i32, records: u64, size: u64) -> DataFile {
    DataFileBuilder::default()
        .content(DataContentType::Data)
        .file_path(path.to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .partition_spec_id(spec_id)
        .record_count(records)
        .file_size_in_bytes(size)
        .build()
        .unwrap()
}

// ---------------------------------------------------------------------------
// g01_catalog_ns — TEST_INTERFACE.md `MemoryCatalog`/`Catalog` namespace
// contract; spec.md "Metastore Tables" (catalog holds the table pointers).
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g01_catalog_ns__create_get_and_exists() {
    let (_dir, catalog) = temp_catalog();
    let ident = NamespaceIdent::from_strs(["accounting"]).unwrap();
    let props = HashMap::from([("owner".to_string(), "ops".to_string())]);

    assert!(!catalog.namespace_exists(&ident).await.unwrap());
    let created = catalog
        .create_namespace(&ident, props.clone())
        .await
        .unwrap();
    assert_eq!(created.name(), &ident);
    assert_eq!(created.properties(), &props);
    assert!(catalog.namespace_exists(&ident).await.unwrap());

    let loaded = catalog.get_namespace(&ident).await.unwrap();
    assert_eq!(loaded.name(), &ident);
    assert_eq!(loaded.properties(), &props);
}

#[tokio::test]
async fn g01_catalog_ns__lists_top_level_and_children() {
    let (_dir, catalog) = temp_catalog();
    let a = NamespaceIdent::from_strs(["a"]).unwrap();
    let b = NamespaceIdent::from_strs(["b"]).unwrap();
    let a_child = NamespaceIdent::from_strs(["a", "child"]).unwrap();
    for ns in [&a, &b, &a_child] {
        catalog.create_namespace(ns, HashMap::new()).await.unwrap();
    }

    let mut top = catalog.list_namespaces(None).await.unwrap();
    top.sort();
    assert_eq!(top, vec![a.clone(), b.clone()]);

    // Children are reported by their own single-level name.
    let children = catalog.list_namespaces(Some(&a)).await.unwrap();
    assert_eq!(children, vec![NamespaceIdent::from_strs(["child"]).unwrap()]);
    assert!(catalog.list_namespaces(Some(&b)).await.unwrap().is_empty());
    assert!(catalog.namespace_exists(&a_child).await.unwrap());
}

#[tokio::test]
async fn g01_catalog_ns__update_replaces_full_property_set() {
    let (_dir, catalog) = temp_catalog();
    let ident = NamespaceIdent::from_strs(["ns"]).unwrap();
    catalog
        .create_namespace(
            &ident,
            HashMap::from([
                ("k1".to_string(), "v1".to_string()),
                ("k2".to_string(), "v2".to_string()),
            ]),
        )
        .await
        .unwrap();

    let replacement = HashMap::from([("k2".to_string(), "changed".to_string())]);
    catalog
        .update_namespace(&ident, replacement.clone())
        .await
        .unwrap();
    assert_eq!(
        catalog.get_namespace(&ident).await.unwrap().properties(),
        &replacement
    );
}

#[tokio::test]
async fn g01_catalog_ns__drop_removes_it() {
    let (_dir, catalog) = temp_catalog();
    let ident = NamespaceIdent::from_strs(["gone"]).unwrap();
    catalog.create_namespace(&ident, HashMap::new()).await.unwrap();
    catalog.drop_namespace(&ident).await.unwrap();
    assert!(!catalog.namespace_exists(&ident).await.unwrap());
    assert!(catalog.get_namespace(&ident).await.is_err());
    assert!(catalog.drop_namespace(&ident).await.is_err());
}

// ---------------------------------------------------------------------------
// g02_catalog_table — spec.md "Table Metadata Fields" (what a freshly created
// table must record) and TEST_INTERFACE.md `MemoryCatalog` table contract.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g02_catalog_table__create_records_table_metadata_fields() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t1").await;

    assert_eq!(table.identifier(), &TableIdent::from_strs(["db", "t1"]).unwrap());
    let md = table.metadata();
    assert_eq!(md.format_version(), FormatVersion::V2);
    assert_eq!(md.current_schema().as_struct().fields().len(), 2);
    assert_eq!(md.current_schema().field_by_name("id").unwrap().id, 1);
    // "Table Metadata Fields": a table with no snapshots has no current
    // snapshot and a last-sequence-number of 0.
    assert!(md.current_snapshot().is_none());
    assert!(md.current_snapshot_id().is_none());
    assert_eq!(md.last_sequence_number(), 0);
    assert_eq!(md.snapshots().count(), 0);
    assert_eq!(md.history().len(), 0);
    assert!(md.default_sort_order().is_unsorted());
    assert!(md.default_partition_spec().is_unpartitioned());
    assert!(table.metadata_location().is_some());
}

#[tokio::test]
async fn g02_catalog_table__last_column_and_partition_ids() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t2").await;
    let md = table.metadata();
    // "Table Metadata Fields": `last-column-id` is the highest assigned column
    // id and `last-partition-id` the highest assigned partition field id.
    assert_eq!(md.last_column_id(), md.current_schema().highest_field_id());
    assert_eq!(md.last_column_id(), 2);
    assert_eq!(md.last_partition_id(), 999);
}

#[tokio::test]
async fn g02_catalog_table__load_and_list() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t3").await;
    let ident = table.identifier().clone();

    assert!(catalog.table_exists(&ident).await.unwrap());
    assert_eq!(catalog.list_tables(&db()).await.unwrap(), vec![ident.clone()]);

    let loaded = catalog.load_table(&ident).await.unwrap();
    assert_eq!(loaded.identifier(), &ident);
    assert_eq!(loaded.metadata().uuid(), table.metadata().uuid());
    assert_eq!(loaded.metadata(), table.metadata());
    assert_eq!(loaded.metadata_location(), table.metadata_location());
}

#[tokio::test]
async fn g02_catalog_table__location_comes_from_namespace_or_request() {
    let (dir, catalog) = temp_catalog();
    let warehouse = dir.path().to_str().unwrap().to_string();
    catalog.create_namespace(&db(), HashMap::new()).await.unwrap();

    let derived = catalog
        .create_table(
            &db(),
            TableCreation::builder()
                .name("derived".to_string())
                .schema(test_schema())
                .build(),
        )
        .await
        .unwrap();
    assert_eq!(
        derived.metadata().location(),
        format!("{warehouse}/db/derived")
    );

    let explicit_location = format!("{warehouse}/custom/place");
    let explicit = catalog
        .create_table(
            &db(),
            TableCreation::builder()
                .name("explicit".to_string())
                .location(explicit_location.clone())
                .schema(test_schema())
                .build(),
        )
        .await
        .unwrap();
    assert_eq!(explicit.metadata().location(), explicit_location);
}

#[tokio::test]
async fn g02_catalog_table__location_prefers_the_namespace_property() {
    let (dir, catalog) = temp_catalog();
    let ns_location = format!("{}/ns-owned", dir.path().to_str().unwrap());
    let ns = NamespaceIdent::from_strs(["propns"]).unwrap();
    catalog
        .create_namespace(
            &ns,
            HashMap::from([("location".to_string(), ns_location.clone())]),
        )
        .await
        .unwrap();

    // With no explicit location the namespace's own `location` property is the
    // prefix, in preference to the catalog's warehouse location, and the table
    // name is appended to it.
    let table = catalog
        .create_table(
            &ns,
            TableCreation::builder()
                .name("t".to_string())
                .schema(test_schema())
                .build(),
        )
        .await
        .unwrap();
    assert_eq!(table.metadata().location(), format!("{ns_location}/t"));
    // "Metastore Tables": the metadata document the catalog points at sits
    // under the table's own location.
    assert!(
        table
            .metadata_location()
            .expect("a created table has a metadata location")
            .starts_with(&format!("{ns_location}/t")),
        "metadata location {:?} must sit under the table location",
        table.metadata_location()
    );
}

// ---------------------------------------------------------------------------
// g03_catalog_move — TEST_INTERFACE.md `MemoryCatalog` rename/drop/register
// contract; spec.md "Metastore Tables" (the catalog owns the metadata pointer).
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g03_catalog_move__rename_preserves_metadata() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "before").await;
    let src = table.identifier().clone();
    let dest = TableIdent::from_strs(["db", "after"]).unwrap();

    catalog.rename_table(&src, &dest).await.unwrap();
    assert!(!catalog.table_exists(&src).await.unwrap());
    assert!(catalog.table_exists(&dest).await.unwrap());

    let renamed = catalog.load_table(&dest).await.unwrap();
    assert_eq!(renamed.identifier(), &dest);
    assert_eq!(renamed.metadata().uuid(), table.metadata().uuid());
    assert_eq!(renamed.metadata_location(), table.metadata_location());
}

#[tokio::test]
async fn g03_catalog_move__drop_removes_the_pointer() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "doomed").await;
    let ident = table.identifier().clone();
    let metadata_file = table
        .metadata_location()
        .expect("a created table has a metadata location")
        .to_string();
    assert!(
        std::fs::metadata(&metadata_file).is_ok(),
        "the metadata document must exist before the drop: {metadata_file}"
    );

    catalog.drop_table(&ident).await.unwrap();
    assert!(!catalog.table_exists(&ident).await.unwrap());
    assert!(catalog.list_tables(&db()).await.unwrap().is_empty());
    assert!(catalog.load_table(&ident).await.is_err());
    // Section 4.2: `drop_table` removes the pointer AND the metadata file.
    assert!(
        std::fs::metadata(&metadata_file).is_err(),
        "drop_table must delete the metadata document, {metadata_file} is still there"
    );

    // Dropping a table that does not exist is an error.
    assert!(catalog.drop_table(&ident).await.is_err());
}

#[tokio::test]
async fn g03_catalog_move__register_existing_metadata() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "src").await;
    let metadata_location = table.metadata_location().unwrap().to_string();

    let clone_ident = TableIdent::from_strs(["db", "clone"]).unwrap();
    let registered = catalog
        .register_table(&clone_ident, metadata_location.clone())
        .await
        .unwrap();
    assert_eq!(registered.identifier(), &clone_ident);
    assert_eq!(registered.metadata().uuid(), table.metadata().uuid());
    assert_eq!(registered.metadata_location(), Some(metadata_location.as_str()));

    let mut listed = catalog.list_tables(&db()).await.unwrap();
    listed.sort_by(|a, b| a.name().cmp(b.name()));
    assert_eq!(listed, vec![clone_ident, table.identifier().clone()]);
}

// ---------------------------------------------------------------------------
// g04_catalog_reject — documented catalog failures.
// TEST_INTERFACE.md `MemoryCatalog` contract.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g04_catalog_reject__duplicate_table_and_namespace() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "dup").await;
    let creation = TableCreation::builder()
        .name(table.identifier().name().to_string())
        .schema(test_schema())
        .build();
    assert!(
        catalog.create_table(&db(), creation).await.is_err(),
        "creating a table that already exists must be rejected"
    );
    assert!(
        catalog.create_namespace(&db(), HashMap::new()).await.is_err(),
        "creating a namespace that already exists must be rejected"
    );
}

#[tokio::test]
async fn g04_catalog_reject__missing_table_or_namespace() {
    let (_dir, catalog) = temp_catalog();
    let missing = TableIdent::from_strs(["db", "nope"]).unwrap();
    assert!(
        catalog.load_table(&missing).await.is_err(),
        "loading a table that does not exist must be rejected"
    );
    assert!(catalog.drop_table(&missing).await.is_err());
    assert!(
        catalog
            .create_table(
                &db(),
                TableCreation::builder()
                    .name("orphan".to_string())
                    .schema(test_schema())
                    .build(),
            )
            .await
            .is_err(),
        "creating a table under a namespace that does not exist must be rejected"
    );
}

#[tokio::test]
async fn g04_catalog_reject__namespace_parent_must_exist() {
    let (_dir, catalog) = temp_catalog();
    let orphan = NamespaceIdent::from_strs(["absent", "child"]).unwrap();
    assert!(
        catalog
            .create_namespace(&orphan, HashMap::new())
            .await
            .is_err(),
        "creating a namespace whose parent does not exist must be rejected"
    );
    assert!(!catalog.namespace_exists(&orphan).await.unwrap());

    // The same call succeeds once the parent is there.
    let parent = NamespaceIdent::from_strs(["absent"]).unwrap();
    catalog
        .create_namespace(&parent, HashMap::new())
        .await
        .unwrap();
    catalog
        .create_namespace(&orphan, HashMap::new())
        .await
        .unwrap();
    assert!(catalog.namespace_exists(&orphan).await.unwrap());
}

#[tokio::test]
async fn g04_catalog_reject__create_table_with_no_location_anywhere() {
    // No explicit location, no namespace `location` property and no warehouse
    // location: the catalog has nowhere to put the table.
    let dir = tempfile::TempDir::new().unwrap();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let catalog = MemoryCatalog::new(io, None);
    catalog.create_namespace(&db(), HashMap::new()).await.unwrap();
    assert!(
        catalog
            .create_table(
                &db(),
                TableCreation::builder()
                    .name("nowhere".to_string())
                    .schema(test_schema())
                    .build(),
            )
            .await
            .is_err(),
        "create_table with no location, no namespace location and no warehouse \
         location must be rejected"
    );

    // An explicit location is enough on the same catalog.
    let location = format!("{}/explicit", dir.path().to_str().unwrap());
    let table = catalog
        .create_table(
            &db(),
            TableCreation::builder()
                .name("somewhere".to_string())
                .location(location.clone())
                .schema(test_schema())
                .build(),
        )
        .await
        .unwrap();
    assert_eq!(table.metadata().location(), location);
}

#[tokio::test]
async fn g04_catalog_reject__rename_onto_existing_name() {
    let (_dir, catalog) = temp_catalog();
    let one = fresh_table(&catalog, "one").await;
    catalog
        .create_table(
            &db(),
            TableCreation::builder()
                .name("two".to_string())
                .schema(test_schema())
                .build(),
        )
        .await
        .unwrap();
    let two = TableIdent::from_strs(["db", "two"]).unwrap();
    assert!(catalog.rename_table(one.identifier(), &two).await.is_err());
    // Both tables survive a rejected rename.
    assert!(catalog.table_exists(one.identifier()).await.unwrap());
    assert!(catalog.table_exists(&two).await.unwrap());
}

#[tokio::test]
async fn g04_catalog_reject__memory_catalog_cannot_swap_metadata() {
    // `MemoryCatalog` implements every catalog read/write except the metadata
    // swap of spec.md "Metastore Tables"; `update_table` must report that
    // rather than silently succeeding.
    let dir = tempfile::TempDir::new().unwrap();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let memory = MemoryCatalog::new(io, Some(dir.path().to_str().unwrap().to_string()));
    memory.create_namespace(&db(), HashMap::new()).await.unwrap();
    let table = memory
        .create_table(
            &db(),
            TableCreation::builder()
                .name("t".to_string())
                .schema(test_schema())
                .build(),
        )
        .await
        .unwrap();

    let tx = Transaction::new(&table);
    let tx = tx
        .update_table_properties()
        .set("a".to_string(), "b".to_string())
        .apply(tx)
        .unwrap();
    assert!(tx.commit(&memory).await.is_err());
}

// ---------------------------------------------------------------------------
// g05_append_snapshot — spec.md "Snapshots" and "Sequence Numbers".
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g05_append_snapshot__first_append_creates_snapshot() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let spec_id = table.metadata().default_partition_spec_id();
    let location = table.metadata().location().to_string();

    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/1.parquet"),
            spec_id,
            4,
            100,
        )])
        .apply(tx)
        .unwrap();
    let committed = tx.commit(&catalog).await.unwrap();

    let md = committed.metadata();
    let snapshot = md.current_snapshot().expect("append must create a snapshot");
    // "Snapshots": the first snapshot has no parent.
    assert_eq!(snapshot.parent_snapshot_id(), None);
    // "Sequence Numbers": every successful commit is assigned the next
    // sequence number, starting from 1.
    assert_eq!(snapshot.sequence_number(), 1);
    assert_eq!(md.last_sequence_number(), 1);
    assert_eq!(snapshot.schema_id(), Some(md.current_schema_id()));
    assert!(!snapshot.manifest_list().is_empty());
    // "Snapshot References": the append advances the `main` branch.
    assert_eq!(
        md.snapshot_for_ref(MAIN_BRANCH).unwrap().snapshot_id(),
        snapshot.snapshot_id()
    );
    // "Snapshots": the snapshot log gains one entry for the new snapshot.
    assert_eq!(md.history().len(), 1);
    assert_eq!(md.history()[0].snapshot_id, snapshot.snapshot_id());
    assert_eq!(md.history()[0].timestamp_ms, snapshot.timestamp_ms());

    // Section 4.5: a fast append emits exactly
    // `[AddSnapshot, SetSnapshotRef { "main", branch(None, None, None) }]`,
    // in that order.
    let updates = catalog.last_updates();
    assert_eq!(
        updates.len(),
        2,
        "a fast append emits two updates, got {updates:?}"
    );
    let TableUpdate::AddSnapshot { snapshot: added } = &updates[0] else {
        panic!(
            "the first update must add the new snapshot, got {:?}",
            updates[0]
        )
    };
    assert_eq!(added.snapshot_id(), snapshot.snapshot_id());
    assert_eq!(added.sequence_number(), snapshot.sequence_number());
    // The reference carries a branch retention with no limits.
    assert_eq!(updates[1], TableUpdate::SetSnapshotRef {
        ref_name: MAIN_BRANCH.to_string(),
        reference: SnapshotReference::new(
            snapshot.snapshot_id(),
            SnapshotRetention::branch(None, None, None)
        )
    });
}

#[tokio::test]
async fn g05_append_snapshot__second_append_chains_and_increments() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let spec_id = table.metadata().default_partition_spec_id();
    let location = table.metadata().location().to_string();

    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/1.parquet"),
            spec_id,
            2,
            100,
        )])
        .apply(tx)
        .unwrap();
    let first = tx.commit(&catalog).await.unwrap();
    let first_id = first.metadata().current_snapshot().unwrap().snapshot_id();

    let tx = Transaction::new(&first);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/2.parquet"),
            spec_id,
            3,
            120,
        )])
        .apply(tx)
        .unwrap();
    let second = tx.commit(&catalog).await.unwrap();

    let md = second.metadata();
    let snapshot = md.current_snapshot().unwrap();
    // "Snapshots": `parent-snapshot-id` is the id of the snapshot that was
    // current when the commit started.
    assert_eq!(snapshot.parent_snapshot_id(), Some(first_id));
    // "Sequence Numbers": one higher than the previous commit.
    assert_eq!(snapshot.sequence_number(), 2);
    assert_eq!(md.last_sequence_number(), 2);
    assert_eq!(md.snapshots().count(), 2);
    assert!(md.snapshot_by_id(first_id).is_some());
    // The snapshot log is append-ordered.
    assert_eq!(md.history().len(), 2);
    assert_eq!(md.history()[0].snapshot_id, first_id);
    assert_eq!(md.history()[1].snapshot_id, snapshot.snapshot_id());
}

#[tokio::test]
async fn g05_append_snapshot__manifest_list_tracks_added_and_existing() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let spec_id = table.metadata().default_partition_spec_id();
    let location = table.metadata().location().to_string();

    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/1.parquet"),
            spec_id,
            7,
            100,
        )])
        .apply(tx)
        .unwrap();
    let first = tx.commit(&catalog).await.unwrap();

    let snapshot = first.metadata().current_snapshot().unwrap();
    let bytes = std::fs::read(snapshot.manifest_list()).unwrap();
    assert_eq!(&bytes[..4], b"Obj\x01", "manifest list must be an Avro file");
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2).unwrap();
    assert_eq!(list.entries().len(), 1);
    let entry = &list.entries()[0];
    // "Manifest Lists": counts describe the entries in the manifest.
    assert_eq!(entry.added_files_count, Some(1));
    assert_eq!(entry.existing_files_count, Some(0));
    assert_eq!(entry.deleted_files_count, Some(0));
    assert_eq!(entry.added_rows_count, Some(7));
    assert_eq!(entry.content, ManifestContentType::Data);
    assert_eq!(entry.added_snapshot_id, snapshot.snapshot_id());
    // "Sequence Numbers": a manifest added by this commit carries the
    // snapshot's sequence number.
    assert_eq!(entry.sequence_number, snapshot.sequence_number());

    // A second fast append keeps the earlier manifest in the new list.
    let tx = Transaction::new(&first);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/2.parquet"),
            spec_id,
            1,
            100,
        )])
        .apply(tx)
        .unwrap();
    let second = tx.commit(&catalog).await.unwrap();
    let bytes = std::fs::read(second.metadata().current_snapshot().unwrap().manifest_list())
        .unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2).unwrap();
    assert_eq!(list.entries().len(), 2);
    assert!(
        list.entries()
            .iter()
            .any(|e| e.manifest_path == entry.manifest_path),
        "the manifest written by the first append must still be listed"
    );
}

#[tokio::test]
async fn g05_append_snapshot__transaction_owns_its_base_table() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let spec_id = table.metadata().default_partition_spec_id();
    let location = table.metadata().location().to_string();

    // The transaction keeps its own copy of the base table: it outlives the
    // binding it was constructed from.
    let tx = {
        let base = table.clone();
        let tx = Transaction::new(&base);
        tx.fast_append()
            .add_data_files(vec![a_data_file(
                &format!("{location}/data/1.parquet"),
                spec_id,
                1,
                50,
            )])
            .apply(tx)
            .unwrap()
    };
    // A `Transaction` is `Clone`: a caller may keep a copy of the configured
    // transaction and commit that copy instead.
    let copy = tx.clone();
    drop(tx);
    let committed = copy.commit(&catalog).await.unwrap();
    assert!(committed.metadata().current_snapshot().is_some());
}

#[tokio::test]
async fn g05_append_snapshot__commit_uuid_and_key_metadata_are_carried() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let spec_id = table.metadata().default_partition_spec_id();
    let location = table.metadata().location().to_string();
    let commit_uuid = uuid::Uuid::from_u128(0x1234_5678_9abc_def0_1122_3344_5566_7788);
    let key_metadata = vec![7u8, 8, 9];

    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .set_commit_uuid(commit_uuid)
        .set_key_metadata(key_metadata.clone())
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/1.parquet"),
            spec_id,
            2,
            80,
        )])
        .apply(tx)
        .unwrap();
    let committed = tx.commit(&catalog).await.unwrap();

    let snapshot = committed.metadata().current_snapshot().unwrap();
    let bytes = std::fs::read(snapshot.manifest_list()).unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2).unwrap();
    assert_eq!(list.entries().len(), 1);
    // "Manifest Lists" field 519: the key metadata the action was given is the
    // manifest's, and it comes back through the manifest list.
    assert_eq!(list.entries()[0].key_metadata, Some(key_metadata));
    // The commit uuid names the files the commit wrote.
    let uuid_text = commit_uuid.to_string();
    assert!(
        snapshot.manifest_list().contains(&uuid_text)
            || list.entries()[0].manifest_path.contains(&uuid_text),
        "the commit uuid must appear in a path the commit wrote: list {}, manifest {}",
        snapshot.manifest_list(),
        list.entries()[0].manifest_path
    );
}

// ---------------------------------------------------------------------------
// g06_append_summary — spec.md "Snapshots" (`summary` must carry `operation`)
// and Appendix F "Optional Snapshot Summary Fields".
// ---------------------------------------------------------------------------

/// Every metric name Appendix F "Optional Snapshot Summary Fields" defines.
const SPEC_SUMMARY_METRICS: [&str; 26] = [
    "added-data-files",
    "deleted-data-files",
    "total-data-files",
    "added-delete-files",
    "added-equality-delete-files",
    "removed-equality-delete-files",
    "added-position-delete-files",
    "removed-position-delete-files",
    "added-dvs",
    "removed-dvs",
    "removed-delete-files",
    "total-delete-files",
    "added-records",
    "deleted-records",
    "total-records",
    "added-files-size",
    "removed-files-size",
    "total-files-size",
    "added-position-deletes",
    "removed-position-deletes",
    "total-position-deletes",
    "added-equality-deletes",
    "removed-equality-deletes",
    "total-equality-deletes",
    "deleted-duplicate-files",
    "changed-partition-count",
];

async fn append_once(catalog: &TxCatalog, extra: HashMap<String, String>) -> Table {
    let table = fresh_table(catalog, "t").await;
    let spec_id = table.metadata().default_partition_spec_id();
    let location = table.metadata().location().to_string();
    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .set_snapshot_properties(extra)
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/1.parquet"),
            spec_id,
            5,
            250,
        )])
        .apply(tx)
        .unwrap();
    tx.commit(catalog).await.unwrap()
}

#[tokio::test]
async fn g06_append_summary__operation_is_append() {
    let (_dir, catalog) = temp_catalog();
    let table = append_once(&catalog, HashMap::new()).await;
    let summary = table.metadata().current_snapshot().unwrap().summary();
    // "Snapshots": `operation` is a required summary field.
    assert_eq!(summary.operation, Operation::Append);
    // The same summary survives the metadata JSON round trip the catalog does.
    let json = serde_json::to_value(table.metadata()).unwrap();
    assert_eq!(json["snapshots"][0]["summary"]["operation"], "append");
}

#[tokio::test]
async fn g06_append_summary__metric_keys_and_values_follow_the_spec() {
    let (_dir, catalog) = temp_catalog();
    let table = append_once(&catalog, HashMap::new()).await;
    let summary = table.metadata().current_snapshot().unwrap().summary();
    let allowed: HashSet<&str> = SPEC_SUMMARY_METRICS.into_iter().collect();

    assert!(
        !summary.additional_properties.is_empty(),
        "a fast append must record summary metrics"
    );
    for (key, value) in &summary.additional_properties {
        // Appendix F "Optional Snapshot Summary Fields": a per-partition
        // summary is a `partitions.<partition-path>` key whose value is a
        // `k=v,k=v` list of that partition's metrics, not a single count.
        // Section 8 leaves those unscored, so only the metric names below are
        // checked for a decimal value.
        if key.starts_with("partitions.") {
            continue;
        }
        assert!(
            allowed.contains(key.as_str()),
            "summary key {key} is not a metric named by the specification"
        );
        // Appendix F: "The value of these fields should be of string type
        // (e.g., \"120\")" — every metric is a decimal count.
        assert!(
            value.parse::<i64>().is_ok(),
            "summary metric {key} must hold a numeric string, got {value}"
        );
    }
    for required in ["total-data-files", "total-records", "total-files-size"] {
        assert!(
            summary.additional_properties.contains_key(required),
            "summary must carry the running total {required}"
        );
    }
}

#[tokio::test]
async fn g06_append_summary__caller_supplied_properties_replace_and_are_kept() {
    let (_dir, catalog) = temp_catalog();
    // Appendix F "Other Fields" names `engine-name`/`engine-version`.
    let extra = HashMap::from([
        ("engine-name".to_string(), "verifier".to_string()),
        ("engine-version".to_string(), "1".to_string()),
    ]);
    // Section 4.4: `set_snapshot_properties` **replaces** the map it is
    // given, so a second call discards the first call's properties entirely
    // rather than merging with them.
    let discarded = HashMap::from([
        ("engine-name".to_string(), "discarded".to_string()),
        ("engine-extra".to_string(), "discarded".to_string()),
    ]);

    let table = fresh_table(&catalog, "t").await;
    let spec_id = table.metadata().default_partition_spec_id();
    let location = table.metadata().location().to_string();
    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .set_snapshot_properties(discarded)
        .set_snapshot_properties(extra.clone())
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/1.parquet"),
            spec_id,
            5,
            250,
        )])
        .apply(tx)
        .unwrap();
    let table = tx.commit(&catalog).await.unwrap();

    let summary = table.metadata().current_snapshot().unwrap().summary();
    for (key, value) in &extra {
        assert_eq!(
            summary.additional_properties.get(key),
            Some(value),
            "snapshot property {key} must appear in the summary"
        );
    }
    assert!(
        !summary
            .additional_properties
            .contains_key("engine-extra"),
        "the second set_snapshot_properties must replace the first map, not merge with it: {:?}",
        summary.additional_properties
    );
}

// ---------------------------------------------------------------------------
// g07_props_loc — spec.md "Table Metadata Fields" (`properties`, `location`).
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g07_props_loc__property_update_pair_and_effect() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;

    let tx = Transaction::new(&table);
    let tx = tx
        .update_table_properties()
        .set("read.split.target-size".to_string(), "134217728".to_string())
        .set("doomed".to_string(), "value".to_string())
        .apply(tx)
        .unwrap();
    let with_props = tx.commit(&catalog).await.unwrap();
    assert_eq!(
        with_props
            .metadata()
            .properties()
            .get("read.split.target-size")
            .map(String::as_str),
        Some("134217728")
    );

    let tx = Transaction::new(&with_props);
    let tx = tx
        .update_table_properties()
        .remove("doomed".to_string())
        .apply(tx)
        .unwrap();
    let removed = tx.commit(&catalog).await.unwrap();
    assert!(!removed.metadata().properties().contains_key("doomed"));
    assert!(
        removed
            .metadata()
            .properties()
            .contains_key("read.split.target-size")
    );

    // The action produces exactly one `set-properties` and one
    // `remove-properties` update, and asserts nothing about the base table.
    let updates = catalog.last_updates();
    assert_eq!(updates.len(), 2);
    assert_eq!(updates[0], TableUpdate::SetProperties {
        updates: HashMap::new()
    });
    assert_eq!(updates[1], TableUpdate::RemoveProperties {
        removals: vec!["doomed".to_string()]
    });
    assert!(catalog.last_requirements().is_empty());
}

#[tokio::test]
async fn g07_props_loc__update_serializes_with_documented_action_names() {
    // Appendix C / the catalog wire form: updates are tagged objects.
    let set = TableUpdate::SetProperties {
        updates: HashMap::from([("a".to_string(), "b".to_string())]),
    };
    let json = serde_json::to_value(&set).unwrap();
    assert_eq!(json["action"], "set-properties");
    assert_eq!(json["updates"]["a"], "b");

    let remove = TableUpdate::RemoveProperties {
        removals: vec!["a".to_string()],
    };
    let json = serde_json::to_value(&remove).unwrap();
    assert_eq!(json["action"], "remove-properties");
    assert_eq!(json["removals"], serde_json::json!(["a"]));

    let location = TableUpdate::SetLocation {
        location: "s3://bucket/t".to_string(),
    };
    let json = serde_json::to_value(&location).unwrap();
    assert_eq!(json["action"], "set-location");
    assert_eq!(json["location"], "s3://bucket/t");
}

#[tokio::test]
async fn g07_props_loc__location_update_pair_and_effect() {
    let (dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let moved = format!("{}/relocated", dir.path().to_str().unwrap());

    let tx = Transaction::new(&table);
    let tx = tx
        .update_location()
        .set_location(moved.clone())
        .apply(tx)
        .unwrap();
    let committed = tx.commit(&catalog).await.unwrap();

    assert_eq!(committed.metadata().location(), moved);
    assert_eq!(catalog.last_updates(), vec![TableUpdate::SetLocation {
        location: moved
    }]);
    assert!(catalog.last_requirements().is_empty());
}

#[tokio::test]
async fn g07_props_loc__reserved_properties_are_rejected() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    // "Table Metadata Fields": `format-version` is metadata, not a table
    // property, so it must not be settable through `properties`.
    let tx = Transaction::new(&table);
    let tx = tx
        .update_table_properties()
        .set("format-version".to_string(), "3".to_string())
        .apply(tx)
        .unwrap();
    assert!(tx.commit(&catalog).await.is_err());
    assert_eq!(
        catalog.load_table(table.identifier()).await.unwrap().metadata(),
        table.metadata(),
        "a rejected commit must leave the table unchanged"
    );
}

// ---------------------------------------------------------------------------
// g08_format_upgrade — spec.md Appendix E "Format version changes";
// "Table Metadata Fields" (`format-version`).
// ---------------------------------------------------------------------------

const V1_METADATA_TEMPLATE: &str = r#"{
  "format-version": 1,
  "table-uuid": "d20125c8-7284-442c-9aea-15fee620737c",
  "location": "LOCATION",
  "last-updated-ms": 1602638573874,
  "last-column-id": 2,
  "schema": {
    "type": "struct",
    "schema-id": 0,
    "fields": [
      {"id": 1, "name": "id", "required": true, "type": "long"},
      {"id": 2, "name": "name", "required": false, "type": "string"}
    ]
  },
  "partition-spec": [],
  "properties": {},
  "current-snapshot-id": -1,
  "snapshots": []
}"#;

async fn register_v1_table(dir: &tempfile::TempDir, catalog: &TxCatalog) -> Table {
    catalog.create_namespace(&db(), HashMap::new()).await.unwrap();
    let warehouse = dir.path().to_str().unwrap().to_string();
    let location = format!("{warehouse}/db/v1table");
    let json = V1_METADATA_TEMPLATE.replace("LOCATION", &location);
    let metadata_location = format!("{location}/metadata/v1.metadata.json");
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    io.new_output(&metadata_location)
        .unwrap()
        .write(json.into_bytes().into())
        .await
        .unwrap();
    catalog
        .register_table(
            &TableIdent::from_strs(["db", "v1table"]).unwrap(),
            metadata_location,
        )
        .await
        .unwrap()
}

#[tokio::test]
async fn g08_format_upgrade__v1_to_v2() {
    let (dir, catalog) = temp_catalog();
    let table = register_v1_table(&dir, &catalog).await;
    assert_eq!(table.metadata().format_version(), FormatVersion::V1);

    let tx = Transaction::new(&table);
    let tx = tx
        .upgrade_table_version()
        .set_format_version(FormatVersion::V2)
        .apply(tx)
        .unwrap();
    let upgraded = tx.commit(&catalog).await.unwrap();

    assert_eq!(upgraded.metadata().format_version(), FormatVersion::V2);
    assert_eq!(catalog.last_updates(), vec![
        TableUpdate::UpgradeFormatVersion {
            format_version: FormatVersion::V2
        }
    ]);
    assert!(catalog.last_requirements().is_empty());
    // Appendix E "Version 2": the schema and its column ids survive the
    // upgrade unchanged.
    assert_eq!(
        upgraded.metadata().current_schema().as_struct(),
        table.metadata().current_schema().as_struct()
    );
    assert_eq!(
        serde_json::to_value(upgraded.metadata()).unwrap()["format-version"],
        2
    );
}

#[tokio::test]
async fn g08_format_upgrade__same_version_is_accepted() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let tx = Transaction::new(&table);
    let tx = tx
        .upgrade_table_version()
        .set_format_version(FormatVersion::V2)
        .apply(tx)
        .unwrap();
    let committed = tx.commit(&catalog).await.unwrap();
    assert_eq!(committed.metadata().format_version(), FormatVersion::V2);
}

#[tokio::test]
async fn g08_format_upgrade__downgrade_is_rejected() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    assert_eq!(table.metadata().format_version(), FormatVersion::V2);
    // Appendix E only describes forward version changes; a v2 table must not
    // be downgraded to v1.
    let tx = Transaction::new(&table);
    let tx = tx
        .upgrade_table_version()
        .set_format_version(FormatVersion::V1)
        .apply(tx)
        .unwrap();
    assert!(tx.commit(&catalog).await.is_err());
}

// ---------------------------------------------------------------------------
// g09_sort_order — spec.md "Sorting"; Appendix C "Sort Orders".
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g09_sort_order__replace_produces_documented_pair() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;

    let tx = Transaction::new(&table);
    let tx = tx
        .replace_sort_order()
        .asc("name", NullOrder::First)
        .desc("id", NullOrder::Last)
        .apply(tx)
        .unwrap();
    tx.commit(&catalog).await.unwrap();

    let updates = catalog.last_updates();
    assert_eq!(updates.len(), 2);
    let TableUpdate::AddSortOrder { sort_order } = &updates[0] else {
        panic!("first update must add the new sort order, got {:?}", updates[0])
    };
    assert_eq!(sort_order.fields.len(), 2);
    // "Sorting": a sort field names a source column, a transform, a direction
    // and a null order.
    assert_eq!(sort_order.fields[0].source_id, 2);
    assert_eq!(sort_order.fields[0].transform, Transform::Identity);
    assert_eq!(sort_order.fields[0].direction, SortDirection::Ascending);
    assert_eq!(sort_order.fields[0].null_order, NullOrder::First);
    assert_eq!(sort_order.fields[1].source_id, 1);
    assert_eq!(sort_order.fields[1].direction, SortDirection::Descending);
    assert_eq!(sort_order.fields[1].null_order, NullOrder::Last);
    // The second update makes the just-added order the default.
    assert_eq!(updates[1], TableUpdate::SetDefaultSortOrder {
        sort_order_id: -1
    });

    // The action asserts the base it read: the schema it resolved names
    // against, and the sort order it is replacing.
    let requirements = catalog.last_requirements();
    assert!(requirements.contains(&TableRequirement::CurrentSchemaIdMatch {
        current_schema_id: table.metadata().current_schema_id()
    }));
    assert!(
        requirements.contains(&TableRequirement::DefaultSortOrderIdMatch {
            default_sort_order_id: table.metadata().default_sort_order().order_id
        })
    );
}

#[tokio::test]
async fn g09_sort_order__becomes_the_default_order() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    assert!(table.metadata().default_sort_order().is_unsorted());
    assert_eq!(table.metadata().default_sort_order().order_id, 0);

    let tx = Transaction::new(&table);
    let tx = tx
        .replace_sort_order()
        .asc("name", NullOrder::First)
        .apply(tx)
        .unwrap();
    let committed = tx.commit(&catalog).await.unwrap();

    let order = committed.metadata().default_sort_order();
    // "Sorting": order id 0 is reserved for the unsorted order, so a real
    // order gets a fresh, non-zero id.
    assert!(!order.is_unsorted());
    assert_ne!(order.order_id, 0);
    assert_eq!(order.fields.len(), 1);
    assert_eq!(order.fields[0].source_id, 2);
    assert_eq!(
        committed
            .metadata()
            .sort_order_by_id(order.order_id)
            .unwrap()
            .fields,
        order.fields
    );
    // The unsorted order stays reachable.
    assert!(committed.metadata().sort_order_by_id(0).is_some());
}

#[tokio::test]
async fn g09_sort_order__unknown_column_is_rejected() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let tx = Transaction::new(&table);
    let tx = tx
        .replace_sort_order()
        .asc("not_a_column", NullOrder::First)
        .apply(tx)
        .unwrap();
    assert!(
        tx.commit(&catalog).await.is_err(),
        "a sort field naming a column absent from the current schema must be rejected"
    );
}

// ---------------------------------------------------------------------------
// g10_statistics — spec.md "Table Statistics".
// ---------------------------------------------------------------------------

fn statistics_file(snapshot_id: i64, path: &str) -> StatisticsFile {
    StatisticsFile {
        snapshot_id,
        statistics_path: path.to_string(),
        file_size_in_bytes: 413,
        file_footer_size_in_bytes: 42,
        key_metadata: None,
        blob_metadata: vec![BlobMetadata {
            r#type: "apache-datasketches-theta-v1".to_string(),
            snapshot_id,
            sequence_number: 1,
            fields: vec![1],
            properties: HashMap::new(),
        }],
    }
}

#[tokio::test]
async fn g10_statistics__set_records_the_file_for_its_snapshot() {
    let (_dir, catalog) = temp_catalog();
    let table = append_once(&catalog, HashMap::new()).await;
    let snapshot_id = table.metadata().current_snapshot().unwrap().snapshot_id();
    let stats = statistics_file(snapshot_id, "s3://a/b/stats.puffin");

    let tx = Transaction::new(&table);
    let tx = tx
        .update_statistics()
        .set_statistics(stats.clone())
        .apply(tx)
        .unwrap();
    let committed = tx.commit(&catalog).await.unwrap();

    // "Table Statistics": `statistics` is a list of statistics files, each
    // keyed by the snapshot it describes.
    assert_eq!(
        committed.metadata().statistics_for_snapshot(snapshot_id),
        Some(&stats)
    );
    assert_eq!(committed.metadata().statistics_iter().count(), 1);
    assert_eq!(catalog.last_updates(), vec![TableUpdate::SetStatistics {
        statistics: stats
    }]);
    assert!(catalog.last_requirements().is_empty());
}

#[tokio::test]
async fn g10_statistics__remove_drops_it() {
    let (_dir, catalog) = temp_catalog();
    let table = append_once(&catalog, HashMap::new()).await;
    let snapshot_id = table.metadata().current_snapshot().unwrap().snapshot_id();

    let tx = Transaction::new(&table);
    let tx = tx
        .update_statistics()
        .set_statistics(statistics_file(snapshot_id, "s3://a/b/stats.puffin"))
        .apply(tx)
        .unwrap();
    let with_stats = tx.commit(&catalog).await.unwrap();
    assert!(
        with_stats
            .metadata()
            .statistics_for_snapshot(snapshot_id)
            .is_some()
    );

    let tx = Transaction::new(&with_stats);
    let tx = tx
        .update_statistics()
        .remove_statistics(snapshot_id)
        .apply(tx)
        .unwrap();
    let without = tx.commit(&catalog).await.unwrap();

    assert!(
        without
            .metadata()
            .statistics_for_snapshot(snapshot_id)
            .is_none()
    );
    assert_eq!(without.metadata().statistics_iter().count(), 0);
    assert_eq!(catalog.last_updates(), vec![TableUpdate::RemoveStatistics {
        snapshot_id
    }]);
}

#[tokio::test]
async fn g10_statistics__file_serializes_with_documented_field_names() {
    // "Table Statistics": the statistics-file fields are `snapshot-id`,
    // `statistics-path`, `file-size-in-bytes`, `file-footer-size-in-bytes`
    // and `blob-metadata`.
    let stats = statistics_file(42, "s3://a/b/stats.puffin");
    let json = serde_json::to_value(&stats).unwrap();
    assert_eq!(json["snapshot-id"], 42);
    assert_eq!(json["statistics-path"], "s3://a/b/stats.puffin");
    assert_eq!(json["file-size-in-bytes"], 413);
    assert_eq!(json["file-footer-size-in-bytes"], 42);
    assert_eq!(json["blob-metadata"][0]["type"], "apache-datasketches-theta-v1");
    assert_eq!(json["blob-metadata"][0]["sequence-number"], 1);
    assert_eq!(json["blob-metadata"][0]["fields"], serde_json::json!([1]));

    // `key-metadata` is omitted when there is none, and a blob's `properties`
    // are omitted when empty.
    assert!(
        json.get("key-metadata").is_none(),
        "key-metadata must be omitted when None, got {json}"
    );
    assert!(
        json["blob-metadata"][0].get("properties").is_none(),
        "an empty blob properties map must be omitted, got {json}"
    );

    let back: StatisticsFile = serde_json::from_value(json).unwrap();
    assert_eq!(back, stats);
}

// ---------------------------------------------------------------------------
// g11_conflict — spec.md "Optimistic Concurrency" and "Commit Conflict
// Resolution and Retry".
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g11_conflict__append_asserts_the_base_it_read() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let spec_id = table.metadata().default_partition_spec_id();
    let location = table.metadata().location().to_string();

    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/1.parquet"),
            spec_id,
            1,
            50,
        )])
        .apply(tx)
        .unwrap();
    tx.commit(&catalog).await.unwrap();

    // "Optimistic Concurrency": the writer commits assuming the version it
    // read is still current, so it states the identity of that version.
    let requirements = catalog.last_requirements();
    assert!(requirements.contains(&TableRequirement::UuidMatch {
        uuid: table.metadata().uuid()
    }));
    assert!(
        requirements.contains(&TableRequirement::RefSnapshotIdMatch {
            r#ref: MAIN_BRANCH.to_string(),
            snapshot_id: None
        }),
        "an append on a table with no snapshot must require `main` to be unset, got {requirements:?}"
    );
}

#[tokio::test]
async fn g11_conflict__stale_requirement_no_longer_holds() {
    let (_dir, catalog) = temp_catalog();
    let table = append_once(&catalog, HashMap::new()).await;
    let base_metadata = table.metadata().clone();
    let base_snapshot_id = base_metadata.current_snapshot().unwrap().snapshot_id();

    // A writer that read `table` would carry this requirement.
    let requirement = TableRequirement::RefSnapshotIdMatch {
        r#ref: MAIN_BRANCH.to_string(),
        snapshot_id: Some(base_snapshot_id),
    };
    assert!(requirement.check(Some(&base_metadata)).is_ok());

    // Another writer commits first.
    let spec_id = table.metadata().default_partition_spec_id();
    let location = table.metadata().location().to_string();
    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/other.parquet"),
            spec_id,
            1,
            50,
        )])
        .apply(tx)
        .unwrap();
    let advanced = tx.commit(&catalog).await.unwrap();
    assert_ne!(
        advanced.metadata().current_snapshot().unwrap().snapshot_id(),
        base_snapshot_id
    );

    // "Commit Conflict Resolution and Retry": the stale requirement must now
    // fail, so the second writer cannot swap over the first one's commit.
    assert!(requirement.check(Some(advanced.metadata())).is_err());
    assert!(
        TableRequirement::UuidMatch {
            uuid: base_metadata.uuid()
        }
        .check(Some(advanced.metadata()))
        .is_ok(),
        "the table identity is unchanged by a concurrent append"
    );
}

#[tokio::test]
async fn g11_conflict__commit_over_a_stale_read_is_rejected() {
    let (_dir, catalog) = temp_catalog();
    let table = append_once(&catalog, HashMap::new()).await;
    let spec_id = table.metadata().default_partition_spec_id();
    let location = table.metadata().location().to_string();

    // Writer A commits.
    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/a.parquet"),
            spec_id,
            1,
            50,
        )])
        .apply(tx)
        .unwrap();
    let advanced = tx.commit(&catalog).await.unwrap();

    // Writer B read the table before A committed: the catalog keeps serving
    // that stale version, so B builds its commit on the superseded base.
    catalog.serve_stale(table.clone());
    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/b.parquet"),
            spec_id,
            1,
            50,
        )])
        .apply(tx)
        .unwrap();
    assert!(
        tx.commit(&catalog).await.is_err(),
        "a commit whose base is no longer current must be rejected"
    );

    // A's commit is still the one in the catalog.
    catalog.stale.lock().unwrap().take();
    let reloaded = catalog.load_table(table.identifier()).await.unwrap();
    assert_eq!(
        reloaded.metadata().current_snapshot().unwrap().snapshot_id(),
        advanced.metadata().current_snapshot().unwrap().snapshot_id()
    );
}

#[tokio::test]
async fn g11_conflict__a_changed_base_is_refreshed_before_the_actions_run() {
    let (_dir, catalog) = temp_catalog();
    let table = append_once(&catalog, HashMap::new()).await;
    let spec_id = table.metadata().default_partition_spec_id();
    let location = table.metadata().location().to_string();
    let base_snapshot_id = table.metadata().current_snapshot().unwrap().snapshot_id();

    // Writer A commits, so the version writer B is holding is superseded.
    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/a.parquet"),
            spec_id,
            1,
            50,
        )])
        .apply(tx)
        .unwrap();
    let advanced = tx.commit(&catalog).await.unwrap();
    let advanced_id = advanced.metadata().current_snapshot().unwrap().snapshot_id();
    assert_ne!(advanced_id, base_snapshot_id);

    // Writer B built its transaction from the superseded base, and this time
    // the catalog serves the current version. Section 4.6: `commit` re-loads
    // the table first, sees that the metadata changed, re-bases onto the
    // refreshed table and derives its updates and requirements from THAT — so
    // the commit succeeds and chains onto A's snapshot instead of conflicting
    // with it.
    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/b.parquet"),
            spec_id,
            2,
            60,
        )])
        .apply(tx)
        .unwrap();
    let committed = tx.commit(&catalog).await.unwrap();

    let snapshot = committed.metadata().current_snapshot().unwrap();
    assert_eq!(
        snapshot.parent_snapshot_id(),
        Some(advanced_id),
        "a re-based commit must chain onto the snapshot that was current, not onto the stale base"
    );
    assert_eq!(snapshot.sequence_number(), 3);
    assert_eq!(committed.metadata().history().len(), 3);
    // The requirement it stated names the REFRESHED base, which is the whole
    // point of the re-base: had it kept the base it read, the requirement
    // would have named `base_snapshot_id` and the commit would have failed.
    let requirements = catalog.last_requirements();
    assert!(
        requirements.contains(&TableRequirement::RefSnapshotIdMatch {
            r#ref: MAIN_BRANCH.to_string(),
            snapshot_id: Some(advanced_id)
        }),
        "the re-based commit must require the refreshed snapshot, got {requirements:?}"
    );
}

#[tokio::test]
async fn g11_conflict__apply_rejects_a_commit_whose_requirement_failed() {
    let (_dir, catalog) = temp_catalog();
    let table = append_once(&catalog, HashMap::new()).await;
    let spec_id = table.metadata().default_partition_spec_id();
    let location = table.metadata().location().to_string();

    // Writer A commits, advancing `main` past the base `table` holds.
    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/a.parquet"),
            spec_id,
            1,
            50,
        )])
        .apply(tx)
        .unwrap();
    let advanced = tx.commit(&catalog).await.unwrap();

    // The catalog keeps serving the superseded version, so the transaction
    // does NOT re-base and its requirements name the base it read. Hand the
    // resulting commit to `TableCommit::apply` against the advanced table:
    // section 4.3 says a failing requirement makes `apply` an `Err` and no
    // update is applied.
    catalog.serve_stale(table.clone());
    catalog.apply_against(advanced.clone());
    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/b.parquet"),
            spec_id,
            1,
            50,
        )])
        .apply(tx)
        .unwrap();
    assert!(
        tx.commit(&catalog).await.is_err(),
        "`TableCommit::apply` must reject a commit whose requirement no longer holds"
    );

    // The same path against the base the transaction actually read applies:
    // `apply` returns the new metadata carrying the OLD metadata location,
    // because persisting the document is the catalog's job.
    catalog.apply_against(table.clone());
    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![a_data_file(
            &format!("{location}/data/c.parquet"),
            spec_id,
            3,
            70,
        )])
        .apply(tx)
        .unwrap();
    let applied = tx.commit(&catalog).await.unwrap();
    let snapshot = applied
        .metadata()
        .current_snapshot()
        .expect("apply must have added the snapshot");
    assert_eq!(
        snapshot.parent_snapshot_id(),
        table.metadata().current_snapshot().map(|s| s.snapshot_id())
    );
    assert_eq!(applied.metadata_location(), table.metadata_location());
    assert_eq!(applied.identifier(), table.identifier());
}

#[tokio::test]
async fn g11_conflict__retry_is_configured_by_table_properties() {
    // "Commit Conflict Resolution and Retry": a writer retries a conflicting
    // commit, bounded by the table's retry configuration.
    assert_eq!(PROPERTY_COMMIT_NUM_RETRIES, "commit.retry.num-retries");
    assert_eq!(PROPERTY_COMMIT_MIN_RETRY_WAIT_MS, "commit.retry.min-wait-ms");
    assert_eq!(PROPERTY_COMMIT_MAX_RETRY_WAIT_MS, "commit.retry.max-wait-ms");
    assert_eq!(
        PROPERTY_COMMIT_TOTAL_RETRY_TIME_MS,
        "commit.retry.total-timeout-ms"
    );
    assert_eq!(PROPERTY_COMMIT_NUM_RETRIES_DEFAULT, 4);
    assert_eq!(PROPERTY_COMMIT_MIN_RETRY_WAIT_MS_DEFAULT, 100);
    assert_eq!(PROPERTY_COMMIT_MAX_RETRY_WAIT_MS_DEFAULT, 60_000);
    assert_eq!(PROPERTY_COMMIT_TOTAL_RETRY_TIME_MS_DEFAULT, 1_800_000);

    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let tx = Transaction::new(&table);
    let tx = tx
        .update_table_properties()
        .set(PROPERTY_COMMIT_NUM_RETRIES.to_string(), "2".to_string())
        .apply(tx)
        .unwrap();
    let configured = tx.commit(&catalog).await.unwrap();
    assert_eq!(
        configured
            .metadata()
            .properties()
            .get(PROPERTY_COMMIT_NUM_RETRIES)
            .map(String::as_str),
        Some("2")
    );

    // An unparseable retry setting must be reported, not ignored.
    let tx = Transaction::new(&configured);
    let tx = tx
        .update_table_properties()
        .set(
            PROPERTY_COMMIT_NUM_RETRIES.to_string(),
            "not-a-number".to_string(),
        )
        .apply(tx)
        .unwrap();
    let broken = tx.commit(&catalog).await.unwrap();
    assert_eq!(
        broken
            .metadata()
            .properties()
            .get(PROPERTY_COMMIT_NUM_RETRIES)
            .map(String::as_str),
        Some("not-a-number")
    );

    let tx = Transaction::new(&broken);
    let tx = tx
        .update_table_properties()
        .set("anything".to_string(), "1".to_string())
        .apply(tx)
        .unwrap();
    assert!(tx.commit(&catalog).await.is_err());
}

#[tokio::test]
async fn g11_conflict__a_retryable_failure_is_retried() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;

    // The retry bounds come from the table's own properties, so shorten them
    // first: this commit is the one that writes them.
    let tx = Transaction::new(&table);
    let tx = tx
        .update_table_properties()
        .set(PROPERTY_COMMIT_MIN_RETRY_WAIT_MS.to_string(), "1".to_string())
        .set(PROPERTY_COMMIT_MAX_RETRY_WAIT_MS.to_string(), "5".to_string())
        .set(PROPERTY_COMMIT_NUM_RETRIES.to_string(), "3".to_string())
        .apply(tx)
        .unwrap();
    let configured = tx.commit(&catalog).await.unwrap();

    // "Commit Conflict Resolution and Retry": a failure that reports itself as
    // retryable is retried, within the configured bound, and the commit still
    // succeeds.
    catalog.inject_failures(2, true);
    let tx = Transaction::new(&configured);
    let tx = tx
        .update_table_properties()
        .set("survived".to_string(), "yes".to_string())
        .apply(tx)
        .unwrap();
    let committed = tx.commit(&catalog).await.unwrap();
    assert_eq!(
        committed
            .metadata()
            .properties()
            .get("survived")
            .map(String::as_str),
        Some("yes")
    );
    assert_eq!(
        catalog.attempts(),
        3,
        "two retryable failures must be followed by a third, successful attempt"
    );

    // An error that is not retryable fails the commit immediately.
    catalog.inject_failures(1, false);
    let tx = Transaction::new(&committed);
    let tx = tx
        .update_table_properties()
        .set("doomed".to_string(), "yes".to_string())
        .apply(tx)
        .unwrap();
    assert!(tx.commit(&catalog).await.is_err());
    assert_eq!(
        catalog.attempts(),
        1,
        "a commit must not retry an error that is not retryable"
    );

    // The flag itself: `retryable()` is false unless `with_retryable` set it.
    let plain = Error::new(ErrorKind::CatalogCommitConflicts, "conflict");
    assert!(!plain.retryable());
    assert!(plain.with_retryable(true).retryable());
}

// ---------------------------------------------------------------------------
// g12_action_reject — documented rejections of the transaction-action model.
// TEST_INTERFACE.md action contract; spec.md "Writer requirements".
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g12_action_reject__append_without_data_files() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let tx = Transaction::new(&table);
    let tx = tx.fast_append().add_data_files(vec![]).apply(tx).unwrap();
    assert!(
        tx.commit(&catalog).await.is_err(),
        "a fast append with no data files must be rejected"
    );
}

#[tokio::test]
async fn g12_action_reject__append_of_a_delete_file() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let spec_id = table.metadata().default_partition_spec_id();
    // "Manifests": a data manifest may only track data files, so an append
    // must refuse a delete file.
    let delete_file = DataFileBuilder::default()
        .content(DataContentType::PositionDeletes)
        .file_path(format!("{}/deletes.parquet", table.metadata().location()))
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .partition_spec_id(spec_id)
        .record_count(1)
        .file_size_in_bytes(50)
        .build()
        .unwrap();
    let tx = Transaction::new(&table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![delete_file])
        .apply(tx)
        .unwrap();
    assert!(tx.commit(&catalog).await.is_err());
}

#[tokio::test]
async fn g12_action_reject__append_with_foreign_partition_spec_id() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    // "Partitioning": a data file's partition spec must be the table's
    // current default spec for a fast append.
    let file = a_data_file(
        &format!("{}/data/1.parquet", table.metadata().location()),
        table.metadata().default_partition_spec_id() + 7,
        1,
        50,
    );
    let tx = Transaction::new(&table);
    let tx = tx.fast_append().add_data_files(vec![file]).apply(tx).unwrap();
    assert!(tx.commit(&catalog).await.is_err());
}

#[tokio::test]
async fn g12_action_reject__property_set_and_removed_together() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let tx = Transaction::new(&table);
    let tx = tx
        .update_table_properties()
        .set("k".to_string(), "v".to_string())
        .remove("k".to_string())
        .apply(tx)
        .unwrap();
    assert!(
        tx.commit(&catalog).await.is_err(),
        "a key may not be both set and removed by one action"
    );
}

#[tokio::test]
async fn g12_action_reject__incomplete_location_and_version_actions() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;

    let tx = Transaction::new(&table);
    let tx = tx.update_location().apply(tx).unwrap();
    assert!(
        tx.commit(&catalog).await.is_err(),
        "a location update with no location must be rejected"
    );

    let tx = Transaction::new(&table);
    let tx = tx.upgrade_table_version().apply(tx).unwrap();
    assert!(
        tx.commit(&catalog).await.is_err(),
        "a format-version upgrade with no target version must be rejected"
    );
}

#[test]
fn g12_action_reject__action_commit_carries_updates_and_requirements() {
    // `ActionCommit` is what an action produces when the transaction commits;
    // taking either vec out leaves it empty, as for `TableCommit`.
    let updates = vec![TableUpdate::SetLocation {
        location: "/tmp/moved".to_string(),
    }];
    let requirements = vec![TableRequirement::UuidMatch {
        uuid: uuid::Uuid::from_u128(9),
    }];
    let mut commit = ActionCommit::new(updates.clone(), requirements.clone());
    assert_eq!(commit.take_updates(), updates);
    assert_eq!(commit.take_requirements(), requirements);
    assert!(commit.take_updates().is_empty());
    assert!(commit.take_requirements().is_empty());
}

// ---------------------------------------------------------------------------
// g13_bounds — spec.md "Manifest Lists" fields 510/511 (`lower_bound` and
// `upper_bound` are `bytes`) and Appendix D "Bound serialization".
// ---------------------------------------------------------------------------

struct PartitionedFixture {
    _dir: tempfile::TempDir,
    io: FileIO,
    root: String,
    schema: SchemaRef,
    spec: PartitionSpec,
}

fn partitioned_fixture(source: &str) -> PartitionedFixture {
    let dir = tempfile::TempDir::new().unwrap();
    let root = dir.path().to_str().unwrap().to_string();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let schema: SchemaRef = Arc::new(
        Schema::builder()
            .with_schema_id(0)
            .with_fields(vec![
                NestedField::optional(1, "num", Type::Primitive(PrimitiveType::Long)).into(),
                NestedField::optional(2, "label", Type::Primitive(PrimitiveType::String)).into(),
            ])
            .build()
            .unwrap(),
    );
    let spec = PartitionSpec::builder(schema.clone())
        .with_spec_id(0)
        .add_partition_field(source, format!("{source}_part"), Transform::Identity)
        .unwrap()
        .build()
        .unwrap();
    PartitionedFixture {
        _dir: dir,
        io,
        root,
        schema,
        spec,
    }
}

fn partitioned_data_file(path: &str, value: Option<Literal>) -> DataFile {
    DataFileBuilder::default()
        .content(DataContentType::Data)
        .file_path(path.to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::from_iter([value]))
        .partition_spec_id(0)
        .record_count(1)
        .file_size_in_bytes(100)
        .build()
        .unwrap()
}

/// Writes one manifest holding `files`, then writes and reads back a manifest
/// list containing it. Returns the round-tripped manifest-list entry.
async fn round_trip_manifest(
    fixture: &PartitionedFixture,
    tag: &str,
    files: Vec<DataFile>,
) -> ManifestFile {
    let manifest_path = format!("{}/{tag}-m0.avro", fixture.root);
    let mut writer = ManifestWriterBuilder::new(
        fixture.io.new_output(&manifest_path).unwrap(),
        Some(10),
        None,
        fixture.schema.clone(),
        fixture.spec.clone(),
    )
    .build_v2_data();
    for file in files {
        writer.add_file(file, 1).unwrap();
    }
    let manifest_file = writer.write_manifest_file().await.unwrap();

    let list_path = format!("{}/{tag}-list.avro", fixture.root);
    let mut list_writer = ManifestListWriter::v2(
        fixture.io.new_output(&list_path).unwrap(),
        10,
        Some(9),
        1,
    );
    list_writer
        .add_manifests(vec![manifest_file.clone()].into_iter())
        .unwrap();
    list_writer.close().await.unwrap();

    let bytes = std::fs::read(&list_path).unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2).unwrap();
    assert_eq!(list.entries().len(), 1);
    let read_back = list.entries()[0].clone();
    // The writer's in-memory summary and the summary read back from Avro agree.
    assert_eq!(read_back.partitions, manifest_file.partitions);
    read_back
}

#[tokio::test]
async fn g13_bounds__long_bounds_are_binary_single_values() {
    let fixture = partitioned_fixture("num");
    let entry = round_trip_manifest(&fixture, "long", vec![
        partitioned_data_file(&format!("{}/1.parquet", fixture.root), Some(Literal::long(100))),
        partitioned_data_file(&format!("{}/2.parquet", fixture.root), Some(Literal::long(300))),
        partitioned_data_file(&format!("{}/3.parquet", fixture.root), Some(Literal::long(200))),
    ])
    .await;

    let partitions = entry.partitions.expect("partition field summaries");
    assert_eq!(partitions.len(), 1);
    let summary = &partitions[0];
    assert!(!summary.contains_null);
    assert_eq!(summary.contains_nan, Some(false));

    // Appendix D "Bound serialization": bounds use the binary single-value
    // serialization, which for `long` is 8 bytes little-endian.
    let lower = summary.lower_bound.as_ref().expect("lower bound");
    let upper = summary.upper_bound.as_ref().expect("upper bound");
    assert_eq!(&lower[..], &100i64.to_le_bytes()[..]);
    assert_eq!(&upper[..], &300i64.to_le_bytes()[..]);
    assert_eq!(lower, &Datum::long(100).to_bytes().unwrap());
    assert_eq!(upper, &Datum::long(300).to_bytes().unwrap());
    // The bytes decode back to the bounding values.
    assert_eq!(
        Datum::try_from_bytes(lower, PrimitiveType::Long).unwrap(),
        Datum::long(100)
    );
    assert_eq!(
        Datum::try_from_bytes(upper, PrimitiveType::Long).unwrap(),
        Datum::long(300)
    );
}

#[tokio::test]
async fn g13_bounds__string_bounds_are_utf8_without_length_prefix() {
    let fixture = partitioned_fixture("label");
    let entry = round_trip_manifest(&fixture, "string", vec![
        partitioned_data_file(
            &format!("{}/1.parquet", fixture.root),
            Some(Literal::string("mango")),
        ),
        partitioned_data_file(
            &format!("{}/2.parquet", fixture.root),
            Some(Literal::string("apple")),
        ),
    ])
    .await;

    let partitions = entry.partitions.unwrap();
    let summary = &partitions[0];
    // Appendix D: `string` is UTF-8 bytes with no length prefix.
    assert_eq!(&summary.lower_bound.as_ref().unwrap()[..], b"apple");
    assert_eq!(&summary.upper_bound.as_ref().unwrap()[..], b"mango");
    assert_eq!(
        summary.lower_bound.as_ref().unwrap(),
        &Datum::string("apple").to_bytes().unwrap()
    );
}

#[tokio::test]
async fn g13_bounds__null_partition_values_are_excluded_from_bounds() {
    let fixture = partitioned_fixture("num");
    let entry = round_trip_manifest(&fixture, "nulls", vec![
        partitioned_data_file(&format!("{}/1.parquet", fixture.root), None),
        partitioned_data_file(&format!("{}/2.parquet", fixture.root), Some(Literal::long(7))),
    ])
    .await;

    let partitions = entry.partitions.unwrap();
    let summary = &partitions[0];
    // "Manifest Lists" field 509/510/511: `contains_null` records the null,
    // and the bounds cover only the non-null values.
    assert!(summary.contains_null);
    assert_eq!(
        &summary.lower_bound.as_ref().unwrap()[..],
        &7i64.to_le_bytes()[..]
    );
    assert_eq!(
        &summary.upper_bound.as_ref().unwrap()[..],
        &7i64.to_le_bytes()[..]
    );

    // All values null: no bounds at all.
    let entry = round_trip_manifest(&fixture, "allnull", vec![partitioned_data_file(
        &format!("{}/3.parquet", fixture.root),
        None,
    )])
    .await;
    let partitions = entry.partitions.unwrap();
    assert!(partitions[0].contains_null);
    assert!(partitions[0].lower_bound.is_none());
    assert!(partitions[0].upper_bound.is_none());
}

#[tokio::test]
async fn g13_bounds__field_summary_json_uses_documented_names() {
    let summary = FieldSummary {
        contains_null: true,
        contains_nan: Some(false),
        lower_bound: Some(Datum::int(1).to_bytes().unwrap()),
        upper_bound: Some(Datum::int(2).to_bytes().unwrap()),
    };
    let json = serde_json::to_value(&summary).unwrap();
    assert_eq!(json["contains_null"], true);
    assert_eq!(json["contains_nan"], false);
    // The bounds are byte buffers, not decoded values.
    assert_eq!(json["lower_bound"], serde_json::json!([1, 0, 0, 0]));
    assert_eq!(json["upper_bound"], serde_json::json!([2, 0, 0, 0]));
    let back: FieldSummary = serde_json::from_value(json).unwrap();
    assert_eq!(back, summary);
}

// ---------------------------------------------------------------------------
// g14_manifest_list — spec.md "Manifest Lists" (optional columns) and
// "Manifests".
// ---------------------------------------------------------------------------

fn unpartitioned_writer_inputs() -> (tempfile::TempDir, FileIO, String, SchemaRef, PartitionSpec) {
    let dir = tempfile::TempDir::new().unwrap();
    let root = dir.path().to_str().unwrap().to_string();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let schema: SchemaRef = Arc::new(
        Schema::builder()
            .with_schema_id(3)
            .with_fields(vec![
                NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            ])
            .build()
            .unwrap(),
    );
    let spec = PartitionSpec::builder(schema.clone())
        .with_spec_id(0)
        .build()
        .unwrap();
    (dir, io, root, schema, spec)
}

#[tokio::test]
async fn g14_manifest_list__parses_without_a_partition_type_provider() {
    let (_dir, io, root, schema, spec) = unpartitioned_writer_inputs();
    let manifest_path = format!("{root}/m0.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&manifest_path).unwrap(),
        Some(42),
        None,
        schema,
        spec,
    )
    .build_v2_data();
    writer
        .add_file(a_data_file(&format!("{root}/1.parquet"), 0, 3, 512), 5)
        .unwrap();
    let manifest_file = writer.write_manifest_file().await.unwrap();

    let list_path = format!("{root}/list.avro");
    let mut list_writer =
        ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 42, Some(41), 5);
    list_writer
        .add_manifests(vec![manifest_file].into_iter())
        .unwrap();
    list_writer.close().await.unwrap();

    let bytes = std::fs::read(&list_path).unwrap();
    assert_eq!(&bytes[..4], b"Obj\x01");
    // The whole manifest-list record is self-describing: reading it needs
    // only the bytes and the table format version.
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2).unwrap();
    let entry = &list.entries()[0];
    assert_eq!(entry.manifest_path, manifest_path);
    assert_eq!(entry.added_snapshot_id, 42);
    assert_eq!(entry.content, ManifestContentType::Data);
    assert_eq!(entry.partition_spec_id, 0);
    assert_eq!(entry.sequence_number, 5);
    assert_eq!(entry.min_sequence_number, 5);
    assert_eq!(entry.added_files_count, Some(1));
    assert_eq!(entry.added_rows_count, Some(3));
    assert!(entry.has_added_files());
    assert!(!entry.has_existing_files());
    assert!(!entry.has_deleted_files());
}

#[tokio::test]
async fn g14_manifest_list__partitions_and_key_metadata_are_optional() {
    let (_dir, io, root, schema, spec) = unpartitioned_writer_inputs();
    // No key metadata: "Manifest Lists" field 519 is optional.
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(format!("{root}/plain.avro")).unwrap(),
        Some(1),
        None,
        schema.clone(),
        spec.clone(),
    )
    .build_v2_data();
    writer
        .add_file(a_data_file(&format!("{root}/1.parquet"), 0, 1, 10), 1)
        .unwrap();
    let plain = writer.write_manifest_file().await.unwrap();
    assert_eq!(plain.key_metadata, None);
    // An unpartitioned spec has no partition fields, so the summary list is
    // present but empty.
    assert_eq!(plain.partitions, Some(vec![]));

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(format!("{root}/keyed.avro")).unwrap(),
        Some(1),
        Some(vec![1u8, 2, 3]),
        schema,
        spec,
    )
    .build_v2_data();
    writer
        .add_file(a_data_file(&format!("{root}/2.parquet"), 0, 1, 10), 1)
        .unwrap();
    let keyed = writer.write_manifest_file().await.unwrap();
    assert_eq!(keyed.key_metadata, Some(vec![1u8, 2, 3]));

    let list_path = format!("{root}/list.avro");
    let mut list_writer = ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 1, None, 1);
    list_writer
        .add_manifests(vec![plain, keyed].into_iter())
        .unwrap();
    list_writer.close().await.unwrap();
    let bytes = std::fs::read(&list_path).unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2).unwrap();
    assert_eq!(list.entries().len(), 2);
    assert!(list.entries().iter().any(|e| e.key_metadata.is_none()));
    assert!(
        list.entries()
            .iter()
            .any(|e| e.key_metadata.as_deref() == Some(&[1u8, 2, 3][..]))
    );
}

#[tokio::test]
async fn g14_manifest_list__v1_round_trip() {
    let (_dir, io, root, schema, spec) = unpartitioned_writer_inputs();
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(format!("{root}/v1-m0.avro")).unwrap(),
        Some(7),
        None,
        schema,
        spec,
    )
    .build_v1();
    writer
        .add_file(a_data_file(&format!("{root}/1.parquet"), 0, 2, 64), 0)
        .unwrap();
    let manifest_file = writer.write_manifest_file().await.unwrap();

    let list_path = format!("{root}/v1-list.avro");
    let mut list_writer = ManifestListWriter::v1(io.new_output(&list_path).unwrap(), 7, Some(6));
    list_writer
        .add_manifests(vec![manifest_file].into_iter())
        .unwrap();
    list_writer.close().await.unwrap();

    let bytes = std::fs::read(&list_path).unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V1).unwrap();
    assert_eq!(list.entries().len(), 1);
    let entry = &list.entries()[0];
    // "Sequence Numbers": v1 manifest lists have no sequence numbers; readers
    // use 0.
    assert_eq!(entry.sequence_number, 0);
    assert_eq!(entry.min_sequence_number, 0);
    assert_eq!(entry.content, ManifestContentType::Data);
    assert_eq!(entry.added_snapshot_id, 7);
}

#[tokio::test]
async fn g14_manifest_list__data_and_delete_manifests_share_one_list() {
    let (_dir, io, root, schema, spec) = unpartitioned_writer_inputs();
    let mut data_writer = ManifestWriterBuilder::new(
        io.new_output(format!("{root}/data-m0.avro")).unwrap(),
        Some(20),
        None,
        schema.clone(),
        spec.clone(),
    )
    .build_v2_data();
    data_writer
        .add_file(a_data_file(&format!("{root}/1.parquet"), 0, 4, 128), 3)
        .unwrap();
    let data_manifest = data_writer.write_manifest_file().await.unwrap();

    let mut delete_writer = ManifestWriterBuilder::new(
        io.new_output(format!("{root}/deletes-m0.avro")).unwrap(),
        Some(20),
        None,
        schema,
        spec,
    )
    .build_v2_deletes();
    delete_writer
        .add_file(
            position_delete_file(
                &format!("{root}/pos.parquet"),
                &format!("{root}/1.parquet"),
                2,
            ),
            3,
        )
        .unwrap();
    let delete_manifest = delete_writer.write_manifest_file().await.unwrap();

    let list_path = format!("{root}/mixed-list.avro");
    let mut list_writer =
        ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 20, Some(19), 3);
    list_writer
        .add_manifests(vec![data_manifest, delete_manifest].into_iter())
        .unwrap();
    list_writer.close().await.unwrap();

    let bytes = std::fs::read(&list_path).unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2).unwrap();
    assert_eq!(list.entries().len(), 2);
    // "Manifest Lists": a data manifest and a delete manifest may sit in the
    // same list, each keeping its own `content`. `consume_entries` hands the
    // entries over by value.
    let contents: Vec<ManifestContentType> = list
        .consume_entries()
        .into_iter()
        .map(|entry| entry.content)
        .collect();
    assert_eq!(contents.len(), 2);
    assert!(contents.contains(&ManifestContentType::Data));
    assert!(contents.contains(&ManifestContentType::Deletes));
}

// ---------------------------------------------------------------------------
// g15_delete_manifest — spec.md "Manifests" (manifest `content`) and
// "Position Delete Files".
// ---------------------------------------------------------------------------

fn position_delete_file(path: &str, referenced: &str, records: u64) -> DataFile {
    DataFileBuilder::default()
        .content(DataContentType::PositionDeletes)
        .file_path(path.to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .partition_spec_id(0)
        .record_count(records)
        .file_size_in_bytes(80)
        .referenced_data_file(Some(referenced.to_string()))
        .build()
        .unwrap()
}

#[tokio::test]
async fn g15_delete_manifest__delete_manifest_is_tagged_as_deletes() {
    let (_dir, io, root, schema, spec) = unpartitioned_writer_inputs();
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(format!("{root}/deletes.avro")).unwrap(),
        Some(100),
        None,
        schema,
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(
            position_delete_file(
                &format!("{root}/pos-deletes.parquet"),
                &format!("{root}/1.parquet"),
                3,
            ),
            2,
        )
        .unwrap();
    let manifest_file = writer.write_manifest_file().await.unwrap();

    // "Manifests": `content` distinguishes data manifests from delete
    // manifests.
    assert_eq!(manifest_file.content, ManifestContentType::Deletes);
    assert_eq!(manifest_file.added_files_count, Some(1));

    let list_path = format!("{root}/list.avro");
    let mut list_writer = ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 100, None, 2);
    list_writer
        .add_manifests(vec![manifest_file].into_iter())
        .unwrap();
    list_writer.close().await.unwrap();
    let bytes = std::fs::read(&list_path).unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2).unwrap();
    assert_eq!(list.entries()[0].content, ManifestContentType::Deletes);
    assert_eq!(list.entries()[0].sequence_number, 2);
}

#[tokio::test]
async fn g15_delete_manifest__entry_round_trips_with_its_metadata() {
    let (_dir, io, root, schema, spec) = unpartitioned_writer_inputs();
    let manifest_path = format!("{root}/deletes.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&manifest_path).unwrap(),
        Some(100),
        None,
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(
            position_delete_file(
                &format!("{root}/pos-deletes.parquet"),
                &format!("{root}/1.parquet"),
                3,
            ),
            2,
        )
        .unwrap();
    writer.write_manifest_file().await.unwrap();

    let bytes = std::fs::read(&manifest_path).unwrap();
    assert_eq!(&bytes[..4], b"Obj\x01", "manifest must be an Avro file");
    let manifest = Manifest::parse_avro(&bytes).unwrap();

    // "Manifests": the manifest file records its content type, format
    // version, schema and partition spec.
    let metadata = manifest.metadata();
    assert_eq!(*metadata.content(), ManifestContentType::Deletes);
    assert_eq!(*metadata.format_version(), FormatVersion::V2);
    assert_eq!(metadata.schema_id(), 3);
    assert_eq!(metadata.schema().as_struct(), schema.as_struct());
    assert!(metadata.partition_spec().is_unpartitioned());

    assert_eq!(manifest.entries().len(), 1);
    let entry = &manifest.entries()[0];
    assert_eq!(entry.status(), ManifestStatus::Added);
    assert_eq!(entry.content_type(), DataContentType::PositionDeletes);
    assert_eq!(entry.record_count(), 3);
    assert_eq!(entry.file_size_in_bytes(), 80);
    assert_eq!(entry.file_path(), format!("{root}/pos-deletes.parquet"));
    assert_eq!(entry.sequence_number(), Some(2));
    assert!(entry.is_alive());
    // "Position Delete Files": `referenced_data_file` names the single data
    // file all of the deletes apply to.
    assert_eq!(
        entry.data_file().referenced_data_file(),
        Some(format!("{root}/1.parquet"))
    );
    assert!(entry.data_file().equality_ids().is_empty());
}

#[tokio::test]
async fn g15_delete_manifest__content_type_mismatches_are_rejected() {
    let (_dir, io, root, schema, spec) = unpartitioned_writer_inputs();
    // A data file may not go into a delete manifest ...
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(format!("{root}/a.avro")).unwrap(),
        Some(1),
        None,
        schema.clone(),
        spec.clone(),
    )
    .build_v2_deletes();
    assert!(
        writer
            .add_file(a_data_file(&format!("{root}/1.parquet"), 0, 1, 10), 1)
            .is_err()
    );

    // ... and a delete file may not go into a data manifest.
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(format!("{root}/b.avro")).unwrap(),
        Some(1),
        None,
        schema,
        spec,
    )
    .build_v2_data();
    assert!(
        writer
            .add_file(
                position_delete_file(
                    &format!("{root}/pos.parquet"),
                    &format!("{root}/1.parquet"),
                    1
                ),
                1,
            )
            .is_err()
    );
}

// ---------------------------------------------------------------------------
// g16_float_order — spec.md "Sorting": floating-point ordering is
// `-NaN < -Infinity < -value < value < Infinity < NaN`.
// ---------------------------------------------------------------------------

#[test]
fn g16_float_order__float_total_order() {
    // "Sorting": `-NaN < -Infinity < -value < -0 < 0 < value < Infinity < NaN`.
    // The values below span a subnormal-adjacent smallest positive, a negative
    // finite, and a large finite, in an order unrelated to the expected one.
    let mut values = vec![
        Datum::float(0.5f32),
        Datum::float(f32::NAN),
        Datum::float(7.25e10f32),
        Datum::float(f32::NEG_INFINITY),
        Datum::float(-12.5f32),
        Datum::float(f32::INFINITY),
        Datum::float(f32::MIN_POSITIVE),
        Datum::float(-f32::NAN),
    ];
    values.sort_by(|a, b| a.partial_cmp(b).unwrap());
    assert_eq!(values, vec![
        Datum::float(-f32::NAN),
        Datum::float(f32::NEG_INFINITY),
        Datum::float(-12.5f32),
        Datum::float(f32::MIN_POSITIVE),
        Datum::float(0.5f32),
        Datum::float(7.25e10f32),
        Datum::float(f32::INFINITY),
        Datum::float(f32::NAN),
    ]);
}

#[test]
fn g16_float_order__double_total_order() {
    let mut values = vec![
        Datum::double(42.0f64),
        Datum::double(-f64::NAN),
        Datum::double(3.0e-300f64),
        Datum::double(f64::INFINITY),
        Datum::double(-1.0e300f64),
        Datum::double(f64::NAN),
        Datum::double(f64::NEG_INFINITY),
    ];
    values.sort_by(|a, b| a.partial_cmp(b).unwrap());
    assert_eq!(values, vec![
        Datum::double(-f64::NAN),
        Datum::double(f64::NEG_INFINITY),
        Datum::double(-1.0e300f64),
        Datum::double(3.0e-300f64),
        Datum::double(42.0f64),
        Datum::double(f64::INFINITY),
        Datum::double(f64::NAN),
    ]);
}

#[test]
fn g16_float_order__nan_is_ordered_not_incomparable() {
    // "Sorting": NaN is ordered, not incomparable, and its sign places it at
    // one end of the range.
    assert!(Datum::float(f32::NAN) > Datum::float(f32::INFINITY));
    assert!(Datum::float(f32::NAN) > Datum::float(6.5f32));
    assert!(Datum::float(-f32::NAN) < Datum::float(f32::NEG_INFINITY));
    assert!(Datum::float(-f32::NAN) < Datum::float(-6.5f32));
    assert!(Datum::double(f64::NAN) > Datum::double(9.5e17f64));
    assert!(Datum::double(-f64::NAN) < Datum::double(-9.5e17f64));
    assert_eq!(
        Datum::float(f32::NAN).partial_cmp(&Datum::float(f32::NAN)),
        Some(std::cmp::Ordering::Equal)
    );

    // Every pair of same-typed float values is comparable.
    let values = [
        Datum::float(f32::NAN),
        Datum::float(-f32::NAN),
        Datum::float(f32::INFINITY),
        Datum::float(f32::NEG_INFINITY),
        Datum::float(f32::MIN_POSITIVE),
        Datum::float(6.5f32),
        Datum::float(-6.5f32),
    ];
    for a in &values {
        for b in &values {
            assert!(
                a.partial_cmp(b).is_some(),
                "{a:?} and {b:?} must be comparable"
            );
        }
    }
}

// ---------------------------------------------------------------------------
// g17_crate_version — TEST_INTERFACE.md section 1: this milestone's release is
// the crate's package version, read out of `/app/Cargo.toml`. The manifest is
// located through the path the driver crate itself declares, so this works
// wherever the drivers are staged. The package NAME and the edition are
// milestone-1 and milestone-4 facts and are left to the gate and to
// `cargo build`; only the version this milestone moves is scored here.
// ---------------------------------------------------------------------------

/// The `Cargo.toml` of the crate under test, located through the driver
/// crate's own `iceberg = { path = ... }` dependency declaration.
fn crate_under_test_manifest() -> String {
    let driver_dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let driver_manifest = std::fs::read_to_string(driver_dir.join("Cargo.toml"))
        .expect("the driver crate's own Cargo.toml");
    let dep_line = driver_manifest
        .lines()
        .find(|l| l.trim_start().starts_with("iceberg") && l.contains("path"))
        .expect("the driver manifest declares iceberg by path");
    let after_path = &dep_line[dep_line.find("path").unwrap()..];
    let open = after_path.find('"').expect("a quoted path") + 1;
    let close = open + after_path[open..].find('"').expect("a closing quote");
    let crate_dir = driver_dir.join(&after_path[open..close]);
    std::fs::read_to_string(crate_dir.join("Cargo.toml"))
        .unwrap_or_else(|e| panic!("cannot read {}/Cargo.toml: {e}", crate_dir.display()))
}

/// Read a `key = "value"` line out of the manifest's `[package]` table.
fn package_key(manifest: &str, key: &str) -> Option<String> {
    let mut in_package = false;
    for raw in manifest.lines() {
        let line = raw.trim();
        if line.starts_with('#') {
            continue;
        }
        if line.starts_with('[') {
            in_package = line == "[package]";
            continue;
        }
        if !in_package {
            continue;
        }
        let Some((k, v)) = line.split_once('=') else {
            continue;
        };
        if k.trim() == key {
            return Some(v.trim().trim_matches('"').to_string());
        }
    }
    None
}

#[test]
fn g17_crate_version__package_version_is_this_milestones_release() {
    let manifest = crate_under_test_manifest();
    assert_eq!(
        package_key(&manifest, "version").as_deref(),
        Some("0.6.0"),
        "TEST_INTERFACE.md section 1 pins this milestone's package version"
    );
}

// ---------------------------------------------------------------------------
// Additional cases for milestone 05, from TEST_INTERFACE.md sections 4.2, 4.4,
// 4.5 and 4.6.
// ---------------------------------------------------------------------------

// No case asserts `add_snapshot`'s one-minute clock-skew guard. Section 5 does
// state the rule — "`add_snapshot` still refuses a snapshot whose timestamp
// precedes the last snapshot-log entry's timestamp — or the table's
// `last-updated-ms` — by more than a minute" — but the very next
// sentence is "No case in this milestone sits near that boundary", which is the
// same "no case does X" idiom section 8 uses for behaviour it declares
// unexercised ("`with_check_duplicate` must exist and default to enabled, but
// no case adds the same data file twice"). Milestone 3, which introduces
// `add_snapshot`, enumerates its errors and does not list this one, and the
// rule appears in no other milestone's documents. Asserting it here would
// contradict what the documents promise about this milestone's cases.

/// TEST_INTERFACE.md 4.6: *"These four values are read from the table's
/// properties at commit time. A value that does not parse as its type must make
/// the commit an `Err` rather than being silently replaced by the default — and
/// note that the commit which writes such a property still succeeds, because it
/// read the properties as they were before it ran."*
///
/// Every value here is wholly non-numeric. Whether a numeral padded with
/// whitespace (`" 2 "`) "parses as its type" is left to the implementation:
/// nothing in the shipped documents says a property value is read without
/// trimming, so a case built on that would be scoring a parser detail the
/// documents do not pin. `g11_conflict__retry_is_configured_by_table_properties` already covers
/// `num-retries`; the other three keys are covered here.
#[tokio::test]
async fn g11_conflict__an_unparseable_retry_property_fails_the_next_commit() {
    for (key, unparseable) in [
        (PROPERTY_COMMIT_NUM_RETRIES, "lots"),
        (PROPERTY_COMMIT_MIN_RETRY_WAIT_MS, "soon"),
        (PROPERTY_COMMIT_MAX_RETRY_WAIT_MS, "one second"),
        (PROPERTY_COMMIT_TOTAL_RETRY_TIME_MS, "forever"),
    ] {
        let (_dir, catalog) = temp_catalog();
        let table = fresh_table(&catalog, "t").await;

        // The commit that writes the property read the properties as they were
        // before it ran, so it still succeeds.
        let tx = Transaction::new(&table);
        let written = tx
            .update_table_properties()
            .set(key.to_string(), unparseable.to_string())
            .apply(tx)
            .unwrap()
            .commit(&catalog)
            .await
            .unwrap();
        assert_eq!(
            written.metadata().properties().get(key).map(String::as_str),
            Some(unparseable)
        );

        // The next commit reads it, and must report it rather than fall back to
        // the default.
        let tx = Transaction::new(&written);
        let tx = tx
            .update_table_properties()
            .set("anything".to_string(), "1".to_string())
            .apply(tx)
            .unwrap();
        assert!(
            tx.commit(&catalog).await.is_err(),
            "{key} = {unparseable:?} does not parse as its type and must fail the commit"
        );
    }
}

/// TEST_INTERFACE.md 4.5: *"`TableRequirement`'s own JSON is not asserted — only
/// that it round-trips through serde at all"*, and section 4's preamble makes
/// `Serialize + Deserialize` part of the type's contract.
#[test]
fn g12_action_reject__table_requirements_round_trip_through_serde() {
    for requirement in [
        TableRequirement::NotExist,
        TableRequirement::UuidMatch {
            uuid: uuid::Uuid::from_u128(1),
        },
        TableRequirement::RefSnapshotIdMatch {
            r#ref: MAIN_BRANCH.to_string(),
            snapshot_id: None,
        },
        TableRequirement::RefSnapshotIdMatch {
            r#ref: MAIN_BRANCH.to_string(),
            snapshot_id: Some(7),
        },
        TableRequirement::CurrentSchemaIdMatch {
            current_schema_id: 3,
        },
        TableRequirement::DefaultSortOrderIdMatch {
            default_sort_order_id: 1,
        },
    ] {
        let json = serde_json::to_value(&requirement).unwrap();
        assert_eq!(
            serde_json::from_value::<TableRequirement>(json).unwrap(),
            requirement
        );
        // `Clone` is load-bearing for a catalog that checks and then applies.
        assert_eq!(requirement.clone(), requirement);
    }
}

/// TEST_INTERFACE.md 4.4: *"Keyed by `statistics_file.snapshot_id`"*, and 4.5:
/// *"one `SetStatistics { statistics }` per file set, one `RemoveStatistics
/// { snapshot_id }` per snapshot removed"*.
#[tokio::test]
async fn g10_statistics__set_is_keyed_by_the_files_snapshot_id() {
    let (_dir, catalog) = temp_catalog();
    let table = fresh_table(&catalog, "t").await;
    let tx = Transaction::new(&table);
    let tx = tx
        .update_statistics()
        .set_statistics(statistics_file(1, "file:///stats-a.puffin"))
        .set_statistics(statistics_file(1, "file:///stats-b.puffin"))
        .set_statistics(statistics_file(2, "file:///stats-c.puffin"))
        .apply(tx)
        .unwrap();
    let committed = tx.commit(&catalog).await.unwrap();

    assert_eq!(
        catalog.last_updates().len(),
        2,
        "one SetStatistics per distinct snapshot id"
    );
    assert_eq!(
        committed
            .metadata()
            .statistics_for_snapshot(1)
            .unwrap()
            .statistics_path,
        "file:///stats-b.puffin",
        "the later file for a snapshot replaces the earlier one"
    );
    assert_eq!(committed.metadata().statistics_iter().count(), 2);
}

/// TEST_INTERFACE.md 4.2: *"`get_namespace`, `update_namespace` and
/// `drop_namespace` naming a namespace that does not exist are errors too."*
#[tokio::test]
async fn g04_catalog_reject__update_namespace_of_a_missing_namespace() {
    let (_dir, catalog) = temp_catalog();
    assert!(catalog
        .update_namespace(
            &NamespaceIdent::from_strs(["nope"]).unwrap(),
            HashMap::new()
        )
        .await
        .is_err());
}

/// TEST_INTERFACE.md 4.2: *"otherwise the catalog's `warehouse_location` joined
/// with the namespace components by `/`; the table location is that prefix plus
/// `/` plus the table name."*
#[tokio::test]
async fn g02_catalog_table__a_nested_namespace_joins_its_components_with_a_slash() {
    let (dir, catalog) = temp_catalog();
    let warehouse = dir.path().to_str().unwrap().to_string();
    let parent = NamespaceIdent::from_strs(["a"]).unwrap();
    let child = NamespaceIdent::from_strs(["a", "b"]).unwrap();
    catalog
        .create_namespace(&parent, HashMap::new())
        .await
        .unwrap();
    catalog
        .create_namespace(&child, HashMap::new())
        .await
        .unwrap();

    let table = catalog
        .create_table(
            &child,
            TableCreation::builder()
                .name("t".to_string())
                .schema(test_schema())
                .build(),
        )
        .await
        .unwrap();
    assert_eq!(table.metadata().location(), format!("{warehouse}/a/b/t"));
}

// ---------------------------------------------------------------------------
// g20_inherited — milestone-1 behaviour this milestone's own section 5 lists as
// "unchanged from the previous milestone and still required in exactly the same
// form", and WORKSPACE_HANDOFF.md's `src/` row: "the cumulative implementation;
// every earlier milestone's behaviour must still work".
//
// These are deliberately narrow: three pure-value families whose signatures do
// not change between milestones (the type system, schema validation and the
// Appendix D single-value forms), so an inherited regression costs one group
// rather than going unnoticed.
// ---------------------------------------------------------------------------

#[test]
fn g20_inherited__transform_result_types_and_names() {
    // spec.md "Partition Transforms": the result-type column, and the textual
    // form `FromStr`/`Display`/serde share.
    for src in [
        PrimitiveType::Int,
        PrimitiveType::Long,
        PrimitiveType::Date,
        PrimitiveType::Time,
        PrimitiveType::Timestamp,
        PrimitiveType::Timestamptz,
        PrimitiveType::String,
        PrimitiveType::Uuid,
        PrimitiveType::Binary,
    ] {
        assert_eq!(
            Transform::Bucket(8)
                .result_type(&Type::Primitive(src.clone()))
                .unwrap_or_else(|e| panic!("bucket over {src:?}: {e}")),
            Type::Primitive(PrimitiveType::Int),
            "bucket always produces int"
        );
    }
    // The four temporal transforms are defined over exactly the source types
    // the "Partition Transforms" table lists for each. Their result *type* is
    // the one row where the specification's table and the released crates have
    // differed between versions, so only the acceptance is asserted here.
    for t in [Transform::Year, Transform::Month, Transform::Day] {
        for src in [
            PrimitiveType::Date,
            PrimitiveType::Timestamp,
            PrimitiveType::Timestamptz,
        ] {
            assert!(
                t.result_type(&Type::Primitive(src.clone())).is_ok(),
                "{t:?} is defined over {src:?}"
            );
        }
    }
    for src in [PrimitiveType::Timestamp, PrimitiveType::Timestamptz] {
        assert!(
            Transform::Hour
                .result_type(&Type::Primitive(src.clone()))
                .is_ok(),
            "hour is defined over {src:?}"
        );
    }
    let s = Type::Primitive(PrimitiveType::String);
    assert_eq!(Transform::Identity.result_type(&s).unwrap(), s);
    assert_eq!(Transform::Void.result_type(&s).unwrap(), s);
    // The rejections the "Source types" column requires.
    assert!(Transform::Bucket(8)
        .result_type(&Type::Primitive(PrimitiveType::Double))
        .is_err());
    assert!(Transform::Truncate(4)
        .result_type(&Type::Primitive(PrimitiveType::Boolean))
        .is_err());
    assert!(Transform::Hour
        .result_type(&Type::Primitive(PrimitiveType::Date))
        .is_err());
    assert!(Transform::Year
        .result_type(&Type::Primitive(PrimitiveType::String))
        .is_err());

    // Ordering properties.
    assert!(Transform::Identity.preserves_order());
    assert!(Transform::Truncate(4).preserves_order());
    assert!(!Transform::Bucket(8).preserves_order());
    assert!(!Transform::Void.preserves_order());
    assert!(Transform::Day.satisfies_order_of(&Transform::Year));
    assert!(Transform::Day.satisfies_order_of(&Transform::Month));
    assert!(Transform::Hour.satisfies_order_of(&Transform::Day));
    assert!(!Transform::Year.satisfies_order_of(&Transform::Day));
    assert!(Transform::Identity.satisfies_order_of(&Transform::Day));
    assert!(!Transform::Bucket(8).satisfies_order_of(&Transform::Identity));
    assert_eq!(Transform::Year.dedup_name(), Transform::Month.dedup_name());
    assert_eq!(Transform::Day.dedup_name(), Transform::Hour.dedup_name());
    assert_ne!(
        Transform::Bucket(8).dedup_name(),
        Transform::Truncate(8).dedup_name()
    );

    // Appendix C textual forms, in all three directions.
    for (name, t) in [
        ("identity", Transform::Identity),
        ("void", Transform::Void),
        ("year", Transform::Year),
        ("month", Transform::Month),
        ("day", Transform::Day),
        ("hour", Transform::Hour),
        ("bucket[16]", Transform::Bucket(16)),
        ("truncate[3]", Transform::Truncate(3)),
    ] {
        assert_eq!(name.parse::<Transform>().unwrap(), t, "parsing {name}");
        assert_eq!(format!("{t}"), name);
        assert_eq!(serde_json::to_string(&t).unwrap(), format!("\"{name}\""));
    }
    assert!("nosuchtransform".parse::<Transform>().is_err());
    assert!("bucket".parse::<Transform>().is_err());
}

#[test]
fn g20_inherited__schema_and_type_json() {
    // Appendix C: primitive type names, the two decimal spellings (PROFILE P9),
    // and a name the specification does not define.
    for (ty, json) in [
        (PrimitiveType::Boolean, "\"boolean\""),
        (PrimitiveType::Int, "\"int\""),
        (PrimitiveType::Long, "\"long\""),
        (PrimitiveType::Date, "\"date\""),
        (PrimitiveType::Time, "\"time\""),
        (PrimitiveType::Timestamp, "\"timestamp\""),
        (PrimitiveType::Timestamptz, "\"timestamptz\""),
        (PrimitiveType::String, "\"string\""),
        (PrimitiveType::Uuid, "\"uuid\""),
        (PrimitiveType::Binary, "\"binary\""),
    ] {
        assert_eq!(
            serde_json::to_string(&Type::Primitive(ty.clone())).unwrap(),
            json
        );
        let back: Type = serde_json::from_str(json).unwrap();
        assert_eq!(back, Type::Primitive(ty));
    }
    assert_eq!(
        serde_json::from_str::<Type>("\"fixed[16]\"").unwrap(),
        Type::Primitive(PrimitiveType::Fixed(16))
    );
    let decimal = Type::Primitive(PrimitiveType::Decimal {
        precision: 9,
        scale: 2,
    });
    for spelling in ["\"decimal(9,2)\"", "\"decimal(9, 2)\""] {
        assert_eq!(
            serde_json::from_str::<Type>(spelling).unwrap(),
            decimal,
            "PROFILE.md P9 requires both spellings to parse"
        );
    }
    assert!(serde_json::from_str::<Type>("\"int64\"").is_err());
    assert!(Type::decimal(39, 2).is_err());
    assert_eq!(Type::decimal_required_bytes(9).unwrap(), 4);
    assert_eq!(Type::decimal_required_bytes(18).unwrap(), 8);
    assert_eq!(Type::decimal_required_bytes(38).unwrap(), 16);
    assert!(Type::decimal_required_bytes(39).is_err());

    // "Identifier Field IDs": an identifier field must exist, be required, and
    // not be floating point.
    assert!(Schema::builder()
        .with_schema_id(0)
        .with_identifier_field_ids(vec![1])
        .with_fields(vec![NestedField::required(
            1,
            "id",
            Type::Primitive(PrimitiveType::Long)
        )
        .into()])
        .build()
        .is_ok());
    for (ids, fields) in [
        (
            vec![2],
            vec![
                NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)),
                NestedField::optional(2, "name", Type::Primitive(PrimitiveType::String)),
            ],
        ),
        (
            vec![1],
            vec![NestedField::required(
                1,
                "d",
                Type::Primitive(PrimitiveType::Double),
            )],
        ),
        (
            vec![7],
            vec![NestedField::required(
                1,
                "id",
                Type::Primitive(PrimitiveType::Long),
            )],
        ),
    ] {
        assert!(
            Schema::builder()
                .with_schema_id(0)
                .with_identifier_field_ids(ids.clone())
                .with_fields(fields.into_iter().map(Into::into).collect::<Vec<_>>())
                .build()
                .is_err(),
            "identifier field ids {ids:?} must be rejected"
        );
    }

    // Nested field names are dotted paths.
    let nested = Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![NestedField::required(
            1,
            "location",
            Type::Struct(StructType::new(vec![
                NestedField::required(2, "lat", Type::Primitive(PrimitiveType::Double)).into(),
                NestedField::required(3, "lon", Type::Primitive(PrimitiveType::Double)).into(),
            ])),
        )
        .into()])
        .build()
        .unwrap();
    assert_eq!(nested.field_id_by_name("location.lat"), Some(2));
    assert_eq!(nested.name_by_field_id(3), Some("location.lon"));
    assert!(nested.field_by_name("absent").is_none());
}

#[test]
fn g20_inherited__appendix_d_single_values() {
    // Appendix D "JSON single-value serialization", both directions.
    for (ty, json) in [
        (PrimitiveType::Boolean, serde_json::json!(true)),
        (PrimitiveType::Int, serde_json::json!(34)),
        (PrimitiveType::Long, serde_json::json!(34)),
        (PrimitiveType::Double, serde_json::json!(1.0)),
        (PrimitiveType::String, serde_json::json!("iceberg")),
        (PrimitiveType::Date, serde_json::json!("2017-11-16")),
        (PrimitiveType::Time, serde_json::json!("22:31:08.123456")),
        (
            PrimitiveType::Timestamp,
            serde_json::json!("2017-11-16T22:31:08.123456"),
        ),
        (
            PrimitiveType::Timestamptz,
            serde_json::json!("2017-11-16T22:31:08.123456+00:00"),
        ),
        (
            PrimitiveType::Uuid,
            serde_json::json!("f79c3e09-677c-4bbd-a479-3f349cb785e7"),
        ),
    ] {
        let t = Type::Primitive(ty.clone());
        let lit = Literal::try_from_json(json.clone(), &t)
            .unwrap_or_else(|e| panic!("try_from_json for {ty:?}: {e}"))
            .unwrap_or_else(|| panic!("try_from_json returned None for {ty:?}"));
        assert_eq!(lit.try_into_json(&t).unwrap(), json, "json round trip {ty:?}");
    }
    // A decimal literal carries the UNSCALED value; the scale is the type's,
    // so the same unscaled value read against two scales compares equal.
    let scale_two = Type::Primitive(PrimitiveType::Decimal {
        precision: 9,
        scale: 2,
    });
    let scale_one = Type::Primitive(PrimitiveType::Decimal {
        precision: 9,
        scale: 1,
    });
    let from_scale_two = Literal::try_from_json(serde_json::json!("14.20"), &scale_two)
        .unwrap()
        .unwrap();
    let from_scale_one = Literal::try_from_json(serde_json::json!("142.0"), &scale_one)
        .unwrap()
        .unwrap();
    assert_eq!(
        from_scale_two.clone().try_into_json(&scale_two).unwrap(),
        serde_json::json!("14.20")
    );
    assert_eq!(
        from_scale_one.clone().try_into_json(&scale_one).unwrap(),
        serde_json::json!("142.0")
    );
    assert_eq!(
        from_scale_one, from_scale_two,
        "two literals built from the same unscaled value compare equal"
    );
    // JSON null decodes to no literal.
    assert!(Literal::try_from_json(
        serde_json::Value::Null,
        &Type::Primitive(PrimitiveType::Int)
    )
    .unwrap()
    .is_none());

    // Appendix D "Binary single-value serialization", through the typed value
    // this milestone uses for bounds.
    assert_eq!(
        Datum::try_from_bytes(&34i32.to_le_bytes(), PrimitiveType::Int).unwrap(),
        Datum::int(34)
    );
    assert_eq!(
        Datum::try_from_bytes(&34i64.to_le_bytes(), PrimitiveType::Long).unwrap(),
        Datum::long(34)
    );
    assert_eq!(
        Datum::try_from_bytes(&[0x00], PrimitiveType::Boolean).unwrap(),
        Datum::bool(false)
    );
    assert_eq!(
        Datum::try_from_bytes(&[0x01], PrimitiveType::Boolean).unwrap(),
        Datum::bool(true)
    );
    assert_eq!(
        Datum::try_from_bytes(&1.5f32.to_le_bytes(), PrimitiveType::Float).unwrap(),
        Datum::float(1.5f32)
    );
    assert_eq!(
        Datum::try_from_bytes(&1.5f64.to_le_bytes(), PrimitiveType::Double).unwrap(),
        Datum::double(1.5f64)
    );
    assert_eq!(
        Datum::try_from_bytes("iceberg".as_bytes(), PrimitiveType::String).unwrap(),
        Datum::string("iceberg")
    );
}
