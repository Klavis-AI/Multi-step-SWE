// Verifier-owned conformance driver for milestone 04 (format-v2 row-level deletes,
// Puffin, metadata tables, object cache).
//
// Every assertion here traces to a named section of the pinned Apache Iceberg table
// specification (/app/iceberg-spec/spec.md) or the Puffin specification
// (/app/iceberg-spec/puffin-spec.md), or to /app/TEST_INTERFACE.md, which states the
// public Rust surface the milestone must expose.
//
// Group = the `gNN_<name>__` prefix of the test function name. The runner invokes one
// `cargo test` per group and asserts the executed count.
//
// Milestones 01-03 (types, schemas, transforms, metadata, manifests, scan planning,
// TableMetadataBuilder) are inherited; they are gated by tests/gate.rs, not scored here.

use std::collections::HashMap;
use std::sync::Arc;

use iceberg::io::{FileIO, FileIOBuilder};
use iceberg::spec::*;

// ---------------------------------------------------------------------------
// shared fixtures — no mutable state is shared between groups; every helper
// builds fresh values in the caller's own temporary directory.
// ---------------------------------------------------------------------------

fn temp_io() -> (tempfile::TempDir, FileIO) {
    let dir = tempfile::TempDir::new().unwrap();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    (dir, io)
}

fn path_in(dir: &tempfile::TempDir, name: &str) -> String {
    format!("{}/{}", dir.path().to_str().unwrap(), name)
}

/// The table schema used by every manifest written in this file.
fn table_schema() -> Schema {
    Schema::builder()
        .with_schema_id(0)
        .with_identifier_field_ids(vec![1])
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "category", Type::Primitive(PrimitiveType::String)).into(),
            NestedField::optional(3, "score", Type::Primitive(PrimitiveType::Double)).into(),
        ])
        .build()
        .unwrap()
}

/// A schema whose field 4 is a struct, i.e. a non-primitive column. Used only by
/// the equality-delete rejection case; the manifests in this file all use
/// `table_schema()`.
fn schema_with_a_nested_column() -> Schema {
    Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "category", Type::Primitive(PrimitiveType::String)).into(),
            NestedField::optional(3, "score", Type::Primitive(PrimitiveType::Double)).into(),
            NestedField::optional(
                4,
                "address",
                Type::Struct(StructType::new(vec![
                    NestedField::optional(5, "city", Type::Primitive(PrimitiveType::String)).into(),
                    NestedField::optional(6, "zip", Type::Primitive(PrimitiveType::Long)).into(),
                ])),
            )
            .into(),
        ])
        .build()
        .unwrap()
}

fn unpartitioned_spec(schema: &SchemaRef) -> PartitionSpec {
    PartitionSpec::builder(schema.clone())
        .with_spec_id(0)
        .build()
        .unwrap()
}

fn data_file(path: &str) -> DataFile {
    DataFileBuilder::default()
        .content(DataContentType::Data)
        .file_path(path.to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .record_count(10)
        .file_size_in_bytes(2048)
        .partition_spec_id(0)
        .build()
        .unwrap()
}

/// A position delete file. Per spec.md "Position Delete Files" the entry names the
/// data file it deletes from; `equality_ids` is empty and the sort order id is null.
fn position_delete_file(path: &str, referenced: &str) -> DataFile {
    DataFileBuilder::default()
        .content(DataContentType::PositionDeletes)
        .file_path(path.to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .record_count(3)
        .file_size_in_bytes(512)
        .partition_spec_id(0)
        .first_row_id(Some(77))
        .referenced_data_file(Some(referenced.to_string()))
        .content_offset(Some(4))
        .content_size_in_bytes(Some(128))
        .build()
        .unwrap()
}

/// An equality delete file. Per spec.md "Equality Delete Files" the delete columns
/// are named by `equality_ids`.
fn equality_delete_file(path: &str, equality_ids: Vec<i32>) -> DataFile {
    DataFileBuilder::default()
        .content(DataContentType::EqualityDeletes)
        .file_path(path.to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .record_count(2)
        .file_size_in_bytes(256)
        .partition_spec_id(0)
        .equality_ids(equality_ids)
        .build()
        .unwrap()
}

// ---------------------------------------------------------------------------
// g01_content_type — spec.md "Manifests" / "Manifest Entry Fields":
// `content` is 0 for data, 1 for position deletes and 2 for equality deletes.
// ---------------------------------------------------------------------------

#[test]
fn g01_content_type__codes_are_0_1_2() {
    assert_eq!(DataContentType::Data as i32, 0);
    assert_eq!(DataContentType::PositionDeletes as i32, 1);
    assert_eq!(DataContentType::EqualityDeletes as i32, 2);

    assert_eq!(DataContentType::try_from(0).unwrap(), DataContentType::Data);
    assert_eq!(
        DataContentType::try_from(1).unwrap(),
        DataContentType::PositionDeletes
    );
    assert_eq!(
        DataContentType::try_from(2).unwrap(),
        DataContentType::EqualityDeletes
    );
}

#[test]
fn g01_content_type__undefined_code_is_rejected() {
    // Only 0, 1 and 2 are defined by the specification.
    assert!(DataContentType::try_from(3).is_err());
    assert!(DataContentType::try_from(-1).is_err());
    assert!(DataContentType::try_from(i32::MAX).is_err());
}

#[tokio::test]
async fn g01_content_type__manifest_entry_reports_its_content() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);

    let path = path_in(&dir, "deletes.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(7),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(
            position_delete_file("/tmp/pd.parquet", "/tmp/d.parquet"),
            2,
        )
        .unwrap();
    writer
        .add_file(equality_delete_file("/tmp/ed.parquet", vec![1]), 2)
        .unwrap();
    writer.write_manifest_file().await.unwrap();

    let bytes = std::fs::read(&path).unwrap();
    let manifest = Manifest::parse_avro(&bytes).unwrap();
    let kinds: Vec<DataContentType> = manifest.entries().iter().map(|e| e.content_type()).collect();
    assert!(kinds.contains(&DataContentType::PositionDeletes));
    assert!(kinds.contains(&DataContentType::EqualityDeletes));
    assert!(!kinds.contains(&DataContentType::Data));
}

// ---------------------------------------------------------------------------
// g02_file_format — spec.md "Delete Formats" / "Deletion Vectors": deletion
// vectors are stored in Puffin files, so `puffin` is a data file format.
// ---------------------------------------------------------------------------

#[test]
fn g02_file_format__puffin_name_round_trips() {
    assert_eq!(
        "puffin".parse::<DataFileFormat>().unwrap(),
        DataFileFormat::Puffin
    );
    assert_eq!(format!("{}", DataFileFormat::Puffin), "puffin");
    assert_eq!(
        serde_json::to_string(&DataFileFormat::Puffin).unwrap(),
        "\"puffin\""
    );
    let back: DataFileFormat = serde_json::from_str("\"puffin\"").unwrap();
    assert_eq!(back, DataFileFormat::Puffin);
}

#[test]
fn g02_file_format__other_names_and_rejection() {
    for (name, want) in [
        ("avro", DataFileFormat::Avro),
        ("orc", DataFileFormat::Orc),
        ("parquet", DataFileFormat::Parquet),
    ] {
        assert_eq!(name.parse::<DataFileFormat>().unwrap(), want);
        assert_eq!(format!("{want}"), name);
    }
    // TEST_INTERFACE.md 4.1: `from_str` lower-cases its input before matching,
    // so a capitalised spelling of one of the four names still parses.
    assert_eq!(
        "Parquet".parse::<DataFileFormat>().unwrap(),
        DataFileFormat::Parquet
    );
    assert_eq!(
        "AVRO".parse::<DataFileFormat>().unwrap(),
        DataFileFormat::Avro
    );
    assert!("puffin1".parse::<DataFileFormat>().is_err());
    assert!("".parse::<DataFileFormat>().is_err());
    assert!(serde_json::from_str::<DataFileFormat>("\"deletion-vector\"").is_err());
}

// ---------------------------------------------------------------------------
// g03_delete_manifest — spec.md "Manifests": "A separate set of manifests is
// used for delete files ..., but the same manifest schema is used for both data
// and delete manifests." Row-level deletes are added by v2 and are not
// supported in v1 (spec.md "Delete Formats").
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g03_delete_manifest__position_delete_round_trip() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "pos-deletes.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(11),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(
            position_delete_file("/tmp/pos-0.parquet", "/tmp/data-0.parquet"),
            5,
        )
        .unwrap();
    let manifest_file = writer.write_manifest_file().await.unwrap();
    assert_eq!(manifest_file.content, ManifestContentType::Deletes);
    assert_eq!(manifest_file.added_files_count, Some(1));

    let bytes = std::fs::read(&path).unwrap();
    // spec.md "Manifests" key-value metadata: the file carries `schema`,
    // `schema-id`, `partition-spec`, `partition-spec-id`, `format-version` and,
    // for v2, `content`. The keys live in the (uncompressed) Avro
    // object-container header, so the exact spellings are visible in the raw
    // bytes. Avro encodes a map key as a zigzag varint length followed by the
    // UTF-8 bytes, and every key here is shorter than 64 characters, so the
    // length is a single byte `len << 1`. Searching for the length-prefixed
    // form is what makes the check discriminating: a bare `"schema"` window
    // would also be satisfied by `schema-id`, and `"content"` by the
    // `data_file` field of the same name inside the embedded writer schema.
    for key in [
        "schema",
        "schema-id",
        "partition-spec",
        "partition-spec-id",
        "format-version",
        "content",
    ] {
        let mut needle = Vec::with_capacity(key.len() + 1);
        needle.push((key.len() as u8) << 1);
        needle.extend_from_slice(key.as_bytes());
        assert!(
            bytes.windows(needle.len()).any(|w| w == needle.as_slice()),
            "the manifest's user metadata must use the key spelling {key:?}"
        );
    }
    let (entries, metadata) = Manifest::parse_avro(&bytes).unwrap().into_parts();
    assert_eq!(metadata.content, ManifestContentType::Deletes);
    assert_eq!(metadata.format_version, FormatVersion::V2);
    // The other three `ManifestMetadata` fields describe the schema and spec the
    // writer was built from.
    assert_eq!(metadata.schema_id, 0);
    assert_eq!(metadata.schema.as_struct(), schema.as_struct());
    assert_eq!(metadata.partition_spec, unpartitioned_spec(&schema));
    assert_eq!(entries.len(), 1);
    // The entry-level accessors report the same file as the `DataFile` beneath
    // it (spec.md "Manifest Entry Fields").
    assert_eq!(entries[0].file_format(), DataFileFormat::Parquet);
    assert_eq!(entries[0].record_count(), 3);
    assert_eq!(entries[0].file_size_in_bytes(), 512);
    let df = entries[0].data_file();
    assert_eq!(df.content_type(), DataContentType::PositionDeletes);
    assert_eq!(df.file_path(), "/tmp/pos-0.parquet");
    assert_eq!(df.record_count(), 3);
    assert_eq!(df.partition(), &Struct::empty());
}

#[tokio::test]
async fn g03_delete_manifest__equality_delete_round_trip() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "eq-deletes.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(12),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(equality_delete_file("/tmp/eq-0.parquet", vec![1, 2]), 5)
        .unwrap();
    writer.write_manifest_file().await.unwrap();

    let bytes = std::fs::read(&path).unwrap();
    let manifest = Manifest::parse_avro(&bytes).unwrap();
    let df = manifest.entries()[0].data_file();
    assert_eq!(df.content_type(), DataContentType::EqualityDeletes);
    assert_eq!(df.equality_ids(), &[1, 2]);
}

#[tokio::test]
async fn g03_delete_manifest__data_manifest_rejects_delete_file() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "data.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(13),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_data();
    assert!(
        writer
            .add_file(
                position_delete_file("/tmp/pos-1.parquet", "/tmp/data-1.parquet"),
                5
            )
            .is_err(),
        "a data manifest must not track a position delete file"
    );
    assert!(
        writer
            .add_file(equality_delete_file("/tmp/eq-1.parquet", vec![1]), 5)
            .is_err(),
        "a data manifest must not track an equality delete file"
    );
}

