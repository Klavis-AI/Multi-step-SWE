# TEST_INTERFACE.md — milestone 02 (scan planning)

This is the public contract the hidden verifier holds you to. Everything the
verifier names — crate identity, build command, module paths, exported symbols,
signatures, formats crossing the boundary, and the pinned crates it links
against — appears here or in a section of the specification corpus named from
`instruction.md`'s reading list. The verifier may combine these in ways this
document does not enumerate; it will not use an interface this document does
not name.

Section 8 lists what is **out of scope** at this milestone. Read it: several
things the Iceberg specification defines are deliberately outside this
milestone's contract.

## 1. Crate identity and build contract

| Item | Value |
| --- | --- |
| Package name | `iceberg` |
| Package version | `0.3.0` |
| Manifest | `/app/Cargo.toml` |
| Lockfile | `/app/Cargo.lock` — present and used; the verifier reuses it |
| Crate root | `/app/src/lib.rs` |
| Crate type | Rust library (`lib`), edition `2021` |
| Build command | `cargo build` run in `/app` |
| Toolchain | the `rustc`/`cargo` in the image (1.88.0); no nightly features |

The verifier runs `cargo build` in `/app` first. Then it creates a
**throw-away driver crate outside `/app`** that declares
`iceberg = { path = "/app" }` and compiles its own integration tests against
your crate. Your `Cargo.toml` is never modified, and no test of yours is ever
run or read as evidence.

The driver crate copies `/app/Cargo.lock` as its own starting resolution, so a
dependency you pin is the version the drivers link against. Keep `Cargo.lock`
in the workspace and keep it consistent with `Cargo.toml`. If your lockfile
cannot resolve the driver crate's own dependencies, the verifier retries once
with its own resolution instead of failing you — a rescue path, not a
guarantee: after it your pins are no longer what the drivers link against.

The verifier image carries a pre-populated Cargo registry cache for the
dependency set this milestone's own resolution needs, plus the driver crate's.
That is not necessarily the set the workspace arrives with: the workspace
carries the previous milestone's lockfile until you change it. A dependency
you add is fetched when the checker runs, so prefer the crates already resolved in
`Cargo.lock`.

## 2. Workspace layout

```text
/app/
  Cargo.toml            your crate manifest       (required, name = "iceberg")
  Cargo.lock            your resolved graph       (required)
  src/                  your implementation       (layout is yours)
  LICENSE  NOTICE       Apache-2.0 notices carried forward
  iceberg-spec/         the normative specification corpus (read-only)
  TEST_INTERFACE.md     this file
  WORKSPACE_HANDOFF.md  what to preserve when this milestone ends
  timer.sh              advisory countdown for this milestone's budget
```

Internal module organisation under `src/` is entirely yours **except** for the
module paths this document names in section 4: those are part of the public
API and the drivers use them verbatim (`iceberg::spec::Schema`, and so on).

## 3. Crates the verifier links against

The driver crate declares exactly these dependencies and no others:

```toml
iceberg     = { path = "/app" }
serde_json  = "1"
tempfile    = "3"
tokio       = { version = "1", features = ["rt-multi-thread", "macros"] }
uuid        = "1"
chrono      = "0.4"
```

Consequences you must design for:

- Types from `uuid` (`uuid::Uuid`) and `chrono` (`chrono::DateTime<chrono::Utc>`)
  cross the API boundary, so your crate must depend on **semver-compatible**
  versions of those crates (`uuid` 1.x, `chrono` 0.4.x). A different major
  version makes the types distinct and the drivers will not compile.
- Serialisation and deserialisation go through `serde`: every type this
  document marks *serde* must implement `serde::Serialize` and
  `serde::Deserialize` such that `serde_json` produces and accepts the JSON
  the specification defines.
- Async entry points are driven by `tokio`. Any `async fn` this document names
  must be executable on a plain tokio runtime, single- or multi-threaded; do
  not require a particular executor or flavour to be installed by the caller.
- The drivers use `tempfile::TempDir` for scratch directories and address the
  files inside them through your `FileIO`, so the local-filesystem scheme must
  accept plain absolute paths such as `/tmp/xyz/manifest.avro`.
- The drivers unwrap your `Result` and format the failure with `{}` and `{:?}`,
  so `Error` must implement `std::fmt::Debug` and `std::fmt::Display`. Where
  section 4 annotates a type with standard traits (`Debug`, `Clone`,
  `PartialEq`, …), those impls are required as well. Section 8 excludes the
  **text** `Debug` and `Display` produce; it never excludes the impls
  themselves.

## 4. Public API required at this milestone

Milestone 1's `spec`, `io` and crate-root exports stay required and are
re-exercised by the gate. This milestone adds five public modules and changes
several milestone-1 signatures (section 5).

```rust
pub mod spec;      // milestone 1, extended below
pub mod io;        // milestone 1, extended below
pub mod expr;      // predicates, references, binding
pub mod transform; // transform functions over single values
pub mod scan;      // table scan planning
pub mod table;     // Table, StaticTable
pub mod arrow;     // Iceberg <-> Arrow schema conversion
pub use error::{Error, ErrorKind, Result};
```

**Standard traits on the new value types.** The drivers compare values with
`assert_eq!`/`assert_ne!`, format them into failure messages and clone them, so
every *value* type **sections 4.1 to 4.9** name must implement
`std::fmt::Debug`, `Clone` and `PartialEq` on top of whatever traits are listed
beside it. (Milestone 1 already required the same three on its own value types,
which section 4.0 restates.) Those types are
`Datum`, `PrimitiveLiteral`, `Reference`, `Predicate`, `BoundPredicate`,
`PredicateOperator`, `PartitionField`, `UnboundPartitionSpec`,
`UnboundPartitionField`, `ManifestFile`, `FieldSummary`, `DataFile`,
`ViewMetadata`, `ViewVersion`, `ViewVersionLog`, `ViewFormatVersion`,
`ViewRepresentations`, `ViewRepresentation` and `SqlViewRepresentation`. The
builders, the writers, the `io` handles, `Table` and `StaticTable` need none of
them. Section 8 excludes the *text* `Debug` and `Display` produce; it never
excludes the impls.

**Three signatures below name crates section 3 does not give the drivers** —
`serde_bytes::ByteBuf` (`Datum::to_bytes`), `arrow_array::ArrayRef`
(`TransformFunction::transform`) and `bytes::Bytes` (`InputFile::read`,
`OutputFile::write`). The drivers never name those types: they read a
`ByteBuf` through `AsRef<[u8]>`, they hand the value `InputFile::read` returns
straight back to `OutputFile::write` without naming it, and they never call
`transform` at all (section 8). Which version of those crates you depend on is
therefore not part of the contract — only that the signatures accept and return
values carrying the byte-level forms the specification defines.

### 4.0 Inherited milestone-1 surface the drivers still name

