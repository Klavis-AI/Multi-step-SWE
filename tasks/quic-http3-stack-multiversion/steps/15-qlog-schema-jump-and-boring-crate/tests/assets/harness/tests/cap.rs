//! new-capability groups: what this milestone can do that the previous one could not
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `cap-curves-list` is `fn cap_curves_list()`. The
//! driver runs each one on its own with `--exact` and asserts that exactly one
//! test executed, so a renamed, `#[ignore]`d or accidentally deleted function
//! scores zero for that group rather than passing silently.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.
//!
//! Every group in this file is a **scored** group: under `reward = gate x
//! score` these carry the whole of the step's weight between them, and the
//! gate groups in `gate.rs` and `reg.rs` carry none of it.
//!
//! Two of them read a file the candidate wrote rather than calling it. That is
//! the T4 surface, and it is used here because this milestone's largest change
//! is a *format*: the qlog file schema moves from the `qlog_version`/`0.3`
//! shape this package pinned at milestone 06 to the IETF URI shape, and every
//! event name moves with it. A format change can only be observed in the
//! bytes. The third T4 group reads Cargo manifests, which is the one place
//! this package allows a group to look at a file the candidate authored, and
//! it is scored as layout and never as behaviour.

use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
// Cold `cargo build` of a probe crate against the candidate's `qlog` package.
// Measured against this step's Oracle tree at 24 crates and roughly 30 s; 90 s
// leaves room for a loaded machine and still fits inside the driver's per-case
// ceiling with the probe's own run budget to spare.
const LONG: Duration = Duration::from_secs(90);

// ---------------------------------------------------------------------------
// shared helpers
// ---------------------------------------------------------------------------

/// First `n` bytes of a byte string as lowercase hex, for error messages.
#[allow(dead_code)]
fn head(d: &[u8], n: usize) -> String {
    testkit::hex(&d[..d.len().min(n)])
}

/// Write a throwaway crate under `dir` whose only dependency is one package
/// from the candidate's workspace, given by path, and return its directory.
///
/// The crate carries `[workspace]` so it is a workspace of its own and is not
/// adopted by anything above it, and it lives outside the candidate's tree so
/// nothing the group does can leave a mark there.
fn write_probe(dir: &Path, probe_name: &str, dep: &str, main_rs: &str) -> PathBuf {
    let probe = dir.join(probe_name);
    std::fs::create_dir_all(probe.join("src")).expect("create probe crate");
    let dep_path = testkit::candidate_root().join(dep);
    assert!(
        dep_path.join("Cargo.toml").is_file(),
        "expected the candidate's `{dep}` package at {}; observed no Cargo.toml there",
        dep_path.display()
    );
    std::fs::write(
        probe.join("Cargo.toml"),
        format!(
            "[package]\nname = \"{probe_name}\"\nversion = \"0.0.0\"\nedition = \"2021\"\n\
             publish = false\n\n[workspace]\n\n[dependencies]\n\
             {dep} = {{ path = \"{}\" }}\n",
            dep_path.display()
        ),
    )
    .expect("write probe manifest");
    std::fs::write(probe.join("src/main.rs"), main_rs).expect("write probe main");
    probe
}

/// `cargo build --offline` a probe crate into `dir/target`, and return the
/// finished `Run` alongside the binary it should have produced.
fn build_probe(dir: &Path, probe: &Path, tag: &str) -> (testkit::Run, PathBuf) {
    let target = dir.join("target");
    let mut c = testkit::cmd("cargo");
    c.arg("build")
        .arg("--offline")
        .arg("--jobs")
        .arg("4")
        .arg("--manifest-path")
        .arg(probe.join("Cargo.toml"))
        .env("CARGO_TARGET_DIR", &target);
    let run = testkit::run(&mut c, LONG, dir, tag);
    let name = probe.file_name().and_then(|n| n.to_str()).expect("probe name");
    (run, target.join("debug").join(name))
}

/// Run a built probe binary and return its `key=value` output, failing with
/// everything it printed if it did not reach the end of `main`.
fn run_probe(dir: &Path, exe: &Path, tag: &str) -> (BTreeMap<String, String>, String) {
    let mut r = testkit::cmd(exe);
    let run = testkit::run(&mut r, SHORT, dir, tag);
    assert!(
        !run.timed_out,
        "expected {} to finish within {SHORT:?}; observed: still running at the deadline, \
         killed.\n--- its stdout ---\n{}\n--- its stderr ---\n{}",
        exe.display(),
        run.stdout,
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to exit 0; observed status {:?}.\n--- its stdout ---\n{}\n\
         --- its stderr ---\n{}",
        exe.display(),
        run.status,
        run.stdout,
        run.stderr
    );
    (testkit::kv(&run.stdout), run.output())
}

// --- the smallest JSON reader that can answer a qlog question --------------

/// A parsed JSON value. Enough of JSON to walk a qlog record; no more.
#[derive(Debug, Clone, PartialEq)]
enum Json {
    Null,
    Bool(bool),
    Num(f64),
    Str(String),
    Arr(Vec<Json>),
    Obj(BTreeMap<String, Json>),
}

impl Json {
    fn get(&self, key: &str) -> Option<&Json> {
        match self {
            Json::Obj(m) => m.get(key),
            _ => None,
        }
    }

    #[allow(dead_code)]
    fn path(&self, keys: &[&str]) -> Option<&Json> {
        let mut cur = self;
        for k in keys {
            cur = cur.get(k)?;
        }
        Some(cur)
    }

    fn as_str(&self) -> Option<&str> {
        match self {
            Json::Str(s) => Some(s),
            _ => None,
        }
    }

    fn as_arr(&self) -> Option<&[Json]> {
        match self {
            Json::Arr(v) => Some(v),
            _ => None,
        }
    }

    fn as_u64(&self) -> Option<u64> {
        match self {
            Json::Num(n) if *n >= 0.0 && n.fract() == 0.0 => Some(*n as u64),
            _ => None,
        }
    }

    fn keys(&self) -> BTreeSet<String> {
        match self {
            Json::Obj(m) => m.keys().cloned().collect(),
            _ => BTreeSet::new(),
        }
    }
}

fn json_parse(s: &str) -> Result<Json, String> {
    let b: Vec<char> = s.chars().collect();
    let mut i = 0usize;
    let v = json_value(&b, &mut i)?;
    json_ws(&b, &mut i);
    if i != b.len() {
        return Err(format!("trailing bytes at char {i}"));
    }
    Ok(v)
}

fn json_ws(b: &[char], i: &mut usize) {
    while *i < b.len() && b[*i].is_whitespace() {
        *i += 1;
    }
}

