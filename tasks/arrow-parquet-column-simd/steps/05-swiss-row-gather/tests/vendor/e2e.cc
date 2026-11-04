// SPDX-License-Identifier: Apache-2.0
//
// e2e -- pass/fail end-to-end guard for the arrow-parquet-column-simd
// task: run the "real pipeline" the seed names (read a Parquet file into Arrow
// arrays, then hash-join the result), print wall milliseconds and a digest of
// the join output.  A timing win cannot be bought with wrong answers.
//
//   e2e          # write, read, join; best-of-5 wall ms for read+join
//   e2e <reps>   # best-of-<reps>
//
// stdout is exactly two lines, for a shell to parse: "e2e_ms <float>" and
// "e2e_digest <16 hex chars>".  Per-rep times and the row count go to stderr.
// The file is written and read by the same tree, in one process, so the only
// inputs are the seeds and sizes below: no data files, no absolute paths.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#include <arrow/acero/api.h>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/util/bit_util.h>
#include <arrow/util/thread_pool.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <parquet/properties.h>

namespace {

// 400k rows at 64k rows per row group is ~6.3 row groups; 4k rows per data page
// means the page decoder is entered ~100x per column per row group.
constexpr int64_t kLeftRows = 400'000;
constexpr int64_t kRightRows = 50'000;
constexpr int64_t kRowGroupRows = 64'000;   // rows per parquet row group
constexpr int64_t kPageRows = 4'096;        // max rows per parquet data page
constexpr int64_t kReadBatchRows = 65'536;  // rows per record batch handed to acero
constexpr uint64_t kLeftSeed = 0x5eed1234ULL;
constexpr uint64_t kRightSeed = 0x0badc0deULL;
constexpr int64_t kMaxStrLen = 40;  // `s` is 0..40 bytes

// The timed work is folded in here and the digest is read out of here, so
// neither the read nor the join can be dropped as dead code.
volatile double g_sink_d = 0.0;
volatile uint64_t g_sink_u = 0;

// splitmix64: fixed seed -> fixed stream, no rand(), no global state.
inline uint64_t splitmix64(uint64_t& state) {
  uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

arrow::Result<std::shared_ptr<arrow::Table>> BuildLeftTable() {
  arrow::Int64Builder kb;
  arrow::DoubleBuilder vb;
  arrow::StringBuilder sb;
  arrow::Int32Builder nb;
  ARROW_RETURN_NOT_OK(kb.Reserve(kLeftRows));
  ARROW_RETURN_NOT_OK(vb.Reserve(kLeftRows));
  // Reserve() counts elements, ReserveData() counts value bytes. UnsafeAppend
  // needs both, and the byte side is the one that silently overflows.
  ARROW_RETURN_NOT_OK(sb.Reserve(kLeftRows));
  ARROW_RETURN_NOT_OK(sb.ReserveData(kLeftRows * kMaxStrLen + kMaxStrLen));
  ARROW_RETURN_NOT_OK(nb.Reserve(kLeftRows));

  uint64_t st = kLeftSeed;
  int64_t key = 0;
  char buf[kMaxStrLen];
  for (int64_t i = 0; i < kLeftRows; ++i) {
    const uint64_t r = splitmix64(st);
    // `k` walks upward, standing still on every 4th row: ~1.33 rows per distinct
    // key, so an inner join has real matches without being one-to-one.
    if ((r % 4) != 0) ++key;
    kb.UnsafeAppend(key);
    // A double with a wide, varied exponent, so byte-stream-split has something
    // to split rather than a run of near-identical bytes.
    vb.UnsafeAppend(static_cast<double>(static_cast<int64_t>(r >> 17)) * 3.7e-11);
    const int64_t len = static_cast<int64_t>((r >> 8) % (kMaxStrLen + 1));
    for (int64_t j = 0; j < len; ++j) buf[j] = static_cast<char>('a' + ((r >> (11 + (j % 20))) % 26));
    sb.UnsafeAppend(buf, len);
    // ~10% nulls, so the definition levels are not all-ones.
    if (((r >> 32) % 10) == 0) nb.UnsafeAppendNull();
    else nb.UnsafeAppend(static_cast<int32_t>(r >> 40));
  }
  auto schema = arrow::schema({arrow::field("k", arrow::int64(), false),
                               arrow::field("v", arrow::float64(), false),
                               arrow::field("s", arrow::utf8(), false),
                               arrow::field("n", arrow::int32(), true)});
  ARROW_ASSIGN_OR_RAISE(auto k_arr, kb.Finish());
  ARROW_ASSIGN_OR_RAISE(auto v_arr, vb.Finish());
  ARROW_ASSIGN_OR_RAISE(auto s_arr, sb.Finish());
  ARROW_ASSIGN_OR_RAISE(auto n_arr, nb.Finish());
  return arrow::Table::Make(schema, {k_arr, v_arr, s_arr, n_arr});
}

// The small side of the join.  Keys are drawn from the left key column itself,
// so every probe row has somewhere to land.
arrow::Result<std::shared_ptr<arrow::Table>> BuildRightTable(
    const std::shared_ptr<arrow::Table>& left) {
  // The generator hands back one chunk per column, so chunk 0 is the key column.
  const auto& keys = static_cast<const arrow::Int64Array&>(*left->column(0)->chunk(0));
  arrow::Int64Builder kb;
  arrow::DoubleBuilder vb;
  ARROW_RETURN_NOT_OK(kb.Reserve(kRightRows));
  ARROW_RETURN_NOT_OK(vb.Reserve(kRightRows));
  uint64_t st = kRightSeed;
  for (int64_t i = 0; i < kRightRows; ++i) {
    const uint64_t r = splitmix64(st);
    kb.UnsafeAppend(keys.Value(static_cast<int64_t>(r % kLeftRows)));
    vb.UnsafeAppend(static_cast<double>(static_cast<int64_t>(r >> 19)) * 1.9e-9);
  }
  auto schema = arrow::schema({arrow::field("k", arrow::int64(), false),
                               arrow::field("rv", arrow::float64(), false)});
  ARROW_ASSIGN_OR_RAISE(auto k_arr, kb.Finish());
  ARROW_ASSIGN_OR_RAISE(auto v_arr, vb.Finish());
  return arrow::Table::Make(schema, {k_arr, v_arr});
}

// INNER join on `k`; the left side keeps every column it has, the right side
// lends only its payload, so the output schema is k,v,s,n,rv with no clashes.
// This tree's acero registry spells the hash-join factory "hashjoin" (no
// underscore) and the table source "table_source".
arrow::Result<std::shared_ptr<arrow::Table>> RunJoin(
    const std::shared_ptr<arrow::Table>& left, const std::shared_ptr<arrow::Table>& right) {
  using arrow::acero::Declaration;
  using arrow::acero::HashJoinNodeOptions;
  using arrow::acero::JoinType;
  using arrow::acero::TableSourceNodeOptions;
  Declaration lhs{"table_source", TableSourceNodeOptions(left, kReadBatchRows)};
  Declaration rhs{"table_source", TableSourceNodeOptions(right, kReadBatchRows)};
  HashJoinNodeOptions opts(
      JoinType::INNER, /*in_left_keys=*/{arrow::FieldRef("k")},
      /*in_right_keys=*/{arrow::FieldRef("k")},
      /*in_left_output=*/{arrow::FieldRef("k"), arrow::FieldRef("v"), arrow::FieldRef("s"),
                          arrow::FieldRef("n")},
      /*in_right_output=*/{arrow::FieldRef("rv")});
  Declaration join{"hashjoin", {std::move(lhs), std::move(rhs)}, opts};
  return arrow::acero::DeclarationToTable(std::move(join), /*use_threads=*/true);
}

struct Fnv1a64 {
  uint64_t h = 0xcbf29ce484222325ULL;
  static constexpr uint64_t kPrime = 0x100000001b3ULL;
  void Update(const void* data, size_t n) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= kPrime; }
  }
  void UpdateU8(uint8_t b) { h ^= b; h *= kPrime; }
  void UpdateU64(uint64_t v) { Update(&v, sizeof(v)); }
  void UpdateI64(int64_t v) { Update(&v, sizeof(v)); }
  void UpdateI32(int32_t v) { Update(&v, sizeof(v)); }
};

