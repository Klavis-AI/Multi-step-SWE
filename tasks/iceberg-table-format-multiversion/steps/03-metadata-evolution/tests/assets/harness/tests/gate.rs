// Verifier-owned gate for milestone 03 (metadata update algebra).
//
// The gate is this step's headline capability at proof-of-life depth, plus one
// end-to-end path through every milestone it inherits, scored as the single
// group `gate-endtoend`. Harder variants are scored by tests/conformance.rs and
// still run when this fails.
//
//   gate01__ : milestone 01 — table format core (metadata, manifests).
//   gate02__ : milestone 02 — scan planning over a real FileIO-backed table.
//   gate03__ : milestone 03 — TableMetadataBuilder produces committable
//              metadata (this step's headline capability).

use std::collections::HashMap;
use std::sync::Arc;

use iceberg::expr::Reference;
use iceberg::io::FileIO;
use iceberg::spec::*;
use iceberg::table::Table;
use iceberg::{TableCreation, TableIdent, TableRequirement, TableUpdate};

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

/// Drives one async gate case to completion on a **multi-thread** tokio
/// runtime, which is the runtime `TEST_INTERFACE.md` section 3 promises an
/// `async fn` must be executable on. It is written as an explicit runtime
/// rather than `#[tokio::test(flavor = "multi_thread")]` so that the attribute
/// on every gate case stays the plain `#[test]` the packager counts.
fn block_on<F: std::future::Future>(fut: F) -> F::Output {
    tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .expect("build a multi-thread tokio runtime")
        .block_on(fut)
}

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

fn gate_data_file(path: &str, lower: i64, upper: i64) -> DataFile {
    DataFileBuilder::default()
        .content(DataContentType::Data)
        .file_path(path.to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .record_count(3)
        .file_size_in_bytes(512)
        .lower_bounds(HashMap::from([(1, Datum::long(lower))]))
        .upper_bounds(HashMap::from([(1, Datum::long(upper))]))
        .key_metadata(None)
        .build()
        .expect("build data file")
}

// ---------------------------------------------------------------------------
// gate01 — milestone 01: table metadata and the manifest layer.
// ---------------------------------------------------------------------------

/// Step 01 gate: table metadata is readable and exposes the current schema,
/// the default partition spec and the current snapshot.
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
    // PROFILE P1: only format versions 1 and 2 are in scope, and anything else
    // must be rejected when parsing table metadata.
    assert!(
        serde_json::from_str::<TableMetadata>(
            &GATE_METADATA.replace("\"format-version\": 2", "\"format-version\": 99")
        )
        .is_err(),
        "an unsupported format-version must be rejected"
    );
    // Section 6 / PROFILE P9: Appendix C admits both decimal spellings and both
    // must be accepted.
    for spelling in ["\"decimal(9,2)\"", "\"decimal(9, 2)\""] {
        let ty: Type = serde_json::from_str(spelling)
            .unwrap_or_else(|e| panic!("{spelling} must parse: {e}"));
        assert_eq!(
            ty,
            Type::Primitive(PrimitiveType::Decimal {
                precision: 9,
                scale: 2
            })
        );
    }
}

/// Step 01 gate: a manifest written through the public writer can be read back
/// through the public reader, and is a real Avro object container file.
#[test]
fn gate01__manifest_write_read_roundtrip() {
    block_on(gate01__manifest_write_read_roundtrip_impl());
}

