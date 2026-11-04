// Verifier-owned conformance driver for milestone 03 (metadata update algebra).
//
// Every assertion here traces to the pinned Apache Iceberg table specification
// shipped at /app/iceberg-spec/spec.md, or to /app/TEST_INTERFACE.md, which
// states the public Rust surface the milestone must expose.
//
// Group = the `gNN_<name>__` prefix of the test function name. The runner
// invokes one `cargo test` per group and asserts the executed count.
//
// This file scores ONLY milestone-03 behaviour: the `TableMetadataBuilder`
// update algebra, `TableUpdate` / `TableRequirement`, nanosecond timestamp
// types, and the bound / unbound partition-spec split. Milestone 01-02
// behaviour is exercised by tests/gate.rs, not scored here.

use std::collections::HashMap;

use iceberg::spec::*;
use iceberg::{TableCreation, TableRequirement, TableUpdate};

// ---------------------------------------------------------------------------
// Shared fixtures. Every helper returns a fresh value; no test shares state.
// ---------------------------------------------------------------------------

/// A three-column schema whose field ids deliberately do NOT start at 1, so
/// that field-id reassignment is observable.
fn source_schema() -> Schema {
    Schema::builder()
        .with_schema_id(7)
        .with_fields(vec![
            NestedField::required(11, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(12, "ts", Type::Primitive(PrimitiveType::Timestamptz)).into(),
            NestedField::optional(13, "name", Type::Primitive(PrimitiveType::String)).into(),
        ])
        .build()
        .unwrap()
}

/// The schema as it looks after `TableMetadataBuilder` has reassigned field ids
/// from 1: `id` = 1, `ts` = 2, `name` = 3.
fn assigned_schema(schema_id: i32) -> Schema {
    Schema::builder()
        .with_schema_id(schema_id)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "ts", Type::Primitive(PrimitiveType::Timestamptz)).into(),
            NestedField::optional(3, "name", Type::Primitive(PrimitiveType::String)).into(),
        ])
        .build()
        .unwrap()
}

fn table_creation() -> TableCreation {
    TableCreation::builder()
        .name("orders".to_string())
        .location("file:///warehouse/db/orders".to_string())
        .schema(source_schema())
        .build()
}

/// Freshly created, unpartitioned, format version 2 table metadata.
fn fresh_metadata() -> TableMetadata {
    TableMetadataBuilder::from_table_creation(table_creation())
        .unwrap()
        .build()
        .unwrap()
        .metadata
}

fn fresh_metadata_v1() -> TableMetadata {
    TableMetadataBuilder::new(
        source_schema(),
        UnboundPartitionSpec::builder().build(),
        SortOrder::unsorted_order(),
        "file:///warehouse/db/orders".to_string(),
        FormatVersion::V1,
        HashMap::new(),
    )
    .unwrap()
    .build()
    .unwrap()
    .metadata
}

/// The `action` tag of every recorded `TableUpdate`, in order.
fn change_actions(changes: &[TableUpdate]) -> Vec<String> {
    changes
        .iter()
        .map(|c| {
            serde_json::to_value(c).unwrap()["action"]
                .as_str()
                .unwrap()
                .to_string()
        })
        .collect()
}

/// Read a documented metadata field straight out of the serialized document,
/// so that fields without a Rust accessor are still observable.
fn field(metadata: &TableMetadata, name: &str) -> serde_json::Value {
    serde_json::to_value(metadata).unwrap()[name].clone()
}

fn append_snapshot(id: i64, parent: Option<i64>, sequence_number: i64, timestamp_ms: i64) -> Snapshot {
    Snapshot::builder()
        .with_snapshot_id(id)
        .with_parent_snapshot_id(parent)
        .with_sequence_number(sequence_number)
        .with_timestamp_ms(timestamp_ms)
        .with_manifest_list(format!("file:///warehouse/db/orders/metadata/snap-{id}.avro"))
        .with_schema_id(0)
        .with_summary(Summary {
            operation: Operation::Append,
            additional_properties: HashMap::new(),
        })
        .build()
}

// ---------------------------------------------------------------------------
// g01_create — spec.md "Table Metadata" + "Table Metadata Fields"
// ---------------------------------------------------------------------------

#[test]
fn g01_create__fresh_metadata_defaults() {
    let md = fresh_metadata();
    // "Table Metadata Fields": a new table gets a uuid, a location, a current
    // schema, a default spec, a default sort order and no snapshots.
    assert_eq!(md.format_version(), FormatVersion::V2);
    assert_eq!(md.location(), "file:///warehouse/db/orders");
    assert_ne!(md.uuid(), uuid::Uuid::nil());
    assert_eq!(md.current_schema_id(), 0);
    assert_eq!(md.schemas_iter().count(), 1);
    assert_eq!(md.default_partition_spec_id(), 0);
    assert!(md.default_partition_spec().is_unpartitioned());
    assert_eq!(md.default_sort_order_id(), 0);
    assert!(md.default_sort_order().is_unsorted());
    assert_eq!(md.current_snapshot_id(), None);
    assert_eq!(md.snapshots().count(), 0);
    assert_eq!(md.history().len(), 0);
    assert_eq!(md.last_sequence_number(), 0);
    // "Partition Evolution": partition field ids start at 1000, so the last
    // assigned partition id of an unpartitioned table is 999.
    assert_eq!(field(&md, "last-partition-id"), serde_json::json!(999));
}

#[test]
fn g01_create__reassigns_field_ids_from_one() {
    // "Schema Evolution": each table tracks `last-column-id`; a table created
    // from scratch numbers its columns from 1 and records the highest id.
    let md = fresh_metadata();
    let schema = md.current_schema();
    assert_eq!(schema.field_id_by_name("id"), Some(1));
    assert_eq!(schema.field_id_by_name("ts"), Some(2));
    assert_eq!(schema.field_id_by_name("name"), Some(3));
    assert_eq!(schema.highest_field_id(), 3);
    assert_eq!(field(&md, "last-column-id"), serde_json::json!(3));
}

#[test]
fn g01_create__location_is_required_and_normalized() {
    // A table cannot be created without a location.
    let no_location = TableCreation::builder()
        .name("orders".to_string())
        .schema(source_schema())
        .build();
    assert!(TableMetadataBuilder::from_table_creation(no_location).is_err());

    // Trailing slashes are not part of the table location.
    let trailing = TableCreation::builder()
        .name("orders".to_string())
        .location("file:///warehouse/db/orders/".to_string())
        .schema(source_schema())
        .build();
    let md = TableMetadataBuilder::from_table_creation(trailing)
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    assert_eq!(md.location(), "file:///warehouse/db/orders");
}

#[test]
fn g01_create__records_the_updates_that_built_it() {
    let result = TableMetadataBuilder::from_table_creation(table_creation())
        .unwrap()
        .set_properties(HashMap::from([(
            "read.split.target-size".to_string(),
            "134217728".to_string(),
        )]))
        .unwrap()
        .build()
        .unwrap();
    let actions = change_actions(&result.changes);
    for expected in [
        "set-location",
        "add-schema",
        "set-current-schema",
        "add-spec",
        "set-default-spec",
        "add-sort-order",
        "set-default-sort-order",
        "set-properties",
    ] {
        assert!(
            actions.contains(&expected.to_string()),
            "creation must record a {expected} update, got {actions:?}"
        );
    }
    // A schema must be added before it can be made current.
    let add = actions.iter().position(|a| a == "add-schema").unwrap();
    let set = actions.iter().position(|a| a == "set-current-schema").unwrap();
    assert!(add < set, "add-schema must precede set-current-schema");
}

// ---------------------------------------------------------------------------
// g02_identity — spec.md "Table Metadata Fields" (table-uuid, location,
// metadata-log)
// ---------------------------------------------------------------------------

#[test]
fn g02_identity__assign_uuid() {
    let md = fresh_metadata();
    let target = "f79c3e09-677c-4bbd-a479-3f349cb785e7"
        .parse::<uuid::Uuid>()
        .unwrap();
    let result = md.into_builder(None).assign_uuid(target).build().unwrap();
    assert_eq!(result.metadata.uuid(), target);
    assert_eq!(change_actions(&result.changes), vec!["assign-uuid"]);
}

#[test]
fn g02_identity__assigning_the_same_uuid_is_a_no_op() {
    let md = fresh_metadata();
    let current = md.uuid();
    let result = md.into_builder(None).assign_uuid(current).build().unwrap();
    assert_eq!(result.metadata.uuid(), current);
    assert!(
        result.changes.is_empty(),
        "an update that changes nothing must not be recorded: {:?}",
        change_actions(&result.changes)
    );
}

#[test]
fn g02_identity__set_location() {
    let md = fresh_metadata();
    let result = md
        .into_builder(None)
        .set_location("file:///warehouse/db/orders_v2/".to_string())
        .build()
        .unwrap();
    assert_eq!(result.metadata.location(), "file:///warehouse/db/orders_v2");
    assert_eq!(change_actions(&result.changes), vec!["set-location"]);
}

#[test]
fn g02_identity__metadata_log_records_the_previous_file() {
    // "Table Metadata Fields": `metadata-log` holds the previous metadata file
    // locations with the timestamp at which each stopped being current.
    let md = fresh_metadata();
    let previous_ms = md.last_updated_ms();
    let result = md
        .into_builder(Some("file:///warehouse/db/orders/metadata/v1.json".to_string()))
        .set_location("file:///warehouse/db/orders2".to_string())
        .build()
        .unwrap();
    assert_eq!(result.metadata.metadata_log().len(), 1);
    assert_eq!(
        result.metadata.metadata_log()[0].metadata_file,
        "file:///warehouse/db/orders/metadata/v1.json"
    );
    assert_eq!(result.metadata.metadata_log()[0].timestamp_ms, previous_ms);
    // Building without a current file location leaves the log untouched.
    let result2 = fresh_metadata()
        .into_builder(None)
        .set_location("file:///warehouse/db/orders3".to_string())
        .build()
        .unwrap();
    assert!(result2.metadata.metadata_log().is_empty());
}

