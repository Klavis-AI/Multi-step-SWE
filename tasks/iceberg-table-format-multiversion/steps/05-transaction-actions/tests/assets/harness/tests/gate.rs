// Verifier-owned gate for milestone 05 (transaction actions + memory catalog).
//
// The gate is this milestone's headline capability at proof-of-life depth,
// plus one end-to-end path through every milestone it inherits, scored as the
// single group `gate-endtoend`. Harder variants are scored by
// tests/conformance.rs and still run when this fails.
//
//   gate01 — table format core (metadata, manifests, manifest lists, transforms)
//   gate02 — scan planning
//   gate03 — TableMetadataBuilder produces committable metadata
//   gate04 — delete files round trip through a delete manifest
//   gate05 — MemoryCatalog + Transaction fast append commits a new snapshot

use std::collections::HashMap;
use std::future::Future;
use std::pin::Pin;
use std::sync::{Arc, Mutex};

use iceberg::io::{FileIO, FileIOBuilder};
use iceberg::spec::*;
use iceberg::table::Table;
use iceberg::transaction::{ApplyTransactionAction, Transaction};
use iceberg::{
    Catalog, MemoryCatalog, Namespace, NamespaceIdent, Result, TableCommit, TableCreation,
    TableIdent,
};

const GATE_METADATA: &str = r#"{
  "format-version": 2,
  "table-uuid": "9c12d441-03fe-4693-9a96-a0705ddf69c1",
  "location": "file:///tmp/gate/table",
  "last-sequence-number": 3,
  "last-updated-ms": 1602638573590,
  "last-column-id": 2,
  "current-schema-id": 0,
  "schemas": [
    {"type": "struct", "schema-id": 0, "fields": [
      {"id": 1, "name": "id", "required": true, "type": "long"},
      {"id": 2, "name": "ts", "required": false, "type": "timestamptz"}
    ]}
  ],
  "default-spec-id": 0,
  "partition-specs": [{"spec-id": 0, "fields": [
    {"name": "ts_day", "transform": "day", "source-id": 2, "field-id": 1000}
  ]}],
  "last-partition-id": 1000,
  "default-sort-order-id": 0,
  "sort-orders": [{"order-id": 0, "fields": []}],
  "properties": {},
  "current-snapshot-id": 42,
  "snapshots": [
    {"snapshot-id": 42, "timestamp-ms": 1555100955770, "sequence-number": 3,
     "summary": {"operation": "append"}, "manifest-list": "file:///tmp/gate/ml.avro",
     "schema-id": 0}
  ],
  "snapshot-log": [{"snapshot-id": 42, "timestamp-ms": 1555100955770}],
  "metadata-log": []
}"#;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

fn next_metadata_uuid() -> uuid::Uuid {
    // `Uuid::new_v4()` is behind uuid's non-default `v4` feature; the driver
    // manifest does not enable it and must not depend on a candidate's graph
    // enabling it transitively. `from_u128` is feature-free.
    static N: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);
    uuid::Uuid::from_u128(N.fetch_add(1, std::sync::atomic::Ordering::Relaxed) as u128)
}

fn gate_schema() -> Schema {
    Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "name", Type::Primitive(PrimitiveType::String)).into(),
        ])
        .build()
        .unwrap()
}

