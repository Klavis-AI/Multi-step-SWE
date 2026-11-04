// Verifier-owned gate for milestone 04 (format-v2 row-level deletes, Puffin,
// metadata tables, object cache).
//
// The gate is this milestone's headline capability at proof-of-life depth, plus
// one end-to-end path through every milestone it inherits, scored as the single
// group `gate-endtoend`. Harder variants are scored by tests/conformance.rs and
// still run when this fails.
//
//   gate01__ — milestone 01: table format core (types, schemas, transforms,
//              metadata, manifests, manifest lists).
//   gate02__ — milestone 02: scan planning end to end.
//   gate03__ — milestone 03: TableMetadataBuilder produces committable metadata.
//   gate04__ — milestone 04: a delete file round-trips through a delete manifest.

use std::collections::HashMap;
use std::sync::Arc;

use iceberg::io::{FileIO, FileIOBuilder};
use iceberg::spec::*;

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

fn gate_schema() -> Schema {
    Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "ts", Type::Primitive(PrimitiveType::Timestamptz)).into(),
        ])
        .build()
        .unwrap()
}

fn gate_data_file(path: &str) -> DataFile {
    DataFileBuilder::default()
        .content(DataContentType::Data)
        .file_path(path.to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .record_count(3)
        .file_size_in_bytes(512)
        .partition_spec_id(0)
        .build()
        .unwrap()
}

// ---------------------------------------------------------------------------
// gate01 — milestone 01: table format core.
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

/// A manifest written through the public writer can be read back through the
/// public reader, and is a real Avro object container file.
#[tokio::test]
async fn gate01__manifest_write_read_roundtrip() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let schema: SchemaRef = Arc::new(gate_schema());
    let spec = PartitionSpec::builder(schema.clone())
        .with_spec_id(0)
        .build()
        .unwrap();

    let path = format!("{}/gate-m0.avro", dir.path().to_str().unwrap());
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(42),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_data();
    writer
        .add_file(gate_data_file("/tmp/gate/data-0.parquet"), 3)
        .expect("add data file");
    let manifest_file = writer.write_manifest_file().await.expect("write manifest");
    assert_eq!(manifest_file.added_files_count, Some(1));
    assert_eq!(manifest_file.added_snapshot_id, 42);
    assert_eq!(manifest_file.content, ManifestContentType::Data);

    let bytes = std::fs::read(&path).unwrap();
    assert_eq!(&bytes[..4], b"Obj\x01");
    let parsed = Manifest::parse_avro(&bytes).expect("parse manifest");
    assert_eq!(parsed.entries().len(), 1);
    assert_eq!(parsed.entries()[0].file_path(), "/tmp/gate/data-0.parquet");
    assert_eq!(parsed.entries()[0].content_type(), DataContentType::Data);
    assert!(parsed.entries()[0].is_alive());
}

/// A manifest list written through the public writer round trips.
#[tokio::test]
async fn gate01__manifest_list_roundtrip() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let path = format!("{}/gate-ml.avro", dir.path().to_str().unwrap());
    let mut writer = ManifestListWriter::v2(io.new_output(&path).unwrap(), 42, None, 3);
    writer
        .add_manifests(
            vec![ManifestFile {
                manifest_path: "/tmp/gate/m0.avro".to_string(),
                manifest_length: 512,
                partition_spec_id: 0,
                content: ManifestContentType::Data,
                sequence_number: 3,
                min_sequence_number: 3,
                added_snapshot_id: 42,
                added_files_count: Some(1),
                existing_files_count: Some(0),
                deleted_files_count: Some(0),
                added_rows_count: Some(3),
                existing_rows_count: Some(0),
                deleted_rows_count: Some(0),
                partitions: vec![],
                key_metadata: vec![],
            }]
            .into_iter(),
        )
        .unwrap();
    writer.close().await.expect("close manifest list");

    let bytes = std::fs::read(&path).unwrap();
    assert_eq!(&bytes[..4], b"Obj\x01");
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2, |_id| Ok(None))
        .expect("parse manifest list");
    assert_eq!(list.entries().len(), 1);
    assert_eq!(list.entries()[0].manifest_path, "/tmp/gate/m0.avro");
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
    assert!("nosuchtransform".parse::<Transform>().is_err());
}

// ---------------------------------------------------------------------------
// gate02 — milestone 02: scan planning end to end.
// ---------------------------------------------------------------------------

