//! new-capability groups: what this milestone can do that the previous one could not
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `cap-initial-keys` is `fn cap_initial_keys()`.
//! The driver runs each one on its own with `--exact` and asserts that
//! exactly one test executed, so a renamed, `#[ignore]`d or accidentally
//! deleted function scores zero for that group rather than passing silently.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.

use std::collections::BTreeMap;
use std::collections::BTreeSet;
use std::path::Path;
use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/// Where the first byte of `got` stops matching `want`, for an assert message.
fn first_diff(want: &[u8], got: &[u8]) -> String {
    match want.iter().zip(got.iter()).position(|(a, b)| a != b) {
        Some(off) => format!(
            "first difference at offset {off}: expected {}, observed {}",
            testkit::hex(&want[off..(off + 16).min(want.len())]),
            testkit::hex(&got[off..(off + 16).min(got.len())])
        ),
        None => format!(
            "the first {} bytes agree (expected {} bytes in total, observed {})",
            want.len().min(got.len()),
            want.len(),
            got.len()
        ),
    }
}

// --- the smallest manifest reader that can answer a layout question --------

/// The `key = "value"` pairs of one `[table]` of a TOML manifest.
///
/// Deliberately tiny, and deliberately not a general TOML parser: a T4 layout
/// group asks three questions — which table is this, what is `name`, what is
/// in `members` — and answering them by hand keeps the harness free of a TOML
/// dependency. Array values are returned joined by `\u{1}` so that `members`
/// can be split back out by the caller.
fn manifest_table(text: &str, table: &str) -> BTreeMap<String, String> {
    let mut out = BTreeMap::new();
    let mut inside = false;
    let mut pending: Option<(String, String)> = None;

    for raw in text.lines() {
        let line = raw.split('#').next().unwrap_or("").trim();
        if line.is_empty() {
            continue;
        }
        if let Some((key, acc)) = pending.as_mut() {
            acc.push(' ');
            acc.push_str(line);
            if acc.contains(']') {
                let v = acc.clone();
                let k = key.clone();
                out.insert(k, toml_array(&v));
                pending = None;
            }
            continue;
        }
        if line.starts_with('[') {
            inside = line == format!("[{table}]");
            continue;
        }
        if !inside {
            continue;
        }
        let Some((k, v)) = line.split_once('=') else { continue };
        let (k, v) = (k.trim().to_string(), v.trim().to_string());
        if v.starts_with('[') && !v.contains(']') {
            pending = Some((k, v));
            continue;
        }
        if v.starts_with('[') {
            out.insert(k, toml_array(&v));
        } else {
            out.insert(k, v.trim_matches('"').to_string());
        }
    }
    out
}