Everything in `spec` — milestone 1's types and the ones added below — is
re-exported at `iceberg::spec::<Name>`. The drivers write `use
iceberg::spec::*;` and then name these types unqualified, so each must be
reachable from that one path whichever file defines it.

Milestone 1's contract still holds in full and the gate re-exercises it,
including its requirement that every milestone-1 *value* type implement
`std::fmt::Debug`, `Clone` and `PartialEq`. This subsection restates only the
inherited elements the drivers name **directly**, so that this document alone
names every symbol they compile against. Section 5 lists the milestone-1 forms
that **change** at this milestone; where section 5 and this subsection could be
read as disagreeing, section 5 is the new form.

```rust
// iceberg::spec — the type system
pub enum Type { Primitive(PrimitiveType), Struct(StructType), List(ListType), Map(MapType) }  // serde, per Appendix C
pub enum PrimitiveType {
    Boolean, Int, Long, Float, Double, Date, Time, Timestamp, Timestamptz,
    String, Uuid, Fixed(u64), Binary, Decimal { precision: u32, scale: u32 },
}
pub type NestedFieldRef = std::sync::Arc<NestedField>;
pub struct NestedField {
    pub id: i32,
    pub name: String,
    pub required: bool,
    pub field_type: Box<Type>,          // compare through `.as_ref()`
    pub doc: Option<String>,
    // initial_default / write_default may be present; out of scope (milestone 1)
}
impl NestedField {
    pub fn required(id: i32, name: impl ToString, field_type: Type) -> Self;
    pub fn optional(id: i32, name: impl ToString, field_type: Type) -> Self;
}
// NestedField: Into<NestedFieldRef> — the drivers write `NestedField::required(..).into()`
impl StructType {
    pub fn new(fields: Vec<NestedFieldRef>) -> Self;
    pub fn fields(&self) -> &[NestedFieldRef];
}

// iceberg::spec — schemas
pub type SchemaRef = std::sync::Arc<Schema>;
impl Schema {
    pub fn builder() -> SchemaBuilder;
    pub fn schema_id(&self) -> i32;
    pub fn as_struct(&self) -> &StructType;
    // indexes EVERY field, nested ones included: a list element, a map key or
    // value and a struct member must all be findable by their own id, not just
    // the top-level fields.
    pub fn field_by_id(&self, field_id: i32) -> Option<&NestedFieldRef>;
    pub fn field_by_name(&self, field_name: &str) -> Option<&NestedFieldRef>;
    // the two lookups below both use dotted nested paths, e.g. `"location.lat"`
    pub fn field_id_by_name(&self, name: &str) -> Option<i32>;
    pub fn name_by_field_id(&self, field_id: i32) -> Option<&str>;
}
impl SchemaBuilder {                    // consuming; setters in any order, build() validates
    pub fn with_schema_id(self, schema_id: i32) -> Self;
    pub fn with_fields(self, fields: impl IntoIterator<Item = NestedFieldRef>) -> Self;
    pub fn with_identifier_field_ids(self, ids: impl IntoIterator<Item = i32>) -> Self;
    pub fn build(self) -> Result<Schema>;
}

// iceberg::spec — table metadata, snapshots, sort orders
pub enum FormatVersion { V1 = 1, V2 = 2 }
pub type PartitionSpecRef = std::sync::Arc<PartitionSpec>;
pub type SortOrderRef     = std::sync::Arc<SortOrder>;
pub type SnapshotRef      = std::sync::Arc<Snapshot>;
pub struct TableMetadata { /* private */ }      // serde; accepts both v1 and v2 documents
impl TableMetadata {
    pub fn format_version(&self) -> FormatVersion;
    pub fn current_schema(&self) -> &SchemaRef;
    pub fn default_partition_spec(&self) -> Option<&PartitionSpecRef>;
    pub fn current_snapshot(&self) -> Option<&SnapshotRef>;
    pub fn default_sort_order(&self) -> Option<&SortOrderRef>;
    pub fn sort_order_by_id(&self, sort_order_id: i64) -> Option<&SortOrderRef>;
}
impl Snapshot {
    pub fn snapshot_id(&self) -> i64;
    pub fn sequence_number(&self) -> i64;
}
pub enum Transform { Identity, Bucket(u32), Truncate(u32), Year, Month, Day, Hour, Void, Unknown }
pub enum SortDirection { Ascending, Descending }
pub enum NullOrder { First, Last }
pub struct SortField {
    pub source_id: i32,
    pub transform: Transform,
    pub direction: SortDirection,
    pub null_order: NullOrder,
}
pub struct SortOrder { pub order_id: i64, pub fields: Vec<SortField> }
impl SortOrder { pub fn is_unsorted(&self) -> bool; }

// iceberg::spec — values
impl Literal {
    pub fn long<T: Into<i64>>(t: T) -> Self;
    pub fn try_from_json(value: serde_json::Value, data_type: &Type) -> Result<Option<Self>>;
    pub fn try_into_json(self, r#type: &Type) -> Result<serde_json::Value>;
}
impl Struct { pub fn empty() -> Self; }

// iceberg::spec — manifests and manifest lists
pub enum ManifestContentType { Data = 0, Deletes = 1 }
pub enum ManifestStatus { Existing = 0, Added = 1, Deleted = 2 }
pub enum DataContentType { Data = 0, PositionDeletes = 1, EqualityDeletes = 2 }
pub enum DataFileFormat { Avro, Orc, Parquet }
pub type ManifestEntryRef = std::sync::Arc<ManifestEntry>;
impl Manifest {
    pub fn new(metadata: ManifestMetadata, entries: Vec<ManifestEntry>) -> Self;
    pub fn parse_avro(bs: &[u8]) -> Result<Self>;
    pub fn entries(&self) -> &[ManifestEntryRef];
}
// `ManifestMetadata::builder()` — typed, consuming, all five setters required:
//   .schema(Schema).schema_id(i32).partition_spec(PartitionSpec)
//   .content(ManifestContentType).format_version(FormatVersion)
//   .build()   // -> ManifestMetadata
impl ManifestEntry {
    pub fn is_alive(&self) -> bool;         // false for status `Deleted` (section 4.6)
    pub fn content_type(&self) -> DataContentType;
    pub fn file_path(&self) -> &str;
}
impl ManifestWriter {
    pub fn new(output: OutputFile, snapshot_id: i64, key_metadata: Vec<u8>) -> Self;
    pub async fn write(self, manifest: Manifest) -> Result<ManifestFile>;   // section 5
}
impl ManifestList {
    pub fn parse_with_version(
        bs: &[u8],
        version: FormatVersion,
        partition_type_provider: impl Fn(i32) -> Result<Option<StructType>>,
    ) -> Result<ManifestList>;
}
impl ManifestListWriter {
    pub fn v1(output_file: OutputFile, snapshot_id: i64, parent_snapshot_id: i64) -> Self;
    pub fn v2(output_file: OutputFile, snapshot_id: i64, parent_snapshot_id: i64,
              sequence_number: i64) -> Self;
    pub async fn close(self) -> Result<()>;
}
pub struct ManifestFile {           // renamed from ManifestListEntry (section 5);
    pub manifest_path: String,      // public fields, struct-literal constructible
    pub manifest_length: i64,
    pub partition_spec_id: i32,
    pub content: ManifestContentType,
    pub sequence_number: i64,
    pub min_sequence_number: i64,
    pub added_snapshot_id: i64,
    pub added_files_count: Option<u32>,      // renamed (section 5)
    pub existing_files_count: Option<u32>,   // renamed (section 5)
    pub deleted_files_count: Option<u32>,    // renamed (section 5)
    pub added_rows_count: Option<u64>,
    pub existing_rows_count: Option<u64>,
    pub deleted_rows_count: Option<u64>,
    pub partitions: Vec<FieldSummary>,
    pub key_metadata: Vec<u8>,
}

// iceberg::io
impl FileIOBuilder {
    pub fn new(scheme_str: impl ToString) -> Self;
    pub fn new_fs_io() -> Self;
    pub fn build(self) -> Result<FileIO>;
}
impl FileIO {                                       // Clone
    pub fn new_input(&self, path: impl AsRef<str>) -> Result<InputFile>;
    pub fn new_output(&self, path: impl AsRef<str>) -> Result<OutputFile>;
    pub async fn is_exist(&self, path: impl AsRef<str>) -> Result<bool>;
    pub async fn delete(&self, path: impl AsRef<str>) -> Result<()>;
}
impl InputFile  { pub fn location(&self) -> &str; }
impl OutputFile { pub fn location(&self) -> &str; }
```

