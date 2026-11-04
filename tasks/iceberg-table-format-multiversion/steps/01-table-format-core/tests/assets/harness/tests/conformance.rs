// Verifier-owned conformance driver for milestone 01 (table format core).
//
// Every assertion here traces to the pinned Apache Iceberg table specification
// shipped at /app/iceberg-spec/spec.md, or to /app/TEST_INTERFACE.md, which
// states the public Rust surface the milestone must expose.
//
// Group = the `gNN_<name>__` prefix of the test function name. The runner
// invokes one `cargo test` per group and asserts the executed count.

use std::collections::HashMap;
use std::sync::Arc;

use iceberg::spec::*;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

/// Decode one Avro `long` in the zig-zag varint encoding the object container
/// file format uses for its header. Appendix A: manifests and manifest lists
/// are Avro object container files, whose header is always uncompressed.
fn avro_zigzag_long(bs: &[u8], pos: &mut usize) -> i64 {
    let mut shift = 0u32;
    let mut acc: u64 = 0;
    loop {
        let b = *bs
            .get(*pos)
            .expect("truncated Avro header while reading a long");
        *pos += 1;
        acc |= ((b & 0x7f) as u64) << shift;
        if b & 0x80 == 0 {
            break;
        }
        shift += 7;
        assert!(shift < 64, "malformed Avro varint in the file header");
    }
    ((acc >> 1) as i64) ^ -((acc & 1) as i64)
}

fn avro_take<'a>(bs: &'a [u8], pos: &mut usize, len: usize) -> &'a [u8] {
    let end = pos.checked_add(len).expect("Avro header length overflow");
    assert!(end <= bs.len(), "truncated Avro header");
    let out = &bs[*pos..end];
    *pos = end;
    out
}

/// The Avro object container file header: the magic `Obj\x01` followed by a
/// `map<bytes>` of file metadata. `avro.schema` in that map is the writer
/// schema the file was written with.
fn avro_file_metadata(bs: &[u8]) -> HashMap<String, Vec<u8>> {
    assert!(bs.len() > 4, "file is too short to be an Avro file");
    assert_eq!(&bs[..4], b"Obj\x01", "file must start with the Avro magic");
    let mut pos = 4usize;
    let mut out = HashMap::new();
    loop {
        let mut count = avro_zigzag_long(bs, &mut pos);
        if count == 0 {
            break;
        }
        if count < 0 {
            count = -count;
            let _block_size = avro_zigzag_long(bs, &mut pos);
        }
        for _ in 0..count {
            let klen = avro_zigzag_long(bs, &mut pos) as usize;
            let key = String::from_utf8(avro_take(bs, &mut pos, klen).to_vec())
                .expect("Avro metadata key must be UTF-8");
            let vlen = avro_zigzag_long(bs, &mut pos) as usize;
            let val = avro_take(bs, &mut pos, vlen).to_vec();
            out.insert(key, val);
        }
    }
    out
}

fn avro_writer_schema(bs: &[u8]) -> serde_json::Value {
    let meta = avro_file_metadata(bs);
    let raw = meta
        .get("avro.schema")
        .expect("the Avro header must record the writer schema under `avro.schema`");
    serde_json::from_slice(raw).expect("`avro.schema` must be a JSON Avro schema")
}

fn avro_metadata_string(meta: &HashMap<String, Vec<u8>>, key: &str) -> String {
    let raw = meta
        .get(key)
        .unwrap_or_else(|| panic!("the Avro file metadata must record `{key}`"));
    String::from_utf8(raw.clone()).unwrap_or_else(|_| panic!("`{key}` must be UTF-8"))
}

/// Field names of an Avro record schema, in declaration order.
fn avro_record_fields(schema: &serde_json::Value) -> Vec<String> {
    schema["fields"]
        .as_array()
        .unwrap_or_else(|| panic!("expected an Avro record schema, got {schema}"))
        .iter()
        .map(|f| {
            f["name"]
                .as_str()
                .expect("every Avro record field has a name")
                .to_string()
        })
        .collect()
}

fn avro_field_type<'a>(schema: &'a serde_json::Value, name: &str) -> &'a serde_json::Value {
    schema["fields"]
        .as_array()
        .expect("expected an Avro record schema")
        .iter()
        .find(|f| f["name"] == name)
        .unwrap_or_else(|| panic!("the writer schema must name the column `{name}`"))
        .get("type")
        .expect("an Avro record field has a type")
}

// ---------------------------------------------------------------------------
// g01_types  — spec.md "Schemas and Data Types" + Appendix C "Schemas"
// ---------------------------------------------------------------------------

#[test]
fn g01_types__primitive_json_names() {
    // Appendix C: primitive types serialize as JSON strings using the names
    // given in "Primitive Types".
    let cases: Vec<(PrimitiveType, &str)> = vec![
        (PrimitiveType::Boolean, "\"boolean\""),
        (PrimitiveType::Int, "\"int\""),
        (PrimitiveType::Long, "\"long\""),
        (PrimitiveType::Float, "\"float\""),
        (PrimitiveType::Double, "\"double\""),
        (PrimitiveType::Date, "\"date\""),
        (PrimitiveType::Time, "\"time\""),
        (PrimitiveType::Timestamp, "\"timestamp\""),
        (PrimitiveType::Timestamptz, "\"timestamptz\""),
        (PrimitiveType::String, "\"string\""),
        (PrimitiveType::Uuid, "\"uuid\""),
        (PrimitiveType::Binary, "\"binary\""),
    ];
    for (ty, json) in cases {
        let got = serde_json::to_string(&Type::Primitive(ty.clone())).unwrap();
        assert_eq!(got, json, "serialization of {ty:?}");
        let back: Type = serde_json::from_str(json).unwrap();
        assert_eq!(back, Type::Primitive(ty));
    }
}

#[test]
fn g01_types__parameterized_primitive_json() {
    // Appendix C: `fixed[L]` and `decimal(P, S)` are parameterized names.
    let fixed: Type = serde_json::from_str("\"fixed[16]\"").unwrap();
    assert_eq!(fixed, Type::Primitive(PrimitiveType::Fixed(16)));
    assert_eq!(serde_json::to_string(&fixed).unwrap(), "\"fixed[16]\"");

    // Appendix C lists BOTH `"decimal(9,2)"` and `"decimal(9, 2)"` as valid
    // JSON spellings, so only the parse and the round trip are asserted.
    let expected = Type::Primitive(PrimitiveType::Decimal {
        precision: 9,
        scale: 2,
    });
    for spelling in ["\"decimal(9,2)\"", "\"decimal(9, 2)\""] {
        let dec: Type = serde_json::from_str(spelling).unwrap();
        assert_eq!(dec, expected, "parsing {spelling}");
    }
    let encoded = serde_json::to_string(&expected).unwrap();
    let reparsed: Type = serde_json::from_str(&encoded).unwrap();
    assert_eq!(reparsed, expected, "decimal type name must round trip");
}

#[test]
fn g01_types__struct_list_map_json() {
    // Appendix C "Schemas": nested types serialize as objects carrying field ids.
    let json = r#"{
      "type": "struct",
      "fields": [
        {"id": 1, "name": "id", "required": true, "type": "long"},
        {"id": 2, "name": "data", "required": false, "type": "string", "doc": "payload"}
      ]
    }"#;
    let ty: Type = serde_json::from_str(json).unwrap();
    let Type::Struct(st) = &ty else {
        panic!("expected struct type, got {ty:?}")
    };
    assert_eq!(st.fields().len(), 2);
    assert!(st.field_by_id(1).unwrap().required);
    assert!(!st.field_by_id(2).unwrap().required);
    assert_eq!(st.field_by_name("data").unwrap().id, 2);
    assert_eq!(st.field_by_id(2).unwrap().doc.as_deref(), Some("payload"));

    let list: Type =
        serde_json::from_str(r#"{"type":"list","element-id":3,"element-required":true,"element":"int"}"#)
            .unwrap();
    assert!(matches!(list, Type::List(_)));

    let map: Type = serde_json::from_str(
        r#"{"type":"map","key-id":4,"key":"string","value-id":5,"value-required":false,"value":"double"}"#,
    )
    .unwrap();
    assert!(matches!(map, Type::Map(_)));
}