/// Builds a real table on disk: one data manifest, one manifest list, one
/// metadata file, all reachable through `FileIO`.
async fn build_gate_table(dir: &tempfile::TempDir) -> (FileIO, iceberg::table::Table) {
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let root = dir.path().to_str().unwrap().to_string();
    std::fs::create_dir_all(format!("{root}/metadata")).unwrap();

    let schema: SchemaRef = Arc::new(gate_schema());
    let spec = PartitionSpec::builder(schema.clone())
        .with_spec_id(0)
        .build()
        .unwrap();

    let manifest_path = format!("{root}/metadata/m0.avro");
    let mut mw = ManifestWriterBuilder::new(
        io.new_output(&manifest_path).unwrap(),
        Some(42),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_data();
    mw.add_file(
        gate_data_file(&format!("{root}/data/data-0.parquet")),
        UNASSIGNED_SEQUENCE_NUMBER,
    )
    .unwrap();
    let manifest_file = mw.write_manifest_file().await.unwrap();

    let list_path = format!("{root}/metadata/snap-42.avro");
    let mut lw = ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 42, None, 3);
    lw.add_manifests(vec![manifest_file].into_iter()).unwrap();
    lw.close().await.unwrap();

    let metadata_json = GATE_METADATA
        .replace("file:///tmp/gate/table", &root)
        .replace("file:///tmp/gate/ml.avro", &list_path);
    let metadata: TableMetadata = serde_json::from_str(&metadata_json).unwrap();
    let metadata_location = format!("{root}/metadata/v1.metadata.json");
    std::fs::write(&metadata_location, &metadata_json).unwrap();

    let table = iceberg::table::Table::builder()
        .identifier(iceberg::TableIdent::from_strs(["gate", "t"]).unwrap())
        .file_io(io.clone())
        .metadata(metadata)
        .metadata_location(metadata_location)
        .build()
        .unwrap();
    (io, table)
}

/// A table assembled from files on disk plans a scan over its current snapshot.
#[tokio::test]
async fn gate02__scan_plans_files_from_disk() {
    let dir = tempfile::TempDir::new().unwrap();
    let (io, table) = build_gate_table(&dir).await;

    assert_eq!(table.metadata().current_snapshot().unwrap().snapshot_id(), 42);
    let manifest_list_path = table
        .metadata()
        .current_snapshot()
        .unwrap()
        .manifest_list()
        .to_string();
    // The manifest list the snapshot points at is readable through FileIO.
    let bytes = io.new_input(&manifest_list_path).unwrap().read().await.unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2, |_id| Ok(None)).unwrap();
    assert_eq!(list.entries().len(), 1);
    // Sequence numbers were assigned by the manifest list writer.
    assert_eq!(list.entries()[0].sequence_number, 3);

    // And the manifest it names loads with the inherited sequence number.
    let manifest = list.entries()[0].load_manifest(&io).await.unwrap();
    assert_eq!(manifest.entries().len(), 1);
    assert_eq!(manifest.entries()[0].sequence_number(), Some(3));

    let scan = table.scan().build().expect("build scan");
    assert_eq!(scan.snapshot().unwrap().snapshot_id(), 42);
    assert!(scan.plan_files().await.is_ok(), "plan_files must succeed");

    // Planning really does read the manifest list: point the snapshot at a file
    // that does not exist and planning fails.
    let broken_json = GATE_METADATA
        .replace("file:///tmp/gate/table", &dir.path().to_str().unwrap().to_string())
        .replace("file:///tmp/gate/ml.avro", "/nonexistent/gate/ml.avro");
    let broken: TableMetadata = serde_json::from_str(&broken_json).unwrap();
    let broken_table = iceberg::table::Table::builder()
        .identifier(iceberg::TableIdent::from_strs(["gate", "broken"]).unwrap())
        .file_io(io.clone())
        .metadata(broken)
        .build()
        .unwrap();
    assert!(broken_table.scan().build().unwrap().plan_files().await.is_err());
}

/// Column projection and predicate binding are validated against the snapshot's
/// schema when the scan is built.
#[tokio::test]
async fn gate02__scan_projects_and_filters() {
    use iceberg::expr::Reference;

    let dir = tempfile::TempDir::new().unwrap();
    let (_io, table) = build_gate_table(&dir).await;

    let scan = table
        .scan()
        .select(["id"])
        .with_filter(Reference::new("id").greater_than(Datum::long(5)))
        .build()
        .expect("build filtered scan");
    assert_eq!(scan.column_names(), Some(["id".to_string()].as_slice()));
    assert!(scan.plan_files().await.is_ok());

    // A column that is not in the schema is rejected.
    assert!(table.scan().select(["nope"]).build().is_err());
    // A predicate over a column that is not in the schema is rejected.
    assert!(
        table
            .scan()
            .with_filter(Reference::new("nope").equal_to(Datum::long(1)))
            .build()
            .is_err()
    );
    // A snapshot that does not exist is rejected.
    assert!(table.scan().snapshot_id(999).build().is_err());
}