fn gate_data_file(path: &str, spec_id: i32, records: u64) -> DataFile {
    DataFileBuilder::default()
        .content(DataContentType::Data)
        .file_path(path.to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .partition_spec_id(spec_id)
        .record_count(records)
        .file_size_in_bytes(512)
        .build()
        .unwrap()
}

/// A `Catalog` that delegates to `MemoryCatalog` and supplies the metadata
/// swap of spec.md "Metastore Tables" that `MemoryCatalog` does not implement:
/// `update_table` validates the commit's requirements, applies its updates and
/// re-points the catalog entry at the new metadata file.
type Fut<'x, T> = Pin<Box<dyn Future<Output = Result<T>> + Send + 'x>>;

#[derive(Debug)]
struct GateCatalog {
    inner: MemoryCatalog,
    file_io: FileIO,
    commits: Mutex<usize>,
}

impl GateCatalog {
    fn new(warehouse: String) -> Self {
        let file_io = FileIOBuilder::new_fs_io().build().unwrap();
        Self {
            inner: MemoryCatalog::new(file_io.clone(), Some(warehouse)),
            file_io,
            commits: Mutex::new(0),
        }
    }
}

impl Catalog for GateCatalog {
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
        Box::pin(async move { self.inner.load_table(table).await })
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
            let ident = commit.identifier().clone();
            let current = self.inner.load_table(&ident).await?;
            let old_metadata_location = current.metadata_location().map(str::to_string);
            // `TableCommit::apply` checks the requirements and applies the
            // updates, returning the table the commit would produce — carrying
            // the OLD metadata location, because writing the new document and
            // re-pointing the catalog is the catalog's job.
            let updated = commit.apply(current)?;
            assert_eq!(
                updated.metadata_location().map(str::to_string),
                old_metadata_location,
                "TableCommit::apply must keep the old metadata location"
            );
            let metadata = updated.metadata().clone();

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
            *self.commits.lock().unwrap() += 1;
            self.inner.register_table(&ident, new_location).await
        })
    }
}

async fn gate_table(dir: &tempfile::TempDir) -> (GateCatalog, Table) {
    let catalog = GateCatalog::new(dir.path().to_str().unwrap().to_string());
    let namespace = NamespaceIdent::from_strs(["db"]).unwrap();
    catalog
        .create_namespace(&namespace, HashMap::new())
        .await
        .unwrap();
    let table = catalog
        .create_table(
            &namespace,
            TableCreation::builder()
                .name("gate".to_string())
                .schema(gate_schema())
                .build(),
        )
        .await
        .unwrap();
    (catalog, table)
}

/// Commits one fast append of `records` rows and returns the resulting table.
async fn append(catalog: &GateCatalog, table: &Table, file: &str, records: u64) -> Table {
    let path = format!("{}/data/{file}", table.metadata().location());
    let tx = Transaction::new(table);
    let tx = tx
        .fast_append()
        .add_data_files(vec![gate_data_file(
            &path,
            table.metadata().default_partition_spec_id(),
            records,
        )])
        .apply(tx)
        .unwrap();
    tx.commit(catalog).await.expect("fast append must commit")
}

// ---------------------------------------------------------------------------
// gate01 — table format core (inherited from milestone 01)
// ---------------------------------------------------------------------------

/// Table metadata is readable and exposes the current schema, the default
/// partition spec and the current snapshot.
#[test]
fn gate01__table_metadata_is_readable() {
    let md: TableMetadata = serde_json::from_str(GATE_METADATA).expect("parse table metadata");
    assert_eq!(md.format_version(), FormatVersion::V2);
    assert_eq!(md.current_schema().as_struct().fields().len(), 2);
    assert_eq!(md.current_schema().field_by_name("id").unwrap().id, 1);
    let spec = md.default_partition_spec();
    assert_eq!(spec.fields()[0].transform, Transform::Day);
    assert_eq!(spec.fields()[0].field_id, 1000);
    let snapshot = md.current_snapshot().expect("current snapshot");
    assert_eq!(snapshot.snapshot_id(), 42);
    assert_eq!(snapshot.sequence_number(), 3);
}

/// PROFILE.md P1: format versions 1 and 2 are in scope, and a
/// `format-version` outside that set must be rejected when parsing table
/// metadata. Milestone 1 introduced this; section 7 gates inherited
/// behaviour rather than scoring it.
#[test]
fn gate01__an_out_of_range_format_version_is_rejected() {
    let v3 = GATE_METADATA.replace("\"format-version\": 2", "\"format-version\": 3");
    assert!(
        serde_json::from_str::<TableMetadata>(&v3).is_err(),
        "table metadata claiming format version 3 must be rejected"
    );
    // The same document at version 2 parses, so the rejection is about the
    // version and nothing else.
    let v2: TableMetadata = serde_json::from_str(GATE_METADATA).expect("parse table metadata");
    assert_eq!(v2.format_version(), FormatVersion::V2);
}