#[test]
fn g01_types__decimal_constraints() {
    // "Primitive Types": decimal precision is limited to 38.
    assert!(Type::decimal(38, 10).is_ok());
    assert!(Type::decimal(39, 10).is_err());
    // Appendix A: a decimal is stored as `fixed` using the minimum number of
    // bytes for the given precision, `minBytesRequired(P)`.
    assert_eq!(Type::decimal_required_bytes(9).unwrap(), 4);
    assert_eq!(Type::decimal_required_bytes(18).unwrap(), 8);
    assert_eq!(Type::decimal_required_bytes(38).unwrap(), 16);
    assert!(Type::decimal_required_bytes(39).is_err());
}

#[test]
fn g01_types__unknown_primitive_is_rejected() {
    // A name the specification does not define must not deserialize.
    assert!(serde_json::from_str::<Type>("\"int64\"").is_err());
    assert!(serde_json::from_str::<Type>("\"decimal(9)\"").is_err());
}

#[test]
fn g01_types__predicates_and_nested_field_helpers() {
    assert!(Type::Primitive(PrimitiveType::Int).is_primitive());
    assert!(!Type::Primitive(PrimitiveType::Int).is_struct());
    let st = Type::Struct(StructType::new(vec![NestedField::required(
        1,
        "id",
        Type::Primitive(PrimitiveType::Long),
    )
    .into()]));
    assert!(st.is_struct());
    assert!(!st.is_primitive());

    // Appendix C names the anonymous positions of a list and a map `element`,
    // `key` and `value`; the helpers build exactly those fields.
    let element = NestedField::list_element(3, Type::Primitive(PrimitiveType::Int), true);
    assert_eq!(element.id, 3);
    assert_eq!(element.name, "element");
    assert!(element.required);
    assert_eq!(
        element.field_type.as_ref(),
        &Type::Primitive(PrimitiveType::Int)
    );

    let key = NestedField::map_key_element(4, Type::Primitive(PrimitiveType::String));
    assert_eq!(key.id, 4);
    assert_eq!(key.name, "key");
    assert!(key.required, "a map key is always required");

    let value = NestedField::map_value_element(5, Type::Primitive(PrimitiveType::Double), false);
    assert_eq!(value.id, 5);
    assert_eq!(value.name, "value");
    assert!(!value.required);

    let documented =
        NestedField::required(9, "x", Type::Primitive(PrimitiveType::Int)).with_doc("a comment");
    assert_eq!(documented.doc.as_deref(), Some("a comment"));
    assert_eq!(documented.id, 9);
}

// ---------------------------------------------------------------------------
// g02_schema — spec.md "Schemas and Data Types", "Identifier Field IDs"
// ---------------------------------------------------------------------------

fn sample_schema() -> Schema {
    Schema::builder()
        .with_schema_id(1)
        .with_identifier_field_ids(vec![1])
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "name", Type::Primitive(PrimitiveType::String)).into(),
            NestedField::required(
                3,
                "ts",
                Type::Primitive(PrimitiveType::Timestamptz),
            )
            .into(),
        ])
        .build()
        .unwrap()
}

#[test]
fn g02_schema__lookup_by_id_and_name() {
    let schema = sample_schema();
    assert_eq!(schema.schema_id(), 1);
    assert_eq!(schema.field_by_id(2).unwrap().name, "name");
    assert_eq!(schema.field_by_name("ts").unwrap().id, 3);
    assert_eq!(schema.field_id_by_name("id"), Some(1));
    assert_eq!(schema.name_by_field_id(3), Some("ts"));
    assert!(schema.field_by_id(99).is_none());
    assert!(schema.field_by_name("absent").is_none());
    assert_eq!(schema.as_struct().fields().len(), 3);
}

#[test]
fn g02_schema__highest_field_id() {
    let schema = sample_schema();
    assert_eq!(schema.highest_field_id(), 3);
}

#[test]
fn g02_schema__identifier_fields_must_be_required() {
    // "Identifier Field IDs": identifier fields must be required, primitive,
    // and not floating point.
    let bad = Schema::builder()
        .with_schema_id(0)
        .with_identifier_field_ids(vec![2])
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "name", Type::Primitive(PrimitiveType::String)).into(),
        ])
        .build();
    assert!(bad.is_err(), "optional identifier field must be rejected");

    let bad_float = Schema::builder()
        .with_schema_id(0)
        .with_identifier_field_ids(vec![1])
        .with_fields(vec![NestedField::required(
            1,
            "d",
            Type::Primitive(PrimitiveType::Double),
        )
        .into()])
        .build();
    assert!(bad_float.is_err(), "double identifier field must be rejected");
}

#[test]
fn g02_schema__identifier_field_id_must_name_a_field() {
    // "Identifier Field IDs": an identifier field id that names no field in the
    // schema is invalid.
    let bad = Schema::builder()
        .with_schema_id(0)
        .with_identifier_field_ids(vec![7])
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "name", Type::Primitive(PrimitiveType::String)).into(),
        ])
        .build();
    assert!(
        bad.is_err(),
        "an identifier field id that names no field must be rejected"
    );
}

#[test]
fn g02_schema__nested_names_are_dotted() {
    let schema = Schema::builder()
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
    assert_eq!(schema.field_id_by_name("location.lat"), Some(2));
    assert_eq!(schema.name_by_field_id(3), Some("location.lon"));
}

// ---------------------------------------------------------------------------
// g03_schema_json — Appendix C "Schemas"
// ---------------------------------------------------------------------------

#[test]
fn g03_schema_json__roundtrip() {
    let schema = sample_schema();
    let value = serde_json::to_value(&schema).unwrap();
    assert_eq!(value["type"], "struct");
    assert_eq!(value["schema-id"], 1);
    assert_eq!(value["identifier-field-ids"], serde_json::json!([1]));
    assert_eq!(value["fields"][0]["id"], 1);
    assert_eq!(value["fields"][0]["name"], "id");
    assert_eq!(value["fields"][0]["required"], true);
    assert_eq!(value["fields"][0]["type"], "long");

    let back: Schema = serde_json::from_value(value).unwrap();
    assert_eq!(back.schema_id(), schema.schema_id());
    assert_eq!(back.as_struct(), schema.as_struct());
}

#[test]
fn g03_schema_json__parses_documented_example() {
    // Appendix C "Schemas" example document.
    let json = r#"{
      "type": "struct",
      "schema-id": 0,
      "fields": [
        {"id": 1, "name": "id", "required": true, "type": "uuid"},
        {"id": 2, "name": "data", "required": false, "type": "int"}
      ]
    }"#;
    let schema: Schema = serde_json::from_str(json).unwrap();
    assert_eq!(schema.schema_id(), 0);
    assert_eq!(
        schema.field_by_id(1).unwrap().field_type.as_ref(),
        &Type::Primitive(PrimitiveType::Uuid)
    );
}

#[test]
fn g03_schema_json__identifier_field_ids_omitted_when_empty() {
    let schema = Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
        ])
        .build()
        .unwrap();
    let value = serde_json::to_value(&schema).unwrap();
    assert!(
        value.get("identifier-field-ids").is_none(),
        "`identifier-field-ids` must be omitted when the schema has none, got {value}"
    );
    // `Schema: PartialEq`, and the emitted document parses back to an equal value.
    let back: Schema = serde_json::from_value(value).unwrap();
    assert_eq!(back, schema);
}

// ---------------------------------------------------------------------------
// g04_values_json — Appendix D "JSON single-value serialization"
// ---------------------------------------------------------------------------

fn json_roundtrip(ty: Type, json: serde_json::Value) -> Literal {
    let lit = Literal::try_from_json(json.clone(), &ty)
        .unwrap_or_else(|e| panic!("try_from_json failed for {ty:?}: {e}"))
        .unwrap_or_else(|| panic!("try_from_json returned None for {ty:?}"));
    let back = lit.clone().try_into_json(&ty).unwrap();
    assert_eq!(back, json, "json round trip for {ty:?}");
    lit
}

#[test]
fn g04_values_json__primitives() {
    json_roundtrip(
        Type::Primitive(PrimitiveType::Boolean),
        serde_json::json!(true),
    );
    json_roundtrip(Type::Primitive(PrimitiveType::Int), serde_json::json!(34));
    json_roundtrip(Type::Primitive(PrimitiveType::Long), serde_json::json!(34));
    json_roundtrip(
        Type::Primitive(PrimitiveType::Double),
        serde_json::json!(1.0),
    );
    json_roundtrip(
        Type::Primitive(PrimitiveType::String),
        serde_json::json!("iceberg"),
    );
}