// FNV-1a over one value of one row.  Logical, not buffer-image: nothing here
// reads an Arrow allocation's padding bytes, so the result depends on the data
// alone.  A null slot contributes its null flag and nothing else -- the value
// buffer under a null is not part of the logical data, and two correct trees
// need not leave the same bytes there.
inline void HashValueAt(Fnv1a64& h, const arrow::Array& arr, int64_t i) {
  if (arr.IsNull(i)) {
    h.UpdateU8(0);
    return;
  }
  h.UpdateU8(1);
  switch (arr.type_id()) {
    case arrow::Type::INT64:
      h.UpdateI64(arr.data()->GetValues<int64_t>(1)[i]);
      break;
    case arrow::Type::INT32:
      h.UpdateI32(arr.data()->GetValues<int32_t>(1)[i]);
      break;
    case arrow::Type::DOUBLE: {
      const double v = arr.data()->GetValues<double>(1)[i];
      h.Update(&v, sizeof(v));
      break;
    }
    case arrow::Type::STRING: {
      const auto sv = static_cast<const arrow::StringArray&>(arr).GetView(i);
      h.UpdateI64(static_cast<int64_t>(sv.size()));
      h.Update(sv.data(), sv.size());
      break;
    }
    default:
      break;  // unreachable: DigestTable rejects unhandled types up front
  }
}

