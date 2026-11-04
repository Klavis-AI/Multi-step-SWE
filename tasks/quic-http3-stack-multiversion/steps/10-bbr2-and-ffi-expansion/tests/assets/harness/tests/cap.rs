//! new-capability groups: what this milestone can do that the previous one could not
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `cap-bbr2` is `fn cap_bbr2()`. The driver runs
//! each one on its own with `--exact` and asserts that exactly one test
//! executed, so a renamed, `#[ignore]`d or accidentally deleted function
//! scores zero for that group rather than passing silently.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.

use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
// Cold `cargo build` of a probe crate. Measured against this step's Oracle tree:
// quiche with `--features ffi` ~11 s, the qlog probe ~29 s over 24 crates. 90 s
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

/// The first C compiler on PATH that answers `--version`.
fn c_compiler() -> String {
    for cc in ["cc", "gcc", "clang"] {
        if let Ok(o) = testkit::cmd(cc).arg("--version").output() {
            if o.status.success() {
                return cc.to_string();
            }
        }
    }
    panic!("no C compiler (tried cc, gcc, clang) in the verifier image");
}

/// The congestion window of a connection's active path.
///
/// `cwnd` is per-path and lives on `PathStats`; the connection-wide `Stats`
/// this milestone returns does not carry one.
fn active_cwnd(conn: &quiche::Connection) -> usize {
    conn.path_stats()
        .find(|s| s.active)
        .unwrap_or_else(|| panic!("{} has no active path", conn.trace_id()))
        .cwnd
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

/// Build the candidate's `quiche` package with the `ffi` feature into a
/// scratch target directory, and return the directory holding the resulting
/// `libquiche.a`.
///
/// The scratch directory is not optional. When a lib target's `crate-type`
/// includes `staticlib` or `cdylib`, cargo drops the `-<hash>` extra-filename
/// from the rlib it emits, so a feature-enabled build and a default build
/// write to the same `deps/libquiche.rlib`. Building `--features ffi` into the
/// shared `CARGO_TARGET_DIR` the driver uses would therefore replace the
/// default-feature rlib every other group is compiled against, and each of
/// them would rebuild quiche from scratch on its next invocation. A cold build
/// into a directory of this group's own costs seconds, because the image
/// already has BoringSSL built and `QUICHE_BSSL_PATH` pointing at it.
fn build_quiche_ffi(dir: &Path) -> PathBuf {
    let target = dir.join("ffi-target");
    let manifest = testkit::quiche_crate_dir().join("Cargo.toml");

    let mut c = testkit::cmd("cargo");
    c.arg("build")
        .arg("--offline")
        .arg("--jobs")
        .arg("4")
        .arg("--manifest-path")
        .arg(&manifest)
        .arg("--features")
        .arg("ffi")
        .env("CARGO_TARGET_DIR", &target);
    let run = testkit::run(&mut c, LONG, dir, "ffi-build");
    assert!(
        run.ok(),
        "expected `cargo build --features ffi` on {} to succeed; observed status {:?}. The \
         step requires the C entry points to live behind the `ffi` feature, so this feature \
         must exist and the crate must compile with it on.\n--- cargo stdout ---\n{}\n\
         --- cargo stderr ---\n{}",
        manifest.display(),
        run.status,
        run.stdout,
        run.stderr
    );

    let libdir = target.join("debug");
    assert!(
        libdir.join("libquiche.a").is_file(),
        "expected a staticlib at {}/libquiche.a after building with --features ffi; observed \
         none. The crate-type must include \"staticlib\" for a C program to link against it.",
        libdir.display()
    );
    libdir
}

/// Compile one C program against the candidate's header and the
/// feature-enabled staticlib, run it, and return its `key=value` output.
///
/// The link line carries nothing but the C runtime's own companions
/// (`-lpthread -ldl -lm -lstdc++`): every QUIC and TLS symbol has to come out
/// of `libquiche.a` itself, so a header that declares a function the library
/// never exported fails here with the linker's own message.
fn c_probe(dir: &Path, name: &str, program: &str, libdir: &Path) -> (BTreeMap<String, String>, String) {
    let src = dir.join(format!("{name}.c"));
    let exe = dir.join(name);
    std::fs::write(&src, program).expect("write C program");

    let include = testkit::quiche_crate_dir().join("include");
    let header = include.join("quiche.h");
    assert!(
        header.is_file(),
        "expected the C header at {}; observed no such file. The header lives in an \
         `include/` directory of the transport crate's own, wherever TEST_INTERFACE.md's \
         search order finds that crate.",
        header.display()
    );

    let cc = c_compiler();
    let mut b = testkit::cmd(&cc);
    b.arg("-std=c99")
        .arg("-Wall")
        .arg("-I")
        .arg(&include)
        .arg("-o")
        .arg(&exe)
        .arg(&src)
        .arg(libdir.join("libquiche.a"))
        .arg("-lpthread")
        .arg("-ldl")
        .arg("-lm")
        .arg("-lstdc++");
    let build = testkit::run(&mut b, SHORT, dir, &format!("{name}-build"));
    assert!(
        build.ok(),
        "expected `{cc}` to compile {name}.c against {} and link it against {}/libquiche.a; \
         observed status {:?}. A missing declaration is a header defect; an undefined \
         reference is a missing `#[no_mangle] pub extern \"C\"` export.\n\
         --- compiler stdout ---\n{}\n--- compiler stderr ---\n{}",
        header.display(),
        libdir.display(),
        build.status,
        build.stdout,
        build.stderr
    );

    run_probe(dir, &exe, &format!("{name}-run"))
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// cap-bbr2  (T3)
// ---------------------------------------------------------------------------

/// `cap-bbr2`: a fourth congestion controller joins the enum, reachable by
/// both selection paths, and a connection driven under it carries data.
///
/// What this group deliberately does **not** assert is a behavioural
/// difference between `bbr` and `bbr2`. On a lossless in-memory path with no
/// delay model, the two controllers see the same signals and there is no
/// black-box observation that separates them; an assertion that claimed
/// otherwise would be asserting noise. What is observable is the name table,
/// the discriminant, and that a transfer under the new name completes with a
/// congestion window that grew — which is what a fourth controller has to do
/// to be a controller at all.
#[test]
fn cap_bbr2() {
    use quiche::CongestionControlAlgorithm as CCA;
    use std::str::FromStr;

    // 1. The name table. `bbr2` joins; nothing else does, and the match is
    //    exact -- no case folding, no trimming, no near-miss.
    assert_eq!(
        CCA::from_str("bbr2"),
        Ok(CCA::BBR2),
        "expected the string \"bbr2\" to name the new congestion controller"
    );
    for (name, why) in [
        ("BBR2", "upper case: the table is matched exactly, not case-folded"),
        ("bbr2 ", "a trailing space: the name is not trimmed before matching"),
        ("bbr3", "a controller this release does not have"),
        ("", "the empty string"),
    ] {
        assert_eq!(
            CCA::from_str(name),
            Err(quiche::Error::CongestionControl),
            "expected {name:?} to be rejected with Error::CongestionControl ({why})"
        );
    }
    for (name, want) in [("reno", CCA::Reno), ("cubic", CCA::CUBIC), ("bbr", CCA::BBR)] {
        assert_eq!(
            CCA::from_str(name),
            Ok(want),
            "expected the three controllers step 05 established to keep their names"
        );
    }

    // 2. The discriminant. BBR2 is the fourth variant and takes the next
    //    value; the enum is `#[repr(C)]` and the C API hands these numbers
    //    across the boundary, so the three that came before must not move.
    assert_eq!(
        (
            CCA::Reno as i32,
            CCA::CUBIC as i32,
            CCA::BBR as i32,
            CCA::BBR2 as i32,
        ),
        (0, 1, 2, 3),
        "expected the congestion-control discriminants to read (Reno, CUBIC, BBR, BBR2) = \
         (0, 1, 2, 3). These cross the C boundary as plain integers, so renumbering the \
         existing three to make room for the fourth silently changes what every C caller \
         selects."
    );

    // 3. Both selection paths reach it, and a transfer under it completes.
    for (how, by_name) in [("set_cc_algorithm_name(\"bbr2\")", true), ("set_cc_algorithm(BBR2)", false)] {
        let mut cc = pair::client_config(pair::ALPN_HQ);
        let mut sc = pair::server_config(pair::ALPN_HQ);
        if by_name {
            cc.set_cc_algorithm_name("bbr2")
                .unwrap_or_else(|e| panic!("{how} on a client Config: {e:?}"));
            sc.set_cc_algorithm_name("bbr2")
                .unwrap_or_else(|e| panic!("{how} on a server Config: {e:?}"));
        } else {
            cc.set_cc_algorithm(CCA::BBR2);
            sc.set_cc_algorithm(CCA::BBR2);
        }

        let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
        p.handshake();

        // `cwnd` is a per-path number and lives on `PathStats`, not on the
        // connection-wide `Stats`. The pair has exactly one path here.
        let initial_cwnd = active_cwnd(&p.client);
        assert!(
            initial_cwnd > 0,
            "expected a positive congestion window on a handshaken connection selected via \
             {how}; observed {initial_cwnd}"
        );

        // A congestion-controlled connection does not accept a quarter of a
        // megabyte in one call: `stream_send` truncates at the connection's
        // send capacity, which is the congestion window less what is already
        // in flight, and returns `Err(Done)` when that capacity is zero.
        // Feeding the payload in over repeated rounds is both the only way to
        // move it and the thing that opens the window, which is what the
        // assertion after it is about.
        const LEN: usize = 256 * 1024;
        let want = testkit::body(LEN, 0x1020_0002);
        let mut sent = 0usize;
        let mut got: Vec<u8> = Vec::new();
        let mut fin = false;
        let mut rounds = 0usize;
        while sent < LEN || !fin {
            rounds += 1;
            assert!(
                rounds <= 4096,
                "the transfer under {how} stalled after {rounds} rounds with {sent} of {LEN} \
                 bytes accepted and {} delivered",
                got.len()
            );
            if sent < LEN {
                match p.client.stream_send(4, &want[sent..], false) {
                    Ok(n) => sent += n,
                    // Zero send capacity. Settling below collects the
                    // acknowledgements that reopen it.
                    Err(quiche::Error::Done) => {},
                    Err(e) => {
                        panic!("stream_send under {how} after {sent} of {LEN} bytes: {e:?}")
                    },
                }
                if sent == LEN {
                    // Written exactly once, and separately, because a
                    // truncated `stream_send` drops the `fin` it was given.
                    p.client
                        .stream_send(4, &[], true)
                        .expect("mark the stream finished");
                }
            }
            p.settle(8);
            let (chunk, f) = pair::read_stream(&mut p.server, 4);
            got.extend_from_slice(&chunk);
            fin |= f;
        }
        assert!(
            got == want,
            "expected {LEN} bytes to arrive intact under the controller selected via {how}; \
             observed {} bytes",
            got.len()
        );

        let stats = p.client.stats();
        assert!(
            stats.sent_bytes >= LEN as u64,
            "expected the client to have put at least the {LEN} payload bytes on the wire \
             under {how}; observed sent_bytes={}",
            stats.sent_bytes
        );
        let cwnd = active_cwnd(&p.client);
        assert!(
            cwnd >= initial_cwnd,
            "expected the congestion window not to shrink below its initial value over a \
             lossless {LEN}-byte transfer under {how}; observed it fall from {initial_cwnd} \
             to {cwnd}. A controller that never opens its window is not controlling \
             congestion."
        );
    }
}

// ---------------------------------------------------------------------------
// cap-scid-renames  (T3)
// ---------------------------------------------------------------------------

/// `cap-scid-renames`: the four connection-ID functions that changed name at
/// this milestone do the job their old spellings did, and the names that did
/// **not** change are still there.
///
/// `active_source_cids`, `new_source_cid`, `retire_destination_cid` and
/// `source_cids_left` became `active_scids`, `new_scid`, `retire_dcid` and
/// `scids_left`. That looks like a systematic `source` → `scid` sweep, and it
/// is not: `source_ids()`, `retired_scids()`, `retired_scid_next()` and
/// `available_dcids()` all keep the names step 09 gave them. The second half
/// of this group reads the survivors, so a candidate who applies the rename
/// with a blanket text substitution fails here rather than at some later
/// step where the cause is no longer obvious.
#[test]
fn cap_scid_renames() {
    // The pair's shared limit is 2; this group needs room for two extra IDs
    // so it can watch the counter run out, so it builds its own configs.
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.set_active_connection_id_limit(3);
    sc.set_active_connection_id_limit(3);

    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let original = p.client.source_id().into_owned();

    // --- the renamed four ---------------------------------------------------

    assert_eq!(
        p.client.active_scids(),
        1,
        "expected a freshly handshaken connection to have exactly one active source \
         connection ID -- the one it started with"
    );
    assert_eq!(
        p.client.scids_left(), 2,
        "with an active_connection_id_limit of 3 and one ID in use, expected room for two \
         more; observed {}",
        p.client.scids_left()
    );

    let (cid1, token1) = pair::spare_cid(0x51);
    assert_eq!(
        p.client.new_scid(&cid1, token1, false),
        Ok(1),
        "expected the first additional source connection ID to be issued sequence number 1 \
         (the handshake ID holds 0)"
    );
    p.settle(16);

    assert_eq!(
        p.client.active_scids(), 2,
        "expected the issued ID to count as active immediately; observed {}",
        p.client.active_scids()
    );
    assert_eq!(
        p.client.scids_left(), 1,
        "expected the room remaining to fall by one as an ID is issued; observed {}",
        p.client.scids_left()
    );
    assert_eq!(
        p.server.available_dcids(), 1,
        "expected the NEW_CONNECTION_ID frame to reach the server and leave it one spare \
         destination ID; observed {}",
        p.server.available_dcids()
    );

    let (cid2, token2) = pair::spare_cid(0x52);
    assert_eq!(
        p.client.new_scid(&cid2, token2, false),
        Ok(2),
        "expected the second additional ID to be issued sequence number 2"
    );
    p.settle(16);
    assert_eq!(
        (p.client.active_scids(), p.client.scids_left(), p.server.available_dcids()),
        (3, 0, 2),
        "expected the limit to be exhausted after two additional IDs: three active, none \
         left, both spares delivered"
    );

    let (cid3, token3) = pair::spare_cid(0x53);
    assert_eq!(
        p.client.new_scid(&cid3, token3, false),
        Err(quiche::Error::IdLimit),
        "expected issuing past the peer's active_connection_id_limit to fail with \
         Error::IdLimit rather than silently overwriting a live ID"
    );

    // Retiring runs from the other side: the server retires the destination
    // ID it was given at sequence 0, which is the client's original source ID.
    assert_eq!(
        p.server.retire_dcid(0),
        Ok(()),
        "expected the server to be able to retire destination connection ID 0 while it holds \
         spares"
    );
    p.settle(16);

    assert_eq!(
        p.client.retired_scids(), 1,
        "expected the RETIRE_CONNECTION_ID frame to reach the client and leave exactly one \
         retired source ID; observed {}",
        p.client.retired_scids()
    );
    assert_eq!(
        p.client.active_scids(), 2,
        "expected the retired ID to stop counting as active; observed {}",
        p.client.active_scids()
    );

    assert_eq!(
        p.server.retire_dcid(9),
        Err(quiche::Error::InvalidState),
        "expected retiring a sequence number that was never issued to fail with \
         Error::InvalidState"
    );

    // --- the names that did NOT change -------------------------------------

    assert_eq!(
        p.client.retired_scid_next(),
        Some(original),
        "expected `retired_scid_next` -- which kept its name -- to hand back the retired \
         connection ID itself, not merely a count"
    );
    assert_eq!(
        p.client.retired_scid_next(),
        None,
        "expected the retired-ID queue to drain: a second call must come back empty"
    );

    let active: Vec<Vec<u8>> = p.client.source_ids().map(|c| c.to_vec()).collect();
    assert_eq!(
        active.len(),
        p.client.active_scids(),
        "expected `source_ids()` -- which kept its name -- to yield exactly as many IDs as \
         `active_scids()` counts; observed {} from the iterator against {} from the counter",
        active.len(),
        p.client.active_scids()
    );
    let unique: BTreeSet<Vec<u8>> = active.iter().cloned().collect();
    assert_eq!(
        unique.len(),
        active.len(),
        "expected every active source connection ID to be distinct; observed {} entries \
         collapsing to {} distinct values",
        active.len(),
        unique.len()
    );
    assert!(
        active.iter().any(|c| c.as_slice() == cid1.as_ref())
            && active.iter().any(|c| c.as_slice() == cid2.as_ref()),
        "expected both issued IDs to appear among the active ones"
    );

    // The connection still works after all of that: retiring the ID the
    // handshake used must not strand the peer.
    let want = testkit::body(4096, 0x1020_0003);
    let (got, fin) = p.client_to_server(4, &want, true);
    assert!(
        got == want && fin,
        "expected the connection to keep carrying data after a connection-ID rotation; \
         observed {} of {} bytes, fin={fin}",
        got.len(),
        want.len()
    );
}

// ---------------------------------------------------------------------------
// cap-stream-reset-stop-counts  (T3)
// ---------------------------------------------------------------------------

/// `cap-stream-reset-stop-counts`: four new `Stats` counters, and the
/// asymmetry RFC 9000 Section 3.5 creates between the two endpoints of one
/// connection.
///
/// The counters are of *frames*, not of API calls, so one endpoint's `local`
/// is the other endpoint's `remote` and the two readings differ by exactly
/// the number of resets each side originated. A candidate that counts calls
/// to `stream_shutdown`, or that folds both directions into a single field,
/// produces a symmetric table and fails here.
#[test]
fn cap_stream_reset_stop_counts() {
    /// The four counters as one tuple, in a fixed order, so a mismatch prints
    /// the whole table rather than the first field that happened to differ.
    fn table(conn: &quiche::Connection) -> (u64, u64, u64, u64) {
        let s = conn.stats();
        (
            s.reset_stream_count_local,
            s.reset_stream_count_remote,
            s.stopped_stream_count_local,
            s.stopped_stream_count_remote,
        )
    }
    const ORDER: &str =
        "(reset_local, reset_remote, stopped_local, stopped_remote)";
    const STREAMS: [u64; 4] = [0, 2, 4, 8];
    const ERR: u64 = 1001;

    // --- reset path --------------------------------------------------------

    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();
    for id in STREAMS {
        p.client.stream_send(id, b"a", false).expect("stream_send");
    }
    p.settle(32);

    assert_eq!(
        (table(&p.client), table(&p.server)),
        ((0, 0, 0, 0), (0, 0, 0, 0)),
        "expected all four counters to read zero on both ends of a connection that has only \
         carried stream data; the order is {ORDER}"
    );

    p.client.stream_shutdown(0, quiche::Shutdown::Write, ERR).expect("client shutdown write");
    p.settle(32);
    assert_eq!(
        table(&p.client),
        (1, 0, 0, 0),
        "after the client resets one stream, expected the client to count it under \
         reset_stream_count_*local*; the order is {ORDER}"
    );
    assert_eq!(
        table(&p.server),
        (0, 1, 0, 0),
        "the very same RESET_STREAM frame must count under the server's \
         reset_stream_count_*remote*: local and remote name who originated the frame, not \
         who is reading the counter. The order is {ORDER}"
    );

    p.server.stream_shutdown(0, quiche::Shutdown::Write, ERR).expect("server shutdown write");
    p.settle(32);
    assert_eq!(
        (table(&p.client), table(&p.server)),
        ((1, 1, 0, 0), (1, 1, 0, 0)),
        "once each side has reset the same stream once, expected both ends to read 1/1. \
         The order is {ORDER}"
    );

    for id in [2, 4, 8] {
        p.client.stream_shutdown(id, quiche::Shutdown::Write, ERR).expect("client shutdown write");
    }
    p.settle(32);
    for id in [4, 8] {
        p.server.stream_shutdown(id, quiche::Shutdown::Write, ERR).expect("server shutdown write");
    }
    p.settle(32);

    assert_eq!(
        table(&p.client),
        (4, 3, 0, 0),
        "the client originated four resets and received three, so expected 4 local and 3 \
         remote. The order is {ORDER}"
    );
    assert_eq!(
        table(&p.server),
        (3, 4, 0, 0),
        "the server originated three resets and received four, so expected exactly the \
         mirror of the client's reading. Two endpoints of one connection disagree here, by \
         exactly the number of resets each originated. The order is {ORDER}"
    );

    // --- stop path, on a fresh pair -----------------------------------------

    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();
    for id in STREAMS {
        p.client.stream_send(id, b"a", false).expect("stream_send");
    }
    p.settle(32);

    p.server.stream_shutdown(0, quiche::Shutdown::Read, ERR).expect("server shutdown read");
    p.settle(32);
    assert_eq!(
        table(&p.server),
        (0, 1, 1, 0),
        "one STOP_SENDING from the server moves two of the server's counters: it originated \
         the stop (stopped_local) and it received the reset the client is required to send \
         back (reset_remote). The order is {ORDER}"
    );
    assert_eq!(
        table(&p.client),
        (1, 0, 0, 1),
        "and the exact mirror at the client: it received the stop (stopped_remote) and \
         originated the reset RFC 9000 Section 3.5 requires in answer (reset_local). One API \
         call on one side moved a counter under each name on both sides. The order is {ORDER}"
    );

    for id in [2, 4, 8] {
        p.server.stream_shutdown(id, quiche::Shutdown::Read, ERR).expect("server shutdown read");
    }
    p.settle(32);

    assert_eq!(
        table(&p.server),
        (0, 4, 4, 0),
        "after four STOP_SENDINGs, expected the server to read four stopped-local and four \
         reset-remote, and zero of the other two. The order is {ORDER}"
    );
    assert_eq!(
        table(&p.client),
        (4, 0, 0, 4),
        "and the client to read exactly the transpose. A single counter shared between the \
         two directions, or a counter incremented by the API call rather than by the frame, \
         cannot produce this table. The order is {ORDER}"
    );
}

// ---------------------------------------------------------------------------
// cap-path-challenge-queue  (T3)
// ---------------------------------------------------------------------------

/// `cap-path-challenge-queue`: `Stats::path_challenge_rx_count`, and the
/// `Config` setter that bounds how many challenges one path will hold.
///
/// The counter is incremented **on receipt of the frame**, before the path
/// decides whether it has room to queue it. That is the whole point of the
/// group, and it is observable without touching the private queue: set the
/// bound to one, deliver four challenges on a single path before the peer
/// answers any of them, and the count still reads four. A candidate that
/// increments the counter where it queues -- the natural place to put it --
/// reads one.
///
/// What this group deliberately does **not** assert is a *behavioural*
/// consequence of the bound. Measured against the reference: with the bound
/// at one and four challenges outstanding, the path still validates, because
/// the one challenge that was queued still produces a PATH_RESPONSE and one
/// response is all validation needs. The discard is genuinely invisible from
/// outside; only the counter's disagreement with it is visible.
#[test]
fn cap_path_challenge_queue() {
    /// Take exactly one datagram from `conn` for the given 4-tuple and hand it
    /// to `peer`. Returns the number of bytes and the descriptor that came
    /// back, so the caller can check which path the packet actually went out
    /// on rather than which one it asked for.
    fn one_packet(
        conn: &mut quiche::Connection, peer: &mut quiche::Connection,
        from: Option<std::net::SocketAddr>, to: Option<std::net::SocketAddr>,
    ) -> (usize, quiche::SendInfo) {
        let mut buf = [0u8; pair::MAX_DATAGRAM];
        let (len, info) = conn
            .send_on_path(&mut buf, from, to)
            .unwrap_or_else(|e| panic!("send_on_path({from:?}, {to:?}): {e:?}"));
        let mut d = buf[..len].to_vec();
        peer.recv(&mut d, quiche::RecvInfo { from: info.from, to: info.to })
            .unwrap_or_else(|e| panic!("peer recv of a {len} byte datagram: {e:?}"));
        (len, info)
    }

    let client_addr = pair::client_addr();
    let server_addr = pair::server_addr();
    let client_addr_2 = pair::client_addr_2();
    let client_addr_3 = pair::unknown_addr();
    let server_addr_2: std::net::SocketAddr =
        "127.0.0.1:44998".parse().expect("a second server address");

    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.set_active_connection_id_limit(4);
    sc.set_active_connection_id_limit(4);
    // One challenge per path at a time: the bound this milestone made settable.
    sc.set_path_challenge_recv_max_queue_len(1);

    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    // Each new path consumes a connection ID, so issue three spares each way.
    for i in 0..3u8 {
        let (c_cid, c_tok) = pair::spare_cid(0x61 + i);
        let (s_cid, s_tok) = pair::spare_cid(0x71 + i);
        p.client.new_scid(&c_cid, c_tok, true).expect("client new_scid");
        p.server.new_scid(&s_cid, s_tok, true).expect("server new_scid");
    }
    p.settle(32);
    assert!(
        p.client.available_dcids() >= 3,
        "expected the client to hold at least three spare destination connection IDs before \
         opening three additional paths; observed {}",
        p.client.available_dcids()
    );

    assert_eq!(
        p.server.stats().path_challenge_rx_count,
        0,
        "expected a handshaken connection that has exchanged connection IDs but probed no \
         path to have received no PATH_CHALLENGE"
    );

    // Nothing is pending on the original path, so asking for a packet on it
    // is Done -- not an empty packet, and not a probe misdelivered here.
    let mut buf = [0u8; pair::MAX_DATAGRAM];
    assert_eq!(
        p.client.send_on_path(&mut buf, Some(client_addr), Some(server_addr)),
        Err(quiche::Error::Done),
        "expected send_on_path on the active path with nothing pending to return \
         Err(Error::Done)"
    );

    // --- one challenge, one increment --------------------------------------

    assert_eq!(
        p.client.probe_path(client_addr_2, server_addr),
        Ok(1),
        "expected probing a second local address to return the sequence number of the \
         destination connection ID it claimed"
    );
    let (len, info) = one_packet(&mut p.client, &mut p.server, Some(client_addr_2), Some(server_addr));
    assert_eq!(
        (info.from, info.to),
        (client_addr_2, server_addr),
        "expected the probe to leave from the address it is probing"
    );
    assert!(
        len >= 1200,
        "expected the PATH_CHALLENGE to be padded to at least the 1200-byte anti-amplification \
         minimum RFC 9000 Section 8.2.1 requires of a path probe; observed {len} bytes"
    );
    assert_eq!(
        p.server.stats().path_challenge_rx_count,
        1,
        "expected the first PATH_CHALLENGE to bring the server's count to exactly 1"
    );

    // --- a 4-tuple that is not a path of this connection --------------------

    assert_eq!(
        p.client.send_on_path(&mut buf, Some(client_addr_3), Some(server_addr)),
        Err(quiche::Error::InvalidState),
        "expected a local address that is not part of any path to return \
         Err(Error::InvalidState)"
    );
    assert_eq!(
        p.client.send_on_path(&mut buf, Some(client_addr), Some(server_addr_2)),
        Err(quiche::Error::InvalidState),
        "expected a peer address that is not part of any path to return \
         Err(Error::InvalidState) too -- the check is on the 4-tuple, not on either half"
    );

    // --- challenges count, stream data does not -----------------------------

    p.client.probe_path(client_addr, server_addr_2).expect("probe a second peer address");
    p.client.probe_path(client_addr_3, server_addr).expect("probe a third local address");
    // Small enough to fit beside the probes without splitting across packets.
    p.client.stream_send(0, &testkit::body(1201, 0x1020_0004), true).expect("stream_send");

    // A probe leaves the original local address for the new peer address.
    let (len, info) = one_packet(&mut p.client, &mut p.server, Some(client_addr), None);
    assert_eq!((info.from, info.to), (client_addr, server_addr_2));
    assert!(len >= 1200, "expected a padded probe; observed {len} bytes");
    assert_eq!(
        p.server.stats().path_challenge_rx_count,
        2,
        "expected the second PATH_CHALLENGE to bring the count to 2"
    );

    // The next packet on that same local address is the stream data, on the
    // original path, and it must not touch the counter.
    let (_, info) = one_packet(&mut p.client, &mut p.server, Some(client_addr), None);
    assert_eq!(
        (info.from, info.to),
        (client_addr, server_addr),
        "expected the stream data to go out on the original path"
    );
    assert_eq!(
        p.server.stats().path_challenge_rx_count,
        2,
        "expected a STREAM frame on the active path to leave path_challenge_rx_count alone. \
         The counter is of PATH_CHALLENGE frames, not of packets and not of paths."
    );

    let (len, info) = one_packet(&mut p.client, &mut p.server, None, Some(server_addr));
    assert_eq!((info.from, info.to), (client_addr_3, server_addr));
    assert!(len >= 1200, "expected a padded probe; observed {len} bytes");
    assert_eq!(
        p.server.stats().path_challenge_rx_count,
        3,
        "expected the third PATH_CHALLENGE to bring the count to 3"
    );

    // --- the count runs past the queue bound --------------------------------
    //
    // The server's queue holds one challenge per path. Re-probing the same
    // 4-tuple three more times, delivering each before the server is ever
    // given a chance to answer, puts four challenges on one path against a
    // bound of one. Three of them cannot be queued. All four are counted.
    let before = p.server.stats().path_challenge_rx_count;
    for round in 0..3 {
        p.client.probe_path(client_addr_2, server_addr).expect("re-probe");
        let (_, info) =
            one_packet(&mut p.client, &mut p.server, Some(client_addr_2), Some(server_addr));
        assert_eq!(
            (info.from, info.to),
            (client_addr_2, server_addr),
            "expected re-probe {round} to go out on the path being probed"
        );
    }
    assert_eq!(
        p.server.stats().path_challenge_rx_count,
        before + 3,
        "expected all three re-probes to be counted even though the receiving path was \
         configured to queue only one challenge at a time and was never given the chance to \
         drain it. path_challenge_rx_count is incremented where the frame is parsed, before \
         the path decides whether it has room -- a counter incremented at the point of \
         queueing would have stopped at {before}."
    );

    // The connection is still healthy after all that: the probed path
    // validates and the stream data arrives.
    p.settle(64);
    assert_eq!(
        p.client.is_path_validated(client_addr_2, server_addr),
        Ok(true),
        "expected the probed path to validate: the one challenge the bound did allow through \
         is answered, and one PATH_RESPONSE is all validation needs"
    );
    let (got, fin) = pair::read_stream(&mut p.server, 0);
    assert!(
        got.len() == 1201 && fin,
        "expected the 1201 bytes of stream data sent alongside the probes to arrive intact; \
         observed {} bytes, fin={fin}",
        got.len()
    );
}

// ---------------------------------------------------------------------------
// cap-peer-transport-params  (T3)
// ---------------------------------------------------------------------------

/// `cap-peer-transport-params`: the negotiated transport parameters of the
/// *peer* become readable, and the connection-wide `Stats` struct is now
/// exactly the fourteen counters that are genuinely connection-wide.
///
/// Before this milestone an application could read back only what it had
/// configured itself. `peer_transport_params` is the other half: what the
/// other end asked for. It returns `None` until the parameters have actually
/// been parsed, which makes the pre-handshake reading as much a part of the
/// contract as the post-handshake one -- an implementation that hands back a
/// `Default` before the peer has spoken is reporting RFC defaults as though
/// they were the peer's choices.
#[test]
fn cap_peer_transport_params() {
    // Values chosen so that each side's are distinctive and neither matches a
    // default: the group reads them back across the connection, so a stub
    // that echoes the caller's own configuration fails.
    const C_MAX_DATA: u64 = 3_000_001;
    const S_MAX_DATA: u64 = 7_000_002;
    const C_STREAMS_BIDI: u64 = 33;
    const S_STREAMS_BIDI: u64 = 77;
    const C_IDLE: u64 = 21_000;
    const S_IDLE: u64 = 22_000;
    // `max_udp_payload_size` and `active_conn_id_limit` are both set by the
    // harness's shared tuning, so the group gives each side its own value
    // rather than asserting the shared one back: a cross-read is worth more
    // than an equality against a number both Configs happen to carry.
    const C_UDP_PAYLOAD: u64 = 1_391;
    const S_UDP_PAYLOAD: u64 = 1_409;
    const C_CID_LIMIT: u64 = 5;
    const S_CID_LIMIT: u64 = 6;

    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.set_initial_max_data(C_MAX_DATA);
    cc.set_initial_max_streams_bidi(C_STREAMS_BIDI);
    cc.set_max_idle_timeout(C_IDLE);
    cc.set_disable_active_migration(true);
    cc.set_max_recv_udp_payload_size(C_UDP_PAYLOAD as usize);
    cc.set_active_connection_id_limit(C_CID_LIMIT);
    sc.set_initial_max_data(S_MAX_DATA);
    sc.set_initial_max_streams_bidi(S_STREAMS_BIDI);
    sc.set_max_idle_timeout(S_IDLE);
    sc.set_disable_active_migration(false);
    sc.set_max_recv_udp_payload_size(S_UDP_PAYLOAD as usize);
    sc.set_active_connection_id_limit(S_CID_LIMIT);

    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);

    // 1. Nothing to report before the peer has been heard from.
    assert!(
        p.client.peer_transport_params().is_none(),
        "expected peer_transport_params() to be None on a connection whose handshake has not \
         begun: the peer has sent nothing, so there is nothing to report. Returning a \
         Default here would report RFC 9000 Section 18.2 defaults as the peer's own choices."
    );
    assert!(
        p.server.peer_transport_params().is_none(),
        "expected the server's peer_transport_params() to be None before the handshake too"
    );

    p.handshake();

    // 2. Each side reads the *other* side's configured values.
    let at_client = p
        .client
        .peer_transport_params()
        .expect("expected the client to have parsed the server's transport parameters after a \
                 completed handshake")
        .clone();
    let at_server = p
        .server
        .peer_transport_params()
        .expect("expected the server to have parsed the client's transport parameters after a \
                 completed handshake")
        .clone();

    assert_eq!(
        (at_client.initial_max_data, at_client.initial_max_streams_bidi, at_client.max_idle_timeout),
        (S_MAX_DATA, S_STREAMS_BIDI, S_IDLE),
        "expected the client to read back the *server's* configured values. Observing the \
         client's own ({C_MAX_DATA}, {C_STREAMS_BIDI}, {C_IDLE}) means the accessor is \
         reporting local configuration under a name that promises the peer's."
    );
    assert_eq!(
        (at_server.initial_max_data, at_server.initial_max_streams_bidi, at_server.max_idle_timeout),
        (C_MAX_DATA, C_STREAMS_BIDI, C_IDLE),
        "and the server to read back the client's"
    );
    assert_eq!(
        (at_client.disable_active_migration, at_server.disable_active_migration),
        (false, true),
        "expected the migration flag to cross in both directions independently: the server \
         disabled nothing and the client disabled migration, so each reads the other's answer"
    );

    // 3. The two parameters the harness's shared tuning also sets cross the
    //    same way. They are read separately from the block above because a
    //    `TransportParams` that carried *some* of the peer's answers and its
    //    own defaults for the rest would pass that block and fail this one.
    assert_eq!(
        (at_client.max_udp_payload_size, at_client.active_conn_id_limit),
        (S_UDP_PAYLOAD, S_CID_LIMIT),
        "expected the client to read the server's max_udp_payload_size and \
         active_conn_id_limit, not its own ({C_UDP_PAYLOAD}, {C_CID_LIMIT})"
    );
    assert_eq!(
        (at_server.max_udp_payload_size, at_server.active_conn_id_limit),
        (C_UDP_PAYLOAD, C_CID_LIMIT),
        "and the server to read the client's"
    );

    // 4. Parameters neither side set carry RFC 9000 Section 18.2's defaults,
    //    not zero. A struct zeroed on construction gets these wrong.
    assert_eq!(
        (
            at_client.ack_delay_exponent,
            at_client.max_ack_delay,
            at_server.ack_delay_exponent,
            at_server.max_ack_delay,
        ),
        (3, 25, 3, 25),
        "expected the two parameters no Config in this group touches to carry their RFC 9000 \
         Section 18.2 defaults -- ack_delay_exponent 3 and max_ack_delay 25 -- rather than \
         zero. A struct zeroed on construction and filled in only from the parameters that \
         appeared on the wire reads (0, 0) here, because a peer that accepts the defaults \
         sends neither."
    );

    // 5. `initial_source_connection_id` is the peer's, and it is the one this
    //    connection is actually talking to.
    assert_eq!(
        at_client.initial_source_connection_id.as_deref(),
        Some(p.client.destination_id().as_ref()),
        "expected the server's advertised initial_source_connection_id to be the very \
         connection ID the client is addressing it by"
    );
    assert_eq!(
        at_client.retry_source_connection_id, None,
        "no Retry was performed, so expected retry_source_connection_id to be absent"
    );
    assert_eq!(
        at_client.max_datagram_frame_size, None,
        "neither side enabled the DATAGRAM extension, so expected its parameter to be absent \
         rather than present-and-zero"
    );

    // 6. `Stats` is the connection-wide fourteen. Per-path numbers -- cwnd,
    //    rtt, pmtu and the rest -- moved to `PathStats` and must not have
    //    been left behind here; the destructuring is exhaustive, so a
    //    fifteenth field or a missing one fails to compile.
    let want_paths = p.client.path_stats().count() as usize;
    let quiche::Stats {
        recv,
        sent,
        lost,
        retrans,
        sent_bytes,
        recv_bytes,
        lost_bytes,
        stream_retrans_bytes,
        paths_count,
        reset_stream_count_local,
        stopped_stream_count_local,
        reset_stream_count_remote,
        stopped_stream_count_remote,
        path_challenge_rx_count,
    } = p.client.stats();

    assert!(
        recv > 0 && sent > 0 && recv_bytes > 0 && sent_bytes > 0,
        "expected a handshaken connection to have both sent and received packets and bytes; \
         observed recv={recv} sent={sent} recv_bytes={recv_bytes} sent_bytes={sent_bytes}"
    );
    assert_eq!(
        (lost, retrans, lost_bytes, stream_retrans_bytes),
        (0, 0, 0, 0),
        "expected nothing lost or retransmitted on a lossless in-memory handshake"
    );
    assert_eq!(
        paths_count, want_paths,
        "expected paths_count to agree with the number of paths path_stats() yields"
    );
    assert_eq!(
        (
            reset_stream_count_local,
            stopped_stream_count_local,
            reset_stream_count_remote,
            stopped_stream_count_remote,
            path_challenge_rx_count,
        ),
        (0, 0, 0, 0, 0),
        "expected the five counters this milestone added to read zero on a connection that \
         has done nothing but handshake"
    );
}

// ---------------------------------------------------------------------------
// cap-ffi-paths  (T1)
// ---------------------------------------------------------------------------

/// The C program `cap-ffi-paths` compiles, links and runs.
///
/// `__CERT__` and `__KEY__` are replaced with the verifier's own fixture paths
/// before it is written out; nothing else in it is generated.
const FFI_PATHS_PROGRAM: &str = r#"/* struct timespec, inet_pton: the header uses both and -std=c99 hides them. */
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <quiche.h>

#define CERT "__CERT__"
#define KEY  "__KEY__"

static uint8_t buf[65535];

static void die(const char *what, long code) {
    printf("fail=%s\nfail_code=%ld\n", what, code);
    fflush(stdout);
    exit(1);
}

static void mkaddr(struct sockaddr_in *a, const char *ip, uint16_t port) {
    memset(a, 0, sizeof(*a));
    a->sin_family = AF_INET;
    a->sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &a->sin_addr) != 1) die("inet_pton", 0);
}