#[test]
fn g04_values_json__date_and_time_strings() {
    // Appendix D: date is "YYYY-MM-DD", time is "HH:MM:SS.ssssss",
    // timestamp is ISO-8601 without zone, timestamptz has "+00:00".
    json_roundtrip(
        Type::Primitive(PrimitiveType::Date),
        serde_json::json!("2017-11-16"),
    );
    json_roundtrip(
        Type::Primitive(PrimitiveType::Time),
        serde_json::json!("22:31:08.123456"),
    );
    json_roundtrip(
        Type::Primitive(PrimitiveType::Timestamp),
        serde_json::json!("2017-11-16T22:31:08.123456"),
    );
    json_roundtrip(
        Type::Primitive(PrimitiveType::Timestamptz),
        serde_json::json!("2017-11-16T22:31:08.123456+00:00"),
    );
}

#[test]
fn g04_values_json__uuid_and_decimal_strings() {
    // Appendix D: uuid uses the lower-case hyphenated form; decimal uses the
    // string representation of the value, with the scale of the type.
    json_roundtrip(
        Type::Primitive(PrimitiveType::Uuid),
        serde_json::json!("f79c3e09-677c-4bbd-a479-3f349cb785e7"),
    );
    json_roundtrip(
        Type::Primitive(PrimitiveType::Decimal {
            precision: 9,
            scale: 2,
        }),
        serde_json::json!("14.20"),
    );
}

#[test]
fn g04_values_json__decimal_constructor() {
    // `Literal::decimal` takes the unscaled value; the scale comes from the
    // type at serialization time (Appendix D "decimal").
    let ty = Type::Primitive(PrimitiveType::Decimal {
        precision: 9,
        scale: 2,
    });
    let built = Literal::decimal(1420);
    let parsed = Literal::try_from_json(serde_json::json!("14.20"), &ty)
        .unwrap()
        .unwrap();
    assert_eq!(built, parsed);
    assert_eq!(
        built.try_into_json(&ty).unwrap(),
        serde_json::json!("14.20")
    );
}

#[test]
fn g04_values_json__null_maps_to_none() {
    let none = Literal::try_from_json(
        serde_json::Value::Null,
        &Type::Primitive(PrimitiveType::Int),
    )
    .unwrap();
    assert!(none.is_none(), "JSON null must decode to no literal");
}

// ---------------------------------------------------------------------------
// g05_values_binary — Appendix D "Binary single-value serialization"
// ---------------------------------------------------------------------------

#[test]
fn g05_values_binary__integers_are_little_endian() {
    // Appendix D: int is 4 bytes little-endian, long is 8 bytes little-endian.
    let v = Literal::try_from_bytes(&34i32.to_le_bytes(), &Type::Primitive(PrimitiveType::Int))
        .unwrap();
    assert_eq!(v, Literal::int(34));
    let v = Literal::try_from_bytes(&34i64.to_le_bytes(), &Type::Primitive(PrimitiveType::Long))
        .unwrap();
    assert_eq!(v, Literal::long(34));
}

#[test]
fn g05_values_binary__floats_and_bools() {
    // Appendix D: boolean is 0x00/0x01; float/double are little-endian IEEE 754.
    assert_eq!(
        Literal::try_from_bytes(&[0x00], &Type::Primitive(PrimitiveType::Boolean)).unwrap(),
        Literal::bool(false)
    );
    assert_eq!(
        Literal::try_from_bytes(&[0x01], &Type::Primitive(PrimitiveType::Boolean)).unwrap(),
        Literal::bool(true)
    );
    assert_eq!(
        Literal::try_from_bytes(&1.5f32.to_le_bytes(), &Type::Primitive(PrimitiveType::Float))
            .unwrap(),
        Literal::float(1.5f32)
    );
    assert_eq!(
        Literal::try_from_bytes(&1.5f64.to_le_bytes(), &Type::Primitive(PrimitiveType::Double))
            .unwrap(),
        Literal::double(1.5f64)
    );
}

#[test]
fn g05_values_binary__string_uuid_and_binary() {
    // Appendix D: string is UTF-8 bytes with no length prefix; UUID is
    // 16 big-endian bytes; fixed and binary are the bytes themselves.
    assert_eq!(
        Literal::try_from_bytes("iceberg".as_bytes(), &Type::Primitive(PrimitiveType::String))
            .unwrap(),
        Literal::string("iceberg")
    );
    let uuid_bytes: [u8; 16] = [
        0xF7, 0x9C, 0x3E, 0x09, 0x67, 0x7C, 0x4B, 0xBD, 0xA4, 0x79, 0x3F, 0x34, 0x9C, 0xB7, 0x85,
        0xE7,
    ];
    assert_eq!(
        Literal::try_from_bytes(&uuid_bytes, &Type::Primitive(PrimitiveType::Uuid)).unwrap(),
        Literal::uuid(
            "f79c3e09-677c-4bbd-a479-3f349cb785e7"
                .parse::<uuid::Uuid>()
                .unwrap()
        )
    );
    assert_eq!(
        Literal::try_from_bytes(&[0u8, 1, 2, 3], &Type::Primitive(PrimitiveType::Binary)).unwrap(),
        Literal::binary(vec![0u8, 1, 2, 3])
    );
    assert_eq!(
        Literal::try_from_bytes(&[0u8, 1, 2, 3], &Type::Primitive(PrimitiveType::Fixed(4))).unwrap(),
        Literal::fixed(vec![0u8, 1, 2, 3])
    );
}

// ---------------------------------------------------------------------------
// g06_transform_parse — spec.md "Partition Transforms" + Appendix C
// ---------------------------------------------------------------------------

#[test]
fn g06_transform_parse__names_round_trip() {
    let cases: Vec<(&str, Transform)> = vec![
        ("identity", Transform::Identity),
        ("void", Transform::Void),
        ("year", Transform::Year),
        ("month", Transform::Month),
        ("day", Transform::Day),
        ("hour", Transform::Hour),
        ("bucket[16]", Transform::Bucket(16)),
        ("truncate[3]", Transform::Truncate(3)),
        // `unknown` is the textual form reserved for a transform this release
        // does not recognise (TEST_INTERFACE.md 4.4).
        ("unknown", Transform::Unknown),
    ];
    for (name, transform) in cases {
        let parsed: Transform = name.parse().unwrap_or_else(|e| panic!("parse {name}: {e}"));
        assert_eq!(parsed, transform, "parsing {name}");
        assert_eq!(format!("{transform}"), name, "display of {transform:?}");
        assert_eq!(
            serde_json::to_string(&transform).unwrap(),
            format!("\"{name}\""),
            "json of {transform:?}"
        );
    }
}

#[test]
fn g06_transform_parse__rejects_unknown_names() {
    assert!("nosuchtransform".parse::<Transform>().is_err());
    assert!("bucket".parse::<Transform>().is_err());
    assert!("truncate[]".parse::<Transform>().is_err());

    // TEST_INTERFACE.md section 4 / PROFILE.md P7: `Error`, `ErrorKind` and
    // `Result` are public at the crate root, and `Result<T>` is
    // `std::result::Result<T, Error>` — which, with 4.4's `type Err = Error`,
    // is what these annotations check. Nothing here looks inside the error:
    // P7 leaves its variant, kind, message and source chain to you.
    let rejected: iceberg::Result<Transform> = "nosuchtransform".parse::<Transform>();
    let _err: iceberg::Error = rejected.unwrap_err();
    let _kind: Option<iceberg::ErrorKind> = None;
}

// ---------------------------------------------------------------------------
// g07_transform_types — spec.md "Partition Transforms" result-type table
// ---------------------------------------------------------------------------

#[test]
fn g07_transform_types__identity_and_void_keep_source_type() {
    let long = Type::Primitive(PrimitiveType::Long);
    assert_eq!(Transform::Identity.result_type(&long).unwrap(), long);
    let s = Type::Primitive(PrimitiveType::String);
    assert_eq!(Transform::Void.result_type(&s).unwrap(), s);
}

#[test]
fn g07_transform_types__bucket_produces_int() {
    for source in [
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
        let got = Transform::Bucket(8)
            .result_type(&Type::Primitive(source.clone()))
            .unwrap_or_else(|e| panic!("bucket over {source:?}: {e}"));
        assert_eq!(got, Type::Primitive(PrimitiveType::Int));
    }
    // Not a valid bucket source type.
    assert!(Transform::Bucket(8)
        .result_type(&Type::Primitive(PrimitiveType::Double))
        .is_err());
}