// Digest of the join output.
//
// The probe side of an acero hash join runs on the plan's thread pool, so the
// ORDER of the output rows is not reproducible run to run -- only the multiset
// of rows is, and a digest over concatenated buffers would change every run.
// So: FNV-1a each row on its own, then fold the row hashes together with a
// commutative combine (rotate-xor plus sum).  Order-insensitive, still sensitive
// to every value, every null flag and the row count, duplicates included (the
// sum accumulates them; the xor cancels only where hash and rotation collide).
arrow::Result<uint64_t> DigestTable(const std::shared_ptr<arrow::Table>& table) {
  const int num_cols = static_cast<int>(table->num_columns());
  if (num_cols == 0) return uint64_t{0};

  Fnv1a64 schema_h;  // order-sensitive: names/types/positions are fixed
  schema_h.UpdateI64(table->num_rows());
  schema_h.UpdateI64(num_cols);
  for (int c = 0; c < num_cols; ++c) {
    const auto& field = table->field(c);
    const std::string name = field->name();
    const std::string type_str = field->type()->ToString();
    const auto tid = field->type()->id();
    schema_h.UpdateI64(c);
    schema_h.UpdateI64(static_cast<int64_t>(name.size()));
    schema_h.Update(name.data(), name.size());
    schema_h.UpdateI64(static_cast<int64_t>(type_str.size()));
    schema_h.Update(type_str.data(), type_str.size());
    if (tid != arrow::Type::INT64 && tid != arrow::Type::INT32 &&
        tid != arrow::Type::DOUBLE && tid != arrow::Type::STRING) {
      return arrow::Status::NotImplemented("e2e digest: unhandled column type ",
                                           type_str);
    }
  }

  uint64_t acc_xor = 0, acc_sum = 0, rows = 0;
  std::vector<const arrow::Array*> cols(static_cast<size_t>(num_cols));
  for (int k = 0; k < table->column(0)->num_chunks(); ++k) {
    const int64_t len = table->column(0)->chunk(k)->length();
    for (int c = 0; c < num_cols; ++c) {
      cols[static_cast<size_t>(c)] = table->column(c)->chunk(k).get();
      if (cols[static_cast<size_t>(c)]->length() != len) {
        return arrow::Status::Invalid("e2e digest: columns are chunked differently");
      }
    }
    for (int64_t i = 0; i < len; ++i) {
      Fnv1a64 row_h;
      for (int c = 0; c < num_cols; ++c) {
        row_h.UpdateI32(c);
        HashValueAt(row_h, *cols[static_cast<size_t>(c)], i);
      }
      const uint64_t rh = row_h.h;
      const unsigned s = static_cast<unsigned>(rh & 63);
      acc_xor ^= (rh << s) | (rh >> ((64 - s) & 63));
      acc_sum += rh;
      ++rows;
    }
  }
  Fnv1a64 out;
  out.UpdateU64(schema_h.h);
  out.UpdateU64(acc_xor);
  out.UpdateU64(acc_sum);
  out.UpdateU64(rows);
  return out.h;
}

struct RepOutcome {
  double ms = 0.0;       // read + join, the number the guard scores
  double ms_read = 0.0;  // split out only to say which half moved
  double ms_join = 0.0;
  std::shared_ptr<arrow::Table> joined;
};

// Steps 3 (read every row group and every column into arrays we then touch) and
// 4 (hash-join through acero) -- and only those -- are inside the clock.
arrow::Result<RepOutcome> RunRep(const std::string& path,
                                 const std::shared_ptr<arrow::Table>& right) {
  RepOutcome outcome;
  const auto t0 = std::chrono::steady_clock::now();

  ARROW_ASSIGN_OR_RAISE(auto infile, arrow::io::ReadableFile::Open(path));
  ARROW_ASSIGN_OR_RAISE(auto reader,
                        parquet::arrow::OpenFile(infile, arrow::default_memory_pool()));
  reader->set_use_threads(true);
  reader->set_batch_size(kReadBatchRows);
  ARROW_ASSIGN_OR_RAISE(auto batch_reader, reader->GetRecordBatchReader());
  ARROW_ASSIGN_OR_RAISE(auto left, arrow::Table::FromRecordBatchReader(batch_reader.get()));

  // Touch the decoded arrays: one pass over the double column of what we just
  // read back, folded into a sink that cannot be discarded.
  const auto& vcol = left->column(1);
  double acc = 0.0;
  for (int k = 0; k < vcol->num_chunks(); ++k) {
    const auto& chunk = static_cast<const arrow::DoubleArray&>(*vcol->chunk(k));
    const double* p = chunk.raw_values();
    for (int64_t i = 0; i < chunk.length(); ++i) acc += p[i];
  }
  g_sink_d = g_sink_d + acc;

  const auto t1 = std::chrono::steady_clock::now();
  ARROW_ASSIGN_OR_RAISE(outcome.joined, RunJoin(left, right));
  const auto t2 = std::chrono::steady_clock::now();

  using std::chrono::duration;
  outcome.ms_read = duration<double, std::milli>(t1 - t0).count();
  outcome.ms_join = duration<double, std::milli>(t2 - t1).count();
  outcome.ms = outcome.ms_read + outcome.ms_join;
  return outcome;
}