/// `[ "a", "b" ]` -> `a\u{1}b`.
fn toml_array(v: &str) -> String {
    let inner = v.trim_start_matches('[').trim_end_matches(']');
    inner
        .split(',')
        .map(|s| s.trim().trim_matches('"').trim())
        .filter(|s| !s.is_empty())
        .collect::<Vec<_>>()
        .join("\u{1}")
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

    fn as_bool(&self) -> Option<bool> {
        match self {
            Json::Bool(b) => Some(*b),
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

/// The single file in `dir` whose name starts with `prefix`.
fn one_file(dir: &Path, prefix: &str) -> std::path::PathBuf {
    let mut hits: Vec<_> = std::fs::read_dir(dir)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", dir.display()))
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| {
            p.file_name()
                .and_then(|n| n.to_str())
                .map(|n| n.starts_with(prefix) && n.ends_with(".sqlog"))
                .unwrap_or(false)
        })
        .collect();
    hits.sort();
    assert_eq!(
        hits.len(),
        1,
        "expected exactly one {prefix}*.sqlog in {}; observed {:?}",
        dir.display(),
        std::fs::read_dir(dir)
            .map(|d| d.filter_map(|e| e.ok()).map(|e| e.file_name()).collect::<Vec<_>>())
            .unwrap_or_default()
    );
    hits.remove(0)
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// cap-workspace-layout  (T4)
// ---------------------------------------------------------------------------

/// `cap-workspace-layout`: the repository is a cargo workspace whose members
/// are `quiche`, `qlog` and `apps`, and the crate root has moved out of the
/// repository root.
///
/// Structural facts about the produced tree only: which manifests exist, what
/// they declare themselves to be, and which executables the build left behind.
/// No source file is read and nothing is grepped for an identifier.
#[test]
fn cap_workspace_layout() {
    let root = testkit::candidate_root();

    // 1. The root manifest is a workspace manifest.
    let root_manifest = root.join("Cargo.toml");
    let text = std::fs::read_to_string(&root_manifest)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", root_manifest.display()));
    let ws = manifest_table(&text, "workspace");
    let members: Vec<String> = ws
        .get("members")
        .unwrap_or_else(|| {
            panic!(
                "{} has no [workspace] table with a `members` key. At this milestone the \
                 repository root is a workspace manifest that builds no code of its own.",
                root_manifest.display()
            )
        })
        .split('\u{1}')
        .map(|s| s.to_string())
        .collect();

    for want in ["quiche", "qlog", "apps"] {
        assert!(
            members.iter().any(|m| m == want),
            "the workspace at {} does not list `{want}` among its members; observed {members:?}",
            root_manifest.display()
        );
    }

    // The repository still carries `fuzz/` and `tools/http3_test/` -- real
    // nested crates that are not workspace members. Without `exclude` naming
    // them, cargo treats an unlisted nested manifest as a path it should have
    // been told about; a workspace that dropped the line is not the same
    // workspace the upstream restructuring produced.
    let excluded: Vec<String> = ws
        .get("exclude")
        .unwrap_or_else(|| {
            panic!(
                "{} has no `exclude` key in [workspace]. `fuzz` and `tools/http3_test` are real \
                 nested crates under this tree that are not workspace members; they must be \
                 named in `exclude` so cargo does not treat them as unlisted workspace paths.",
                root_manifest.display()
            )
        })
        .split('\u{1}')
        .map(|s| s.to_string())
        .collect();
    for want in ["fuzz", "tools/http3_test"] {
        assert!(
            excluded.iter().any(|e| e == want),
            "the workspace at {} does not exclude `{want}`; observed exclude = {excluded:?}",
            root_manifest.display()
        );
    }

    let pkg = manifest_table(&text, "package");
    assert!(
        pkg.get("name").is_none(),
        "{} declares a [package] called {:?} as well as a [workspace]. The root of this \
         milestone's repository is a virtual workspace: the library is the `quiche` member.",
        root_manifest.display(),
        pkg.get("name")
    );

    // 2. The crate moved rather than being copied.
    assert!(
        !root.join("src").join("lib.rs").exists(),
        "{} still exists. The library root moved to quiche/src/lib.rs at this milestone; a tree \
         that has both has two copies of the crate, not a workspace.",
        root.join("src").join("lib.rs").display()
    );

    // 3. Each member declares the package it is supposed to be.
    for (dir, want_name) in [("quiche", "quiche"), ("qlog", "qlog"), ("apps", "quiche_apps")] {
        let m = root.join(dir).join("Cargo.toml");
        let t = std::fs::read_to_string(&m)
            .unwrap_or_else(|e| panic!("cannot read {}: {e}", m.display()));
        let p = manifest_table(&t, "package");
        let got = p.get("name").cloned().unwrap_or_default();
        assert_eq!(
            got, want_name,
            "{} declares package name {got:?}; the workspace member `{dir}` must be the package \
             `{want_name}`",
            m.display()
        );
    }

    // 3b. The transport crate declares the release this milestone *is*. A tree
    //     with the workspace shape whose `quiche` package still calls itself
    //     0.9.0 has moved the files without taking the version bump, and the
    //     version field is the only place in the tree that records which
    //     release the candidate believes it has produced.
    {
        let m = root.join("quiche").join("Cargo.toml");
        let t = std::fs::read_to_string(&m)
            .unwrap_or_else(|e| panic!("cannot read {}: {e}", m.display()));
        let got = manifest_table(&t, "package")
            .get("version")
            .cloned()
            .unwrap_or_default();
        assert_eq!(
            got, "0.11.0",
            "{} declares version {got:?}. This milestone is quiche 0.11.0 and the transport \
             crate's own manifest is where that is stated.",
            m.display()
        );
    }

    let lib = root.join("quiche").join("src").join("lib.rs");
    assert!(lib.exists(), "the library root {} does not exist", lib.display());

    // 4. The workspace produced the two binaries the milestone is named for,
    //    and they are executables rather than leftover build scripts.
    let target = testkit::candidate_target_dir();
    for name in ["quiche-client", "quiche-server"] {
        let found = testkit::find_bin(&[name]).unwrap_or_else(|| {
            panic!(
                "the workspace did not produce an executable called `{name}`. At this milestone \
                 the `apps` member builds `quiche-client` and `quiche-server` into the \
                 workspace's shared target directory ({}).",
                target.display()
            )
        });
        assert!(
            found.is_file(),
            "{} is not a regular file",
            found.display()
        );
    }
}

// ---------------------------------------------------------------------------
// cap-apps-cli-flags  (T1)
// ---------------------------------------------------------------------------

/// `cap-apps-cli-flags`: the server's listen address, certificate, key,
/// document root and index file all come from the command line, and the client
/// repeats a request as many times as `-n` says.
///
/// Everything asserted here was compiled into the binary at the previous three
/// milestones: `examples/http3-server` took no arguments at all.
#[test]
fn cap_apps_cli_flags() {
    const LEN: usize = 24 * 1024;

    let dir = testkit::scratch("cap-apps-cli-flags");
    let want = testkit::body(LEN, 0x0611_0001);
    let index = b"the index file --index named".to_vec();
    let root = testkit::docroot(
        &dir,
        &[("payload.bin", want.clone()), ("welcome.html", index.clone())],
    );

    let tls = testkit::tls();
    let port = testkit::free_port();
    let server_bin = testkit::need_bin(&["quiche-server"]);
    let client_bin = testkit::need_bin(&["quiche-client"]);

    let server_dump = dir.join("server-dump");
    std::fs::create_dir_all(&server_dump).expect("create --dump-packets directory");
    let mut sc = testkit::cmd(&server_bin);
    sc.arg("--listen")
        .arg(format!("127.0.0.1:{port}"))
        .arg("--cert")
        .arg(&tls.cert)
        .arg("--key")
        .arg(&tls.key)
        .arg("--root")
        .arg(&root)
        .arg("--index")
        .arg("welcome.html")
        .arg("--dump-packets")
        .arg(&server_dump);
    let mut server = testkit::spawn_server(&mut sc, port, &dir, "server", SHORT);

    let base = format!("https://127.0.0.1:{port}");

    // --- one request: the file `--root` named, byte for byte, on stdout ----
    let mut c1 = testkit::cmd(&client_bin);
    c1.arg("--no-verify").arg(format!("{base}/payload.bin"));
    let run = testkit::run(&mut c1, LONG, &dir, "client-one");
    let log = server.log();
    assert!(
        !run.timed_out,
        "expected `quiche-client --no-verify {base}/payload.bin` to finish within {LONG:?}; \
         observed: killed at the deadline having written {} bytes.\n--- client stderr ---\n{}\n\
         --- server log ---\n{log}",
        run.stdout_bytes.len(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected exit status 0; observed {:?}.\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        run.status,
        run.stderr
    );
    assert!(
        want == run.stdout_bytes,
        "the response body is not the {LEN}-byte file in the directory `--root` named, served \
         from the address `--listen` named under the certificate `--cert` named: {}",
        first_diff(&want, &run.stdout_bytes)
    );

    // --- -n 3: three responses, each the whole file ------------------------
    //
    // The three requests are in flight together, so the reference interleaves
    // their bodies on stdout and the concatenation is not three copies of the
    // file. `--dump-responses` is the flag that separates them: one file per
    // request, `payload.bin`, `payload.bin.2`, `payload.bin.3`.
    let dump = dir.join("dump");
    std::fs::create_dir_all(&dump).expect("create --dump-responses directory");
    let mut cc = testkit::cmd(&client_bin);
    cc.arg("--no-verify")
        .arg("-n")
        .arg("3")
        .arg("--dump-responses")
        .arg(&dump)
        .arg(format!("{base}/payload.bin"));
    let run = testkit::run(&mut cc, LONG, &dir, "client-n3");
    let log = server.log();
    assert!(
        !run.timed_out,
        "expected `quiche-client --no-verify -n 3 --dump-responses DIR {base}/payload.bin` to \
         finish within {LONG:?}; observed: killed at the deadline.\n--- client stderr ---\n{}\n\
         --- server log ---\n{log}",
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected exit status 0; observed {:?}.\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        run.status,
        run.stderr
    );

    assert!(
        run.stdout_bytes.is_empty(),
        "`--dump-responses DIR` sends the bodies to files, so nothing may be written to stdout; \
         observed {} bytes there.\n--- server log ---\n{log}",
        run.stdout_bytes.len()
    );

    let mut dumped: Vec<String> = std::fs::read_dir(&dump)
        .expect("read the --dump-responses directory")
        .filter_map(|e| e.ok())
        .map(|e| e.file_name().to_string_lossy().to_string())
        .collect();
    dumped.sort();
    assert_eq!(
        dumped,
        vec![
            "payload.bin".to_string(),
            "payload.bin.2".to_string(),
            "payload.bin.3".to_string()
        ],
        "`-n 3` must send the request three times, so `--dump-responses` must leave three files \
         named after the resource and its cardinal; observed {dumped:?}"
    );
    for name in &dumped {
        let got = std::fs::read(dump.join(name))
            .unwrap_or_else(|e| panic!("cannot read the dumped response {name}: {e}"));
        assert_eq!(
            got.len(),
            LEN,
            "the dumped response {name} is {} bytes, not the {LEN} bytes of the file `--root` \
             named ({})",
            got.len(),
            first_diff(&want, &got)
        );
        assert!(
            want == got,
            "the dumped response {name} is not the file `--root` named: {}",
            first_diff(&want, &got)
        );
    }

    // --- --index: `/` is the file the flag names, not index.html -----------
    let mut ic = testkit::cmd(&client_bin);
    ic.arg("--no-verify").arg(format!("{base}/"));
    let run = testkit::run(&mut ic, LONG, &dir, "client-index");
    run.expect_ok("GET / with --index welcome.html");
    assert_eq!(
        run.stdout_bytes, index,
        "a request for `/` must be served the file `--index` named. Expected {:?}, observed {:?}.\
         \n--- server log ---\n{}",
        String::from_utf8_lossy(&index),
        String::from_utf8_lossy(&run.stdout_bytes),
        server.log()
    );

    // --- a path that is not in the root ------------------------------------
    let mut nc = testkit::cmd(&client_bin);
    nc.arg("--no-verify").arg(format!("{base}/no-such-file.bin"));
    let run = testkit::run(&mut nc, LONG, &dir, "client-404");
    assert_eq!(
        String::from_utf8_lossy(&run.stdout_bytes).trim_end(),
        "Not Found!",
        "a request for a path that is not under `--root` must be answered `Not Found!`; observed \
         {:?}",
        String::from_utf8_lossy(&run.stdout_bytes)
    );

    assert!(
        server.alive(),
        "the server exited while serving three requests.\n--- server log ---\n{}",
        server.log()
    );

    // --- --dump-packets: the server's own incoming datagrams, verbatim -----
    //
    // By now the server above has answered five requests over `--dump-packets
    // server_dump`, so the directory must hold one raw file per datagram it
    // received, and at least one of them -- the very first flight of any of
    // those connections -- must still carry the long-header form bit that
    // every Initial packet sets, because these are dumped before any header
    // is even parsed.
    let dumped_at_server: Vec<Vec<u8>> = std::fs::read_dir(&server_dump)
        .expect("read the --dump-packets directory")
        .filter_map(|e| e.ok())
        .filter_map(|e| std::fs::read(e.path()).ok())
        .collect();
    assert!(
        !dumped_at_server.is_empty(),
        "`--dump-packets DIR` must leave one file per incoming datagram in DIR; observed none in \
         {} after five requests were served",
        server_dump.display()
    );
    assert!(
        dumped_at_server.iter().any(|b| !b.is_empty() && b[0] & 0x80 != 0),
        "none of the {} files `--dump-packets` left behind begins with the long-header form bit \
         set (byte & 0x80); a fresh connection's first flight is always a long-header Initial \
         packet, so the dump cannot be genuine raw incoming datagrams",
        dumped_at_server.len()
    );

    // --- --no-retry and --wire-version: raw bytes on the wire ---------------
    //
    // Both flags are otherwise invisible from either process's own stdout.
    // `--dump-packets` on one side captures the raw datagrams the *other*
    // side put on the wire, byte for byte, before any header is even parsed,
    // and a QUIC long header's version field sits at a fixed,
    // version-independent offset regardless of what that version is (the
    // invariants RFC, 8999). For the one real (non-grease) version either
    // side here ever speaks, the packet-type bits at that same offset mean
    // what RFC 9000 Table 5 says they mean, which is enough to tell a Retry
    // packet from anything else without decrypting a single byte.
    fn parse_long_header(b: &[u8]) -> Option<(u8, u32)> {
        if b.len() < 5 || b[0] & 0x80 == 0 {
            return None;
        }
        let ty = (b[0] >> 4) & 0x3;
        let version = u32::from_be_bytes([b[1], b[2], b[3], b[4]]);
        Some((ty, version))
    }

    fn dumped_headers(dir: &Path) -> Vec<(u8, u32)> {
        let mut entries: Vec<(u64, std::path::PathBuf)> = std::fs::read_dir(dir)
            .unwrap_or_else(|e| panic!("read {}: {e}", dir.display()))
            .filter_map(|e| e.ok())
            .map(|e| e.path())
            .filter_map(|p| {
                let n: u64 = p.file_stem()?.to_str()?.parse().ok()?;
                Some((n, p))
            })
            .collect();
        entries.sort_by_key(|(n, _)| *n);
        entries
            .into_iter()
            .filter_map(|(_, p)| std::fs::read(&p).ok())
            .filter_map(|b| parse_long_header(&b))
            .collect()
    }

    const RETRY: u8 = 3;

    // Default: no `--no-retry`, so a token-less Initial must be met with a
    // genuine (real-version) Retry before the connection is allowed to
    // proceed.
    {
        let dir = testkit::scratch("cap-apps-cli-flags-retry-default");
        let root =
            testkit::docroot(&dir, &[("payload.bin", testkit::body(1024, 0x0611_0006))]);
        let tls = testkit::tls();
        let port = testkit::free_port();
        let mut sc = testkit::cmd(&server_bin);
        sc.arg("--listen")
            .arg(format!("127.0.0.1:{port}"))
            .arg("--cert")
            .arg(&tls.cert)
            .arg("--key")
            .arg(&tls.key)
            .arg("--root")
            .arg(&root);
        let mut server = testkit::spawn_server(&mut sc, port, &dir, "server-retry-default", SHORT);
        let client_dump = dir.join("client-dump");
        std::fs::create_dir_all(&client_dump).expect("create client --dump-packets directory");
        let mut cc = testkit::cmd(&client_bin);
        cc.arg("--no-verify")
            .arg("--dump-packets")
            .arg(&client_dump)
            .arg(format!("https://127.0.0.1:{port}/payload.bin"));
        let run = testkit::run(&mut cc, LONG, &dir, "client-retry-default");
        run.expect_ok("default (no --no-retry) request");
        let headers = dumped_headers(&client_dump);
        assert!(
            headers
                .iter()
                .any(|&(ty, v)| ty == RETRY && v == quiche::PROTOCOL_VERSION),
            "without `--no-retry` the server must answer a token-less Initial with a genuine \
             Retry packet at version {:#x}; none of the {} long-header packets the client \
             received matched. Observed (type,version) pairs: {headers:?}\n--- server log ---\n{}",
            quiche::PROTOCOL_VERSION,
            headers.len(),
            server.log()
        );
        assert!(
            server.alive(),
            "server exited during the default-retry check.\n--- server log ---\n{}",
            server.log()
        );
    }

    // `--no-retry`: the same handshake, but the server must skip straight to
    // completing the connection -- no Retry of any version anywhere in what
    // the client received.
    {
        let dir = testkit::scratch("cap-apps-cli-flags-no-retry");
        let root =
            testkit::docroot(&dir, &[("payload.bin", testkit::body(1024, 0x0611_0007))]);
        let tls = testkit::tls();
        let port = testkit::free_port();
        let mut sc = testkit::cmd(&server_bin);
        sc.arg("--listen")
            .arg(format!("127.0.0.1:{port}"))
            .arg("--cert")
            .arg(&tls.cert)
            .arg("--key")
            .arg(&tls.key)
            .arg("--root")
            .arg(&root)
            .arg("--no-retry");
        let mut server = testkit::spawn_server(&mut sc, port, &dir, "server-no-retry", SHORT);
        let client_dump = dir.join("client-dump");
        std::fs::create_dir_all(&client_dump).expect("create client --dump-packets directory");
        let mut cc = testkit::cmd(&client_bin);
        cc.arg("--no-verify")
            .arg("--dump-packets")
            .arg(&client_dump)
            .arg(format!("https://127.0.0.1:{port}/payload.bin"));
        let run = testkit::run(&mut cc, LONG, &dir, "client-no-retry");
        run.expect_ok("--no-retry request");
        let headers = dumped_headers(&client_dump);
        assert!(
            !headers
                .iter()
                .any(|&(ty, v)| ty == RETRY && v == quiche::PROTOCOL_VERSION),
            "with `--no-retry` the server must never send a Retry packet; observed one at version \
             {:#x} among the client's {} long-header packets: {headers:?}\n--- server log ---\n{}",
            quiche::PROTOCOL_VERSION,
            headers.len(),
            server.log()
        );
        assert!(
            server.alive(),
            "server exited during the --no-retry check.\n--- server log ---\n{}",
            server.log()
        );
    }

    // `--wire-version`: the client's own default is the grease value
    // `babababa`, which always trips version negotiation before the real
    // handshake begins (see the default in the CLI usage string); a distinct,
    // explicitly chosen grease value must appear as the very first version
    // the server ever sees instead.
    {
        let dir = testkit::scratch("cap-apps-cli-flags-wire-version");
        let root =
            testkit::docroot(&dir, &[("payload.bin", testkit::body(1024, 0x0611_0008))]);
        let tls = testkit::tls();
        let port = testkit::free_port();
        let server_dump = dir.join("server-dump");
        std::fs::create_dir_all(&server_dump).expect("create server --dump-packets directory");
        let mut sc = testkit::cmd(&server_bin);
        sc.arg("--listen")
            .arg(format!("127.0.0.1:{port}"))
            .arg("--cert")
            .arg(&tls.cert)
            .arg("--key")
            .arg(&tls.key)
            .arg("--root")
            .arg(&root)
            .arg("--dump-packets")
            .arg(&server_dump);
        let mut server = testkit::spawn_server(&mut sc, port, &dir, "server-wire-version", SHORT);
        // Matches the reserved/"grease" pattern the default `babababa` also
        // matches (`version & 0xfafafafa == version`), so `Config::new` in
        // the candidate accepts it without erroring, but is not the default.
        const CHOSEN: u32 = 0x1a2a_3a4a;
        assert_ne!(
            CHOSEN, 0xbaba_baba,
            "test bug: the chosen version must differ from the CLI default"
        );
        let mut cc = testkit::cmd(&client_bin);
        cc.arg("--no-verify")
            .arg("--wire-version")
            .arg(format!("{CHOSEN:x}"))
            .arg(format!("https://127.0.0.1:{port}/payload.bin"));
        let run = testkit::run(&mut cc, LONG, &dir, "client-wire-version");
        run.expect_ok("--wire-version request");
        let headers = dumped_headers(&server_dump);
        let first_version = headers.first().map(|&(_, v)| v);
        assert_eq!(
            first_version,
            Some(CHOSEN),
            "`--wire-version {CHOSEN:x}` must put that exact version in the first packet the \
             server ever receives; observed (type,version) pairs {headers:?}\n--- server log \
             ---\n{}",
            server.log()
        );
        assert!(
            server.alive(),
            "server exited during the --wire-version check.\n--- server log ---\n{}",
            server.log()
        );
    }

    // --- --session-file: a TLS session gets cached to the named file --------
    {
        let dir = testkit::scratch("cap-apps-cli-flags-session-file");
        let root =
            testkit::docroot(&dir, &[("payload.bin", testkit::body(1024, 0x0611_0009))]);
        let tls = testkit::tls();
        let port = testkit::free_port();
        let mut sc = testkit::cmd(&server_bin);
        sc.arg("--listen")
            .arg(format!("127.0.0.1:{port}"))
            .arg("--cert")
            .arg(&tls.cert)
            .arg("--key")
            .arg(&tls.key)
            .arg("--root")
            .arg(&root);
        let mut server = testkit::spawn_server(&mut sc, port, &dir, "server-session-file", SHORT);
        let session_file = dir.join("session.bin");
        assert!(
            !session_file.exists(),
            "test bug: the session file must not pre-exist"
        );
        let mut cc = testkit::cmd(&client_bin);
        cc.arg("--no-verify")
            .arg("--session-file")
            .arg(&session_file)
            .arg(format!("https://127.0.0.1:{port}/payload.bin"));
        let run = testkit::run(&mut cc, LONG, &dir, "client-session-file");
        run.expect_ok("--session-file request");
        let saved = std::fs::read(&session_file).unwrap_or_else(|e| {
            panic!(
                "`--session-file {}` must leave a file behind after a connection completes; it \
                 does not exist ({e}).\n--- server log ---\n{}",
                session_file.display(),
                server.log()
            )
        });
        assert!(
            !saved.is_empty(),
            "`--session-file {}` left an empty file; a cached TLS session must be non-empty \
             bytes.\n--- server log ---\n{}",
            session_file.display(),
            server.log()
        );
        assert!(
            server.alive(),
            "server exited during the --session-file check.\n--- server log ---\n{}",
            server.log()
        );
    }
}

// ---------------------------------------------------------------------------
// cap-qlog-jsonseq  (T4)
// ---------------------------------------------------------------------------

/// `cap-qlog-jsonseq`: with `QLOGDIR` set, each connection leaves a `.sqlog`
/// file that is a JSON text sequence — record separator, one line of JSON,
/// newline — carrying an exact set of event names and the transport parameters
/// the process was actually given. A second connection, driven with a tiny
/// `--max-stream-window` ceiling on the client, uses that same trace format to
/// show `--max-window`/`--max-stream-window` reach
/// `Config::set_max_connection_window`/`set_max_stream_window`: the client's
/// own MAX_STREAM_DATA extensions on the response stream stay bounded by the
/// ceiling instead of the CLI flag being silently ignored.
#[test]
fn cap_qlog_jsonseq() {
    const LEN: usize = 8 * 1024;
    // The four values are deliberately not any release's defaults, so a trace
    // that reports them is a trace of this connection.
    const MAX_DATA: u64 = 3_000_000;
    const MAX_STREAM_DATA: u64 = 700_000;
    const MAX_STREAMS_BIDI: u64 = 37;
    const IDLE_TIMEOUT: u64 = 21_000;

    let dir = testkit::scratch("cap-qlog-jsonseq");
    let qlogdir = dir.join("qlog");
    std::fs::create_dir_all(&qlogdir).expect("create QLOGDIR");
    let want = testkit::body(LEN, 0x0611_0002);
    let root = testkit::docroot(&dir, &[("payload.bin", want.clone())]);

    let tls = testkit::tls();
    let port = testkit::free_port();
    let server_bin = testkit::need_bin(&["quiche-server"]);
    let client_bin = testkit::need_bin(&["quiche-client"]);

    let mut sc = testkit::cmd(&server_bin);
    sc.env("QLOGDIR", &qlogdir)
        .arg("--listen")
        .arg(format!("127.0.0.1:{port}"))
        .arg("--cert")
        .arg(&tls.cert)
        .arg("--key")
        .arg(&tls.key)
        .arg("--root")
        .arg(&root)
        .arg("--max-data")
        .arg(MAX_DATA.to_string())
        .arg("--max-stream-data")
        .arg(MAX_STREAM_DATA.to_string())
        .arg("--max-streams-bidi")
        .arg(MAX_STREAMS_BIDI.to_string())
        .arg("--idle-timeout")
        .arg(IDLE_TIMEOUT.to_string());
    let server = testkit::spawn_server(&mut sc, port, &dir, "server", SHORT);

    let mut cc = testkit::cmd(&client_bin);
    cc.env("QLOGDIR", &qlogdir)
        .arg("--no-verify")
        .arg(format!("https://127.0.0.1:{port}/payload.bin"));
    let run = testkit::run(&mut cc, LONG, &dir, "client");
    run.expect_ok("GET /payload.bin with QLOGDIR set");
    assert!(
        want == run.stdout_bytes,
        "the qlog group needs a completed transfer to have something to describe: {}",
        first_diff(&want, &run.stdout_bytes)
    );

    // The server writes its trace as the connection runs, but give the
    // process a moment to flush the last records before reading the file.
    let server_log = one_file(&qlogdir, "server-");
    let client_log = one_file(&qlogdir, "client-");
    testkit::until(SHORT, || {
        std::fs::metadata(&server_log).map(|m| m.len() > 0).unwrap_or(false)
    });
    let log = server.stop();

    let want_names: BTreeSet<String> = [
        "recovery:metrics_updated",
        "transport:data_moved",
        "transport:packet_received",
        "transport:packet_sent",
        "transport:parameters_set",
    ]
    .iter()
    .map(|s| s.to_string())
    .collect();

    for (path, vantage) in [(&server_log, "server"), (&client_log, "client")] {
        let name = path.file_name().unwrap().to_string_lossy().to_string();
        let raw = std::fs::read(path).unwrap_or_else(|e| panic!("cannot read {name}: {e}"));
        let recs = jsonseq_records(&raw, &name);
        assert!(
            recs.len() > 5,
            "{name} has only {} records; a completed {LEN}-byte transfer produces a header and \
             many events",
            recs.len()
        );

        // The header record.
        let hdr = &recs[0];
        assert_eq!(
            hdr.get("qlog_version").and_then(Json::as_str),
            Some("0.3"),
            "{name}: expected the header record's qlog_version to be \"0.3\"; observed {:?}",
            hdr.get("qlog_version")
        );
        assert_eq!(
            hdr.get("qlog_format").and_then(Json::as_str),
            Some("JSON-SEQ"),
            "{name}: expected the header record's qlog_format to be \"JSON-SEQ\"; observed {:?}. \
             A single JSON document with a \"traces\" array is the format this milestone \
             replaced.",
            hdr.get("qlog_format")
        );
        assert!(
            hdr.get("traces").is_none(),
            "{name}: the header record carries a \"traces\" array, which belongs to the \
             non-streaming format. JSON-SEQ carries a single \"trace\"."
        );
        assert_eq!(
            hdr.path(&["trace", "vantage_point", "type"]).and_then(Json::as_str),
            Some(vantage),
            "{name}: expected trace.vantage_point.type {vantage:?}; observed {:?}",
            hdr.path(&["trace", "vantage_point", "type"])
        );

        // Every other record is one event: time, name, data. Nothing else.
        let mut names: BTreeSet<String> = BTreeSet::new();
        for (n, ev) in recs.iter().enumerate().skip(1) {
            let keys = ev.keys();
            let want_keys: BTreeSet<String> =
                ["data", "name", "time"].iter().map(|s| s.to_string()).collect();
            assert_eq!(
                keys, want_keys,
                "{name} record {n}: expected an event with exactly the keys {want_keys:?}; \
                 observed {keys:?}"
            );
            let ev_name = ev
                .get("name")
                .and_then(Json::as_str)
                .unwrap_or_else(|| panic!("{name} record {n}: `name` is not a string"))
                .to_string();
            assert!(
                ev_name.contains(':'),
                "{name} record {n}: event name {ev_name:?} is not `category:event`"
            );
            names.insert(ev_name);
        }

        assert_eq!(
            names, want_names,
            "{name}: the set of event names in a completed transfer at the default qlog level \
             must be exactly {want_names:?}; observed {names:?}"
        );
    }

    // The local transport parameters are the ones the server was told to use.
    let raw = std::fs::read(&server_log).expect("re-read the server trace");
    let recs = jsonseq_records(&raw, "server trace");
    let local = recs
        .iter()
        .skip(1)
        .find(|ev| {
            ev.get("name").and_then(Json::as_str) == Some("transport:parameters_set") &&
                ev.path(&["data", "owner"]).and_then(Json::as_str) == Some("local")
        })
        .unwrap_or_else(|| {
            panic!(
                "the server trace has no transport:parameters_set event with \"owner\":\"local\".\
                 \n--- server log ---\n{log}"
            )
        });

    for (field, want) in [
        ("initial_max_data", MAX_DATA),
        ("initial_max_stream_data_bidi_local", MAX_STREAM_DATA),
        ("initial_max_stream_data_bidi_remote", MAX_STREAM_DATA),
        ("initial_max_streams_bidi", MAX_STREAMS_BIDI),
        ("max_idle_timeout", IDLE_TIMEOUT),
        // The remaining local fields are not set by any CLI flag this group
        // passes, so their values are not "deliberately not any release's
        // defaults" the way the five above are -- they are either another
        // CLI flag's *default* (`initial_max_streams_uni`), a value shared
        // with a flag already covered above because the apps binary reuses
        // the same CLI knob for both the bidi-remote and uni ceilings
        // (`initial_max_stream_data_uni`), the constant every quiche-apps
        // binary hardcodes for its UDP payload ceiling
        // (`max_udp_payload_size`), or quiche's own built-in
        // `TransportParams::default()` (`ack_delay_exponent`,
        // `max_ack_delay`, `active_connection_id_limit`). Asserting them
        // still pins real, checkable behaviour: a regression that stopped
        // plumbing any of these into the outgoing transport parameters
        // would go unnoticed without this.
        ("initial_max_stream_data_uni", MAX_STREAM_DATA),
        ("initial_max_streams_uni", 100),
        ("max_udp_payload_size", 1350),
        ("ack_delay_exponent", 3),
        ("max_ack_delay", 25),
        ("active_connection_id_limit", 2),
    ] {
        assert_eq!(
            local.path(&["data", field]).and_then(Json::as_u64),
            Some(want),
            "the server was started with the value {want} for {field}, so its own \
             transport:parameters_set event must report it; observed {:?}",
            local.path(&["data", field])
        );
    }

    assert_eq!(
        local.path(&["data", "disable_active_migration"]).and_then(Json::as_bool),
        Some(true),
        "quiche-server hardcodes set_disable_active_migration(true), so its own \
         transport:parameters_set event must report disable_active_migration: true; observed {:?}",
        local.path(&["data", "disable_active_migration"])
    );

    assert_eq!(
        recs.iter()
            .skip(1)
            .filter(|ev| ev.get("name").and_then(Json::as_str) == Some("transport:parameters_set"))
            .count(),
        2,
        "expected exactly two transport:parameters_set events — one for the local parameters and \
         one for the peer's"
    );

    // The remote/peer `transport:parameters_set` event is the server's report
    // of what quiche-client sent it. The client above was launched with no
    // `--max-data`/`--max-stream-data`/`--max-streams-bidi`/`--max-streams-uni`/
    // `--idle-timeout` overrides, so every one of these is quiche-client's own
    // docopt default (see `apps/src/args.rs`); the remaining fields are the
    // same apps-hardcoded constant or quiche built-in default already checked
    // above for the local side. A trace that reported only presence/count for
    // this event, and not these values, would not catch a regression that
    // silently dropped or corrupted the peer's transport parameters on the
    // way into the qlog record.
    let remote = recs
        .iter()
        .skip(1)
        .find(|ev| {
            ev.get("name").and_then(Json::as_str) == Some("transport:parameters_set") &&
                ev.path(&["data", "owner"]).and_then(Json::as_str) == Some("remote")
        })
        .unwrap_or_else(|| {
            panic!(
                "the server trace has no transport:parameters_set event with \"owner\":\"remote\".\
                 \n--- server log ---\n{log}"
            )
        });

    for (field, want) in [
        ("initial_max_data", 10_000_000),
        ("initial_max_stream_data_bidi_local", 1_000_000),
        ("initial_max_stream_data_bidi_remote", 1_000_000),
        ("initial_max_stream_data_uni", 1_000_000),
        ("initial_max_streams_bidi", 100),
        ("initial_max_streams_uni", 100),
        ("max_idle_timeout", 30_000),
        ("max_udp_payload_size", 1350),
        ("ack_delay_exponent", 3),
        ("max_ack_delay", 25),
        ("active_connection_id_limit", 2),
    ] {
        assert_eq!(
            remote.path(&["data", field]).and_then(Json::as_u64),
            Some(want),
            "quiche-client was launched with no override for {field}, so it sent its docopt \
             default/apps constant {want}, and the server's own transport:parameters_set event \
             for \"owner\":\"remote\" must report it; observed {:?}",
            remote.path(&["data", field])
        );
    }

    assert_eq!(
        remote.path(&["data", "disable_active_migration"]).and_then(Json::as_bool),
        Some(true),
        "quiche-client hardcodes set_disable_active_migration(true), so the server's \
         transport:parameters_set event for \"owner\":\"remote\" must report \
         disable_active_migration: true; observed {:?}",
        remote.path(&["data", "disable_active_migration"])
    );

    // `--max-window`/`--max-stream-window` are CLI-only plumbing on top of
    // `Config::set_max_connection_window`/`set_max_stream_window`. The Config
    // setters are already exercised in-memory by `cap-stream-window-autotune`;
    // what is not yet shown anywhere is that the CLI flags reach them. The
    // large response body flows server-to-client, so it is the *client's* own
    // ceiling that has to bound the MAX_STREAM_DATA extensions it sends the
    // server: a client honouring a small, pinned ceiling on a body many times
    // its size must send many small extensions rather than one or two huge
    // ones, and no single extension may grow the offered window by much more
    // than the ceiling.
    {
        const WINDOW: u64 = 4096;
        // Large enough that a receiver honouring a 4096-byte ceiling needs
        // dozens of window extensions; small enough the run stays well inside
        // `LONG`.
        const BODY: usize = 256 * 1024;

        let dir = testkit::scratch("cap-qlog-jsonseq-max-window");
        let qlogdir = dir.join("qlog");
        std::fs::create_dir_all(&qlogdir).expect("create QLOGDIR");
        let want = testkit::body(BODY, 0x0611_0003);
        let root = testkit::docroot(&dir, &[("big.bin", want.clone())]);

        let tls = testkit::tls();
        let port = testkit::free_port();
        let server_bin = testkit::need_bin(&["quiche-server"]);
        let client_bin = testkit::need_bin(&["quiche-client"]);

        let mut sc = testkit::cmd(&server_bin);
        sc.arg("--listen")
            .arg(format!("127.0.0.1:{port}"))
            .arg("--cert")
            .arg(&tls.cert)
            .arg("--key")
            .arg(&tls.key)
            .arg("--root")
            .arg(&root);
        let server = testkit::spawn_server(&mut sc, port, &dir, "server-mw", SHORT);

        let mut cc = testkit::cmd(&client_bin);
        cc.env("QLOGDIR", &qlogdir)
            .arg("--no-verify")
            .arg("--max-stream-data")
            .arg(WINDOW.to_string())
            .arg("--max-stream-window")
            .arg(WINDOW.to_string())
            .arg(format!("https://127.0.0.1:{port}/big.bin"));
        let run = testkit::run(&mut cc, LONG, &dir, "client-mw");
        let log = server.stop();
        run.expect_ok(&format!(
            "GET /big.bin with --max-stream-data {WINDOW} --max-stream-window {WINDOW}"
        ));
        assert!(
            want == run.stdout_bytes,
            "the max-window group needs a completed {BODY}-byte transfer: {}\n--- server log \
             ---\n{log}",
            first_diff(&want, &run.stdout_bytes)
        );

        let client_log = one_file(&qlogdir, "client-");
        let raw = std::fs::read(&client_log)
            .unwrap_or_else(|e| panic!("cannot read {}: {e}", client_log.display()));
        let recs = jsonseq_records(&raw, "client-mw trace");

        // Every MAX_STREAM_DATA frame the client sent on the response stream
        // (client-initiated bidi stream 0), in trace order.
        let mut maxima: Vec<u64> = Vec::new();
        for ev in recs.iter().skip(1) {
            if ev.get("name").and_then(Json::as_str) != Some("transport:packet_sent") {
                continue;
            }
            let frames = match ev.path(&["data", "frames"]) {
                Some(Json::Arr(frames)) => frames,
                _ => continue,
            };
            for f in frames {
                if f.get("frame_type").and_then(Json::as_str) != Some("max_stream_data") {
                    continue;
                }
                if f.get("stream_id").and_then(Json::as_u64) != Some(0) {
                    continue;
                }
                if let Some(m) = f.get("maximum").and_then(Json::as_u64) {
                    maxima.push(m);
                }
            }
        }

        let want_extensions = BODY as u64 / WINDOW;
        assert!(
            maxima.len() >= 10,
            "the client was started with --max-stream-window {WINDOW} and asked to receive a \
             {BODY}-byte body on stream 0, which needs at least {want_extensions} window \
             extensions if the ceiling is honoured; observed only {} MAX_STREAM_DATA frames for \
             that stream in {}. A client that does not plumb --max-stream-window into \
             Config::set_max_stream_window would need far fewer.",
            maxima.len(),
            client_log.display()
        );

        let mut sorted = maxima.clone();
        sorted.sort_unstable();
        sorted.dedup();
        let max_step = sorted.windows(2).map(|w| w[1] - w[0]).max().unwrap_or(0);
        assert!(
            max_step <= WINDOW * 4,
            "a single MAX_STREAM_DATA extension on stream 0 grew the offered window by \
             {max_step} bytes, more than 4x the configured --max-stream-window {WINDOW}; the \
             ceiling does not appear to be bounding real extensions. Full sequence: {sorted:?}"
        );
    }
}

// ---------------------------------------------------------------------------
// cap-ecn-ack-frame  (T2)
// ---------------------------------------------------------------------------

/// `cap-ecn-ack-frame`: frame type `0x03` — the ACK frame with ECN counts — is
/// parsed, all three of its varints are consumed, and the frame that follows
/// it in the same packet is processed.
///
/// The packet is the verifier's: `quiche_verifier_support` does the AEAD, the
/// nonce and the header-protection mask, using the client's application
/// traffic secret taken out of the candidate through the standard NSS keylog
/// hook. The verifier proves its own keys first by decrypting one of the
/// candidate's own 1-RTT packets with them, so a failure here is a failure of
/// the candidate's frame parser and not of the verifier's crypto.
#[test]
fn cap_ecn_ack_frame() {
    const STREAM_ID: u64 = 8;
    const ECT0: u64 = 7;
    const ECT1: u64 = 11;
    const CE: u64 = 3;
    // Far beyond anything the real client will have used, and well inside the
    // 4-byte packet number this is encoded in.
    const INJECTED_PN: u64 = 4096;

    let mut cc = pair::client_config(pair::ALPN_HQ);
    cc.log_keys();
    let mut sc = pair::server_config(pair::ALPN_HQ);
    let (mut p, keylog) = pair::Pair::with_keylog(&mut cc, &mut sc);
    p.handshake();

    // Move a little application data so that both sides have sent 1-RTT
    // packets: the ACK below acknowledges the server's application packet 0,
    // and the verifier's key check needs a short-header packet to open.
    p.client.stream_send(0, b"warm", false).expect("stream_send on stream 0");
    p.settle(16);
    assert!(
        p.client.is_established() && p.server.is_established(),
        "the handshake did not complete, so there are no 1-RTT keys to work with"
    );

    let secret = pair::keylog_secret(&keylog.contents(), "CLIENT_TRAFFIC_SECRET_0")
        .expect("the client did not write CLIENT_TRAFFIC_SECRET_0 to its keylog");
    let keys = support::PacketKeys::from_secret(support::Suite::Aes128Gcm, &secret)
        .expect("deriving 1-RTT keys from the client's traffic secret");

    // Prove the verifier's keys before using them to make a claim.
    let opened = p
        .client_out
        .iter()
        .rev()
        .filter(|d| !d.is_empty() && !support::is_long_header(d[0]))
        .find_map(|d| pair::decrypt_1rtt(d, pair::SERVER_CID.len(), &keys, None));
    assert!(
        opened.is_some(),
        "the verifier could not open any of the {} datagrams the client sent with the keys it \
         derived from CLIENT_TRAFFIC_SECRET_0, so the injected packet below would prove nothing. \
         This is a key-schedule or cipher-suite disagreement, not a frame-parsing one.",
        p.client_out.len()
    );

    // ACK frame, type 0x03, RFC 9000 Section 19.3: largest acknowledged, ACK
    // delay, ACK range count, first ACK range, then the three ECN counts.
    let mut payload = Vec::new();
    pair::varint(0x03, &mut payload);
    pair::varint(0, &mut payload); // largest acknowledged
    pair::varint(0, &mut payload); // ACK delay
    pair::varint(0, &mut payload); // ACK range count
    pair::varint(0, &mut payload); // first ACK range
    pair::varint(ECT0, &mut payload);
    pair::varint(ECT1, &mut payload);
    pair::varint(CE, &mut payload);

    // STREAM frame, type 0x0a: stream id and length present, no offset, no
    // fin. Reaching it at all requires the three ECN varints above to have
    // been consumed.
    let body = testkit::body(64, 0x0611_0003);
    pair::varint(0x0a, &mut payload);
    pair::varint(STREAM_ID, &mut payload);
    pair::varint(body.len() as u64, &mut payload);
    payload.extend_from_slice(&body);

    let mut pkt = pair::seal_1rtt(&keys, &pair::SERVER_CID, INJECTED_PN, &payload);
    let n = p.server.recv(&mut pkt, quiche::RecvInfo { from: pair::client_addr() });
    assert!(
        n.is_ok(),
        "a 1-RTT packet whose first frame is an ACK of type 0x03 with the ECN counts \
         ({ECT0}, {ECT1}, {CE}) was rejected with {:?}. At this milestone frame type 0x03 is the \
         ACK frame carrying an ECN Counts section (RFC 9000 Section 19.3.2) and must be parsed; \
         rejecting it as an unknown frame type is the previous milestone's behaviour.",
        n.unwrap_err()
    );

    assert!(
        !p.server.is_closed(),
        "the server closed the connection after reading an ACK frame of type 0x03"
    );

    let (got, fin) = pair::read_stream(&mut p.server, STREAM_ID);
    assert_eq!(
        got.len(),
        body.len(),
        "the STREAM frame that followed the ECN counts in the same packet delivered {} of {} \
         bytes on stream {STREAM_ID}. Reading fewer than all of them means the ECN Counts \
         section was not consumed as three variable-length integers and the parser resumed in \
         the middle of the next frame. ({})",
        got.len(),
        body.len(),
        first_diff(&body, &got)
    );
    assert!(
        body == got,
        "the bytes delivered on stream {STREAM_ID} are not the ones the frame after the ECN \
         counts carried: {}",
        first_diff(&body, &got)
    );
    assert!(!fin, "the injected STREAM frame did not set fin, but the stream reports finished");

    // A connection error would have queued a CONNECTION_CLOSE, so read what
    // the server writes next and decode it with the verifier's own crypto.
    //
    // The packets are pulled off by hand rather than exchanged: the injected
    // packet number is one the real client never sent, so the ACK that
    // acknowledges it is -- correctly -- a protocol violation at the client,
    // and handing it back would be measuring the verifier's fiction rather
    // than the candidate.
    let server_secret = pair::keylog_secret(&keylog.contents(), "SERVER_TRAFFIC_SECRET_0")
        .expect("the client did not write SERVER_TRAFFIC_SECRET_0 to its keylog");
    let server_keys = support::PacketKeys::from_secret(support::Suite::Aes128Gcm, &server_secret)
        .expect("deriving the server's 1-RTT keys from its traffic secret");

    let mut out = [0u8; pair::MAX_DATAGRAM];
    let mut acked_injected = false;
    let mut decoded = Vec::new();
    loop {
        let len = match p.server.send(&mut out) {
            Ok((len, _)) => len,
            Err(quiche::Error::Done) => break,
            Err(e) => panic!("the server could not write after reading the ECN counts: {e:?}"),
        };
        let Some(payload) =
            pair::decrypt_1rtt(&out[..len], pair::CLIENT_CID.len(), &server_keys, None)
        else {
            continue;
        };
        for f in support::frames(&payload) {
            let Ok(f) = f else { break };
            if let support::Frame::ConnectionClose { error_code, frame_type, reason } = &f {
                panic!(
                    "the server answered the packet carrying the ECN counts with a \
                     CONNECTION_CLOSE: error code {error_code:#x}, frame type {frame_type:?}, \
                     reason {:?}. Transport error 0x07 (FRAME_ENCODING_ERROR) is the previous \
                     milestone's answer to frame type 0x03; at this one the frame is parsed.",
                    String::from_utf8_lossy(reason)
                );
            }
            if let support::Frame::Ack { ranges, .. } = &f {
                if ranges.iter().any(|(lo, hi)| *lo <= INJECTED_PN && INJECTED_PN <= *hi) {
                    acked_injected = true;
                }
            }
            decoded.push(format!("{f:?}"));
        }
    }

    assert!(
        acked_injected,
        "the server never acknowledged packet {INJECTED_PN}, the one carrying the ACK frame of \
         type 0x03 and the STREAM frame after it. A packet whose frames were all processed is \
         ack-eliciting because of that STREAM frame and must be acknowledged; the frames the \
         server did write were {decoded:?}"
    );
    assert!(
        !p.server.is_closed() && !p.server.is_draining(),
        "the server tore the connection down after the packet carrying the ECN counts"
    );
}

// ---------------------------------------------------------------------------
// cap-stream-window-autotune  (T3)
// ---------------------------------------------------------------------------

/// `cap-stream-window-autotune`: the receiver's stream flow control window
/// grows when the application drains it quickly, and `set_max_stream_window`
/// is what stops it.
///
/// Three pairs, one setting apart. The pinned one must behave exactly as the
/// previous milestone did; the other two must walk the doubling ladder up to
/// the maximum each was given, and no further.
#[test]
fn cap_stream_window_autotune() {
    // The advertised initial per-stream limit, in all three runs. Deliberately
    // small: even eight times this is well inside the initial congestion
    // window, so the only thing that can bound a round is flow control.
    const WINDOW: u64 = 1024;
    const ROUNDS: usize = 8;

    // Run `ROUNDS` rounds of "offer the sender everything, exchange, drain the
    // receiver completely", and return how many bytes the sender was allowed
    // to put on the stream in each round.
    //
    // The receiver is the server: it advertises `WINDOW` as the initial
    // per-stream limit and is told how far that window may grow.
    fn run(max_stream_window: u64) -> Vec<u64> {
        let mut cc = pair::client_config(pair::ALPN_HQ);
        let mut sc = pair::server_config(pair::ALPN_HQ);
        sc.set_initial_max_stream_data_bidi_remote(WINDOW);
        sc.set_max_stream_window(max_stream_window);
        // The connection-level limit must never be the thing that bites, so
        // that every number this function returns is about the per-stream
        // window alone.
        sc.set_initial_max_data(WINDOW * 4096);
        sc.set_max_connection_window(WINDOW * 4096);

        let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
        p.handshake();

        // Longer than the largest window any run may reach, so the sender is
        // never the one that runs out of material.
        let data = testkit::body((WINDOW * 64) as usize, 0x0611_0004);
        let mut per_round = Vec::with_capacity(ROUNDS);

        for round in 0..ROUNDS {
            let n = p
                .client
                .stream_send(0, &data, false)
                .unwrap_or_else(|e| panic!("stream_send in round {round}: {e:?}"));
            per_round.push(n as u64);
            p.settle(64);
            // Drain the receiver completely: the window only grows for an
            // application that is keeping up with it.
            let (chunk, _) = pair::read_stream(&mut p.server, 0);
            assert_eq!(
                chunk.len(),
                n,
                "round {round}: the sender was allowed {n} bytes but the receiver read back {} \
                 of them",
                chunk.len()
            );
            p.settle(64);
        }
        per_round
    }

    let pinned = run(WINDOW);
    let capped = run(WINDOW * 2);
    let grown = run(WINDOW * 8);

    // 1. Pinning the maximum to the advertised initial limit reproduces the
    //    previous milestone exactly: one window per round, every round.
    assert_eq!(
        pinned,
        vec![WINDOW; ROUNDS],
        "with set_max_stream_window({WINDOW}) equal to the advertised initial limit the receive \
         window cannot grow, so the sender must be handed exactly {WINDOW} bytes in each of the \
         {ROUNDS} rounds; observed {pinned:?}"
    );

    // 2. Whatever the maximum, the first round is bounded by the advertised
    //    initial limit, and a window the application keeps draining never
    //    shrinks.
    for (label, seen) in [("max=2x", &capped), ("max=8x", &grown)] {
        assert_eq!(
            seen[0], WINDOW,
            "{label}: the first round is bounded by the advertised initial limit whatever the \
             maximum is, but the sender was handed {} bytes; observed {seen:?}",
            seen[0]
        );
        let shrank = seen.windows(2).position(|w| w[1] < w[0]);
        assert!(
            shrank.is_none(),
            "{label}: the per-round allowance went down between round {} and round {}, but a \
             receive window the application is draining every round only ever grows; observed \
             {seen:?}",
            shrank.unwrap_or_default(),
            shrank.unwrap_or_default() + 1
        );
    }

    // 3. The growth is a doubling, not a drift: across a whole run the
    //    allowance only ever takes the values on the doubling ladder between
    //    the advertised limit and the configured maximum, and it reaches that
    //    maximum inside the run.
    let ladder = |seen: &[u64]| -> BTreeSet<u64> { seen.iter().copied().collect() };

    assert_eq!(
        ladder(&capped),
        BTreeSet::from([WINDOW, WINDOW * 2]),
        "with set_max_stream_window({}) the window must double once and then stop, so the only \
         per-round allowances in the run are {WINDOW} and {}; observed {capped:?}",
        WINDOW * 2,
        WINDOW * 2
    );
    assert_eq!(
        capped[ROUNDS - 1],
        WINDOW * 2,
        "with set_max_stream_window({}) the window must have reached its maximum within {ROUNDS} \
         rounds, but the last round handed the sender {} bytes; observed {capped:?}",
        WINDOW * 2,
        capped[ROUNDS - 1]
    );

    assert_eq!(
        ladder(&grown),
        BTreeSet::from([WINDOW, WINDOW * 2, WINDOW * 4, WINDOW * 8]),
        "with set_max_stream_window({}) the window must double every time the application drains \
         it inside two round trips, so the per-round allowances must walk the ladder {WINDOW}, \
         {}, {}, {} and take no other value; observed {grown:?}",
        WINDOW * 8,
        WINDOW * 2,
        WINDOW * 4,
        WINDOW * 8
    );
    assert_eq!(
        grown[ROUNDS - 1],
        WINDOW * 8,
        "with set_max_stream_window({}) the window must have reached its maximum within {ROUNDS} \
         rounds, but the last round handed the sender {} bytes; observed {grown:?}",
        WINDOW * 8,
        grown[ROUNDS - 1]
    );

    // 4. A larger maximum moves strictly more data in the same number of
    //    rounds, which is the whole point of the setting.
    let pinned_total: u64 = pinned.iter().sum();
    let capped_total: u64 = capped.iter().sum();
    let grown_total: u64 = grown.iter().sum();
    assert!(
        pinned_total < capped_total && capped_total < grown_total,
        "in {ROUNDS} identical rounds the three receivers moved {pinned_total}, {capped_total} \
         and {grown_total} bytes for set_max_stream_window({WINDOW}), ({}) and ({}); raising the \
         maximum has to move strictly more data. Per-round allowances were {pinned:?}, {capped:?} \
         and {grown:?}",
        WINDOW * 2,
        WINDOW * 8
    );
    assert!(
        grown_total >= pinned_total * 4,
        "the pinned receiver moved {pinned_total} bytes in {ROUNDS} rounds and the receiver \
         allowed to grow to {} moved {grown_total}; doubling from {WINDOW} would have moved at \
         least four times as much. Observed per-round allowances {grown:?}",
        WINDOW * 8
    );

    // 5. TEST_INTERFACE's own claim about this mechanism: "the connection
    //    window is additionally kept at or above a multiple of the largest
    //    stream window, so a grown stream window is never throttled by an
    //    unmoved connection limit." The three runs above never exercise that
    //    claim -- they pin the connection ceiling at WINDOW*4096, far above
    //    any multiple that could plausibly bite, specifically so the
    //    connection level is never the thing under test. This run pins the
    //    connection ceiling at exactly the largest stream window (a 1x
    //    multiple -- the tightest one a single-stream connection could need)
    //    and requires the stream to reach that same maximum anyway.
    fn run_tight_connection_window(max_stream_window: u64) -> Vec<u64> {
        let mut cc = pair::client_config(pair::ALPN_HQ);
        let mut sc = pair::server_config(pair::ALPN_HQ);
        sc.set_initial_max_stream_data_bidi_remote(WINDOW);
        sc.set_max_stream_window(max_stream_window);
        // Tight: the connection-level ceiling is exactly the stream's own
        // maximum, not a deliberately oversized one, and its initial limit
        // matches the stream's so the connection level has to grow too.
        sc.set_initial_max_data(WINDOW);
        sc.set_max_connection_window(max_stream_window);

        let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
        p.handshake();

        let data = testkit::body((max_stream_window * 64) as usize, 0x0611_0005);
        let mut per_round = Vec::with_capacity(ROUNDS);
        for round in 0..ROUNDS {
            let n = p
                .client
                .stream_send(0, &data, false)
                .unwrap_or_else(|e| panic!("tight-connection-window round {round}: {e:?}"));
            per_round.push(n as u64);
            p.settle(64);
            let (chunk, _) = pair::read_stream(&mut p.server, 0);
            assert_eq!(
                chunk.len(),
                n,
                "tight-connection-window round {round}: the sender was allowed {n} bytes but the \
                 receiver read back {} of them",
                chunk.len()
            );
            p.settle(64);
        }
        per_round
    }

    let grown_tight_conn = run_tight_connection_window(WINDOW * 8);
    assert_eq!(
        ladder(&grown_tight_conn),
        BTreeSet::from([WINDOW, WINDOW * 2, WINDOW * 4, WINDOW * 8]),
        "with the connection-level ceiling pinned at exactly the stream's own maximum ({}) \
         instead of a deliberately oversized one, the stream window must still walk the same \
         ladder {WINDOW}, {}, {}, {} -- an unmoved connection limit must not throttle a grown \
         stream window that stays within it; observed {grown_tight_conn:?}",
        WINDOW * 8,
        WINDOW * 2,
        WINDOW * 4,
        WINDOW * 8
    );
    assert_eq!(
        grown_tight_conn[ROUNDS - 1],
        WINDOW * 8,
        "with the connection-level ceiling pinned at exactly the stream's own maximum, the last \
         round must still hand the sender the full {} bytes; observed {grown_tight_conn:?}",
        WINDOW * 8
    );
}

// ---------------------------------------------------------------------------
// cap-stream-reset-error  (T3)
// ---------------------------------------------------------------------------

/// `cap-stream-reset-error`: a stream the peer reset is reported to the
/// application as `Error::StreamReset(code)` carrying the peer's error code,
/// once, and does not disturb any other stream.
#[test]
fn cap_stream_reset_error() {
    const CODE: u64 = 0xbeef;
    const OTHER: u64 = 4;

    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let first = testkit::body(100, 0x0611_0005);
    p.client.stream_send(0, &first, false).expect("stream_send on stream 0");
    p.settle(16);

    let (got, fin) = pair::read_stream(&mut p.server, 0);
    assert!(first == got, "the server did not read the 100 bytes first: {}", first_diff(&first, &got));
    assert!(!fin, "stream 0 was not finished yet");

    // A second stream, opened before the reset and never touched by it.
    let other = testkit::body(64, 0x0611_0006);
    p.client.stream_send(OTHER, &other, true).expect("stream_send on the second stream");

    p.client
        .stream_shutdown(0, quiche::Shutdown::Write, CODE)
        .expect("stream_shutdown(Shutdown::Write) to send RESET_STREAM");
    p.settle(16);

    // The reset is what makes the stream readable: the application is told
    // about it through the same path it would have been told about data.
    let readable: Vec<u64> = p.server.readable().collect();
    assert!(
        readable.contains(&0),
        "a stream the peer has reset must be offered to the application as readable so the \
         reset can be collected; readable() listed {readable:?}"
    );

    let mut buf = [0u8; 256];
    let err = p.server.stream_recv(0, &mut buf).expect_err(
        "reading a stream the peer has reset must fail rather than return data or Done",
    );
    assert_eq!(
        err,
        quiche::Error::StreamReset(CODE),
        "expected Error::StreamReset({CODE}) carrying the RESET_STREAM frame's application error \
         code; observed {err:?}. At the previous milestone there was no such variant and a reset \
         stream simply went quiet."
    );

    // Reported once: having handed the reset over, the stream stops coming
    // back as readable, now and after any amount of further exchange.
    let still_readable: Vec<u64> = p.server.readable().collect();
    assert!(
        !still_readable.contains(&0),
        "stream 0 is still listed as readable after its reset was reported; the reset is \
         delivered once. readable() listed {still_readable:?}"
    );
    p.settle(8);
    let still_readable: Vec<u64> = p.server.readable().collect();
    assert!(
        !still_readable.contains(&0),
        "stream 0 became readable again after further exchanges, so the reset would be reported \
         twice; readable() listed {still_readable:?}"
    );

    // And the answer is stable: asking the reset stream again reports the
    // same reset with the same code, never data and never Done.
    let again = p.server.stream_recv(0, &mut buf);
    assert_eq!(
        again,
        Err(quiche::Error::StreamReset(CODE)),
        "reading the reset stream again must keep reporting Error::StreamReset({CODE}) rather \
         than returning data or Error::Done; observed {again:?}"
    );

    // The other stream is untouched.
    let (got, fin) = pair::read_stream(&mut p.server, OTHER);
    assert!(
        other == got,
        "resetting stream 0 disturbed stream {OTHER}: {}",
        first_diff(&other, &got)
    );
    assert!(fin, "stream {OTHER} was sent with fin and must still report it");
    assert!(
        !p.server.is_closed() && !p.client.is_closed(),
        "a RESET_STREAM closed the connection; it closes one stream"
    );
}

// ---------------------------------------------------------------------------
// cap-h3-peer-settings-raw  (T3)
// ---------------------------------------------------------------------------

/// `cap-h3-peer-settings-raw`: the peer's SETTINGS frame is readable exactly
/// as it arrived — every identifier, including ones this implementation does
/// not recognise, in receipt order. An HTTP/2-reserved identifier is not
/// "unrecognised" the same way, and is still a connection error
/// (`h3::Error::SettingsError`); a request whose header block exceeds the
/// receiver's `set_max_field_section_size` is still `h3::Error::ExcessiveLoad`.
///
/// The client side here is the candidate's plain transport `Connection` with
/// an HTTP/3 control stream written onto it by hand, so the pairs the server
/// reports are the pairs the verifier chose rather than the pairs the
/// candidate's own HTTP/3 layer would have sent. The reserved-identifier and
/// field-section-size cases each need their own pair: SETTINGS may only be
/// sent once per control stream, and the field-section-size case needs the
/// server's HTTP/3 limit to differ from the client's.
#[test]
fn cap_h3_peer_settings_raw() {
    // RFC 9114 Section 7.2.4.1 reserves 0x1f * N + 0x21 for greasing; no
    // implementation may recognise one.
    const GREASE: u64 = 0x1f * 3 + 0x21;
    let want: Vec<(u64, u64)> = vec![(0x01, 4096), (GREASE, 0xdead), (0x06, 1024), (0x07, 12)];

    let mut cc = pair::client_config(pair::ALPN_H3);
    let mut sc = pair::server_config(pair::ALPN_H3);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let h3_cfg = pair::h3_config();
    let mut h3_server = quiche::h3::Connection::with_transport(&mut p.server, &h3_cfg)
        .expect("h3 server on an established connection");

    assert_eq!(
        h3_server.peer_settings_raw(),
        None,
        "peer_settings_raw() must be None before the peer's SETTINGS frame has arrived"
    );

    // The HTTP/3 control stream: stream type 0x00, then a SETTINGS frame.
    let mut body = Vec::new();
    for (id, value) in &want {
        pair::varint(*id, &mut body);
        pair::varint(*value, &mut body);
    }
    let mut control = Vec::new();
    pair::varint(0x00, &mut control); // HTTP/3 control stream type
    pair::varint(0x04, &mut control); // SETTINGS frame type
    pair::varint(body.len() as u64, &mut control);
    control.extend_from_slice(&body);

    p.client
        .stream_send(2, &control, false)
        .expect("writing an HTTP/3 control stream onto client uni stream 2");
    p.settle(32);

    loop {
        match h3_server.poll(&mut p.server) {
            Ok(_) => continue,
            Err(quiche::h3::Error::Done) => break,
            Err(e) => panic!(
                "polling the server after a SETTINGS frame containing the unrecognised \
                 identifier {GREASE:#x} raised {e:?}. RFC 9114 Section 7.2.4 requires an \
                 unrecognised SETTINGS identifier to be ignored, not rejected."
            ),
        }
    }

    let got = h3_server
        .peer_settings_raw()
        .expect("peer_settings_raw() is still None after a SETTINGS frame arrived")
        .to_vec();

    assert_eq!(
        got, want,
        "peer_settings_raw() must return every (identifier, value) pair of the peer's SETTINGS \
         frame, unrecognised identifiers included, in the order they arrived. Expected \
         {want:?}, observed {got:?}."
    );

    assert!(
        !p.server.is_closed(),
        "the server closed the connection over a SETTINGS frame carrying a greased identifier"
    );

    // A HTTP/2-reserved identifier is not "unrecognised" the way GREASE is:
    // RFC 9114 Section 7.2.4.1 requires the connection to be closed with
    // H3_SETTINGS_ERROR. A second, independent pair is used because SETTINGS
    // may only be sent once per control stream, so this cannot reuse `p`
    // above.
    {
        let mut cc = pair::client_config(pair::ALPN_H3);
        let mut sc = pair::server_config(pair::ALPN_H3);
        let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
        p.handshake();

        let h3_cfg = pair::h3_config();
        let mut h3_server = quiche::h3::Connection::with_transport(&mut p.server, &h3_cfg)
            .expect("h3 server on an established connection");

        // 0x2 is SETTINGS_ENABLE_PUSH, one of the five identifiers HTTP/2
        // defined and HTTP/3 reserves (RFC 9114 Section 7.2.4.1).
        const RESERVED: u64 = 0x02;

        let mut body = Vec::new();
        pair::varint(RESERVED, &mut body);
        pair::varint(1, &mut body);
        let mut control = Vec::new();
        pair::varint(0x00, &mut control); // HTTP/3 control stream type
        pair::varint(0x04, &mut control); // SETTINGS frame type
        pair::varint(body.len() as u64, &mut control);
        control.extend_from_slice(&body);

        p.client
            .stream_send(2, &control, false)
            .expect("writing an HTTP/3 control stream onto client uni stream 2");
        p.settle(32);

        let err = loop {
            match h3_server.poll(&mut p.server) {
                Ok(_) => continue,
                Err(e) => break e,
            }
        };
        assert_eq!(
            err,
            quiche::h3::Error::SettingsError,
            "a SETTINGS frame carrying the HTTP/2-reserved identifier {RESERVED:#x} must be a \
             connection error (h3::Error::SettingsError) per RFC 9114 Section 7.2.4.1; observed \
             {err:?}"
        );
    }

    // `set_max_field_section_size` is not just a renamed setter: exceeding the
    // limit it configures still raises h3::Error::ExcessiveLoad. The server's
    // limit has to differ from the client's here, so this needs its own h3
    // configs rather than the shared `pair::h3_config()`.
    {
        let mut cc = pair::client_config(pair::ALPN_H3);
        let mut sc = pair::server_config(pair::ALPN_H3);
        let client_h3 = pair::h3_config();
        let mut server_h3 = pair::h3_config();
        // Small enough that the request below overflows it, large enough that
        // the handshake's own control-stream SETTINGS exchange does not.
        const LIMIT: u64 = 65;
        server_h3.set_max_field_section_size(LIMIT);

        let mut h3p = pair::H3Pair::with_h3_configs(&mut cc, &mut sc, &client_h3, &server_h3);

        // Raw (name, value) pairs, kept alongside the `Header`s built from
        // them so the size check below does not need `h3::NameValue` in
        // scope just for this one group.
        let raw: &[(&[u8], &[u8])] = &[
            (b":method", b"GET"),
            (b":scheme", b"https"),
            (b":authority", b"quic.test"),
            (b":path", b"/excessive-load"),
            (b"aaaaaaa", b"aaaaaaaa"),
        ];
        let sent: u64 = raw.iter().map(|(n, v)| (n.len() + v.len()) as u64).sum();
        assert!(
            sent > LIMIT,
            "test bug: the request's header block ({sent} bytes of name+value) must exceed the \
             server's configured limit ({LIMIT}) for this group to mean anything"
        );
        let req: Vec<quiche::h3::Header> =
            raw.iter().map(|(n, v)| quiche::h3::Header::new(n, v)).collect();

        h3p.client
            .send_request(&mut h3p.pair.client, &req, true)
            .expect("send_request under the server's max_field_section_size limit at the QUIC \
                      layer; the limit is enforced by the server's HTTP/3 layer, not the client's");
        h3p.pair.settle(32);

        let err = loop {
            match h3p.server.poll(&mut h3p.pair.server) {
                Ok(_) => continue,
                Err(e) => break e,
            }
        };
        assert_eq!(
            err,
            quiche::h3::Error::ExcessiveLoad,
            "a request whose header block ({sent} bytes of name+value) exceeds the server's \
             set_max_field_section_size({LIMIT}) must raise h3::Error::ExcessiveLoad; observed \
             {err:?}"
        );
    }
}
