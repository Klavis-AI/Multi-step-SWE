// Verifier-owned conformance driver for milestone 02 (scan planning, predicates,
// transform evaluation, Arrow interop, view metadata).
//
// Every assertion here traces to the pinned Apache Iceberg table specification
// shipped at /app/iceberg-spec/spec.md (and view-spec.md), or to
// /app/TEST_INTERFACE.md, which states the public Rust surface the milestone
// must expose.
//
// Group = the `gNN_<name>__` prefix of the test function name. The runner
// invokes one `cargo test` per group and asserts the executed count.

use std::collections::HashMap;
use std::sync::Arc;

use iceberg::arrow::{arrow_schema_to_schema, schema_to_arrow_schema};
use iceberg::expr::{Bind, BoundPredicate, Predicate, PredicateOperator, Reference};
use iceberg::io::{FileIO, FileIOBuilder};
use iceberg::spec::*;
use iceberg::table::{StaticTable, Table};
use iceberg::transform::create_transform_function;
use iceberg::TableIdent;

// ---------------------------------------------------------------------------
// Shared, immutable fixtures. Every helper below constructs fresh values; no
// group observes state produced by another group.
// ---------------------------------------------------------------------------

fn bucket_of(transform: &Transform, datum: &Datum) -> i32 {
    let f = create_transform_function(transform).expect("create transform function");
    let out = f
        .transform_literal(datum)
        .expect("transform_literal")
        .expect("bucket transform always produces a value");
    assert_eq!(out.data_type(), &PrimitiveType::Int);
    match out.literal() {
        PrimitiveLiteral::Int(v) => *v,
        other => panic!("bucket transform must produce an int literal, got {other:?}"),
    }
}

/// spec.md "Bucket Transform Details":
/// `def bucket_N(x) = (murmur3_x86_32_hash(x) & Integer.MAX_VALUE) % N`
fn expected_bucket(hash: i32, n: u32) -> i32 {
    (hash & i32::MAX) % (n as i32)
}

fn scan_schema() -> Schema {
    Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "name", Type::Primitive(PrimitiveType::String)).into(),
            NestedField::optional(3, "score", Type::Primitive(PrimitiveType::Double)).into(),
        ])
        .build()
        .expect("build schema")
}

fn scan_schema_ref() -> SchemaRef {
    Arc::new(scan_schema())
}

const SNAPSHOT_ID: i64 = 3051729675574597004;

/// Writes a complete, spec-shaped table on the local filesystem: one manifest
/// holding two added data files, one manifest list referencing it, and a v2
/// table metadata document pointing at that manifest list.
///
/// Returns the temp dir (kept alive by the caller) and the metadata file path.
async fn write_table(dir: &tempfile::TempDir) -> String {
    let root = dir.path().to_str().unwrap().to_string();
    let io: FileIO = FileIOBuilder::new_fs_io().build().expect("build fs FileIO");

    let schema = scan_schema();
    let partition_spec = PartitionSpec::builder(&schema)
        .with_spec_id(0)
        .add_partition_field("id", "id_part", Transform::Identity)
        .expect("add partition field")
        .build()
        .expect("build partition spec");

    let manifest_metadata = ManifestMetadata::builder()
        .schema(schema.clone())
        .schema_id(0)
        .partition_spec(partition_spec)
        .content(ManifestContentType::Data)
        .format_version(FormatVersion::V2)
        .build();

    let file_a = DataFileBuilder::default()
        .content(DataContentType::Data)
        .file_path(format!("{root}/data-a.parquet"))
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::from_iter([Some(Literal::long(1))]))
        .record_count(5)
        .file_size_in_bytes(1024)
        .value_counts(HashMap::from([(1, 5u64), (2, 5u64)]))
        .null_value_counts(HashMap::from([(1, 0u64), (2, 2u64)]))
        .nan_value_counts(HashMap::from([(1, 0u64)]))
        .lower_bounds(HashMap::from([(1, Datum::long(1))]))
        .upper_bounds(HashMap::from([(1, Datum::long(5))]))
        .build()
        .expect("build data file a");

    let file_b = DataFileBuilder::default()
        .content(DataContentType::Data)
        .file_path(format!("{root}/data-b.parquet"))
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::from_iter([Some(Literal::long(10))]))
        .record_count(7)
        .file_size_in_bytes(2048)
        .value_counts(HashMap::from([(1, 7u64), (2, 7u64)]))
        .null_value_counts(HashMap::from([(1, 0u64), (2, 0u64)]))
        .nan_value_counts(HashMap::from([(1, 0u64)]))
        .lower_bounds(HashMap::from([(1, Datum::long(10))]))
        .upper_bounds(HashMap::from([(1, Datum::long(20))]))
        .build()
        .expect("build data file b");

    let manifest = Manifest::new(manifest_metadata, vec![
        ManifestEntry::builder()
            .status(ManifestStatus::Added)
            .data_file(file_a)
            .build(),
        ManifestEntry::builder()
            .status(ManifestStatus::Added)
            .data_file(file_b)
            .build(),
    ]);

    let manifest_path = format!("{root}/m0.avro");
    let manifest_file = ManifestWriter::new(
        io.new_output(&manifest_path).expect("manifest output"),
        SNAPSHOT_ID,
        vec![],
    )
    .write(manifest)
    .await
    .expect("write manifest");

    let manifest_list_path = format!("{root}/snap-0.avro");
    let mut list_writer = ManifestListWriter::v2(
        io.new_output(&manifest_list_path)
            .expect("manifest list output"),
        SNAPSHOT_ID,
        -1,
        1,
    );
    list_writer
        .add_manifests(vec![manifest_file].into_iter())
        .expect("add manifests");
    list_writer.close().await.expect("close manifest list");

    let metadata = format!(
        r#"{{
  "format-version": 2,
  "table-uuid": "9c12d441-03fe-4693-9a96-a0705ddf69c1",
  "location": "{root}",
  "last-sequence-number": 1,
  "last-updated-ms": 1602638573590,
  "last-column-id": 3,
  "current-schema-id": 0,
  "schemas": [{{"type": "struct", "schema-id": 0, "fields": [
    {{"id": 1, "name": "id", "required": true, "type": "long"}},
    {{"id": 2, "name": "name", "required": false, "type": "string"}},
    {{"id": 3, "name": "score", "required": false, "type": "double"}}
  ]}}],
  "default-spec-id": 0,
  "partition-specs": [{{"spec-id": 0, "fields": [
    {{"name": "id_part", "transform": "identity", "source-id": 1, "field-id": 1000}}
  ]}}],
  "last-partition-id": 1000,
  "default-sort-order-id": 0,
  "sort-orders": [{{"order-id": 0, "fields": []}}],
  "properties": {{}},
  "current-snapshot-id": {SNAPSHOT_ID},
  "snapshots": [
    {{"snapshot-id": {SNAPSHOT_ID}, "timestamp-ms": 1515100955770, "sequence-number": 1,
     "summary": {{"operation": "append"}}, "manifest-list": "{manifest_list_path}",
     "schema-id": 0}}
  ],
  "snapshot-log": [{{"snapshot-id": {SNAPSHOT_ID}, "timestamp-ms": 1515100955770}}],
  "metadata-log": []
}}"#
    );

    let metadata_path = format!("{root}/metadata.json");
    std::fs::write(&metadata_path, metadata).expect("write table metadata");
    metadata_path
}

async fn load_table(dir: &tempfile::TempDir) -> StaticTable {
    let metadata_path = write_table(dir).await;
    let io = FileIOBuilder::new_fs_io().build().expect("build fs FileIO");
    StaticTable::from_metadata_file(
        &metadata_path,
        TableIdent::from_strs(["ns", "t"]).expect("table ident"),
        io,
    )
    .await
    .expect("load static table")
}

// ---------------------------------------------------------------------------
// g01_datum — spec.md "Primitive Types" and Appendix D "Single-value
// serialization". `Datum` is the milestone's typed single-value carrier: a
// primitive type paired with its primitive literal.
// ---------------------------------------------------------------------------

#[test]
fn g01_datum__type_and_literal_are_paired() {
    let d = Datum::int(34);
    assert_eq!(d.data_type(), &PrimitiveType::Int);
    assert_eq!(d.literal(), &PrimitiveLiteral::Int(34));

    let d = Datum::long(34);
    assert_eq!(d.data_type(), &PrimitiveType::Long);
    assert_eq!(d.literal(), &PrimitiveLiteral::Long(34));

    let d = Datum::string("iceberg");
    assert_eq!(d.data_type(), &PrimitiveType::String);
    assert_eq!(d.literal(), &PrimitiveLiteral::String("iceberg".to_string()));

    let d = Datum::bool(true);
    assert_eq!(d.data_type(), &PrimitiveType::Boolean);
    assert_eq!(d.literal(), &PrimitiveLiteral::Boolean(true));

    // Section 4.1: a `Datum` is ordered, hashable and `Display`. Bounds
    // comparison in scan planning needs the ordering; the `Display` *text* is
    // not scored (section 8), only that the impl exists.
    assert!(Datum::int(1) < Datum::int(2));
    assert!(Datum::long(2) > Datum::long(1));
    assert!(Datum::string("a") < Datum::string("b"));
    let mut seen = std::collections::HashSet::new();
    assert!(seen.insert(Datum::string("a")));
    assert!(!seen.insert(Datum::string("a")));
    let _ = format!("{}", Datum::int(34));
}

#[test]
fn g01_datum__dates_are_days_from_epoch() {
    // spec.md "Primitive Types": `date` is "Calendar date without timezone or
    // time", stored as days from 1970-01-01.
    assert_eq!(
        Datum::date_from_str("1970-01-01").unwrap().literal(),
        &PrimitiveLiteral::Int(0)
    );
    assert_eq!(
        Datum::date_from_str("2017-11-16").unwrap().literal(),
        &PrimitiveLiteral::Int(17486)
    );
    assert_eq!(
        Datum::date_from_ymd(1969, 12, 31).unwrap().literal(),
        &PrimitiveLiteral::Int(-1)
    );
    assert_eq!(
        Datum::date_from_ymd(2017, 11, 16).unwrap(),
        Datum::date_from_str("2017-11-16").unwrap()
    );

    // Section 4.1: `Datum::to` converts to a target type — an `int` day count
    // is the same value as the `date` it denotes.
    assert_eq!(
        Datum::int(17486)
            .to(&Type::Primitive(PrimitiveType::Date))
            .expect("int -> date"),
        Datum::date_from_str("2017-11-16").unwrap()
    );
    assert_eq!(
        Datum::int(34)
            .to(&Type::Primitive(PrimitiveType::Int))
            .expect("int -> int"),
        Datum::int(34)
    );
    // A conversion the contract does not define is an error.
    assert!(Datum::string("nope")
        .to(&Type::Primitive(PrimitiveType::Long))
        .is_err());
}