The rest of milestone 1's surface — the remainder of `Type`, `Schema`,
`TableMetadata`, `Snapshot`, `Literal` and the Avro file-level obligations — is
unchanged, still required, and exercised by the gate; it is not restated here.

### 4.1 `Datum` — "Primitive Types", Appendix D

`Datum` is the typed single value that predicates, bounds and transforms use.
It carries its `PrimitiveType` alongside the value.

```rust
// iceberg::spec
pub struct Datum { /* private */ }   // Clone + PartialEq + Eq + Hash + PartialOrd + Display
impl Datum {
    pub fn bool<T: Into<bool>>(t: T) -> Self;
    pub fn int<T: Into<i32>>(t: T) -> Self;
    pub fn long<T: Into<i64>>(t: T) -> Self;
    pub fn float<T: Into<f32>>(t: T) -> Self;
    pub fn double<T: Into<f64>>(t: T) -> Self;
    pub fn date(days: i32) -> Self;
    pub fn date_from_str<S: AsRef<str>>(s: S) -> Result<Self>;          // "YYYY-MM-DD"
    pub fn date_from_ymd(year: i32, month: u32, day: u32) -> Result<Self>;
    pub fn time_from_str<S: AsRef<str>>(s: S) -> Result<Self>;          // "HH:MM:SS[.ffffff]"
    pub fn timestamp_from_str<S: AsRef<str>>(s: S) -> Result<Self>;     // ISO-8601, no zone
    pub fn timestamptz_from_str<S: AsRef<str>>(s: S) -> Result<Self>;   // RFC 3339 with offset
    pub fn string<S: ToString>(s: S) -> Self;
    pub fn uuid(uuid: uuid::Uuid) -> Self;
    pub fn uuid_from_str<S: AsRef<str>>(s: S) -> Result<Self>;
    pub fn fixed<I: IntoIterator<Item = u8>>(input: I) -> Self;
    pub fn binary<I: IntoIterator<Item = u8>>(input: I) -> Self;
    pub fn decimal_from_str<S: AsRef<str>>(s: S) -> Result<Self>;
    pub fn to_bytes(&self) -> serde_bytes::ByteBuf;   // Appendix D binary form
    pub fn try_from_bytes(bytes: &[u8], data_type: PrimitiveType) -> Result<Self>;
    pub fn literal(&self) -> &PrimitiveLiteral;
    pub fn data_type(&self) -> &PrimitiveType;
    pub fn to(self, target: &Type) -> Result<Datum>;  // type conversion, e.g. int -> date
}
```

`PrimitiveLiteral` variants the drivers pattern-match: `Boolean(bool)`,
`Int(i32)`, `Long(i64)`, `String(String)`, `Binary(Vec<u8>)`, `Int128(i128)`
(decimals, carried unscaled), `UInt128(u128)`. Which variant a temporal value
carries follows Appendix D's in-memory forms: a `date` carries
`Int(days from the 1970-01-01 epoch)`, and a `time`, `timestamp` or
`timestamptz` carries `Long(microseconds)`. `to_bytes` follows Appendix D:
little-endian integers and floats, `0x00`/`0x01` for booleans, raw UTF-8 for
strings, big-endian two's complement unscaled bytes for decimals, days from the
epoch for dates.

`PartialOrd` compares two `Datum`s of the **same** primitive type by value;
comparison across primitive types is out of scope.

`to` converts between a value and a compatible target type — `int` to `date`
(the day count denotes that date) and a type to itself. A conversion this
milestone does not define, such as `string` to `long`, is an error; in
particular `int` to `long` is **not** required to succeed.

`Literal` (milestone 1) remains for values stored *inside* a `Struct`; `Datum`
is what bounds, predicates and transforms use. See section 5.

### 4.2 Transform functions — Appendix B, "Bucket/Truncate Transform Details"

```rust
pub trait TransformFunction: Send {
    fn transform(&self, input: ArrayRef) -> Result<ArrayRef>;
    fn transform_literal(&self, input: &Datum) -> Result<Option<Datum>>;
    fn transform_literal_result(&self, input: &Datum) -> Result<Datum>;  // Err when None
}
pub type BoxedTransformFunction = Box<dyn TransformFunction>;
pub fn create_transform_function(transform: &Transform) -> Result<BoxedTransformFunction>;
```
(`iceberg::transform::*`.)

Required behaviour, all of it from the specification:

- **`bucket[N]`** — `(murmur3_x86_32(hash_input) & i32::MAX) % N`, with the
  hash inputs and **32-bit hash values** of "Appendix B: 32-bit Hash
  Requirements". The numbers below are those hash values `h`, **not** bucket
  outputs; a bucket follows from one by `(h & i32::MAX) % N` and is therefore
  always in `[0, N)`:
  `int 34 → 2017239379`, `long 34 → 2017239379`,
  `string "iceberg" → 1210000089`,
  `uuid f79c3e09-677c-4bbd-a479-3f349cb785e7 → 1488055340`,
  `date 2017-11-16 → -653330422`, `time 22:31:08 → -662762989`,
  `timestamp 2017-11-16T22:31:08 → -2047944441`,
  `decimal 14.20 → -500754589`, `binary/fixed 00 01 02 03 → -188683207`.
  The result is always a `Datum::int`.