fn json_value(b: &[char], i: &mut usize) -> Result<Json, String> {
    json_ws(b, i);
    let c = *b.get(*i).ok_or("unexpected end of input")?;
    match c {
        '{' => {
            *i += 1;
            let mut m = BTreeMap::new();
            json_ws(b, i);
            if b.get(*i) == Some(&'}') {
                *i += 1;
                return Ok(Json::Obj(m));
            }
            loop {
                json_ws(b, i);
                let k = match json_value(b, i)? {
                    Json::Str(s) => s,
                    other => return Err(format!("object key is not a string: {other:?}")),
                };
                json_ws(b, i);
                if b.get(*i) != Some(&':') {
                    return Err(format!("expected ':' at char {i}"));
                }
                *i += 1;
                let v = json_value(b, i)?;
                m.insert(k, v);
                json_ws(b, i);
                match b.get(*i) {
                    Some(',') => *i += 1,
                    Some('}') => {
                        *i += 1;
                        return Ok(Json::Obj(m));
                    },
                    _ => return Err(format!("expected ',' or '}}' at char {i}")),
                }
            }
        },
        '[' => {
            *i += 1;
            let mut a = Vec::new();
            json_ws(b, i);
            if b.get(*i) == Some(&']') {
                *i += 1;
                return Ok(Json::Arr(a));
            }
            loop {
                a.push(json_value(b, i)?);
                json_ws(b, i);
                match b.get(*i) {
                    Some(',') => *i += 1,
                    Some(']') => {
                        *i += 1;
                        return Ok(Json::Arr(a));
                    },
                    _ => return Err(format!("expected ',' or ']' at char {i}")),
                }
            }
        },
        '"' => {
            *i += 1;
            let mut s = String::new();
            loop {
                let c = *b.get(*i).ok_or("unterminated string")?;
                *i += 1;
                match c {
                    '"' => return Ok(Json::Str(s)),
                    '\\' => {
                        let e = *b.get(*i).ok_or("unterminated escape")?;
                        *i += 1;
                        match e {
                            'n' => s.push('\n'),
                            't' => s.push('\t'),
                            'r' => s.push('\r'),
                            'b' => s.push('\u{8}'),
                            'f' => s.push('\u{c}'),
                            'u' => {
                                let hex: String = b[*i..(*i + 4).min(b.len())].iter().collect();
                                *i += 4;
                                let n = u32::from_str_radix(&hex, 16)
                                    .map_err(|_| format!("bad \\u escape {hex}"))?;
                                s.push(char::from_u32(n).unwrap_or('\u{fffd}'));
                            },
                            other => s.push(other),
                        }
                    },
                    other => s.push(other),
                }
            }
        },
        't' | 'f' | 'n' => {
            let rest: String = b[*i..(*i + 5).min(b.len())].iter().collect();
            if rest.starts_with("true") {
                *i += 4;
                Ok(Json::Bool(true))
            } else if rest.starts_with("false") {
                *i += 5;
                Ok(Json::Bool(false))
            } else if rest.starts_with("null") {
                *i += 4;
                Ok(Json::Null)
            } else {
                Err(format!("unknown literal at char {i}: {rest}"))
            }
        },
        _ => {
            let start = *i;
            while *i < b.len() && (b[*i].is_ascii_digit() || "+-.eE".contains(b[*i])) {
                *i += 1;
            }
            let t: String = b[start..*i].iter().collect();
            t.parse::<f64>().map(Json::Num).map_err(|_| format!("bad number {t:?}"))
        },
    }
}