#[test]
fn g01_datum__decimal_carries_unscaled_value() {
    // Appendix D: decimals are serialized as the unscaled value; the scale is
    // part of the type, not the value.
    let d = Datum::decimal_from_str("14.20").unwrap();
    assert_eq!(d.literal(), &PrimitiveLiteral::Int128(1420));
    let d = Datum::decimal_from_str("10.65").unwrap();
    assert_eq!(d.literal(), &PrimitiveLiteral::Int128(1065));
    assert!(Datum::decimal_from_str("not-a-decimal").is_err());
}

#[test]
fn g01_datum__binary_single_value_round_trip() {
    // Appendix D "Binary single-value serialization": int/long are little
    // endian two's complement; strings are UTF-8 bytes without length.
    let cases: Vec<(Datum, PrimitiveType)> = vec![
        (Datum::int(34), PrimitiveType::Int),
        (Datum::long(34), PrimitiveType::Long),
        (Datum::string("iceberg"), PrimitiveType::String),
        (Datum::bool(true), PrimitiveType::Boolean),
        (Datum::date_from_str("2017-11-16").unwrap(), PrimitiveType::Date),
    ];
    for (datum, ty) in cases {
        let bytes = datum.to_bytes();
        let back = Datum::try_from_bytes(&bytes, ty).expect("decode datum");
        assert_eq!(back, datum);
    }
    assert_eq!(Datum::int(34).to_bytes().as_ref(), &34i32.to_le_bytes());
    assert_eq!(Datum::long(34).to_bytes().as_ref(), &34i64.to_le_bytes());
    assert_eq!(Datum::string("iceberg").to_bytes().as_ref(), b"iceberg");
}

// ---------------------------------------------------------------------------
// g02_bucket_hash — spec.md "Appendix B: 32-bit Hash Requirements" and
// "Bucket Transform Details". The published test values are driven through
// `Transform::Bucket(N)`; the expected bucket is derived from the published
// hash with the spec's own formula.
// ---------------------------------------------------------------------------

/// N large enough that `(hash & MAX) % N == hash & MAX` for every published
/// test value, so the assertion pins the 32-bit hash itself.
const WIDE_N: u32 = i32::MAX as u32;

#[test]
fn g02_bucket_hash__int_and_long_share_hash() {
    // Appendix B note 1: "Integer and long hash results must be identical for
    // all integer values."
    let t = Transform::Bucket(WIDE_N);
    assert_eq!(bucket_of(&t, &Datum::int(34)), expected_bucket(2017239379, WIDE_N));
    assert_eq!(bucket_of(&t, &Datum::long(34)), expected_bucket(2017239379, WIDE_N));
    assert_eq!(
        bucket_of(&t, &Datum::int(34)),
        bucket_of(&t, &Datum::long(34))
    );
}

#[test]
fn g02_bucket_hash__string_uuid_and_binary() {
    let t = Transform::Bucket(WIDE_N);
    assert_eq!(
        bucket_of(&t, &Datum::string("iceberg")),
        expected_bucket(1210000089, WIDE_N)
    );
    assert_eq!(
        bucket_of(
            &t,
            &Datum::uuid_from_str("f79c3e09-677c-4bbd-a479-3f349cb785e7").unwrap()
        ),
        expected_bucket(1488055340, WIDE_N)
    );
    // Appendix B: fixed(L) and binary both hash the raw bytes.
    assert_eq!(
        bucket_of(&t, &Datum::binary(vec![0u8, 1, 2, 3])),
        expected_bucket(-188683207, WIDE_N)
    );
    assert_eq!(
        bucket_of(&t, &Datum::fixed(vec![0u8, 1, 2, 3])),
        expected_bucket(-188683207, WIDE_N)
    );
}

#[test]
fn g02_bucket_hash__temporal_and_decimal() {
    let t = Transform::Bucket(WIDE_N);
    assert_eq!(
        bucket_of(&t, &Datum::date_from_str("2017-11-16").unwrap()),
        expected_bucket(-653330422, WIDE_N)
    );
    assert_eq!(
        bucket_of(&t, &Datum::time_from_str("22:31:08").unwrap()),
        expected_bucket(-662762989, WIDE_N)
    );
    assert_eq!(
        bucket_of(
            &t,
            &Datum::timestamp_from_str("2017-11-16T22:31:08").unwrap()
        ),
        expected_bucket(-2047944441, WIDE_N)
    );
    assert_eq!(
        bucket_of(
            &t,
            &Datum::timestamp_from_str("2017-11-16T22:31:08.000001").unwrap()
        ),
        expected_bucket(-1207196810, WIDE_N)
    );
    // Appendix B note 2: hash results do not depend on decimal scale.
    assert_eq!(
        bucket_of(&t, &Datum::decimal_from_str("14.20").unwrap()),
        expected_bucket(-500754589, WIDE_N)
    );
}

#[test]
fn g02_bucket_hash__modulo_discards_sign_bit() {
    // "Bucket Transform Details": the hash mod N must produce a positive value
    // by first discarding the sign bit of the hash value.
    for n in [16u32, 100, 1_000_000] {
        let t = Transform::Bucket(n);
        let b = bucket_of(&t, &Datum::string("iceberg"));
        assert_eq!(b, expected_bucket(1210000089, n));
        assert!(b >= 0 && b < n as i32);

        // A negative hash must still bucket into [0, N).
        let b = bucket_of(&t, &Datum::date_from_str("2017-11-16").unwrap());
        assert_eq!(b, expected_bucket(-653330422, n));
        assert!(b >= 0 && b < n as i32);
    }
}

#[test]
fn g02_bucket_hash__rejects_unbucketable_type() {
    // Appendix B: boolean/float/double "are not currently valid for bucketing".
    let t = Transform::Bucket(16);
    let f = create_transform_function(&t).unwrap();
    assert!(f.transform_literal(&Datum::bool(true)).is_err());
    assert!(f.transform_literal(&Datum::double(1.0)).is_err());
}

// ---------------------------------------------------------------------------
// g03_truncate — spec.md "Truncate Transform Details".
// ---------------------------------------------------------------------------

#[test]
fn g03_truncate__int_and_long_remainder_is_positive() {
    // `W=10`: `1` -> `0`, `-1` -> `-10`.
    let f = create_transform_function(&Transform::Truncate(10)).unwrap();
    assert_eq!(
        f.transform_literal(&Datum::int(1)).unwrap().unwrap(),
        Datum::int(0)
    );
    assert_eq!(
        f.transform_literal(&Datum::int(-1)).unwrap().unwrap(),
        Datum::int(-10)
    );
    assert_eq!(
        f.transform_literal(&Datum::long(1)).unwrap().unwrap(),
        Datum::long(0)
    );
    assert_eq!(
        f.transform_literal(&Datum::long(-1)).unwrap().unwrap(),
        Datum::long(-10)
    );
}

#[test]
fn g03_truncate__string_is_first_l_code_points() {
    // `L=3`: `iceberg` -> `ice`. Note 3: strings truncate to at most L code
    // points and the result must remain valid UTF-8.
    let f = create_transform_function(&Transform::Truncate(3)).unwrap();
    assert_eq!(
        f.transform_literal(&Datum::string("iceberg"))
            .unwrap()
            .unwrap(),
        Datum::string("ice")
    );
    // Shorter than L is returned unchanged.
    assert_eq!(
        f.transform_literal(&Datum::string("ic")).unwrap().unwrap(),
        Datum::string("ic")
    );
    // Multi-byte code points are not split.
    assert_eq!(
        f.transform_literal(&Datum::string("イスバーグ"))
            .unwrap()
            .unwrap(),
        Datum::string("イスバ")
    );
}

#[test]
fn g03_truncate__decimal_applies_width_at_column_scale() {
    // `W=50`, `s=2`: `10.65` -> `10.50`; the unscaled value 1065 truncates to
    // 1050 because the width is applied using the scale of the column.
    let f = create_transform_function(&Transform::Truncate(50)).unwrap();
    let out = f
        .transform_literal(&Datum::decimal_from_str("10.65").unwrap())
        .unwrap()
        .unwrap();
    assert_eq!(out.literal(), &PrimitiveLiteral::Int128(1050));

    let out = f
        .transform_literal(&Datum::decimal_from_str("-0.05").unwrap())
        .unwrap()
        .unwrap();
    // -5 - (((-5 % 50) + 50) % 50) = -5 - 45 = -50
    assert_eq!(out.literal(), &PrimitiveLiteral::Int128(-50));
}

#[test]
fn g03_truncate__rejects_untruncatable_type() {
    let f = create_transform_function(&Transform::Truncate(10)).unwrap();
    assert!(f.transform_literal(&Datum::bool(true)).is_err());
    assert!(f.transform_literal(&Datum::double(1.5)).is_err());
}

// ---------------------------------------------------------------------------
// g04_temporal_transform — spec.md "Partition Transforms": `year` is "Extract
// a date or timestamp year, as years from 1970"; `month` "as months from
// 1970-01-01"; `day` "as days from 1970-01-01"; `hour` "as hours from
// 1970-01-01 00:00:00". All four produce `int`.
// ---------------------------------------------------------------------------