#[test]
fn g02_identity__new_from_metadata_is_the_same_entry_point_as_into_builder() {
    // TEST_INTERFACE section 4.1: `TableMetadata::into_builder` and
    // `TableMetadataBuilder::new_from_metadata` are the same operation, and a
    // build result converts into its metadata.
    let md = fresh_metadata();
    let previous_ms = md.last_updated_ms();
    let log_path = "file:///warehouse/db/orders/metadata/v1.json".to_string();

    let via_assoc = TableMetadataBuilder::new_from_metadata(md.clone(), Some(log_path.clone()))
        .set_location("file:///warehouse/db/orders_v2".to_string())
        .build()
        .unwrap();
    let via_method = md
        .into_builder(Some(log_path.clone()))
        .set_location("file:///warehouse/db/orders_v2".to_string())
        .build()
        .unwrap();
    assert_eq!(change_actions(&via_assoc.changes), vec!["set-location"]);
    assert_eq!(change_actions(&via_method.changes), change_actions(&via_assoc.changes));
    assert_eq!(via_assoc.metadata.location(), via_method.metadata.location());
    assert_eq!(via_assoc.metadata.metadata_log(), via_method.metadata.metadata_log());
    assert_eq!(via_assoc.metadata.metadata_log()[0].metadata_file, log_path);
    assert_eq!(via_assoc.metadata.metadata_log()[0].timestamp_ms, previous_ms);

    // `From<TableMetadataBuildResult> for TableMetadata` yields the same value
    // as reading the `metadata` field. Compared field by field rather than as
    // whole values: `last-updated-ms` is a generated timestamp and PROFILE.md
    // P6 leaves a build free to restamp it, so two independent builds may
    // legitimately differ in that one field.
    let expected = via_method.metadata.clone();
    let converted: TableMetadata = via_assoc.into();
    assert_eq!(converted.uuid(), expected.uuid());
    assert_eq!(converted.location(), expected.location());
    assert_eq!(converted.current_schema_id(), expected.current_schema_id());
    assert_eq!(
        converted.default_partition_spec_id(),
        expected.default_partition_spec_id()
    );
    assert_eq!(converted.default_sort_order_id(), expected.default_sort_order_id());
    assert_eq!(converted.metadata_log(), expected.metadata_log());
    assert_eq!(converted.properties(), expected.properties());
    // "Table Metadata Fields": `last-updated-ms` also has a typed reading.
    assert_eq!(
        converted.last_updated_timestamp().unwrap().timestamp_millis(),
        converted.last_updated_ms()
    );
}

#[test]
fn g02_identity__metadata_log_is_capped_at_the_documented_default() {
    // "Table Metadata Fields" plus TEST_INTERFACE section 4.1: entries beyond
    // `write.metadata.previous-versions-max` (default 100) are dropped from
    // `metadata-log` and returned in `expired_metadata_logs`.
    let mut md = fresh_metadata();
    let mut expired_total = 0usize;
    for version in 1..=105 {
        let result = md
            .into_builder(Some(format!(
                "file:///warehouse/db/orders/metadata/v{version}.json"
            )))
            .set_location(format!("file:///warehouse/db/orders_v{version}"))
            .build()
            .unwrap();
        expired_total += result.expired_metadata_logs.len();
        md = result.metadata;
    }
    assert_eq!(
        md.metadata_log().len(),
        100,
        "the metadata log must be capped at the documented default of 100"
    );
    assert_eq!(
        expired_total, 5,
        "every entry past the cap must be reported in expired_metadata_logs"
    );
    // The surviving window is the most recent 100, in order.
    assert_eq!(
        md.metadata_log()[0].metadata_file,
        "file:///warehouse/db/orders/metadata/v6.json"
    );
    assert_eq!(
        md.metadata_log()[99].metadata_file,
        "file:///warehouse/db/orders/metadata/v105.json"
    );
}

// ---------------------------------------------------------------------------
// g03_schema — spec.md "Schema Evolution"
// ---------------------------------------------------------------------------

/// The created schema plus one new optional column.
fn widened_schema() -> Schema {
    Schema::builder()
        .with_schema_id(-1)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(2, "ts", Type::Primitive(PrimitiveType::Timestamptz)).into(),
            NestedField::optional(3, "name", Type::Primitive(PrimitiveType::String)).into(),
            NestedField::optional(4, "note", Type::Primitive(PrimitiveType::String)).into(),
        ])
        .build()
        .unwrap()
}

#[test]
fn g03_schema__add_current_schema_assigns_a_new_id() {
    let md = fresh_metadata();
    let result = md
        .into_builder(None)
        .add_current_schema(widened_schema())
        .unwrap()
        .build()
        .unwrap();
    let new_md = result.metadata;
    // A new schema is appended with the next free schema id; the old schema is
    // retained so that older snapshots stay readable.
    assert_eq!(new_md.current_schema_id(), 1);
    assert_eq!(new_md.schemas_iter().count(), 2);
    assert!(new_md.schema_by_id(0).is_some());
    assert_eq!(new_md.current_schema().field_id_by_name("note"), Some(4));
    // "Schema Evolution": last-column-id tracks the highest assigned column id.
    assert_eq!(field(&new_md, "last-column-id"), serde_json::json!(4));
    assert_eq!(
        change_actions(&result.changes),
        vec!["add-schema", "set-current-schema"]
    );
}

#[test]
fn g03_schema__adding_an_identical_schema_reuses_its_id() {
    // Re-adding the schema the table already has must not grow the schema list
    // nor change the current schema id.
    let md = fresh_metadata();
    let result = md
        .into_builder(None)
        .add_current_schema(assigned_schema(-1))
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(result.metadata.current_schema_id(), 0);
    assert_eq!(result.metadata.schemas_iter().count(), 1);
}

#[test]
fn g03_schema__last_column_id_never_shrinks() {
    // "Schema Evolution": column ids are never reused, so dropping a column
    // must not lower last-column-id.
    let md = fresh_metadata()
        .into_builder(None)
        .add_current_schema(widened_schema())
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    assert_eq!(field(&md, "last-column-id"), serde_json::json!(4));

    let narrowed = Schema::builder()
        .with_schema_id(-1)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
        ])
        .build()
        .unwrap();
    let after = md
        .into_builder(None)
        .add_current_schema(narrowed)
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    assert_eq!(after.current_schema().as_struct().fields().len(), 1);
    assert_eq!(field(&after, "last-column-id"), serde_json::json!(4));
}

#[test]
fn g03_schema__unknown_current_schema_is_rejected() {
    let md = fresh_metadata();
    assert!(
        md.clone().into_builder(None).set_current_schema(42).is_err(),
        "setting the current schema to an id that was never added must fail"
    );
    // LAST_ADDED means "the schema added by this set of updates"; with no
    // schema added
    // there is nothing to point at.
    assert!(md
        .into_builder(None)
        .set_current_schema(TableMetadataBuilder::LAST_ADDED)
        .is_err());
}

// ---------------------------------------------------------------------------
// g04_spec — spec.md "Partition Evolution"
// ---------------------------------------------------------------------------

fn day_spec() -> UnboundPartitionSpec {
    UnboundPartitionSpec::builder()
        .add_partition_field(2, "ts_day", Transform::Day)
        .unwrap()
        .build()
}

fn bucket_spec() -> UnboundPartitionSpec {
    UnboundPartitionSpec::builder()
        .add_partition_field(1, "id_bucket", Transform::Bucket(8))
        .unwrap()
        .build()
}

#[test]
fn g04_spec__add_default_spec_assigns_ids_from_1000() {
    let result = fresh_metadata()
        .into_builder(None)
        .add_default_partition_spec(day_spec())
        .unwrap()
        .build()
        .unwrap();
    let md = result.metadata;
    // "Partition Evolution": a new spec gets a new spec id; partition field ids
    // are assigned starting at 1000.
    assert_eq!(md.default_partition_spec_id(), 1);
    assert_eq!(md.default_partition_spec().fields().len(), 1);
    assert_eq!(md.default_partition_spec().fields()[0].field_id, 1000);
    assert_eq!(md.default_partition_spec().fields()[0].source_id, 2);
    assert_eq!(md.default_partition_spec().fields()[0].transform, Transform::Day);
    assert_eq!(field(&md, "last-partition-id"), serde_json::json!(1000));
    assert_eq!(
        change_actions(&result.changes),
        vec!["add-spec", "set-default-spec"]
    );
}

#[test]
fn g04_spec__second_spec_continues_from_last_partition_id() {
    // "Partition Evolution": in v2 metadata partition field ids are unique
    // across all specs, so the next spec continues from `last-partition-id`.
    let md = fresh_metadata()
        .into_builder(None)
        .add_default_partition_spec(day_spec())
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    let md = md
        .into_builder(None)
        .add_default_partition_spec(bucket_spec())
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    assert_eq!(md.default_partition_spec_id(), 2);
    assert_eq!(md.default_partition_spec().fields()[0].field_id, 1001);
    assert_eq!(field(&md, "last-partition-id"), serde_json::json!(1001));
    // The older specs are retained.
    assert_eq!(md.partition_specs_iter().count(), 3);
    assert!(md.partition_spec_by_id(1).is_some());
}

#[test]
fn g04_spec__default_partition_type_uses_transform_result_types() {
    // "Partitioning": the partition tuple's field ids are the partition field
    // ids and each field is optional.
    let md = fresh_metadata()
        .into_builder(None)
        .add_default_partition_spec(bucket_spec())
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    let ty = md.default_partition_type();
    assert_eq!(ty.fields().len(), 1);
    assert_eq!(ty.fields()[0].id, 1000);
    assert_eq!(ty.fields()[0].name, "id_bucket");
    assert_eq!(
        ty.fields()[0].field_type.as_ref(),
        &Type::Primitive(PrimitiveType::Int)
    );
    assert!(!ty.fields()[0].required);
}