#[tokio::test]
async fn g03_delete_manifest__delete_manifest_rejects_data_file() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "deletes-only.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(14),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    assert!(
        writer.add_file(data_file("/tmp/data-2.parquet"), 5).is_err(),
        "a delete manifest must not track a data file"
    );

    // TEST_INTERFACE.md 4.2: the required builder fields have no defaults and
    // `build()` is fallible, so an incomplete builder is an error rather than a
    // partially filled `DataFile`.
    assert!(
        DataFileBuilder::default()
            .content(DataContentType::PositionDeletes)
            .build()
            .is_err(),
        "an incomplete DataFileBuilder must not build"
    );
    // `partition_spec_id` is the field this milestone makes required; a builder
    // that supplies every other required field still fails without it.
    let mut without_spec_id = DataFileBuilder::default();
    without_spec_id
        .content(DataContentType::EqualityDeletes)
        .file_path("/tmp/eq-nospec.parquet".to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .record_count(1)
        .file_size_in_bytes(64);
    assert!(
        without_spec_id.build().is_err(),
        "partition_spec_id is a required builder field at this milestone"
    );
    assert!(
        without_spec_id.partition_spec_id(0).build().is_ok(),
        "supplying partition_spec_id completes the builder"
    );
}

#[tokio::test]
async fn g03_delete_manifest__v1_manifest_rejects_delete_file() {
    // "Delete Formats": row-level deletes are added by v2 and are not supported
    // in v1, so a v1 manifest only tracks data files.
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "v1.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(15),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v1();
    assert!(
        writer
            .add_file(
                position_delete_file("/tmp/pos-2.parquet", "/tmp/data-2.parquet"),
                5
            )
            .is_err(),
        "a v1 manifest must not declare a delete file"
    );
    assert!(
        writer
            .add_file(equality_delete_file("/tmp/eq-2.parquet", vec![1]), 5)
            .is_err(),
        "a v1 manifest must not declare a delete file"
    );
}

// ---------------------------------------------------------------------------
// g04_manifest_content — spec.md "Manifest Lists": field 517 `content` is
// "the type of files tracked by the manifest, either data or delete files;
// 0 for all v1 manifests".
// ---------------------------------------------------------------------------

#[test]
fn g04_manifest_content__names_and_codes() {
    assert_eq!(ManifestContentType::Data as i32, 0);
    assert_eq!(ManifestContentType::Deletes as i32, 1);
    assert_eq!(
        ManifestContentType::try_from(0).unwrap(),
        ManifestContentType::Data
    );
    assert_eq!(
        ManifestContentType::try_from(1).unwrap(),
        ManifestContentType::Deletes
    );
    assert_eq!("data".parse::<ManifestContentType>().unwrap(), ManifestContentType::Data);
    assert_eq!(
        "deletes".parse::<ManifestContentType>().unwrap(),
        ManifestContentType::Deletes
    );
    assert_eq!(format!("{}", ManifestContentType::Data), "data");
    assert_eq!(format!("{}", ManifestContentType::Deletes), "deletes");
}

#[test]
fn g04_manifest_content__undefined_content_is_rejected() {
    assert!(ManifestContentType::try_from(2).is_err());
    assert!(ManifestContentType::try_from(-1).is_err());
    assert!("Deletes".parse::<ManifestContentType>().is_err());
    assert!("delete".parse::<ManifestContentType>().is_err());
}

#[tokio::test]
async fn g04_manifest_content__manifest_list_keeps_data_and_delete_manifests_apart() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);

    let data_path = path_in(&dir, "m-data.avro");
    let mut dw = ManifestWriterBuilder::new(
        io.new_output(&data_path).unwrap(),
        Some(20),
        vec![],
        schema.clone(),
        spec.clone(),
    )
    .build_v2_data();
    dw.add_file(data_file("/tmp/d0.parquet"), 4).unwrap();
    let data_manifest = dw.write_manifest_file().await.unwrap();

    let del_path = path_in(&dir, "m-del.avro");
    let mut xw = ManifestWriterBuilder::new(
        io.new_output(&del_path).unwrap(),
        Some(20),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    xw.add_file(equality_delete_file("/tmp/e0.parquet", vec![1]), 4)
        .unwrap();
    let delete_manifest = xw.write_manifest_file().await.unwrap();

    let list_path = path_in(&dir, "ml.avro");
    let mut lw = ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 20, Some(19), 4);
    lw.add_manifests(vec![data_manifest, delete_manifest].into_iter())
        .unwrap();
    lw.close().await.unwrap();

    let bytes = std::fs::read(&list_path).unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2, |_id| Ok(None)).unwrap();
    assert_eq!(list.entries().len(), 2);
    let contents: Vec<ManifestContentType> = list.entries().iter().map(|e| e.content).collect();
    assert!(contents.contains(&ManifestContentType::Data));
    assert!(contents.contains(&ManifestContentType::Deletes));
}

#[tokio::test]
async fn g04_manifest_content__v1_manifest_list_round_trips() {
    // "Manifest Lists" field 517: `content` is "0 for all v1 manifests", so a
    // format-1 manifest list reads back as data manifests whatever else it holds.
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);

    let data_path = path_in(&dir, "v1-m.avro");
    let mut dw = ManifestWriterBuilder::new(
        io.new_output(&data_path).unwrap(),
        Some(21),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v1();
    dw.add_file(data_file("/tmp/v1-d0.parquet"), 1).unwrap();
    let manifest = dw.write_manifest_file().await.unwrap();
    assert_eq!(manifest.added_snapshot_id, 21);
    // The manifest list stores each `ManifestFile`'s fields and reads them back
    // unchanged (TEST_INTERFACE.md 4.4). How the writer spells the manifest's
    // own location is not asserted — only that the list preserves it and that
    // it names the file that was written.
    let written_path = manifest.manifest_path.clone();
    assert!(
        written_path.ends_with("v1-m.avro"),
        "the returned ManifestFile must locate the manifest that was written, got {written_path:?}"
    );
    let manifest_length = manifest.manifest_length;
    let partitions = manifest.partitions.clone();
    let key_metadata = manifest.key_metadata.clone();

    let list_path = path_in(&dir, "v1-ml.avro");
    let mut lw = ManifestListWriter::v1(io.new_output(&list_path).unwrap(), 21, Some(20));
    lw.add_manifests(vec![manifest].into_iter()).unwrap();
    lw.close().await.unwrap();

    let bytes = std::fs::read(&list_path).unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V1, |_id| Ok(None)).unwrap();
    assert_eq!(list.entries().len(), 1);
    assert_eq!(list.entries()[0].manifest_path, written_path);
    assert_eq!(list.entries()[0].added_snapshot_id, 21);
    assert_eq!(list.entries()[0].added_files_count, Some(1));
    assert_eq!(list.entries()[0].content, ManifestContentType::Data);
    // The three fields nothing else reads back. 4.4: the list returns
    // `manifest_length`, `partitions` and `key_metadata` as they were written.
    // Only the round trip is asserted — what a writer *puts* in `partitions`
    // and `key_metadata` is not this milestone's business.
    assert_eq!(list.entries()[0].manifest_length, manifest_length);
    assert_eq!(list.entries()[0].partitions, partitions);
    assert_eq!(list.entries()[0].key_metadata, key_metadata);
}

// ---------------------------------------------------------------------------
// g05_pos_delete_layout — spec.md "Position Delete Files" and
// "Deletion Vectors".
// ---------------------------------------------------------------------------

/// spec.md "Position Delete Files": `2147483546 file_path`.
const POS_DELETE_FILE_PATH_FIELD_ID: i32 = 2147483546;
/// spec.md "Position Delete Files": `2147483545 pos`.
const POS_DELETE_POS_FIELD_ID: i32 = 2147483545;

#[test]
fn g05_pos_delete_layout__reserved_field_ids() {
    let schema = Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![
            NestedField::required(
                POS_DELETE_FILE_PATH_FIELD_ID,
                "file_path",
                Type::Primitive(PrimitiveType::String),
            )
            .into(),
            NestedField::required(
                POS_DELETE_POS_FIELD_ID,
                "pos",
                Type::Primitive(PrimitiveType::Long),
            )
            .into(),
        ])
        .build()
        .unwrap();

    assert_eq!(
        schema.field_id_by_name("file_path"),
        Some(POS_DELETE_FILE_PATH_FIELD_ID)
    );
    assert_eq!(schema.field_id_by_name("pos"), Some(POS_DELETE_POS_FIELD_ID));
    assert_eq!(
        schema
            .field_by_id(POS_DELETE_FILE_PATH_FIELD_ID)
            .unwrap()
            .field_type
            .as_ref(),
        &Type::Primitive(PrimitiveType::String)
    );
    assert_eq!(
        schema
            .field_by_id(POS_DELETE_POS_FIELD_ID)
            .unwrap()
            .field_type
            .as_ref(),
        &Type::Primitive(PrimitiveType::Long)
    );
    assert_eq!(schema.highest_field_id(), POS_DELETE_FILE_PATH_FIELD_ID);

    // The reserved ids survive JSON serialization unchanged.
    let value = serde_json::to_value(&schema).unwrap();
    assert_eq!(value["fields"][0]["id"], POS_DELETE_FILE_PATH_FIELD_ID);
    assert_eq!(value["fields"][1]["id"], POS_DELETE_POS_FIELD_ID);
    let back: Schema = serde_json::from_value(value).unwrap();
    assert_eq!(back.as_struct(), schema.as_struct());
}

#[tokio::test]
async fn g05_pos_delete_layout__entry_names_the_referenced_data_file() {
    // "Deletion Vectors": delete manifests track a delete blob by the containing
    // file location, `content_offset` and `content_size_in_bytes`, and the
    // referenced data file is required.
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "pos-ref.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(30),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(
            position_delete_file("/tmp/dv.puffin", "/tmp/data-ref.parquet"),
            6,
        )
        .unwrap();
    writer.write_manifest_file().await.unwrap();

    let bytes = std::fs::read(&path).unwrap();
    let manifest = Manifest::parse_avro(&bytes).unwrap();
    let df = manifest.entries()[0].data_file();
    assert_eq!(
        df.referenced_data_file().as_deref(),
        Some("/tmp/data-ref.parquet")
    );
    assert_eq!(df.content_offset(), Some(4));
    assert_eq!(df.content_size_in_bytes(), Some(128));
    // Field 142 is one of the five delete-related fields that must survive the
    // format-2 manifest round trip.
    assert_eq!(df.first_row_id(), Some(77));
}

#[tokio::test]
async fn g05_pos_delete_layout__no_equality_ids_and_no_sort_order() {
    // "Position Delete Files": position deletes are sorted by file and position,
    // not by a table order, so the sort order id is null; `equality_ids` is
    // required only for equality deletes.
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "pos-plain.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(31),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(
            position_delete_file("/tmp/pos-plain.parquet", "/tmp/data-plain.parquet"),
            6,
        )
        .unwrap();
    writer.write_manifest_file().await.unwrap();

    let bytes = std::fs::read(&path).unwrap();
    let manifest = Manifest::parse_avro(&bytes).unwrap();
    let df = manifest.entries()[0].data_file();
    assert!(df.equality_ids().is_empty());
    assert_eq!(df.sort_order_id(), None);

    // "Only data files and equality delete files should be written with a
    // non-null order id", so a sort order id set on an equality delete survives.
    let sorted_path = path_in(&dir, "eq-sorted.avro");
    let mut w2 = ManifestWriterBuilder::new(
        io.new_output(&sorted_path).unwrap(),
        Some(32),
        vec![],
        schema.clone(),
        unpartitioned_spec(&schema),
    )
    .build_v2_deletes();
    w2.add_file(
        DataFileBuilder::default()
            .content(DataContentType::EqualityDeletes)
            .file_path("/tmp/eq-sorted.parquet".to_string())
            .file_format(DataFileFormat::Parquet)
            .partition(Struct::empty())
            .record_count(1)
            .file_size_in_bytes(64)
            .partition_spec_id(0)
            .equality_ids(vec![1])
            .sort_order_id(3)
            .build()
            .unwrap(),
        6,
    )
    .unwrap();
    w2.write_manifest_file().await.unwrap();
    let bytes = std::fs::read(&sorted_path).unwrap();
    let manifest = Manifest::parse_avro(&bytes).unwrap();
    assert_eq!(manifest.entries()[0].data_file().sort_order_id(), Some(3));
}