#[test]
fn g04_temporal_transform__year_counts_from_1970() {
    let f = create_transform_function(&Transform::Year).unwrap();
    let year = |d: Datum| match f.transform_literal(&d).unwrap().unwrap().literal() {
        PrimitiveLiteral::Int(v) => *v,
        other => panic!("year must produce int, got {other:?}"),
    };
    assert_eq!(year(Datum::date_from_str("1970-01-01").unwrap()), 0);
    assert_eq!(year(Datum::date_from_str("1970-12-31").unwrap()), 0);
    assert_eq!(year(Datum::date_from_str("1971-01-01").unwrap()), 1);
    assert_eq!(year(Datum::date_from_str("2017-11-16").unwrap()), 47);
    assert_eq!(year(Datum::date_from_ymd(1969, 12, 31).unwrap()), -1);
    assert_eq!(
        year(Datum::timestamp_from_str("2017-11-16T22:31:08").unwrap()),
        47
    );
}

#[test]
fn g04_temporal_transform__month_counts_from_1970_01() {
    let f = create_transform_function(&Transform::Month).unwrap();
    let month = |d: Datum| match f.transform_literal(&d).unwrap().unwrap().literal() {
        PrimitiveLiteral::Int(v) => *v,
        other => panic!("month must produce int, got {other:?}"),
    };
    assert_eq!(month(Datum::date_from_str("1970-01-01").unwrap()), 0);
    assert_eq!(month(Datum::date_from_str("1970-02-01").unwrap()), 1);
    assert_eq!(month(Datum::date_from_str("1971-01-01").unwrap()), 12);
    // 2017-11 is (2017-1970)*12 + 10 months after 1970-01.
    assert_eq!(month(Datum::date_from_str("2017-11-16").unwrap()), 574);
    assert_eq!(month(Datum::date_from_ymd(1969, 12, 31).unwrap()), -1);
}

#[test]
fn g04_temporal_transform__day_counts_from_1970_01_01() {
    let f = create_transform_function(&Transform::Day).unwrap();
    let day = |d: Datum| match f.transform_literal(&d).unwrap().unwrap().literal() {
        PrimitiveLiteral::Int(v) => *v,
        other => panic!("day must produce int, got {other:?}"),
    };
    assert_eq!(day(Datum::date_from_str("1970-01-01").unwrap()), 0);
    assert_eq!(day(Datum::date_from_str("1970-01-02").unwrap()), 1);
    assert_eq!(day(Datum::date_from_str("2017-11-16").unwrap()), 17486);
    assert_eq!(day(Datum::date_from_ymd(1969, 12, 31).unwrap()), -1);
    assert_eq!(
        day(Datum::timestamp_from_str("2017-11-16T22:31:08").unwrap()),
        17486
    );
}

#[test]
fn g04_temporal_transform__hour_counts_from_1970_01_01T00() {
    let f = create_transform_function(&Transform::Hour).unwrap();
    let hour = |d: Datum| match f.transform_literal(&d).unwrap().unwrap().literal() {
        PrimitiveLiteral::Int(v) => *v,
        other => panic!("hour must produce int, got {other:?}"),
    };
    assert_eq!(
        hour(Datum::timestamp_from_str("1970-01-01T00:00:00").unwrap()),
        0
    );
    assert_eq!(
        hour(Datum::timestamp_from_str("1970-01-01T01:00:00").unwrap()),
        1
    );
    // 17486 days * 24 + 22 hours.
    assert_eq!(
        hour(Datum::timestamp_from_str("2017-11-16T22:31:08").unwrap()),
        419686
    );
    // "Partition Transforms": hour is not defined for `date`.
    assert!(f
        .transform_literal(&Datum::date_from_str("2017-11-16").unwrap())
        .is_err());
}

#[test]
fn g04_temporal_transform__timestamptz_matches_utc_instant() {
    // Appendix B: `2017-11-16T14:31:08-08:00` is the same instant as
    // `2017-11-16T22:31:08`, so every temporal transform must agree.
    let tz = Datum::timestamptz_from_str("2017-11-16T14:31:08-08:00").unwrap();
    let utc = Datum::timestamp_from_str("2017-11-16T22:31:08").unwrap();
    for transform in [Transform::Year, Transform::Month, Transform::Day, Transform::Hour] {
        let f = create_transform_function(&transform).unwrap();
        assert_eq!(
            f.transform_literal(&tz).unwrap().unwrap().literal(),
            f.transform_literal(&utc).unwrap().unwrap().literal(),
            "transform {transform} must agree across timestamp and timestamptz"
        );
    }
}

// ---------------------------------------------------------------------------
// g05_identity_void_transform — spec.md "Partition Transforms": `identity`
// is "Source value, unmodified"; `void` "Always produces `null`".
// ---------------------------------------------------------------------------

#[test]
fn g05_identity_void_transform__identity_is_the_source_value() {
    let f = create_transform_function(&Transform::Identity).unwrap();
    for d in [
        Datum::int(-7),
        Datum::long(1234567890123i64),
        Datum::string("iceberg"),
        Datum::bool(false),
        Datum::date_from_str("2017-11-16").unwrap(),
    ] {
        assert_eq!(f.transform_literal(&d).unwrap().unwrap(), d);
    }
}

#[test]
fn g05_identity_void_transform__void_produces_null() {
    let f = create_transform_function(&Transform::Void).unwrap();
    assert!(f.transform_literal(&Datum::int(1)).unwrap().is_none());
    assert!(f.transform_literal(&Datum::string("x")).unwrap().is_none());
    // The `_result` wrapper turns the absent value into an error.
    assert!(f.transform_literal_result(&Datum::int(1)).is_err());
}

#[test]
fn g05_identity_void_transform__unknown_transform_has_no_function() {
    // spec.md "Partition Transforms": readers must accept an unknown transform
    // in metadata, but it cannot be evaluated.
    assert!(create_transform_function(&Transform::Unknown).is_err());
}

// ---------------------------------------------------------------------------
// g06_predicate_build — spec.md "Scan Planning": scan predicates filter data
// rows. Construction is by column reference plus operator plus literal.
// ---------------------------------------------------------------------------

#[test]
fn g06_predicate_build__reference_operators() {
    assert_eq!(Reference::new("id").name(), "id");

    let cases: Vec<(Predicate, PredicateOperator)> = vec![
        (
            Reference::new("id").less_than(Datum::long(10)),
            PredicateOperator::LessThan,
        ),
        (
            Reference::new("id").less_than_or_equal_to(Datum::long(10)),
            PredicateOperator::LessThanOrEq,
        ),
        (
            Reference::new("id").greater_than(Datum::long(10)),
            PredicateOperator::GreaterThan,
        ),
        (
            Reference::new("id").greater_than_or_equal_to(Datum::long(10)),
            PredicateOperator::GreaterThanOrEq,
        ),
        (
            Reference::new("id").equal_to(Datum::long(10)),
            PredicateOperator::Eq,
        ),
        (
            Reference::new("id").not_equal_to(Datum::long(10)),
            PredicateOperator::NotEq,
        ),
    ];
    for (pred, op) in cases {
        assert!(matches!(pred, Predicate::Binary(_)));
        assert!(op.clone().is_binary());
        assert!(!op.clone().is_unary());
        assert!(!op.is_set());
    }

    assert!(matches!(
        Reference::new("name").is_null(),
        Predicate::Unary(_)
    ));
    assert!(matches!(
        Reference::new("name").is_not_null(),
        Predicate::Unary(_)
    ));
    assert!(matches!(
        Reference::new("id").is_in([Datum::long(1), Datum::long(2)]),
        Predicate::Set(_)
    ));
    assert!(matches!(
        Reference::new("id").is_not_in([Datum::long(1), Datum::long(2)]),
        Predicate::Set(_)
    ));
    // The remaining unary and binary builders of section 4.3.
    assert!(matches!(
        Reference::new("score").is_nan(),
        Predicate::Unary(_)
    ));
    assert!(matches!(
        Reference::new("score").is_not_nan(),
        Predicate::Unary(_)
    ));
    for pred in [
        Reference::new("name").starts_with(Datum::string("ice")),
        Reference::new("name").not_starts_with(Datum::string("ice")),
    ] {
        assert!(matches!(pred, Predicate::Binary(_)));
    }
    assert!(PredicateOperator::StartsWith.is_binary());
    assert!(PredicateOperator::NotStartsWith.is_binary());
    assert!(PredicateOperator::IsNan.is_unary());
    assert!(PredicateOperator::In.is_set());
}

#[test]
fn g06_predicate_build__and_or_nest() {
    let left = Reference::new("id").greater_than(Datum::long(1));
    let right = Reference::new("name").is_not_null();
    assert!(matches!(
        left.clone().and(right.clone()),
        Predicate::And(_)
    ));
    assert!(matches!(left.clone().or(right.clone()), Predicate::Or(_)));

    // `and`/`or` consume both operands and are not commuted or simplified.
    let combined = left.clone().and(right.clone());
    assert_ne!(combined, right.and(left));

    // Section 4.3: `Predicate` is serde and `Display`. The emitted text is not
    // scored (section 8); the round trip through `serde_json` is.
    let json = serde_json::to_string(&combined).expect("serialise predicate");
    let back: Predicate = serde_json::from_str(&json).expect("deserialise predicate");
    assert_eq!(back, combined);
    let _ = format!("{combined}");
}