- **`truncate[W]`** — `v - (((v % W) + W) % W)` for `int` and `long`
  (`W=10`: `1 → 0`, `-1 → -10`); the first `L` Unicode code points for
  `string` (`L=3`: `"iceberg" → "ice"`); and for `decimal`, the same integer
  rule over the unscaled value (`W=50`, scale 2: `10.65 → 10.50`, i.e.
  unscaled `1065 → 1050`). Truncation of a `binary` value is **not** required
  at this milestone (section 8).
- **`year`/`month`/`day`/`hour`** — years from 1970, months from 1970-01-01,
  days from 1970-01-01 and hours from 1970-01-01T00:00:00, returning a
  `Datum::int`. Each one accepts exactly the source types the "Partition
  Transforms" table lists for it: `year`, `month` and `day` over `date`,
  `timestamp` and `timestamptz`, and `hour` over `timestamp` and `timestamptz`
  **only** — `hour` of a `date` is an error. A `timestamptz`
  yields the same value as the equivalent UTC instant. The count is **signed**:
  an instant before the epoch gives a negative result, so `year`, `month` and
  `day` of `1969-12-31` are each `-1`.
- **`identity`** — returns the input unchanged. **`void`** — returns
  `Ok(None)` for every input. **`Transform::Unknown`** — `create_transform_function`
  returns `Err`.

### 4.3 Expressions and binding — "Scan Planning"

```rust
// iceberg::expr
pub struct Reference { /* private */ }
impl Reference {
    pub fn new(name: impl Into<String>) -> Self;
    pub fn name(&self) -> &str;
    // each consumes self and returns Predicate:
    pub fn less_than(self, datum: Datum) -> Predicate;
    pub fn less_than_or_equal_to(self, datum: Datum) -> Predicate;
    pub fn greater_than(self, datum: Datum) -> Predicate;
    pub fn greater_than_or_equal_to(self, datum: Datum) -> Predicate;
    pub fn equal_to(self, datum: Datum) -> Predicate;
    pub fn not_equal_to(self, datum: Datum) -> Predicate;
    pub fn starts_with(self, datum: Datum) -> Predicate;
    pub fn not_starts_with(self, datum: Datum) -> Predicate;
    pub fn is_null(self) -> Predicate;
    pub fn is_not_null(self) -> Predicate;
    pub fn is_nan(self) -> Predicate;
    pub fn is_not_nan(self) -> Predicate;
    pub fn is_in(self, literals: impl IntoIterator<Item = Datum>) -> Predicate;
    pub fn is_not_in(self, literals: impl IntoIterator<Item = Datum>) -> Predicate;
}

pub enum Predicate {
    AlwaysTrue, AlwaysFalse,
    And(LogicalExpression<Predicate, 2>),
    Or(LogicalExpression<Predicate, 2>),
    Not(LogicalExpression<Predicate, 1>),
    Unary(UnaryExpression<Reference>),
    Binary(BinaryExpression<Reference>),
    Set(SetExpression<Reference>),
}
impl Predicate {
    pub fn and(self, other: Predicate) -> Predicate;
    pub fn or(self, other: Predicate) -> Predicate;
    pub fn negate(self) -> Predicate;       // pushes NOT to the leaves (De Morgan)
    pub fn rewrite_not(self) -> Predicate;  // yields a NOT-free predicate
}
impl std::ops::Not for Predicate {}         // `!p` wraps in Predicate::Not
impl<T, const N: usize> LogicalExpression<T, N> { pub fn inputs(&self) -> [&T; N]; }

pub trait Bind {
    type Bound;
    fn bind(&self, schema: SchemaRef, case_sensitive: bool) -> Result<Self::Bound>;
}
impl Bind for Predicate { type Bound = BoundPredicate; }

pub enum BoundPredicate {
    AlwaysTrue, AlwaysFalse,
    And(LogicalExpression<BoundPredicate, 2>),
    Or(LogicalExpression<BoundPredicate, 2>),
    Not(LogicalExpression<BoundPredicate, 1>),
    Unary(UnaryExpression<BoundReference>),
    Binary(BinaryExpression<BoundReference>),
    Set(SetExpression<BoundReference>),
}
#[repr(u16)]
pub enum PredicateOperator {
    IsNull = 101, NotNull = 102, IsNan = 103, NotNan = 104,
    LessThan = 201, LessThanOrEq = 202, GreaterThan = 203, GreaterThanOrEq = 204,
    Eq = 205, NotEq = 206, StartsWith = 207, NotStartsWith = 208,
    In = 301, NotIn = 302,
}
impl PredicateOperator {
    pub fn is_unary(self) -> bool;
    pub fn is_binary(self) -> bool;
    pub fn is_set(self) -> bool;
    pub fn negate(self) -> PredicateOperator;
}
```

`Predicate` and `BoundPredicate` are `Clone + PartialEq + Display` and *serde*:
a predicate written out with `serde_json` and read back compares equal to the
original. The accessors on `UnaryExpression`, `BinaryExpression` and
`SetExpression` are crate-private: the drivers only match variants and compare
with `PartialEq`, so you need not expose them — and consequently
`BoundReference`'s own accessors are unreachable from the drivers and are out
of scope (section 8).

`and` and `or` keep their operands **in the order given** and perform no
simplification, deduplication or reordering: `a.and(b)` and `b.and(a)` are two
distinct `Predicate`s, and `LogicalExpression::inputs` yields them in that
order. Constant folding happens at `bind`, not at construction (the folding
rules are listed below); `negate` and `rewrite_not` restructure a predicate but
never commute an `And` or an `Or`.

`PredicateOperator::negate` returns the complementary operator, in both
directions, so it is an involution:

| operator | negation |
| --- | --- |
| `IsNull` | `NotNull` |
| `IsNan` | `NotNan` |
| `LessThan` | `GreaterThanOrEq` |
| `LessThanOrEq` | `GreaterThan` |
| `Eq` | `NotEq` |
| `StartsWith` | `NotStartsWith` |
| `In` | `NotIn` |

Binding rules the drivers assert, all from "Scan Planning" and
"Schemas and Data Types":

- a reference naming no column is an error;
- `case_sensitive = false` resolves a differently-cased column name, and the
  bound reference keeps the *reference's* spelling, so a case-insensitive bind
  is not `==` to the exactly-cased one;
- a binary literal is converted to the field's type (`Datum::to`); a literal
  that cannot convert is an error;
- `IsNan`/`NotNan` against a non-floating column is an error;
- `IsNull` against a **required** field folds to `AlwaysFalse` and `NotNull`
  folds to `AlwaysTrue`, because a required field is never null;