#[test]
fn g07_transform_types__truncate_keeps_source_type() {
    for source in [
        PrimitiveType::Int,
        PrimitiveType::Long,
        PrimitiveType::String,
        PrimitiveType::Binary,
    ] {
        let ty = Type::Primitive(source.clone());
        assert_eq!(Transform::Truncate(4).result_type(&ty).unwrap(), ty);
    }
    assert!(Transform::Truncate(4)
        .result_type(&Type::Primitive(PrimitiveType::Boolean))
        .is_err());
}

#[test]
fn g07_transform_types__temporal_produce_int() {
    // "Partition Transforms": year, month and day are defined over `date`,
    // `timestamp` and `timestamptz`; hour over `timestamp` and `timestamptz`.
    // Every one of them has result type `int`.
    for t in [Transform::Year, Transform::Month, Transform::Day] {
        for src in [
            PrimitiveType::Date,
            PrimitiveType::Timestamp,
            PrimitiveType::Timestamptz,
        ] {
            let got = t
                .result_type(&Type::Primitive(src.clone()))
                .unwrap_or_else(|e| panic!("{t:?} over {src:?}: {e}"));
            assert_eq!(
                got,
                Type::Primitive(PrimitiveType::Int),
                "{t:?} over {src:?} must produce int"
            );
        }
    }
    for src in [PrimitiveType::Timestamp, PrimitiveType::Timestamptz] {
        let got = Transform::Hour
            .result_type(&Type::Primitive(src.clone()))
            .unwrap_or_else(|e| panic!("hour over {src:?}: {e}"));
        assert_eq!(
            got,
            Type::Primitive(PrimitiveType::Int),
            "hour over {src:?} must produce int"
        );
    }
    // "hour" is not defined over `date`.
    assert!(Transform::Hour
        .result_type(&Type::Primitive(PrimitiveType::Date))
        .is_err());
    // No temporal transform is defined over `string`.
    assert!(Transform::Year
        .result_type(&Type::Primitive(PrimitiveType::String))
        .is_err());
}

// ---------------------------------------------------------------------------
// g08_transform_order — spec.md "Sorting" / transform ordering properties
// ---------------------------------------------------------------------------

#[test]
fn g08_transform_order__preserves_order() {
    assert!(Transform::Identity.preserves_order());
    assert!(Transform::Year.preserves_order());
    assert!(Transform::Month.preserves_order());
    assert!(Transform::Day.preserves_order());
    assert!(Transform::Hour.preserves_order());
    assert!(Transform::Truncate(4).preserves_order());
    assert!(!Transform::Bucket(8).preserves_order());
    assert!(!Transform::Void.preserves_order());
}

#[test]
fn g08_transform_order__satisfies_order_of() {
    // A finer-grained temporal transform satisfies a coarser one.
    assert!(Transform::Day.satisfies_order_of(&Transform::Year));
    assert!(Transform::Day.satisfies_order_of(&Transform::Month));
    assert!(Transform::Hour.satisfies_order_of(&Transform::Day));
    assert!(!Transform::Year.satisfies_order_of(&Transform::Day));
    assert!(Transform::Identity.satisfies_order_of(&Transform::Day));
    assert!(!Transform::Bucket(8).satisfies_order_of(&Transform::Identity));
}

#[test]
fn g08_transform_order__dedup_name() {
    // year/month/day/hour share one dedup name so that a spec may not contain
    // two time transforms of the same source field.
    assert_eq!(Transform::Year.dedup_name(), Transform::Month.dedup_name());
    assert_eq!(Transform::Day.dedup_name(), Transform::Hour.dedup_name());
    assert_ne!(
        Transform::Bucket(8).dedup_name(),
        Transform::Truncate(8).dedup_name()
    );
}

// ---------------------------------------------------------------------------
// g09_partition_spec — spec.md "Partitioning" + Appendix C "Partition Specs"
// ---------------------------------------------------------------------------

#[test]
fn g09_partition_spec__json_shape() {
    let json = r#"{
      "spec-id": 0,
      "fields": [
        {"source-id": 3, "field-id": 1000, "name": "ts_day", "transform": "day"},
        {"source-id": 1, "field-id": 1001, "name": "id_bucket", "transform": "bucket[16]"}
      ]
    }"#;
    let spec: PartitionSpec = serde_json::from_str(json).unwrap();
    assert_eq!(spec.spec_id, 0);
    assert_eq!(spec.fields.len(), 2);
    assert_eq!(spec.fields[0].source_id, 3);
    assert_eq!(spec.fields[0].field_id, 1000);
    assert_eq!(spec.fields[0].name, "ts_day");
    assert_eq!(spec.fields[0].transform, Transform::Day);
    assert_eq!(spec.fields[1].transform, Transform::Bucket(16));

    let value = serde_json::to_value(&spec).unwrap();
    assert_eq!(value["spec-id"], 0);
    assert_eq!(value["fields"][1]["field-id"], 1001);
    assert_eq!(value["fields"][1]["transform"], "bucket[16]");
}

#[test]
fn g09_partition_spec__partition_type_uses_result_types() {
    // "Partitioning": the partition tuple's field ids are the partition field
    // ids, and each field's type is the transform's result type.
    let schema = sample_schema();
    let spec: PartitionSpec = serde_json::from_str(
        r#"{"spec-id":0,"fields":[
             {"source-id":3,"field-id":1000,"name":"ts_day","transform":"day"},
             {"source-id":1,"field-id":1001,"name":"id_bucket","transform":"bucket[16]"}]}"#,
    )
    .unwrap();
    let ty = spec.partition_type(&schema).unwrap();
    assert_eq!(ty.fields().len(), 2);
    assert_eq!(ty.fields()[0].id, 1000);
    assert_eq!(ty.fields()[0].name, "ts_day");
    assert_eq!(ty.fields()[1].id, 1001);
    assert_eq!(
        ty.fields()[1].field_type.as_ref(),
        &Type::Primitive(PrimitiveType::Int)
    );
    // Partition fields are always optional in the partition tuple.
    assert!(!ty.fields()[0].required);
}

#[test]
fn g09_partition_spec__unpartitioned() {
    let spec: PartitionSpec =
        serde_json::from_str(r#"{"spec-id":0,"fields":[]}"#).unwrap();
    assert!(spec.is_unpartitioned());
    // A spec whose every field is `void` partitions nothing either.
    let voided: PartitionSpec = serde_json::from_str(
        r#"{"spec-id":0,"fields":[{"source-id":1,"field-id":1000,"name":"x","transform":"void"}]}"#,
    )
    .unwrap();
    assert!(voided.is_unpartitioned());
    let spec2: PartitionSpec = serde_json::from_str(
        r#"{"spec-id":0,"fields":[{"source-id":1,"field-id":1000,"name":"x","transform":"identity"}]}"#,
    )
    .unwrap();
    assert!(!spec2.is_unpartitioned());
}

#[test]
fn g09_partition_spec__unknown_source_field_is_rejected() {
    let schema = sample_schema();
    let spec: PartitionSpec = serde_json::from_str(
        r#"{"spec-id":0,"fields":[{"source-id":99,"field-id":1000,"name":"x","transform":"identity"}]}"#,
    )
    .unwrap();
    assert!(
        spec.partition_type(&schema).is_err(),
        "a partition field whose source id is not in the schema must be rejected"
    );
}

// ---------------------------------------------------------------------------
// g10_sort_order — spec.md "Sorting" + Appendix C "Sort Orders"
// ---------------------------------------------------------------------------

#[test]
fn g10_sort_order__json_shape() {
    let json = r#"{
      "order-id": 1,
      "fields": [
        {"transform": "identity", "source-id": 2, "direction": "asc", "null-order": "nulls-first"},
        {"transform": "bucket[4]", "source-id": 3, "direction": "desc", "null-order": "nulls-last"}
      ]
    }"#;
    let order: SortOrder = serde_json::from_str(json).unwrap();
    assert_eq!(order.order_id, 1);
    assert_eq!(order.fields.len(), 2);
    assert_eq!(order.fields[0].source_id, 2);
    assert_eq!(order.fields[0].direction, SortDirection::Ascending);
    assert_eq!(order.fields[0].null_order, NullOrder::First);
    assert_eq!(order.fields[1].direction, SortDirection::Descending);
    assert_eq!(order.fields[1].null_order, NullOrder::Last);
    assert_eq!(order.fields[1].transform, Transform::Bucket(4));

    let value = serde_json::to_value(&order).unwrap();
    assert_eq!(value["order-id"], 1);
    assert_eq!(value["fields"][0]["direction"], "asc");
    assert_eq!(value["fields"][0]["null-order"], "nulls-first");
    assert_eq!(value["fields"][1]["null-order"], "nulls-last");
}