#[test]
fn g04_spec__compatible_spec_reuses_its_id_and_unused_specs_are_removed() {
    // "Partition Evolution": specs are never redefined, so re-adding a spec
    // that is compatible with one the table already has reuses that spec's id
    // instead of allocating a new one.
    let md = fresh_metadata()
        .into_builder(None)
        .add_default_partition_spec(day_spec())
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    assert_eq!(md.default_partition_spec_id(), 1);
    assert_eq!(md.partition_specs_iter().count(), 2);

    // A spec whose fields agree on name, source and transform is compatible,
    // whatever spec id it carries, so no third spec appears.
    let again = md
        .into_builder(None)
        .add_partition_spec(day_spec().with_spec_id(77))
        .unwrap()
        .build()
        .unwrap();
    let md = again.metadata;
    assert_eq!(md.partition_specs_iter().count(), 2);
    assert!(md.partition_spec_by_id(77).is_none());
    assert!(md.partition_spec_by_id(1).is_some());
    assert_eq!(field(&md, "last-partition-id"), serde_json::json!(1000));

    // A spec that is not the default may be removed.
    let md = md
        .into_builder(None)
        .add_default_partition_spec(bucket_spec())
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    assert_eq!(md.default_partition_spec_id(), 2);
    assert_eq!(md.partition_specs_iter().count(), 3);
    let removed = md
        .into_builder(None)
        .remove_partition_specs(&[1])
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(
        change_actions(&removed.changes),
        vec!["remove-partition-specs"]
    );
    assert!(removed.metadata.partition_spec_by_id(1).is_none());
    assert_eq!(removed.metadata.partition_specs_iter().count(), 2);
    assert_eq!(removed.metadata.default_partition_spec_id(), 2);
}

#[test]
fn g04_spec__unknown_and_default_spec_rejections() {
    let md = fresh_metadata();
    assert!(
        md.clone()
            .into_builder(None)
            .set_default_partition_spec(42)
            .is_err(),
        "the default spec must exist in partition-specs"
    );
    assert!(md
        .clone()
        .into_builder(None)
        .set_default_partition_spec(-1)
        .is_err());
    // The default spec is in use and must not be removed.
    assert!(md
        .clone()
        .into_builder(None)
        .remove_partition_specs(&[0])
        .is_err());
    // A spec whose source column is not in the current schema cannot be bound.
    let dangling = UnboundPartitionSpec::builder()
        .add_partition_field(99, "nope", Transform::Identity)
        .unwrap()
        .build();
    assert!(md.into_builder(None).add_partition_spec(dangling).is_err());
}

#[test]
fn g04_spec__version_1_requires_sequential_field_ids() {
    // Appendix E "Version 2": v1 metadata stores a single partition spec whose
    // field ids must be sequential from 1000.
    let md = fresh_metadata_v1();
    let gapped = UnboundPartitionSpec::builder()
        .add_partition_fields(vec![UnboundPartitionField {
            source_id: 2,
            field_id: Some(1005),
            name: "ts_day".to_string(),
            transform: Transform::Day,
        }])
        .unwrap()
        .build();
    assert!(md.into_builder(None).add_partition_spec(gapped).is_err());
}

// ---------------------------------------------------------------------------
// g05_sort — spec.md "Sorting" + Appendix C "Sort Orders"
// ---------------------------------------------------------------------------

fn identity_sort_order() -> SortOrder {
    SortOrder::builder()
        .with_sort_field(
            SortField::builder()
                .source_id(1)
                .transform(Transform::Identity)
                .direction(SortDirection::Ascending)
                .null_order(NullOrder::First)
                .build(),
        )
        .build_unbound()
        .unwrap()
}

#[test]
fn g05_sort__add_default_sort_order() {
    let result = fresh_metadata()
        .into_builder(None)
        .add_sort_order(identity_sort_order())
        .unwrap()
        .set_default_sort_order(-1)
        .unwrap()
        .build()
        .unwrap();
    let md = result.metadata;
    // "Sorting": order id 0 is reserved for the unsorted order, so the first
    // real sort order gets id 1.
    assert_eq!(md.default_sort_order_id(), 1);
    assert_eq!(md.default_sort_order().fields.len(), 1);
    assert_eq!(md.default_sort_order().fields[0].source_id, 1);
    assert_eq!(md.default_sort_order().fields[0].direction, SortDirection::Ascending);
    assert_eq!(md.default_sort_order().fields[0].null_order, NullOrder::First);
    assert_eq!(md.sort_orders_iter().count(), 2);
    assert_eq!(md.sort_order_by_id(1).unwrap().order_id, 1);
    assert!(md.sort_order_by_id(42).is_none());
    assert_eq!(
        change_actions(&result.changes),
        vec!["add-sort-order", "set-default-sort-order"]
    );

    // Section 4.6: an order can equally be built *against* the schema it binds
    // to, and `SortOrder::with_order_id` restamps a built order's id.
    let bound = SortOrder::builder()
        .with_sort_field(
            SortField::builder()
                .source_id(1)
                .transform(Transform::Identity)
                .direction(SortDirection::Ascending)
                .null_order(NullOrder::First)
                .build(),
        )
        .build(md.current_schema())
        .unwrap();
    assert_eq!(bound.fields.len(), 1);
    assert_eq!(bound.fields[0].source_id, 1);
    assert_eq!(bound.with_order_id(7).order_id, 7);
}

#[test]
fn g05_sort__unsorted_order_keeps_id_zero() {
    let result = fresh_metadata()
        .into_builder(None)
        .add_sort_order(SortOrder::unsorted_order())
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(result.metadata.default_sort_order_id(), 0);
    assert!(result.metadata.default_sort_order().is_unsorted());
    assert_eq!(result.metadata.sort_orders_iter().count(), 1);
}

#[test]
fn g05_sort__incompatible_or_unknown_order_is_rejected() {
    // "Sorting": a sort field's source id must exist in the schema the order is
    // bound to.
    let dangling = SortOrder::builder()
        .with_sort_field(
            SortField::builder()
                .source_id(99)
                .transform(Transform::Identity)
                .direction(SortDirection::Ascending)
                .null_order(NullOrder::First)
                .build(),
        )
        .build_unbound()
        .unwrap();
    let md = fresh_metadata();
    assert!(md.clone().into_builder(None).add_sort_order(dangling).is_err());
    assert!(md
        .clone()
        .into_builder(None)
        .set_default_sort_order(42)
        .is_err());
    assert!(md.into_builder(None).set_default_sort_order(-1).is_err());
}

// ---------------------------------------------------------------------------
// g06_properties — spec.md "Table Metadata Fields" (properties)
// ---------------------------------------------------------------------------

#[test]
fn g06_properties__set_then_remove() {
    let result = fresh_metadata()
        .into_builder(None)
        .set_properties(HashMap::from([
            ("a".to_string(), "1".to_string()),
            ("b".to_string(), "2".to_string()),
        ]))
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(result.metadata.properties().len(), 2);
    assert_eq!(
        result.metadata.properties().get("a").map(String::as_str),
        Some("1")
    );
    assert_eq!(change_actions(&result.changes), vec!["set-properties"]);

    let removed = result
        .metadata
        .into_builder(None)
        .remove_properties(&["a".to_string()])
        .unwrap()
        .build()
        .unwrap();
    assert!(removed.metadata.properties().get("a").is_none());
    assert_eq!(
        removed.metadata.properties().get("b").map(String::as_str),
        Some("2")
    );
    assert_eq!(change_actions(&removed.changes), vec!["remove-properties"]);
}

#[test]
fn g06_properties__set_overwrites_and_empty_change_is_not_recorded() {
    let md = fresh_metadata()
        .into_builder(None)
        .set_properties(HashMap::from([("a".to_string(), "1".to_string())]))
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    let result = md
        .into_builder(None)
        .set_properties(HashMap::from([("a".to_string(), "2".to_string())]))
        .unwrap()
        .set_properties(HashMap::new())
        .unwrap()
        .remove_properties(&[])
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(
        result.metadata.properties().get("a").map(String::as_str),
        Some("2")
    );
    assert_eq!(change_actions(&result.changes), vec!["set-properties"]);
}

#[test]
fn g06_properties__reserved_properties_are_rejected() {
    // "Table Metadata Fields": none of the reserved keys is an arbitrary table
    // property; each is a metadata field with its own update, and neither
    // method may persist one. TEST_INTERFACE section 4.1 lists all nine.
    let md = fresh_metadata();
    for key in [
        "format-version",
        "uuid",
        "snapshot-count",
        "current-snapshot-id",
        "current-snapshot-summary",
        "current-snapshot-timestamp-ms",
        "current-schema",
        "default-partition-spec",
        "default-sort-order",
    ] {
        assert!(
            md.clone()
                .into_builder(None)
                .set_properties(HashMap::from([(key.to_string(), "x".to_string())]))
                .is_err(),
            "set_properties must reject the reserved key {key}"
        );
        assert!(
            md.clone()
                .into_builder(None)
                .remove_properties(&[key.to_string()])
                .is_err(),
            "remove_properties must reject the reserved key {key}"
        );
    }
}

// ---------------------------------------------------------------------------
// g07_snapshot — spec.md "Snapshots" + "Snapshot References"
// ---------------------------------------------------------------------------

#[test]
fn g07_snapshot__set_branch_snapshot_updates_main_and_the_log() {
    let md = fresh_metadata();
    let ts = md.last_updated_ms() + 1_000;
    let result = md
        .into_builder(None)
        .set_branch_snapshot(append_snapshot(3051, None, 1, ts), "main")
        .unwrap()
        .build()
        .unwrap();
    let new_md = result.metadata;
    // "Snapshot References": the `main` branch reference and
    // `current-snapshot-id` must agree.
    assert_eq!(new_md.current_snapshot_id(), Some(3051));
    assert_eq!(new_md.snapshot_for_ref("main").unwrap().snapshot_id(), 3051);
    assert_eq!(new_md.snapshots().count(), 1);
    // "Snapshots": last-sequence-number tracks the highest assigned sequence
    // number.
    assert_eq!(new_md.last_sequence_number(), 1);
    // "Sequence Numbers": the next commit's sequence number follows the last
    // assigned one.
    assert_eq!(new_md.next_sequence_number(), 2);
    assert_eq!(new_md.snapshot_by_id(3051).unwrap().snapshot_id(), 3051);
    assert!(new_md.snapshot_by_id(999).is_none());
    // Section 4.6's `Snapshot` accessors, read off the stored snapshot.
    let stored = new_md.snapshot_by_id(3051).unwrap();
    assert_eq!(stored.parent_snapshot_id(), None);
    assert_eq!(stored.sequence_number(), 1);
    assert_eq!(stored.timestamp_ms(), ts);
    assert_eq!(stored.timestamp().unwrap().timestamp_millis(), ts);
    assert_eq!(stored.summary().operation, Operation::Append);
    assert!(stored.summary().additional_properties.is_empty());
    assert_eq!(stored.schema_id(), Some(0));
    assert_eq!(stored.schema(&new_md).unwrap().schema_id(), 0);
    // The snapshot-log entry this snapshot contributes is observed through
    // `TableMetadata::history()` above, not through a method on `Snapshot`
    // (TEST_INTERFACE section 8).
    // "Table Metadata Fields": snapshot-log gains an entry each time the
    // current snapshot changes.
    assert_eq!(new_md.history().len(), 1);
    assert_eq!(new_md.history()[0].snapshot_id, 3051);
    assert_eq!(
        change_actions(&result.changes),
        vec!["add-snapshot", "set-snapshot-ref"]
    );
}