// ---------------------------------------------------------------------------
// gate03 — milestone 03: TableMetadataBuilder produces committable metadata.
// ---------------------------------------------------------------------------

/// A table metadata document can be built from scratch and carries the schema,
/// spec, sort order and location it was given.
#[test]
fn gate03__builds_new_table_metadata() {
    let schema = gate_schema();
    let spec = UnboundPartitionSpec::builder()
        .with_spec_id(0)
        .add_partition_field(2, "ts_day".to_string(), Transform::Day)
        .unwrap()
        .build();
    let uuid = uuid::Uuid::parse_str("9c12d441-03fe-4693-9a96-a0705ddf69c1").unwrap();

    let result = TableMetadataBuilder::new(
        schema,
        spec,
        SortOrder::unsorted_order(),
        "file:///tmp/gate/new".to_string(),
        FormatVersion::V2,
        HashMap::new(),
    )
    .expect("new table metadata builder")
    .assign_uuid(uuid)
    .build()
    .expect("build table metadata");

    let md = result.metadata;
    assert_eq!(md.format_version(), FormatVersion::V2);
    assert_eq!(md.uuid(), uuid);
    assert_eq!(md.location(), "file:///tmp/gate/new");
    assert_eq!(md.current_schema().as_struct().fields().len(), 2);
    assert_eq!(md.default_partition_spec().fields().len(), 1);
    assert_eq!(md.default_partition_spec().fields()[0].transform, Transform::Day);
    assert!(md.current_snapshot().is_none());
    assert!(!result.changes.is_empty(), "the build must record its updates");

    // The result is serializable metadata that reads back identically.
    let json = serde_json::to_value(&md).unwrap();
    assert_eq!(json["format-version"], 2);
    let back: TableMetadata = serde_json::from_value(json).unwrap();
    assert_eq!(back, md);
}

/// An existing metadata document can gain a schema, a partition spec and a
/// snapshot, each becoming the current/default one.
#[test]
fn gate03__adds_schema_spec_and_snapshot() {
    let md: TableMetadata = serde_json::from_str(GATE_METADATA).unwrap();
    let new_schema = Schema::builder()
        .with_schema_id(1)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "ts", Type::Primitive(PrimitiveType::Timestamptz)).into(),
            NestedField::optional(3, "note", Type::Primitive(PrimitiveType::String)).into(),
        ])
        .build()
        .unwrap();
    let new_spec = UnboundPartitionSpec::builder()
        .with_spec_id(1)
        .add_partition_field(1, "id_bucket".to_string(), Transform::Bucket(8))
        .unwrap()
        .build();
    let snapshot = Snapshot::builder()
        .with_snapshot_id(43)
        .with_parent_snapshot_id(Some(42))
        .with_sequence_number(4)
        .with_timestamp_ms(1700000000000)
        .with_manifest_list("file:///tmp/gate/ml-43.avro")
        .with_schema_id(1)
        .with_summary(Summary {
            operation: Operation::Overwrite,
            additional_properties: HashMap::new(),
        })
        .build();

    let result = TableMetadataBuilder::new_from_metadata(md, None)
        .add_schema(new_schema)
        .set_current_schema(TableMetadataBuilder::LAST_ADDED)
        .unwrap()
        .add_partition_spec(new_spec)
        .unwrap()
        .set_default_partition_spec(TableMetadataBuilder::LAST_ADDED)
        .unwrap()
        .add_snapshot(snapshot)
        .unwrap()
        .set_ref(MAIN_BRANCH, SnapshotReference::new(43, SnapshotRetention::Branch {
            min_snapshots_to_keep: None,
            max_snapshot_age_ms: None,
            max_ref_age_ms: None,
        }))
        .unwrap()
        .build()
        .expect("build updated table metadata");

    let md = result.metadata;
    assert_eq!(md.current_schema().schema_id(), 1);
    assert_eq!(md.current_schema().as_struct().fields().len(), 3);
    assert_eq!(md.default_partition_spec().spec_id(), 1);
    assert_eq!(
        md.default_partition_spec().fields()[0].transform,
        Transform::Bucket(8)
    );
    assert!(md.snapshot_by_id(43).is_some());
    assert_eq!(md.current_snapshot().unwrap().snapshot_id(), 43);
    assert_eq!(md.last_sequence_number(), 4);
    assert!(!result.changes.is_empty());
}