#[test]
fn g10_sort_order__unsorted_order_is_id_zero() {
    // "Sorting": order id 0 is reserved for the unsorted order.
    let order: SortOrder = serde_json::from_str(r#"{"order-id":0,"fields":[]}"#).unwrap();
    assert!(order.is_unsorted());
    let sorted: SortOrder = serde_json::from_str(
        r#"{"order-id":1,"fields":[{"transform":"identity","source-id":1,"direction":"asc","null-order":"nulls-first"}]}"#,
    )
    .unwrap();
    assert!(!sorted.is_unsorted());
}

// ---------------------------------------------------------------------------
// g11_metadata_v1 — spec.md "Table Metadata" + Appendix E "Version 2"
// ---------------------------------------------------------------------------

const V1_METADATA: &str = r#"{
  "format-version": 1,
  "table-uuid": "d20125c8-7284-442c-9aea-15fee620737c",
  "location": "s3://bucket/test/location",
  "last-updated-ms": 1602638573874,
  "last-column-id": 3,
  "schema": {
    "type": "struct",
    "schema-id": 0,
    "fields": [
      {"id": 1, "name": "x", "required": true, "type": "long"},
      {"id": 2, "name": "y", "required": true, "type": "long", "doc": "comment"},
      {"id": 3, "name": "z", "required": true, "type": "long"}
    ]
  },
  "partition-spec": [{"name": "x", "transform": "identity", "source-id": 1, "field-id": 1000}],
  "properties": {},
  "current-snapshot-id": -1,
  "snapshots": []
}"#;

#[test]
fn g11_metadata_v1__parses_single_schema_and_spec() {
    let md: TableMetadata = serde_json::from_str(V1_METADATA).unwrap();
    assert_eq!(md.format_version(), FormatVersion::V1);
    assert_eq!(md.location(), "s3://bucket/test/location");
    assert_eq!(
        md.uuid().to_string(),
        "d20125c8-7284-442c-9aea-15fee620737c"
    );
    // "Table Metadata Fields": a v1 table's single `schema` becomes the
    // current schema of the `schemas` list.
    assert_eq!(md.current_schema().schema_id(), 0);
    assert_eq!(md.current_schema().as_struct().fields().len(), 3);
    // The v1 `partition-spec` becomes the default spec.
    let spec = md.default_partition_spec().expect("default partition spec");
    assert_eq!(spec.fields.len(), 1);
    assert_eq!(spec.fields[0].field_id, 1000);
    assert_eq!(spec.fields[0].transform, Transform::Identity);
}

#[test]
fn g11_metadata_v1__no_current_snapshot() {
    // "Table Metadata Fields": current-snapshot-id -1 means no snapshot.
    let md: TableMetadata = serde_json::from_str(V1_METADATA).unwrap();
    assert!(md.current_snapshot().is_none());
    assert_eq!(md.snapshots().count(), 0);
    assert_eq!(md.history().len(), 0);
    assert_eq!(md.last_sequence_number(), 0);
}

#[test]
fn g11_metadata_v1__rejects_unknown_format_version() {
    let bad = V1_METADATA.replace("\"format-version\": 1", "\"format-version\": 99");
    assert!(
        serde_json::from_str::<TableMetadata>(&bad).is_err(),
        "an unsupported format-version must be rejected"
    );
}

// ---------------------------------------------------------------------------
// g12_metadata_v2 — spec.md "Table Metadata", "Snapshot References"
// ---------------------------------------------------------------------------

const V2_METADATA: &str = r#"{
  "format-version": 2,
  "table-uuid": "9c12d441-03fe-4693-9a96-a0705ddf69c1",
  "location": "s3://bucket/test/location",
  "last-sequence-number": 34,
  "last-updated-ms": 1602638573590,
  "last-column-id": 3,
  "current-schema-id": 1,
  "schemas": [
    {"type": "struct", "schema-id": 0, "fields": [{"id": 1, "name": "x", "required": true, "type": "long"}]},
    {
      "type": "struct",
      "schema-id": 1,
      "identifier-field-ids": [1, 2],
      "fields": [
        {"id": 1, "name": "x", "required": true, "type": "long"},
        {"id": 2, "name": "y", "required": true, "type": "long", "doc": "comment"},
        {"id": 3, "name": "z", "required": true, "type": "long"}
      ]
    }
  ],
  "default-spec-id": 0,
  "partition-specs": [{"spec-id": 0, "fields": [{"name": "x", "transform": "identity", "source-id": 1, "field-id": 1000}]}],
  "last-partition-id": 1000,
  "default-sort-order-id": 3,
  "sort-orders": [
    {
      "order-id": 3,
      "fields": [
        {"transform": "identity", "source-id": 2, "direction": "asc", "null-order": "nulls-first"},
        {"transform": "bucket[4]", "source-id": 3, "direction": "desc", "null-order": "nulls-last"}
      ]
    }
  ],
  "properties": {"read.split.target.size": "134217728"},
  "current-snapshot-id": 3055729675574597004,
  "snapshots": [
    {
      "snapshot-id": 3051729675574597004,
      "timestamp-ms": 1515100955770,
      "sequence-number": 0,
      "summary": {"operation": "append"},
      "manifest-list": "s3://a/b/1.avro"
    },
    {
      "snapshot-id": 3055729675574597004,
      "parent-snapshot-id": 3051729675574597004,
      "timestamp-ms": 1555100955770,
      "sequence-number": 1,
      "summary": {"operation": "append"},
      "manifest-list": "s3://a/b/2.avro",
      "schema-id": 1
    }
  ],
  "snapshot-log": [
    {"snapshot-id": 3051729675574597004, "timestamp-ms": 1515100955770},
    {"snapshot-id": 3055729675574597004, "timestamp-ms": 1555100955770}
  ],
  "metadata-log": [{"metadata-file": "s3://bucket/.../v1.json", "timestamp-ms": 1515100}],
  "refs": {"main": {"snapshot-id": 3055729675574597004, "type": "branch"}}
}"#;

#[test]
fn g12_metadata_v2__core_fields() {
    let md: TableMetadata = serde_json::from_str(V2_METADATA).unwrap();
    assert_eq!(md.format_version(), FormatVersion::V2);
    assert_eq!(md.last_sequence_number(), 34);
    assert_eq!(md.current_schema().schema_id(), 1);
    assert_eq!(md.schemas_iter().count(), 2);
    assert!(md.schema_by_id(0).is_some());
    assert_eq!(md.partition_specs_iter().count(), 1);
    assert_eq!(md.default_partition_spec().unwrap().spec_id, 0);
    assert_eq!(md.default_sort_order().unwrap().order_id, 3);
    assert_eq!(
        md.properties().get("read.split.target.size").map(String::as_str),
        Some("134217728")
    );
}

#[test]
fn g12_metadata_v2__lookups_by_id() {
    let md: TableMetadata = serde_json::from_str(V2_METADATA).unwrap();
    // The document's own `last-updated-ms` survives the parse (PROFILE.md P6).
    assert_eq!(md.last_updated_ms().timestamp_millis(), 1602638573590);
    assert_eq!(md.partition_spec_by_id(0).unwrap().spec_id, 0);
    assert!(md.partition_spec_by_id(9).is_none());
    assert_eq!(md.sort_orders_iter().count(), 1);
    assert_eq!(md.sort_order_by_id(3).unwrap().order_id, 3);
    assert!(md.sort_order_by_id(9).is_none());
}

#[test]
fn g12_metadata_v2__snapshots_and_log() {
    let md: TableMetadata = serde_json::from_str(V2_METADATA).unwrap();
    assert_eq!(md.snapshots().count(), 2);
    let current = md.current_snapshot().expect("current snapshot");
    assert_eq!(current.snapshot_id(), 3055729675574597004);
    assert_eq!(current.parent_snapshot_id(), Some(3051729675574597004));
    assert_eq!(current.sequence_number(), 1);
    assert_eq!(current.schema_id(), Some(1));
    assert!(md.snapshot_by_id(3051729675574597004).is_some());
    assert_eq!(md.history().len(), 2);
    assert_eq!(md.history()[0].snapshot_id, 3051729675574597004);
    // Appendix C `snapshot-log`: each entry carries the snapshot id and the
    // timestamp the snapshot became current (TEST_INTERFACE.md 4.6).
    assert_eq!(md.history()[0].timestamp_ms, 1515100955770);
    assert_eq!(md.history()[1].timestamp_ms, 1555100955770);
}