#[test]
fn g07_snapshot__snapshot_log_grows_in_order() {
    let md = fresh_metadata();
    let ts = md.last_updated_ms() + 1_000;
    let md = md
        .into_builder(None)
        .set_branch_snapshot(append_snapshot(3051, None, 1, ts), "main")
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    let md = md
        .into_builder(None)
        .set_branch_snapshot(append_snapshot(3055, Some(3051), 2, ts + 1_000), "main")
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    assert_eq!(md.current_snapshot_id(), Some(3055));
    assert_eq!(md.last_sequence_number(), 2);
    assert_eq!(md.snapshots().count(), 2);
    assert_eq!(md.history().len(), 2);
    assert_eq!(md.history()[0].snapshot_id, 3051);
    assert_eq!(md.history()[1].snapshot_id, 3055);
    assert!(md.history()[0].timestamp_ms <= md.history()[1].timestamp_ms);
    assert_eq!(
        md.current_snapshot().unwrap().parent_snapshot_id(),
        Some(3051)
    );
}

#[test]
fn g07_snapshot__duplicate_id_and_backwards_sequence_number_are_rejected() {
    let md = fresh_metadata();
    let ts = md.last_updated_ms() + 1_000;
    let snapshot = append_snapshot(3051, None, 1, ts);
    // "Snapshots": snapshot ids are unique within a table.
    assert!(md
        .clone()
        .into_builder(None)
        .add_snapshot(snapshot.clone())
        .unwrap()
        .add_snapshot(snapshot.clone())
        .is_err());

    let committed = md
        .into_builder(None)
        .set_branch_snapshot(snapshot, "main")
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    // "Snapshots": sequence numbers are monotonically increasing.
    let stale = append_snapshot(3055, Some(3051), 1, ts + 1_000);
    assert!(committed.into_builder(None).add_snapshot(stale).is_err());
}

#[test]
fn g07_snapshot__ref_must_point_at_a_known_snapshot() {
    // "Snapshot References": a reference names a snapshot that exists.
    let md = fresh_metadata();
    assert!(md
        .into_builder(None)
        .set_ref(
            "main",
            SnapshotReference::new(999, SnapshotRetention::branch(None, None, None))
        )
        .is_err());
}

#[test]
fn g07_snapshot__tag_reference_does_not_move_main() {
    // "Snapshot References": only the `main` branch drives
    // `current-snapshot-id`; a tag, and any other branch, is an independent
    // reference. A bare `add_snapshot` moves neither.
    let md = fresh_metadata();
    let ts = md.last_updated_ms() + 1_000;
    let result = md
        .clone()
        .into_builder(None)
        .add_snapshot(append_snapshot(3051, None, 1, ts))
        .unwrap()
        .set_ref(
            "release",
            SnapshotReference::new(3051, SnapshotRetention::Tag {
                max_ref_age_ms: Some(86_400_000),
            }),
        )
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(result.metadata.current_snapshot_id(), None);
    assert_eq!(
        result.metadata.snapshot_for_ref("release").unwrap().snapshot_id(),
        3051
    );
    assert!(result.metadata.snapshot_for_ref(MAIN_BRANCH).is_none());
    assert!(result.metadata.history().is_empty());

    // A branch other than `main` behaves the same way.
    let side = md
        .into_builder(None)
        .set_branch_snapshot(append_snapshot(3052, None, 1, ts), "audit")
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(side.metadata.current_snapshot_id(), None);
    assert!(side.metadata.snapshot_for_ref(MAIN_BRANCH).is_none());
    assert_eq!(
        side.metadata.snapshot_for_ref("audit").unwrap().snapshot_id(),
        3052
    );
    assert!(side.metadata.history().is_empty());
    // A reference knows which kind it is.
    assert!(SnapshotReference::new(3052, SnapshotRetention::branch(None, None, None)).is_branch());
    assert!(!SnapshotReference::new(3051, SnapshotRetention::Tag {
        max_ref_age_ms: None
    })
    .is_branch());
}

#[test]
fn g07_snapshot__remove_snapshots_and_refs() {
    // TEST_INTERFACE section 4.1: `remove_snapshots` drops the named snapshots
    // and any reference left pointing at one of them; `remove_ref` drops a
    // reference, and dropping `main` clears `current-snapshot-id` and the
    // snapshot log.
    let md = fresh_metadata();
    let ts = md.last_updated_ms() + 1_000;
    let md = md
        .into_builder(None)
        .set_branch_snapshot(append_snapshot(3051, None, 1, ts), MAIN_BRANCH)
        .unwrap()
        .build()
        .unwrap()
        .metadata
        .into_builder(None)
        .set_branch_snapshot(append_snapshot(3055, Some(3051), 2, ts + 1_000), MAIN_BRANCH)
        .unwrap()
        .set_ref(
            "release",
            SnapshotReference::new(3051, SnapshotRetention::Tag {
                max_ref_age_ms: None,
            }),
        )
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    assert_eq!(md.snapshots().count(), 2);
    assert_eq!(md.current_snapshot_id(), Some(3055));

    // Removing a reference leaves the snapshots alone.
    let dropped_ref = md.into_builder(None).remove_ref("release").build().unwrap();
    assert_eq!(
        change_actions(&dropped_ref.changes),
        vec!["remove-snapshot-ref"]
    );
    let md = dropped_ref.metadata;
    assert!(md.snapshot_for_ref("release").is_none());
    assert_eq!(md.snapshots().count(), 2);
    assert_eq!(md.current_snapshot_id(), Some(3055));

    // Removing a snapshot drops it from `snapshots` and repairs the log: the
    // entry naming the removed snapshot goes with it, and the log's last entry
    // is still the current snapshot (TEST_INTERFACE section 4.1).
    let dropped_snapshot = md.into_builder(None).remove_snapshots(&[3051]).build().unwrap();
    assert_eq!(
        change_actions(&dropped_snapshot.changes),
        vec!["remove-snapshots"]
    );
    let md = dropped_snapshot.metadata;
    assert_eq!(md.snapshots().count(), 1);
    assert!(md.snapshot_by_id(3051).is_none());
    assert_eq!(md.current_snapshot_id(), Some(3055));
    assert_eq!(md.history().len(), 1);
    assert_eq!(md.history().last().unwrap().snapshot_id, 3055);

    // Removing `main` leaves the table with no current snapshot.
    let no_main = md.into_builder(None).remove_ref(MAIN_BRANCH).build().unwrap();
    assert_eq!(
        change_actions(&no_main.changes),
        vec!["remove-snapshot-ref"]
    );
    assert_eq!(no_main.metadata.current_snapshot_id(), None);
    assert!(no_main.metadata.snapshot_for_ref(MAIN_BRANCH).is_none());
    assert!(no_main.metadata.history().is_empty());
}

// ---------------------------------------------------------------------------
// g08_format_version — spec.md Appendix E "Format version changes"
// ---------------------------------------------------------------------------

#[test]
fn g08_format_version__upgrade_v1_to_v2() {
    let md = fresh_metadata_v1();
    assert_eq!(md.format_version(), FormatVersion::V1);
    let result = md
        .into_builder(None)
        .upgrade_format_version(FormatVersion::V2)
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(result.metadata.format_version(), FormatVersion::V2);
    assert_eq!(change_actions(&result.changes), vec!["upgrade-format-version"]);
}

#[test]
fn g08_format_version__downgrade_is_rejected() {
    // Appendix E: format versions only move forward.
    let md = fresh_metadata();
    assert_eq!(md.format_version(), FormatVersion::V2);
    assert!(md.into_builder(None).upgrade_format_version(FormatVersion::V1).is_err());
}

#[test]
fn g08_format_version__upgrade_to_the_same_version_is_a_no_op() {
    let md = fresh_metadata();
    let result = md
        .into_builder(None)
        .upgrade_format_version(FormatVersion::V2)
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(result.metadata.format_version(), FormatVersion::V2);
    assert!(result.changes.is_empty());
    // FormatVersion is ordered so that v1 < v2.
    assert!(FormatVersion::V1 < FormatVersion::V2);
}

// ---------------------------------------------------------------------------
// g09_update_json — the REST metadata-update documents named after the
// spec.md "Table Metadata Fields" they change
// ---------------------------------------------------------------------------

#[test]
fn g09_update_json__action_tags() {
    let cases: Vec<(TableUpdate, &str)> = vec![
        (
            TableUpdate::UpgradeFormatVersion {
                format_version: FormatVersion::V2,
            },
            "upgrade-format-version",
        ),
        (
            TableUpdate::AssignUuid {
                uuid: uuid::Uuid::nil(),
            },
            "assign-uuid",
        ),
        (
            TableUpdate::AddSchema {
                schema: assigned_schema(1),
            },
            "add-schema",
        ),
        (TableUpdate::SetCurrentSchema { schema_id: -1 }, "set-current-schema"),
        (TableUpdate::AddSpec { spec: day_spec() }, "add-spec"),
        (TableUpdate::SetDefaultSpec { spec_id: -1 }, "set-default-spec"),
        (
            TableUpdate::AddSortOrder {
                sort_order: identity_sort_order(),
            },
            "add-sort-order",
        ),
        (
            TableUpdate::SetDefaultSortOrder { sort_order_id: -1 },
            "set-default-sort-order",
        ),
        (
            TableUpdate::AddSnapshot {
                snapshot: append_snapshot(1, None, 1, 1_600_000_000_000),
            },
            "add-snapshot",
        ),
        (
            TableUpdate::RemoveSnapshots {
                snapshot_ids: vec![1],
            },
            "remove-snapshots",
        ),
        (
            TableUpdate::RemoveSnapshotRef {
                ref_name: "main".to_string(),
            },
            "remove-snapshot-ref",
        ),
        (
            TableUpdate::SetLocation {
                location: "file:///t".to_string(),
            },
            "set-location",
        ),
        (
            TableUpdate::SetProperties {
                updates: HashMap::from([("a".to_string(), "1".to_string())]),
            },
            "set-properties",
        ),
        (
            TableUpdate::RemoveProperties {
                removals: vec!["a".to_string()],
            },
            "remove-properties",
        ),
        (
            TableUpdate::RemovePartitionSpecs { spec_ids: vec![3] },
            "remove-partition-specs",
        ),
    ];
    for (update, action) in cases {
        let value = serde_json::to_value(&update).unwrap();
        assert_eq!(value["action"], action, "action tag of {update:?}");
        let back: TableUpdate = serde_json::from_value(value).unwrap();
        assert_eq!(back, update, "round trip of {action}");
    }
}