#[test]
fn g06_predicate_build__operator_negation_table() {
    // spec.md "Scan Planning": each operator has a complementary operator.
    assert_eq!(
        PredicateOperator::IsNull.negate(),
        PredicateOperator::NotNull
    );
    assert_eq!(
        PredicateOperator::NotNull.negate(),
        PredicateOperator::IsNull
    );
    assert_eq!(PredicateOperator::IsNan.negate(), PredicateOperator::NotNan);
    assert_eq!(PredicateOperator::NotNan.negate(), PredicateOperator::IsNan);
    assert_eq!(
        PredicateOperator::LessThan.negate(),
        PredicateOperator::GreaterThanOrEq
    );
    assert_eq!(
        PredicateOperator::LessThanOrEq.negate(),
        PredicateOperator::GreaterThan
    );
    assert_eq!(
        PredicateOperator::GreaterThan.negate(),
        PredicateOperator::LessThanOrEq
    );
    assert_eq!(
        PredicateOperator::GreaterThanOrEq.negate(),
        PredicateOperator::LessThan
    );
    assert_eq!(PredicateOperator::Eq.negate(), PredicateOperator::NotEq);
    assert_eq!(PredicateOperator::NotEq.negate(), PredicateOperator::Eq);
    assert_eq!(PredicateOperator::In.negate(), PredicateOperator::NotIn);
    assert_eq!(PredicateOperator::NotIn.negate(), PredicateOperator::In);
    assert_eq!(
        PredicateOperator::StartsWith.negate(),
        PredicateOperator::NotStartsWith
    );
    assert_eq!(
        PredicateOperator::NotStartsWith.negate(),
        PredicateOperator::StartsWith
    );
    // Negation is an involution.
    for op in [
        PredicateOperator::IsNull,
        PredicateOperator::LessThan,
        PredicateOperator::Eq,
        PredicateOperator::In,
        PredicateOperator::StartsWith,
    ] {
        assert_eq!(op.clone().negate().negate(), op);
    }

    // Section 4.3 fixes the `#[repr(u16)]` discriminants, which is what makes
    // the operator kind readable from the value.
    for (op, code) in [
        (PredicateOperator::IsNull, 101u16),
        (PredicateOperator::NotNull, 102),
        (PredicateOperator::IsNan, 103),
        (PredicateOperator::NotNan, 104),
        (PredicateOperator::LessThan, 201),
        (PredicateOperator::LessThanOrEq, 202),
        (PredicateOperator::GreaterThan, 203),
        (PredicateOperator::GreaterThanOrEq, 204),
        (PredicateOperator::Eq, 205),
        (PredicateOperator::NotEq, 206),
        (PredicateOperator::StartsWith, 207),
        (PredicateOperator::NotStartsWith, 208),
        (PredicateOperator::In, 301),
        (PredicateOperator::NotIn, 302),
    ] {
        assert_eq!(op as u16, code);
    }
}

// ---------------------------------------------------------------------------
// g07_predicate_rewrite_not — spec.md "Scan Planning". Partition pruning is
// defined over predicates without `NOT`, so `NOT` must be pushed down into the
// leaves by negating operators (De Morgan).
// ---------------------------------------------------------------------------

#[test]
fn g07_predicate_rewrite_not__negates_leaf_operators() {
    assert_eq!(
        Reference::new("id").less_than(Datum::long(5)).negate(),
        Reference::new("id").greater_than_or_equal_to(Datum::long(5))
    );
    assert_eq!(
        Reference::new("name").is_null().negate(),
        Reference::new("name").is_not_null()
    );
    assert_eq!(
        Reference::new("id")
            .is_in([Datum::long(1), Datum::long(2)])
            .negate(),
        Reference::new("id").is_not_in([Datum::long(1), Datum::long(2)])
    );
    assert_eq!(Predicate::AlwaysTrue.negate(), Predicate::AlwaysFalse);
    assert_eq!(Predicate::AlwaysFalse.negate(), Predicate::AlwaysTrue);
}

#[test]
fn g07_predicate_rewrite_not__applies_de_morgan() {
    let a = Reference::new("id").less_than(Datum::long(5));
    let b = Reference::new("id").greater_than(Datum::long(100));
    // NOT (a AND b) == (NOT a) OR (NOT b)
    assert_eq!(
        a.clone().and(b.clone()).negate(),
        a.clone().negate().or(b.clone().negate())
    );
    // NOT (a OR b) == (NOT a) AND (NOT b)
    assert_eq!(
        a.clone().or(b.clone()).negate(),
        a.negate().and(b.negate())
    );
}

#[test]
fn g07_predicate_rewrite_not__removes_every_not_node() {
    let a = Reference::new("id").less_than(Datum::long(5));
    let b = Reference::new("name").is_null();

    fn has_not(p: &Predicate) -> bool {
        match p {
            Predicate::Not(_) => true,
            Predicate::And(e) => e.inputs().iter().any(|i| has_not(i)),
            Predicate::Or(e) => e.inputs().iter().any(|i| has_not(i)),
            _ => false,
        }
    }

    let nested = !(a.clone().and(!b.clone()));
    assert!(has_not(&nested));
    let rewritten = nested.rewrite_not();
    assert!(!has_not(&rewritten));
    assert_eq!(
        rewritten,
        Reference::new("id")
            .greater_than_or_equal_to(Datum::long(5))
            .or(b)
    );

    // Predicates that already contain no NOT are unchanged.
    let plain = a.clone().and(Reference::new("name").is_not_null());
    assert_eq!(plain.clone().rewrite_not(), plain);
}

// ---------------------------------------------------------------------------
// g08_predicate_bind — spec.md "Scan Planning" + "Schemas and Data Types".
// Binding resolves a column name against a schema, producing a bound
// predicate whose term carries the resolved field (and therefore its field id).
// ---------------------------------------------------------------------------

#[test]
fn g08_predicate_bind__binds_known_columns() {
    let schema = scan_schema_ref();
    let bound = Reference::new("id")
        .less_than(Datum::long(10))
        .bind(schema.clone(), true)
        .expect("bind id < 10");
    assert!(matches!(bound, BoundPredicate::Binary(_)));

    let bound = Reference::new("name")
        .is_null()
        .bind(schema.clone(), true)
        .expect("bind name IS NULL");
    assert!(matches!(bound, BoundPredicate::Unary(_)));

    let bound = Reference::new("id")
        .is_in([Datum::long(1), Datum::long(2)])
        .bind(schema, true)
        .expect("bind id IN (1, 2)");
    assert!(matches!(bound, BoundPredicate::Set(_)));

    // Section 4.3: `BoundPredicate` is serde and `Display` too.
    let json = serde_json::to_string(&bound).expect("serialise bound predicate");
    let back: BoundPredicate =
        serde_json::from_str(&json).expect("deserialise bound predicate");
    assert_eq!(back, bound);
    let _ = format!("{bound}");
}

#[test]
fn g08_predicate_bind__unknown_column_fails() {
    let schema = scan_schema_ref();
    assert!(Reference::new("does_not_exist")
        .less_than(Datum::long(1))
        .bind(schema.clone(), true)
        .is_err());
    assert!(Reference::new("does_not_exist")
        .is_null()
        .bind(schema.clone(), true)
        .is_err());
    // A failure anywhere in the tree fails the whole bind.
    assert!(Reference::new("id")
        .less_than(Datum::long(1))
        .and(Reference::new("nope").is_null())
        .bind(schema, true)
        .is_err());
}

#[test]
fn g08_predicate_bind__case_sensitivity_is_selectable() {
    let schema = scan_schema_ref();
    // A differently-cased column name resolves only when case-insensitive
    // binding is requested.
    assert!(Reference::new("ID")
        .less_than(Datum::long(1))
        .bind(schema.clone(), true)
        .is_err());
    assert!(Reference::new("ID")
        .less_than(Datum::long(1))
        .bind(schema.clone(), false)
        .is_ok());
    assert!(Reference::new("NaMe")
        .is_null()
        .bind(schema.clone(), false)
        .is_ok());
    // The exact name always resolves, under either mode.
    assert!(Reference::new("id")
        .less_than(Datum::long(1))
        .bind(schema.clone(), true)
        .is_ok());
    assert!(Reference::new("id")
        .less_than(Datum::long(1))
        .bind(schema.clone(), false)
        .is_ok());
    // A name that matches no column in any case still fails.
    assert!(Reference::new("NOPE")
        .less_than(Datum::long(1))
        .bind(schema.clone(), false)
        .is_err());
    // Section 4.3: the bound reference keeps the *reference's* spelling, so a
    // case-insensitive bind is not equal to the exactly-cased one.
    let insensitive = Reference::new("ID")
        .less_than(Datum::long(1))
        .bind(schema.clone(), false)
        .expect("case-insensitive bind");
    let exact = Reference::new("id")
        .less_than(Datum::long(1))
        .bind(schema, true)
        .expect("exact bind");
    assert_ne!(insensitive, exact);
}

#[test]
fn g08_predicate_bind__literal_is_converted_to_the_column_type() {
    let schema = scan_schema_ref();
    // Section 4.3: the literal of a binary predicate is converted to the
    // field's type with `Datum::to` before the predicate is bound.
    let bound = Reference::new("score")
        .less_than(Datum::double(1.0))
        .bind(schema.clone(), true)
        .expect("bind score < 1.0");
    assert!(matches!(bound, BoundPredicate::Binary(_)));

    // A literal of a *different* primitive type is converted rather than
    // rejected: section 4.1 defines `int -> date`, so an int literal binds
    // against a `date` column.
    let dated: SchemaRef = Arc::new(
        Schema::builder()
            .with_schema_id(0)
            .with_fields(vec![NestedField::optional(
                1,
                "when",
                Type::Primitive(PrimitiveType::Date),
            )
            .into()])
            .build()
            .expect("build date schema"),
    );
    assert!(Reference::new("when")
        .less_than(Datum::int(17486))
        .bind(dated, true)
        .is_ok());

    // A literal that cannot be converted to the column type is rejected.
    assert!(Reference::new("id")
        .less_than(Datum::string("ten"))
        .bind(schema, true)
        .is_err());
}

#[test]
fn g08_predicate_bind__nan_predicate_requires_floating_column() {
    let schema = scan_schema_ref();
    assert!(Reference::new("score")
        .is_nan()
        .bind(schema.clone(), true)
        .is_ok());
    assert!(Reference::new("score")
        .is_not_nan()
        .bind(schema.clone(), true)
        .is_ok());
    // spec.md "Primitive Types": only float/double can hold NaN.
    assert!(Reference::new("id").is_nan().bind(schema.clone(), true).is_err());
    assert!(Reference::new("name").is_nan().bind(schema, true).is_err());
}

// ---------------------------------------------------------------------------
// g09_bound_predicate_simplify — spec.md "Schemas and Data Types" (required
// fields can never be null) and "Scan Planning" (predicates that can never
// match are resolved before pruning).
// ---------------------------------------------------------------------------

#[test]
fn g09_bound_predicate_simplify__required_column_null_checks() {
    let schema = scan_schema_ref();
    // `id` is required, so `id IS NULL` can never match and `id IS NOT NULL`
    // always matches.
    assert_eq!(
        Reference::new("id")
            .is_null()
            .bind(schema.clone(), true)
            .unwrap(),
        BoundPredicate::AlwaysFalse
    );
    assert_eq!(
        Reference::new("id")
            .is_not_null()
            .bind(schema.clone(), true)
            .unwrap(),
        BoundPredicate::AlwaysTrue
    );
    // `name` is optional, so neither collapses.
    assert!(matches!(
        Reference::new("name")
            .is_null()
            .bind(schema, true)
            .unwrap(),
        BoundPredicate::Unary(_)
    ));
}