// ---------------------------------------------------------------------------
// g06_eq_delete_ids — spec.md "Equality Delete Files": "Delete columns are
// identified by id in the delete file metadata column `equality_ids`" and the
// column restrictions are those of "Identifier Field IDs" (primitive, not
// floating point).
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g06_eq_delete_ids__ids_round_trip_through_the_manifest() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "eq-ids.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(40),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(equality_delete_file("/tmp/eq-a.parquet", vec![1]), 7)
        .unwrap();
    writer
        .add_file(equality_delete_file("/tmp/eq-b.parquet", vec![1, 2]), 7)
        .unwrap();
    writer.write_manifest_file().await.unwrap();

    let bytes = std::fs::read(&path).unwrap();
    let manifest = Manifest::parse_avro(&bytes).unwrap();
    let mut seen: Vec<(String, Vec<i32>)> = manifest
        .entries()
        .iter()
        .map(|e| {
            (
                e.data_file().file_path().to_string(),
                e.data_file().equality_ids().to_vec(),
            )
        })
        .collect();
    seen.sort();
    assert_eq!(seen, vec![
        ("/tmp/eq-a.parquet".to_string(), vec![1]),
        ("/tmp/eq-b.parquet".to_string(), vec![1, 2]),
    ]);
}

#[test]
fn g06_eq_delete_ids__writer_config_accepts_primitive_columns() {
    use iceberg::writer::base_writer::equality_delete_writer::EqualityDeleteWriterConfig;

    let schema: SchemaRef = Arc::new(table_schema());
    let config = EqualityDeleteWriterConfig::new(vec![1, 2], schema, None, 0)
        .expect("equality ids over primitive, non-floating-point columns are valid");
    // The projected delete-file schema carries exactly the delete columns.
    assert_eq!(config.projected_arrow_schema_ref().fields().len(), 2);
}

#[test]
fn g06_eq_delete_ids__writer_config_rejects_floating_point_columns() {
    use iceberg::writer::base_writer::equality_delete_writer::EqualityDeleteWriterConfig;

    // "Identifier Field IDs": floating point columns may not be used, and field 3
    // of the table schema is a `double`.
    let schema: SchemaRef = Arc::new(table_schema());
    assert!(EqualityDeleteWriterConfig::new(vec![3], schema.clone(), None, 0).is_err());
    assert!(EqualityDeleteWriterConfig::new(vec![1, 3], schema, None, 0).is_err());
}

#[test]
fn g06_eq_delete_ids__writer_config_rejects_nested_columns() {
    use iceberg::writer::base_writer::equality_delete_writer::EqualityDeleteWriterConfig;

    // "Identifier Field IDs": a schema tracks "the set of primitive fields that
    // identify rows in a table", and identifier fields "may be nested in
    // structs" — the struct itself is never one of them. "Equality Delete
    // Files" applies the same column restrictions, so a struct column is not a
    // usable delete column even though its own sub-fields are primitive.
    // TEST_INTERFACE.md 4.6 states the rule directly.
    let schema: SchemaRef = Arc::new(schema_with_a_nested_column());
    assert!(
        EqualityDeleteWriterConfig::new(vec![4], schema.clone(), None, 0).is_err(),
        "a struct column must not be usable as an equality delete column"
    );
    assert!(
        EqualityDeleteWriterConfig::new(vec![1, 4], schema.clone(), None, 0).is_err(),
        "one nested column invalidates the whole equality id list"
    );
    // The primitive columns of the same schema are still accepted.
    assert!(EqualityDeleteWriterConfig::new(vec![1, 2], schema, None, 0).is_ok());
}

#[test]
fn g06_eq_delete_ids__writer_config_rejects_unknown_field_ids() {
    use iceberg::writer::base_writer::equality_delete_writer::EqualityDeleteWriterConfig;

    // "Equality Delete Files": "Fields with ids listed in this column must be
    // present in the delete file", so an id absent from the table schema is not
    // a usable delete column.
    let schema: SchemaRef = Arc::new(table_schema());
    assert!(EqualityDeleteWriterConfig::new(vec![99], schema, None, 0).is_err());
}

// ---------------------------------------------------------------------------
// g07_seq_inherit — spec.md "Sequence Number Inheritance": "When adding a new
// file, its data and file sequence numbers are set to `null` ... When reading,
// sequence numbers are inherited by replacing `null` with the manifest's
// sequence number from the manifest list."
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g07_seq_inherit__null_sequence_number_is_inherited_on_read() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "inherit.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(50),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    // UNASSIGNED_SEQUENCE_NUMBER means "not yet assigned"; it is written as null.
    writer
        .add_file(
            position_delete_file("/tmp/inh.parquet", "/tmp/data-inh.parquet"),
            UNASSIGNED_SEQUENCE_NUMBER,
        )
        .unwrap();
    let mut manifest_file = writer.write_manifest_file().await.unwrap();

    // Read straight from the Avro bytes: the entry carries no sequence number.
    let bytes = std::fs::read(&path).unwrap();
    let raw = Manifest::parse_avro(&bytes).unwrap();
    assert_eq!(raw.entries()[0].sequence_number(), None);
    assert_eq!(raw.entries()[0].snapshot_id(), Some(50));

    // Read through the manifest list entry: the manifest's sequence number is
    // substituted for the null.
    manifest_file.sequence_number = 9;
    manifest_file.min_sequence_number = 9;
    let loaded = manifest_file.load_manifest(&io).await.unwrap();
    assert_eq!(loaded.entries()[0].sequence_number(), Some(9));
}

#[tokio::test]
async fn g07_seq_inherit__explicit_sequence_number_is_not_overwritten() {
    // "It is also possible to add a new file with data that logically belongs to
    // an older sequence number. In that case, the data sequence number must be
    // provided explicitly and not inherited."
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "explicit.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(51),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(equality_delete_file("/tmp/eq-exp.parquet", vec![1]), 3)
        .unwrap();
    let mut manifest_file = writer.write_manifest_file().await.unwrap();
    // The manifest's own minimum live sequence number is the one supplied.
    assert_eq!(manifest_file.min_sequence_number, 3);

    manifest_file.sequence_number = 9;
    let loaded = manifest_file.load_manifest(&io).await.unwrap();
    assert_eq!(loaded.entries()[0].sequence_number(), Some(3));
}

#[tokio::test]
async fn g07_seq_inherit__delete_applies_only_to_older_data_files() {
    // "Sequence Numbers" / "Scan Planning": a delete file applies to a data file
    // only when the data file's data sequence number is smaller. Writing a data
    // manifest at sequence 4 and a delete manifest at sequence 5 through the same
    // public path must preserve that ordering after a read-back.
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);

    let data_path = path_in(&dir, "old-data.avro");
    let mut dw = ManifestWriterBuilder::new(
        io.new_output(&data_path).unwrap(),
        Some(60),
        vec![],
        schema.clone(),
        spec.clone(),
    )
    .build_v2_data();
    dw.add_file(data_file("/tmp/old.parquet"), UNASSIGNED_SEQUENCE_NUMBER)
        .unwrap();
    let mut data_manifest = dw.write_manifest_file().await.unwrap();
    data_manifest.sequence_number = 4;
    data_manifest.min_sequence_number = 4;

    let del_path = path_in(&dir, "new-del.avro");
    let mut xw = ManifestWriterBuilder::new(
        io.new_output(&del_path).unwrap(),
        Some(61),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    xw.add_file(
        position_delete_file("/tmp/new-del.parquet", "/tmp/old.parquet"),
        UNASSIGNED_SEQUENCE_NUMBER,
    )
    .unwrap();
    let mut delete_manifest = xw.write_manifest_file().await.unwrap();
    delete_manifest.sequence_number = 5;
    delete_manifest.min_sequence_number = 5;

    let data_seq = data_manifest.load_manifest(&io).await.unwrap().entries()[0]
        .sequence_number()
        .unwrap();
    let delete_seq = delete_manifest.load_manifest(&io).await.unwrap().entries()[0]
        .sequence_number()
        .unwrap();
    assert_eq!(data_seq, 4);
    assert_eq!(delete_seq, 5);
    assert!(
        data_seq < delete_seq,
        "the delete file must carry a larger data sequence number than the data it deletes"
    );
}

// ---------------------------------------------------------------------------
// g08_seq_assign — spec.md "Sequence Numbers": "All manifests, data files, and
// delete files created for a snapshot inherit the snapshot's sequence number.
// Manifest file metadata in the manifest list stores a manifest's sequence
// number."
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g08_seq_assign__unassigned_manifest_takes_the_snapshot_sequence_number() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);

    let del_path = path_in(&dir, "assign-del.avro");
    let mut xw = ManifestWriterBuilder::new(
        io.new_output(&del_path).unwrap(),
        Some(70),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    xw.add_file(
        equality_delete_file("/tmp/eq-assign.parquet", vec![1]),
        UNASSIGNED_SEQUENCE_NUMBER,
    )
    .unwrap();
    let manifest = xw.write_manifest_file().await.unwrap();
    assert_eq!(manifest.sequence_number, UNASSIGNED_SEQUENCE_NUMBER);
    assert_eq!(manifest.min_sequence_number, UNASSIGNED_SEQUENCE_NUMBER);

    let list_path = path_in(&dir, "assign-ml.avro");
    let mut lw = ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 70, None, 12);
    lw.add_manifests(vec![manifest].into_iter()).unwrap();
    lw.close().await.unwrap();

    let bytes = std::fs::read(&list_path).unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2, |_id| Ok(None)).unwrap();
    assert_eq!(list.entries()[0].sequence_number, 12);
    assert_eq!(list.entries()[0].min_sequence_number, 12);
    assert_eq!(list.entries()[0].content, ManifestContentType::Deletes);
}

#[tokio::test]
async fn g08_seq_assign__manifest_from_another_snapshot_must_carry_its_number() {
    // Only a manifest added by *this* snapshot may take the snapshot's sequence
    // number; an inherited manifest must already carry its own.
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);

    let del_path = path_in(&dir, "foreign-del.avro");
    let mut xw = ManifestWriterBuilder::new(
        io.new_output(&del_path).unwrap(),
        Some(80),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    xw.add_file(
        equality_delete_file("/tmp/eq-foreign.parquet", vec![1]),
        UNASSIGNED_SEQUENCE_NUMBER,
    )
    .unwrap();
    let manifest = xw.write_manifest_file().await.unwrap();
    assert_eq!(manifest.added_snapshot_id, 80);

    // The manifest list belongs to snapshot 81, not 80.
    let list_path = path_in(&dir, "foreign-ml.avro");
    let mut lw = ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 81, Some(80), 13);
    assert!(
        lw.add_manifests(vec![manifest].into_iter()).is_err(),
        "an unassigned sequence number from a foreign snapshot must be rejected"
    );
}

#[tokio::test]
async fn g08_seq_assign__min_sequence_number_is_the_smallest_live_entry() {
    // "Manifest Lists" field 516: the minimum data sequence number of all live
    // data or delete files in the manifest.
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "minseq.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(90),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(equality_delete_file("/tmp/eq-hi.parquet", vec![1]), 8)
        .unwrap();
    writer
        .add_file(equality_delete_file("/tmp/eq-lo.parquet", vec![1]), 2)
        .unwrap();
    writer
        .add_file(equality_delete_file("/tmp/eq-mid.parquet", vec![1]), 5)
        .unwrap();
    let manifest = writer.write_manifest_file().await.unwrap();
    assert_eq!(manifest.min_sequence_number, 2);
    assert_eq!(manifest.added_files_count, Some(3));
    assert_eq!(manifest.added_rows_count, Some(6));

    // A manifest that mixes an entry still waiting to inherit its number with an
    // explicitly numbered one takes the minimum over the numbered entries only:
    // a null data sequence number does not contribute (TEST_INTERFACE.md 4.3).
    let mixed_path = path_in(&dir, "mixed-min.avro");
    let mut mixed = ManifestWriterBuilder::new(
        io.new_output(&mixed_path).unwrap(),
        Some(61),
        vec![],
        schema.clone(),
        unpartitioned_spec(&schema),
    )
    .build_v2_deletes();
    mixed
        .add_file(
            equality_delete_file("/tmp/eq-null.parquet", vec![1]),
            UNASSIGNED_SEQUENCE_NUMBER,
        )
        .unwrap();
    mixed
        .add_file(equality_delete_file("/tmp/eq-five.parquet", vec![1]), 5)
        .unwrap();
    let mixed_manifest = mixed.write_manifest_file().await.unwrap();
    assert_eq!(mixed_manifest.min_sequence_number, 5);
    assert_eq!(mixed_manifest.added_files_count, Some(2));
}