#[test]
fn g09_update_json__field_names_are_kebab_case() {
    let value = serde_json::to_value(TableUpdate::SetCurrentSchema { schema_id: -1 }).unwrap();
    assert_eq!(value["schema-id"], -1);

    let value = serde_json::to_value(TableUpdate::SetDefaultSpec { spec_id: 4 }).unwrap();
    assert_eq!(value["spec-id"], 4);

    let value =
        serde_json::to_value(TableUpdate::SetDefaultSortOrder { sort_order_id: 2 }).unwrap();
    assert_eq!(value["sort-order-id"], 2);

    let value = serde_json::to_value(TableUpdate::UpgradeFormatVersion {
        format_version: FormatVersion::V2,
    })
    .unwrap();
    assert_eq!(value["format-version"], 2);

    let value = serde_json::to_value(TableUpdate::RemoveSnapshots {
        snapshot_ids: vec![7, 8],
    })
    .unwrap();
    assert_eq!(value["snapshot-ids"], serde_json::json!([7, 8]));

    let value = serde_json::to_value(TableUpdate::RemovePartitionSpecs { spec_ids: vec![3] }).unwrap();
    assert_eq!(value["spec-ids"], serde_json::json!([3]));
}

#[test]
fn g09_update_json__snapshot_ref_is_flattened() {
    // "Snapshot References": a reference document carries `snapshot-id`, a
    // `type` of `branch` or `tag`, and the retention fields inline.
    let update = TableUpdate::SetSnapshotRef {
        ref_name: "main".to_string(),
        reference: SnapshotReference::new(
            3055,
            SnapshotRetention::Branch {
                min_snapshots_to_keep: Some(2),
                max_snapshot_age_ms: None,
                max_ref_age_ms: None,
            },
        ),
    };
    let value = serde_json::to_value(&update).unwrap();
    assert_eq!(value["action"], "set-snapshot-ref");
    assert_eq!(value["ref-name"], "main");
    assert_eq!(value["snapshot-id"], 3055);
    assert_eq!(value["type"], "branch");
    assert_eq!(value["min-snapshots-to-keep"], 2);
    let back: TableUpdate = serde_json::from_value(value).unwrap();
    assert_eq!(back, update);
}

#[test]
fn g09_update_json__add_spec_carries_an_unbound_spec() {
    // Appendix C "Partition Specs": a spec document has `spec-id` and `fields`
    // with `source-id`, `name` and `transform`. In an `add-spec` update the
    // field ids are still unassigned.
    let update = TableUpdate::AddSpec { spec: day_spec() };
    let value = serde_json::to_value(&update).unwrap();
    assert_eq!(value["action"], "add-spec");
    assert_eq!(value["spec"]["fields"][0]["source-id"], 2);
    assert_eq!(value["spec"]["fields"][0]["name"], "ts_day");
    assert_eq!(value["spec"]["fields"][0]["transform"], "day");
    let back: TableUpdate = serde_json::from_value(value).unwrap();
    assert_eq!(back, update);
}

// ---------------------------------------------------------------------------
// g10_requirement_json — the REST commit requirements, named after the
// spec.md "Table Metadata Fields" they assert
// ---------------------------------------------------------------------------

#[test]
fn g10_requirement_json__type_tags() {
    let cases: Vec<(TableRequirement, &str)> = vec![
        (TableRequirement::NotExist, "assert-create"),
        (
            TableRequirement::UuidMatch {
                uuid: uuid::Uuid::nil(),
            },
            "assert-table-uuid",
        ),
        (
            TableRequirement::RefSnapshotIdMatch {
                r#ref: "main".to_string(),
                snapshot_id: Some(3055),
            },
            "assert-ref-snapshot-id",
        ),
        (
            TableRequirement::LastAssignedFieldIdMatch {
                last_assigned_field_id: 3,
            },
            "assert-last-assigned-field-id",
        ),
        (
            TableRequirement::CurrentSchemaIdMatch {
                current_schema_id: 1,
            },
            "assert-current-schema-id",
        ),
        (
            TableRequirement::LastAssignedPartitionIdMatch {
                last_assigned_partition_id: 1000,
            },
            "assert-last-assigned-partition-id",
        ),
        (
            TableRequirement::DefaultSpecIdMatch { default_spec_id: 0 },
            "assert-default-spec-id",
        ),
        (
            TableRequirement::DefaultSortOrderIdMatch {
                default_sort_order_id: 0,
            },
            "assert-default-sort-order-id",
        ),
    ];
    for (requirement, tag) in cases {
        let value = serde_json::to_value(&requirement).unwrap();
        assert_eq!(value["type"], tag, "type tag of {requirement:?}");
        let back: TableRequirement = serde_json::from_value(value).unwrap();
        assert_eq!(back, requirement, "round trip of {tag}");
    }
}

#[test]
fn g10_requirement_json__field_names_are_kebab_case() {
    let value = serde_json::to_value(TableRequirement::RefSnapshotIdMatch {
        r#ref: "main".to_string(),
        snapshot_id: Some(3055),
    })
    .unwrap();
    assert_eq!(value["ref"], "main");
    assert_eq!(value["snapshot-id"], 3055);

    let value = serde_json::to_value(TableRequirement::CurrentSchemaIdMatch {
        current_schema_id: 4,
    })
    .unwrap();
    assert_eq!(value["current-schema-id"], 4);

    let value = serde_json::to_value(TableRequirement::LastAssignedFieldIdMatch {
        last_assigned_field_id: 9,
    })
    .unwrap();
    assert_eq!(value["last-assigned-field-id"], 9);

    let value = serde_json::to_value(TableRequirement::LastAssignedPartitionIdMatch {
        last_assigned_partition_id: 1002,
    })
    .unwrap();
    assert_eq!(value["last-assigned-partition-id"], 1002);

    let value =
        serde_json::to_value(TableRequirement::DefaultSpecIdMatch { default_spec_id: 5 }).unwrap();
    assert_eq!(value["default-spec-id"], 5);

    let value = serde_json::to_value(TableRequirement::DefaultSortOrderIdMatch {
        default_sort_order_id: 6,
    })
    .unwrap();
    assert_eq!(value["default-sort-order-id"], 6);
}

// ---------------------------------------------------------------------------
// g11_requirement_check — commit requirements evaluated against metadata
// ---------------------------------------------------------------------------

/// Metadata with one committed snapshot on `main` and a partitioned spec, so
/// that every requirement kind has something non-default to assert against.
fn committed_metadata() -> TableMetadata {
    let md = fresh_metadata();
    let ts = md.last_updated_ms() + 1_000;
    md.into_builder(None)
        .add_default_partition_spec(day_spec())
        .unwrap()
        .set_branch_snapshot(append_snapshot(3055, None, 1, ts), "main")
        .unwrap()
        .build()
        .unwrap()
        .metadata
}

#[test]
fn g11_requirement_check__uuid_and_ids() {
    let md = committed_metadata();
    assert!(TableRequirement::UuidMatch { uuid: md.uuid() }
        .check(Some(&md))
        .is_ok());
    assert!(TableRequirement::UuidMatch {
        uuid: uuid::Uuid::nil()
    }
    .check(Some(&md))
    .is_err());

    assert!(TableRequirement::CurrentSchemaIdMatch {
        current_schema_id: md.current_schema_id()
    }
    .check(Some(&md))
    .is_ok());
    assert!(TableRequirement::CurrentSchemaIdMatch {
        current_schema_id: md.current_schema_id() + 1
    }
    .check(Some(&md))
    .is_err());

    assert!(TableRequirement::DefaultSpecIdMatch {
        default_spec_id: md.default_partition_spec_id()
    }
    .check(Some(&md))
    .is_ok());
    assert!(TableRequirement::DefaultSpecIdMatch {
        default_spec_id: md.default_partition_spec_id() + 1
    }
    .check(Some(&md))
    .is_err());

    assert!(TableRequirement::DefaultSortOrderIdMatch {
        default_sort_order_id: md.default_sort_order_id()
    }
    .check(Some(&md))
    .is_ok());
    assert!(TableRequirement::DefaultSortOrderIdMatch {
        default_sort_order_id: md.default_sort_order_id() + 1
    }
    .check(Some(&md))
    .is_err());
}

#[test]
fn g11_requirement_check__last_assigned_ids() {
    // "Schema Evolution" / "Partition Evolution": a committer asserts the last
    // assigned column id and partition field id so that concurrent id
    // assignment is detected.
    let md = committed_metadata();
    assert!(TableRequirement::LastAssignedFieldIdMatch {
        last_assigned_field_id: 3
    }
    .check(Some(&md))
    .is_ok());
    assert!(TableRequirement::LastAssignedFieldIdMatch {
        last_assigned_field_id: 4
    }
    .check(Some(&md))
    .is_err());

    assert!(TableRequirement::LastAssignedPartitionIdMatch {
        last_assigned_partition_id: 1000
    }
    .check(Some(&md))
    .is_ok());
    assert!(TableRequirement::LastAssignedPartitionIdMatch {
        last_assigned_partition_id: 1001
    }
    .check(Some(&md))
    .is_err());
}

#[test]
fn g11_requirement_check__ref_snapshot_id() {
    // "Snapshot References": asserting a ref's snapshot id detects a concurrent
    // commit on that branch. A null snapshot id asserts the ref does not exist.
    let md = committed_metadata();
    assert!(TableRequirement::RefSnapshotIdMatch {
        r#ref: "main".to_string(),
        snapshot_id: Some(3055)
    }
    .check(Some(&md))
    .is_ok());
    assert!(TableRequirement::RefSnapshotIdMatch {
        r#ref: "main".to_string(),
        snapshot_id: Some(3051)
    }
    .check(Some(&md))
    .is_err());
    assert!(TableRequirement::RefSnapshotIdMatch {
        r#ref: "main".to_string(),
        snapshot_id: None
    }
    .check(Some(&md))
    .is_err());
    assert!(TableRequirement::RefSnapshotIdMatch {
        r#ref: "absent".to_string(),
        snapshot_id: None
    }
    .check(Some(&md))
    .is_ok());
    assert!(TableRequirement::RefSnapshotIdMatch {
        r#ref: "absent".to_string(),
        snapshot_id: Some(3055)
    }
    .check(Some(&md))
    .is_err());
}