/// A manifest written through the public writer can be read back through the
/// public reader, and is a real Avro object container file.
#[tokio::test]
async fn gate01__manifest_write_read_roundtrip() {
    let dir = tempfile::TempDir::new().unwrap();
    let root = dir.path().to_str().unwrap().to_string();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let schema: SchemaRef = Arc::new(gate_schema());
    let spec = PartitionSpec::builder(schema.clone())
        .with_spec_id(0)
        .build()
        .unwrap();
    let spec_id = spec.spec_id();

    let path = format!("{root}/gate-m0.avro");
    let mut writer =
        ManifestWriterBuilder::new(io.new_output(&path).unwrap(), Some(42), None, schema, spec)
            .build_v2_data();
    writer
        .add_file(gate_data_file(&format!("{root}/data-0.parquet"), spec_id, 3), 3)
        .unwrap();
    let manifest_file = writer.write_manifest_file().await.expect("write manifest");
    assert_eq!(manifest_file.added_files_count, Some(1));
    assert_eq!(manifest_file.added_rows_count, Some(3));

    let bytes = std::fs::read(&path).unwrap();
    assert_eq!(&bytes[..4], b"Obj\x01");
    let parsed = Manifest::parse_avro(&bytes).expect("parse manifest");
    assert_eq!(parsed.entries().len(), 1);
    assert_eq!(
        parsed.entries()[0].file_path(),
        format!("{root}/data-0.parquet")
    );
    assert_eq!(parsed.entries()[0].content_type(), DataContentType::Data);
}

/// A manifest list written through the public writer round trips.
#[tokio::test]
async fn gate01__manifest_list_roundtrip() {
    let dir = tempfile::TempDir::new().unwrap();
    let root = dir.path().to_str().unwrap().to_string();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let schema: SchemaRef = Arc::new(gate_schema());
    let spec = PartitionSpec::builder(schema.clone())
        .with_spec_id(0)
        .build()
        .unwrap();
    let spec_id = spec.spec_id();

    let manifest_path = format!("{root}/gate-m0.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&manifest_path).unwrap(),
        Some(42),
        None,
        schema,
        spec,
    )
    .build_v2_data();
    writer
        .add_file(gate_data_file(&format!("{root}/data-0.parquet"), spec_id, 3), 3)
        .unwrap();
    let manifest_file = writer.write_manifest_file().await.unwrap();

    let list_path = format!("{root}/gate-ml.avro");
    let mut list_writer =
        ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 42, Some(41), 3);
    list_writer
        .add_manifests(vec![manifest_file].into_iter())
        .unwrap();
    list_writer.close().await.expect("close manifest list");

    let bytes = std::fs::read(&list_path).unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2)
        .expect("parse manifest list");
    assert_eq!(list.entries().len(), 1);
    assert_eq!(list.entries()[0].manifest_path, manifest_path);
    assert_eq!(list.entries()[0].added_snapshot_id, 42);
    assert_eq!(list.entries()[0].sequence_number, 3);
}

/// Partition transforms are parseable and typed per the spec.
#[test]
fn gate01__partition_transforms() {
    assert_eq!(
        "bucket[16]".parse::<Transform>().unwrap(),
        Transform::Bucket(16)
    );
    assert_eq!(
        Transform::Bucket(16)
            .result_type(&Type::Primitive(PrimitiveType::String))
            .unwrap(),
        Type::Primitive(PrimitiveType::Int)
    );
    assert_eq!(
        Transform::Identity
            .result_type(&Type::Primitive(PrimitiveType::Long))
            .unwrap(),
        Type::Primitive(PrimitiveType::Long)
    );
}

// ---------------------------------------------------------------------------
// gate02 — scan planning
// ---------------------------------------------------------------------------