/* Hand every packet `from` wants to send to `to`, addressed exactly as the
 * sender asked. Returns how many packets moved. */
static int pump(quiche_conn *from, quiche_conn *to) {
    int n = 0;
    for (;;) {
        quiche_send_info si;
        memset(&si, 0, sizeof(si));
        ssize_t w = quiche_conn_send(from, buf, sizeof(buf), &si);
        if (w == QUICHE_ERR_DONE) break;
        if (w < 0) die("conn_send", (long) w);
        quiche_recv_info ri;
        ri.from = (struct sockaddr *) &si.from;
        ri.from_len = si.from_len;
        ri.to = (struct sockaddr *) &si.to;
        ri.to_len = si.to_len;
        ssize_t r = quiche_conn_recv(to, buf, (size_t) w, &ri);
        if (r < 0) die("conn_recv", (long) r);
        n++;
    }
    return n;
}

static void settle(quiche_conn *c, quiche_conn *s) {
    int i;
    for (i = 0; i < 24; i++) {
        int n = pump(c, s);
        n += pump(s, c);
        if (n == 0) return;
    }
}

static void common_config(quiche_config *c) {
    if (quiche_config_set_application_protos(c, (uint8_t *) "\x02h3", 3) < 0)
        die("set_application_protos", 0);
    quiche_config_verify_peer(c, false);
    quiche_config_set_active_connection_id_limit(c, 4);
}