#[test]
fn g12_metadata_v2__roundtrip_preserves_documented_fields() {
    let md: TableMetadata = serde_json::from_str(V2_METADATA).unwrap();
    let value = serde_json::to_value(&md).unwrap();
    assert_eq!(value["format-version"], 2);
    assert_eq!(value["last-sequence-number"], 34);
    assert_eq!(value["current-schema-id"], 1);
    assert_eq!(value["default-spec-id"], 0);
    assert_eq!(value["default-sort-order-id"], 3);
    assert_eq!(value["current-snapshot-id"], 3055729675574597004i64);
    assert_eq!(value["refs"]["main"]["type"], "branch");
    let back: TableMetadata = serde_json::from_value(value).unwrap();
    assert_eq!(back, md);
}

// ---------------------------------------------------------------------------
// g13_snapshot — spec.md "Snapshots"
// ---------------------------------------------------------------------------

#[test]
fn g13_snapshot__summary_operation() {
    let md: TableMetadata = serde_json::from_str(V2_METADATA).unwrap();
    let snap = md.current_snapshot().unwrap();
    assert_eq!(snap.summary().operation, Operation::Append);
    assert_eq!(
        snap.manifest_list_file_path().unwrap(),
        "s3://a/b/2.avro"
    );
    assert_eq!(snap.timestamp().timestamp_millis(), 1555100955770);
}

#[test]
fn g13_snapshot__resolves_its_schema() {
    let md: TableMetadata = serde_json::from_str(V2_METADATA).unwrap();
    let snap = md.current_snapshot().unwrap();
    let schema = snap.schema(&md).unwrap();
    assert_eq!(schema.schema_id(), 1);
    assert_eq!(schema.as_struct().fields().len(), 3);
}

#[test]
fn g13_snapshot__documented_operations_round_trip() {
    // "Snapshots": the summary's `operation` is one of these four, lower case.
    for (text, op) in [
        ("append", Operation::Append),
        ("replace", Operation::Replace),
        ("overwrite", Operation::Overwrite),
        ("delete", Operation::Delete),
    ] {
        let parsed: Operation = serde_json::from_str(&format!("\"{text}\""))
            .unwrap_or_else(|e| panic!("operation {text}: {e}"));
        assert_eq!(parsed, op);
        assert_eq!(serde_json::to_string(&op).unwrap(), format!("\"{text}\""));
    }
    // The whole document parses with each of them in the snapshot summary.
    for (text, op) in [
        ("replace", Operation::Replace),
        ("overwrite", Operation::Overwrite),
        ("delete", Operation::Delete),
    ] {
        let doc = V2_METADATA.replace(
            "\"operation\": \"append\"",
            &format!("\"operation\": \"{text}\""),
        );
        let md: TableMetadata = serde_json::from_str(&doc)
            .unwrap_or_else(|e| panic!("metadata with operation {text}: {e}"));
        assert_eq!(md.current_snapshot().unwrap().summary().operation, op);
    }
}

#[test]
fn g13_snapshot__unknown_operation_is_rejected() {
    let bad = V2_METADATA.replace("\"operation\": \"append\"", "\"operation\": \"frobnicate\"");
    assert!(
        serde_json::from_str::<TableMetadata>(&bad).is_err(),
        "an undefined snapshot operation must be rejected"
    );
}

// ---------------------------------------------------------------------------
// g14_manifest_list — spec.md "Manifest Lists" + Appendix A
// ---------------------------------------------------------------------------

fn temp_io() -> (tempfile::TempDir, iceberg::io::FileIO) {
    let dir = tempfile::TempDir::new().unwrap();
    let io = iceberg::io::FileIOBuilder::new_fs_io().build().unwrap();
    (dir, io)
}

fn sample_manifest_list_entry(path: &str) -> ManifestListEntry {
    ManifestListEntry {
        manifest_path: path.to_string(),
        manifest_length: 1024,
        partition_spec_id: 0,
        content: ManifestContentType::Data,
        sequence_number: 1,
        min_sequence_number: 1,
        added_snapshot_id: 100,
        added_data_files_count: Some(1),
        existing_data_files_count: Some(0),
        deleted_data_files_count: Some(0),
        added_rows_count: Some(10),
        existing_rows_count: Some(0),
        deleted_rows_count: Some(0),
        partitions: vec![],
        key_metadata: vec![],
    }
}

const MANIFEST_LIST_V2_COLUMNS: [&str; 15] = [
    "manifest_path",
    "manifest_length",
    "partition_spec_id",
    "content",
    "sequence_number",
    "min_sequence_number",
    "added_snapshot_id",
    "added_data_files_count",
    "existing_data_files_count",
    "deleted_data_files_count",
    "added_rows_count",
    "existing_rows_count",
    "deleted_rows_count",
    "partitions",
    "key_metadata",
];

#[tokio::test(flavor = "multi_thread")]
async fn g14_manifest_list__v2_write_then_read() {
    let (dir, io) = temp_io();
    let path = format!("{}/manifest-list-v2.avro", dir.path().to_str().unwrap());
    let out = io.new_output(&path).unwrap();
    let mut writer = ManifestListWriter::v2(out, 100, 0, 1);
    writer
        .add_manifest_entries(vec![sample_manifest_list_entry("/tmp/m0.avro")].into_iter())
        .unwrap();
    writer.close().await.unwrap();

    let bytes = std::fs::read(&path).unwrap();
    // Appendix A: manifest lists are stored in Avro object container files.
    assert_eq!(&bytes[..4], b"Obj\x01", "manifest list must be an Avro file");

    let list =
        ManifestList::parse_with_version(&bytes, FormatVersion::V2, |_id| Ok(None)).unwrap();
    assert_eq!(list.entries().len(), 1);
    let e = &list.entries()[0];
    assert_eq!(e.manifest_path, "/tmp/m0.avro");
    assert_eq!(e.manifest_length, 1024);
    assert_eq!(e.added_snapshot_id, 100);
    assert_eq!(e.sequence_number, 1);
    assert_eq!(e.content, ManifestContentType::Data);
}

#[tokio::test(flavor = "multi_thread")]
async fn g14_manifest_list__avro_schema_and_metadata() {
    // "Manifest Lists": the manifest-list record is named `manifest_file` and
    // carries the columns named in that section. The file metadata records the
    // snapshot the list belongs to and the table format version.
    let (dir, io) = temp_io();
    let path = format!("{}/manifest-list-ids.avro", dir.path().to_str().unwrap());
    let out = io.new_output(&path).unwrap();
    let mut writer = ManifestListWriter::v2(out, 100, 0, 1);
    writer
        .add_manifest_entries(vec![sample_manifest_list_entry("/tmp/m1.avro")].into_iter())
        .unwrap();
    writer.close().await.unwrap();

    let bytes = std::fs::read(&path).unwrap();
    let schema = avro_writer_schema(&bytes);
    assert_eq!(
        schema["name"], "manifest_file",
        "the manifest-list writer schema must be a record named `manifest_file`"
    );
    let columns = avro_record_fields(&schema);
    for needle in MANIFEST_LIST_V2_COLUMNS {
        assert!(
            columns.iter().any(|c| c == needle),
            "the manifest-list Avro schema must name the column {needle}; got {columns:?}"
        );
    }

    let meta = avro_file_metadata(&bytes);
    assert_eq!(avro_metadata_string(&meta, "snapshot-id"), "100");
    assert_eq!(avro_metadata_string(&meta, "parent-snapshot-id"), "0");
    assert_eq!(avro_metadata_string(&meta, "sequence-number"), "1");
    assert_eq!(avro_metadata_string(&meta, "format-version"), "2");
}