/// A scan built over a committed table resolves the snapshot and projection.
#[tokio::test]
async fn gate02__scan_resolves_snapshot_and_projection() {
    let dir = tempfile::TempDir::new().unwrap();
    let (catalog, table) = gate_table(&dir).await;
    let table = append(&catalog, &table, "1.parquet", 4).await;
    let snapshot_id = table.metadata().current_snapshot().unwrap().snapshot_id();

    let scan = table.scan().build().expect("build scan");
    assert_eq!(scan.snapshot().unwrap().snapshot_id(), snapshot_id);
    assert!(scan.column_names().is_none(), "no projection means all columns");

    let projected = table.scan().select(["id"]).build().expect("build scan");
    assert_eq!(projected.column_names(), Some(&["id".to_string()][..]));

    let pinned = table
        .scan()
        .snapshot_id(snapshot_id)
        .build()
        .expect("build scan");
    assert_eq!(pinned.snapshot().unwrap().snapshot_id(), snapshot_id);
}

/// Planning reads the snapshot's manifest list from storage.
#[tokio::test]
async fn gate02__plan_files_reads_the_manifest_list() {
    let dir = tempfile::TempDir::new().unwrap();
    let (catalog, table) = gate_table(&dir).await;
    let table = append(&catalog, &table, "1.parquet", 4).await;
    let manifest_list = table
        .metadata()
        .current_snapshot()
        .unwrap()
        .manifest_list()
        .to_string();

    let scan = table.scan().build().unwrap();
    assert!(
        scan.plan_files().await.is_ok(),
        "planning must load and parse the manifest list"
    );

    // With the manifest list gone, planning must report the failure.
    std::fs::remove_file(&manifest_list).unwrap();
    let reloaded = catalog.load_table(table.identifier()).await.unwrap();
    let scan = reloaded.scan().build().unwrap();
    assert!(scan.plan_files().await.is_err());
}

/// Scan building rejects projections and snapshots the table does not have.
#[tokio::test]
async fn gate02__scan_rejects_unknown_column_and_snapshot() {
    let dir = tempfile::TempDir::new().unwrap();
    let (catalog, table) = gate_table(&dir).await;
    let table = append(&catalog, &table, "1.parquet", 1).await;

    assert!(table.scan().select(["not_a_column"]).build().is_err());
    assert!(table.scan().snapshot_id(-999).build().is_err());
}

// ---------------------------------------------------------------------------
// gate03 — TableMetadataBuilder produces committable metadata
// ---------------------------------------------------------------------------

/// Metadata built from scratch carries every field a commit needs.
#[test]
fn gate03__builder_creates_committable_metadata() {
    let result = TableMetadataBuilder::new(
        gate_schema(),
        UnboundPartitionSpec::builder().build(),
        SortOrder::unsorted_order(),
        "file:///tmp/gate/built".to_string(),
        FormatVersion::V2,
        HashMap::from([("k".to_string(), "v".to_string())]),
    )
    .expect("new metadata builder")
    .build()
    .expect("build metadata");
    let metadata = result.metadata;

    assert_eq!(metadata.format_version(), FormatVersion::V2);
    assert_eq!(metadata.location(), "file:///tmp/gate/built");
    assert_eq!(metadata.current_schema().as_struct().fields().len(), 2);
    assert_eq!(metadata.last_column_id(), 2);
    assert!(metadata.default_partition_spec().is_unpartitioned());
    assert!(metadata.default_sort_order().is_unsorted());
    assert!(metadata.current_snapshot().is_none());
    assert_eq!(metadata.last_sequence_number(), 0);
    assert_eq!(metadata.properties().get("k").map(String::as_str), Some("v"));
    assert!(metadata.last_updated_ms() > 0);
    assert!(!result.changes.is_empty(), "the builder records its updates");

    // The metadata a catalog would persist round trips through JSON.
    let json = serde_json::to_value(&metadata).unwrap();
    assert_eq!(json["format-version"], 2);
    let back: TableMetadata = serde_json::from_value(json).unwrap();
    assert_eq!(back, metadata);
}