#[tokio::test]
async fn g08_seq_assign__writer_without_a_snapshot_id_is_unassigned() {
    // "Manifest Lists" field 503 is the snapshot the manifest was added in.
    // `ManifestWriterBuilder::new` takes `Option<i64>`, so a writer built
    // without one reports `UNASSIGNED_SNAPSHOT_ID` and leaves the entries'
    // snapshot id null for inheritance at read time.
    //
    // TEST_INTERFACE.md 4.1 pins the values of the three constants this
    // milestone declares; two of them are load-bearing here and the third names
    // the default branch the metadata-update algebra writes refs under.
    assert_eq!(UNASSIGNED_SNAPSHOT_ID, -1);
    assert_eq!(UNASSIGNED_SEQUENCE_NUMBER, -1);
    assert_eq!(MAIN_BRANCH, "main");

    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "no-snapshot.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        None,
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(
            equality_delete_file("/tmp/eq-anon.parquet", vec![1]),
            UNASSIGNED_SEQUENCE_NUMBER,
        )
        .unwrap();
    let manifest = writer.write_manifest_file().await.unwrap();
    assert_eq!(manifest.added_snapshot_id, UNASSIGNED_SNAPSHOT_ID);

    let bytes = std::fs::read(&path).unwrap();
    let raw = Manifest::parse_avro(&bytes).unwrap();
    assert_eq!(raw.entries()[0].snapshot_id(), None);
}

// ---------------------------------------------------------------------------
// g09_delete_stats — spec.md "Delete File Stats": "Manifests hold the same
// statistics for delete files and data files. For delete files, the metrics
// describe the values that were deleted."
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g09_delete_stats__metrics_round_trip_for_a_delete_file() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "stats.avro");

    let delete_file = DataFileBuilder::default()
        .content(DataContentType::EqualityDeletes)
        .file_path("/tmp/eq-stats.parquet".to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .record_count(4)
        .file_size_in_bytes(900)
        .partition_spec_id(0)
        .equality_ids(vec![1])
        .column_sizes(HashMap::from([(1, 64u64), (2, 128u64)]))
        .value_counts(HashMap::from([(1, 4u64), (2, 4u64)]))
        .null_value_counts(HashMap::from([(2, 1u64)]))
        .nan_value_counts(HashMap::from([(3, 2u64)]))
        .lower_bounds(HashMap::from([(1, Datum::long(1))]))
        .upper_bounds(HashMap::from([(1, Datum::long(9))]))
        .split_offsets(vec![4])
        .build()
        .unwrap();

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(100),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer.add_file(delete_file, 6).unwrap();
    writer.write_manifest_file().await.unwrap();

    let bytes = std::fs::read(&path).unwrap();
    let manifest = Manifest::parse_avro(&bytes).unwrap();
    let df = manifest.entries()[0].data_file();
    assert_eq!(df.column_sizes().get(&1), Some(&64));
    assert_eq!(df.column_sizes().get(&2), Some(&128));
    assert_eq!(df.value_counts().get(&1), Some(&4));
    assert_eq!(df.null_value_counts().get(&2), Some(&1));
    assert_eq!(df.nan_value_counts().get(&3), Some(&2));
    assert_eq!(df.lower_bounds().get(&1), Some(&Datum::long(1)));
    assert_eq!(df.upper_bounds().get(&1), Some(&Datum::long(9)));
    assert_eq!(df.split_offsets(), &[4]);
    // The required non-stats fields survive the same round trip, read off the
    // `DataFile` itself rather than through `ManifestEntry`'s delegates
    // (TEST_INTERFACE.md 4.2's accessor list).
    assert_eq!(df.file_format(), DataFileFormat::Parquet);
    assert_eq!(df.file_size_in_bytes(), 900);
    // `key_metadata` was never set, and TEST_INTERFACE.md 4.2 gives every
    // optional field its type's own empty default; section 8 keeps its
    // *contents* out of scope, so only the default is asserted here.
    assert_eq!(df.key_metadata(), None);
}

#[tokio::test]
async fn g09_delete_stats__delete_manifest_counts_files_and_rows() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "counts.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(101),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(
            position_delete_file("/tmp/pd-c.parquet", "/tmp/d-c.parquet"),
            6,
        )
        .unwrap();
    writer
        .add_file(equality_delete_file("/tmp/ed-c.parquet", vec![1]), 6)
        .unwrap();
    let manifest = writer.write_manifest_file().await.unwrap();

    assert_eq!(manifest.content, ManifestContentType::Deletes);
    assert_eq!(manifest.added_files_count, Some(2));
    assert_eq!(manifest.existing_files_count, Some(0));
    assert_eq!(manifest.deleted_files_count, Some(0));
    // 3 position deletes + 2 equality deletes.
    assert_eq!(manifest.added_rows_count, Some(5));
    assert!(manifest.has_added_files());
    assert!(!manifest.has_deleted_files());
}

// ---------------------------------------------------------------------------
// g10_puffin_layout — puffin-spec.md "Format specification", "File structure",
// "Footer structure": `Magic Blob₁ ... Blobₙ Footer`, and
// `Footer = Magic FooterPayload FooterPayloadSize Flags Magic`.
// ---------------------------------------------------------------------------

/// puffin-spec.md "File structure": four bytes 0x50, 0x46, 0x41, 0x31.
const PUFFIN_MAGIC: [u8; 4] = [0x50, 0x46, 0x41, 0x31];

fn sample_blob(blob_type: &str, data: Vec<u8>) -> iceberg::puffin::Blob {
    iceberg::puffin::Blob::builder()
        .r#type(blob_type.to_string())
        .fields(vec![1])
        .snapshot_id(3)
        .sequence_number(2)
        .data(data)
        .properties(HashMap::new())
        .build()
}

async fn write_puffin(
    io: &FileIO,
    path: &str,
    blobs: Vec<(iceberg::puffin::Blob, iceberg::puffin::CompressionCodec)>,
    properties: HashMap<String, String>,
) {
    let output = io.new_output(path).unwrap();
    let mut writer = iceberg::puffin::PuffinWriter::new(&output, properties, false)
        .await
        .unwrap();
    for (blob, codec) in blobs {
        writer.add(blob, codec).await.unwrap();
    }
    writer.close().await.unwrap();
}

#[tokio::test]
async fn g10_puffin_layout__magic_at_both_ends() {
    let (dir, io) = temp_io();
    let path = path_in(&dir, "magic.puffin");
    write_puffin(
        &io,
        &path,
        vec![(
            sample_blob(iceberg::puffin::DELETION_VECTOR_V1, vec![1, 2, 3, 4]),
            iceberg::puffin::CompressionCodec::None,
        )],
        HashMap::new(),
    )
    .await;

    let bytes = std::fs::read(&path).unwrap();
    assert_eq!(&bytes[..4], &PUFFIN_MAGIC, "file must start with PFA1");
    assert_eq!(
        &bytes[bytes.len() - 4..],
        &PUFFIN_MAGIC,
        "file must end with PFA1"
    );
    assert_eq!(&bytes[..4], b"PFA1");
}

#[tokio::test]
async fn g10_puffin_layout__footer_struct_fields() {
    let (dir, io) = temp_io();
    let path = path_in(&dir, "footer.puffin");
    write_puffin(
        &io,
        &path,
        vec![(
            sample_blob(iceberg::puffin::APACHE_DATASKETCHES_THETA_V1, vec![7; 16]),
            iceberg::puffin::CompressionCodec::None,
        )],
        HashMap::from([(
            iceberg::puffin::CREATED_BY_PROPERTY.to_string(),
            "harness".to_string(),
        )]),
    )
    .await;

    let bytes = std::fs::read(&path).unwrap();
    let len = bytes.len();
    // FOOTER_STRUCT = FooterPayloadSize(4) + Flags(4) + Magic(4).
    let payload_size =
        u32::from_le_bytes([bytes[len - 12], bytes[len - 11], bytes[len - 10], bytes[len - 9]])
            as usize;
    // Flags: all bits reserved and written as 0 when the payload is uncompressed.
    assert_eq!(&bytes[len - 8..len - 4], &[0u8, 0, 0, 0]);
    // The footer starts with the magic, then the payload of the recorded size.
    let footer_start = len - 12 - payload_size - 4;
    assert_eq!(&bytes[footer_start..footer_start + 4], &PUFFIN_MAGIC);
    let payload = &bytes[footer_start + 4..footer_start + 4 + payload_size];
    let json: serde_json::Value = serde_json::from_slice(payload).unwrap();
    assert!(json["blobs"].is_array());
    assert_eq!(json["properties"]["created-by"], "harness");

    let metadata = iceberg::puffin::PuffinReader::new(io.new_input(&path).unwrap())
        .file_metadata()
        .await
        .unwrap()
        .clone();
    assert_eq!(
        metadata.properties().get(iceberg::puffin::CREATED_BY_PROPERTY),
        Some(&"harness".to_string())
    );
    // "File structure": the first blob starts immediately after the leading magic.
    assert_eq!(metadata.blobs()[0].offset(), 4);
    assert_eq!(metadata.blobs()[0].length(), 16);
}

#[tokio::test]
async fn g10_puffin_layout__file_without_blobs() {
    let (dir, io) = temp_io();
    let path = path_in(&dir, "empty.puffin");
    write_puffin(&io, &path, vec![], HashMap::new()).await;

    let bytes = std::fs::read(&path).unwrap();
    assert_eq!(&bytes[..4], &PUFFIN_MAGIC);
    assert_eq!(&bytes[bytes.len() - 4..], &PUFFIN_MAGIC);

    // The omit-when-empty rule applies to `FileMetadata` as well as to
    // `BlobMetadata`: an empty `blobs` array is written, and with no properties
    // there is no `properties` member at all.
    let len = bytes.len();
    let payload_size =
        u32::from_le_bytes([bytes[len - 12], bytes[len - 11], bytes[len - 10], bytes[len - 9]])
            as usize;
    let footer_start = len - 12 - payload_size - 4;
    let payload: serde_json::Value =
        serde_json::from_slice(&bytes[footer_start + 4..footer_start + 4 + payload_size]).unwrap();
    assert_eq!(payload["blobs"].as_array().map(|a| a.len()), Some(0));
    assert!(
        payload.get("properties").is_none(),
        "an empty `properties` map is omitted from the footer payload"
    );

    let metadata = iceberg::puffin::PuffinReader::new(io.new_input(&path).unwrap())
        .file_metadata()
        .await
        .unwrap()
        .clone();
    assert!(metadata.blobs().is_empty());
    assert!(metadata.properties().is_empty());
    // The same value is constructible directly (TEST_INTERFACE.md 4.7's
    // `FileMetadata::new`), which is what "still a valid Puffin file" means.
    assert_eq!(
        metadata,
        iceberg::puffin::FileMetadata::new(vec![], HashMap::new())
    );
}

// ---------------------------------------------------------------------------
// g11_puffin_blob — puffin-spec.md "BlobMetadata" and "Blob types".
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g11_puffin_blob__round_trips_through_reader() {
    let (dir, io) = temp_io();
    let path = path_in(&dir, "blobs.puffin");
    let payload_a: Vec<u8> = (0u8..32).collect();
    let payload_b: Vec<u8> = vec![9; 8];
    write_puffin(
        &io,
        &path,
        vec![
            (
                sample_blob(iceberg::puffin::DELETION_VECTOR_V1, payload_a.clone()),
                iceberg::puffin::CompressionCodec::None,
            ),
            (
                sample_blob(
                    iceberg::puffin::APACHE_DATASKETCHES_THETA_V1,
                    payload_b.clone(),
                ),
                iceberg::puffin::CompressionCodec::None,
            ),
        ],
        HashMap::new(),
    )
    .await;

    let reader = iceberg::puffin::PuffinReader::new(io.new_input(&path).unwrap());
    let metadata = reader.file_metadata().await.unwrap().clone();
    assert_eq!(metadata.blobs().len(), 2);

    let first = &metadata.blobs()[0];
    assert_eq!(first.blob_type(), "deletion-vector-v1");
    assert_eq!(first.fields(), &[1]);
    assert_eq!(first.snapshot_id(), 3);
    assert_eq!(first.sequence_number(), 2);
    assert_eq!(first.length(), payload_a.len() as u64);

    let second = &metadata.blobs()[1];
    assert_eq!(second.blob_type(), "apache-datasketches-theta-v1");
    // Blobs are laid out back to back after the leading magic.
    assert_eq!(second.offset(), first.offset() + first.length());

    let blob_a = reader.blob(first).await.unwrap();
    assert_eq!(blob_a.data(), payload_a.as_slice());
    assert_eq!(blob_a.blob_type(), iceberg::puffin::DELETION_VECTOR_V1);
    assert_eq!(blob_a.snapshot_id(), 3);
    assert_eq!(blob_a.sequence_number(), 2);
    // 4.7: all five of the returned `Blob`'s non-payload fields come from the
    // `BlobMetadata`, `fields` and `properties` included.
    assert_eq!(blob_a.fields(), &[1]);
    assert!(blob_a.properties().is_empty());
    let blob_b = reader.blob(second).await.unwrap();
    assert_eq!(blob_b.data(), payload_b.as_slice());
}

