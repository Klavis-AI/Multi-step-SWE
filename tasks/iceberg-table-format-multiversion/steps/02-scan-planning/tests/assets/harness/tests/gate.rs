// Verifier-owned gate for milestone 02 (scan planning).
//
// The gate is one end-to-end path through the milestone's headline
// capability, at proof-of-life depth, plus one path through every milestone it
// inherits, scored as the single group `gate-endtoend`. Harder variants are
// scored by tests/conformance.rs and still run when this fails.

use std::collections::HashMap;
use std::sync::Arc;

use iceberg::expr::{Bind, BoundPredicate, Reference};
use iceberg::io::FileIOBuilder;
use iceberg::spec::*;
use iceberg::table::StaticTable;
use iceberg::transform::create_transform_function;
use iceberg::TableIdent;

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

/// Step 01 gate: table metadata is readable and exposes the current schema,
/// the default partition spec and the current snapshot.
#[test]
fn gate01__table_metadata_is_readable() {
    let md: TableMetadata = serde_json::from_str(GATE_METADATA).expect("parse table metadata");
    assert_eq!(md.format_version(), FormatVersion::V2);
    assert_eq!(md.current_schema().as_struct().fields().len(), 2);
    assert_eq!(md.current_schema().field_by_name("id").unwrap().id, 1);
    let spec = md.default_partition_spec().expect("default partition spec");
    assert_eq!(spec.fields()[0].transform, Transform::Day);
    assert_eq!(spec.fields()[0].field_id, 1000);
    let snapshot = md.current_snapshot().expect("current snapshot");
    assert_eq!(snapshot.snapshot_id(), 42);
    assert_eq!(snapshot.sequence_number(), 3);
}

/// Step 01 gate: a manifest written through the public writer can be read back
/// through the public reader, and is a real Avro object container file.
#[tokio::test]
async fn gate01__manifest_write_read_roundtrip() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let schema = Arc::new(
        Schema::builder()
            .with_schema_id(0)
            .with_fields(vec![
                NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            ])
            .build()
            .unwrap(),
    );
    let metadata = ManifestMetadata::builder()
        .schema(schema.as_ref().clone())
        .schema_id(0)
        .partition_spec(
            PartitionSpec::builder(schema.as_ref())
                .with_spec_id(0)
                .build()
                .unwrap(),
        )
        .content(ManifestContentType::Data)
        .format_version(FormatVersion::V2)
        .build();
    let data_file = DataFileBuilder::default()
        .content(DataContentType::Data)
        .file_path("/tmp/gate/data-0.parquet".to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .record_count(3)
        .file_size_in_bytes(512)
        .column_sizes(HashMap::new())
        .value_counts(HashMap::new())
        .null_value_counts(HashMap::new())
        .nan_value_counts(HashMap::new())
        .lower_bounds(HashMap::new())
        .upper_bounds(HashMap::new())
        .key_metadata(vec![])
        .split_offsets(vec![])
        .equality_ids(vec![])
        .build()
        .unwrap();
    let manifest = Manifest::new(
        metadata,
        vec![ManifestEntry::builder()
            .status(ManifestStatus::Added)
            .data_file(data_file)
            .build()],
    );

    let path = format!("{}/gate-m0.avro", dir.path().to_str().unwrap());
    let out = io.new_output(&path).unwrap();
    let entry = ManifestWriter::new(out, 42, vec![])
        .write(manifest)
        .await
        .expect("write manifest");
    assert_eq!(entry.added_files_count, Some(1));

    let bytes = std::fs::read(&path).unwrap();
    assert_eq!(&bytes[..4], b"Obj\x01");
    let parsed = Manifest::parse_avro(&bytes).expect("parse manifest");
    assert_eq!(parsed.entries().len(), 1);
    assert_eq!(parsed.entries()[0].file_path(), "/tmp/gate/data-0.parquet");
}

/// Step 01 gate: a manifest list written through the public writer round trips.
#[tokio::test]
async fn gate01__manifest_list_roundtrip() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let path = format!("{}/gate-ml.avro", dir.path().to_str().unwrap());
    let out = io.new_output(&path).unwrap();
    let mut writer = ManifestListWriter::v2(out, 42, 0, 3);
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
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2, |_id| Ok(None))
        .expect("parse manifest list");
    assert_eq!(list.entries().len(), 1);
    assert_eq!(list.entries()[0].manifest_path, "/tmp/gate/m0.avro");
    assert_eq!(list.entries()[0].added_snapshot_id, 42);
}