/// Applying catalog updates through the builder is how a commit lands.
#[test]
fn gate03__updates_apply_through_the_builder() {
    let base: TableMetadata = serde_json::from_str(GATE_METADATA).unwrap();
    let mut builder = base.clone().into_builder(None);
    for update in [
        iceberg::TableUpdate::SetProperties {
            updates: HashMap::from([("owner".to_string(), "gate".to_string())]),
        },
        iceberg::TableUpdate::SetLocation {
            location: "file:///tmp/gate/moved".to_string(),
        },
    ] {
        builder = update.apply(builder).expect("apply update");
    }
    let metadata = builder.build().expect("build metadata").metadata;

    assert_eq!(
        metadata.properties().get("owner").map(String::as_str),
        Some("gate")
    );
    assert_eq!(metadata.location(), "file:///tmp/gate/moved");
    assert_eq!(metadata.uuid(), base.uuid());
    assert_eq!(
        metadata.current_snapshot().unwrap().snapshot_id(),
        base.current_snapshot().unwrap().snapshot_id()
    );
}

/// A snapshot added through the builder becomes the current snapshot.
#[test]
fn gate03__builder_adds_a_snapshot() {
    let base: TableMetadata = serde_json::from_str(GATE_METADATA).unwrap();
    let snapshot = Snapshot::builder()
        .with_snapshot_id(43)
        .with_parent_snapshot_id(Some(42))
        .with_sequence_number(4)
        .with_timestamp_ms(1602638574000)
        .with_manifest_list("file:///tmp/gate/ml-2.avro")
        .with_schema_id(0)
        .with_summary(Summary {
            operation: Operation::Append,
            additional_properties: HashMap::new(),
        })
        .build();

    let metadata = base
        .into_builder(None)
        .add_snapshot(snapshot)
        .expect("add snapshot")
        .set_ref(
            MAIN_BRANCH,
            SnapshotReference::new(43, SnapshotRetention::branch(None, None, None)),
        )
        .expect("set main")
        .build()
        .expect("build metadata")
        .metadata;

    let current = metadata.current_snapshot().expect("current snapshot");
    assert_eq!(current.snapshot_id(), 43);
    assert_eq!(current.parent_snapshot_id(), Some(42));
    assert_eq!(current.sequence_number(), 4);
    assert_eq!(metadata.last_sequence_number(), 4);
    assert_eq!(metadata.snapshots().count(), 2);
    assert_eq!(metadata.history().last().unwrap().snapshot_id, 43);
}

// ---------------------------------------------------------------------------
// gate04 — delete files round trip through a delete manifest
// ---------------------------------------------------------------------------

fn delete_manifest_inputs() -> (tempfile::TempDir, FileIO, String, SchemaRef, PartitionSpec) {
    let dir = tempfile::TempDir::new().unwrap();
    let root = dir.path().to_str().unwrap().to_string();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let schema: SchemaRef = Arc::new(gate_schema());
    let spec = PartitionSpec::builder(schema.clone())
        .with_spec_id(0)
        .build()
        .unwrap();
    (dir, io, root, schema, spec)
}

/// A position delete file round trips through a v2 delete manifest.
#[tokio::test]
async fn gate04__position_delete_round_trip() {
    let (_dir, io, root, schema, spec) = delete_manifest_inputs();
    let spec_id = spec.spec_id();
    let path = format!("{root}/gate-deletes.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(77),
        None,
        schema,
        spec,
    )
    .build_v2_deletes();
    let delete_file = DataFileBuilder::default()
        .content(DataContentType::PositionDeletes)
        .file_path(format!("{root}/pos-deletes.parquet"))
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .partition_spec_id(spec_id)
        .record_count(2)
        .file_size_in_bytes(64)
        .referenced_data_file(Some(format!("{root}/data-0.parquet")))
        .build()
        .unwrap();
    writer.add_file(delete_file, 5).unwrap();
    let manifest_file = writer.write_manifest_file().await.expect("write manifest");
    assert_eq!(manifest_file.content, ManifestContentType::Deletes);
    assert_eq!(manifest_file.added_files_count, Some(1));

    let parsed = Manifest::parse_avro(&std::fs::read(&path).unwrap()).expect("parse manifest");
    assert_eq!(*parsed.metadata().content(), ManifestContentType::Deletes);
    assert_eq!(parsed.entries().len(), 1);
    let entry = &parsed.entries()[0];
    assert_eq!(entry.content_type(), DataContentType::PositionDeletes);
    assert_eq!(entry.record_count(), 2);
    assert_eq!(
        entry.data_file().referenced_data_file(),
        Some(format!("{root}/data-0.parquet"))
    );
}