#[tokio::test]
async fn g11_puffin_blob__footer_payload_uses_the_documented_json_names() {
    let (dir, io) = temp_io();
    let path = path_in(&dir, "names.puffin");
    let blob = iceberg::puffin::Blob::builder()
        .r#type(iceberg::puffin::DELETION_VECTOR_V1.to_string())
        .fields(vec![2, 5])
        .snapshot_id(11)
        .sequence_number(4)
        .data(vec![1, 2, 3])
        .properties(HashMap::from([("ndv".to_string(), "3".to_string())]))
        .build();
    write_puffin(
        &io,
        &path,
        vec![(blob, iceberg::puffin::CompressionCodec::None)],
        HashMap::new(),
    )
    .await;

    let bytes = std::fs::read(&path).unwrap();
    let len = bytes.len();
    let payload_size =
        u32::from_le_bytes([bytes[len - 12], bytes[len - 11], bytes[len - 10], bytes[len - 9]])
            as usize;
    let start = len - 12 - payload_size;
    let json: serde_json::Value = serde_json::from_slice(&bytes[start..len - 12]).unwrap();
    let b = &json["blobs"][0];
    assert_eq!(b["type"], "deletion-vector-v1");
    assert_eq!(b["fields"], serde_json::json!([2, 5]));
    assert_eq!(b["snapshot-id"], 11);
    assert_eq!(b["sequence-number"], 4);
    assert_eq!(b["offset"], 4);
    assert_eq!(b["length"], 3);
    assert_eq!(b["properties"]["ndv"], "3");
    // "compression-codec ... If omitted, the data is assumed to be uncompressed."
    assert!(b.get("compression-codec").is_none());
}

#[test]
fn g11_puffin_blob__documented_blob_type_names() {
    // puffin-spec.md "Blob types".
    assert_eq!(iceberg::puffin::DELETION_VECTOR_V1, "deletion-vector-v1");
    assert_eq!(
        iceberg::puffin::APACHE_DATASKETCHES_THETA_V1,
        "apache-datasketches-theta-v1"
    );
    // puffin-spec.md "Common properties".
    assert_eq!(iceberg::puffin::CREATED_BY_PROPERTY, "created-by");
}

// ---------------------------------------------------------------------------
// g12_puffin_codec — puffin-spec.md "Compression codecs" and the
// `compression-codec` field of "BlobMetadata".
// ---------------------------------------------------------------------------

#[test]
fn g12_puffin_codec__json_names() {
    use iceberg::puffin::CompressionCodec;
    assert_eq!(serde_json::to_string(&CompressionCodec::None).unwrap(), "\"none\"");
    assert_eq!(serde_json::to_string(&CompressionCodec::Lz4).unwrap(), "\"lz4\"");
    assert_eq!(serde_json::to_string(&CompressionCodec::Zstd).unwrap(), "\"zstd\"");
    assert_eq!(
        serde_json::from_str::<CompressionCodec>("\"zstd\"").unwrap(),
        CompressionCodec::Zstd
    );
    assert_eq!(CompressionCodec::default(), CompressionCodec::None);
    assert!(serde_json::from_str::<CompressionCodec>("\"gzip\"").is_err());
}

#[tokio::test]
async fn g12_puffin_codec__uncompressed_blob_is_stored_verbatim() {
    let (dir, io) = temp_io();
    let path = path_in(&dir, "plain.puffin");
    let payload: Vec<u8> = (0u8..64).collect();
    write_puffin(
        &io,
        &path,
        vec![(
            sample_blob(iceberg::puffin::DELETION_VECTOR_V1, payload.clone()),
            iceberg::puffin::CompressionCodec::None,
        )],
        HashMap::new(),
    )
    .await;

    let reader = iceberg::puffin::PuffinReader::new(io.new_input(&path).unwrap());
    let metadata = reader.file_metadata().await.unwrap().clone();
    let bm = &metadata.blobs()[0];
    assert_eq!(bm.compression_codec(), iceberg::puffin::CompressionCodec::None);
    assert_eq!(bm.length(), payload.len() as u64);

    let raw = std::fs::read(&path).unwrap();
    let start = bm.offset() as usize;
    assert_eq!(&raw[start..start + payload.len()], payload.as_slice());
    assert_eq!(reader.blob(bm).await.unwrap().data(), payload.as_slice());
}

#[tokio::test]
async fn g12_puffin_codec__zstd_blob_round_trips() {
    let (dir, io) = temp_io();
    let path = path_in(&dir, "zstd.puffin");
    // Highly compressible payload so that the stored length must differ.
    let payload: Vec<u8> = vec![0u8; 4096];
    write_puffin(
        &io,
        &path,
        vec![(
            sample_blob(iceberg::puffin::DELETION_VECTOR_V1, payload.clone()),
            iceberg::puffin::CompressionCodec::Zstd,
        )],
        HashMap::new(),
    )
    .await;

    let reader = iceberg::puffin::PuffinReader::new(io.new_input(&path).unwrap());
    let metadata = reader.file_metadata().await.unwrap().clone();
    let bm = &metadata.blobs()[0];
    assert_eq!(bm.compression_codec(), iceberg::puffin::CompressionCodec::Zstd);
    // "length: the length of the blob stored in the file (after compression)".
    assert!(bm.length() < payload.len() as u64);
    assert_eq!(reader.blob(bm).await.unwrap().data(), payload.as_slice());

    // The codec is recorded in the footer payload.
    let bytes = std::fs::read(&path).unwrap();
    let len = bytes.len();
    let payload_size =
        u32::from_le_bytes([bytes[len - 12], bytes[len - 11], bytes[len - 10], bytes[len - 9]])
            as usize;
    let json: serde_json::Value =
        serde_json::from_slice(&bytes[len - 12 - payload_size..len - 12]).unwrap();
    assert_eq!(json["blobs"][0]["compression-codec"], "zstd");
}

// ---------------------------------------------------------------------------
// g13_puffin_reject — puffin-spec.md "File structure" / "Footer structure":
// the magic must be present at the start of the file and at both ends of the
// footer, and the footer payload must be a JSON `FileMetadata` object.
// ---------------------------------------------------------------------------

async fn corrupted_puffin(dir: &tempfile::TempDir, io: &FileIO, name: &str, mutate: impl Fn(&mut Vec<u8>)) -> String {
    let good = path_in(dir, &format!("good-{name}"));
    write_puffin(
        io,
        &good,
        vec![(
            sample_blob(iceberg::puffin::DELETION_VECTOR_V1, vec![1, 2, 3, 4, 5, 6, 7, 8]),
            iceberg::puffin::CompressionCodec::None,
        )],
        HashMap::new(),
    )
    .await;
    let mut bytes = std::fs::read(&good).unwrap();
    mutate(&mut bytes);
    let bad = path_in(dir, &format!("bad-{name}"));
    std::fs::write(&bad, &bytes).unwrap();
    bad
}

#[tokio::test]
async fn g13_puffin_reject__leading_magic_must_match() {
    let (dir, io) = temp_io();
    let bad = corrupted_puffin(&dir, &io, "head", |b| b[0] = b'X').await;
    let reader = iceberg::puffin::PuffinReader::new(io.new_input(&bad).unwrap());
    assert!(
        reader.file_metadata().await.is_err(),
        "a file that does not begin with PFA1 must be rejected"
    );
}

#[tokio::test]
async fn g13_puffin_reject__trailing_magic_must_match() {
    let (dir, io) = temp_io();
    let bad = corrupted_puffin(&dir, &io, "tail", |b| {
        let n = b.len();
        b[n - 1] = b'X';
    })
    .await;
    let reader = iceberg::puffin::PuffinReader::new(io.new_input(&bad).unwrap());
    assert!(
        reader.file_metadata().await.is_err(),
        "a footer that does not end with PFA1 must be rejected"
    );
}

#[tokio::test]
async fn g13_puffin_reject__footer_payload_must_be_json() {
    let (dir, io) = temp_io();
    // The last byte of the footer payload sits immediately before FOOTER_STRUCT.
    let bad = corrupted_puffin(&dir, &io, "payload", |b| {
        let n = b.len();
        b[n - 13] = b'X';
    })
    .await;
    let reader = iceberg::puffin::PuffinReader::new(io.new_input(&bad).unwrap());
    assert!(
        reader.file_metadata().await.is_err(),
        "a footer payload that is not a JSON FileMetadata object must be rejected"
    );
}

// ---------------------------------------------------------------------------
// g14_inspect — metadata tables. Row and column shapes trace to spec.md
// "Snapshots" (snapshot fields) and "Manifest Lists" (manifest_file fields);
// the Rust surface is stated by TEST_INTERFACE.md.
// ---------------------------------------------------------------------------

const INSPECT_METADATA: &str = r#"{
  "format-version": 2,
  "table-uuid": "9c12d441-03fe-4693-9a96-a0705ddf69c1",
  "location": "file:///tmp/inspect/table",
  "last-sequence-number": 3,
  "last-updated-ms": 1602638573590,
  "last-column-id": 3,
  "current-schema-id": 0,
  "schemas": [{"type": "struct", "schema-id": 0, "fields": [
    {"id": 1, "name": "id", "required": true, "type": "long"},
    {"id": 2, "name": "category", "required": false, "type": "string"},
    {"id": 3, "name": "score", "required": false, "type": "double"}
  ]}],
  "default-spec-id": 0,
  "partition-specs": [{"spec-id": 0, "fields": []}],
  "last-partition-id": 999,
  "default-sort-order-id": 0,
  "sort-orders": [{"order-id": 0, "fields": []}],
  "properties": {},
  "current-snapshot-id": 3055729675574597004,
  "snapshots": [
    {"snapshot-id": 3055729675574597004, "timestamp-ms": 1555100955770, "sequence-number": 3,
     "summary": {"operation": "overwrite", "added-position-delete-files": "1"},
     "manifest-list": "file:///tmp/inspect/ml.avro", "schema-id": 0}
  ],
  "snapshot-log": [{"snapshot-id": 3055729675574597004, "timestamp-ms": 1555100955770}],
  "metadata-log": []
}"#;

fn inspect_table() -> iceberg::table::Table {
    let metadata: TableMetadata = serde_json::from_str(INSPECT_METADATA).unwrap();
    iceberg::table::Table::builder()
        .identifier(iceberg::TableIdent::from_strs(["ns", "inspect"]).unwrap())
        .file_io(FileIOBuilder::new_fs_io().build().unwrap())
        .metadata(metadata)
        .build()
        .unwrap()
}

#[test]
fn g14_inspect__snapshots_table_schema() {
    let table = inspect_table();
    let schema = table.inspect().snapshots().schema();
    let names: Vec<&str> = schema
        .as_struct()
        .fields()
        .iter()
        .map(|f| f.name.as_str())
        .collect();
    assert_eq!(names, vec![
        "committed_at",
        "snapshot_id",
        "parent_id",
        "operation",
        "manifest_list",
        "summary",
    ]);
    // The ids are part of the fixed schema, not just the names and the order.
    let ids: Vec<i32> = schema.as_struct().fields().iter().map(|f| f.id).collect();
    assert_eq!(ids, vec![1, 2, 3, 4, 5, 6]);
    // The map's key element is field 7 (string, required) and its value element
    // field 8 (string, optional).
    let key = schema.field_by_id(7).expect("the summary map's key element");
    assert_eq!(
        key.field_type.as_ref(),
        &Type::Primitive(PrimitiveType::String)
    );
    assert!(key.required);
    let value = schema.field_by_id(8).expect("the summary map's value element");
    assert_eq!(
        value.field_type.as_ref(),
        &Type::Primitive(PrimitiveType::String)
    );
    assert!(!value.required);
    assert_eq!(
        schema.field_by_name("committed_at").unwrap().field_type.as_ref(),
        &Type::Primitive(PrimitiveType::Timestamptz)
    );
    assert_eq!(
        schema.field_by_name("snapshot_id").unwrap().field_type.as_ref(),
        &Type::Primitive(PrimitiveType::Long)
    );
    assert!(schema.field_by_name("snapshot_id").unwrap().required);
    assert!(!schema.field_by_name("parent_id").unwrap().required);
    assert!(matches!(
        schema.field_by_name("summary").unwrap().field_type.as_ref(),
        Type::Map(_)
    ));
}