/// Step 01 gate: partition transforms are parseable and typed per the spec.
#[test]
fn gate01__partition_transforms() {
    assert_eq!("bucket[16]".parse::<Transform>().unwrap(), Transform::Bucket(16));
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

/// A v1 table metadata document, per spec.md "Table Metadata Fields": one
/// `schema`, a bare `partition-spec` field list, and `current-snapshot-id: -1`
/// for "no current snapshot".
const GATE_V1_METADATA: &str = r#"{
  "format-version": 1,
  "table-uuid": "d20125c8-7284-442c-9aea-15fee620737c",
  "location": "file:///tmp/gate/v1",
  "last-updated-ms": 1602638573874,
  "last-column-id": 2,
  "schema": {
    "type": "struct",
    "schema-id": 0,
    "fields": [
      {"id": 1, "name": "x", "required": true, "type": "long"},
      {"id": 2, "name": "y", "required": false, "type": "string"}
    ]
  },
  "partition-spec": [{"name": "x", "transform": "identity", "source-id": 1, "field-id": 1000}],
  "sort-orders": [
    {"order-id": 0, "fields": []},
    {"order-id": 1, "fields": [
      {"transform": "identity", "source-id": 1, "direction": "asc", "null-order": "nulls-first"}]}
  ],
  "default-sort-order-id": 1,
  "properties": {},
  "current-snapshot-id": -1,
  "snapshots": []
}"#;

/// Step 01 gate: the milestone-1 areas the scan path does not otherwise touch
/// — **v1** table metadata, **sort orders**, dotted nested schema names,
/// identifier-field-id validation, and Appendix D's **JSON** single-value form
/// — are still required and still work.
#[test]
fn gate01__v1_metadata_sort_orders_and_json_values() {
    let md: TableMetadata = serde_json::from_str(GATE_V1_METADATA).expect("parse v1 metadata");
    assert_eq!(md.format_version(), FormatVersion::V1);
    assert_eq!(md.current_schema().as_struct().fields().len(), 2);
    assert_eq!(md.current_schema().field_by_name("x").unwrap().id, 1);
    let spec = md.default_partition_spec().expect("default partition spec");
    assert_eq!(spec.fields()[0].field_id, 1000);
    assert_eq!(spec.fields()[0].transform, Transform::Identity);
    // "Table Metadata Fields": current-snapshot-id -1 means no snapshot.
    assert!(md.current_snapshot().is_none());

    // "Sorting": order id 0 is the unsorted order; the default resolves.
    let order = md.default_sort_order().expect("default sort order");
    assert_eq!(order.order_id, 1);
    assert_eq!(order.fields[0].source_id, 1);
    assert_eq!(order.fields[0].direction, SortDirection::Ascending);
    assert_eq!(order.fields[0].null_order, NullOrder::First);
    assert!(!order.is_unsorted());
    assert!(md.sort_order_by_id(0).expect("unsorted order").is_unsorted());

    // "Schemas and Data Types": nested field names are dotted paths.
    let nested = Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![NestedField::required(
            1,
            "location",
            Type::Struct(StructType::new(vec![NestedField::required(
                2,
                "lat",
                Type::Primitive(PrimitiveType::Long),
            )
            .into()])),
        )
        .into()])
        .build()
        .expect("build nested schema");
    assert_eq!(nested.field_id_by_name("location.lat"), Some(2));
    assert_eq!(nested.name_by_field_id(2), Some("location.lat"));
    // "Identifier Field IDs": an identifier field must be required.
    assert!(Schema::builder()
        .with_schema_id(0)
        .with_identifier_field_ids([1])
        .with_fields(vec![
            NestedField::optional(1, "opt", Type::Primitive(PrimitiveType::Long)).into(),
        ])
        .build()
        .is_err());

    // Appendix D "JSON single-value serialization".
    let ty = Type::Primitive(PrimitiveType::Date);
    let lit = Literal::try_from_json(serde_json::json!("2017-11-16"), &ty)
        .expect("try_from_json")
        .expect("non-null literal");
    assert_eq!(
        lit.try_into_json(&ty).expect("try_into_json"),
        serde_json::json!("2017-11-16")
    );
    assert!(Literal::try_from_json(serde_json::Value::Null, &ty)
        .expect("try_from_json(null)")
        .is_none());
}

