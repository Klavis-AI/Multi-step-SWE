// Verifier-owned gate for milestone 01 (table format core).
//
// The gate is one end-to-end path through the milestone's headline capability,
// at proof-of-life depth, scored as the single group `gate-endtoend`. Harder
// variants are scored by tests/conformance.rs and still run when this fails.

use std::collections::HashMap;
use std::sync::Arc;

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

/// Step 01 gate: table metadata is readable and exposes the current schema,
/// the default partition spec and the current snapshot.
#[test]
fn gate01__table_metadata_is_readable() {
    let md: TableMetadata = serde_json::from_str(GATE_METADATA).expect("parse table metadata");
    assert_eq!(md.format_version(), FormatVersion::V2);
    assert_eq!(md.current_schema().as_struct().fields().len(), 2);
    assert_eq!(md.current_schema().field_by_name("id").unwrap().id, 1);
    let spec = md.default_partition_spec().expect("default partition spec");
    assert_eq!(spec.fields[0].transform, Transform::Day);
    assert_eq!(spec.fields[0].field_id, 1000);
    let snapshot = md.current_snapshot().expect("current snapshot");
    assert_eq!(snapshot.snapshot_id(), 42);
    assert_eq!(snapshot.sequence_number(), 3);
}

/// Step 01 gate: a manifest written through the public writer can be read back
/// through the public reader, and is a real Avro object container file.
#[tokio::test(flavor = "multi_thread")]
async fn gate01__manifest_write_read_roundtrip() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = iceberg::io::FileIOBuilder::new_fs_io().build().unwrap();
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
        .partition_spec(PartitionSpec {
            spec_id: 0,
            fields: vec![],
        })
        .content(ManifestContentType::Data)
        .format_version(FormatVersion::V2)
        .build();
    let data_file = DataFile::builder()
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
        .build();
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
    assert_eq!(entry.added_data_files_count, Some(1));

    let bytes = std::fs::read(&path).unwrap();
    assert_eq!(&bytes[..4], b"Obj\x01");
    let parsed = Manifest::parse_avro(&bytes).expect("parse manifest");
    assert_eq!(parsed.entries().len(), 1);
    assert_eq!(parsed.entries()[0].file_path(), "/tmp/gate/data-0.parquet");
}

/// Step 01 gate: a manifest list written through the public writer round trips.
#[tokio::test(flavor = "multi_thread")]
async fn gate01__manifest_list_roundtrip() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = iceberg::io::FileIOBuilder::new_fs_io().build().unwrap();
    let path = format!("{}/gate-ml.avro", dir.path().to_str().unwrap());
    let out = io.new_output(&path).unwrap();
    let mut writer = ManifestListWriter::v2(out, 42, 0, 3);
    writer
        .add_manifest_entries(
            vec![ManifestListEntry {
                manifest_path: "/tmp/gate/m0.avro".to_string(),
                manifest_length: 512,
                partition_spec_id: 0,
                content: ManifestContentType::Data,
                sequence_number: 3,
                min_sequence_number: 3,
                added_snapshot_id: 42,
                added_data_files_count: Some(1),
                existing_data_files_count: Some(0),
                deleted_data_files_count: Some(0),
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