#[test]
fn g09_bound_predicate_simplify__constant_folding_through_and_or() {
    let schema = scan_schema_ref();
    let real = Reference::new("id").less_than(Datum::long(10));
    let always_false = Reference::new("id").is_null(); // required column
    let always_true = Reference::new("id").is_not_null();

    assert_eq!(
        real.clone()
            .and(always_false.clone())
            .bind(schema.clone(), true)
            .unwrap(),
        BoundPredicate::AlwaysFalse
    );
    assert_eq!(
        real.clone()
            .and(always_true.clone())
            .bind(schema.clone(), true)
            .unwrap(),
        real.clone().bind(schema.clone(), true).unwrap()
    );
    assert_eq!(
        real.clone()
            .or(always_true)
            .bind(schema.clone(), true)
            .unwrap(),
        BoundPredicate::AlwaysTrue
    );
    assert_eq!(
        real.clone().or(always_false).bind(schema.clone(), true).unwrap(),
        real.bind(schema, true).unwrap()
    );
}

#[test]
fn g09_bound_predicate_simplify__set_predicates_collapse() {
    let schema = scan_schema_ref();
    // A single-element IN is exactly an equality test.
    assert_eq!(
        Reference::new("id")
            .is_in([Datum::long(7)])
            .bind(schema.clone(), true)
            .unwrap(),
        Reference::new("id")
            .equal_to(Datum::long(7))
            .bind(schema.clone(), true)
            .unwrap()
    );
    // A single-element NOT IN is exactly an inequality test.
    assert_eq!(
        Reference::new("id")
            .is_not_in([Datum::long(7)])
            .bind(schema.clone(), true)
            .unwrap(),
        Reference::new("id")
            .not_equal_to(Datum::long(7))
            .bind(schema.clone(), true)
            .unwrap()
    );
    // An empty IN matches nothing; an empty NOT IN matches everything.
    assert_eq!(
        Reference::new("id")
            .is_in([])
            .bind(schema.clone(), true)
            .unwrap(),
        BoundPredicate::AlwaysFalse
    );
    assert_eq!(
        Reference::new("id")
            .is_not_in([])
            .bind(schema, true)
            .unwrap(),
        BoundPredicate::AlwaysTrue
    );
}

#[test]
fn g09_bound_predicate_simplify__constants_bind_to_constants() {
    let schema = scan_schema_ref();
    assert_eq!(
        Predicate::AlwaysTrue.bind(schema.clone(), true).unwrap(),
        BoundPredicate::AlwaysTrue
    );
    assert_eq!(
        Predicate::AlwaysFalse.bind(schema.clone(), true).unwrap(),
        BoundPredicate::AlwaysFalse
    );
    // NOT of a constant folds to the opposite constant.
    assert_eq!(
        (!Predicate::AlwaysTrue).bind(schema, true).unwrap(),
        BoundPredicate::AlwaysFalse
    );
}

// ---------------------------------------------------------------------------
// g10_partition_spec_bind — spec.md "Partitioning" and "Partition Evolution".
// Scan planning projects a row filter onto the partition tuple, which requires
// a partition spec bound to a schema and its derived partition struct type.
// ---------------------------------------------------------------------------

#[test]
fn g10_partition_spec_bind__assigns_field_ids_from_1000() {
    // "Partition Evolution": partition field IDs are assigned starting at 1000
    // for v2 tables.
    let schema = scan_schema();
    let spec = PartitionSpec::builder(&schema)
        .with_spec_id(7)
        .add_partition_field("id", "id_bucket", Transform::Bucket(16))
        .unwrap()
        .add_partition_field("name", "name_trunc", Transform::Truncate(4))
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(spec.spec_id(), 7);
    assert_eq!(spec.fields().len(), 2);
    assert_eq!(spec.fields()[0].field_id, 1000);
    assert_eq!(spec.fields()[0].source_id, 1);
    assert_eq!(spec.fields()[0].name, "id_bucket");
    assert_eq!(spec.fields()[0].transform, Transform::Bucket(16));
    assert_eq!(spec.fields()[1].field_id, 1001);
    assert_eq!(spec.fields()[1].source_id, 2);
    assert!(!spec.is_unpartitioned());

    // Section 4.4: `with_last_assigned_field_id(n)` makes the next
    // auto-assigned partition field id `n + 1`.
    let resumed = PartitionSpec::builder(&schema)
        .with_spec_id(9)
        .with_last_assigned_field_id(1005)
        .add_partition_field("id", "id_part", Transform::Identity)
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(resumed.fields()[0].field_id, 1006);

    // Section 4.4: `to_unbound` drops the schema but keeps the assigned
    // partition field id, so re-binding reproduces it.
    let unbound = resumed.to_unbound();
    assert_eq!(unbound.spec_id(), Some(9));
    assert_eq!(unbound.fields().len(), 1);
    let rebound = unbound.bind(&schema).expect("re-bind the unbound spec");
    assert_eq!(rebound.spec_id(), 9);
    assert_eq!(rebound.fields().len(), 1);
    assert_eq!(rebound.fields()[0].field_id, 1006);
    assert_eq!(rebound.fields()[0].name, "id_part");
}

#[test]
fn g10_partition_spec_bind__partition_type_uses_transform_result_types() {
    // "Partitioning": the partition struct's field ids are the partition field
    // ids and its types are the transform result types.
    let schema = scan_schema();
    let spec = PartitionSpec::builder(&schema)
        .with_spec_id(0)
        .add_partition_field("id", "id_bucket", Transform::Bucket(16))
        .unwrap()
        .add_partition_field("id", "id_ident", Transform::Identity)
        .unwrap()
        .build()
        .unwrap();
    let ty = spec.partition_type(&schema).unwrap();
    assert_eq!(ty.fields().len(), 2);
    assert_eq!(ty.fields()[0].id, 1000);
    assert_eq!(ty.fields()[0].name, "id_bucket");
    assert_eq!(ty.fields()[0].field_type.as_ref(), &Type::Primitive(PrimitiveType::Int));
    assert_eq!(ty.fields()[1].id, 1001);
    assert_eq!(
        ty.fields()[1].field_type.as_ref(),
        &Type::Primitive(PrimitiveType::Long)
    );
    // Partition fields are always optional in the partition struct.
    assert!(!ty.fields()[0].required);
}

#[test]
fn g10_partition_spec_bind__rejects_invalid_specs() {
    let schema = scan_schema();
    // Source column must exist in the schema.
    assert!(PartitionSpec::builder(&schema)
        .add_partition_field("no_such_column", "p", Transform::Identity)
        .is_err());
    // Partition field names must be unique.
    assert!(PartitionSpec::builder(&schema)
        .add_partition_field("id", "p", Transform::Identity)
        .unwrap()
        .add_partition_field("name", "p", Transform::Identity)
        .is_err());
    // The transform must be valid for the source column's type.
    assert!(PartitionSpec::builder(&schema)
        .add_partition_field("name", "n_year", Transform::Year)
        .is_err());
}

#[test]
fn g10_partition_spec_bind__unbound_spec_binds_to_a_schema() {
    let schema = scan_schema();
    let unbound = UnboundPartitionSpec::builder()
        .with_spec_id(3)
        .add_partition_field(1, "id_part".to_string(), Transform::Identity)
        .unwrap()
        .build();
    assert_eq!(unbound.spec_id(), Some(3));
    let bound = unbound.bind(&schema).expect("bind partition spec");
    assert_eq!(bound.spec_id(), 3);
    assert_eq!(bound.fields()[0].source_id, 1);
    assert_eq!(bound.fields()[0].transform, Transform::Identity);

    // Section 4.4: the bound builder also accepts a whole
    // `UnboundPartitionField` — one an `UnboundPartitionSpec` already carries —
    // and validates it on the same call.
    let template = UnboundPartitionSpec::builder()
        .add_partition_field(1, "id_bucket".to_string(), Transform::Bucket(8))
        .unwrap()
        .build();
    let spec = PartitionSpec::builder(&schema)
        .with_spec_id(4)
        .add_unbound_field(template.fields()[0].clone())
        .expect("add unbound field")
        .build()
        .unwrap();
    assert_eq!(spec.fields().len(), 1);
    assert_eq!(spec.fields()[0].field_id, 1000);
    assert_eq!(spec.fields()[0].source_id, 1);
    assert_eq!(spec.fields()[0].transform, Transform::Bucket(8));
    // An unbound field naming no schema column is rejected on that same call.
    let orphan = UnboundPartitionSpec::builder()
        .add_partition_field(99, "nope".to_string(), Transform::Identity)
        .unwrap()
        .build();
    assert!(PartitionSpec::builder(&schema)
        .add_unbound_field(orphan.fields()[0].clone())
        .is_err());
}

// ---------------------------------------------------------------------------
// g11_scan_builder — spec.md "Scan Planning". A scan is planned against one
// snapshot of one table; the builder resolves the snapshot and validates the
// projected column list before any I/O.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g11_scan_builder__defaults_to_the_current_snapshot() {
    let dir = tempfile::TempDir::new().unwrap();
    let table = load_table(&dir).await;
    let scan = table.scan().build().expect("build scan");
    assert_eq!(scan.snapshot().snapshot_id(), SNAPSHOT_ID);
    assert_eq!(scan.snapshot().sequence_number(), 1);
    // Section 4.5: an empty `column_names()` means every column.
    assert!(scan.column_names().is_empty());
    // Section 4.5: the batch size and concurrency limit are accepted and do
    // not change which snapshot or columns the scan resolves.
    let tuned = table
        .scan()
        .with_batch_size(Some(1024))
        .with_concurrency_limit(2)
        .build()
        .expect("build tuned scan");
    assert_eq!(tuned.snapshot().snapshot_id(), SNAPSHOT_ID);
    assert!(tuned.column_names().is_empty());
}