/// Format version 1 metadata can be upgraded to version 2.
#[test]
fn gate03__upgrades_format_version() {
    let v1 = r#"{
      "format-version": 1,
      "table-uuid": "d20125c8-7284-442c-9aea-15fee620737c",
      "location": "file:///tmp/gate/v1",
      "last-updated-ms": 1602638573874,
      "last-column-id": 1,
      "schema": {"type": "struct", "schema-id": 0, "fields": [
        {"id": 1, "name": "x", "required": true, "type": "long"}
      ]},
      "partition-spec": [],
      "properties": {},
      "current-snapshot-id": -1,
      "snapshots": []
    }"#;
    let md: TableMetadata = serde_json::from_str(v1).unwrap();
    assert_eq!(md.format_version(), FormatVersion::V1);

    let upgraded = TableMetadataBuilder::new_from_metadata(md, None)
        .upgrade_format_version(FormatVersion::V2)
        .expect("upgrade to v2")
        .build()
        .expect("build upgraded metadata")
        .metadata;
    assert_eq!(upgraded.format_version(), FormatVersion::V2);
    assert_eq!(upgraded.current_schema().field_by_name("x").unwrap().id, 1);

    // Downgrading is not allowed.
    assert!(
        TableMetadataBuilder::new_from_metadata(upgraded, None)
            .upgrade_format_version(FormatVersion::V1)
            .is_err()
    );
}

// ---------------------------------------------------------------------------
// gate04 — milestone 04: a delete file round-trips through a delete manifest.
// ---------------------------------------------------------------------------