#[test]
fn g11_requirement_check__assert_create() {
    // `assert-create` requires that the table does not exist yet.
    let md = committed_metadata();
    assert!(TableRequirement::NotExist.check(None).is_ok());
    assert!(TableRequirement::NotExist.check(Some(&md)).is_err());
    // Every other requirement fails against a table that does not exist.
    assert!(TableRequirement::UuidMatch { uuid: md.uuid() }.check(None).is_err());
}

// ---------------------------------------------------------------------------
// g12_update_apply — a TableUpdate applied to a builder makes the change it
// describes (spec.md "Table Metadata")
// ---------------------------------------------------------------------------

#[test]
fn g12_update_apply__sequence_of_updates() {
    let md = fresh_metadata();
    let uuid = "d20125c8-7284-442c-9aea-15fee620737c"
        .parse::<uuid::Uuid>()
        .unwrap();
    let updates = vec![
        TableUpdate::AssignUuid { uuid },
        TableUpdate::SetLocation {
            location: "file:///warehouse/db/orders_moved".to_string(),
        },
        TableUpdate::AddSchema {
            schema: widened_schema(),
        },
        TableUpdate::SetCurrentSchema { schema_id: -1 },
        TableUpdate::AddSpec { spec: day_spec() },
        TableUpdate::SetDefaultSpec { spec_id: -1 },
        TableUpdate::SetProperties {
            updates: HashMap::from([("owner".to_string(), "eng".to_string())]),
        },
    ];
    let mut builder = md.into_builder(None);
    for update in updates {
        builder = update.apply(builder).unwrap();
    }
    let result = builder.build().unwrap();
    let new_md = result.metadata;
    assert_eq!(new_md.uuid(), uuid);
    assert_eq!(new_md.location(), "file:///warehouse/db/orders_moved");
    assert_eq!(new_md.current_schema_id(), 1);
    assert_eq!(new_md.current_schema().field_id_by_name("note"), Some(4));
    assert_eq!(new_md.default_partition_spec_id(), 1);
    assert_eq!(new_md.default_partition_spec().fields()[0].field_id, 1000);
    assert_eq!(
        new_md.properties().get("owner").map(String::as_str),
        Some("eng")
    );
}

#[test]
fn g12_update_apply__recorded_changes_replay_to_the_same_metadata() {
    // The recorded `changes` are the update log for the commit: replaying them
    // on the original metadata must reproduce the same metadata.
    let base = fresh_metadata();
    let ts = base.last_updated_ms() + 1_000;
    let result = base
        .clone()
        .into_builder(None)
        .add_default_partition_spec(day_spec())
        .unwrap()
        .set_properties(HashMap::from([("a".to_string(), "1".to_string())]))
        .unwrap()
        .set_branch_snapshot(append_snapshot(3055, None, 1, ts), "main")
        .unwrap()
        .build()
        .unwrap();
    assert!(!result.changes.is_empty());

    let mut builder = base.into_builder(None);
    for update in result.changes.clone() {
        builder = update.apply(builder).unwrap();
    }
    let replayed = builder.build().unwrap().metadata;
    assert_eq!(replayed.uuid(), result.metadata.uuid());
    assert_eq!(replayed.default_partition_spec_id(), result.metadata.default_partition_spec_id());
    assert_eq!(replayed.current_snapshot_id(), result.metadata.current_snapshot_id());
    assert_eq!(replayed.last_sequence_number(), result.metadata.last_sequence_number());
    assert_eq!(replayed.properties(), result.metadata.properties());
    assert_eq!(replayed.history().len(), result.metadata.history().len());
}

#[test]
fn g12_update_apply__failing_update_is_reported() {
    // Applying an update the metadata cannot accept surfaces an error rather
    // than silently producing invalid metadata.
    let md = fresh_metadata();
    let builder = md.into_builder(None);
    assert!(TableUpdate::SetCurrentSchema { schema_id: 99 }
        .apply(builder)
        .is_err());
}

// ---------------------------------------------------------------------------
// g13_ns_types — spec.md "Primitive Types" (timestamp_ns / timestamptz_ns)
// and the "Partition Transforms" result-type table
// ---------------------------------------------------------------------------

#[test]
fn g13_ns_types__spec_names() {
    // "Primitive Types": the nanosecond timestamp types are named
    // `timestamp_ns` and `timestamptz_ns`.
    assert_eq!(format!("{}", PrimitiveType::TimestampNs), "timestamp_ns");
    assert_eq!(format!("{}", PrimitiveType::TimestamptzNs), "timestamptz_ns");
    // They are distinct from the microsecond types.
    assert_ne!(PrimitiveType::TimestampNs, PrimitiveType::Timestamp);
    assert_ne!(PrimitiveType::TimestamptzNs, PrimitiveType::Timestamptz);
    assert!(Type::Primitive(PrimitiveType::TimestampNs).is_primitive());
    assert!(Type::Primitive(PrimitiveType::TimestamptzNs).is_primitive());
}

#[test]
fn g13_ns_types__transform_result_types() {
    // "Partition Transforms": `year`, `month` and `hour` over a nanosecond
    // timestamp produce `int`; `bucket` produces `int`; `identity` keeps the
    // source type; `truncate` is not defined over timestamps.
    for source in [PrimitiveType::TimestampNs, PrimitiveType::TimestamptzNs] {
        let ty = Type::Primitive(source.clone());
        let int = Type::Primitive(PrimitiveType::Int);
        assert_eq!(Transform::Year.result_type(&ty).unwrap(), int, "year over {source}");
        assert_eq!(Transform::Month.result_type(&ty).unwrap(), int, "month over {source}");
        assert_eq!(Transform::Hour.result_type(&ty).unwrap(), int, "hour over {source}");
        assert_eq!(
            Transform::Bucket(16).result_type(&ty).unwrap(),
            int,
            "bucket over {source}"
        );
        assert_eq!(Transform::Identity.result_type(&ty).unwrap(), ty);
        assert_eq!(Transform::Void.result_type(&ty).unwrap(), ty);
        // `day` is defined over nanosecond timestamps.
        assert!(Transform::Day.result_type(&ty).is_ok(), "day over {source}");
        // `truncate` is not.
        assert!(Transform::Truncate(4).result_type(&ty).is_err());
    }
}

#[test]
fn g13_ns_types__binary_single_value_is_an_eight_byte_little_endian_long() {
    // Appendix D "Binary single-value serialization": `timestamp_ns` and
    // `timestamptz_ns` store nanoseconds since the epoch in an 8-byte
    // little-endian long.
    let nanos: i64 = 1_510_871_468_123_456_789;
    for source in [PrimitiveType::TimestampNs, PrimitiveType::TimestamptzNs] {
        let datum = Datum::try_from_bytes(&nanos.to_le_bytes(), source.clone()).unwrap();
        assert_eq!(datum.data_type(), &source);
        assert_eq!(datum.literal(), &PrimitiveLiteral::Long(nanos));
        assert_eq!(datum.to_bytes().unwrap().as_ref(), &nanos.to_le_bytes());
        // A nanosecond timestamp value is a long, not an int.
        assert!(source.compatible(&PrimitiveLiteral::Long(nanos)));
        assert!(!source.compatible(&PrimitiveLiteral::Int(1)));
    }
    assert_eq!(
        Datum::timestamp_nanos(nanos).data_type(),
        &PrimitiveType::TimestampNs
    );
    assert_eq!(
        Datum::timestamptz_nanos(nanos).data_type(),
        &PrimitiveType::TimestamptzNs
    );
}