#[tokio::test]
async fn g11_scan_builder__scans_a_table_built_from_metadata() {
    // Section 4.5: a `Table` is built directly from a `FileIO`, a
    // `TableMetadata` and an identifier, and scans exactly like a
    // `StaticTable` does.
    let dir = tempfile::TempDir::new().unwrap();
    let metadata_path = write_table(&dir).await;
    let io = FileIOBuilder::new_fs_io().build().expect("fs io");
    let metadata: TableMetadata =
        serde_json::from_str(&std::fs::read_to_string(&metadata_path).unwrap())
            .expect("parse table metadata");
    let table = Table::builder()
        .file_io(io)
        .metadata(metadata)
        .identifier(TableIdent::from_strs(["ns", "t"]).expect("table ident"))
        .metadata_location(metadata_path.clone())
        .readonly(false)
        .build();
    assert_eq!(table.metadata_location(), Some(metadata_path.as_str()));
    assert!(!table.readonly());
    assert_eq!(
        table.metadata_ref().current_snapshot().unwrap().snapshot_id(),
        SNAPSHOT_ID
    );
    assert_eq!(
        table.metadata().current_schema().as_struct().fields().len(),
        3
    );
    let scan = table.scan().select(["id"]).build().expect("build scan");
    assert_eq!(scan.column_names(), ["id".to_string()]);
    assert!(scan.plan_files().await.is_ok());
    // The table's own `FileIO` is the one the scan reads through.
    assert!(table.file_io().is_exist(&metadata_path).await.unwrap());
}

#[tokio::test]
async fn g11_scan_builder__selects_named_columns() {
    let dir = tempfile::TempDir::new().unwrap();
    let table = load_table(&dir).await;
    let scan = table.scan().select(["id", "name"]).build().expect("build scan");
    assert_eq!(scan.column_names(), ["id".to_string(), "name".to_string()]);

    // Section 4.5: `select_all` clears any prior `select`.
    let scan = table.scan().select(["id"]).select_all().build().unwrap();
    assert!(scan.column_names().is_empty());
}

#[tokio::test]
async fn g11_scan_builder__rejects_unknown_column_and_snapshot() {
    let dir = tempfile::TempDir::new().unwrap();
    let table = load_table(&dir).await;
    assert!(table.scan().select(["not_a_column"]).build().is_err());
    assert!(table.scan().snapshot_id(1).build().is_err());
    // The snapshot recorded in the metadata does resolve.
    assert!(table.scan().snapshot_id(SNAPSHOT_ID).build().is_ok());
}

/// A table whose schema carries a **struct** column, for the projection
/// rejections of section 4.5. Its manifest list is never read: the projection
/// is validated by `TableScanBuilder::build`, before any I/O.
async fn nested_column_table() -> StaticTable {
    let metadata: TableMetadata = serde_json::from_str(
        r#"{
          "format-version": 2,
          "table-uuid": "1d5e0f8c-1b6a-4a0e-9a4d-3d2c7e1f5a60",
          "location": "/tmp/nested",
          "last-sequence-number": 1,
          "last-updated-ms": 1602638573590,
          "last-column-id": 3,
          "current-schema-id": 0,
          "schemas": [{"type": "struct", "schema-id": 0, "fields": [
            {"id": 1, "name": "id", "required": true, "type": "long"},
            {"id": 2, "name": "info", "required": false, "type": {
              "type": "struct", "fields": [
                {"id": 3, "name": "note", "required": false, "type": "string"}
              ]}}
          ]}],
          "default-spec-id": 0,
          "partition-specs": [{"spec-id": 0, "fields": []}],
          "last-partition-id": 999,
          "default-sort-order-id": 0,
          "sort-orders": [{"order-id": 0, "fields": []}],
          "properties": {},
          "current-snapshot-id": 11,
          "snapshots": [
            {"snapshot-id": 11, "timestamp-ms": 1515100955770, "sequence-number": 1,
             "summary": {"operation": "append"},
             "manifest-list": "/tmp/nested/snap-11.avro", "schema-id": 0}
          ],
          "snapshot-log": [{"snapshot-id": 11, "timestamp-ms": 1515100955770}],
          "metadata-log": []
        }"#,
    )
    .expect("parse nested-column metadata");
    let io = FileIOBuilder::new_fs_io().build().expect("fs io");
    StaticTable::from_metadata(
        metadata,
        TableIdent::from_strs(["ns", "nested"]).expect("table ident"),
        io,
    )
    .await
    .expect("static table")
}

#[tokio::test]
async fn g11_scan_builder__rejects_nested_and_non_primitive_columns() {
    // Section 4.5: `build()` must fail when a selected column is unknown,
    // nested or non-primitive. `info` is a struct column, and `info.note` is
    // a field nested inside it — reachable by its dotted schema name but not
    // a direct child of the schema.
    let table = nested_column_table().await;
    assert!(table.scan().select(["info"]).build().is_err());
    assert!(table.scan().select(["info.note"]).build().is_err());
    // A primitive top-level column of the same table still projects, so the
    // rejection is about the column, not about the table.
    assert!(table.scan().select(["id"]).build().is_ok());
}

#[tokio::test]
async fn g11_scan_builder__filter_is_bound_against_the_snapshot_schema() {
    let dir = tempfile::TempDir::new().unwrap();
    let table = load_table(&dir).await;
    assert!(table
        .scan()
        .with_filter(Reference::new("id").greater_than(Datum::long(3)))
        .build()
        .is_ok());
    // A filter naming a column that is not in the snapshot schema cannot bind.
    assert!(table
        .scan()
        .with_filter(Reference::new("nope").greater_than(Datum::long(3)))
        .build()
        .is_err());
    // A failure anywhere in the filter tree fails the whole scan build.
    assert!(table
        .scan()
        .with_filter(
            Reference::new("id")
                .greater_than(Datum::long(3))
                .and(Reference::new("nope").is_null())
        )
        .build()
        .is_err());
    // The case-sensitivity setting is accepted and does not change the result
    // for an exactly-cased filter.
    assert!(table
        .scan()
        .with_case_sensitive(false)
        .with_filter(Reference::new("id").greater_than(Datum::long(3)))
        .build()
        .is_ok());
}

// ---------------------------------------------------------------------------
// g12_scan_plan_files — spec.md "Scan Planning": "Scans are planned by reading
// the manifest files for the current snapshot."
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g12_scan_plan_files__plans_from_the_current_snapshot() {
    let dir = tempfile::TempDir::new().unwrap();
    let table = load_table(&dir).await;
    let scan = table.scan().build().expect("build scan");
    assert!(scan.plan_files().await.is_ok());
}

#[tokio::test]
async fn g12_scan_plan_files__plans_with_a_partition_filter() {
    let dir = tempfile::TempDir::new().unwrap();
    let table = load_table(&dir).await;
    // The filter is projected onto the identity-partitioned `id` column and
    // then evaluated against the manifest's partition field summaries.
    for predicate in [
        Reference::new("id").greater_than(Datum::long(3)),
        Reference::new("id").less_than(Datum::long(0)),
        Reference::new("id")
            .greater_than_or_equal_to(Datum::long(1))
            .and(Reference::new("id").less_than_or_equal_to(Datum::long(20))),
    ] {
        let scan = table
            .scan()
            .with_filter(predicate)
            .build()
            .expect("build filtered scan");
        assert!(scan.plan_files().await.is_ok());
    }
}

#[tokio::test]
async fn g12_scan_plan_files__missing_manifest_list_is_an_error() {
    let dir = tempfile::TempDir::new().unwrap();
    let table = load_table(&dir).await;
    let manifest_list = table
        .metadata()
        .current_snapshot()
        .unwrap()
        .manifest_list()
        .to_string();
    std::fs::remove_file(&manifest_list).expect("remove manifest list");
    let scan = table.scan().build().expect("build scan");
    assert!(scan.plan_files().await.is_err());
}

#[tokio::test]
async fn g12_scan_plan_files__table_without_snapshots_cannot_be_scanned() {
    // "Scan Planning" requires a snapshot; metadata with no current snapshot
    // has nothing to plan.
    let metadata: TableMetadata = serde_json::from_str(
        r#"{
          "format-version": 2,
          "table-uuid": "9c12d441-03fe-4693-9a96-a0705ddf69c1",
          "location": "/tmp/empty",
          "last-sequence-number": 0,
          "last-updated-ms": 1602638573590,
          "last-column-id": 1,
          "current-schema-id": 0,
          "schemas": [{"type": "struct", "schema-id": 0, "fields": [
            {"id": 1, "name": "id", "required": true, "type": "long"}]}],
          "default-spec-id": 0,
          "partition-specs": [{"spec-id": 0, "fields": []}],
          "last-partition-id": 999,
          "default-sort-order-id": 0,
          "sort-orders": [{"order-id": 0, "fields": []}],
          "properties": {},
          "snapshots": [],
          "snapshot-log": [],
          "metadata-log": []
        }"#,
    )
    .expect("parse metadata");
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let table = StaticTable::from_metadata(
        metadata,
        TableIdent::from_strs(["ns", "empty"]).unwrap(),
        io,
    )
    .await
    .expect("static table");
    assert!(table.scan().build().is_err());
}