- `And`/`Or` fold their constant operands, and so does `Not`: binding
  `Not(AlwaysTrue)` yields `AlwaysFalse` and `Not(AlwaysFalse)` yields
  `AlwaysTrue`;
- a one-element `In`/`NotIn` collapses to `Eq`/`NotEq`; an empty `In` folds to
  `AlwaysFalse` and an empty `NotIn` to `AlwaysTrue`;
- `rewrite_not` produces a predicate containing no `Not` node.

### 4.4 Partition specs, bound and unbound — "Partitioning", "Partition Evolution"

`PartitionSpec`'s fields become private and it gains a schema-aware builder.

```rust
// iceberg::spec
impl PartitionSpec {
    pub fn builder(schema: &Schema) -> PartitionSpecBuilder<'_>;
    pub fn spec_id(&self) -> i32;
    pub fn fields(&self) -> &[PartitionField];
    pub fn is_unpartitioned(&self) -> bool;
    pub fn partition_type(&self, schema: &Schema) -> Result<StructType>;
    pub fn to_unbound(self) -> UnboundPartitionSpec;
}
impl PartitionSpecBuilder<'_> {           // every method consumes self
    pub fn with_spec_id(self, spec_id: i32) -> Self;
    pub fn with_last_assigned_field_id(self, id: i32) -> Self;
    pub fn add_partition_field(self, source_name: impl AsRef<str>,
                               target_name: impl Into<String>,
                               transform: Transform) -> Result<Self>;
    pub fn add_unbound_field(self, field: UnboundPartitionField) -> Result<Self>;
    pub fn build(self) -> Result<PartitionSpec>;
}
impl UnboundPartitionSpec {
    pub fn builder() -> UnboundPartitionSpecBuilder;
    pub fn spec_id(&self) -> Option<i32>;
    pub fn fields(&self) -> &[UnboundPartitionField];
    pub fn bind(self, schema: &Schema) -> Result<PartitionSpec>;
}
impl UnboundPartitionSpecBuilder {         // every method consumes self
    pub fn with_spec_id(self, spec_id: i32) -> Self;
    pub fn add_partition_field(self, source_id: i32, target_name: impl ToString,
                               transform: Transform) -> Result<Self>;
    pub fn build(self) -> UnboundPartitionSpec;   // infallible
}
```

`partition_type(&schema)` derives the `StructType` of the partition tuple: one
field per partition-spec field, in spec order, whose `id` is the partition
field's **`field_id`**, whose `name` is its target **`name`**, and whose type is
`transform.result_type(source type)`. Every one of those fields is
**optional** (`required == false`), because "Partition Transforms" says all
transforms must return `null` for a `null` input, so no partition value can be
guaranteed non-null.

`PartitionField` keeps its public fields `source_id`, `field_id`, `name`,
`transform`. Auto-assigned partition field ids start at **1000** and increase
by one, per "Partition Evolution"; `with_last_assigned_field_id(n)` makes the
next auto-assigned id `n + 1`. `to_unbound` keeps each field's assigned
partition field id, so re-binding its result to the same schema reproduces the
same `field_id`s.

`UnboundPartitionField` is the unbound counterpart of a `PartitionField`. The
drivers never build one from parts: they take a field an `UnboundPartitionSpec`
already carries (`fields()`) and `Clone` it, so the struct's own field names
and visibility are not part of this milestone's contract (section 8) — only
that it is `Clone` and that `add_unbound_field` accepts it.

The bound builder must reject an unknown source column, a duplicate
partition-field name, and a transform the source type does not admit —
**on the call that adds the field**: `add_partition_field` and
`add_unbound_field` return `Err` themselves, rather than deferring the check to
`build()`.

### 4.5 Table and scan — "Scan Planning"

```rust
// iceberg::spec  (used throughout this section)
pub type TableMetadataRef = std::sync::Arc<TableMetadata>;

// iceberg::table
impl Table {
    pub fn builder() -> TableBuilder;   // typed builder, any setter order:
        // required: .file_io(FileIO).metadata(TableMetadata).identifier(TableIdent)
        // optional: .metadata_location(impl Into<String>) (default None)
        //           .readonly(bool)                       (default false)
        // .build() -> Table   (infallible: the required setters are checked at compile time)
    pub fn identifier(&self) -> &TableIdent;
    pub fn metadata(&self) -> &TableMetadata;
    pub fn metadata_ref(&self) -> TableMetadataRef;
    pub fn metadata_location(&self) -> Option<&str>;
    pub fn file_io(&self) -> &FileIO;
    pub fn readonly(&self) -> bool;
    pub fn scan(&self) -> TableScanBuilder<'_>;
}
pub struct StaticTable { /* private */ }
impl StaticTable {
    pub async fn from_metadata(metadata: TableMetadata, table_ident: TableIdent,
                               file_io: FileIO) -> Result<Self>;
    pub async fn from_metadata_file(metadata_file_path: &str, table_ident: TableIdent,
                                    file_io: FileIO) -> Result<Self>;
    pub fn scan(&self) -> TableScanBuilder<'_>;
    pub fn metadata(&self) -> TableMetadataRef;
    pub fn into_table(self) -> Table;
}
// iceberg::TableIdent (crate root, milestone 1)
impl TableIdent {
    pub fn from_strs(iter: impl IntoIterator<Item = impl ToString>) -> Result<TableIdent>;
}

// iceberg::scan
impl TableScanBuilder<'_> {               // every method consumes self
    pub fn with_batch_size(self, batch_size: Option<usize>) -> Self;
    pub fn with_case_sensitive(self, case_sensitive: bool) -> Self;
    pub fn with_filter(self, predicate: Predicate) -> Self;   // rewrite_not is applied
    pub fn select_all(self) -> Self;
    pub fn select(self, column_names: impl IntoIterator<Item = impl ToString>) -> Self;
    pub fn snapshot_id(self, snapshot_id: i64) -> Self;
    pub fn with_concurrency_limit(self, limit: usize) -> Self;
    pub fn build(self) -> Result<TableScan>;
}
impl TableScan {
    pub async fn plan_files(&self) -> Result<FileScanTaskStream>;
    pub fn column_names(&self) -> &[String];
    pub fn snapshot(&self) -> &SnapshotRef;
}
pub struct FileScanTask { /* private fields */ }   // opaque here; see section 8
```

`TableScanBuilder::build` must fail when: the table has no current snapshot, a
`snapshot_id` names no snapshot, a selected column is unknown, nested or
non-primitive, or the filter cannot be bound. `plan_files` resolves the
snapshot's manifest list before returning, so a missing manifest list is an
error from `plan_files` itself.