async fn gate01__manifest_write_read_roundtrip_impl() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = iceberg::io::FileIOBuilder::new_fs_io().build().unwrap();
    let schema = Arc::new(gate_schema());
    let metadata = ManifestMetadata::builder()
        .schema(schema.clone())
        .schema_id(0)
        .partition_spec(PartitionSpec::unpartition_spec())
        .content(ManifestContentType::Data)
        .format_version(FormatVersion::V2)
        .build();
    let manifest = Manifest::new(
        metadata,
        vec![ManifestEntry::builder()
            .status(ManifestStatus::Added)
            .data_file(gate_data_file("/tmp/gate/data-0.parquet", 1, 10))
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
    assert_eq!(parsed.entries()[0].content_type(), DataContentType::Data);
    assert!(parsed.entries()[0].is_alive());
}

/// Step 01 gate: a manifest list written through the public writer round trips.
#[test]
fn gate01__manifest_list_roundtrip() {
    block_on(gate01__manifest_list_roundtrip_impl());
}

async fn gate01__manifest_list_roundtrip_impl() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = iceberg::io::FileIOBuilder::new_fs_io().build().unwrap();
    let path = format!("{}/gate-ml.avro", dir.path().to_str().unwrap());
    let out = io.new_output(&path).unwrap();
    let mut writer = ManifestListWriter::v2(out, 42, None, 3);
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

    // A version-1 manifest list round trips through the same pair, with the
    // parent snapshot id now optional (section 5).
    let v1_path = format!("{}/gate-ml-v1.avro", dir.path().to_str().unwrap());
    let mut v1_writer = ManifestListWriter::v1(io.new_output(&v1_path).unwrap(), 43, Some(42));
    v1_writer
        .add_manifests(
            vec![ManifestFile {
                manifest_path: "/tmp/gate/m1.avro".to_string(),
                manifest_length: 256,
                partition_spec_id: 0,
                content: ManifestContentType::Data,
                sequence_number: 0,
                min_sequence_number: 0,
                added_snapshot_id: 43,
                added_files_count: Some(1),
                existing_files_count: Some(0),
                deleted_files_count: Some(0),
                added_rows_count: Some(2),
                existing_rows_count: Some(0),
                deleted_rows_count: Some(0),
                partitions: vec![],
                key_metadata: vec![],
            }]
            .into_iter(),
        )
        .unwrap();
    v1_writer.close().await.expect("close v1 manifest list");
    let v1_bytes = std::fs::read(&v1_path).unwrap();
    assert_eq!(&v1_bytes[..4], b"Obj\x01");
    let v1_list = ManifestList::parse_with_version(&v1_bytes, FormatVersion::V1, |_id| Ok(None))
        .expect("parse v1 manifest list");
    assert_eq!(v1_list.entries().len(), 1);
    assert_eq!(v1_list.entries()[0].manifest_path, "/tmp/gate/m1.avro");
    assert_eq!(v1_list.entries()[0].added_snapshot_id, 43);
}

/// Step 01 gate: partition transforms are parseable and typed per the spec.
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
// gate02 — milestone 02: scan planning over a table built through FileIO.
// ---------------------------------------------------------------------------

/// Builds a real table on disk: metadata is produced by the metadata builder,
/// a manifest and a manifest list are written through the public writers, and
/// the resulting metadata is committed and reopened as a `Table`.
async fn gate_table() -> (tempfile::TempDir, Table, String) {
    let dir = tempfile::TempDir::new().unwrap();
    let location = format!("file://{}", dir.path().to_str().unwrap());
    let io = FileIO::from_path(&location).unwrap().build().unwrap();

    let creation = TableCreation::builder()
        .name("gate".to_string())
        .location(location.clone())
        .schema(gate_schema())
        .build();
    let base = TableMetadataBuilder::from_table_creation(creation)
        .unwrap()
        .build()
        .unwrap()
        .metadata;

    let snapshot_id = 42i64;
    let manifest_path = format!("{location}/metadata/m0.avro");
    let manifest_list_path = format!("{location}/metadata/snap-42.avro");

    let schema = base.current_schema().clone();
    let manifest = Manifest::new(
        ManifestMetadata::builder()
            .schema(schema.clone())
            .schema_id(schema.schema_id())
            .partition_spec((**base.default_partition_spec()).clone())
            .content(ManifestContentType::Data)
            .format_version(FormatVersion::V2)
            .build(),
        vec![
            ManifestEntry::builder()
                .status(ManifestStatus::Added)
                .data_file(gate_data_file(&format!("{location}/data/1.parquet"), 1, 10))
                .build(),
            ManifestEntry::builder()
                .status(ManifestStatus::Added)
                .data_file(gate_data_file(
                    &format!("{location}/data/2.parquet"),
                    100,
                    200,
                ))
                .build(),
        ],
    );
    let manifest_file =
        ManifestWriter::new(io.new_output(&manifest_path).unwrap(), snapshot_id, vec![])
            .write(manifest)
            .await
            .expect("write manifest");
    assert_eq!(manifest_file.added_files_count, Some(2));

    let mut list_writer = ManifestListWriter::v2(
        io.new_output(&manifest_list_path).unwrap(),
        snapshot_id,
        None,
        1,
    );
    list_writer
        .add_manifests(vec![manifest_file].into_iter())
        .unwrap();
    list_writer.close().await.expect("write manifest list");

    let snapshot = Snapshot::builder()
        .with_snapshot_id(snapshot_id)
        .with_parent_snapshot_id(None)
        .with_sequence_number(1)
        .with_timestamp_ms(base.last_updated_ms() + 1_000)
        .with_manifest_list(manifest_list_path.clone())
        .with_schema_id(schema.schema_id())
        .with_summary(Summary {
            operation: Operation::Append,
            additional_properties: HashMap::new(),
        })
        .build();
    let metadata = base
        .into_builder(None)
        .set_branch_snapshot(snapshot, "main")
        .unwrap()
        .build()
        .unwrap()
        .metadata;

    // Write the metadata document itself through FileIO, as a commit would.
    let metadata_location = format!("{location}/metadata/v1.metadata.json");
    io.new_output(&metadata_location)
        .unwrap()
        .write(serde_json::to_vec(&metadata).unwrap().into())
        .await
        .expect("write table metadata");

    let table = Table::builder()
        .file_io(io)
        .metadata(metadata)
        .metadata_location(metadata_location)
        .identifier(TableIdent::from_strs(["db", "gate"]).unwrap())
        .build()
        .expect("build table");
    (dir, table, manifest_list_path)
}