/// An equality delete file round trips, keeping its equality field ids.
#[tokio::test]
async fn gate04__equality_delete_round_trip() {
    let (_dir, io, root, schema, spec) = delete_manifest_inputs();
    let spec_id = spec.spec_id();
    let path = format!("{root}/gate-eq-deletes.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(78),
        None,
        schema,
        spec,
    )
    .build_v2_deletes();
    let delete_file = DataFileBuilder::default()
        .content(DataContentType::EqualityDeletes)
        .file_path(format!("{root}/eq-deletes.parquet"))
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .partition_spec_id(spec_id)
        .record_count(1)
        .file_size_in_bytes(48)
        .equality_ids(vec![1])
        .build()
        .unwrap();
    writer.add_file(delete_file, 6).unwrap();
    let manifest_file = writer.write_manifest_file().await.expect("write manifest");

    let list_path = format!("{root}/gate-eq-list.avro");
    let mut list_writer = ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 78, None, 6);
    list_writer
        .add_manifests(vec![manifest_file].into_iter())
        .unwrap();
    list_writer.close().await.unwrap();
    let list =
        ManifestList::parse_with_version(&std::fs::read(&list_path).unwrap(), FormatVersion::V2)
            .expect("parse manifest list");
    assert_eq!(list.entries()[0].content, ManifestContentType::Deletes);
    assert_eq!(list.entries()[0].sequence_number, 6);

    let parsed = Manifest::parse_avro(&std::fs::read(&path).unwrap()).expect("parse manifest");
    let entry = &parsed.entries()[0];
    assert_eq!(entry.content_type(), DataContentType::EqualityDeletes);
    assert_eq!(entry.data_file().equality_ids(), &[1]);
}

/// Milestone 4's metadata-inspection tables and Puffin blob surface are still
/// exported and still describe themselves the same way.
#[tokio::test]
async fn gate04__inspect_and_puffin_surface_is_preserved() {
    let dir = tempfile::TempDir::new().unwrap();
    let (catalog, table) = gate_table(&dir).await;
    let committed = append(&catalog, &table, "1.parquet", 2).await;

    // `iceberg::inspect`, reached through `Table::inspect()`.
    let snapshots_schema = committed.inspect().snapshots().schema();
    let snapshot_fields = snapshots_schema.as_struct().fields();
    assert_eq!(snapshot_fields.len(), 6);
    assert_eq!(snapshot_fields[0].name, "committed_at");
    assert_eq!(snapshot_fields[0].id, 1);
    assert_eq!(snapshot_fields[5].name, "summary");

    let manifests_schema = committed.inspect().manifests().schema();
    let manifest_fields = manifests_schema.as_struct().fields();
    assert_eq!(manifest_fields.len(), 12);
    assert_eq!(manifest_fields[0].name, "content");
    assert_eq!(manifest_fields[0].id, 14);
    assert_eq!(manifest_fields[1].name, "path");

    // `MetadataTable::new(&table)` and `table.inspect()` are the same view.
    assert_eq!(
        iceberg::inspect::MetadataTable::new(&committed)
            .snapshots()
            .schema()
            .as_struct(),
        snapshots_schema.as_struct()
    );

    // `iceberg::puffin`: a blob carries its type, fields and payload.
    let blob = iceberg::puffin::Blob::builder()
        .r#type(iceberg::puffin::DELETION_VECTOR_V1.to_string())
        .fields(vec![1])
        .snapshot_id(
            committed
                .metadata()
                .current_snapshot()
                .unwrap()
                .snapshot_id(),
        )
        .sequence_number(1)
        .data(vec![1u8, 2, 3])
        .properties(HashMap::new())
        .build();
    assert_eq!(blob.blob_type(), "deletion-vector-v1");
    assert_eq!(blob.fields(), &[1]);
    assert_eq!(blob.data(), &[1u8, 2, 3]);
    assert_eq!(
        iceberg::puffin::CompressionCodec::default(),
        iceberg::puffin::CompressionCodec::None
    );
    // Puffin is one of the data-file formats from milestone 4 on.
    assert_eq!(
        "puffin".parse::<DataFileFormat>().unwrap(),
        DataFileFormat::Puffin
    );
}