#[test]
fn g14_inspect__manifests_table_schema() {
    let table = inspect_table();
    let schema = table.inspect().manifests().schema();
    for name in [
        "content",
        "path",
        "length",
        "partition_spec_id",
        "added_snapshot_id",
        "added_data_files_count",
        "existing_data_files_count",
        "deleted_data_files_count",
        "added_delete_files_count",
        "existing_delete_files_count",
        "deleted_delete_files_count",
        "partition_summaries",
    ] {
        assert!(
            schema.field_by_name(name).is_some(),
            "the manifests metadata table must expose {name}"
        );
    }
    assert_eq!(
        schema.field_by_name("path").unwrap().field_type.as_ref(),
        &Type::Primitive(PrimitiveType::String)
    );
    assert_eq!(
        schema.field_by_name("content").unwrap().field_type.as_ref(),
        &Type::Primitive(PrimitiveType::Int)
    );
    assert!(matches!(
        schema
            .field_by_name("partition_summaries")
            .unwrap()
            .field_type
            .as_ref(),
        Type::List(_)
    ));

    // Declaration order and the ids that go with it: `content` comes first but
    // carries id 14, so the ids are deliberately not ascending.
    let declared: Vec<(i32, &str)> = schema
        .as_struct()
        .fields()
        .iter()
        .map(|f| (f.id, f.name.as_str()))
        .collect();
    assert_eq!(declared, vec![
        (14, "content"),
        (1, "path"),
        (2, "length"),
        (3, "partition_spec_id"),
        (4, "added_snapshot_id"),
        (5, "added_data_files_count"),
        (6, "existing_data_files_count"),
        (7, "deleted_data_files_count"),
        (15, "added_delete_files_count"),
        (16, "existing_delete_files_count"),
        (17, "deleted_delete_files_count"),
        (8, "partition_summaries"),
    ]);
    for f in schema.as_struct().fields() {
        assert!(f.required, "every manifests-table column is required: {}", f.name);
    }

    // The list element is field 9, named `item`, required, and a struct of
    // fields 10-13.
    let item = schema.field_by_id(9).expect("the partition_summaries element");
    assert_eq!(item.name, "item");
    assert!(item.required);
    assert!(matches!(item.field_type.as_ref(), Type::Struct(_)));
    for (id, name, required, ty) in [
        (10, "contains_null", true, PrimitiveType::Boolean),
        (11, "contains_nan", false, PrimitiveType::Boolean),
        (12, "lower_bound", false, PrimitiveType::String),
        (13, "upper_bound", false, PrimitiveType::String),
    ] {
        let f = schema
            .field_by_id(id)
            .unwrap_or_else(|| panic!("partition summary field {id} ({name})"));
        assert_eq!(f.name, name);
        assert_eq!(f.required, required);
        assert_eq!(f.field_type.as_ref(), &Type::Primitive(ty));
    }
}

#[test]
fn g14_inspect__tables_describe_the_tables_own_snapshot() {
    let table = inspect_table();
    // The metadata tables are views over the table they are built from.
    assert_eq!(table.metadata().snapshots().count(), 1);
    let snapshot = table.metadata().current_snapshot().unwrap();
    assert_eq!(snapshot.snapshot_id(), 3055729675574597004);
    assert_eq!(snapshot.sequence_number(), 3);
    assert_eq!(snapshot.summary().operation, Operation::Overwrite);
    assert_eq!(
        snapshot.summary().additional_properties.get("added-position-delete-files"),
        Some(&"1".to_string())
    );
    // Two independently created metadata tables describe the same shape, and so
    // do the two tables' own direct constructors: they are views over the table
    // and hold no state of their own.
    let a = table.inspect().snapshots().schema();
    let b = iceberg::inspect::MetadataTable::new(&table).snapshots().schema();
    let c = iceberg::inspect::SnapshotsTable::new(&table).schema();
    assert_eq!(a.as_struct(), b.as_struct());
    assert_eq!(a.as_struct(), c.as_struct());
    let m = table.inspect().manifests().schema();
    let n = iceberg::inspect::ManifestsTable::new(&table).schema();
    assert_eq!(m.as_struct(), n.as_struct());
}

// ---------------------------------------------------------------------------
// g15_cache — the manifest/metadata object cache. Manifests are immutable
// (spec.md "File System Operations": "Files are not moved or altered once they
// are written"), so a cached read must equal an uncached one.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g15_cache__manifest_read_twice_is_equal() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "cached.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(200),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(equality_delete_file("/tmp/eq-cache.parquet", vec![1]), 3)
        .unwrap();
    let mut manifest_file = writer.write_manifest_file().await.unwrap();
    manifest_file.sequence_number = 3;
    manifest_file.min_sequence_number = 3;

    let first = manifest_file.load_manifest(&io).await.unwrap();
    let second = manifest_file.load_manifest(&io).await.unwrap();
    assert_eq!(first, second);
    assert_eq!(first.entries().len(), second.entries().len());
    assert_eq!(
        first.entries()[0].data_file().file_path(),
        second.entries()[0].data_file().file_path()
    );
}

#[tokio::test]
async fn g15_cache__object_cache_traits_are_implementable() {
    use std::sync::Mutex;

    use iceberg::cache::{ObjectCache, ObjectCacheProvide, ObjectCacheProvider};

    struct MapCache<V> {
        inner: Mutex<HashMap<String, V>>,
    }
    impl<V> Default for MapCache<V> {
        fn default() -> Self {
            Self {
                inner: Mutex::new(HashMap::new()),
            }
        }
    }
    impl<V: Clone + Send + Sync> ObjectCache<String, V> for MapCache<V> {
        fn get(&self, key: &String) -> Option<V> {
            self.inner.lock().unwrap().get(key).cloned()
        }
        fn set(&self, key: String, value: V) {
            self.inner.lock().unwrap().insert(key, value);
        }
    }

    #[derive(Default)]
    struct Provider {
        manifests: MapCache<Arc<Manifest>>,
        manifest_lists: MapCache<Arc<ManifestList>>,
    }

    impl ObjectCacheProvide for Provider {
        fn manifest_cache(&self) -> &dyn ObjectCache<String, Arc<Manifest>> {
            &self.manifests
        }
        fn manifest_list_cache(&self) -> &dyn ObjectCache<String, Arc<ManifestList>> {
            &self.manifest_lists
        }
    }

    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "provider.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(201),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(
            position_delete_file("/tmp/pd-cache.parquet", "/tmp/d-cache.parquet"),
            3,
        )
        .unwrap();
    let mut manifest_file = writer.write_manifest_file().await.unwrap();
    manifest_file.sequence_number = 3;
    manifest_file.min_sequence_number = 3;
    let manifest = Arc::new(manifest_file.load_manifest(&io).await.unwrap());

    let provider: ObjectCacheProvider = Arc::new(Provider::default());
    assert!(provider.manifest_cache().get(&path).is_none());
    provider.manifest_cache().set(path.clone(), manifest.clone());
    let cached = provider.manifest_cache().get(&path).expect("cached manifest");
    assert_eq!(cached.as_ref(), manifest.as_ref());
    assert!(provider.manifest_list_cache().get(&path).is_none());
}

#[tokio::test]
async fn g15_cache__table_can_be_built_with_or_without_a_cache() {
    let metadata: TableMetadata = serde_json::from_str(INSPECT_METADATA).unwrap();
    let ident = iceberg::TableIdent::from_strs(["ns", "cache"]).unwrap();

    let default_cache = iceberg::table::Table::builder()
        .identifier(ident.clone())
        .file_io(FileIOBuilder::new_fs_io().build().unwrap())
        .metadata(metadata.clone())
        .build()
        .unwrap();
    let disabled = iceberg::table::Table::builder()
        .identifier(ident.clone())
        .file_io(FileIOBuilder::new_fs_io().build().unwrap())
        .metadata(metadata.clone())
        .disable_cache()
        .build()
        .unwrap();
    let sized = iceberg::table::Table::builder()
        .identifier(ident)
        .file_io(FileIOBuilder::new_fs_io().build().unwrap())
        .metadata(metadata)
        .cache_size_bytes(1024 * 1024)
        .build()
        .unwrap();

    // Cache configuration never changes what the table reports.
    assert_eq!(default_cache.metadata(), disabled.metadata());
    assert_eq!(default_cache.metadata(), sized.metadata());
    assert_eq!(
        disabled.metadata().current_snapshot().unwrap().snapshot_id(),
        3055729675574597004
    );
}

#[test]
fn g15_cache__table_builder_requires_file_io_metadata_and_identifier() {
    let metadata: TableMetadata = serde_json::from_str(INSPECT_METADATA).unwrap();
    let ident = iceberg::TableIdent::from_strs(["ns", "required"]).unwrap();

    assert!(
        iceberg::table::Table::builder()
            .identifier(ident.clone())
            .metadata(metadata.clone())
            .build()
            .is_err(),
        "a table built without a FileIO must be an error"
    );
    assert!(
        iceberg::table::Table::builder()
            .identifier(ident.clone())
            .file_io(FileIOBuilder::new_fs_io().build().unwrap())
            .build()
            .is_err(),
        "a table built without metadata must be an error"
    );
    assert!(
        iceberg::table::Table::builder()
            .file_io(FileIOBuilder::new_fs_io().build().unwrap())
            .metadata(metadata.clone())
            .build()
            .is_err(),
        "a table built without an identifier must be an error"
    );
    // `metadata_location` is the optional one: the three required setters are
    // enough on their own, and supplying it is also fine.
    iceberg::table::Table::builder()
        .identifier(ident.clone())
        .file_io(FileIOBuilder::new_fs_io().build().unwrap())
        .metadata(metadata.clone())
        .build()
        .expect("file_io + metadata + identifier is a complete table");
    iceberg::table::Table::builder()
        .identifier(ident)
        .file_io(FileIOBuilder::new_fs_io().build().unwrap())
        .metadata(metadata)
        .metadata_location("/tmp/inspect/metadata/v1.json")
        .readonly(true)
        .build()
        .expect("metadata_location and readonly are optional extras");
}

// ---------------------------------------------------------------------------
// g16_scan_deletes — spec.md "Row-level Deletes" and "Scan Planning": a scan
// task carries the delete files that may apply to its data file.
// ---------------------------------------------------------------------------

#[test]
fn g16_scan_deletes__task_delete_file_shape() {
    use iceberg::scan::FileScanTaskDeleteFile;

    let position = FileScanTaskDeleteFile {
        file_path: "/tmp/pos.parquet".to_string(),
        file_type: DataContentType::PositionDeletes,
        partition_spec_id: 0,
        equality_ids: vec![],
    };
    let equality = FileScanTaskDeleteFile {
        file_path: "/tmp/eq.parquet".to_string(),
        file_type: DataContentType::EqualityDeletes,
        partition_spec_id: 0,
        equality_ids: vec![1, 2],
    };
    // Position deletes carry no equality ids; equality deletes name their columns.
    assert!(position.equality_ids.is_empty());
    assert_eq!(equality.equality_ids, vec![1, 2]);
    assert_ne!(position.file_type, equality.file_type);

    let json = serde_json::to_value(&equality).unwrap();
    assert_eq!(json["file_path"], "/tmp/eq.parquet");
    assert_eq!(json["equality_ids"], serde_json::json!([1, 2]));
    let back: FileScanTaskDeleteFile = serde_json::from_value(json).unwrap();
    assert_eq!(back, equality);
}

#[test]
fn g16_scan_deletes__scan_builder_accepts_delete_file_processing() {
    let table = inspect_table();
    // Turning delete-file processing on or off must not change scan planning's
    // view of the snapshot or the projected columns.
    let on = table
        .scan()
        .with_delete_file_processing_enabled(true)
        .select(["id", "category"])
        .build()
        .expect("scan with delete file processing");
    let off = table
        .scan()
        .with_delete_file_processing_enabled(false)
        .select(["id", "category"])
        .build()
        .expect("scan without delete file processing");
    // A builder that never calls the setter behaves like `false`.
    let default = table
        .scan()
        .select(["id", "category"])
        .build()
        .expect("scan with the default delete-file processing state");
    assert_eq!(on.column_names(), Some(["id".to_string(), "category".to_string()].as_slice()));
    assert_eq!(on.column_names(), off.column_names());
    assert_eq!(default.column_names(), off.column_names());
    assert_eq!(
        on.snapshot().unwrap().snapshot_id(),
        off.snapshot().unwrap().snapshot_id()
    );
    assert_eq!(
        default.snapshot().unwrap().snapshot_id(),
        off.snapshot().unwrap().snapshot_id()
    );
}