/// Step 02 gate: a table assembled from files written through `FileIO` can be
/// re-read from those files — the metadata document, the manifest list and the
/// manifest all parse back through the public readers.
#[test]
fn gate02__table_files_round_trip_through_file_io() {
    block_on(gate02__table_files_round_trip_through_file_io_impl());
}

async fn gate02__table_files_round_trip_through_file_io_impl() {
    let (_dir, table, manifest_list_path) = gate_table().await;

    // The committed metadata document parses back into the same metadata.
    let document = table
        .file_io()
        .new_input(table.metadata_location().expect("metadata location"))
        .unwrap()
        .read()
        .await
        .expect("read metadata file");
    let reparsed: TableMetadata = serde_json::from_slice(&document).expect("parse metadata file");
    assert_eq!(&reparsed, table.metadata());

    // The current snapshot's manifest list is where the metadata says it is.
    let snapshot = table.metadata().current_snapshot().expect("current snapshot");
    assert_eq!(snapshot.manifest_list(), manifest_list_path);
    let bytes = table
        .file_io()
        .new_input(snapshot.manifest_list())
        .unwrap()
        .read()
        .await
        .expect("read manifest list");
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2, |_id| Ok(None))
        .expect("parse manifest list");
    assert_eq!(list.entries().len(), 1);
    assert_eq!(list.entries()[0].added_snapshot_id, 42);
    assert_eq!(list.entries()[0].added_files_count, Some(2));

    // And that manifest lists both data files with their column bounds.
    let manifest_bytes = table
        .file_io()
        .new_input(&list.entries()[0].manifest_path)
        .unwrap()
        .read()
        .await
        .expect("read manifest");
    let manifest = Manifest::parse_avro(&manifest_bytes).expect("parse manifest");
    assert_eq!(manifest.entries().len(), 2);
    let mut paths: Vec<&str> = manifest.entries().iter().map(|e| e.file_path()).collect();
    paths.sort();
    assert!(paths[0].ends_with("/data/1.parquet"));
    assert!(paths[1].ends_with("/data/2.parquet"));
    for entry in manifest.entries() {
        assert!(entry.is_alive());
        assert_eq!(entry.content_type(), DataContentType::Data);
        assert!(entry.data_file().lower_bounds().contains_key(&1));
        assert!(entry.data_file().upper_bounds().contains_key(&1));
    }
}

/// Step 02 gate: planning a scan resolves the current snapshot and fetches its
/// manifest list through `FileIO`; a filter is bound against the snapshot's
/// schema.
#[test]
fn gate02__scan_planning_resolves_the_snapshot_and_manifest_list() {
    block_on(gate02__scan_planning_resolves_the_snapshot_and_manifest_list_impl());
}