`FileScanTaskStream` is whatever asynchronous stream of `Result<FileScanTask>`
your `plan_files` returns. The drivers never name it, never poll it and never
drain it — they only check whether the call succeeded — so its concrete type is
yours to choose, and `FileScanTask` itself is out of scope here (section 8).

**Options and their defaults.** A fresh `TableScanBuilder` has selected **no**
columns, and an empty selection means *every* column — so `column_names()` is
empty for a default scan. `select` **replaces** any previous selection and
`column_names()` returns those names in the order they were given;
`select_all()` clears the selection, so `column_names()` is empty again after
`select([..]).select_all()`. The snapshot defaults to the table's current
snapshot, `case_sensitive` to `true`, `batch_size` to `None`, and the
concurrency limit to an implementation-chosen positive value. `with_filter`
applies `rewrite_not` to the predicate before storing it.

### 4.6 Manifest and manifest-list surface, extended

```rust
// iceberg::spec
impl Snapshot {
    pub fn manifest_list(&self) -> &str;          // renamed; see section 5
    pub async fn load_manifest_list(&self, file_io: &FileIO,
                                    table_metadata: &TableMetadata) -> Result<ManifestList>;
}
impl ManifestFile {   // renamed from ManifestListEntry; see section 5
    pub async fn load_manifest(&self, file_io: &FileIO) -> Result<Manifest>;
}
impl ManifestList {
    pub fn entries(&self) -> &[ManifestFile];
    pub fn consume_entries(self) -> impl IntoIterator<Item = ManifestFile>;
}
impl ManifestListWriter {
    pub fn add_manifests(&mut self, manifests: impl Iterator<Item = ManifestFile>) -> Result<()>;
}
pub struct FieldSummary {
    pub contains_null: bool,
    pub contains_nan: Option<bool>,
    pub lower_bound: Option<Datum>,
    pub upper_bound: Option<Datum>,
}
impl ManifestEntry {
    pub fn data_file(&self) -> &DataFile;
    pub fn file_format(&self) -> DataFileFormat;
}
// `ManifestEntry::builder()` keeps milestone 1's shape and gains three optional
// setters, so an entry that cannot inherit its sequence numbers can carry them:
//   ManifestEntry::builder().status(ManifestStatus)          // required
//       .snapshot_id(i64).sequence_number(i64).file_sequence_number(i64)
//       .data_file(DataFile)                                 // data_file required
//       .build()   // -> ManifestEntry
impl DataFile {
    pub fn lower_bounds(&self) -> &std::collections::HashMap<i32, Datum>;
    pub fn upper_bounds(&self) -> &std::collections::HashMap<i32, Datum>;
    pub fn value_counts(&self) -> &std::collections::HashMap<i32, u64>;
    pub fn null_value_counts(&self) -> &std::collections::HashMap<i32, u64>;
    pub fn partition(&self) -> &Struct;
    pub fn record_count(&self) -> u64;
    pub fn file_size_in_bytes(&self) -> u64;
}
impl Struct { pub fn iter(&self) -> impl Iterator<Item = Option<&Literal>>; }
impl FromIterator<Option<Literal>> for Struct {}
```

Observable obligations from "Manifest Lists" and "Sequence Number Inheritance":

- `ManifestWriter::write` returns a `ManifestFile` whose `sequence_number` is
  still unassigned; `ManifestListWriter::add_manifests` substitutes the
  snapshot's sequence number.
- The returned `partitions` vector has exactly one `FieldSummary` per
  partition-spec field, in spec order, aggregated from the data files' bounds
  and null counts keyed by the partition field's **`source_id`**.
  `contains_nan` is `Some(false)` when every contributing data file reported a
  zero `nan_value_counts` entry for that source column, and `None` when no
  contributing data file reported one at all.
- `added_files_count`, `existing_files_count` and `deleted_files_count` count
  the manifest's entries with status `Added`, `Existing` and `Deleted`, and
  `added_rows_count`, `existing_rows_count` and `deleted_rows_count` sum those
  entries' `record_count`s. A count with no contributing entry is `Some(0)`,
  not `None`.
- An entry with status `Deleted` or `Existing` must carry a `sequence_number`
  and a `file_sequence_number`, or `write` is an error.
- An entry with status `Deleted` is not alive.

### 4.7 Arrow schema conversion — "Column Projection"

```rust
// iceberg::arrow
pub fn schema_to_arrow_schema(schema: &Schema) -> Result<arrow_schema::Schema>;
pub fn arrow_schema_to_schema(schema: &arrow_schema::Schema) -> Result<Schema>;
```

Iceberg projects columns by **field id**, not by name ("Column Projection"),
so the ids must survive a conversion to Arrow in both directions. Every
converted Arrow field carries the field-id metadata key
**`"PARQUET:field_id"`**, including nested list-element and struct-member
fields; that key is given here, not in the specification corpus. A required Iceberg field maps to a non-nullable Arrow field. The
conversion round-trips `long`, `string`, `boolean`, `double`, `date`, `binary`,
lists and structs, preserving every field id.

The round trip is compared as a whole: the drivers assert
`arrow_schema_to_schema(&schema_to_arrow_schema(&s)?)?.as_struct() ==
s.as_struct()`, and `StructType` equality covers field **names** as well as ids,
types and requiredness — including the names of nested list-element and
struct-member fields. So the Arrow child field of a list must carry the Iceberg
element field's own name rather than a hard-coded Arrow convention, and
reconstructing an Iceberg list from Arrow must give the element field the same
name your `Type::List` deserialiser gives it for Appendix C's
`element`/`element-id` form.

The drivers touch the returned Arrow values only through inherent methods, so
your `arrow-schema` version is not part of the contract.

### 4.8 View metadata — `iceberg-spec/view-spec.md`