/* The client and the server are given deliberately different numbers, so that
 * a peer_transport_params that echoed local configuration back would print the
 * wrong ones. */
static quiche_config *client_config(void) {
    quiche_config *c = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (c == NULL) die("config_new_client", 0);
    common_config(c);
    quiche_config_set_max_idle_timeout(c, 30000);
    quiche_config_set_initial_max_data(c, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_local(c, 200000);
    quiche_config_set_initial_max_stream_data_bidi_remote(c, 200000);
    quiche_config_set_initial_max_stream_data_uni(c, 200000);
    quiche_config_set_initial_max_streams_bidi(c, 100);
    quiche_config_set_initial_max_streams_uni(c, 100);
    return c;
}

static quiche_config *server_config(void) {
    quiche_config *c = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (c == NULL) die("config_new_server", 0);
    if (quiche_config_load_cert_chain_from_pem_file(c, CERT) < 0)
        die("load_cert_chain", 0);
    if (quiche_config_load_priv_key_from_pem_file(c, KEY) < 0)
        die("load_priv_key", 0);
    common_config(c);
    quiche_config_set_max_idle_timeout(c, 31007);
    quiche_config_set_initial_max_data(c, 1300009);
    quiche_config_set_initial_max_stream_data_bidi_local(c, 210011);
    quiche_config_set_initial_max_stream_data_bidi_remote(c, 220013);
    quiche_config_set_initial_max_stream_data_uni(c, 230017);
    quiche_config_set_initial_max_streams_bidi(c, 111);
    quiche_config_set_initial_max_streams_uni(c, 113);
    return c;
}

static size_t count_source_ids(quiche_conn *conn, size_t *first_len) {
    quiche_connection_id_iter *it = quiche_conn_source_ids(conn);
    if (it == NULL) die("source_ids_null", 0);
    const uint8_t *id = NULL;
    size_t id_len = 0;
    size_t n = 0;
    *first_len = 0;
    while (quiche_connection_id_iter_next(it, &id, &id_len)) {
        if (n == 0) *first_len = id_len;
        n++;
        if (n > 64) die("source_ids_runaway", 0);
    }
    quiche_connection_id_iter_free(it);
    return n;
}

int main(void) {
    struct sockaddr_in ca, sa, ca2, unknown;
    mkaddr(&ca, "127.0.0.1", 4433);
    mkaddr(&sa, "127.0.0.1", 4434);
    mkaddr(&ca2, "127.0.0.2", 4435);
    mkaddr(&unknown, "127.0.0.9", 9999);

    uint8_t ccid[QUICHE_MAX_CONN_ID_LEN], scid[QUICHE_MAX_CONN_ID_LEN];
    memset(ccid, 0x11, sizeof(ccid));
    memset(scid, 0x22, sizeof(scid));

    uint8_t token[16];
    memset(token, 0x44, sizeof(token));

    /* ----- part A: a client that has not yet heard from the server ----- */
    quiche_config *cc = client_config();
    quiche_conn *cl = quiche_connect("quic.test", ccid, sizeof(ccid),
                                     (struct sockaddr *) &ca, sizeof(ca),
                                     (struct sockaddr *) &sa, sizeof(sa), cc);
    if (cl == NULL) die("connect", 0);

    quiche_send_info si;
    memset(&si, 0, sizeof(si));
    ssize_t w = quiche_conn_send(cl, buf, sizeof(buf), &si);
    if (w < 0) die("initial_send", (long) w);
    printf("initial_len=%zd\n", w);

    quiche_transport_params tp;
    memset(&tp, 0xAA, sizeof(tp));
    printf("ptp_before=%d\n", quiche_conn_peer_transport_params(cl, &tp) ? 1 : 0);

    printf("a_active_scids=%zu\n", quiche_conn_active_scids(cl));
    printf("a_retired_scids=%zu\n", quiche_conn_retired_scids(cl));
    printf("a_available_dcids=%zu\n", quiche_conn_available_dcids(cl));

    size_t first_len = 0;
    printf("a_source_ids=%zu\n", count_source_ids(cl, &first_len));
    printf("a_source_id_len=%zu\n", first_len);

    uint64_t pseq = 12345;
    printf("a_probe_rc=%d\n",
           quiche_conn_probe_path(cl, (struct sockaddr *) &ca2, sizeof(ca2),
                                  (struct sockaddr *) &sa, sizeof(sa), &pseq));
    printf("a_validated_unknown=%d\n",
           quiche_conn_is_path_validated(cl, (struct sockaddr *) &unknown,
                                         sizeof(unknown),
                                         (struct sockaddr *) &sa, sizeof(sa)));
    printf("a_retire_unissued_rc=%d\n", quiche_conn_retire_dcid(cl, 7));

    quiche_conn_free(cl);
    quiche_config_free(cc);

    /* ----- part B: a handshaken pair ----- */
    cc = client_config();
    quiche_config *sc = server_config();
    cl = quiche_connect("quic.test", ccid, sizeof(ccid),
                        (struct sockaddr *) &ca, sizeof(ca),
                        (struct sockaddr *) &sa, sizeof(sa), cc);
    if (cl == NULL) die("connect_b", 0);
    quiche_conn *sv = quiche_accept(scid, sizeof(scid), NULL, 0,
                                    (struct sockaddr *) &sa, sizeof(sa),
                                    (struct sockaddr *) &ca, sizeof(ca), sc);
    if (sv == NULL) die("accept", 0);

    settle(cl, sv);
    printf("b_client_established=%d\n", quiche_conn_is_established(cl) ? 1 : 0);
    printf("b_server_established=%d\n", quiche_conn_is_established(sv) ? 1 : 0);

    memset(&tp, 0xAA, sizeof(tp));
    printf("b_ptp_after=%d\n", quiche_conn_peer_transport_params(cl, &tp) ? 1 : 0);
    printf("tp_max_idle_timeout=%llu\n", (unsigned long long) tp.peer_max_idle_timeout);
    printf("tp_max_udp_payload_size=%llu\n", (unsigned long long) tp.peer_max_udp_payload_size);
    printf("tp_initial_max_data=%llu\n", (unsigned long long) tp.peer_initial_max_data);
    printf("tp_bidi_local=%llu\n", (unsigned long long) tp.peer_initial_max_stream_data_bidi_local);
    printf("tp_bidi_remote=%llu\n", (unsigned long long) tp.peer_initial_max_stream_data_bidi_remote);
    printf("tp_uni=%llu\n", (unsigned long long) tp.peer_initial_max_stream_data_uni);
    printf("tp_streams_bidi=%llu\n", (unsigned long long) tp.peer_initial_max_streams_bidi);
    printf("tp_streams_uni=%llu\n", (unsigned long long) tp.peer_initial_max_streams_uni);
    printf("tp_ack_delay_exponent=%llu\n", (unsigned long long) tp.peer_ack_delay_exponent);
    printf("tp_max_ack_delay=%llu\n", (unsigned long long) tp.peer_max_ack_delay);

    printf("b_scids_left_0=%zu\n", quiche_conn_scids_left(cl));
    printf("b_active_scids_0=%zu\n", quiche_conn_active_scids(cl));

    uint8_t n1[QUICHE_MAX_CONN_ID_LEN], n2[QUICHE_MAX_CONN_ID_LEN];
    memset(n1, 0x33, sizeof(n1));
    memset(n2, 0x55, sizeof(n2));

    uint64_t seq1 = 999, seq2 = 999;
    printf("b_new_scid_rc1=%d\n",
           quiche_conn_new_scid(cl, n1, sizeof(n1), token, false, &seq1));
    printf("b_new_scid_seq1=%llu\n", (unsigned long long) seq1);
    printf("b_scids_left_1=%zu\n", quiche_conn_scids_left(cl));
    printf("b_active_scids_1=%zu\n", quiche_conn_active_scids(cl));

    printf("b_new_scid_rc2=%d\n",
           quiche_conn_new_scid(cl, n2, sizeof(n2), token, false, &seq2));
    printf("b_new_scid_seq2=%llu\n", (unsigned long long) seq2);
    printf("b_active_scids_2=%zu\n", quiche_conn_active_scids(cl));
    printf("b_source_ids=%zu\n", count_source_ids(cl, &first_len));

    /* the server issues two of its own so the client has spare destinations */
    uint8_t s1[QUICHE_MAX_CONN_ID_LEN], s2[QUICHE_MAX_CONN_ID_LEN];
    memset(s1, 0x66, sizeof(s1));
    memset(s2, 0x77, sizeof(s2));
    uint64_t sseq = 999;
    if (quiche_conn_new_scid(sv, s1, sizeof(s1), token, false, &sseq) != 0)
        die("server_new_scid_1", 0);
    if (quiche_conn_new_scid(sv, s2, sizeof(s2), token, false, &sseq) != 0)
        die("server_new_scid_2", 0);
    settle(cl, sv);
    printf("b_available_dcids=%zu\n", quiche_conn_available_dcids(cl));

    /* now a probe of a brand new 4-tuple can be issued */
    pseq = 12345;
    printf("b_probe_rc=%d\n",
           quiche_conn_probe_path(cl, (struct sockaddr *) &ca2, sizeof(ca2),
                                  (struct sockaddr *) &sa, sizeof(sa), &pseq));
    printf("b_validated_before=%d\n",
           quiche_conn_is_path_validated(cl, (struct sockaddr *) &ca2,
                                         sizeof(ca2),
                                         (struct sockaddr *) &sa, sizeof(sa)));
    settle(cl, sv);
    printf("b_validated_after=%d\n",
           quiche_conn_is_path_validated(cl, (struct sockaddr *) &ca2,
                                         sizeof(ca2),
                                         (struct sockaddr *) &sa, sizeof(sa)));
    printf("b_validated_unknown=%d\n",
           quiche_conn_is_path_validated(cl, (struct sockaddr *) &unknown,
                                         sizeof(unknown),
                                         (struct sockaddr *) &sa, sizeof(sa)));
    /* retiring a destination the server issued, and the server's own count */
    printf("b_retire_dcid_rc=%d\n", quiche_conn_retire_dcid(cl, 0));
    settle(cl, sv);
    printf("b_server_retired_scids=%zu\n", quiche_conn_retired_scids(sv));
    printf("b_retire_unissued_rc=%d\n", quiche_conn_retire_dcid(cl, 31));

    /* ----- part C: HTTP/3 event numbering ----- */
    quiche_h3_config *h3c = quiche_h3_config_new();
    if (h3c == NULL) die("h3_config_new", 0);
    quiche_h3_conn *ch3 = quiche_h3_conn_new_with_transport(cl, h3c);
    if (ch3 == NULL) die("h3_conn_client", 0);
    quiche_h3_conn *sh3 = quiche_h3_conn_new_with_transport(sv, h3c);
    if (sh3 == NULL) die("h3_conn_server", 0);
    settle(cl, sv);

    quiche_h3_header req[4] = {
        { (const uint8_t *) ":method", 7, (const uint8_t *) "GET", 3 },
        { (const uint8_t *) ":scheme", 7, (const uint8_t *) "https", 5 },
        { (const uint8_t *) ":authority", 10, (const uint8_t *) "quic.test", 9 },
        { (const uint8_t *) ":path", 5, (const uint8_t *) "/", 1 },
    };
    int64_t stream = quiche_h3_send_request(ch3, cl, req, 4, false);
    if (stream < 0) die("h3_send_request", (long) stream);
    settle(cl, sv);

    quiche_h3_event *ev = NULL;
    int64_t sid = quiche_h3_conn_poll(sh3, sv, &ev);
    if (sid < 0) die("poll_headers", (long) sid);
    printf("c_headers_type=%d\n", (int) quiche_h3_event_type(ev));
    quiche_h3_event_free(ev);

    quiche_h3_priority prio = { 3, false };
    if (quiche_h3_send_priority_update_for_request(ch3, cl, (uint64_t) stream,
                                                   &prio) < 0)
        die("h3_send_priority_update", 0);
    settle(cl, sv);
    ev = NULL;
    sid = quiche_h3_conn_poll(sh3, sv, &ev);
    if (sid < 0) die("poll_priority_update", (long) sid);
    printf("c_priority_update_type=%d\n", (int) quiche_h3_event_type(ev));
    quiche_h3_event_free(ev);

    if (quiche_h3_send_goaway(sh3, sv, 0) < 0) die("h3_send_goaway", 0);
    settle(cl, sv);
    ev = NULL;
    sid = quiche_h3_conn_poll(ch3, cl, &ev);
    if (sid < 0) die("poll_goaway", (long) sid);
    printf("c_goaway_type=%d\n", (int) quiche_h3_event_type(ev));
    quiche_h3_event_free(ev);

    if (quiche_conn_stream_shutdown(cl, (uint64_t) stream, QUICHE_SHUTDOWN_WRITE,
                                    0x100) < 0)
        die("stream_shutdown_write", 0);
    if (quiche_conn_stream_shutdown(cl, (uint64_t) stream, QUICHE_SHUTDOWN_READ,
                                    0x100) < 0)
        die("stream_shutdown_read", 0);
    settle(cl, sv);
    ev = NULL;
    sid = quiche_h3_conn_poll(sh3, sv, &ev);
    if (sid < 0) die("poll_reset", (long) sid);
    printf("c_reset_type=%d\n", (int) quiche_h3_event_type(ev));
    quiche_h3_event_free(ev);

    quiche_h3_conn_free(ch3);
    quiche_h3_conn_free(sh3);
    quiche_h3_config_free(h3c);
    quiche_conn_free(cl);
    quiche_conn_free(sv);
    quiche_config_free(cc);
    quiche_config_free(sc);

    printf("done=1\n");
    return 0;
}
"#;

/// `cap-ffi-paths`: the C API grows the path, connection-ID and
/// transport-parameter entry points, and the HTTP/3 event numbering the header
/// has always declared finally matches the integers the library returns.
///
/// Every value below comes out of a compiled, linked, executed C program. The
/// link line is `-lpthread -ldl -lm -lstdc++` and nothing else, so a header
/// that declares a function the library never exported fails at the link step
/// with the linker's own message rather than silently passing.
///
/// Two deliberate omissions, both measured rather than assumed:
///
/// * The program reads only the ten leading `uint64_t` members of
///   `quiche_transport_params`. The reference's Rust mirror of that struct is
///   not `#[repr(C)]`, so the compiler is free to place the `bool` wherever it
///   likes and the three members after `peer_max_ack_delay` do not read back
///   as what the peer sent. Asserting on them would be asserting on a layout
///   nobody promised: a candidate that wrote `#[repr(C)]` and a candidate that
///   did not would both be right by the only contract that exists, and only
///   one of them would pass. The ten that are read agree under either layout.
/// * `quiche_conn_stats` is not touched here. Its Rust mirror *is*
///   `#[repr(C)]`, and `cap-peer-transport-params` already reads the fourteen
///   fields through the Rust API, where the exhaustive destructuring makes a
///   fifteenth field a compile error rather than a silent pass.
#[test]
fn cap_ffi_paths() {
    let dir = testkit::scratch("cap-ffi-paths");
    let libdir = build_quiche_ffi(&dir);

    let tls = testkit::tls();
    let program = FFI_PATHS_PROGRAM
        .replace("__CERT__", &tls.cert.display().to_string())
        .replace("__KEY__", &tls.key.display().to_string());

    let (out, raw) = c_probe(&dir, "ffi_paths", &program, &libdir);

    let get = |k: &str| -> i64 {
        let v = out.get(k).unwrap_or_else(|| {
            panic!(
                "expected the C program to print a `{k}=` line; observed none. It stops at the \
                 first thing that goes wrong and prints `fail=<what>` when it does.\n\
                 --- everything it printed ---\n{raw}"
            )
        });
        v.parse::<i64>().unwrap_or_else(|e| {
            panic!("`{k}={v}` is not a number ({e}).\n--- everything it printed ---\n{raw}")
        })
    };

    assert_eq!(
        out.get("done").map(String::as_str),
        Some("1"),
        "expected the C program to reach the end of main and print `done=1`; observed that it \
         did not.\n--- everything it printed ---\n{raw}"
    );

    // --- part A: a client that has not yet heard from the server ----------

    assert!(
        get("initial_len") >= 1200,
        "expected quiche_conn_send() to hand back a padded Initial of at least \
         QUICHE_MIN_CLIENT_INITIAL_LEN (1200) bytes; observed {}.\n{raw}",
        get("initial_len")
    );
    assert_eq!(
        get("ptp_before"),
        0,
        "expected quiche_conn_peer_transport_params() to return false on a client that has \
         sent its Initial and received nothing: there are no peer parameters yet. This is the \
         C mirror of the Rust accessor returning None, and a candidate that returns true with \
         a zeroed struct is reporting defaults as the peer's choices.\n{raw}"
    );
    assert_eq!(
        (get("a_active_scids"), get("a_retired_scids"), get("a_available_dcids")),
        (1, 0, 0),
        "expected a fresh client to hold exactly its own one source connection ID, to have \
         retired none, and to have no spare destination IDs.\n{raw}"
    );
    assert_eq!(
        (get("a_source_ids"), get("a_source_id_len")),
        (1, 20),
        "expected quiche_conn_source_ids() to yield exactly one ID of QUICHE_MAX_CONN_ID_LEN \
         bytes and then stop. The iterator has to walk the connection's real ID set: a \
         quiche_connection_id_iter_next() that always returns true runs away, and one that \
         always returns false yields nothing.\n{raw}"
    );
    assert_eq!(
        get("a_probe_rc"),
        -18,
        "expected quiche_conn_probe_path() on a client with no spare destination connection \
         IDs to return QUICHE_ERR_OUT_OF_IDENTIFIERS (-18): a probe of a new 4-tuple needs an \
         unused destination ID to address it with.\n{raw}"
    );
    assert_eq!(
        get("a_validated_unknown"),
        -6,
        "expected quiche_conn_is_path_validated() on a 4-tuple the connection has never used \
         to return QUICHE_ERR_INVALID_STATE (-6) rather than crash or answer false; the \
         header says so in as many words.\n{raw}"
    );
    assert_eq!(
        get("a_retire_unissued_rc"),
        -18,
        "expected quiche_conn_retire_dcid() to fail on a client holding a single destination \
         ID and no spare to replace it with, and to fail with \
         QUICHE_ERR_OUT_OF_IDENTIFIERS (-18) -- the spare is checked before the sequence \
         number is looked up.\n{raw}"
    );

    // --- part B: a handshaken pair ----------------------------------------

    assert_eq!(
        (get("b_client_established"), get("b_server_established")),
        (1, 1),
        "expected the C program's in-memory client and server to complete the handshake.\n{raw}"
    );
    assert_eq!(
        get("b_ptp_after"),
        1,
        "expected quiche_conn_peer_transport_params() to return true once the peer's \
         parameters have been parsed.\n{raw}"
    );

    // The server was configured with numbers the client never uses, so an
    // implementation that handed back local configuration reads the client's.
    for (k, want) in [
        ("tp_max_idle_timeout", 31007),
        ("tp_max_udp_payload_size", 65527),
        ("tp_initial_max_data", 1300009),
        ("tp_bidi_local", 210011),
        ("tp_bidi_remote", 220013),
        ("tp_uni", 230017),
        ("tp_streams_bidi", 111),
        ("tp_streams_uni", 113),
        ("tp_ack_delay_exponent", 3),
        ("tp_max_ack_delay", 25),
    ] {
        assert_eq!(
            get(k),
            want,
            "expected the client to read the *server's* {k} as {want}. The client's own \
             configuration uses different numbers throughout (30000, 1000000, 200000, 100), \
             so reading one of those means the accessor is reporting local configuration; \
             65527, 3 and 25 are RFC 9000 Section 18.2 defaults for parameters the server \
             never set, and reading zero for one of those means the struct was zeroed rather \
             than filled.\n{raw}"
        );
    }

    assert_eq!(
        (get("b_scids_left_0"), get("b_active_scids_0")),
        (3, 1),
        "expected a handshaken client that advertised an active_connection_id_limit of 4 and \
         holds one ID of its own to report three still issuable and one active.\n{raw}"
    );
    assert_eq!(
        (get("b_new_scid_rc1"), get("b_new_scid_seq1")),
        (0, 1),
        "expected quiche_conn_new_scid() to succeed and to write sequence number 1 through \
         its out-parameter: the connection's original ID is sequence 0, so the first issued \
         one is 1.\n{raw}"
    );
    assert_eq!(
        (get("b_scids_left_1"), get("b_active_scids_1")),
        (2, 2),
        "expected issuing one ID to move the pair from (3 left, 1 active) to (2 left, 2 \
         active): one out of the allowance, one into the active set.\n{raw}"
    );
    assert_eq!(
        (get("b_new_scid_rc2"), get("b_new_scid_seq2"), get("b_active_scids_2")),
        (0, 2, 3),
        "expected a second issued ID to take sequence number 2 and the active count to \
         3.\n{raw}"
    );
    assert_eq!(
        get("b_source_ids"),
        3,
        "expected quiche_conn_source_ids() to yield all three active IDs after two were \
         issued -- it iterates the connection's state, it does not snapshot one \
         element.\n{raw}"
    );
    assert_eq!(
        get("b_available_dcids"),
        2,
        "expected the client to see two spare destination IDs after the server issued two of \
         its own and the pair was driven.\n{raw}"
    );
    assert_eq!(
        (get("b_probe_rc"), get("b_validated_before"), get("b_validated_after")),
        (0, 0, 1),
        "expected quiche_conn_probe_path() of a new 4-tuple to succeed once a spare \
         destination ID exists, quiche_conn_is_path_validated() to answer false (0) for that \
         4-tuple before the challenge is answered, and true (1) after. A stub that always \
         answers true validates a path that was never probed.\n{raw}"
    );
    assert_eq!(
        get("b_validated_unknown"),
        -6,
        "expected quiche_conn_is_path_validated() to still return QUICHE_ERR_INVALID_STATE \
         (-6) for a 4-tuple that is not a path of this connection, even once other paths \
         exist.\n{raw}"
    );
    assert_eq!(
        (get("b_retire_dcid_rc"), get("b_server_retired_scids")),
        (0, 1),
        "expected quiche_conn_retire_dcid(0) to succeed now that a replacement exists, and \
         the server to count exactly one retired source ID once the frame arrives.\n{raw}"
    );
    assert_eq!(
        get("b_retire_unissued_rc"),
        -6,
        "expected quiche_conn_retire_dcid() on a sequence number that was never issued to \
         return QUICHE_ERR_INVALID_STATE (-6) rather than succeed.\n{raw}"
    );

    // --- part C: the HTTP/3 event numbering -------------------------------

    assert_eq!(
        (
            get("c_headers_type"),
            get("c_goaway_type"),
            get("c_reset_type"),
            get("c_priority_update_type"),
        ),
        (0, 3, 4, 5),
        "expected quiche_h3_event_type() to return the integers the header's \
         `enum quiche_h3_event_type` declares: HEADERS 0, GOAWAY 3, RESET 4, PRIORITY_UPDATE \
         5. The previous milestone's library returned 4, 5 and 6 for the last three while its \
         header declared 3, 4 and 5, so every C caller that switched on the enum took the \
         wrong branch. Nothing here reads the header or the Rust source: the events come out \
         of quiche_h3_conn_poll() on a real in-memory HTTP/3 exchange.\n{raw}"
    );
}

// --- cap-mtu-updated-qlog --------------------------------------------------

/// The probe program `cap_mtu_updated_qlog` builds against the candidate's
/// `qlog` package.
///
/// `__OUT__` is replaced with the scratch directory the two `.sqlog` files are
/// written into. The program only ever reports what the streamer told it; the
/// structural judgement is made by the Rust test over the bytes on disk.
const MTU_QLOG_PROGRAM: &str = r#"use std::fs::File;

use qlog::events::connectivity::MtuUpdated;
use qlog::events::quic::MetricsUpdated;
use qlog::events::{Event, EventData, EventImportance};
use qlog::streamer::QlogStreamer;
use qlog::{Configuration, TraceSeq, VantagePoint, VantagePointType};

const OUT: &str = "__OUT__";

fn trace() -> TraceSeq {
    TraceSeq::new(
        VantagePoint { name: None, ty: VantagePointType::Client, flow: None },
        Some("mtu".to_string()),
        Some("mtu probe".to_string()),
        Some(Configuration { time_offset: Some(0.0), original_uris: None }),
        None,
    )
}

/// An event this package pinned three milestones ago, at importance Core, so
/// that a streamer which writes nothing at all is distinguishable from one
/// that correctly filters a single Extra event out.
fn control() -> EventData {
    EventData::MetricsUpdated(MetricsUpdated {
        min_rtt: None,
        smoothed_rtt: None,
        latest_rtt: None,
        rtt_variance: None,
        pto_count: None,
        congestion_window: Some(14520),
        bytes_in_flight: None,
        ssthresh: None,
        packets_in_flight: None,
        pacing_rate: None,
    })
}

fn streamer(name: &str, level: EventImportance) -> QlogStreamer {
    let path = std::path::Path::new(OUT).join(name);
    let w = Box::new(File::create(&path).expect("create sqlog"));
    QlogStreamer::new(
        qlog::QLOG_VERSION.to_string(),
        Some("mtu".to_string()),
        Some("mtu probe".to_string()),
        None,
        std::time::Instant::now(),
        trace(),
        level,
        w,
    )
}

fn mtu_new_only() -> EventData {
    EventData::MtuUpdated(MtuUpdated { old: None, new: 1350, done: None })
}

fn mtu_all_three() -> EventData {
    EventData::MtuUpdated(MtuUpdated { old: Some(1200), new: 1452, done: Some(true) })
}

fn main() {
    println!("qlog_version={}", qlog::QLOG_VERSION);

    let mut extra = streamer("extra.sqlog", EventImportance::Extra);
    extra.start_log().expect("start_log extra");
    println!("extra_new_only_ok={}", extra.add_event(Event::with_time(1.0, mtu_new_only())).is_ok());
    println!("extra_all_three_ok={}", extra.add_event(Event::with_time(2.0, mtu_all_three())).is_ok());
    println!("extra_control_ok={}", extra.add_event(Event::with_time(3.0, control())).is_ok());
    extra.finish_log().expect("finish_log extra");

    let mut core = streamer("core.sqlog", EventImportance::Core);
    core.start_log().expect("start_log core");
    let r = core.add_event(Event::with_time(1.0, mtu_new_only()));
    println!("core_mtu_ok={}", r.is_ok());
    println!("core_mtu_err={:?}", r.err());
    println!("core_control_ok={}", core.add_event(Event::with_time(2.0, control())).is_ok());
    core.finish_log().expect("finish_log core");

    println!("done=1");
}
"#;

/// The new `connectivity:mtu_updated` event, serialised by the candidate's own
/// `qlog` streamer and read back off disk.
///
/// This is a T4 group and it is deliberately structural. Nothing here greps
/// the emitted file for a substring: every record is parsed as JSON and
/// compared against a fixed expectation table — the exact key set of each
/// record and of each record's `data` object, and the exact value of every
/// member. A serialiser that emitted `"old": null` for the absent field, or
/// spelled the event `connectivity:mtu_update`, or wrote the three members in
/// a nested object, produces a file a substring search would still accept and
/// this group rejects.
///
/// Two independent things are asserted, and the second is the one that costs
/// work to get right:
///
/// * `MtuUpdated` serialises under the name `connectivity:mtu_updated`, and
///   `old` and `done` are *omitted* when unset rather than written as null.
/// * `MtuUpdated` carries importance `Extra`, so a streamer configured at
///   `Core` filters it out and `add_event` reports `Error::Done`. The control
///   event is a `recovery:metrics_updated` this package pinned at milestone
///   06; it is `Core` and survives both streamers. Without it, a streamer that
///   silently dropped *everything* would look identical to one that filters
///   correctly.
///
/// The header record is checked against exactly what milestone 06 specified,
/// because the streamer's own preamble is the part most likely to be broken by
/// a change to the event enum underneath it.
#[test]
fn cap_mtu_updated_qlog() {
    let dir = testkit::scratch("cap-mtu-updated-qlog");
    let program = MTU_QLOG_PROGRAM.replace("__OUT__", &dir.display().to_string());
    let probe = write_probe(&dir, "qlog_mtu_probe", "qlog", &program);
    let (build, exe) = build_probe(&dir, &probe, "qlog-mtu-build");
    assert!(
        build.ok(),
        "expected a program depending on the candidate's `qlog` package by path to build \
         against it. It emits two `qlog::events::connectivity::MtuUpdated` events through a \
         `QlogStreamer` and one `MetricsUpdated` as a control.\n{}",
        build.output()
    );

    let (out, raw) = run_probe(&dir, &exe, "qlog-mtu-run");

    let kv = |k: &str| -> &str {
        out.get(k)
            .map(String::as_str)
            .unwrap_or_else(|| panic!("expected the probe to print `{k}=`.\n{raw}"))
    };

    assert_eq!(kv("done"), "1", "expected the probe to reach the end of main.\n{raw}");
    assert_eq!(
        kv("qlog_version"),
        "0.3",
        "expected `qlog::QLOG_VERSION` to still be the \"0.3\" this package pinned at \
         milestone 06.\n{raw}"
    );

    // The streamer's own report, before anything is read off disk. Both
    // `MtuUpdated` writes are accepted at `Extra`; the first is rejected at
    // `Core` with `Error::Done`, which is how the streamer says "filtered".
    assert_eq!(
        (kv("extra_new_only_ok"), kv("extra_all_three_ok"), kv("extra_control_ok")),
        ("true", "true", "true"),
        "expected a streamer at EventImportance::Extra to accept both MtuUpdated events and \
         the MetricsUpdated control.\n{raw}"
    );
    assert_eq!(
        (kv("core_mtu_ok"), kv("core_mtu_err"), kv("core_control_ok")),
        ("false", "Some(Done)", "true"),
        "expected a streamer at EventImportance::Core to reject MtuUpdated with Error::Done — \
         the event's importance is Extra — while still accepting the Core MetricsUpdated \
         control. A streamer that accepted the MtuUpdated here would be treating the new \
         event as Core; one that rejected the control too would be dropping everything.\n{raw}"
    );

    // --- the Extra stream, record by record -------------------------------

    let extra_path = dir.join("extra.sqlog");
    let extra_raw = std::fs::read(&extra_path)
        .unwrap_or_else(|e| panic!("expected the probe to write {}: {e}\n{raw}", extra_path.display()));
    let extra = jsonseq_records(&extra_raw, "extra.sqlog");
    assert_eq!(
        extra.len(),
        4,
        "expected extra.sqlog to hold the header record plus the three events the probe \
         emitted; observed {} records.\n{raw}",
        extra.len()
    );

    let head = &extra[0];
    assert_eq!(
        head.get("qlog_version").and_then(Json::as_str),
        Some("0.3"),
        "expected the JSON-SEQ header record this package pinned at milestone 06: \
         `qlog_version` \"0.3\". Observed header: {head:?}"
    );
    assert_eq!(
        head.get("qlog_format").and_then(Json::as_str),
        Some("JSON-SEQ"),
        "expected the header record's `qlog_format` to be \"JSON-SEQ\". Observed header: \
         {head:?}"
    );
    assert!(
        head.get("traces").is_none(),
        "expected the streaming header to carry a single `trace`, not the `traces` array of \
         the buffered format. Observed header keys: {:?}",
        head.keys()
    );

    let event_keys: BTreeSet<String> =
        ["data", "name", "time"].iter().map(|s| s.to_string()).collect();

    for (n, want_name) in [
        (1usize, "connectivity:mtu_updated"),
        (2, "connectivity:mtu_updated"),
        (3, "recovery:metrics_updated"),
    ] {
        let ev = &extra[n];
        assert_eq!(
            ev.keys(),
            event_keys,
            "expected extra.sqlog event record {n} to carry exactly the keys {event_keys:?} \
             that milestone 06 pinned for a streamed event. Observed: {ev:?}"
        );
        assert_eq!(
            ev.get("name").and_then(Json::as_str),
            Some(want_name),
            "expected extra.sqlog event record {n} to be named `{want_name}`. Observed: {ev:?}"
        );
    }

    // The event written with only `new` set. `old` and `done` are `None`, and
    // an omitted optional member must not appear at all — not as `null`.
    let one = &extra[1];
    let one_data = one.get("data").expect("event record 1 has a `data` object");
    assert_eq!(
        one_data.keys(),
        ["new"].iter().map(|s| s.to_string()).collect::<BTreeSet<String>>(),
        "expected the MtuUpdated emitted with only `new` set to serialise a `data` object \
         holding exactly `new`; an unset `old` or `done` is omitted, not written as null. \
         Observed: {one_data:?}"
    );
    assert_eq!(
        one_data.get("new").and_then(Json::as_u64),
        Some(1350),
        "expected `data.new` to be the 1350 the probe passed. Observed: {one_data:?}"
    );

    // The event written with all three members set.
    let all = &extra[2];
    let all_data = all.get("data").expect("event record 2 has a `data` object");
    assert_eq!(
        all_data.keys(),
        ["done", "new", "old"].iter().map(|s| s.to_string()).collect::<BTreeSet<String>>(),
        "expected the MtuUpdated emitted with all three members set to serialise a `data` \
         object holding exactly `old`, `new` and `done`, flat — not nested under a further \
         object. Observed: {all_data:?}"
    );
    assert_eq!(
        (
            all_data.get("old").and_then(Json::as_u64),
            all_data.get("new").and_then(Json::as_u64),
            all_data.get("done"),
        ),
        (Some(1200), Some(1452), Some(&Json::Bool(true))),
        "expected `data` to carry old 1200, new 1452 and done true, with `done` a JSON \
         boolean rather than a string or a number. Observed: {all_data:?}"
    );

    // --- the Core stream: the same two writes, one of them filtered -------

    let core_path = dir.join("core.sqlog");
    let core_raw = std::fs::read(&core_path)
        .unwrap_or_else(|e| panic!("expected the probe to write {}: {e}\n{raw}", core_path.display()));
    let core = jsonseq_records(&core_raw, "core.sqlog");
    let core_names: Vec<&str> =
        core.iter().skip(1).filter_map(|e| e.get("name").and_then(Json::as_str)).collect();
    assert_eq!(
        core_names,
        vec!["recovery:metrics_updated"],
        "expected the streamer at EventImportance::Core to write the Core control event and \
         no record at all for the Extra MtuUpdated. Observed event names: {core_names:?}"
    );
    assert_eq!(
        core.len(),
        2,
        "expected core.sqlog to hold the header record and the one surviving event; observed \
         {} records.",
        core.len()
    );
}