/// Split a JSON Text Sequence into its records: each one is the byte `0x1e`,
/// then one JSON value, then `\n` (RFC 7464).
fn jsonseq_records(raw: &[u8], what: &str) -> Vec<Json> {
    assert!(
        raw.first() == Some(&0x1e),
        "{what} is not a JSON text sequence: RFC 7464 requires every record to begin with the \
         record separator 0x1e, and the file begins with {}",
        testkit::hex(&raw[..raw.len().min(16)])
    );

    let mut out = Vec::new();
    for (n, chunk) in raw.split(|b| *b == 0x1e).enumerate() {
        if n == 0 {
            assert!(chunk.is_empty(), "{what} has {} bytes before its first record", chunk.len());
            continue;
        }
        let text = String::from_utf8(chunk.to_vec())
            .unwrap_or_else(|e| panic!("{what} record {n} is not UTF-8: {e}"));
        assert!(
            text.ends_with('\n'),
            "{what} record {n} is not newline-terminated, so it cannot be one line of JSON: \
             {:?}",
            &text[..text.len().min(120)]
        );
        assert!(
            text.trim_end_matches('\n').lines().count() == 1,
            "{what} record {n} spans more than one line; a JSON-SEQ record is one line: {:?}",
            &text[..text.len().min(120)]
        );
        let v = json_parse(text.trim())
            .unwrap_or_else(|e| panic!("{what} record {n} is not valid JSON ({e}): {:?}", &text[..text.len().min(200)]));
        out.push(v);
    }
    out
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// cap-boring-crate-backend  (T4)
// ---------------------------------------------------------------------------

/// The BoringSSL submodule is gone and the crypto comes from the `boring`
/// crates.io binding instead.
///
/// A layout check, scored as layout and never as behaviour: it asks cargo what
/// the workspace declares, and the filesystem what the workspace contains. It
/// never asks what the code does -- `gate-endtoend` and the three groups in
/// `reg.rs` do that, and between them they are what says the new library
/// actually works.
///
/// It is a group of its own because this is the change a candidate is most
/// likely to fake. Every release before this one carried BoringSSL as a git
/// submodule at `quiche/deps/boringssl` and compiled it from source through
/// cmake on every build; this one deletes the submodule, deletes the
/// `boringssl-vendored` and `openssl` features that selected that path, drops
/// the `cmake` and `pkg-config` build-dependencies that drove it, and makes
/// `boringssl-boring-crate` -- which links the library the `boring` crate
/// already ships -- the default. A candidate can leave the whole vendored
/// apparatus in place, add `boring` beside it, and produce a tree that builds
/// and passes every behavioural group at this step while having done none of
/// the work the milestone is about.
///
/// The declarations are read from `cargo metadata`, not from the manifest
/// text: cargo's own answer to "what are this package's features and
/// dependencies" is structural and does not care how the manifest is spelled,
/// where the version came from, or whether the dependency was inherited from
/// `[workspace.dependencies]`.
#[test]
fn cap_boring_crate_backend() {
    let dir = testkit::scratch("cap-boring-crate-backend");
    let root = testkit::candidate_root();

    // --- the submodule is gone --------------------------------------------

    let gitmodules = root.join(".gitmodules");
    assert!(
        !gitmodules.exists(),
        "expected the BoringSSL submodule registration to be gone: this \
         release deletes `quiche/deps/boringssl` and takes its crypto from \
         the `boring` crate, so the workspace has no submodules left and no \
         .gitmodules at all. Observed one at {}:\n{}",
        gitmodules.display(),
        std::fs::read_to_string(&gitmodules).unwrap_or_default()
    );
    let deps = testkit::quiche_crate_dir().join("deps").join("boringssl");
    assert!(
        !deps.exists(),
        "expected no BoringSSL checkout at {}: the submodule that used to \
         live there is deleted at this release",
        deps.display()
    );

    // --- what cargo says the package declares -----------------------------

    let mut c = testkit::cmd("cargo");
    c.arg("metadata")
        .arg("--offline")
        .arg("--no-deps")
        .arg("--format-version")
        .arg("1")
        .arg("--manifest-path")
        .arg(root.join("Cargo.toml"))
        .env("CARGO_TARGET_DIR", dir.join("metadata-target"));
    let meta = testkit::run(&mut c, LONG, &dir, "metadata");
    assert!(
        meta.ok(),
        "expected `cargo metadata` on the candidate workspace at {} to \
         succeed; observed status {:?}.\n--- stderr ---\n{}",
        root.display(),
        meta.status,
        meta.stderr
    );

    let doc = json_parse(meta.stdout.trim()).unwrap_or_else(|e| {
        panic!("`cargo metadata` did not produce JSON ({e}); {} bytes of stdout", meta.stdout.len())
    });
    let pkgs = doc
        .get("packages")
        .and_then(Json::as_arr)
        .unwrap_or_else(|| panic!("`cargo metadata` produced no `packages` array"));
    let quiche = pkgs
        .iter()
        .find(|p| p.get("name").and_then(Json::as_str) == Some("quiche"))
        .unwrap_or_else(|| {
            panic!(
                "the candidate workspace has no package named `quiche`; cargo listed {:?}",
                pkgs.iter().filter_map(|p| p.get("name").and_then(Json::as_str)).collect::<Vec<_>>()
            )
        });

    let features = quiche
        .get("features")
        .unwrap_or_else(|| panic!("cargo reported no `features` table for the quiche package"));
    let names = features.keys();

    let default: Vec<&str> = features
        .get("default")
        .and_then(Json::as_arr)
        .unwrap_or_else(|| panic!("cargo reported no `default` feature for quiche; features: {names:?}"))
        .iter()
        .filter_map(Json::as_str)
        .collect();
    assert_eq!(
        default,
        vec!["boringssl-boring-crate"],
        "expected quiche's default feature set to be exactly \
         [\"boringssl-boring-crate\"] -- the release makes the crates.io \
         binding the default rather than one of two ways to build. Observed \
         {default:?}; the package declares the features {names:?}"
    );

    for gone in ["boringssl-vendored", "openssl"] {
        assert!(
            !names.contains(gone),
            "expected the `{gone}` feature to be deleted: it selected the \
             vendored submodule build this release removes, and leaving it \
             declared means the old path is still reachable. quiche declares \
             the features {names:?}"
        );
    }
    for kept in ["boringssl-boring-crate", "ffi", "qlog"] {
        assert!(
            names.contains(kept),
            "expected quiche to keep declaring the `{kept}` feature; it \
             declares {names:?}"
        );
    }

    // --- and what it depends on -------------------------------------------

    let deps = quiche
        .get("dependencies")
        .and_then(Json::as_arr)
        .unwrap_or_else(|| panic!("cargo reported no `dependencies` array for quiche"));
    let named = |name: &str, kind: Option<&str>| -> bool {
        deps.iter().any(|d| {
            d.get("name").and_then(Json::as_str) == Some(name)
                && d.get("kind").and_then(|k| k.as_str()) == kind
        })
    };
    let listing = deps
        .iter()
        .filter_map(|d| {
            let n = d.get("name").and_then(Json::as_str)?;
            let k = d.get("kind").and_then(Json::as_str).unwrap_or("normal");
            Some(format!("{n}({k})"))
        })
        .collect::<Vec<_>>()
        .join(", ");

    for want in ["boring", "boring-sys"] {
        assert!(
            named(want, None),
            "expected quiche to take a normal dependency on `{want}`, the \
             crates.io binding that now provides its BoringSSL. Observed \
             dependencies: {listing}"
        );
    }
    for gone in ["cmake", "pkg-config"] {
        assert!(
            !named(gone, Some("build")),
            "expected the `{gone}` build-dependency to be gone: it existed \
             only to compile the vendored submodule from the build script, \
             and this release does not compile BoringSSL at all. Observed \
             dependencies: {listing}"
        );
    }
}

// ---------------------------------------------------------------------------
// cap-qlog-file-schema  (T4)
// ---------------------------------------------------------------------------

/// The header record of a streamed qlog file names an IETF schema URI.
///
/// This package pinned the old shape at milestone 06 and has carried it
/// since: `{"qlog_version": "0.3", "qlog_format": "JSON-SEQ", ...}`, with the
/// trace carrying a mandatory `vantage_point` and an optional
/// `configuration`. This release moves the crate to the schema the IETF
/// drafts actually define. The version string is replaced by a URI that names
/// the file schema, `qlog_format` is renamed `serialization_format`, the trace
/// gains an `event_schemas` list naming the event schemas its records may use,
/// and `configuration` is gone.
///
/// T4 because a file format can only be observed in the file. The probe crate
/// depends on the candidate's `qlog` package by path and nothing else, writes
/// one JSON-SEQ stream, and prints the crate's own schema constants; this
/// group then reads the bytes off disk and checks them against a fixed
/// expectation table. Nothing here is a substring search over the file: every
/// record is parsed, and the header's key set is compared exactly, so a
/// candidate that emits both the old keys and the new ones fails.
const QLOG_SCHEMA_PROBE: &str = r#"use std::fs::File;
use std::time::Instant;

use qlog::events::quic::SpinBitUpdated;
use qlog::events::{EventData, EventImportance, HTTP3_URI, QUIC_URI};
use qlog::streamer::{EventTimePrecision, QlogStreamer};
use qlog::{
    CommonFields, ReferenceTime, TraceSeq, VantagePoint, VantagePointType, QLOGFILESEQ_URI,
    QLOGFILE_URI, SQLOG_EXT, SQLOG_GZ_EXT, SQLOG_ZST_EXT,
};

const OUT: &str = "__OUT__";

fn main() {
    println!("qlogfile_uri={}", QLOGFILE_URI);
    println!("qlogfileseq_uri={}", QLOGFILESEQ_URI);
    println!("quic_uri={}", QUIC_URI);
    println!("http3_uri={}", HTTP3_URI);
    println!("sqlog_ext={}", SQLOG_EXT);
    println!("sqlog_gz_ext={}", SQLOG_GZ_EXT);
    println!("sqlog_zst_ext={}", SQLOG_ZST_EXT);

    let trace = TraceSeq::new(
        Some("probe".to_string()),
        Some("step-15 schema probe".to_string()),
        Some(CommonFields {
            reference_time: ReferenceTime::new_monotonic(None),
            ..Default::default()
        }),
        Some(VantagePoint {
            ty: VantagePointType::Client,
            ..Default::default()
        }),
        vec![QUIC_URI.to_string(), HTTP3_URI.to_string()],
    );

    let path = format!("{}/probe{}", OUT, SQLOG_EXT);
    let file = File::create(&path).expect("create the sqlog file");
    let mut s = QlogStreamer::new(
        Some("probe".to_string()),
        Some("step-15 schema probe".to_string()),
        Instant::now(),
        trace,
        EventImportance::Extra,
        EventTimePrecision::MicroSeconds,
        Box::new(file),
    );
    s.start_log().expect("start_log");
    s.add_event_data_now(EventData::QuicSpinBitUpdated(SpinBitUpdated { state: true }))
        .expect("add_event_data_now");
    s.finish_log().expect("finish_log");

    println!("path={}", path);
    println!("done=1");
}
"#;

#[test]
fn cap_qlog_file_schema() {
    let dir = testkit::scratch("cap-qlog-file-schema");
    let program = QLOG_SCHEMA_PROBE.replace("__OUT__", &dir.display().to_string());
    let probe = write_probe(&dir, "qlog_schema_probe", "qlog", &program);
    let (build, exe) = build_probe(&dir, &probe, "qlog-schema-build");
    assert!(
        build.ok(),
        "expected a program depending on the candidate's `qlog` package by path to build \
         against it. It needs `QLOGFILE_URI`, `QLOGFILESEQ_URI`, `SQLOG_EXT`, `SQLOG_GZ_EXT`, \
         `SQLOG_ZST_EXT`, `QUIC_URI`, `HTTP3_URI`, a `TraceSeq::new` taking (title, \
         description, common_fields, vantage_point, event_schemas), and a \
         `QlogStreamer::new` taking (title, description, start_time, trace, log_level, \
         time_precision, writer).\n{}",
        build.output()
    );

    let (out, raw) = run_probe(&dir, &exe, "qlog-schema-run");
    let kv = |k: &str| -> &str {
        out.get(k)
            .map(String::as_str)
            .unwrap_or_else(|| panic!("expected the probe to print `{k}=`.\n{raw}"))
    };
    assert_eq!(kv("done"), "1", "expected the probe to reach the end of main.\n{raw}");

    // --- the crate's own constants ----------------------------------------

    for (key, want) in [
        ("qlogfile_uri", "urn:ietf:params:qlog:file:contained"),
        ("qlogfileseq_uri", "urn:ietf:params:qlog:file:sequential"),
        ("quic_uri", "urn:ietf:params:qlog:events:quic-12"),
        ("http3_uri", "urn:ietf:params:qlog:events:http3-12"),
        ("sqlog_ext", ".sqlog"),
        ("sqlog_gz_ext", ".sqlog.gz"),
        ("sqlog_zst_ext", ".sqlog.zst"),
    ] {
        assert_eq!(
            kv(key),
            want,
            "expected `{key}` to be {want:?} -- the URI the qlog drafts define, verbatim. \
             Observed {:?}.\n{raw}",
            kv(key)
        );
    }

    // --- and the file it wrote --------------------------------------------

    let path = PathBuf::from(kv("path"));
    assert_eq!(
        path.extension().and_then(|e| e.to_str()),
        Some("sqlog"),
        "expected the probe to have written a file named with the crate's own SQLOG_EXT; \
         observed {}.\n{raw}",
        path.display()
    );
    let bytes = std::fs::read(&path)
        .unwrap_or_else(|e| panic!("expected the probe to write {}: {e}\n{raw}", path.display()));
    let records = jsonseq_records(&bytes, "probe.sqlog");
    assert_eq!(
        records.len(),
        2,
        "expected the header record plus the one event the probe emitted; observed {} \
         records.\n{raw}",
        records.len()
    );

    let head = &records[0];
    let want_keys: BTreeSet<String> =
        ["description", "file_schema", "serialization_format", "title", "trace"]
            .iter()
            .map(|s| s.to_string())
            .collect();
    assert_eq!(
        head.keys(),
        want_keys,
        "expected the streaming header record to carry exactly {want_keys:?}. The two names \
         this package carried from milestone 06 -- `qlog_version` and `qlog_format` -- are \
         both replaced at this release, so a header that still has either has not made the \
         jump. Observed header: {head:?}"
    );
    assert_eq!(
        head.get("file_schema").and_then(Json::as_str),
        Some("urn:ietf:params:qlog:file:sequential"),
        "expected the header's `file_schema` to be the sequential-file URI, because this is a \
         JSON-SEQ stream and not a contained file. Observed header: {head:?}"
    );
    assert_eq!(
        head.get("serialization_format").and_then(Json::as_str),
        Some("JSON-SEQ"),
        "expected the header's `serialization_format` to be \"JSON-SEQ\". Observed header: \
         {head:?}"
    );

    let trace = head
        .get("trace")
        .unwrap_or_else(|| panic!("expected the streaming header to carry a single `trace`; observed {head:?}"));
    assert!(
        head.get("traces").is_none(),
        "expected the streaming header to carry a single `trace`, not the `traces` array of \
         the buffered format. Observed header keys: {:?}",
        head.keys()
    );
    let schemas: Vec<&str> = trace
        .get("event_schemas")
        .and_then(Json::as_arr)
        .unwrap_or_else(|| panic!("expected the trace to carry an `event_schemas` array; observed {trace:?}"))
        .iter()
        .filter_map(Json::as_str)
        .collect();
    assert_eq!(
        schemas,
        vec!["urn:ietf:params:qlog:events:quic-12", "urn:ietf:params:qlog:events:http3-12"],
        "expected the trace to declare the two event schemas the probe passed to \
         `TraceSeq::new`, in that order. This field does not exist in the schema this \
         package carried from milestone 06; it is how a reader knows which event names to \
         expect. Observed {schemas:?} in {trace:?}"
    );
    assert!(
        trace.get("configuration").is_none(),
        "expected the trace to have no `configuration`: the field is gone at this schema. \
         Observed trace keys: {:?}",
        trace.keys()
    );
}

// ---------------------------------------------------------------------------
// cap-qlog-event-namespace  (T4)
// ---------------------------------------------------------------------------

/// Every event name moves into the `quic:` and `http3:` namespaces.
///
/// The schema this package carried from milestone 06 spread its events over
/// categories that mirrored the draft documents of the day -- `connectivity:`,
/// `security:`, `transport:`, `recovery:`, `http:`, `qpack:` and `generic:`.
/// This release collapses the first four into `quic:` and renames `http:` to
/// `http3:`, which is why the header record now has to declare which event
/// schemas a reader should expect. The names are the wire format: a tool
/// reading the candidate's logs dispatches on them, so renaming the Rust
/// variants without renaming the serialized strings changes nothing that
/// matters, and renaming the strings is the whole of the work.
///
/// A fixed expectation table over four events drawn from four different old
/// categories, so that a candidate who moved one namespace and not the others
/// fails here rather than passing by luck. The fourth is also the control for
/// the opposite mistake: `stream_type_set` is an HTTP/3 event and belongs in
/// `http3:`, so a candidate who rewrote every name to start with `quic:` fails
/// as surely as one who rewrote none of them.
///
/// The payload of the first event is checked as well as its name. A name is a
/// string and could be produced by a stub; the `MtuUpdated` body proves the
/// name is attached to the event it claims to be.
const QLOG_NAMES_PROBE: &str = r#"use std::fs::File;
use std::time::Instant;

use qlog::events::http3::{StreamType, StreamTypeSet};
use qlog::events::quic::{CongestionStateUpdated, KeyType, KeyUpdated, MtuUpdated};
use qlog::events::{EventData, EventImportance, HTTP3_URI, QUIC_URI};
use qlog::streamer::{EventTimePrecision, QlogStreamer};
use qlog::{CommonFields, ReferenceTime, TraceSeq, VantagePoint, VantagePointType, SQLOG_EXT};

const OUT: &str = "__OUT__";

fn main() {
    let trace = TraceSeq::new(
        Some("probe".to_string()),
        Some("step-15 name probe".to_string()),
        Some(CommonFields {
            reference_time: ReferenceTime::new_monotonic(None),
            ..Default::default()
        }),
        Some(VantagePoint {
            ty: VantagePointType::Server,
            ..Default::default()
        }),
        vec![QUIC_URI.to_string(), HTTP3_URI.to_string()],
    );

    let path = format!("{}/names{}", OUT, SQLOG_EXT);
    let file = File::create(&path).expect("create the sqlog file");
    let mut s = QlogStreamer::new(
        Some("probe".to_string()),
        Some("step-15 name probe".to_string()),
        Instant::now(),
        trace,
        EventImportance::Extra,
        EventTimePrecision::MicroSeconds,
        Box::new(file),
    );
    s.start_log().expect("start_log");

    s.add_event_data_now(EventData::QuicMtuUpdated(MtuUpdated {
        old: Some(1200),
        new: 1452,
        done: Some(true),
    }))
    .expect("mtu_updated");

    s.add_event_data_now(EventData::QuicKeyUpdated(KeyUpdated {
        key_type: KeyType::Server1RttSecret,
        old: None,
        new: None,
        key_phase: Some(1),
        trigger: None,
    }))
    .expect("key_updated");

    s.add_event_data_now(EventData::QuicCongestionStateUpdated(CongestionStateUpdated {
        old: None,
        new: "congestion_avoidance".to_string(),
        trigger: None,
    }))
    .expect("congestion_state_updated");

    s.add_event_data_now(EventData::Http3StreamTypeSet(StreamTypeSet {
        initiator: None,
        stream_id: 2,
        stream_type: StreamType::Control,
        stream_type_bytes: None,
        associated_push_id: None,
    }))
    .expect("stream_type_set");

    s.finish_log().expect("finish_log");

    println!("path={}", path);
    println!("done=1");
}
"#;

#[test]
fn cap_qlog_event_namespace() {
    let dir = testkit::scratch("cap-qlog-event-namespace");
    let program = QLOG_NAMES_PROBE.replace("__OUT__", &dir.display().to_string());
    let probe = write_probe(&dir, "qlog_names_probe", "qlog", &program);
    let (build, exe) = build_probe(&dir, &probe, "qlog-names-build");
    assert!(
        build.ok(),
        "expected a program depending on the candidate's `qlog` package by path to build \
         against it. It emits one event from each of four old categories through a \
         `QlogStreamer`, naming the variants `QuicMtuUpdated`, `QuicKeyUpdated`, \
         `QuicCongestionStateUpdated` and `Http3StreamTypeSet`.\n{}",
        build.output()
    );

    let (out, raw) = run_probe(&dir, &exe, "qlog-names-run");
    let kv = |k: &str| -> &str {
        out.get(k)
            .map(String::as_str)
            .unwrap_or_else(|| panic!("expected the probe to print `{k}=`.\n{raw}"))
    };
    assert_eq!(kv("done"), "1", "expected the probe to reach the end of main.\n{raw}");

    let bytes = std::fs::read(kv("path"))
        .unwrap_or_else(|e| panic!("expected the probe to write {}: {e}\n{raw}", kv("path")));
    let records = jsonseq_records(&bytes, "names.sqlog");
    assert_eq!(
        records.len(),
        5,
        "expected the header record plus the four events the probe emitted; observed {} \
         records.\n{raw}",
        records.len()
    );

    let event_keys: BTreeSet<String> =
        ["data", "name", "time"].iter().map(|s| s.to_string()).collect();

    // The fixed expectation table. The left column is the name this package
    // emitted for the same event at the previous milestone; the right column is
    // what this release must emit.
    for (n, was, want) in [
        (1usize, "connectivity:mtu_updated", "quic:mtu_updated"),
        (2, "security:key_updated", "quic:key_updated"),
        (3, "recovery:congestion_state_updated", "quic:congestion_state_updated"),
        (4, "http:stream_type_set", "http3:stream_type_set"),
    ] {
        let ev = &records[n];
        assert_eq!(
            ev.keys(),
            event_keys,
            "expected streamed event record {n} to carry exactly {event_keys:?}. Observed: \
             {ev:?}"
        );
        let got = ev.get("name").and_then(Json::as_str).unwrap_or("<missing>");
        assert_eq!(
            got, want,
            "expected event record {n} to be named {want:?}; this package emitted it as \
             {was:?} at the previous milestone and this release renames it. Observed {got:?}"
        );
        assert!(
            matches!(ev.get("time"), Some(Json::Num(_))),
            "expected event record {n} to carry a JSON number `time`. Observed {ev:?}"
        );
    }

    // The body of the first event, so that the name is known to be attached to
    // the event it claims to be.
    let mtu = records[1].get("data").unwrap_or_else(|| {
        panic!(
            "expected the mtu_updated record to carry a `data` object; observed {:?}",
            records[1]
        )
    });
    assert_eq!(
        (
            mtu.get("old").and_then(Json::as_u64),
            mtu.get("new").and_then(Json::as_u64),
            mtu.get("done"),
        ),
        (Some(1200), Some(1452), Some(&Json::Bool(true))),
        "expected the mtu_updated body the probe passed in -- old 1200, new 1452, done true. \
         Observed {mtu:?}"
    );
}

/// Settle the transport and poll both HTTP/3 connections until neither has an
/// event left.
///
/// `peer_settings_raw` is filled in when the peer's control stream is parsed,
/// and that parsing happens inside `poll`. Settling moves the datagrams
/// across; polling is what reads them.
fn drive(h: &mut pair::H3Pair) {
    for _ in 0..16 {
        h.settle();
        let quiet = matches!(h.poll_server(), Err(quiche::h3::Error::Done)) &
            matches!(h.poll_client(), Err(quiche::h3::Error::Done));
        if quiet {
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// cap-h3-default-field-section  (T3)
// ---------------------------------------------------------------------------

/// A fresh `h3::Config` now advertises a field section limit.
///
/// Through milestone 14 `h3::Config::new()` left `max_field_section_size` at
/// `None`: the endpoint sent no SETTINGS_MAX_FIELD_SECTION_SIZE at all, and a
/// peer was free to send a header section of any size. This release gives the
/// setting a default, so a peer that never touched the config now sees an
/// explicit limit in the SETTINGS frame.
///
/// Observed from the peer's side of a real HTTP/3 connection rather than by
/// reading the config back: what the local object holds is not the milestone,
/// what crosses the wire is. `peer_settings_raw` reports the identifier/value
/// pairs exactly as they were received, so this reads the SETTINGS frame the
/// candidate actually sent.
///
/// The second half sets the value explicitly on one side. The default must be
/// a default and not a hard-wired constant: a candidate that writes 32768 into
/// the SETTINGS frame unconditionally passes the first half and fails here.
#[test]
fn cap_h3_default_field_section() {
    const ID: u64 = 0x06;

    // The constant is part of the interface -- the release names it so an
    // application can reason about the limit without rediscovering it -- so it
    // is checked alongside the behaviour it describes.
    assert_eq!(
        quiche::h3::SETTINGS_MAX_FIELD_SECTION_SIZE_DEFAULT,
        32_768,
        "expected the advertised default field section limit to be 32768 octets"
    );

    let mut h = pair::H3Pair::new();
    drive(&mut h);

    for (who, raw) in [
        ("client", h.client.peer_settings_raw()),
        ("server", h.server.peer_settings_raw()),
    ] {
        let raw = raw.unwrap_or_else(|| {
            panic!(
                "expected the {who} to have received its peer's SETTINGS frame once the \
                 control streams had settled; observed none"
            )
        });
        let found: Vec<u64> = raw.iter().filter(|(id, _)| *id == ID).map(|(_, v)| *v).collect();
        assert_eq!(
            found,
            vec![quiche::h3::SETTINGS_MAX_FIELD_SECTION_SIZE_DEFAULT],
            "expected the {who}'s peer -- an endpoint built from a default `h3::Config` -- \
             to have sent exactly one SETTINGS_MAX_FIELD_SECTION_SIZE (identifier {ID:#x}) \
             carrying the new default. At the previous milestone the default was `None` and \
             the setting was absent entirely. Observed the whole raw settings list: {raw:?}"
        );
    }

    // A value the application chose still wins.
    let mut server_cfg = pair::h3_config();
    server_cfg.set_max_field_section_size(4_096);
    let mut h = pair::H3Pair::with_h3_configs(
        pair::Pair::new(pair::ALPN_H3),
        &pair::h3_config(),
        &server_cfg,
    );
    drive(&mut h);

    let raw = h
        .client
        .peer_settings_raw()
        .expect("expected the client to have received the server's SETTINGS frame");
    let found: Vec<u64> = raw.iter().filter(|(id, _)| *id == ID).map(|(_, v)| *v).collect();
    assert_eq!(
        found,
        vec![4_096],
        "expected `set_max_field_section_size(4096)` on the server's `h3::Config` to reach \
         the wire in place of the default. Observed the whole raw settings list: {raw:?}"
    );
}

// ---------------------------------------------------------------------------
// cap-dgram-buf-api  (T3)
// ---------------------------------------------------------------------------

/// DATAGRAMs can be handed over and taken back as owned buffers.
///
/// `dgram_send`/`dgram_recv` copy: the sender copies the caller's slice into
/// the send queue and the receiver copies out of the receive queue into the
/// caller's array. This release adds `dgram_send_buf`/`dgram_recv_buf`, which
/// move an owned buffer in each direction instead. The copying pair stays --
/// `reg-04-datagram-roundtrip` still drives it -- so this is an addition to
/// the surface, and the group checks the new pair carries the same bytes the
/// old one would have.
///
/// The two error answers are exact rather than "some error", because they are
/// the whole of the contract for a buffer the connection cannot accept: too
/// large for one datagram is `BufferTooShort`, and a peer that never
/// negotiated the extension is `InvalidState`.
#[test]
fn cap_dgram_buf_api() {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.enable_dgram(true, 10, 10);
    sc.enable_dgram(true, 10, 10);

    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let payload = testkit::body(512, 0x0d6a_0015);

    // Hand the buffer over. The connection takes ownership; nothing is
    // returned, so the only evidence is what arrives.
    p.client
        .dgram_send_buf(payload.clone())
        .expect("expected `dgram_send_buf` to accept an owned buffer that fits in one datagram");
    p.settle(64);

    let got = p
        .server
        .dgram_recv_buf()
        .expect("expected `dgram_recv_buf` to hand back the datagram the client sent");
    assert_eq!(
        got, payload,
        "expected the owned buffer to arrive byte for byte; observed {} bytes against {} sent",
        got.len(),
        payload.len()
    );

    // The queue is a queue: taking the datagram removed it.
    assert_eq!(
        p.server.dgram_recv_buf(),
        Err(quiche::Error::Done),
        "expected a second `dgram_recv_buf` to report `Done`: the one datagram that arrived \
         has already been handed over, and an owned read must consume it exactly as the \
         copying read does"
    );

    // Echo back, so the direction is proven both ways.
    p.server.dgram_send_buf(got).expect("server `dgram_send_buf`");
    p.settle(64);
    assert_eq!(
        p.client.dgram_recv_buf(),
        Ok(payload.clone()),
        "expected the server's echo to arrive back at the client unchanged"
    );

    // Too large for one datagram.
    let max = p
        .client
        .dgram_max_writable_len()
        .expect("client: the peer advertised no DATAGRAM support");
    assert_eq!(
        p.client.dgram_send_buf(vec![0x5au8; max + 1]),
        Err(quiche::Error::BufferTooShort),
        "expected an owned buffer one byte past the writable length ({max}) to be refused \
         with `BufferTooShort` -- a DATAGRAM frame is never fragmented"
    );
    assert_eq!(
        p.client.dgram_send_buf(vec![0x5au8; max]),
        Ok(()),
        "expected a buffer of exactly the writable length ({max}) to be accepted, so that \
         the refusal above is a boundary and not a blanket rejection"
    );

    // A peer that never negotiated the extension.
    let mut p2 = pair::Pair::new(pair::ALPN_HQ);
    p2.handshake();
    assert_eq!(
        p2.client.dgram_send_buf(vec![0u8; 16]),
        Err(quiche::Error::InvalidState),
        "expected `dgram_send_buf` on a connection whose peer advertised no DATAGRAM support \
         to report `InvalidState`"
    );
    assert_eq!(
        p2.server.dgram_recv_buf(),
        Err(quiche::Error::Done),
        "expected `dgram_recv_buf` with nothing queued to report `Done`"
    );
}

// ---------------------------------------------------------------------------
// datagram plumbing for the two groups below that need to choose the order in
// which packets arrive, or to watch a handshake that is meant not to finish.
// `pair::Pair` always delivers everything, in order, and insists the peer
// accepted it; neither group can use it.
// ---------------------------------------------------------------------------

/// Take every datagram `from` currently wants to send, delivering none of
/// them. The caller decides what reaches the peer and in what order.
fn drain(from: &mut quiche::Connection) -> Vec<(Vec<u8>, quiche::SendInfo)> {
    let mut out = Vec::new();
    let mut buf = [0u8; 1452];
    while let Ok((len, info)) = from.send(&mut buf) {
        out.push((buf[..len].to_vec(), info));
        assert!(
            out.len() <= 512,
            "expected {} to stop producing datagrams; observed more than 512 in one drain",
            from.trace_id()
        );
    }
    out
}

/// Deliver datagrams to `to` in the order given, failing the group if one is
/// refused for any reason other than "nothing to report".
fn deliver(to: &mut quiche::Connection, pkts: &[(Vec<u8>, quiche::SendInfo)]) {
    for (i, (d, info)) in pkts.iter().enumerate() {
        let mut d = d.clone();
        let ri = quiche::RecvInfo { from: info.from, to: info.to };
        match to.recv(&mut d, ri) {
            Ok(_) | Err(quiche::Error::Done) => {},
            Err(e) => panic!(
                "expected {} to accept datagram {i} of {} ({} bytes); observed {e:?}",
                to.trace_id(),
                pkts.len(),
                d.len()
            ),
        }
    }
}

/// Move datagrams between two connections until neither has more to say,
/// without insisting either one is healthy. A handshake that is supposed to
/// fail produces errors on both sides, and they are the subject rather than a
/// fault.
fn settle_lenient(a: &mut quiche::Connection, b: &mut quiche::Connection, rounds: usize) {
    fn one_way(from: &mut quiche::Connection, to: &mut quiche::Connection) -> usize {
        let mut buf = [0u8; 1452];
        let mut n = 0usize;
        while let Ok((len, info)) = from.send(&mut buf) {
            let mut d = buf[..len].to_vec();
            let ri = quiche::RecvInfo { from: info.from, to: info.to };
            let _ = to.recv(&mut d, ri);
            n += 1;
            if n > 512 {
                break;
            }
        }
        n
    }

    for _ in 0..rounds {
        if one_way(a, b) + one_way(b, a) == 0 {
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// cap-stream-readable-len  (T3)
// ---------------------------------------------------------------------------

/// How much of a stream can be read right now, without reading it.
///
/// `stream_readable` answers whether there is anything to read. An
/// application sizing a buffer wants the number, and until this release the
/// only way to get it was to call `stream_recv` with an optimistic array and
/// look at what came back. `stream_readable_len` reports it directly, bounded
/// by a caller-supplied ceiling so the walk is cheap.
///
/// The load-bearing word is *contiguous*. A count of everything the receiver
/// has buffered would be wrong: data that arrived out of order and sits behind
/// a gap cannot be read, and an application that sized a buffer from that
/// count and then called `stream_recv` would get less than it asked for. So
/// the group builds the gap deliberately -- the two writes are captured before
/// they are delivered and then handed to the server in the wrong order -- and
/// checks the count is zero while the gap is open and the full length once it
/// closes. A naive implementation that sums the receive buffer passes every
/// other assertion here and fails that one.
#[test]
fn cap_stream_readable_len() {
    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();
    p.settle(16);

    // Start from quiet: nothing either side still owes the other.
    let pending = drain(&mut p.client);
    deliver(&mut p.server, &pending);
    let pending = drain(&mut p.server);
    deliver(&mut p.client, &pending);

    let first_half = testkit::body(1_000, 0x5ea1_0015);
    let second_half = testkit::body(800, 0x5ea2_0015);
    let whole: Vec<u8> = first_half.iter().chain(second_half.iter()).copied().collect();

    // Two writes, each captured before the peer can see it.
    let n = p.client.stream_send(4, &first_half, false).expect("client stream_send");
    assert_eq!(n, first_half.len(), "client stream_send accepted {n} of {}", first_half.len());
    let head = drain(&mut p.client);
    assert!(
        !head.is_empty(),
        "expected the client to have a datagram to send after writing to stream 4"
    );

    let n = p.client.stream_send(4, &second_half, false).expect("client stream_send");
    assert_eq!(n, second_half.len(), "client stream_send accepted {n} of {}", second_half.len());
    let tail = drain(&mut p.client);
    assert!(
        !tail.is_empty(),
        "expected the client to have a second datagram to send after the second write"
    );

    // Nothing has arrived yet, and a stream that does not exist reads as zero
    // rather than as an error.
    assert_eq!(
        p.server.stream_readable_len(4, usize::MAX),
        0,
        "expected zero readable on stream 4 before any of it has arrived"
    );

    // The tail first: the server now holds bytes at offset {} with nothing in
    // front of them.
    deliver(&mut p.server, &tail);
    assert_eq!(
        p.server.stream_readable_len(4, usize::MAX),
        0,
        "expected zero contiguous readable bytes while the front of stream 4 is still \
         missing: the server is holding the {} bytes written second, at offset {}, and none \
         of them can be read until the gap closes. A count of everything buffered would \
         report {} here",
        second_half.len(),
        first_half.len(),
        second_half.len()
    );
    assert!(
        !p.server.stream_readable(4),
        "expected `stream_readable` to agree that stream 4 has nothing to read while the \
         gap is open"
    );

    // Close the gap.
    deliver(&mut p.server, &head);
    assert_eq!(
        p.server.stream_readable_len(4, usize::MAX),
        whole.len(),
        "expected all {} bytes to be readable once the missing front arrived",
        whole.len()
    );

    // The ceiling is a ceiling, not a hint.
    assert_eq!(
        p.server.stream_readable_len(4, 100),
        100,
        "expected the count to stop at the caller's ceiling of 100, even though the first \
         buffered chunk alone is {} bytes long",
        first_half.len()
    );
    assert_eq!(
        p.server.stream_readable_len(4, 0),
        0,
        "expected a ceiling of zero to report zero"
    );

    // Reading moves the count down by exactly what was read.
    let mut buf = vec![0u8; 600];
    let (got, fin) = p.server.stream_recv(4, &mut buf).expect("server stream_recv");
    assert_eq!(got, 600, "expected the 600-byte read to fill the buffer");
    assert!(!fin, "expected no fin: the client has not finished stream 4");
    assert_eq!(
        buf[..got],
        whole[..600],
        "expected the first 600 bytes of the stream, in order"
    );
    assert_eq!(
        p.server.stream_readable_len(4, usize::MAX),
        whole.len() - 600,
        "expected the readable count to track the read offset: {} bytes arrived and 600 were \
         consumed",
        whole.len()
    );

    // A stream nobody has opened.
    assert_eq!(
        p.server.stream_readable_len(44, usize::MAX),
        0,
        "expected zero for a stream that was never opened, rather than an error"
    );
}

// ---------------------------------------------------------------------------
// cap-stream-closed  (T3)
// ---------------------------------------------------------------------------

/// Whether a stream is finished in both the directions that apply to it.
///
/// The connection already exposed `stream_finished`, which reports only that
/// the receive side saw its fin. `stream_closed` is the whole-stream question,
/// and the answer depends on the stream's kind: a bidirectional stream is
/// closed when both directions have finished, a unidirectional stream when the
/// one direction that exists has. It also has to survive the stream being
/// collected -- an application that asks after the connection has dropped its
/// state should be told the stream is closed, not that it never existed.
///
/// So the group walks one bidirectional stream through both halves of its
/// finish and checks the answer flips only at the end, then checks a locally
/// opened unidirectional stream, where the peer's direction does not exist and
/// must not be waited for. The never-opened stream is the other side of the
/// collected-stream rule: the same `false`, for the opposite reason.
#[test]
fn cap_stream_closed() {
    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();

    // Bidirectional, nothing finished.
    let request = testkit::body(256, 0x5c10_0015);
    let (got, fin) = p.client_to_server(4, &request, false);
    assert_eq!(got, request, "expected the request bytes to arrive unchanged");
    assert!(!fin, "expected no fin on a write that did not carry one");
    assert!(
        !p.client.stream_closed(4),
        "expected stream 4 to be open at the client: neither direction has finished"
    );
    assert!(
        !p.server.stream_closed(4),
        "expected stream 4 to be open at the server: neither direction has finished"
    );

    // The client finishes its half. A bidirectional stream is not closed by
    // one direction.
    let (_, fin) = p.client_to_server(4, &[], true);
    assert!(fin, "expected the server to see the client's fin on stream 4");
    assert!(
        !p.client.stream_finished(4),
        "expected `stream_finished` at the client to still be false: it reports the receive \
         side, and the server has not finished"
    );
    assert!(
        !p.server.stream_closed(4),
        "expected stream 4 to still be open at the server after only the client's direction \
         finished: a bidirectional stream needs both"
    );
    assert!(
        !p.client.stream_closed(4),
        "expected stream 4 to still be open at the client after only its own direction \
         finished"
    );

    // The server finishes the other half.
    let response = testkit::body(128, 0x5c11_0015);
    let (got, fin) = p.server_to_client(4, &response, true);
    assert_eq!(got, response, "expected the response bytes to arrive unchanged");
    assert!(fin, "expected the client to see the server's fin on stream 4");
    p.settle(16);
    assert!(
        p.server.stream_closed(4),
        "expected stream 4 to be closed at the server now that both directions have finished"
    );
    assert!(
        p.client.stream_closed(4),
        "expected stream 4 to be closed at the client now that both directions have finished. \
         The stream may well have been collected by now, which is still closed and not \
         `never opened`"
    );

    // A stream nobody has opened is not closed.
    assert!(
        !p.client.stream_closed(44),
        "expected a stream that was never opened to report false, distinguishing it from one \
         that finished and was collected"
    );

    // Unidirectional, opened locally: only the sending direction exists.
    let note = testkit::body(64, 0x5c12_0015);
    p.client.stream_send(2, &note, true).expect("client stream_send on uni stream 2");
    p.client.stream_send(6, &note, false).expect("client stream_send on uni stream 6");
    p.settle(32);
    assert!(
        p.client.stream_closed(2),
        "expected locally opened unidirectional stream 2 to be closed once its only \
         direction finished: there is no receive side to wait for"
    );
    assert!(
        !p.client.stream_closed(6),
        "expected locally opened unidirectional stream 6, written without a fin, to still be \
         open"
    );
}

// ---------------------------------------------------------------------------
// cap-curves-list  (T3)
// ---------------------------------------------------------------------------

/// The TLS group preference list is configurable.
///
/// `Config::set_curves_list` hands a colon-separated preference list through
/// to `SSL_CTX_set1_groups_list`, which is how an application pins the key
/// exchange -- to a post-quantum hybrid, or away from one. Until this release
/// the list was whatever the TLS backend defaulted to and there was no way to
/// reach it.
///
/// A setter that took the string and did nothing would return `Ok` and hand
/// back a connection that handshakes, so "it handshakes" proves nothing. The
/// observation is a pair of configurations with no group in common: the
/// handshake must not complete. The matching pair beside it is the control,
/// so a candidate whose connections never handshake at all fails too.
#[test]
fn cap_curves_list() {
    // A list the backend does not recognise is refused, and refused with the
    // one error that means the TLS layer said no.
    let mut bad = quiche::Config::new(quiche::PROTOCOL_VERSION).expect("Config::new");
    assert_eq!(
        bad.set_curves_list("not-a-curve"),
        Err(quiche::Error::TlsFail),
        "expected an unrecognised group name to be refused with `TlsFail`"
    );

    // No group in common: the handshake cannot complete.
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.set_curves_list("P-256").expect("client set_curves_list(P-256)");
    sc.set_curves_list("X25519").expect("server set_curves_list(X25519)");
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    settle_lenient(&mut p.client, &mut p.server, 40);
    assert!(
        !p.client.is_established() && !p.server.is_established(),
        "expected no handshake between a client offering only P-256 and a server accepting \
         only X25519: they share no group. Observed client established={}, closed={}; server \
         established={}, closed={}. A `set_curves_list` that accepted the string and changed \
         nothing produces exactly this successful handshake",
        p.client.is_established(),
        p.client.is_closed(),
        p.server.is_established(),
        p.server.is_closed()
    );

    // The control: the same mechanism with a group in common.
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.set_curves_list("P-384:P-256").expect("client set_curves_list");
    sc.set_curves_list("P-256").expect("server set_curves_list");
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let body = testkit::body(256, 0x0c17_0015);
    let (got, fin) = p.client_to_server(4, &body, true);
    assert_eq!(
        got, body,
        "expected a connection whose two ends share P-256 to carry stream data normally"
    );
    assert!(fin, "expected the server to see the fin");
}