async fn gate02__scan_planning_resolves_the_snapshot_and_manifest_list_impl() {
    let (_dir, table, _) = gate_table().await;

    let scan = table.scan().build().expect("build scan");
    assert_eq!(scan.snapshot().snapshot_id(), 42);
    assert!(
        scan.plan_files().await.is_ok(),
        "planning must locate and parse the snapshot's manifest list"
    );

    // The same holds for a scan carrying a filter and a projection.
    let scan = table
        .scan()
        .select(["id"])
        .with_filter(Reference::new("id").greater_than(Datum::long(50)))
        .build()
        .expect("build filtered scan");
    assert_eq!(scan.column_names(), Some(["id".to_string()].as_slice()));
    assert!(scan.plan_files().await.is_ok());

    // A snapshot whose manifest list is missing cannot be planned.
    let broken_snapshot = Snapshot::builder()
        .with_snapshot_id(43)
        .with_parent_snapshot_id(Some(42))
        .with_sequence_number(2)
        .with_timestamp_ms(table.metadata().last_updated_ms() + 1_000)
        .with_manifest_list(format!("{}/metadata/missing.avro", table.metadata().location()))
        .with_schema_id(0)
        .with_summary(Summary {
            operation: Operation::Append,
            additional_properties: HashMap::new(),
        })
        .build();
    let broken = table
        .metadata()
        .clone()
        .into_builder(None)
        .set_branch_snapshot(broken_snapshot, "main")
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    let broken_table = Table::builder()
        .file_io(table.file_io().clone())
        .metadata(broken)
        .identifier(TableIdent::from_strs(["db", "gate"]).unwrap())
        .build()
        .unwrap();
    assert!(broken_table
        .scan()
        .build()
        .unwrap()
        .plan_files()
        .await
        .is_err());
}

/// Step 02 gate: the scan builder validates the projection, the filter and the
/// requested snapshot against the table metadata.
#[test]
fn gate02__scan_builder_validates_its_inputs() {
    block_on(gate02__scan_builder_validates_its_inputs_impl());
}

async fn gate02__scan_builder_validates_its_inputs_impl() {
    let (_dir, table, _) = gate_table().await;
    // A column that is not in the snapshot's schema.
    assert!(table.scan().select(["nosuchcolumn"]).build().is_err());
    // A filter that references a column that is not in the schema.
    assert!(table
        .scan()
        .with_filter(Reference::new("nosuchcolumn").greater_than(Datum::long(1)))
        .build()
        .is_err());
    // A snapshot id the table does not have.
    assert!(table.scan().snapshot_id(999).build().is_err());
    // The snapshot that does exist can be named explicitly.
    let scan = table.scan().snapshot_id(42).build().expect("build scan");
    assert_eq!(scan.snapshot().snapshot_id(), 42);
    assert_eq!(scan.snapshot().sequence_number(), 1);
}

// ---------------------------------------------------------------------------
// gate03 — milestone 03: the metadata builder produces committable metadata.
// ---------------------------------------------------------------------------

/// Step 03 gate: table metadata built from a `TableCreation` is valid metadata
/// — it serializes to a metadata document that parses back unchanged.
#[test]
fn gate03__builder_creates_committable_metadata() {
    let creation = TableCreation::builder()
        .name("gate".to_string())
        .location("file:///warehouse/db/gate".to_string())
        .schema(gate_schema())
        .build();
    let result = TableMetadataBuilder::from_table_creation(creation)
        .expect("builder from table creation")
        .build()
        .expect("build metadata");
    let md = result.metadata;

    assert_eq!(md.format_version(), FormatVersion::V2);
    assert_eq!(md.location(), "file:///warehouse/db/gate");
    assert_eq!(md.current_schema_id(), 0);
    assert_eq!(md.current_schema().field_id_by_name("id"), Some(1));
    assert_eq!(md.default_partition_spec_id(), 0);
    assert_eq!(md.default_sort_order_id(), 0);
    assert!(md.current_snapshot().is_none());
    assert!(!result.changes.is_empty());

    let document = serde_json::to_string(&md).expect("serialize metadata");
    let reparsed: TableMetadata = serde_json::from_str(&document).expect("reparse metadata");
    assert_eq!(reparsed, md);
}