#[test]
fn g16_scan_deletes__task_carries_its_delete_files_through_serde() {
    use iceberg::scan::{FileScanTask, FileScanTaskDeleteFile};

    let task = FileScanTask {
        start: 0,
        length: 1024,
        record_count: Some(10),
        data_file_path: "/tmp/sd-data-0.parquet".to_string(),
        data_file_content: DataContentType::Data,
        data_file_format: DataFileFormat::Parquet,
        schema: Arc::new(table_schema()),
        project_field_ids: vec![1, 2],
        predicate: None,
        deletes: vec![
            FileScanTaskDeleteFile {
                file_path: "/tmp/sd-pos-0.parquet".to_string(),
                file_type: DataContentType::PositionDeletes,
                partition_spec_id: 0,
                equality_ids: vec![],
            },
            FileScanTaskDeleteFile {
                file_path: "/tmp/sd-eq-0.parquet".to_string(),
                file_type: DataContentType::EqualityDeletes,
                partition_spec_id: 0,
                equality_ids: vec![1],
            },
        ],
    };

    let json = serde_json::to_value(&task).unwrap();
    assert_eq!(json["data_file_path"], "/tmp/sd-data-0.parquet");
    assert_eq!(json["deletes"][0]["file_path"], "/tmp/sd-pos-0.parquet");
    assert_eq!(json["deletes"][1]["equality_ids"], serde_json::json!([1]));
    let back: FileScanTask = serde_json::from_value(json).unwrap();
    assert_eq!(back, task);
    assert_eq!(back.deletes.len(), 2);
}

#[tokio::test]
async fn g16_scan_deletes__planning_reads_a_manifest_list_that_holds_delete_manifests() {
    // "Delete Formats": "A separate set of manifests is used for delete files
    // and DVs". Scan planning must read such a manifest list end to end.
    let (dir, io) = temp_io();
    let root = dir.path().to_str().unwrap().to_string();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);

    let data_path = format!("{root}/sd-data.avro");
    let mut dw = ManifestWriterBuilder::new(
        io.new_output(&data_path).unwrap(),
        Some(3055729675574597004),
        vec![],
        schema.clone(),
        spec.clone(),
    )
    .build_v2_data();
    dw.add_file(
        data_file(&format!("{root}/data-0.parquet")),
        UNASSIGNED_SEQUENCE_NUMBER,
    )
    .unwrap();
    let data_manifest = dw.write_manifest_file().await.unwrap();

    let del_path = format!("{root}/sd-del.avro");
    let mut xw = ManifestWriterBuilder::new(
        io.new_output(&del_path).unwrap(),
        Some(3055729675574597004),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    xw.add_file(
        position_delete_file(
            &format!("{root}/pos-0.parquet"),
            &format!("{root}/data-0.parquet"),
        ),
        UNASSIGNED_SEQUENCE_NUMBER,
    )
    .unwrap();
    let delete_manifest = xw.write_manifest_file().await.unwrap();

    let list_path = format!("{root}/sd-ml.avro");
    let mut lw = ManifestListWriter::v2(
        io.new_output(&list_path).unwrap(),
        3055729675574597004,
        None,
        3,
    );
    lw.add_manifests(vec![data_manifest, delete_manifest].into_iter())
        .unwrap();
    lw.close().await.unwrap();

    let metadata_json = INSPECT_METADATA
        .replace("file:///tmp/inspect/table", &root)
        .replace("file:///tmp/inspect/ml.avro", &list_path);
    let metadata: TableMetadata = serde_json::from_str(&metadata_json).unwrap();
    let table = iceberg::table::Table::builder()
        .identifier(iceberg::TableIdent::from_strs(["ns", "sd"]).unwrap())
        .file_io(io.clone())
        .metadata(metadata)
        .build()
        .unwrap();

    // Planning reads and parses the manifest list, both with and without delete
    // file processing.
    let plain = table.scan().build().unwrap();
    assert!(plain.plan_files().await.is_ok());
    let with_deletes = table
        .scan()
        .with_delete_file_processing_enabled(true)
        .build()
        .unwrap();
    assert!(with_deletes.plan_files().await.is_ok());

    // The manifest list really does carry both kinds of manifest.
    let bytes = std::fs::read(&list_path).unwrap();
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2, |_id| Ok(None)).unwrap();
    let contents: Vec<ManifestContentType> = list.entries().iter().map(|e| e.content).collect();
    assert!(contents.contains(&ManifestContentType::Data));
    assert!(contents.contains(&ManifestContentType::Deletes));
}

// ---------------------------------------------------------------------------
// g17_entry_status — spec.md "Manifest Entry Fields": `status` is 0 EXISTING,
// 1 ADDED, 2 DELETED and is orthogonal to `content`, plus
// "Sequence Number Inheritance": "When writing an existing file to a new
// manifest or marking an existing file as deleted, the data and file sequence
// numbers must be non-null."
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g17_entry_status__existing_file_keeps_its_snapshot_and_numbers() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "existing.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(110),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    // The snapshot id and the two sequence numbers assigned at the original
    // commit are supplied explicitly and must be preserved.
    writer
        .add_existing_file(
            equality_delete_file("/tmp/eq-existing.parquet", vec![1]),
            7,
            3,
            Some(3),
        )
        .unwrap();
    let manifest_file = writer.write_manifest_file().await.unwrap();
    assert_eq!(manifest_file.existing_files_count, Some(1));
    assert_eq!(manifest_file.existing_rows_count, Some(2));
    assert_eq!(manifest_file.added_files_count, Some(0));
    assert!(manifest_file.has_existing_files());
    assert!(!manifest_file.has_added_files());
    // An `Existing` entry is live, so it counts towards the minimum.
    assert_eq!(manifest_file.min_sequence_number, 3);

    let bytes = std::fs::read(&path).unwrap();
    let manifest = Manifest::parse_avro(&bytes).unwrap();
    let entry = &manifest.entries()[0];
    assert_eq!(entry.status(), ManifestStatus::Existing);
    assert!(entry.is_alive());
    assert_eq!(entry.snapshot_id(), Some(7));
    assert_eq!(entry.sequence_number(), Some(3));
    assert_eq!(entry.content_type(), DataContentType::EqualityDeletes);
}

#[tokio::test]
async fn g17_entry_status__add_delete_file_marks_the_entry_deleted() {
    // `add_delete_file` writes an entry whose *status* is DELETED — a file being
    // removed from the table. It is not the way a delete file is added to a
    // delete manifest; that is `add_file` on a `build_v2_deletes()` writer.
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "deleted.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(111),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_delete_file(
            equality_delete_file("/tmp/eq-removed.parquet", vec![1]),
            4,
            Some(4),
        )
        .unwrap();
    let manifest_file = writer.write_manifest_file().await.unwrap();
    assert_eq!(manifest_file.deleted_files_count, Some(1));
    assert_eq!(manifest_file.deleted_rows_count, Some(2));
    assert_eq!(manifest_file.added_files_count, Some(0));
    assert_eq!(manifest_file.existing_files_count, Some(0));
    assert!(manifest_file.has_deleted_files());
    assert!(!manifest_file.has_added_files());
    // A DELETED entry is not live, so the manifest has no minimum live number.
    assert_eq!(manifest_file.min_sequence_number, UNASSIGNED_SEQUENCE_NUMBER);

    let bytes = std::fs::read(&path).unwrap();
    let manifest = Manifest::parse_avro(&bytes).unwrap();
    let entry = &manifest.entries()[0];
    assert_eq!(entry.status(), ManifestStatus::Deleted);
    assert!(!entry.is_alive());
    // `status` is orthogonal to `content`: the removed file is still an
    // equality delete file.
    assert_eq!(entry.content_type(), DataContentType::EqualityDeletes);
    assert_eq!(entry.file_path(), "/tmp/eq-removed.parquet");
}

#[tokio::test]
async fn g17_entry_status__existing_or_deleted_needs_both_sequence_numbers() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "no-file-seq.avro");

    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(112),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    assert!(
        writer
            .add_existing_file(
                equality_delete_file("/tmp/eq-nofseq.parquet", vec![1]),
                7,
                3,
                None,
            )
            .is_err(),
        "an Existing entry with a null file sequence number must be rejected"
    );
    assert!(
        writer
            .add_delete_file(
                equality_delete_file("/tmp/eq-nofseq2.parquet", vec![1]),
                4,
                None,
            )
            .is_err(),
        "a Deleted entry with a null file sequence number must be rejected"
    );
    // The rejected entries were not recorded.
    let manifest_file = writer.write_manifest_file().await.unwrap();
    assert_eq!(manifest_file.existing_files_count, Some(0));
    assert_eq!(manifest_file.deleted_files_count, Some(0));
}

// ---------------------------------------------------------------------------
// g18_crate_identity — TEST_INTERFACE.md section 1: the package is named
// `iceberg`, its manifest is `/app/Cargo.toml`, its version is this
// milestone's release and its edition is the one section 5's migration table
// moves to. The manifest is read through the path the driver crate itself
// declares, so this works wherever the drivers are staged.
// ---------------------------------------------------------------------------

/// The `Cargo.toml` of the crate under test, located through the driver crate's
/// own `iceberg = { path = ... }` dependency declaration.
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
fn g18_crate_identity__package_name_and_version() {
    let manifest = crate_under_test_manifest();
    assert_eq!(
        package_key(&manifest, "name").as_deref(),
        Some("iceberg"),
        "TEST_INTERFACE.md section 1 pins the package name"
    );
    assert_eq!(
        package_key(&manifest, "version").as_deref(),
        Some("0.5.1"),
        "TEST_INTERFACE.md section 1 pins this milestone's package version"
    );
}

#[test]
fn g18_crate_identity__crate_uses_edition_2024() {
    let manifest = crate_under_test_manifest();
    assert_eq!(
        package_key(&manifest, "edition").as_deref(),
        Some("2024"),
        "section 5's migration table moves the crate to edition 2024"
    );
}

// ---------------------------------------------------------------------------
// Additional cases for milestone 04, from TEST_INTERFACE.md sections 4.2-4.4
// and spec.md "Sequence Number Inheritance".
// ---------------------------------------------------------------------------

/// spec.md "Sequence Number Inheritance": *"When reading v1 manifests with no
/// sequence number column, sequence numbers for all files must default to 0."*
///
/// The read path this is asserted over is the documented one. TEST_INTERFACE.md
/// 4.3 and 4.4 both say inheritance is applied by `ManifestFile::load_manifest`
/// and *not* by `Manifest::parse_avro`, and 4.4 fixes a v1 manifest list's own
/// numbers: *"a v1 list reads back `content == ManifestContentType::Data` …
/// with both sequence numbers `0`"*. So a v1 entry read through a v1 manifest
/// list entry reports `0`. What `parse_avro` alone reports for a column that a
/// v1 manifest does not have is deliberately **not** asserted: no sentence in
/// the shipped documents pins it, and 4.4's carve-out reads the other way.
#[tokio::test]
async fn g07_seq_inherit__v1_entries_default_their_sequence_number_to_zero() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "v1-seq.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(9),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v1();
    writer
        .add_file(data_file("/tmp/v1-d.parquet"), UNASSIGNED_SEQUENCE_NUMBER)
        .unwrap();
    let mut manifest_file = writer.write_manifest_file().await.unwrap();

    let manifest = Manifest::parse_avro(&std::fs::read(&path).unwrap()).unwrap();
    assert_eq!(manifest.entries().len(), 1);
    // v1 `manifest_entry` field 1 is *required*, so the writer's snapshot id
    // reaches the file verbatim.
    assert_eq!(manifest.entries()[0].snapshot_id(), Some(9));

    // The numbers a v1 manifest list gives back for this manifest, per 4.4.
    manifest_file.sequence_number = 0;
    manifest_file.min_sequence_number = 0;
    let loaded = manifest_file.load_manifest(&io).await.unwrap();
    assert_eq!(
        loaded.entries()[0].sequence_number(),
        Some(0),
        "a v1 manifest has no sequence-number column, so every file reads back 0"
    );
}