```rust
// iceberg::spec  (the whole view-metadata family, except NamespaceIdent)
pub type ViewVersionRef = std::sync::Arc<ViewVersion>;
// iceberg::NamespaceIdent (crate root, like TableIdent)
pub struct NamespaceIdent { /* private */ }   // + impl AsRef<Vec<String>>

pub struct ViewMetadata { /* private */ }     // Deserialize (Serialize is NOT required)
impl ViewMetadata {
    pub fn format_version(&self) -> ViewFormatVersion;
    pub fn uuid(&self) -> uuid::Uuid;
    pub fn location(&self) -> &str;
    pub fn current_version_id(&self) -> i32;
    pub fn versions(&self) -> impl Iterator<Item = &ViewVersionRef>;
    pub fn version_by_id(&self, version_id: i32) -> Option<&ViewVersionRef>;
    pub fn current_version(&self) -> &ViewVersionRef;
    pub fn schemas_iter(&self) -> impl Iterator<Item = &SchemaRef>;
    pub fn schema_by_id(&self, schema_id: i32) -> Option<&SchemaRef>;
    pub fn current_schema(&self) -> &SchemaRef;
    pub fn properties(&self) -> &std::collections::HashMap<String, String>;
    pub fn history(&self) -> &[ViewVersionLog];
}
pub enum ViewFormatVersion { V1 = 1 }         // serde_repr
impl ViewVersionLog { pub fn version_id(&self) -> i32; pub fn timestamp_ms(&self) -> i64; }
impl ViewVersion {
    pub fn version_id(&self) -> i32;
    pub fn schema_id(&self) -> i32;
    pub fn timestamp_ms(&self) -> i64;
    pub fn summary(&self) -> &std::collections::HashMap<String, String>;
    pub fn representations(&self) -> &ViewRepresentations;
    pub fn default_catalog(&self) -> Option<&String>;
    pub fn default_namespace(&self) -> &NamespaceIdent;
    pub fn schema(&self, view_metadata: &ViewMetadata) -> Result<SchemaRef>;
}
impl ViewRepresentations { pub fn len(&self) -> usize; pub fn iter(&self) -> impl Iterator<Item = &ViewRepresentation>; }
pub enum ViewRepresentation { Sql(SqlViewRepresentation) }   // serde tag field is "type", value "sql"
pub struct SqlViewRepresentation { pub sql: String, pub dialect: String }
```

JSON keys are the view specification's kebab-case names: `view-uuid`,
`format-version`, `current-version-id`, `version-log`, `default-catalog`,
`default-namespace`, `timestamp-ms`, `schema-id`. Deserialisation must reject a
`current-version-id` naming no listed version, a version whose `schema-id`
names no listed schema, and a `format-version` other than 1.

A `NamespaceIdent` is a namespace's dot-free segments and comes from the JSON
array of strings `default-namespace` holds. It has no accessor of its own: the
drivers read the segments through **`AsRef<Vec<String>>`**, as in
`version.default_namespace().as_ref() == &vec!["default".to_string()]`.

### 4.9 `FileIO` schemes — "File System Operations", PROFILE P3

```rust
impl FileIO { pub fn from_path(path: impl AsRef<str>) -> Result<FileIOBuilder>; }
impl InputFile {
    pub async fn read(&self) -> Result<bytes::Bytes>;
    pub async fn metadata(&self) -> Result<FileMetadata>;
}
impl OutputFile { pub async fn write(&self, bs: bytes::Bytes) -> Result<()>; }
pub struct FileMetadata { pub size: u64 }
```

Recognised scheme strings, all of which `FileIOBuilder::build` must accept:
`"file"` and `""` (local filesystem), `"memory"` (an in-process store, not a
directory on disk), `"s3"` and `"s3a"`. A scheme name outside the set your
crate supports must fail at `FileIOBuilder::build`; the drivers check that with
an invented scheme name, never with a real one.
A Google Cloud Storage backend (`"gs"`) is **not** required at this milestone
(section 8). Locations may be bare absolute paths (`/tmp/...`) or
scheme-prefixed (`file:///tmp/...`, `memory:/...`, `s3://bucket/...`).
`InputFile::location()` and `OutputFile::location()` report the location the
handle was created with, unchanged — a scheme prefix is echoed back, not
normalised away. Only the local filesystem is ever read or written
(PROFILE P3), so `s3`/`s3a` need only be constructible.

## 5. Migration from the previous milestone

Milestone 1's contract changes in the following ways. The new form is what is
required; the old form is not accepted.

| Milestone 1 | This milestone |
| --- | --- |
| `PartitionSpec { spec_id, fields }` public fields, struct-literal constructible | fields are private; use `spec_id()` and `fields()`, and construct with `PartitionSpec::builder(&schema)` |
| milestone 1 had no `PartitionSpec` builder at all | `PartitionSpec::builder(schema: &Schema)`, and every builder method consumes `self` |
| `DataFile::builder()` (typed builder), `.build()` returns `DataFile` | `DataFileBuilder::default()` (derive builder): setters take `&mut self` and return `&mut Self`, and `.build()` returns `Result<DataFile, DataFileBuilderError>` |
| `DataFile` bounds are `HashMap<i32, Literal>` | `HashMap<i32, Datum>` |
| `FieldSummary { lower_bound: Option<Literal>, .. }` | `Option<Datum>` |
| `ManifestListEntry` | renamed **`ManifestFile`** (same public fields, modulo the three renames below) |
| `ManifestListEntry::added_data_files_count` | `ManifestFile::added_files_count` |
| `existing_data_files_count`, `deleted_data_files_count` | `existing_files_count`, `deleted_files_count` |
| `ManifestWriter::write(..) -> Result<ManifestListEntry>` | `-> Result<ManifestFile>` |
| `ManifestListWriter::add_manifest_entries(&mut self, impl Iterator<Item = ManifestListEntry>)` | `add_manifests(&mut self, impl Iterator<Item = ManifestFile>)` |
| there was no `scan` module and no `Table`/`StaticTable` | `pub mod scan` and `pub mod table` (section 4.5) |
| `Snapshot::manifest_list_file_path() -> Result<&str>` | `Snapshot::manifest_list() -> &str` |
| `ManifestEntry::builder()` with `.status()` and `.data_file()` | the same builder, extended with the optional `.snapshot_id(i64)`, `.sequence_number(i64)` and `.file_sequence_number(i64)` setters (section 4.6) |
| `FileIO` has one backend | `FileIO` dispatches on the location's URI scheme |

`DataFileBuilder` requires only the columns the specification marks required:
`.content(DataContentType)`, `.file_path(String)`,
`.file_format(DataFileFormat)`, `.partition(Struct)`, `.record_count(u64)` and
`.file_size_in_bytes(u64)`. The remaining setters each name a column
"Manifests" marks *optional* and may be left unset — `build()` then yields an
empty map, an empty vector or `None` for it: `.column_sizes(HashMap<i32,u64>)`,
`.value_counts(HashMap<i32,u64>)`, `.null_value_counts(HashMap<i32,u64>)`,
`.nan_value_counts(HashMap<i32,u64>)`, `.lower_bounds(HashMap<i32,Datum>)`,
`.upper_bounds(HashMap<i32,Datum>)`, `.key_metadata(Vec<u8>)`,
`.split_offsets(Vec<i64>)`, `.equality_ids(Vec<i32>)` and
`.sort_order_id(i32)`. The drivers rely on that and set only the columns a
case is about. They unwrap the `Result` that `build()` returns, so
`DataFileBuilderError` implements `std::fmt::Debug`.

Unchanged from milestone 1 and still required in exactly the same form:
`ManifestList::parse_with_version` (three arguments), `ManifestMetadata::builder()`
with its five setters, `Manifest::{new, parse_avro, entries}`,
`ManifestListWriter::{v1, v2}`, `Transform::result_type`,
`Transform`'s `FromStr`/`Display`, `Schema`, `SortOrder`, `TableMetadata` and
`Literal`.
## 6. Formats crossing the boundary