// ---------------------------------------------------------------------------
// Step 02 gate: the scan plan.
// ---------------------------------------------------------------------------

const GATE_SNAPSHOT_ID: i64 = 7000000000000000001;

/// Writes a minimal but complete table on the local filesystem: one manifest,
/// one manifest list, one v2 metadata document. Returns the metadata path.
async fn write_gate_table(dir: &tempfile::TempDir) -> String {
    let root = dir.path().to_str().unwrap().to_string();
    let io = FileIOBuilder::new_fs_io().build().unwrap();

    let schema = Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "name", Type::Primitive(PrimitiveType::String)).into(),
        ])
        .build()
        .unwrap();
    let spec = PartitionSpec::builder(&schema)
        .with_spec_id(0)
        .add_partition_field("id", "id_part", Transform::Identity)
        .unwrap()
        .build()
        .unwrap();

    let data_file = DataFileBuilder::default()
        .content(DataContentType::Data)
        .file_path(format!("{root}/data-0.parquet"))
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::from_iter([Some(Literal::long(4))]))
        .record_count(3)
        .file_size_in_bytes(512)
        .null_value_counts(HashMap::from([(1, 0u64)]))
        .lower_bounds(HashMap::from([(1, Datum::long(4))]))
        .upper_bounds(HashMap::from([(1, Datum::long(9))]))
        .build()
        .unwrap();

    let manifest = Manifest::new(
        ManifestMetadata::builder()
            .schema(schema.clone())
            .schema_id(0)
            .partition_spec(spec)
            .content(ManifestContentType::Data)
            .format_version(FormatVersion::V2)
            .build(),
        vec![ManifestEntry::builder()
            .status(ManifestStatus::Added)
            .data_file(data_file)
            .build()],
    );

    let manifest_path = format!("{root}/m0.avro");
    let manifest_file = ManifestWriter::new(
        io.new_output(&manifest_path).unwrap(),
        GATE_SNAPSHOT_ID,
        vec![],
    )
    .write(manifest)
    .await
    .expect("write manifest");

    let manifest_list_path = format!("{root}/snap-0.avro");
    let mut writer = ManifestListWriter::v2(
        io.new_output(&manifest_list_path).unwrap(),
        GATE_SNAPSHOT_ID,
        -1,
        1,
    );
    writer
        .add_manifests(vec![manifest_file].into_iter())
        .unwrap();
    writer.close().await.expect("close manifest list");

    let metadata = format!(
        r#"{{
  "format-version": 2,
  "table-uuid": "b1f0c8a2-9d5e-4c3b-9a71-4c2f9f6b1d20",
  "location": "{root}",
  "last-sequence-number": 1,
  "last-updated-ms": 1602638573590,
  "last-column-id": 2,
  "current-schema-id": 0,
  "schemas": [{{"type": "struct", "schema-id": 0, "fields": [
    {{"id": 1, "name": "id", "required": true, "type": "long"}},
    {{"id": 2, "name": "name", "required": false, "type": "string"}}
  ]}}],
  "default-spec-id": 0,
  "partition-specs": [{{"spec-id": 0, "fields": [
    {{"name": "id_part", "transform": "identity", "source-id": 1, "field-id": 1000}}
  ]}}],
  "last-partition-id": 1000,
  "default-sort-order-id": 0,
  "sort-orders": [{{"order-id": 0, "fields": []}}],
  "properties": {{}},
  "current-snapshot-id": {GATE_SNAPSHOT_ID},
  "snapshots": [
    {{"snapshot-id": {GATE_SNAPSHOT_ID}, "timestamp-ms": 1515100955770, "sequence-number": 1,
     "summary": {{"operation": "append"}}, "manifest-list": "{manifest_list_path}",
     "schema-id": 0}}
  ],
  "snapshot-log": [{{"snapshot-id": {GATE_SNAPSHOT_ID}, "timestamp-ms": 1515100955770}}],
  "metadata-log": []
}}"#
    );
    let metadata_path = format!("{root}/metadata.json");
    std::fs::write(&metadata_path, metadata).unwrap();
    metadata_path
}