// ---------------------------------------------------------------------------
// gate05 — MemoryCatalog + Transaction fast append
// ---------------------------------------------------------------------------

/// A `MemoryCatalog` creates a table and loads it back.
#[tokio::test]
async fn gate05__memory_catalog_creates_and_loads_a_table() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let catalog = MemoryCatalog::new(io, Some(dir.path().to_str().unwrap().to_string()));
    let namespace = NamespaceIdent::from_strs(["db"]).unwrap();
    catalog
        .create_namespace(&namespace, HashMap::new())
        .await
        .expect("create namespace");

    let table = catalog
        .create_table(
            &namespace,
            TableCreation::builder()
                .name("t".to_string())
                .schema(gate_schema())
                .build(),
        )
        .await
        .expect("create table");
    assert_eq!(
        table.identifier(),
        &TableIdent::from_strs(["db", "t"]).unwrap()
    );
    assert_eq!(table.metadata().format_version(), FormatVersion::V2);
    assert!(table.metadata().current_snapshot().is_none());

    let loaded = catalog
        .load_table(table.identifier())
        .await
        .expect("load table");
    assert_eq!(loaded.metadata(), table.metadata());
    assert_eq!(catalog.list_tables(&namespace).await.unwrap().len(), 1);
}

/// A fast-append transaction commits and produces a new snapshot.
#[tokio::test]
async fn gate05__fast_append_transaction_commits_a_snapshot() {
    let dir = tempfile::TempDir::new().unwrap();
    let (catalog, table) = gate_table(&dir).await;
    assert!(table.metadata().current_snapshot().is_none());

    let committed = append(&catalog, &table, "1.parquet", 4).await;
    assert_eq!(*catalog.commits.lock().unwrap(), 1);

    let snapshot = committed
        .metadata()
        .current_snapshot()
        .expect("append must create a snapshot");
    assert_eq!(snapshot.parent_snapshot_id(), None);
    assert_eq!(snapshot.sequence_number(), 1);
    assert_eq!(committed.metadata().last_sequence_number(), 1);
    assert_eq!(snapshot.summary().operation, Operation::Append);
    assert_eq!(committed.metadata().history().len(), 1);

    // The commit is durable: reloading from the catalog sees the snapshot.
    let reloaded = catalog.load_table(table.identifier()).await.unwrap();
    assert_eq!(
        reloaded.metadata().current_snapshot().unwrap().snapshot_id(),
        snapshot.snapshot_id()
    );
}

/// A second fast append chains onto the first snapshot.
#[tokio::test]
async fn gate05__second_append_chains_onto_the_first() {
    let dir = tempfile::TempDir::new().unwrap();
    let (catalog, table) = gate_table(&dir).await;
    let first = append(&catalog, &table, "1.parquet", 2).await;
    let first_id = first.metadata().current_snapshot().unwrap().snapshot_id();
    let second = append(&catalog, &first, "2.parquet", 3).await;

    let snapshot = second.metadata().current_snapshot().unwrap();
    assert_eq!(snapshot.parent_snapshot_id(), Some(first_id));
    assert_eq!(snapshot.sequence_number(), 2);
    assert_eq!(second.metadata().snapshots().count(), 2);
    assert_eq!(
        second
            .metadata()
            .snapshot_for_ref(MAIN_BRANCH)
            .unwrap()
            .snapshot_id(),
        snapshot.snapshot_id()
    );
}