- **Table metadata** is JSON, exactly as `iceberg-spec/spec.md` "Table Metadata",
  "Table Metadata Fields" and Appendix C define it. The drivers deserialise
  documents written by hand from those sections and re-serialise your value,
  comparing the documented field names and values. Field **order** in the
  emitted JSON is never asserted; field **names** and values are.
- **Schemas, partition specs and sort orders** are JSON per Appendix C.
- **Type names** are JSON strings per "Primitive Types" and Appendix C.
  Appendix C admits both `"decimal(9,2)"` and `"decimal(9, 2)"`; the drivers
  accept either spelling from you and require you to accept both.
- **Single values** (bounds, defaults, partition values) follow Appendix D —
  the binary form for bounds inside manifests, the JSON form inside metadata.
- **Manifests and manifest lists** are Avro object container files per
  "Manifests", "Manifest Lists" and Appendix A. The drivers check the Avro
  object-container magic `Obj\x01` and round-trip the file through your own
  reader. Where a milestone's section 4 says so, they also decode the
  uncompressed Avro header and check the writer schema and the file metadata
  it records.
- **Errors** are reported through your `Result` type. The drivers assert only
  that an operation **fails**; the error's variant, kind, message wording and
  source chain are never asserted.

## 7. Scope

- One **gate**: `cargo build` succeeds, one end-to-end path through this
  milestone's headline capability succeeds, and one end-to-end path through
  every milestone this milestone inherits still succeeds.

## 8. Out of scope at this milestone

Never in scope, at any milestone:

- Internal module layout below the paths section 4 names, private type and
  function names, file organisation under `src/`.
- Error message wording, error-variant identity, and the **text** that
  `Debug`/`Display` produce for any type this document does not explicitly name
  a format for. Whether those impls *exist* is not in this list: sections 3 and
  4 require them, and only what they print is out of scope.
- Iteration order the specification does not fix, allocation strategy, timing,
  memory use, throughput.
- Your own tests, benchmarks, examples, documentation and CI configuration.
- Any capability of the Iceberg specification not named in section 4 or in a
  specification section named by `instruction.md`'s reading list — in
  particular: catalogs other than the ones section 4 names, engine
  integrations, query execution beyond scan planning, and any format-version 3
  feature that section 4 does not itself name, unless this
  milestone's section 8 additions say otherwise.

Additionally, out of scope at **this** milestone:

- `TableMetadataBuilder` and the table-update / table-requirement algebra —
  milestone 3.
- Row-level delete files, delete vectors, Puffin, metadata inspection tables
  and the object cache — milestone 4.
- Transactions, catalogs and commit conflict resolution — milestone 5.
- Nanosecond timestamp types (`timestamp_ns`, `timestamptz_ns`), the `variant`,
  `geometry`, `geography` and `unknown` types, and every other
  format-version-3 addition.
- **The contents of the planned file-scan-task set.** The drivers assert that
  `TableScan::plan_files` succeeds for an unfiltered and a filtered scan, and
  that it fails when the snapshot's manifest list is missing. (A table with no
  current snapshot fails earlier still, at `TableScanBuilder::build` — section
  4.5.) They do not drain `FileScanTaskStream`, so neither the number of tasks
  nor which files a predicate pruned is in scope. **`FileScanTask` itself is
  entirely out of scope at this milestone**: its field names, its accessors and
  its trait impls alike. The drivers never name the type, never construct one
  and never observe one, so whether it is `Serialize`, `Deserialize` or
  `PartialEq` is out of scope here.
- **Expression visitors as public types.** `ManifestEvaluator`,
  `InclusiveMetricsEvaluator`, `InclusiveProjection` and `ExpressionEvaluator`
  need not be public at this milestone. What *is* in scope is the data they
  consume, read back through the public readers: a manifest list's
  `FieldSummary` bounds and `contains_null`, and a data file's `lower_bounds`,
  `upper_bounds`, `null_value_counts` and `value_counts` keyed by field id.
- **Which manifests and which data files a scan pruned.** Instruction section 1
  requires partition pruning, column-statistics pruning and inclusive
  projection of a predicate over a partition spec, and a scan that plans files
  must perform all three — but the *outcome* of the three is not observable
  here, so none of it is in scope: the evaluators need not be public (above) and
  the drivers cannot drain a `FileScanTaskStream` (above), so the check cannot
  observe a skipped manifest or a pruned file. What is in scope instead is that
  `plan_files` succeeds with and without a filter, that it fails when the
  manifest list is missing, and that the statistics all three consume are
  readable through the public readers.
- **Case-insensitive binding of a scan's row filter.** `Predicate::bind` with
  `case_sensitive = false` *is* in scope; whether `TableScanBuilder::with_case_sensitive(false)`
  propagates to the row filter is not.
- **Persistence of the `memory` `FileIO` scheme.** Its location handling and
  its failure to read an absent file are in scope; whether a write is visible to
  a later read through the same `FileIO` is not. Only the local filesystem is
  required to persist (PROFILE P3).
- **The result type of a truncated decimal.** The truncated *value* is in scope;
  whether the resulting datum carries the source decimal's scale is not.
- **`truncate` over a `binary` value.** The specification defines truncation of
  a binary value to its first `L` bytes; whether `transform_literal` accepts a
  `binary` input at all is out of scope at this milestone, in either direction.
- **`TransformFunction::transform` over an Arrow array.** Section 4.2 names the
  signature for completeness, but the drivers cannot build an `ArrayRef` — the
  dependency set of section 3 does not include `arrow-array` — so only
  `transform_literal` and `transform_literal_result` are in scope.
- **`UnboundPartitionField`'s own fields.** The drivers obtain one from
  `UnboundPartitionSpec::fields()` and clone it, so the struct's field names
  and visibility are out of scope — only that it is `Clone` and that
  `PartitionSpecBuilder::add_unbound_field` accepts it.
- **`BoundReference`'s accessors.** `UnaryExpression`, `BinaryExpression` and
  `SetExpression` keep their accessors crate-private (section 4.3), so no bound
  term is reachable from a `BoundPredicate` and nothing on `BoundReference` is
  in scope.
- **A Google Cloud Storage `FileIO` backend.** `"gs"` need not be a scheme
  `FileIOBuilder::build` accepts; the schemes it must accept are listed in
  section 4.9.
- **Serialising view metadata.** Parsing a view metadata document, its
  accessors, and its documented rejections are in scope; emitting one is not.
- Row lineage, table statistics, partition statistics, encryption and key
  metadata contents.

Section 4 of **this** milestone names no format-version-3 feature — deletion
vectors included — so the "deletion vectors aside, where section 4 names it"
exception in the *Never in scope, at any milestone* list above is empty here:
every format-version-3 addition is out of scope at this milestone.