// mkstemp under the temp directory, so concurrent runs on a shared host cannot
// collide and nothing absolute is baked into the source.
std::string MakeTempPath() {
  const std::string tmpl =
      (std::filesystem::temp_directory_path() / "arrow_e2e_XXXXXX.parquet").string();
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  const int fd = ::mkstemp(buf.data());
  if (fd >= 0) ::close(fd);
  return std::string(buf.data());
}

struct TempFileGuard {
  std::string path;
  ~TempFileGuard() {
    if (!path.empty()) ::remove(path.c_str());
  }
};

void Fail(const char* what, const arrow::Status& st) {
  std::fprintf(stderr, "e2e: %s: %s\n", what, st.ToString().c_str());
}

}  // namespace

int main(int argc, char** argv) {
  int64_t reps = 5;
  if (argc > 1) reps = std::strtoll(argv[1], nullptr, 10);
  if (reps < 1) reps = 1;

  // The guard runs pinned to two cores.  Arrow's global pool would otherwise be
  // sized from the 16 CPUs the host reports and oversubscribe those two, which
  // is most of what turns a ~70ms rep into a 250ms one.
  (void)arrow::SetCpuThreadPoolCapacity(2);

  // Build and write (untimed) ------------------------------------------------
  auto left = BuildLeftTable().ValueOrDie();
  auto right = BuildRightTable(left).ValueOrDie();

  TempFileGuard guard;
  guard.path = MakeTempPath();
  if (guard.path.empty()) {
    std::fprintf(stderr, "e2e: could not create a temp file\n");
    return 1;
  }

  // default_writer_properties() is the base; override only what the guard needs
  // to be meaningful: no compression and no dictionary, so the pages hold raw
  // encodings, plus 4k-row pages.  The double column is pinned to
  // BYTE_STREAM_SPLIT on top of that -- this tree's writer never chooses that
  // encoding by itself (ColumnWriter::Make falls back to PLAIN), so without this
  // one line the byte-stream-split decoder is never entered at all.  See
  // E2E_NOTES.md.
  auto builder = parquet::WriterProperties::Builder(*parquet::default_writer_properties());
  builder.compression(parquet::Compression::UNCOMPRESSED);
  builder.disable_dictionary();
  builder.max_rows_per_page(kPageRows);
  builder.encoding("v", parquet::Encoding::BYTE_STREAM_SPLIT);

  auto sink = arrow::io::FileOutputStream::Open(guard.path).ValueOrDie();
  auto st = parquet::arrow::WriteTable(*left, arrow::default_memory_pool(), sink,
                                       /*chunk_size=*/kRowGroupRows, builder.build());
  if (!st.ok()) {
    Fail("write", st);
    return 1;
  }
  st = sink->Close();
  if (!st.ok()) {
    Fail("close", st);
    return 1;
  }

  // Read + join (timed) ------------------------------------------------------
  std::vector<double> times;
  times.reserve(static_cast<size_t>(reps));
  std::shared_ptr<arrow::Table> joined;
  for (int64_t i = 0; i < reps; ++i) {
    auto outcome = RunRep(guard.path, right);
    if (!outcome.ok()) {
      Fail("rep", outcome.status());
      return 1;
    }
    times.push_back(outcome->ms);
    std::fprintf(stderr, "e2e_rep %lld %f %f %f\n", static_cast<long long>(i), outcome->ms,
                 outcome->ms_read, outcome->ms_join);
    joined = outcome->joined;
  }

  // Digest (untimed) ---------------------------------------------------------
  auto digest = DigestTable(joined);
  if (!digest.ok()) {
    Fail("digest", digest.status());
    return 1;
  }
  g_sink_u = g_sink_u + *digest;
  std::fprintf(stderr, "e2e_joined_rows %lld\n",
               static_cast<long long>(joined->num_rows()));

  // Exactly two stdout lines, for the shell.
  std::printf("e2e_ms %f\n", *std::min_element(times.begin(), times.end()));
  std::printf("e2e_digest %016llx\n", static_cast<unsigned long long>(*digest));
  return (g_sink_d == 123456789.0 && g_sink_u == 1) ? 2 : 0;  // keeps sinks live
}