#[test]
fn g13_ns_types__usable_as_a_partition_source() {
    // "Partition Evolution": a nanosecond timestamp column can be partitioned
    // by a time transform, and the partition field's type is the transform's
    // result type.
    let schema = Schema::builder()
        .with_schema_id(0)
        .with_fields(vec![
            NestedField::required(1, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::required(2, "event_ts", Type::Primitive(PrimitiveType::TimestampNs))
                .into(),
        ])
        .build()
        .unwrap();
    let spec = UnboundPartitionSpec::builder()
        .add_partition_field(2, "event_hour", Transform::Hour)
        .unwrap()
        .build()
        .bind(schema.clone())
        .unwrap();
    assert_eq!(spec.fields()[0].field_id, 1000);
    let ty = spec.partition_type(&schema).unwrap();
    assert_eq!(
        ty.fields()[0].field_type.as_ref(),
        &Type::Primitive(PrimitiveType::Int)
    );
}

// ---------------------------------------------------------------------------
// g14_unbound_spec — spec.md "Partition Evolution" + Appendix C
// "Partition Specs"
// ---------------------------------------------------------------------------

fn partition_schema() -> SchemaRef {
    assigned_schema(0).into()
}

#[test]
fn g14_unbound_spec__bind_assigns_field_ids_from_1000() {
    // "Partition Evolution": an unbound spec carries no field ids; binding it
    // to a schema assigns them starting at 1000.
    let unbound = UnboundPartitionSpec::builder()
        .add_partition_field(2, "ts_day", Transform::Day)
        .unwrap()
        .add_partition_field(1, "id_bucket", Transform::Bucket(8))
        .unwrap()
        .build();
    assert_eq!(unbound.spec_id(), None);
    assert_eq!(unbound.fields().len(), 2);
    assert_eq!(unbound.fields()[0].field_id, None);

    let bound = unbound.bind(partition_schema()).unwrap();
    assert_eq!(bound.spec_id(), 0);
    assert_eq!(bound.fields()[0].field_id, 1000);
    assert_eq!(bound.fields()[1].field_id, 1001);
    assert!(bound.has_sequential_ids());
    assert_eq!(bound.highest_field_id(), Some(1001));
    assert!(!bound.is_unpartitioned());
    assert!(PartitionSpec::unpartition_spec().is_unpartitioned());
}

#[test]
fn g14_unbound_spec__continues_from_last_assigned_field_id() {
    // "Partition Evolution": in v2 metadata partition field ids are unique
    // across specs, so a new spec continues from the table's
    // `last-partition-id`.
    let unbound = UnboundPartitionSpec::builder()
        .add_partition_field(2, "ts_day", Transform::Day)
        .unwrap()
        .add_partition_field(1, "id_bucket", Transform::Bucket(8))
        .unwrap()
        .build();
    let bound = PartitionSpecBuilder::new_from_unbound(unbound, partition_schema())
        .unwrap()
        .with_last_assigned_field_id(1005)
        .with_spec_id(3)
        .build()
        .unwrap();
    assert_eq!(bound.spec_id(), 3);
    assert_eq!(bound.fields()[0].field_id, 1006);
    assert_eq!(bound.fields()[1].field_id, 1007);
    // Sequential ids start at 1000, so this spec is not sequential and would
    // not be valid in v1 metadata.
    assert!(!bound.has_sequential_ids());
}

#[test]
fn g14_unbound_spec__compatibility_ignores_ids() {
    // Two specs are compatible when their fields agree on order, name, source
    // column and transform, regardless of spec id or partition field id.
    let a = UnboundPartitionSpec::builder()
        .add_partition_field(2, "ts_day", Transform::Day)
        .unwrap()
        .build()
        .bind(partition_schema())
        .unwrap();
    let b = PartitionSpecBuilder::new_from_unbound(
        UnboundPartitionSpec::builder()
            .add_partition_field(2, "ts_day", Transform::Day)
            .unwrap()
            .build(),
        partition_schema(),
    )
    .unwrap()
    .with_last_assigned_field_id(1050)
    .with_spec_id(9)
    .build()
    .unwrap();
    assert_ne!(a.spec_id(), b.spec_id());
    assert_ne!(a.fields()[0].field_id, b.fields()[0].field_id);
    assert!(a.is_compatible_with(&b));
    assert!(b.is_compatible_with(&a));

    // A different transform, name or source column is not compatible.
    let c = UnboundPartitionSpec::builder()
        .add_partition_field(2, "ts_hour", Transform::Hour)
        .unwrap()
        .build()
        .bind(partition_schema())
        .unwrap();
    assert!(!a.is_compatible_with(&c));
    assert!(!a.is_compatible_with(&PartitionSpec::unpartition_spec()));
}

#[test]
fn g14_unbound_spec__bound_builder_takes_column_names() {
    // TEST_INTERFACE section 4.3: the bound builder is reached through
    // `PartitionSpec::builder(schema)` (or `PartitionSpecBuilder::new`) and its
    // `add_partition_field` takes the source **column name**, where the unbound
    // builder's takes the source field id. Both reach the same spec.
    let schema = partition_schema();
    let by_name = PartitionSpec::builder(schema.clone())
        .with_spec_id(2)
        .add_partition_field("ts", "ts_day", Transform::Day)
        .unwrap()
        .add_partition_field("id", "id_bucket", Transform::Bucket(8))
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(by_name.spec_id(), 2);
    assert_eq!(by_name.fields().len(), 2);
    assert_eq!(by_name.fields()[0].source_id, 2);
    assert_eq!(by_name.fields()[0].name, "ts_day");
    assert_eq!(by_name.fields()[0].field_id, 1000);
    assert_eq!(by_name.fields()[1].source_id, 1);
    assert_eq!(by_name.fields()[1].field_id, 1001);

    // A column that is not in the schema cannot be named.
    assert!(PartitionSpec::builder(schema.clone())
        .add_partition_field("nope", "nope_day", Transform::Day)
        .is_err());

    // `PartitionSpecBuilder::new` plus the unbound-field setters produce the
    // same spec as the name-taking setter.
    let by_field = PartitionSpecBuilder::new(schema)
        .with_spec_id(2)
        .add_unbound_field(UnboundPartitionField {
            source_id: 2,
            field_id: None,
            name: "ts_day".to_string(),
            transform: Transform::Day,
        })
        .unwrap()
        .add_unbound_fields(vec![UnboundPartitionField {
            source_id: 1,
            field_id: None,
            name: "id_bucket".to_string(),
            transform: Transform::Bucket(8),
        }])
        .unwrap()
        .build()
        .unwrap();
    assert_eq!(by_field, by_name);
}

#[test]
fn g14_unbound_spec__json_shapes_round_trip() {
    // Appendix C "Partition Specs": a bound spec serializes `spec-id` and
    // `fields` with `source-id`, `field-id`, `name` and `transform`.
    let bound = UnboundPartitionSpec::builder()
        .add_partition_field(2, "ts_day", Transform::Day)
        .unwrap()
        .build()
        .bind(partition_schema())
        .unwrap()
        .with_spec_id(4);
    let value = serde_json::to_value(&bound).unwrap();
    assert_eq!(value["spec-id"], 4);
    assert_eq!(value["fields"][0]["source-id"], 2);
    assert_eq!(value["fields"][0]["field-id"], 1000);
    assert_eq!(value["fields"][0]["name"], "ts_day");
    assert_eq!(value["fields"][0]["transform"], "day");
    let back: PartitionSpec = serde_json::from_value(value).unwrap();
    assert_eq!(back, bound);

    // `into_unbound()` keeps the spec id and each field's assigned field id
    // (TEST_INTERFACE section 4.3); only `source-id` and `spec-id` are asserted
    // here, because the unbound document's `field-id` is optional.
    let unbound = bound.clone().into_unbound();
    assert_eq!(unbound.spec_id(), Some(4));
    let value = serde_json::to_value(&unbound).unwrap();
    assert_eq!(value["spec-id"], 4);
    assert_eq!(value["fields"][0]["source-id"], 2);
    let back: UnboundPartitionSpec = serde_json::from_value(value).unwrap();
    assert_eq!(back, unbound);
}

// ---------------------------------------------------------------------------
// g15_spec_validation — spec.md "Partition Evolution" rejections
// ---------------------------------------------------------------------------

#[test]
fn g15_spec_validation__redundant_transform_on_one_source_column() {
    // "Partition Evolution": a spec may not contain two partition fields that
    // share a source column and a transform dedup name. `day` and `hour` share
    // one dedup name, `year` and `month` share another.
    assert_eq!(Transform::Day.dedup_name(), Transform::Hour.dedup_name());
    assert!(UnboundPartitionSpec::builder()
        .add_partition_field(2, "ts_day", Transform::Day)
        .unwrap()
        .add_partition_field(2, "ts_hour", Transform::Hour)
        .is_err());
    assert!(UnboundPartitionSpec::builder()
        .add_partition_field(2, "ts_year", Transform::Year)
        .unwrap()
        .add_partition_field(2, "ts_month", Transform::Month)
        .is_err());
    // Different source columns are fine.
    assert!(UnboundPartitionSpec::builder()
        .add_partition_field(2, "ts_day", Transform::Day)
        .unwrap()
        .add_partition_field(1, "id_bucket", Transform::Bucket(4))
        .is_ok());
}

#[test]
fn g15_spec_validation__names_must_be_unique_and_non_empty() {
    assert!(UnboundPartitionSpec::builder()
        .add_partition_field(2, "p", Transform::Day)
        .unwrap()
        .add_partition_field(1, "p", Transform::Bucket(4))
        .is_err());
    assert!(UnboundPartitionSpec::builder()
        .add_partition_field(2, "", Transform::Day)
        .is_err());
}

#[test]
fn g15_spec_validation__partition_field_ids_must_be_unique() {
    assert!(UnboundPartitionSpec::builder()
        .add_partition_fields(vec![
            UnboundPartitionField {
                source_id: 2,
                field_id: Some(1000),
                name: "a".to_string(),
                transform: Transform::Day,
            },
            UnboundPartitionField {
                source_id: 1,
                field_id: Some(1000),
                name: "b".to_string(),
                transform: Transform::Bucket(4),
            },
        ])
        .is_err());
}

#[test]
fn g15_spec_validation__binding_checks_the_schema() {
    let schema = partition_schema();
    // The source column must exist.
    assert!(UnboundPartitionSpec::builder()
        .add_partition_field(99, "x", Transform::Identity)
        .unwrap()
        .build()
        .bind(schema.clone())
        .is_err());
    // "Partition Transforms": the transform must be defined for the source
    // column's type — `year` is not defined over `string`.
    assert!(UnboundPartitionSpec::builder()
        .add_partition_field(3, "name_year", Transform::Year)
        .unwrap()
        .build()
        .bind(schema.clone())
        .is_err());
    // A partition name may reuse a schema column name only for an identity
    // transform of that same column.
    assert!(UnboundPartitionSpec::builder()
        .add_partition_field(2, "name", Transform::Day)
        .unwrap()
        .build()
        .bind(schema.clone())
        .is_err());
    assert!(UnboundPartitionSpec::builder()
        .add_partition_field(3, "name", Transform::Identity)
        .unwrap()
        .build()
        .bind(schema)
        .is_ok());
}

// ---------------------------------------------------------------------------
// g17_statistics — spec.md "Table Metadata Fields" (statistics,
// partition-statistics) and TEST_INTERFACE sections 4.1, 4.2, 4.4 and 4.6
// ---------------------------------------------------------------------------

fn statistics_file(snapshot_id: i64) -> StatisticsFile {
    StatisticsFile {
        snapshot_id,
        statistics_path: format!("file:///warehouse/db/orders/metadata/stats-{snapshot_id}.puffin"),
        file_size_in_bytes: 4096,
        file_footer_size_in_bytes: 128,
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

fn partition_statistics_file(snapshot_id: i64) -> PartitionStatisticsFile {
    PartitionStatisticsFile {
        snapshot_id,
        statistics_path: format!("file:///warehouse/db/orders/metadata/pstats-{snapshot_id}.parquet"),
        file_size_in_bytes: 2048,
    }
}

#[test]
fn g17_statistics__builder_records_and_removes_files() {
    // "Table Metadata Fields": `statistics` and `partition-statistics` are
    // per-snapshot lists of statistics files, keyed by snapshot id.
    let md = fresh_metadata();
    let result = md
        .into_builder(None)
        .set_statistics(statistics_file(3051))
        .set_partition_statistics(partition_statistics_file(3051))
        .build()
        .unwrap();
    let md = result.metadata;
    assert_eq!(md.statistics_iter().count(), 1);
    assert_eq!(md.partition_statistics_iter().count(), 1);
    assert_eq!(
        md.statistics_for_snapshot(3051).unwrap().file_size_in_bytes,
        4096
    );
    assert_eq!(
        md.statistics_for_snapshot(3051).unwrap().blob_metadata[0].r#type,
        "apache-datasketches-theta-v1"
    );
    assert_eq!(
        md.partition_statistics_for_snapshot(3051)
            .unwrap()
            .file_size_in_bytes,
        2048
    );
    assert!(md.statistics_for_snapshot(9999).is_none());
    assert!(md.partition_statistics_for_snapshot(9999).is_none());

    // Setting the entry for a snapshot again replaces it.
    let mut replacement = statistics_file(3051);
    replacement.file_size_in_bytes = 8192;
    let md = md
        .into_builder(None)
        .set_statistics(replacement)
        .build()
        .unwrap()
        .metadata;
    assert_eq!(md.statistics_iter().count(), 1);
    assert_eq!(
        md.statistics_for_snapshot(3051).unwrap().file_size_in_bytes,
        8192
    );

    // Removing drops the entry; removing one that is absent records nothing.
    let removed = md
        .into_builder(None)
        .remove_statistics(3051)
        .remove_partition_statistics(3051)
        .build()
        .unwrap();
    assert_eq!(removed.metadata.statistics_iter().count(), 0);
    assert_eq!(removed.metadata.partition_statistics_iter().count(), 0);
    assert!(removed.metadata.statistics_for_snapshot(3051).is_none());
    let noop = removed
        .metadata
        .into_builder(None)
        .remove_statistics(3051)
        .remove_partition_statistics(3051)
        .build()
        .unwrap();
    assert!(
        noop.changes.is_empty(),
        "removing an absent statistics file must record nothing: {:?}",
        change_actions(&noop.changes)
    );
}

#[test]
fn g17_statistics__update_documents_round_trip_and_apply() {
    // TEST_INTERFACE section 4.4: the four statistics updates carry
    // `statistics` / `partition-statistics` / `snapshot-id` under kebab-case
    // action tags, and `apply` routes each to the matching builder method.
    let cases: Vec<(TableUpdate, &str)> = vec![
        (
            TableUpdate::SetStatistics {
                statistics: statistics_file(3051),
            },
            "set-statistics",
        ),
        (
            TableUpdate::RemoveStatistics { snapshot_id: 3051 },
            "remove-statistics",
        ),
        (
            TableUpdate::SetPartitionStatistics {
                partition_statistics: partition_statistics_file(3051),
            },
            "set-partition-statistics",
        ),
        (
            TableUpdate::RemovePartitionStatistics { snapshot_id: 3051 },
            "remove-partition-statistics",
        ),
    ];
    for (update, action) in &cases {
        let value = serde_json::to_value(update).unwrap();
        assert_eq!(value["action"], *action, "action tag of {update:?}");
        let back: TableUpdate = serde_json::from_value(value).unwrap();
        assert_eq!(&back, update, "round trip of {action}");
    }

    let value = serde_json::to_value(&cases[0].0).unwrap();
    assert_eq!(value["statistics"]["snapshot-id"], 3051);
    assert_eq!(value["statistics"]["file-size-in-bytes"], 4096);
    assert_eq!(value["statistics"]["file-footer-size-in-bytes"], 128);
    assert_eq!(value["statistics"]["blob-metadata"][0]["sequence-number"], 1);
    let value = serde_json::to_value(&cases[1].0).unwrap();
    assert_eq!(value["snapshot-id"], 3051);
    let value = serde_json::to_value(&cases[2].0).unwrap();
    assert_eq!(value["partition-statistics"]["file-size-in-bytes"], 2048);

    // Applying the updates reaches the same state as calling the builder.
    let mut builder = fresh_metadata().into_builder(None);
    for (update, _) in cases {
        builder = update.apply(builder).unwrap();
    }
    let md = builder.build().unwrap().metadata;
    assert_eq!(md.statistics_iter().count(), 0);
    assert_eq!(md.partition_statistics_iter().count(), 0);
}

// ---------------------------------------------------------------------------
// g16_reassign — spec.md "Schema Evolution" (field-id assignment for a new
// table)
// ---------------------------------------------------------------------------

fn nested_source_schema() -> Schema {
    Schema::builder()
        .with_schema_id(5)
        .with_identifier_field_ids(vec![10])
        .with_fields(vec![
            NestedField::required(10, "id", Type::Primitive(PrimitiveType::Long)).into(),
            NestedField::optional(
                20,
                "location",
                Type::Struct(StructType::new(vec![
                    NestedField::required(21, "lat", Type::Primitive(PrimitiveType::Double)).into(),
                    NestedField::required(22, "lon", Type::Primitive(PrimitiveType::Double)).into(),
                ])),
            )
            .into(),
        ])
        .build()
        .unwrap()
}

#[test]
fn g16_reassign__nested_field_ids_start_at_one() {
    let creation = TableCreation::builder()
        .name("places".to_string())
        .location("file:///warehouse/db/places".to_string())
        .schema(nested_source_schema())
        .build();
    let md = TableMetadataBuilder::from_table_creation(creation)
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    let schema = md.current_schema();
    // "Schema Evolution": ids are assigned depth-first from 1 and names are
    // preserved.
    assert_eq!(schema.field_id_by_name("id"), Some(1));
    assert_eq!(schema.field_id_by_name("location"), Some(2));
    assert_eq!(schema.field_id_by_name("location.lat"), Some(3));
    assert_eq!(schema.field_id_by_name("location.lon"), Some(4));
    assert_eq!(schema.highest_field_id(), 4);
    assert_eq!(field(&md, "last-column-id"), serde_json::json!(4));
    // A new table's schema is the current schema with id 0.
    assert_eq!(schema.schema_id(), 0);
    assert_eq!(md.current_schema_id(), 0);
}

#[test]
fn g16_reassign__identifier_field_ids_follow_the_columns() {
    // "Identifier Field IDs": the identifier fields must still name the same
    // columns after reassignment.
    let creation = TableCreation::builder()
        .name("places".to_string())
        .location("file:///warehouse/db/places".to_string())
        .schema(nested_source_schema())
        .build();
    let md = TableMetadataBuilder::from_table_creation(creation)
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    let ids: Vec<i32> = md.current_schema().identifier_field_ids().collect();
    assert_eq!(ids, vec![1]);
    assert_eq!(md.current_schema().field_by_id(1).unwrap().name, "id");
}

#[test]
fn g16_reassign__partition_and_sort_sources_follow_the_columns() {
    // The partition spec and sort order supplied at creation reference the
    // original field ids; after reassignment they must reference the new ids of
    // the same columns.
    let creation = TableCreation::builder()
        .name("places".to_string())
        .location("file:///warehouse/db/places".to_string())
        .schema(nested_source_schema())
        .partition_spec(
            UnboundPartitionSpec::builder()
                .add_partition_field(10, "id_bucket", Transform::Bucket(4))
                .unwrap()
                .build(),
        )
        .sort_order(
            SortOrder::builder()
                .with_sort_field(
                    SortField::builder()
                        .source_id(10)
                        .transform(Transform::Identity)
                        .direction(SortDirection::Descending)
                        .null_order(NullOrder::Last)
                        .build(),
                )
                .build_unbound()
                .unwrap(),
        )
        .build();
    let md = TableMetadataBuilder::from_table_creation(creation)
        .unwrap()
        .build()
        .unwrap()
        .metadata;
    let spec = md.default_partition_spec();
    assert_eq!(spec.fields().len(), 1);
    assert_eq!(spec.fields()[0].source_id, 1);
    assert_eq!(spec.fields()[0].field_id, 1000);
    assert_eq!(spec.fields()[0].transform, Transform::Bucket(4));
    assert_eq!(field(&md, "last-partition-id"), serde_json::json!(1000));

    let order = md.default_sort_order();
    assert_eq!(order.fields.len(), 1);
    assert_eq!(order.fields[0].source_id, 1);
    assert_eq!(order.fields[0].direction, SortDirection::Descending);
    assert_eq!(order.fields[0].null_order, NullOrder::Last);
}

#[test]
fn g16_reassign__creation_rejects_a_spec_that_does_not_match_the_schema() {
    let creation = TableCreation::builder()
        .name("places".to_string())
        .location("file:///warehouse/db/places".to_string())
        .schema(nested_source_schema())
        .partition_spec(
            UnboundPartitionSpec::builder()
                .add_partition_field(999, "nope", Transform::Identity)
                .unwrap()
                .build(),
        )
        .build();
    assert!(TableMetadataBuilder::from_table_creation(creation).is_err());
}

// ---------------------------------------------------------------------------
// Additional case for milestone 03, from TEST_INTERFACE.md section 4.3.
// ---------------------------------------------------------------------------

/// TEST_INTERFACE.md 4.3: *"`into_unbound()` also keeps each field's assigned
/// partition field id: `PartitionField { field_id: 1000, .. }` becomes
/// `UnboundPartitionField { field_id: Some(1000), .. }`"*, and *"Binding
/// carries the spec id across … `into_unbound()` keeps the bound spec's
/// `spec_id`, so it comes back as `Some(spec_id)`."* Re-binding the result must
/// therefore reproduce the same ids rather than assign fresh ones.
#[test]
fn g14_unbound_spec__into_unbound_keeps_the_assigned_field_ids() {
    let bound = UnboundPartitionSpec::builder()
        .with_spec_id(7)
        .add_partition_field(2, "ts_day", Transform::Day)
        .unwrap()
        .add_partition_field(1, "id_bucket", Transform::Bucket(8))
        .unwrap()
        .build()
        .bind(partition_schema())
        .unwrap();
    assert_eq!(bound.spec_id(), 7);
    assert_eq!(bound.fields()[0].field_id, 1000);
    assert_eq!(bound.fields()[1].field_id, 1001);

    let unbound = bound.clone().into_unbound();
    assert_eq!(unbound.spec_id(), Some(7));
    assert_eq!(unbound.fields()[0].field_id, Some(1000));
    assert_eq!(unbound.fields()[1].field_id, Some(1001));

    let rebound = unbound.bind(partition_schema()).unwrap();
    assert_eq!(rebound.spec_id(), 7);
    assert_eq!(rebound.fields(), bound.fields());
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