// ---------------------------------------------------------------------------
// g13_scan_inputs — spec.md "Manifest Lists" (partition field summaries) and
// "Manifests" (per-file column bounds and counts). These are the inputs scan
// planning prunes on, read back through `FileIO` exactly as a scan reads them.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g13_scan_inputs__manifest_list_carries_field_summaries() {
    let dir = tempfile::TempDir::new().unwrap();
    let table = load_table(&dir).await;
    // `metadata()` yields an owned `TableMetadataRef`, so the snapshot outlives
    // the `StaticTable` that `into_table` consumes.
    let metadata = table.metadata();
    let snapshot = metadata.current_snapshot().unwrap().clone();
    let list = snapshot
        .load_manifest_list(table.into_table().file_io(), &metadata)
        .await
        .expect("load manifest list");

    assert_eq!(list.entries().len(), 1);
    let entry = &list.entries()[0];
    assert_eq!(entry.content, ManifestContentType::Data);
    assert_eq!(entry.partition_spec_id, 0);
    assert_eq!(entry.added_snapshot_id, SNAPSHOT_ID);
    assert_eq!(entry.added_files_count, Some(2));
    assert_eq!(entry.existing_files_count, Some(0));
    assert_eq!(entry.deleted_files_count, Some(0));
    assert_eq!(entry.added_rows_count, Some(12));
    // "Manifest Lists": sequence numbers unassigned at write time are
    // inherited from the snapshot's sequence number.
    assert_eq!(entry.sequence_number, 1);

    // One field summary per partition field, in partition-spec order.
    assert_eq!(entry.partitions.len(), 1);
    let summary = &entry.partitions[0];
    assert!(!summary.contains_null);
    // Both data files reported a zero NaN count for the source column.
    assert_eq!(summary.contains_nan, Some(false));
    assert_eq!(summary.lower_bound, Some(Datum::long(1)));
    assert_eq!(summary.upper_bound, Some(Datum::long(20)));

    // Section 4.6: `consume_entries` yields the same entries by value.
    let consumed: Vec<ManifestFile> = list.consume_entries().into_iter().collect();
    assert_eq!(consumed.len(), 1);
    assert_eq!(consumed[0].added_files_count, Some(2));
}

#[tokio::test]
async fn g13_scan_inputs__manifest_entries_carry_column_metrics() {
    let dir = tempfile::TempDir::new().unwrap();
    let table = load_table(&dir).await;
    let metadata = table.metadata();
    let snapshot = metadata.current_snapshot().unwrap().clone();
    let inner = table.into_table();
    let list = snapshot
        .load_manifest_list(inner.file_io(), &metadata)
        .await
        .unwrap();
    let manifest = list.entries()[0]
        .load_manifest(inner.file_io())
        .await
        .expect("load manifest");

    assert_eq!(manifest.entries().len(), 2);
    let mut paths: Vec<&str> = manifest.entries().iter().map(|e| e.file_path()).collect();
    paths.sort();
    assert!(paths[0].ends_with("data-a.parquet"));
    assert!(paths[1].ends_with("data-b.parquet"));

    for e in manifest.entries() {
        // "Scan Planning": entries marked DELETED are not used in a scan.
        assert!(e.is_alive());
        assert_eq!(e.content_type(), DataContentType::Data);
        assert_eq!(e.file_format(), DataFileFormat::Parquet);
    }

    let a = manifest
        .entries()
        .iter()
        .find(|e| e.file_path().ends_with("data-a.parquet"))
        .unwrap();
    let df = a.data_file();
    assert_eq!(df.record_count(), 5);
    assert_eq!(df.file_size_in_bytes(), 1024);
    // "Manifests": bounds and counts are keyed by field id.
    assert_eq!(df.lower_bounds().get(&1), Some(&Datum::long(1)));
    assert_eq!(df.upper_bounds().get(&1), Some(&Datum::long(5)));
    assert_eq!(df.null_value_counts().get(&2), Some(&2u64));
    assert_eq!(df.value_counts().get(&1), Some(&5u64));
    // The partition tuple is stored per data file.
    assert_eq!(df.partition().iter().count(), 1);
}

#[tokio::test]
async fn g13_scan_inputs__deleted_entries_are_marked_not_alive() {
    // "Scan Planning": "Deleted entries in data and delete manifests (those
    // marked with status DELETED) are not used in a scan."
    let dir = tempfile::TempDir::new().unwrap();
    let root = dir.path().to_str().unwrap().to_string();
    let io = FileIOBuilder::new_fs_io().build().unwrap();
    let schema = scan_schema();
    let spec = PartitionSpec::builder(&schema)
        .with_spec_id(0)
        .build()
        .unwrap();
    let metadata = ManifestMetadata::builder()
        .schema(schema.clone())
        .schema_id(0)
        .partition_spec(spec)
        .content(ManifestContentType::Data)
        .format_version(FormatVersion::V2)
        .build();
    let mk = |name: &str| {
        DataFileBuilder::default()
            .content(DataContentType::Data)
            .file_path(format!("{root}/{name}"))
            .file_format(DataFileFormat::Parquet)
            .partition(Struct::empty())
            .record_count(1)
            .file_size_in_bytes(64)
            .build()
            .unwrap()
    };
    let manifest = Manifest::new(metadata, vec![
        ManifestEntry::builder()
            .status(ManifestStatus::Added)
            .data_file(mk("added.parquet"))
            .build(),
        ManifestEntry::builder()
            .status(ManifestStatus::Deleted)
            .snapshot_id(SNAPSHOT_ID)
            .sequence_number(1)
            .file_sequence_number(1)
            .data_file(mk("deleted.parquet"))
            .build(),
    ]);
    let path = format!("{root}/m-mixed.avro");
    let written = ManifestWriter::new(io.new_output(&path).unwrap(), SNAPSHOT_ID, vec![])
        .write(manifest)
        .await
        .expect("write manifest");
    assert_eq!(written.added_files_count, Some(1));
    assert_eq!(written.deleted_files_count, Some(1));

    let parsed = Manifest::parse_avro(&std::fs::read(&path).unwrap()).expect("parse manifest");
    let alive: Vec<&str> = parsed
        .entries()
        .iter()
        .filter(|e| e.is_alive())
        .map(|e| e.file_path())
        .collect();
    assert_eq!(alive.len(), 1);
    assert!(alive[0].ends_with("added.parquet"));

    // Section 4.6 / "Sequence Number Inheritance": a `Deleted` or `Existing`
    // entry must carry both sequence numbers, or `write` is an error. Only an
    // `Added` entry may inherit them.
    let metadata2 = ManifestMetadata::builder()
        .schema(schema.clone())
        .schema_id(0)
        .partition_spec(PartitionSpec::builder(&schema).with_spec_id(0).build().unwrap())
        .content(ManifestContentType::Data)
        .format_version(FormatVersion::V2)
        .build();
    let no_seq = Manifest::new(metadata2, vec![ManifestEntry::builder()
        .status(ManifestStatus::Deleted)
        .data_file(mk("no-seq.parquet"))
        .build()]);
    let bad_path = format!("{root}/m-no-seq.avro");
    assert!(
        ManifestWriter::new(io.new_output(&bad_path).unwrap(), SNAPSHOT_ID, vec![])
            .write(no_seq)
            .await
            .is_err()
    );
}

// ---------------------------------------------------------------------------
// g14_arrow_schema — spec.md Appendix A "Format-specific Requirements" /
// "Field IDs": Parquet field ids are stored in the field metadata under the
// key `PARQUET:field_id`, which is how Iceberg's column projection by field id
// survives a conversion to Arrow.
// ---------------------------------------------------------------------------

const PARQUET_FIELD_ID_KEY: &str = "PARQUET:field_id";

/// Appendix C spells a list type
/// `{"type": "list", "element-id": N, "element-required": b, "element": <type>}`,
/// and `Type` is serde, so this is the construction path the contract states
/// for `ListType` (milestone 1, section 4.1).
fn appendix_c_list_of_string(element_id: i32) -> Type {
    serde_json::from_value(serde_json::json!({
        "type": "list",
        "element-id": element_id,
        "element-required": true,
        "element": "string"
    }))
    .expect("Appendix C list type")
}

#[test]
fn g14_arrow_schema__field_ids_are_carried_in_metadata() {
    let schema = scan_schema();
    let arrow = schema_to_arrow_schema(&schema).expect("convert to arrow schema");
    for (name, id, required) in [("id", "1", true), ("name", "2", false), ("score", "3", false)] {
        let field = arrow.field_with_name(name).expect("field present");
        assert_eq!(
            field.metadata().get(PARQUET_FIELD_ID_KEY).map(String::as_str),
            Some(id),
            "field {name} must carry its Iceberg field id"
        );
        // spec.md "Schemas and Data Types": required fields are not nullable.
        assert_eq!(field.is_nullable(), !required);
    }
    assert_eq!(arrow.fields().len(), 3);
}

#[test]
fn g14_arrow_schema__round_trips_through_arrow() {
    // Column Projection is by field id, so the ids must survive both
    // directions of the conversion.
    let schema = Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "name", Type::Primitive(PrimitiveType::String)).into(),
            NestedField::optional(3, "flag", Type::Primitive(PrimitiveType::Boolean)).into(),
            NestedField::optional(4, "score", Type::Primitive(PrimitiveType::Double)).into(),
            NestedField::optional(5, "when", Type::Primitive(PrimitiveType::Date)).into(),
            NestedField::optional(6, "raw", Type::Primitive(PrimitiveType::Binary)).into(),
        ])
        .build()
        .unwrap();
    let arrow = schema_to_arrow_schema(&schema).expect("to arrow");
    let back = arrow_schema_to_schema(&arrow).expect("from arrow");
    assert_eq!(back.as_struct(), schema.as_struct());
    for id in 1..=6 {
        assert!(back.field_by_id(id).is_some(), "field {id} must survive");
    }
}

#[test]
fn g14_arrow_schema__nested_fields_keep_their_ids() {
    // spec.md "Column Projection": projection is by field id, and nested
    // fields (list elements, struct members) have ids of their own that must
    // survive the conversion in both directions.
    let schema = Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "tags", appendix_c_list_of_string(3)).into(),
            NestedField::optional(
                4,
                "info",
                Type::Struct(StructType::new(vec![NestedField::optional(
                    5,
                    "note",
                    Type::Primitive(PrimitiveType::String),
                )
                .into()])),
            )
            .into(),
        ])
        .build()
        .unwrap();

    let arrow = schema_to_arrow_schema(&schema).expect("to arrow");
    for (name, id) in [("id", "1"), ("tags", "2"), ("info", "4")] {
        assert_eq!(
            arrow
                .field_with_name(name)
                .unwrap()
                .metadata()
                .get(PARQUET_FIELD_ID_KEY)
                .map(String::as_str),
            Some(id)
        );
    }

    let back = arrow_schema_to_schema(&arrow).expect("from arrow");
    assert_eq!(back.as_struct(), schema.as_struct());
    for id in 1..=5 {
        assert!(back.field_by_id(id).is_some(), "field {id} must survive");
    }
}

// ---------------------------------------------------------------------------
// g15_view_metadata — view-spec.md "View Metadata" and Appendix A.
// ---------------------------------------------------------------------------