/// TEST_INTERFACE.md 4.4: *"Entry-level inheritance ("Sequence Number
/// Inheritance"), applied by `ManifestFile::load_manifest` and not by
/// `Manifest::parse_avro`: a null `snapshot_id` becomes the manifest list
/// entry's `added_snapshot_id`"*. A writer built with `None` writes the null,
/// so the value has to survive the Avro round trip in order to be inherited.
#[tokio::test]
async fn g07_seq_inherit__null_snapshot_id_is_inherited_from_the_manifest_list() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "inherit-snapshot-id.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        None,
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(
            equality_delete_file("/tmp/eq-inherit.parquet", vec![1]),
            UNASSIGNED_SEQUENCE_NUMBER,
        )
        .unwrap();
    let mut manifest_file = writer.write_manifest_file().await.unwrap();
    assert_eq!(manifest_file.added_snapshot_id, UNASSIGNED_SNAPSHOT_ID);

    // `parse_avro` has no manifest list to inherit from, so both stay null.
    let raw = Manifest::parse_avro(&std::fs::read(&path).unwrap()).unwrap();
    assert_eq!(raw.entries()[0].snapshot_id(), None);
    assert_eq!(raw.entries()[0].sequence_number(), None);

    // `load_manifest` inherits both from the manifest list entry.
    manifest_file.added_snapshot_id = 4242;
    manifest_file.sequence_number = 7;
    manifest_file.min_sequence_number = 7;
    let loaded = manifest_file.load_manifest(&io).await.unwrap();
    assert_eq!(loaded.entries()[0].snapshot_id(), Some(4242));
    assert_eq!(loaded.entries()[0].sequence_number(), Some(7));
}

// A case asserting that `add_delete_file` on a writer built with `None` leaves
// the entry's `snapshot_id` null was considered and dropped: section 4.3 pins
// the entry's snapshot id only in the `add_file` bullet ("its snapshot id to
// the writer's snapshot id"), and says nothing about what `add_delete_file`
// records there. Its remaining assertions duplicate
// `g17_entry_status__add_delete_file_marks_the_entry_deleted`.

/// TEST_INTERFACE.md 4.4: *"`ManifestList::parse_with_version` returns
/// `manifest_path`, `manifest_length`, `partition_spec_id`,
/// `added_snapshot_id`, `partitions`, `key_metadata` and the six counts exactly
/// as they were written."* The v1 `manifest_file` record types all six counts
/// as optional ("Manifest Lists" fields 504-506 and 512-514), so an absent
/// count is a value in its own right and has to come back absent.
#[tokio::test]
async fn g04_manifest_content__v1_list_round_trips_an_absent_count() {
    let (dir, io) = temp_io();
    let list_path = path_in(&dir, "v1-absent-counts.avro");
    let entry = ManifestFile {
        manifest_path: "/tmp/m-absent.avro".to_string(),
        manifest_length: 512,
        partition_spec_id: 3,
        content: ManifestContentType::Data,
        sequence_number: 0,
        min_sequence_number: 0,
        added_snapshot_id: 21,
        added_files_count: None,
        existing_files_count: None,
        deleted_files_count: None,
        added_rows_count: None,
        existing_rows_count: None,
        deleted_rows_count: None,
        partitions: vec![],
        key_metadata: vec![],
    };
    let mut writer = ManifestListWriter::v1(io.new_output(&list_path).unwrap(), 21, None);
    writer.add_manifests(vec![entry].into_iter()).unwrap();
    writer.close().await.unwrap();

    let list = ManifestList::parse_with_version(
        &std::fs::read(&list_path).unwrap(),
        FormatVersion::V1,
        |_spec_id| Ok(None),
    )
    .unwrap();
    let read_back = &list.entries()[0];
    assert_eq!(read_back.manifest_path, "/tmp/m-absent.avro");
    assert_eq!(read_back.manifest_length, 512);
    assert_eq!(read_back.partition_spec_id, 3);
    assert_eq!(read_back.added_snapshot_id, 21);
    assert_eq!(read_back.added_files_count, None);
    assert_eq!(read_back.existing_files_count, None);
    assert_eq!(read_back.deleted_files_count, None);
    assert_eq!(read_back.added_rows_count, None);
    assert_eq!(read_back.existing_rows_count, None);
    assert_eq!(read_back.deleted_rows_count, None);
}

/// TEST_INTERFACE.md 4.3: *"A rejected add … changes nothing: the entry is not
/// recorded and no counter moves, so a writer whose adds were all rejected
/// still writes a manifest with every count `Some(0)`."* Section 6 fixes what
/// "writes a manifest" means: an Avro object container file, which the crate's
/// own reader accepts.
#[tokio::test]
async fn g03_delete_manifest__a_writer_whose_adds_were_all_rejected_still_writes_a_manifest() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "all-rejected.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(5),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_data();
    assert!(writer
        .add_file(
            position_delete_file("/tmp/p.parquet", "/tmp/d.parquet"),
            5
        )
        .is_err());
    assert!(writer
        .add_file(equality_delete_file("/tmp/e.parquet", vec![1]), 5)
        .is_err());

    let manifest_file = writer.write_manifest_file().await.unwrap();
    assert_eq!(manifest_file.added_files_count, Some(0));
    assert_eq!(manifest_file.existing_files_count, Some(0));
    assert_eq!(manifest_file.deleted_files_count, Some(0));
    assert_eq!(manifest_file.added_rows_count, Some(0));
    assert_eq!(manifest_file.existing_rows_count, Some(0));
    assert_eq!(manifest_file.deleted_rows_count, Some(0));
    assert!(!manifest_file.has_added_files());
    assert!(!manifest_file.has_existing_files());
    assert!(!manifest_file.has_deleted_files());
    assert_eq!(manifest_file.min_sequence_number, UNASSIGNED_SEQUENCE_NUMBER);

    // Section 6: manifests "are Avro object container files … The drivers check
    // the Avro object-container magic `Obj\x01` and round-trip the file through
    // your own reader."
    let bytes = std::fs::read(&path).unwrap();
    assert!(
        bytes.starts_with(b"Obj\x01"),
        "an entry-less manifest is still an Avro object container file, \
         but {} bytes were written",
        bytes.len()
    );
    assert!(Manifest::parse_avro(&bytes).unwrap().entries().is_empty());
}

/// PROFILE.md P5: *"Where the specification does fix an order — … entries
/// within a manifest as written — that order is required."*
#[tokio::test]
async fn g03_delete_manifest__entries_keep_the_order_they_were_added_in() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "entry-order.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(1),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    for name in ["/tmp/z.parquet", "/tmp/a.parquet", "/tmp/m.parquet"] {
        writer
            .add_file(equality_delete_file(name, vec![1]), 5)
            .unwrap();
    }
    writer.write_manifest_file().await.unwrap();

    let manifest = Manifest::parse_avro(&std::fs::read(&path).unwrap()).unwrap();
    let paths: Vec<&str> = manifest.entries().iter().map(|e| e.file_path()).collect();
    assert_eq!(
        paths,
        vec!["/tmp/z.parquet", "/tmp/a.parquet", "/tmp/m.parquet"]
    );
}

/// TEST_INTERFACE.md 4.4: *"`add_manifests` writes each `ManifestFile` into the
/// list in the order given"*.
#[tokio::test]
async fn g04_manifest_content__the_list_keeps_the_order_it_was_given() {
    let (dir, io) = temp_io();
    let entry = |p: &str| ManifestFile {
        manifest_path: p.to_string(),
        manifest_length: 8,
        partition_spec_id: 0,
        content: ManifestContentType::Data,
        sequence_number: 3,
        min_sequence_number: 3,
        added_snapshot_id: 42,
        added_files_count: Some(1),
        existing_files_count: Some(0),
        deleted_files_count: Some(0),
        added_rows_count: Some(1),
        existing_rows_count: Some(0),
        deleted_rows_count: Some(0),
        partitions: vec![],
        key_metadata: vec![],
    };
    let list_path = path_in(&dir, "ordered-list.avro");
    let mut writer = ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 42, None, 3);
    writer
        .add_manifests(
            vec![entry("/tmp/c.avro"), entry("/tmp/a.avro"), entry("/tmp/b.avro")].into_iter(),
        )
        .unwrap();
    writer.close().await.unwrap();

    let list = ManifestList::parse_with_version(
        &std::fs::read(&list_path).unwrap(),
        FormatVersion::V2,
        |_spec_id| Ok(None),
    )
    .unwrap();
    let paths: Vec<&str> = list
        .entries()
        .iter()
        .map(|e| e.manifest_path.as_str())
        .collect();
    assert_eq!(paths, vec!["/tmp/c.avro", "/tmp/a.avro", "/tmp/b.avro"]);
}

/// TEST_INTERFACE.md 4.4: *"A manifest carried forward from another snapshot
/// must already carry its own numbers"* — the other half of the rule
/// `g08_seq_assign` tests: one that does carry them is accepted, and the
/// writer's own sequence number is not substituted for it. The substitution is
/// *"for format version 2 only"*, so a v1 list rejects nothing.
#[tokio::test]
async fn g08_seq_assign__a_numbered_foreign_manifest_keeps_its_own_numbers() {
    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "carried-forward.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(80),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_deletes();
    writer
        .add_file(equality_delete_file("/tmp/eq-carried.parquet", vec![1]), 3)
        .unwrap();
    let mut manifest_file = writer.write_manifest_file().await.unwrap();
    manifest_file.sequence_number = 3;
    manifest_file.min_sequence_number = 3;

    let list_path = path_in(&dir, "carried-forward-list.avro");
    let mut list_writer =
        ManifestListWriter::v2(io.new_output(&list_path).unwrap(), 81, Some(80), 13);
    list_writer
        .add_manifests(vec![manifest_file.clone()].into_iter())
        .expect("a foreign manifest that already carries its numbers is accepted");
    list_writer.close().await.unwrap();

    let list = ManifestList::parse_with_version(
        &std::fs::read(&list_path).unwrap(),
        FormatVersion::V2,
        |_spec_id| Ok(None),
    )
    .unwrap();
    assert_eq!(list.entries()[0].sequence_number, 3);
    assert_eq!(list.entries()[0].min_sequence_number, 3);
    assert_eq!(list.entries()[0].added_snapshot_id, 80);

    // A v1 list substitutes nothing, so it rejects nothing either.
    let mut unassigned = manifest_file;
    unassigned.sequence_number = UNASSIGNED_SEQUENCE_NUMBER;
    unassigned.min_sequence_number = UNASSIGNED_SEQUENCE_NUMBER;
    let v1_path = path_in(&dir, "carried-forward-v1.avro");
    let mut v1_writer = ManifestListWriter::v1(io.new_output(&v1_path).unwrap(), 81, Some(80));
    assert!(v1_writer.add_manifests(vec![unassigned].into_iter()).is_ok());
    v1_writer.close().await.unwrap();
}

/// TEST_INTERFACE.md 4.2: *"Each optional field's default is its own type's
/// empty value: an `Option<…>` field that was never set reads back as `None`"*,
/// and the four new delete-file columns (142-145) round-trip through the
/// format-2 manifest.
#[tokio::test]
async fn g09_delete_stats__unset_optional_columns_default_to_empty_and_survive() {
    let file = data_file("/tmp/plain.parquet");
    assert_eq!(file.first_row_id(), None);
    assert_eq!(file.referenced_data_file(), None);
    assert_eq!(file.content_offset(), None);
    assert_eq!(file.content_size_in_bytes(), None);
    assert_eq!(file.sort_order_id(), None);
    assert_eq!(file.key_metadata(), None);
    assert!(file.split_offsets().is_empty());
    assert!(file.equality_ids().is_empty());
    assert!(file.column_sizes().is_empty());
    assert!(file.value_counts().is_empty());
    assert!(file.null_value_counts().is_empty());
    assert!(file.nan_value_counts().is_empty());
    assert!(file.lower_bounds().is_empty());
    assert!(file.upper_bounds().is_empty());

    let (dir, io) = temp_io();
    let schema: SchemaRef = Arc::new(table_schema());
    let spec = unpartitioned_spec(&schema);
    let path = path_in(&dir, "defaults.avro");
    let mut writer = ManifestWriterBuilder::new(
        io.new_output(&path).unwrap(),
        Some(1),
        vec![],
        schema.clone(),
        spec,
    )
    .build_v2_data();
    writer.add_file(file, 2).unwrap();
    writer.write_manifest_file().await.unwrap();

    let manifest = Manifest::parse_avro(&std::fs::read(&path).unwrap()).unwrap();
    let read_back = manifest.entries()[0].data_file();
    assert_eq!(read_back.first_row_id(), None);
    assert_eq!(read_back.referenced_data_file(), None);
    assert_eq!(read_back.content_offset(), None);
    assert_eq!(read_back.content_size_in_bytes(), None);
    assert_eq!(read_back.sort_order_id(), None);
    assert!(read_back.split_offsets().is_empty());
    assert!(read_back.equality_ids().is_empty());
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