#[tokio::test(flavor = "multi_thread")]
async fn g14_manifest_list__v1_write_then_read() {
    // "Manifest Lists": `content`, `sequence_number` and `min_sequence_number`
    // are v2-only columns; a v1 manifest list has neither them nor a
    // `sequence-number` in its file metadata.
    let (dir, io) = temp_io();
    let path = format!("{}/manifest-list-v1.avro", dir.path().to_str().unwrap());
    let out = io.new_output(&path).unwrap();
    let mut writer = ManifestListWriter::v1(out, 100, 0);
    writer
        .add_manifest_entries(vec![sample_manifest_list_entry("/tmp/m-v1.avro")].into_iter())
        .unwrap();
    writer.close().await.unwrap();

    let bytes = std::fs::read(&path).unwrap();
    assert_eq!(&bytes[..4], b"Obj\x01");
    let schema = avro_writer_schema(&bytes);
    assert_eq!(schema["name"], "manifest_file");
    let columns = avro_record_fields(&schema);
    for needle in [
        "manifest_path",
        "manifest_length",
        "partition_spec_id",
        "added_snapshot_id",
        "added_data_files_count",
        "existing_data_files_count",
        "deleted_data_files_count",
        "added_rows_count",
        "existing_rows_count",
        "deleted_rows_count",
        "partitions",
        "key_metadata",
    ] {
        assert!(
            columns.iter().any(|c| c == needle),
            "the v1 manifest-list schema must name {needle}; got {columns:?}"
        );
    }
    for absent in ["content", "sequence_number", "min_sequence_number"] {
        assert!(
            !columns.iter().any(|c| c == absent),
            "{absent} is a v2-only manifest-list column, but the v1 schema names it"
        );
    }
    let meta = avro_file_metadata(&bytes);
    assert_eq!(avro_metadata_string(&meta, "snapshot-id"), "100");
    assert_eq!(avro_metadata_string(&meta, "parent-snapshot-id"), "0");
    assert_eq!(avro_metadata_string(&meta, "format-version"), "1");

    let list =
        ManifestList::parse_with_version(&bytes, FormatVersion::V1, |_id| Ok(None)).unwrap();
    assert_eq!(list.entries().len(), 1);
    let e = &list.entries()[0];
    assert_eq!(e.manifest_path, "/tmp/m-v1.avro");
    assert_eq!(e.manifest_length, 1024);
    assert_eq!(e.added_snapshot_id, 100);
    assert_eq!(e.added_data_files_count, Some(1));
}

#[tokio::test(flavor = "multi_thread")]
async fn g14_manifest_list__partition_summaries_are_typed() {
    // "Manifest Lists": `field_summary` bounds are Appendix D binary values,
    // so reading them needs the manifest's partition type — which is what
    // `partition_type_provider` supplies, keyed by `partition_spec_id`.
    let (dir, io) = temp_io();
    let path = format!("{}/manifest-list-parts.avro", dir.path().to_str().unwrap());
    let out = io.new_output(&path).unwrap();
    let mut writer = ManifestListWriter::v2(out, 100, 0, 1);
    let mut entry = sample_manifest_list_entry("/tmp/m-parts.avro");
    entry.partition_spec_id = 7;
    entry.partitions = vec![FieldSummary {
        contains_null: false,
        contains_nan: Some(false),
        lower_bound: Some(Literal::int(1)),
        upper_bound: Some(Literal::int(9)),
    }];
    writer
        .add_manifest_entries(vec![entry].into_iter())
        .unwrap();
    writer.close().await.unwrap();

    let bytes = std::fs::read(&path).unwrap();
    let partition_type = StructType::new(vec![NestedField::optional(
        1000,
        "id_bucket",
        Type::Primitive(PrimitiveType::Int),
    )
    .into()]);
    let seen = std::cell::RefCell::new(Vec::<i32>::new());
    let list = ManifestList::parse_with_version(&bytes, FormatVersion::V2, |spec_id| {
        seen.borrow_mut().push(spec_id);
        Ok(Some(partition_type.clone()))
    })
    .unwrap();
    assert_eq!(
        seen.into_inner(),
        vec![7],
        "the provider must be asked for the entry's own partition spec id"
    );
    let summaries = &list.entries()[0].partitions;
    assert_eq!(summaries.len(), 1);
    assert!(!summaries[0].contains_null);
    assert_eq!(summaries[0].contains_nan, Some(false));
    assert_eq!(summaries[0].lower_bound, Some(Literal::int(1)));
    assert_eq!(summaries[0].upper_bound, Some(Literal::int(9)));

    // Without a partition type there is no way to decode the bounds, so an
    // entry that carries partition summaries must fail to parse.
    assert!(
        ManifestList::parse_with_version(&bytes, FormatVersion::V2, |_id| Ok(None)).is_err(),
        "partition summaries without a partition type must be rejected"
    );
}

// ---------------------------------------------------------------------------
// g15_manifest — spec.md "Manifests" + Appendix A
// ---------------------------------------------------------------------------

fn sample_data_file(path: &str) -> DataFile {
    DataFile::builder()
        .content(DataContentType::Data)
        .file_path(path.to_string())
        .file_format(DataFileFormat::Parquet)
        .partition(Struct::empty())
        .record_count(10)
        .file_size_in_bytes(2048)
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
}

fn sample_manifest(paths: &[&str]) -> Manifest {
    let schema = Arc::new(sample_schema());
    let metadata = ManifestMetadata::builder()
        .schema(schema.as_ref().clone())
        .schema_id(schema.schema_id())
        .partition_spec(PartitionSpec {
            spec_id: 0,
            fields: vec![],
        })
        .content(ManifestContentType::Data)
        .format_version(FormatVersion::V2)
        .build();
    let entries = paths
        .iter()
        .map(|p| {
            ManifestEntry::builder()
                .status(ManifestStatus::Added)
                .data_file(sample_data_file(p))
                .build()
        })
        .collect();
    Manifest::new(metadata, entries)
}

#[tokio::test(flavor = "multi_thread")]
async fn g15_manifest__write_then_parse() {
    let (dir, io) = temp_io();
    let manifest = sample_manifest(&["/tmp/data-0.parquet", "/tmp/data-1.parquet"]);

    let path = format!("{}/m0.avro", dir.path().to_str().unwrap());
    let out = io.new_output(&path).unwrap();
    let writer = ManifestWriter::new(out, 100, vec![]);
    let list_entry = writer.write(manifest).await.unwrap();
    // "Manifest Lists": the counts report the entries with each status.
    assert_eq!(list_entry.added_data_files_count, Some(2));
    assert_eq!(list_entry.existing_data_files_count, Some(0));
    assert_eq!(list_entry.deleted_data_files_count, Some(0));
    assert_eq!(list_entry.added_snapshot_id, 100);

    let bytes = std::fs::read(&path).unwrap();
    assert_eq!(&bytes[..4], b"Obj\x01", "manifest must be an Avro file");
    let parsed = Manifest::parse_avro(&bytes).unwrap();
    assert_eq!(parsed.entries().len(), 2);
    let e = &parsed.entries()[0];
    assert_eq!(e.file_path(), "/tmp/data-0.parquet");
    assert_eq!(e.content_type(), DataContentType::Data);
    assert_eq!(e.file_size_in_bytes(), 2048);
    assert!(e.is_alive());
    // "Sequence Number Inheritance": a newly added file's data sequence number
    // is written as null and inherited from the manifest list, which
    // `parse_avro` does not have.
    assert_eq!(e.sequence_number(), None);
    assert_eq!(parsed.entries()[1].file_path(), "/tmp/data-1.parquet");
}

#[tokio::test(flavor = "multi_thread")]
async fn g15_manifest__avro_writer_schema_shape() {
    let (dir, io) = temp_io();
    let schema = Arc::new(sample_schema());
    let manifest = sample_manifest(&["/tmp/data-1.parquet"]);
    let path = format!("{}/m1.avro", dir.path().to_str().unwrap());
    let out = io.new_output(&path).unwrap();
    ManifestWriter::new(out, 100, vec![])
        .write(manifest)
        .await
        .unwrap();

    let bytes = std::fs::read(&path).unwrap();
    // "Manifests": the manifest's schema is a struct called `manifest_entry`.
    let writer_schema = avro_writer_schema(&bytes);
    assert_eq!(writer_schema["name"], "manifest_entry");
    let columns = avro_record_fields(&writer_schema);
    for needle in ["status", "snapshot_id", "data_file"] {
        assert!(
            columns.iter().any(|c| c == needle),
            "the manifest writer schema must name {needle}; got {columns:?}"
        );
    }
    // "Manifest Entry Fields": `data_file` is a nested struct with these columns.
    let data_file = avro_field_type(&writer_schema, "data_file");
    let df_columns = avro_record_fields(data_file);
    for needle in [
        "content",
        "file_path",
        "file_format",
        "partition",
        "record_count",
        "file_size_in_bytes",
    ] {
        assert!(
            df_columns.iter().any(|c| c == needle),
            "the data_file record must name {needle}; got {df_columns:?}"
        );
    }

    // "Manifests": the partition spec and other metadata are stored as
    // properties in the Avro file's key-value metadata.
    let meta = avro_file_metadata(&bytes);
    assert_eq!(avro_metadata_string(&meta, "schema-id"), "1");
    assert_eq!(avro_metadata_string(&meta, "partition-spec-id"), "0");
    assert_eq!(avro_metadata_string(&meta, "format-version"), "2");
    assert_eq!(avro_metadata_string(&meta, "content"), "data");
    let recorded: Schema = serde_json::from_str(&avro_metadata_string(&meta, "schema"))
        .expect("the `schema` metadata must be the JSON of the table schema");
    assert_eq!(recorded.as_struct(), schema.as_struct());
    let _: serde_json::Value = serde_json::from_str(&avro_metadata_string(&meta, "partition-spec"))
        .expect("the `partition-spec` metadata must be JSON");
}