const VIEW_METADATA: &str = r#"{
  "view-uuid": "fa6506c3-7681-40c8-86dc-e36561f83385",
  "format-version": 1,
  "location": "s3://bucket/warehouse/default.db/event_agg",
  "current-version-id": 1,
  "properties": {"comment": "Daily event counts"},
  "versions": [
    {
      "version-id": 1,
      "timestamp-ms": 1573518431292,
      "schema-id": 1,
      "default-catalog": "prod",
      "default-namespace": ["default"],
      "summary": {"engine-name": "Spark", "engineVersion": "3.3.2"},
      "representations": [
        {
          "type": "sql",
          "sql": "SELECT\n    COUNT(1), CAST(event_ts AS DATE)\nFROM events\nGROUP BY 2",
          "dialect": "spark"
        }
      ]
    }
  ],
  "schemas": [
    {
      "schema-id": 1,
      "type": "struct",
      "fields": [
        {"id": 1, "name": "event_count", "required": false, "type": "int", "doc": "Count of events"},
        {"id": 2, "name": "event_date", "required": false, "type": "date"}
      ]
    }
  ],
  "version-log": [{"timestamp-ms": 1573518431292, "version-id": 1}]
}"#;

#[test]
fn g15_view_metadata__parses_the_documented_example() {
    let md: ViewMetadata = serde_json::from_str(VIEW_METADATA).expect("parse view metadata");
    assert_eq!(md.format_version(), ViewFormatVersion::V1);
    assert_eq!(
        md.uuid().to_string(),
        "fa6506c3-7681-40c8-86dc-e36561f83385"
    );
    assert_eq!(md.location(), "s3://bucket/warehouse/default.db/event_agg");
    assert_eq!(md.current_version_id(), 1);
    assert_eq!(
        md.properties().get("comment").map(String::as_str),
        Some("Daily event counts")
    );
    assert_eq!(md.versions().count(), 1);
    assert_eq!(md.history().len(), 1);
    assert_eq!(md.history()[0].version_id(), 1);
    assert_eq!(md.history()[0].timestamp_ms(), 1573518431292);
}

#[test]
fn g15_view_metadata__current_version_and_representation() {
    let md: ViewMetadata = serde_json::from_str(VIEW_METADATA).unwrap();
    let v = md.current_version();
    assert_eq!(v.version_id(), 1);
    assert_eq!(v.schema_id(), 1);
    assert_eq!(v.timestamp_ms(), 1573518431292);
    assert_eq!(v.default_catalog().map(String::as_str), Some("prod"));
    assert_eq!(v.default_namespace().as_ref(), &vec!["default".to_string()]);
    assert_eq!(
        v.summary().get("engine-name").map(String::as_str),
        Some("Spark")
    );
    assert_eq!(v.representations().len(), 1);
    match v.representations().iter().next().unwrap() {
        ViewRepresentation::Sql(sql) => {
            assert_eq!(sql.dialect, "spark");
            assert!(sql.sql.starts_with("SELECT"));
        }
    }
    // The version is reachable by id, and matches the current version.
    assert_eq!(md.version_by_id(1).unwrap().as_ref(), v.as_ref());
    assert!(md.version_by_id(99).is_none());
}

#[test]
fn g15_view_metadata__schemas_are_resolved_by_id() {
    let md: ViewMetadata = serde_json::from_str(VIEW_METADATA).unwrap();
    assert_eq!(md.schemas_iter().count(), 1);
    let schema = md.current_schema();
    assert_eq!(schema.schema_id(), 1);
    assert_eq!(schema.as_struct().fields().len(), 2);
    assert_eq!(schema.field_by_name("event_count").unwrap().id, 1);
    assert_eq!(
        schema.field_by_id(2).unwrap().field_type.as_ref(),
        &Type::Primitive(PrimitiveType::Date)
    );
    assert!(md.schema_by_id(1).is_some());
    assert!(md.schema_by_id(42).is_none());
    // The current version's schema id selects the same schema.
    assert_eq!(
        md.current_version().schema(&md).unwrap().schema_id(),
        schema.schema_id()
    );
}

#[test]
fn g15_view_metadata__inconsistent_documents_are_rejected() {
    // view-spec.md: "current-version-id" must reference a version present in
    // "versions", and every version's "schema-id" must reference a schema.
    let bad_version = VIEW_METADATA.replace("\"current-version-id\": 1", "\"current-version-id\": 9");
    assert!(serde_json::from_str::<ViewMetadata>(&bad_version).is_err());

    let bad_schema = VIEW_METADATA.replace("\"schema-id\": 1,\n      \"default-catalog\"", "\"schema-id\": 9,\n      \"default-catalog\"");
    assert!(serde_json::from_str::<ViewMetadata>(&bad_schema).is_err());

    // Only view format version 1 is defined.
    let bad_format = VIEW_METADATA.replace("\"format-version\": 1", "\"format-version\": 2");
    assert!(serde_json::from_str::<ViewMetadata>(&bad_format).is_err());
}

// ---------------------------------------------------------------------------
// g16_file_io — spec.md "File System Operations" and "Table Metadata": table
// locations are URIs, so the storage backend is selected by the URI scheme.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g16_file_io__local_scheme_round_trips_a_file() {
    let dir = tempfile::TempDir::new().unwrap();
    let root = dir.path().to_str().unwrap().to_string();
    let seed = format!("{root}/seed.txt");
    let path = format!("{root}/hello.txt");
    std::fs::write(&seed, b"iceberg").unwrap();

    let io = FileIOBuilder::new_fs_io().build().expect("fs io");
    assert!(!io.is_exist(&path).await.unwrap());

    // What `InputFile::read` yields is what `OutputFile::write` accepts, so a
    // file round trips through `FileIO` alone.
    let payload = io
        .new_input(&seed)
        .expect("seed input")
        .read()
        .await
        .expect("read seed");
    let out = io.new_output(&path).expect("output file");
    assert_eq!(out.location(), path);
    out.write(payload).await.expect("write through FileIO");
    assert!(io.is_exist(&path).await.unwrap());

    let input = io.new_input(&path).expect("input file");
    assert_eq!(input.location(), path);
    assert_eq!(input.read().await.unwrap().as_ref(), b"iceberg");
    assert_eq!(input.metadata().await.unwrap().size, 7);
    // The bytes on disk are the bytes that were written.
    assert_eq!(std::fs::read(&path).unwrap(), b"iceberg");

    io.delete(&path).await.expect("delete");
    assert!(!io.is_exist(&path).await.unwrap());
}

#[test]
fn g16_file_io__scheme_is_taken_from_the_location() {
    // A bare filesystem path and an explicit `file:` URI both resolve to the
    // local filesystem backend.
    assert!(FileIO::from_path("/tmp/some/table/metadata.json")
        .unwrap()
        .build()
        .is_ok());
    assert!(FileIO::from_path("file:///tmp/some/table/metadata.json")
        .unwrap()
        .build()
        .is_ok());
    assert!(FileIOBuilder::new("memory").build().is_ok());
    // `s3` and `s3a` are recognised scheme strings; only construction is
    // exercised, because PROFILE P3 requires no remote backend to work.
    assert!(FileIOBuilder::new("s3").build().is_ok());
    assert!(FileIOBuilder::new("s3a").build().is_ok());
    assert!(FileIO::from_path("s3://bucket/tbl/metadata.json")
        .unwrap()
        .build()
        .is_ok());
    // An unregistered scheme cannot be built.
    assert!(FileIOBuilder::new("no-such-scheme").build().is_err());
}

#[tokio::test]
async fn g16_file_io__memory_scheme_is_isolated_from_disk() {
    let io = FileIOBuilder::new("memory").build().expect("memory io");
    let path = "memory:/a/b/c.txt";
    // The in-memory backend addresses the same locations as any other scheme,
    // and reports the location it was handed.
    assert_eq!(io.new_output(path).expect("output").location(), path);
    assert_eq!(io.new_input(path).expect("input").location(), path);
    // Nothing on the real filesystem is consulted or created.
    assert!(!io.is_exist(path).await.unwrap());
    assert!(io.new_input(path).unwrap().read().await.is_err());
    assert!(!std::path::Path::new("/a/b/c.txt").exists());
}

// ---------------------------------------------------------------------------
// Additional case for milestone 02, from TEST_INTERFACE.md section 4.9.
// ---------------------------------------------------------------------------

/// TEST_INTERFACE.md 4.9: *"Recognised scheme strings, all of which
/// `FileIOBuilder::build` must accept: `"file"` and `""` (local filesystem),
/// `"memory"` … `"s3"` and `"s3a"`. A scheme name outside the set your crate
/// supports must fail at `FileIOBuilder::build`; the drivers check that with an
/// invented scheme name"*, plus `FileIO::from_path`, `InputFile::{read,
/// metadata}` and `OutputFile::write`.
#[tokio::test(flavor = "multi_thread")]
async fn g16_file_io__declared_schemes_and_byte_handles() {
    for scheme in ["file", "", "memory", "s3", "s3a"] {
        iceberg::io::FileIOBuilder::new(scheme)
            .build()
            .unwrap_or_else(|e| panic!("scheme {scheme:?} must be constructible: {e}"));
    }
    assert!(
        iceberg::io::FileIOBuilder::new("notascheme").build().is_err(),
        "an invented scheme name must fail at build"
    );

    let dir = tempfile::TempDir::new().unwrap();
    let base = dir.path().to_str().unwrap().to_string();
    let io = iceberg::io::FileIO::from_path(&base).unwrap().build().unwrap();

    // The bytes `InputFile::read` returns go straight back into
    // `OutputFile::write`, and `FileMetadata::size` reports the file's length.
    let source = format!("{base}/source.bin");
    std::fs::write(&source, b"iceberg").unwrap();
    let input = io.new_input(&source).unwrap();
    assert_eq!(input.metadata().await.unwrap().size, 7);
    let bytes = input.read().await.unwrap();
    let copy = format!("{base}/copy.bin");
    io.new_output(&copy).unwrap().write(bytes).await.unwrap();
    assert_eq!(std::fs::read(&copy).unwrap(), b"iceberg");
    assert!(io.is_exist(&copy).await.unwrap());
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