async fn load_gate_table(dir: &tempfile::TempDir) -> StaticTable {
    let metadata_path = write_gate_table(dir).await;
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    StaticTable::from_metadata_file(
        &metadata_path,
        TableIdent::from_strs(["gate", "t"]).unwrap(),
        io,
    )
    .await
    .expect("load static table")
}

/// Step 02 gate: a table on the local filesystem can be scanned. The scan
/// resolves the current snapshot, validates the projection and plans files by
/// reading the snapshot's manifest list and manifests through `FileIO`.
#[tokio::test]
async fn gate02__table_scan_plans_files() {
    let dir = tempfile::TempDir::new().unwrap();
    let table = load_gate_table(&dir).await;

    let scan = table.scan().select(["id"]).build().expect("build scan");
    assert_eq!(scan.snapshot().snapshot_id(), GATE_SNAPSHOT_ID);
    assert_eq!(scan.column_names(), ["id".to_string()]);
    assert!(scan.plan_files().await.is_ok());

    // Projection is validated against the snapshot schema.
    assert!(table.scan().select(["not_a_column"]).build().is_err());
}

/// Step 02 gate: the scan's manifest list and manifest are readable through
/// `FileIO`, carrying the partition field summaries and column bounds that
/// scan planning prunes on.
#[tokio::test]
async fn gate02__scan_inputs_are_readable() {
    let dir = tempfile::TempDir::new().unwrap();
    let table = load_gate_table(&dir).await;
    // `metadata()` yields an owned `TableMetadataRef`, so the snapshot outlives
    // the `StaticTable` that `into_table` consumes.
    let metadata = table.metadata();
    let snapshot = metadata.current_snapshot().unwrap().clone();
    let inner = table.into_table();

    let list = snapshot
        .load_manifest_list(inner.file_io(), &metadata)
        .await
        .expect("load manifest list");
    assert_eq!(list.entries().len(), 1);
    assert_eq!(list.entries()[0].content, ManifestContentType::Data);
    assert_eq!(list.entries()[0].added_files_count, Some(1));
    assert_eq!(list.entries()[0].partitions.len(), 1);
    assert_eq!(list.entries()[0].partitions[0].lower_bound, Some(Datum::long(4)));

    let manifest = list.entries()[0]
        .load_manifest(inner.file_io())
        .await
        .expect("load manifest");
    assert_eq!(manifest.entries().len(), 1);
    assert!(manifest.entries()[0].is_alive());
    assert_eq!(
        manifest.entries()[0].data_file().lower_bounds().get(&1),
        Some(&Datum::long(4))
    );
}

/// Step 02 gate: a row filter binds to the table schema and drives a scan, and
/// a partition transform evaluates a literal (spec Appendix B / "Bucket
/// Transform Details").
#[tokio::test]
async fn gate02__filter_binds_and_transform_evaluates() {
    let dir = tempfile::TempDir::new().unwrap();
    let table = load_gate_table(&dir).await;

    let schema = table.metadata().current_schema().clone();
    let bound = Reference::new("id")
        .greater_than(Datum::long(3))
        .bind(schema, true)
        .expect("bind id > 3");
    assert!(matches!(bound, BoundPredicate::Binary(_)));

    let scan = table
        .scan()
        .with_filter(Reference::new("id").greater_than(Datum::long(3)))
        .build()
        .expect("build filtered scan");
    assert!(scan.plan_files().await.is_ok());
    assert!(table
        .scan()
        .with_filter(Reference::new("nope").greater_than(Datum::long(3)))
        .build()
        .is_err());

    // Appendix B: murmur3_x86_32("iceberg") == 1210000089.
    let f = create_transform_function(&Transform::Bucket(i32::MAX as u32)).unwrap();
    let out = f
        .transform_literal(&Datum::string("iceberg"))
        .unwrap()
        .unwrap();
    assert_eq!(
        out,
        Datum::int((1210000089i32 & i32::MAX) % (i32::MAX as u32 as i32))
    );
}