#[test]
fn g15_manifest__enum_textual_forms() {
    // "Manifests": manifest content is "data" or "deletes".
    assert_eq!(
        "data".parse::<ManifestContentType>().unwrap(),
        ManifestContentType::Data
    );
    assert_eq!(
        "deletes".parse::<ManifestContentType>().unwrap(),
        ManifestContentType::Deletes
    );
    assert!("neither".parse::<ManifestContentType>().is_err());
    assert_eq!(ManifestContentType::Data.to_string(), "data");
    assert_eq!(ManifestContentType::Deletes.to_string(), "deletes");

    // "Manifest Entry Fields": `file_format` is the string file format name.
    for (text, value) in [
        ("avro", DataFileFormat::Avro),
        ("orc", DataFileFormat::Orc),
        ("parquet", DataFileFormat::Parquet),
    ] {
        assert_eq!(text.parse::<DataFileFormat>().unwrap(), value);
        assert_eq!(value.to_string(), text);
    }
    assert!("csv".parse::<DataFileFormat>().is_err());
}

// ---------------------------------------------------------------------------
// g16_file_io — TEST_INTERFACE.md 4.10 + PROFILE.md P3
// ---------------------------------------------------------------------------

/// Write a small manifest list through `FileIO` so the io cases operate on a
/// file this crate itself produced.
async fn write_through(io: &iceberg::io::FileIO, location: &str) {
    let out = io.new_output(location).unwrap();
    assert_eq!(out.location(), location);
    let mut writer = ManifestListWriter::v2(out, 100, 0, 1);
    writer
        .add_manifest_entries(vec![sample_manifest_list_entry("/tmp/io.avro")].into_iter())
        .unwrap();
    writer.close().await.unwrap();
}

#[tokio::test(flavor = "multi_thread")]
async fn g16_file_io__builder_and_locations() {
    // Both documented ways of naming the local filesystem produce a usable
    // `FileIO`, and properties irrelevant to it are accepted.
    let dir = tempfile::TempDir::new().unwrap();
    let by_scheme = iceberg::io::FileIOBuilder::new("file")
        .with_prop("unused-property", "value")
        .build()
        .unwrap();
    let by_helper = iceberg::io::FileIOBuilder::new_fs_io().build().unwrap();
    // 4.10: the empty scheme string names the local filesystem too.
    let by_empty_scheme = iceberg::io::FileIOBuilder::new("").build().unwrap();

    let path = format!("{}/located.avro", dir.path().to_str().unwrap());
    write_through(&by_scheme, &path).await;

    // `FileIO: Clone`, and a clone addresses the same files.
    let cloned = by_scheme.clone();
    assert!(cloned.is_exist(&path).await.unwrap());
    assert!(by_helper.is_exist(&path).await.unwrap());
    assert!(
        by_empty_scheme.is_exist(&path).await.unwrap(),
        "all three documented ways of naming the local filesystem must address the same files"
    );

    // Locations are echoed back exactly as given.
    assert_eq!(by_scheme.new_input(&path).unwrap().location(), path);
    assert_eq!(by_scheme.new_output(&path).unwrap().location(), path);
}

#[tokio::test(flavor = "multi_thread")]
async fn g16_file_io__exists_and_delete() {
    let dir = tempfile::TempDir::new().unwrap();
    let io = iceberg::io::FileIOBuilder::new_fs_io().build().unwrap();
    let path = format!("{}/deleted.avro", dir.path().to_str().unwrap());
    let absent = format!("{}/never-written.avro", dir.path().to_str().unwrap());

    assert!(!io.is_exist(&absent).await.unwrap());
    write_through(&io, &path).await;
    assert!(io.is_exist(&path).await.unwrap());
    assert!(!std::fs::read(&path).unwrap().is_empty());

    io.delete(&path).await.unwrap();
    assert!(
        !io.is_exist(&path).await.unwrap(),
        "a deleted file must no longer exist"
    );
}

#[tokio::test(flavor = "multi_thread")]
async fn g16_file_io__file_uri_locations() {
    // PROFILE.md P3: the local filesystem must accept `file://` URIs as well
    // as plain absolute paths, and both must name the same file.
    let dir = tempfile::TempDir::new().unwrap();
    let io = iceberg::io::FileIOBuilder::new_fs_io().build().unwrap();
    let base = dir.path().to_str().unwrap().to_string();
    let plain = format!("{base}/via-uri.avro");
    let uri = format!("file://{base}/via-uri.avro");

    write_through(&io, &uri).await;
    assert!(io.is_exist(&uri).await.unwrap());
    assert!(
        io.is_exist(&plain).await.unwrap(),
        "a `file://` URI and the plain path must name the same file"
    );
    assert_eq!(io.new_input(&uri).unwrap().location(), uri);
    assert_eq!(&std::fs::read(&plain).unwrap()[..4], b"Obj\x01");

    io.delete(&uri).await.unwrap();
    assert!(!io.is_exist(&plain).await.unwrap());
}

// ---------------------------------------------------------------------------
// Additional cases for milestone 01, from TEST_INTERFACE.md sections 4.8-4.9.
// ---------------------------------------------------------------------------

/// TEST_INTERFACE.md 4.8: the manifest list's Avro key-value metadata records
/// *"`snapshot-id` and `parent-snapshot-id` from both constructors,
/// `sequence-number` from `v2` only"*. A v1 list has no sequence number to
/// record, so the key must be absent rather than present with a placeholder.
#[tokio::test(flavor = "multi_thread")]
async fn g14_manifest_list__v1_records_no_sequence_number() {
    let (dir, io) = temp_io();
    let path = format!("{}/manifest-list-v1-meta.avro", dir.path().to_str().unwrap());
    let out = io.new_output(&path).unwrap();
    let mut writer = ManifestListWriter::v1(out, 100, 0);
    writer
        .add_manifest_entries(vec![sample_manifest_list_entry("/tmp/m-v1-meta.avro")].into_iter())
        .unwrap();
    writer.close().await.unwrap();

    let meta = avro_file_metadata(&std::fs::read(&path).unwrap());
    assert_eq!(avro_metadata_string(&meta, "format-version"), "1");
    assert_eq!(avro_metadata_string(&meta, "snapshot-id"), "100");
    assert!(
        !meta.contains_key("sequence-number"),
        "`sequence-number` is recorded by the v2 constructor only; got {:?}",
        meta.keys().collect::<Vec<_>>()
    );
}

/// TEST_INTERFACE.md 4.9: *"`decimal(v)` takes the unscaled value … The scale
/// belongs to the `Type`, not to the literal, so `decimal(1420)` is `14.20`
/// against `decimal(9,2)` and `142.0` against `decimal(9,1)`, and two literals
/// built from the same unscaled value compare equal whatever scale the type
/// they are serialised against carries."*
#[test]
fn g04_values_json__decimal_scale_comes_from_the_type() {
    let scale_two = Type::Primitive(PrimitiveType::Decimal {
        precision: 9,
        scale: 2,
    });
    let scale_one = Type::Primitive(PrimitiveType::Decimal {
        precision: 9,
        scale: 1,
    });
    let unscaled = Literal::decimal(1420);
    assert_eq!(
        unscaled.clone().try_into_json(&scale_two).unwrap(),
        serde_json::json!("14.20")
    );
    assert_eq!(
        unscaled.clone().try_into_json(&scale_one).unwrap(),
        serde_json::json!("142.0")
    );

    // The two documents denote the same unscaled value against their own types,
    // so the literals they parse into are equal.
    let from_scale_two = Literal::try_from_json(serde_json::json!("14.20"), &scale_two)
        .unwrap()
        .unwrap();
    let from_scale_one = Literal::try_from_json(serde_json::json!("142.0"), &scale_one)
        .unwrap()
        .unwrap();
    assert_eq!(from_scale_two, unscaled);
    assert_eq!(from_scale_one, unscaled);
    assert_eq!(from_scale_one, from_scale_two);
}