/// A position delete file and an equality delete file are written to a v2
/// delete manifest and read back with their content kinds intact.
#[tokio::test]
async fn gate04__delete_files_round_trip_through_a_delete_manifest() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let schema: SchemaRef = Arc::new(gate_schema());
    let spec = PartitionSpec::builder(schema.clone())
        .with_spec_id(0)
        .build()
        .unwrap();
    let path = format!("{}/gate-deletes.avro", dir.path().to_str().unwrap());

    let position = DataFileBuilder::default()
        .content(DataContentType::PositionDeletes)
        .file_path("/tmp/gate/pos-0.parquet".to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .record_count(2)
        .file_size_in_bytes(128)
        .partition_spec_id(0)
        .referenced_data_file(Some("/tmp/gate/data-0.parquet".to_string()))
        .build()
        .unwrap();
    let equality = DataFileBuilder::default()
        .content(DataContentType::EqualityDeletes)
        .file_path("/tmp/gate/eq-0.parquet".to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .record_count(1)
        .file_size_in_bytes(64)
        .partition_spec_id(0)
        .equality_ids(vec![1])
        .build()
        .unwrap();

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(43),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer.add_file(position, 4).expect("add position delete");
    writer.add_file(equality, 4).expect("add equality delete");
    let manifest_file = writer.write_manifest_file().await.expect("write manifest");

    assert_eq!(manifest_file.content, ManifestContentType::Deletes);
    assert_eq!(manifest_file.added_files_count, Some(2));
    assert_eq!(manifest_file.min_sequence_number, 4);

    let bytes = std::fs::read(&path).unwrap();
    let (entries, metadata) = Manifest::parse_avro(&bytes).unwrap().into_parts();
    assert_eq!(metadata.content, ManifestContentType::Deletes);
    assert_eq!(entries.len(), 2);
    let mut kinds: Vec<i32> = entries
        .iter()
        .map(|e| e.data_file().content_type() as i32)
        .collect();
    kinds.sort();
    assert_eq!(kinds, vec![1, 2]);
    let eq = entries
        .iter()
        .find(|e| e.data_file().content_type() == DataContentType::EqualityDeletes)
        .unwrap();
    assert_eq!(eq.data_file().equality_ids(), &[1]);
    let pos = entries
        .iter()
        .find(|e| e.data_file().content_type() == DataContentType::PositionDeletes)
        .unwrap();
    assert_eq!(
        pos.data_file().referenced_data_file().as_deref(),
        Some("/tmp/gate/data-0.parquet")
    );
}

/// A delete manifest is tracked by the manifest list as a `deletes` manifest,
/// separately from the data manifests.
#[tokio::test]
async fn gate04__delete_manifest_is_tracked_by_the_manifest_list() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let root = dir.path().to_str().unwrap().to_string();
    let schema: SchemaRef = Arc::new(gate_schema());
    let spec = PartitionSpec::builder(schema.clone())
        .with_spec_id(0)
        .build()
        .unwrap();

    let data_path = format!("{root}/gate-md.avro");
    let mut dw = ManifestWriterBuilder::new(
        io.new_output(&data_path).unwrap(),
        Some(44),
        vec![],
        schema.clone(),
        spec.clone(),
    )
    .build_v2_data();
    dw.add_file(gate_data_file("/tmp/gate/d.parquet"), UNASSIGNED_SEQUENCE_NUMBER)
        .unwrap();
    let data_manifest = dw.write_manifest_file().await.unwrap();

    let del_path = format!("{root}/gate-xd.avro");
    let mut xw = ManifestWriterBuilder::new(
        io.new_output(&del_path).unwrap(),
        Some(44),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    xw.add_file(
        DataFileBuilder::default()
            .content(DataContentType::PositionDeletes)
            .file_path("/tmp/gate/pos-1.parquet".to_string())
            .file_format(DataFileFormat::Parquet)
            .partition(Struct::empty())
            .record_count(1)
            .file_size_in_bytes(64)
            .partition_spec_id(0)
            .build()
            .unwrap(),
        UNASSIGNED_SEQUENCE_NUMBER,
    )
    .unwrap();
    let delete_manifest = xw.write_manifest_file().await.unwrap();

    let list_path = format!("{root}/gate-ml-44.avro");
    let mut lw = ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 44, Some(42), 5);
    lw.add_manifests(vec![data_manifest, delete_manifest].into_iter())
        .unwrap();
    lw.close().await.unwrap();

    let bytes = std::fs::read(&list_path).unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2, |_id| Ok(None)).unwrap();
    assert_eq!(list.entries().len(), 2);
    let deletes: Vec<&ManifestFile> = list
        .entries()
        .iter()
        .filter(|e| e.content == ManifestContentType::Deletes)
        .collect();
    assert_eq!(deletes.len(), 1);
    assert_eq!(deletes[0].sequence_number, 5);
    // The delete manifest loads back with the inherited sequence number.
    let loaded = deletes[0].load_manifest(&io).await.unwrap();
    assert_eq!(loaded.entries().len(), 1);
    assert_eq!(loaded.entries()[0].sequence_number(), Some(5));
    assert_eq!(
        loaded.entries()[0].content_type(),
        DataContentType::PositionDeletes
    );
}

/// A Puffin file carrying a deletion-vector blob is written and read back
/// through the public Puffin API.
#[tokio::test]
async fn gate04__puffin_blob_round_trip() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let path = format!("{}/gate.puffin", dir.path().to_str().unwrap());
    let output = io.new_output(&path).unwrap();

    let blob = iceberg::puffin::Blob::builder()
        .r#type(iceberg::puffin::DELETION_VECTOR_V1.to_string())
        .fields(vec![])
        .snapshot_id(43)
        .sequence_number(4)
        .data(vec![0xD1, 0xD3, 0x39, 0x64, 0x00, 0x01])
        .properties(HashMap::new())
        .build();

    let mut writer = iceberg::puffin::PuffinWriter::new(&output, HashMap::new(), false)
        .await
        .expect("create puffin writer");
    writer
        .add(blob, iceberg::puffin::CompressionCodec::None)
        .await
        .expect("add blob");
    writer.close().await.expect("close puffin writer");

    let bytes = std::fs::read(&path).unwrap();
    assert_eq!(&bytes[..4], b"PFA1");
    assert_eq!(&bytes[bytes.len() - 4..], b"PFA1");

    let reader = iceberg::puffin::PuffinReader::new(io.new_input(&path).unwrap());
    let metadata = reader.file_metadata().await.expect("read footer").clone();
    assert_eq!(metadata.blobs().len(), 1);
    let bm = &metadata.blobs()[0];
    assert_eq!(bm.blob_type(), "deletion-vector-v1");
    assert_eq!(bm.snapshot_id(), 43);
    assert_eq!(bm.sequence_number(), 4);
    assert_eq!(bm.offset(), 4);
    let read_back = reader.blob(bm).await.expect("read blob");
    assert_eq!(read_back.data(), &[0xD1, 0xD3, 0x39, 0x64, 0x00, 0x01]);
}