/// Step 03 gate: the builder evolves an existing table — new schema, new
/// partition spec, new snapshot on `main` — and the result is still valid
/// metadata.
#[test]
fn gate03__builder_commits_schema_spec_and_snapshot() {
    let creation = TableCreation::builder()
        .name("gate".to_string())
        .location("file:///warehouse/db/gate".to_string())
        .schema(gate_schema())
        .build();
    let base = TableMetadataBuilder::from_table_creation(creation)
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    let ts = base.last_updated_ms() + 1_000;

    let widened = Schema::builder()
        .with_schema_id(-1)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "ts", Type::Primitive(PrimitiveType::Timestamptz)).into(),
            NestedField::optional(3, "note", Type::Primitive(PrimitiveType::String)).into(),
        ])
        .build()
        .unwrap();
    let snapshot = Snapshot::builder()
        .with_snapshot_id(42)
        .with_parent_snapshot_id(None)
        .with_sequence_number(1)
        .with_timestamp_ms(ts)
        .with_manifest_list("file:///warehouse/db/gate/metadata/snap-42.avro")
        .with_schema_id(1)
        .with_summary(Summary {
            operation: Operation::Append,
            additional_properties: HashMap::new(),
        })
        .build();

    let result = base
        .into_builder(Some(
            "file:///warehouse/db/gate/metadata/v1.json".to_string(),
        ))
        .add_current_schema(widened)
        .unwrap()
        .add_default_partition_spec(
            UnboundPartitionSpec::builder()
                .add_partition_field(2, "ts_day", Transform::Day)
                .unwrap()
                .build(),
        )
        .unwrap()
        .set_branch_snapshot(snapshot, "main")
        .unwrap()
        .build()
        .expect("build metadata");
    let md = result.metadata;

    assert_eq!(md.current_schema_id(), 1);
    assert_eq!(md.current_schema().field_id_by_name("note"), Some(3));
    assert_eq!(md.default_partition_spec_id(), 1);
    assert_eq!(md.default_partition_spec().fields()[0].field_id, 1000);
    assert_eq!(md.current_snapshot_id(), Some(42));
    assert_eq!(md.snapshot_for_ref("main").unwrap().snapshot_id(), 42);
    assert_eq!(md.history().len(), 1);
    assert_eq!(md.last_sequence_number(), 1);
    assert_eq!(md.metadata_log().len(), 1);

    let document = serde_json::to_string(&md).expect("serialize metadata");
    let reparsed: TableMetadata = serde_json::from_str(&document).expect("reparse metadata");
    assert_eq!(reparsed, md);
}

/// Step 03 gate: the recorded updates and the commit requirements describe the
/// commit — the requirements hold against the metadata the commit started from
/// and the updates replay onto it.
#[test]
fn gate03__updates_and_requirements_describe_the_commit() {
    let creation = TableCreation::builder()
        .name("gate".to_string())
        .location("file:///warehouse/db/gate".to_string())
        .schema(gate_schema())
        .build();
    let base = TableMetadataBuilder::from_table_creation(creation)
        .unwrap()
        .build()
        .unwrap()
        .metadata;

    let requirements = vec![
        TableRequirement::UuidMatch { uuid: base.uuid() },
        TableRequirement::CurrentSchemaIdMatch {
            current_schema_id: base.current_schema_id(),
        },
        TableRequirement::DefaultSpecIdMatch {
            default_spec_id: base.default_partition_spec_id(),
        },
    ];
    for requirement in &requirements {
        assert!(
            requirement.check(Some(&base)).is_ok(),
            "requirement {requirement:?} must hold against the base metadata"
        );
    }

    let result = base
        .clone()
        .into_builder(None)
        .add_default_partition_spec(
            UnboundPartitionSpec::builder()
                .add_partition_field(1, "id_bucket", Transform::Bucket(8))
                .unwrap()
                .build(),
        )
        .unwrap()
        .build()
        .unwrap();
    assert!(!result.changes.is_empty());

    // Replaying the recorded updates on the base metadata reproduces the commit.
    let mut builder = base.clone().into_builder(None);
    for update in result.changes.clone() {
        builder = TableUpdate::apply(update, builder).expect("apply recorded update");
    }
    let replayed = builder.build().expect("rebuild metadata").metadata;
    assert_eq!(
        replayed.default_partition_spec_id(),
        result.metadata.default_partition_spec_id()
    );
    assert_eq!(
        replayed.default_partition_spec().fields()[0].field_id,
        result.metadata.default_partition_spec().fields()[0].field_id
    );

    // A requirement that no longer holds after the commit fails.
    assert!(TableRequirement::DefaultSpecIdMatch {
        default_spec_id: base.default_partition_spec_id()
    }
    .check(Some(&result.metadata))
    .is_err());
}
