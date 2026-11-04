//! new-capability groups: what this milestone can do that the previous one could not
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `cap-octets-crate` is `fn cap_octets_crate()`.
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

use std::path::Path;
use std::path::PathBuf;
use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

/// HTTP/3 frame type of a PRIORITY_UPDATE that prioritises a request stream.
const PRIORITY_UPDATE_REQUEST: u64 = 0xF0700;

/// `H3_FRAME_UNEXPECTED`, the application error code an endpoint closes with
/// when a frame arrives somewhere it is not allowed. `h3::Error::to_wire` is
/// private, so the group names the wire value from the specification.
const H3_FRAME_UNEXPECTED: u64 = 0x105;

/// `H3_ID_ERROR`.
const H3_ID_ERROR: u64 = 0x108;

// ---------------------------------------------------------------------------
// shared helpers
// ---------------------------------------------------------------------------

/// Write a throwaway crate under `dir` whose only dependency is the
/// candidate's `octets` package, and return its directory.
///
/// The crate carries `[workspace]` so that it is a workspace of its own and
/// does not get adopted by anything above it, and it is written outside the
/// candidate's tree so that nothing the group does can leave a mark there.
fn write_octets_probe(dir: &Path, main_rs: &str) -> PathBuf {
    let probe = dir.join("octets-probe");
    std::fs::create_dir_all(probe.join("src")).expect("create probe crate");
    let octets = testkit::candidate_root().join("octets");
    std::fs::write(
        probe.join("Cargo.toml"),
        format!(
            "[package]\nname = \"octets-probe\"\nversion = \"0.0.0\"\nedition = \"2021\"\n\
             publish = false\n\n[workspace]\n\n[dependencies]\noctets = {{ path = \"{}\" }}\n",
            octets.display()
        ),
    )
    .expect("write probe manifest");
    std::fs::write(probe.join("src/main.rs"), main_rs).expect("write probe main");
    probe
}

/// `cargo build --offline` the probe crate into `dir/target`, and return the
/// finished `Run` plus the path of the binary it should have produced.
fn build_octets_probe(dir: &Path, probe: &Path, tag: &str) -> (testkit::Run, PathBuf) {
    let target = dir.join("target");
    let mut c = testkit::cmd("cargo");
    c.arg("build")
        .arg("--offline")
        .arg("--manifest-path")
        .arg(probe.join("Cargo.toml"))
        .env("CARGO_TARGET_DIR", &target);
    let run = testkit::run(&mut c, LONG, dir, tag);
    (run, target.join("debug").join("octets-probe"))
}

/// Every `name = "..."` value in a Cargo.lock, in file order.
fn lock_packages(lock: &str) -> Vec<String> {
    lock.lines()
        .filter_map(|l| l.trim().strip_prefix("name = "))
        .map(|v| v.trim().trim_matches('"').to_string())
        .collect()
}

/// The `workspace_members` array of a `cargo metadata` document, as raw text.
///
/// The group does not need a JSON parser for this: it needs to know whether
/// the candidate's workspace has a member whose path ends in `octets`, and
/// the array is a flat list of strings.
fn workspace_members_text(metadata: &str) -> Option<&str> {
    let start = metadata.find("\"workspace_members\":")?;
    let open = metadata[start..].find('[')? + start;
    let close = metadata[open..].find(']')? + open;
    Some(&metadata[open..=close])
}

/// Start the 0.10.0-and-later `quiche-server` on `port`, serving `files`.
///
/// This milestone is in the `apps/` era: the workspace builds flag-taking
/// `quiche-server` and `quiche-client` binaries, so the group hands the
/// server an explicit `--listen` and never needs the process-wide port lock.
/// Stateless retry is turned off because none of these groups is about
/// address validation and a retry only adds a round trip.
fn start_app_server(
    dir: &Path, port: u16, files: &[(&str, Vec<u8>)], tag: &str, envs: &[(&str, &Path)],
) -> testkit::Server {
    let root = testkit::docroot(dir, files);
    let tls = testkit::tls();
    let bin = testkit::server_bin();
    let mut c = testkit::cmd(&bin);
    c.arg("--listen")
        .arg(format!("127.0.0.1:{port}"))
        .arg("--cert")
        .arg(&tls.cert)
        .arg("--key")
        .arg(&tls.key)
        .arg("--root")
        .arg(&root)
        .arg("--no-retry");
    for (k, v) in envs {
        c.env(k, v);
    }
    testkit::spawn_server(&mut c, port, dir, tag, LONG)
}

/// Fetch one URL with the 0.10.0-and-later `quiche-client` over HTTP/3.
fn app_client_get(dir: &Path, url: &str, tag: &str, envs: &[(&str, &Path)]) -> testkit::Run {
    let bin = testkit::client_bin();
    let mut c = testkit::cmd(&bin);
    c.arg("--no-verify").arg("--http-version").arg("HTTP/3").arg(url);
    for (k, v) in envs {
        c.env(k, v);
    }
    testkit::run(&mut c, LONG, dir, tag)
}

/// Decode the first PEM block of `pem` -- the leaf certificate's DER.
fn pem_der(pem: &str) -> Vec<u8> {
    let body: String = pem
        .lines()
        .skip_while(|l| !l.starts_with("-----BEGIN"))
        .skip(1)
        .take_while(|l| !l.starts_with("-----END"))
        .collect();
    let mut out = Vec::new();
    let mut acc: u32 = 0;
    let mut bits = 0;
    for ch in body.bytes() {
        let v = match ch {
            b'A'..=b'Z' => ch - b'A',
            b'a'..=b'z' => ch - b'a' + 26,
            b'0'..=b'9' => ch - b'0' + 52,
            b'+' => 62,
            b'/' => 63,
            b'=' => break,
            _ => continue,
        } as u32;
        acc = (acc << 6) | v;
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            out.push((acc >> bits) as u8);
        }
    }
    out
}

/// Send one raw HTTP/3 frame on a transport stream that already carries an
/// HTTP/3 control stream.
fn send_h3_frame(conn: &mut quiche::Connection, stream_id: u64, ty: u64, payload: &[u8]) {
    let frame = pair::h3_frame(ty, payload);
    let n = conn
        .stream_send(stream_id, &frame, false)
        .unwrap_or_else(|e| panic!("stream_send of a {frame_len} byte HTTP/3 frame on stream {stream_id}: {e:?}", frame_len = frame.len()));
    assert_eq!(
        n,
        frame.len(),
        "expected the whole {} byte HTTP/3 frame to fit on stream {stream_id}; {n} bytes were accepted",
        frame.len()
    );
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// cap-octets-crate  (T4)
// ---------------------------------------------------------------------------

/// `cap-octets-crate`: the byte reader is a separate, self-contained package
/// that anything can depend on, not a module inside the transport crate.
///
/// Three independent facts, none of which holds for a `mod octets` hidden
/// inside `quiche`:
///
/// * the candidate's workspace has a member called `octets` -- read out of
///   `cargo metadata`, which is the same question a downstream consumer's
///   build asks;
/// * a crate written outside the candidate's tree, which has never heard of
///   `quiche`, can name `octets` as a path dependency and `cargo build
///   --offline` it;
/// * the lock file that build resolves contains exactly two packages, the
///   probe and `octets`. That is the part that cannot be faked by a shim: a
///   package that re-exports `quiche::octets` would drag `quiche`, its
///   BoringSSL build script and the rest of its graph into the lock, and a
///   package that pulls in a third-party byte library would show that too.
///   The extracted crate has to stand on nothing.
#[test]
fn cap_octets_crate() {
    let dir = testkit::scratch("cap-octets-crate");
    let root = testkit::candidate_root();

    let manifest = root.join("octets").join("Cargo.toml");
    assert!(
        manifest.is_file(),
        "expected the extracted byte-reader crate's manifest at {}; the workspace at {} does not \
         have one there",
        manifest.display(),
        root.display()
    );

    // 1. the workspace knows about it
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
        "expected `cargo metadata` on the candidate workspace at {} to succeed; observed status \
         {:?}.\n--- stderr ---\n{}",
        root.display(),
        meta.status,
        meta.stderr
    );
    let members = workspace_members_text(&meta.stdout).unwrap_or_else(|| {
        panic!(
            "`cargo metadata` produced no workspace_members array ({} bytes of stdout)",
            meta.stdout.len()
        )
    });
    assert!(
        members.contains("octets"),
        "expected the candidate's workspace to have an `octets` member; cargo reported \
         workspace_members = {members}"
    );

    // 2. a foreign crate can build against it, alone
    let probe = write_octets_probe(
        &dir,
        "fn main() {\n    let mut b = [0u8; 8];\n    let mut w = octets::OctetsMut::with_slice(&mut b);\n    \
         w.put_varint(1).unwrap();\n    println!(\"{}\", w.off());\n}\n",
    );
    let (build, bin) = build_octets_probe(&dir, &probe, "probe-build");
    assert!(
        build.ok(),
        "expected a crate outside the candidate's tree to build with `octets = {{ path = \"{}\" }}` \
         as its only dependency; `cargo build --offline` exited {:?}.\n--- cargo stderr ---\n{}",
        root.join("octets").display(),
        build.status,
        build.stderr
    );
    assert!(
        bin.is_file(),
        "expected `cargo build` to have produced {}; it did not",
        bin.display()
    );

    // 3. and the crate it resolved stands on nothing
    let lock_path = probe.join("Cargo.lock");
    let lock = std::fs::read_to_string(&lock_path)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", lock_path.display()));
    let mut names = lock_packages(&lock);
    names.sort();
    names.dedup();
    assert_eq!(
        names,
        vec!["octets".to_string(), "octets-probe".to_string()],
        "expected depending on `octets` to pull in nothing at all -- the lock file should name \
         only the probe and `octets`. It named {names:?}, so the extracted crate is not \
         self-contained.\n--- Cargo.lock ---\n{lock}"
    );
}

// ---------------------------------------------------------------------------
// cap-octets-api  (T4)
// ---------------------------------------------------------------------------

/// `cap-octets-api`: the extracted crate exports a working byte reader and
/// writer, under the names a consumer outside the workspace has to use.
///
/// The probe is an ordinary program compiled against the candidate's `octets`
/// and nothing else. It writes the four RFC 9000 Appendix A variable-length
/// integer examples -- 37, 15293, 494878333 and 151288809941952652, which are
/// exactly the four encoded lengths -- plus the fixed-width putters, reads
/// them all back through `Octets`, and asks `varint_len` and
/// `varint_parse_len` about the encoding directly. Then it runs off the end of
/// a one-byte buffer, which has to come back as `BufferTooShortError` rather
/// than a panic.
///
/// Everything asserted here is public surface of the *extracted crate*: a
/// candidate that moved the directory but kept the types private, renamed
/// `OctetsMut`, or re-exported a subset, fails on the build.
#[test]
fn cap_octets_api() {
    let dir = testkit::scratch("cap-octets-api");

    const MAIN: &str = r#"
fn main() {
    let mut buf = [0u8; 64];
    let n = {
        let mut b = octets::OctetsMut::with_slice(&mut buf);
        b.put_varint(37).unwrap();
        b.put_varint(15293).unwrap();
        b.put_varint(494878333).unwrap();
        b.put_varint(151288809941952652).unwrap();
        b.put_u8(0x42).unwrap();
        b.put_u16(0xbeef).unwrap();
        b.put_u24(0x0c0ffe).unwrap();
        b.put_u32(0xdeadbeef).unwrap();
        b.put_u64(0x0123456789abcdef).unwrap();
        b.put_bytes(b"quic").unwrap();
        b.off()
    };
    println!("LEN {}", n);

    let mut r = octets::Octets::with_slice(&buf[..n]);
    println!("V {} {} {} {}",
        r.get_varint().unwrap(), r.get_varint().unwrap(),
        r.get_varint().unwrap(), r.get_varint().unwrap());
    println!("F {} {} {} {} {}",
        r.get_u8().unwrap(), r.get_u16().unwrap(), r.get_u24().unwrap(),
        r.get_u32().unwrap(), r.get_u64().unwrap());
    println!("B {:?}", std::str::from_utf8(r.get_bytes(4).unwrap().buf()).unwrap());
    println!("CAP {}", r.cap());

    println!("VL {} {} {} {}",
        octets::varint_len(37), octets::varint_len(15293),
        octets::varint_len(494878333), octets::varint_len(151288809941952652));
    println!("VPL {} {} {} {}",
        octets::varint_parse_len(0x25), octets::varint_parse_len(0x7b),
        octets::varint_parse_len(0x9d), octets::varint_parse_len(0xc2));

    // 0xc2 is the first byte of an eight-byte varint; one byte is not enough.
    let tiny = [0xc2u8];
    let mut short = octets::Octets::with_slice(&tiny);
    let e: octets::BufferTooShortError = short.get_varint().unwrap_err();
    let as_err: &dyn std::error::Error = &e;
    println!("ERR {:?} {} {}", e, e, as_err);
    let mut short = octets::Octets::with_slice(&tiny);
    println!("SKIP {:?}", short.skip(4).unwrap_err());

    // peek_u8 reads without moving the cursor; get_u8 does.
    let mut rp = octets::Octets::with_slice(&[0x07u8, 0x99]);
    let peeked = rp.peek_u8().unwrap();
    let off_after_peek = rp.off();
    let read_again = rp.get_u8().unwrap();
    println!("PEEK {} {} {} {}", peeked, off_after_peek, read_again, rp.off());

    // the three length-prefixed readers, one prefix width each.
    let src8 = [3u8, b'a', b'b', b'c'];
    let mut r8 = octets::Octets::with_slice(&src8);
    let got8 = r8.get_bytes_with_u8_length().unwrap();
    println!("PFX8 {:?}", std::str::from_utf8(got8.buf()).unwrap());

    let src16 = [0u8, 3, b'x', b'y', b'z'];
    let mut r16 = octets::Octets::with_slice(&src16);
    let got16 = r16.get_bytes_with_u16_length().unwrap();
    println!("PFX16 {:?}", std::str::from_utf8(got16.buf()).unwrap());

    let srcv = [3u8, b'p', b'q', b'r'];
    let mut rv = octets::Octets::with_slice(&srcv);
    let gotv = rv.get_bytes_with_varint_length().unwrap();
    println!("PFXV {:?}", std::str::from_utf8(gotv.buf()).unwrap());

    // slice/slice_last hand back a sub-slice without consuming it.
    let srcs = [10u8, 20, 30, 40, 50];
    let rs1 = octets::Octets::with_slice(&srcs);
    println!("SLICE {:?}", rs1.slice(3).unwrap());
    let rs2 = octets::Octets::with_slice(&srcs);
    println!("SLICELAST {:?}", rs2.slice_last(2).unwrap());

    // is_empty answers about the whole buffer's length, not the cursor: it
    // stays false even once every byte has been read, and is only true on a
    // buffer that started at length zero.
    let mut re = octets::Octets::with_slice(&[9u8]);
    let empty_before = re.is_empty();
    let _ = re.get_u8().unwrap();
    let empty_after = re.is_empty();
    let empty_real = octets::Octets::with_slice(&[]).is_empty();
    println!("EMPTY {} {} {}", empty_before, empty_after, empty_real);

    // to_vec copies from the current offset onward, unlike is_empty above.
    let mut rt = octets::Octets::with_slice(&[1u8, 2, 3, 4]);
    rt.skip(1).unwrap();
    println!("TOVEC {:?}", rt.to_vec());

    // OctetsMut::split_at divides one buffer into two independent ones.
    let mut sb = [1u8, 2, 3, 4, 5, 6];
    {
        let mut sw = octets::OctetsMut::with_slice(&mut sb);
        let (left, right) = sw.split_at(2).unwrap();
        println!("SPLIT {:?} {:?}", left.buf(), right.buf());
    }

    // put_varint_with_len forces a wider encoding than the value needs.
    let mut fb = [0u8; 4];
    {
        let mut fw = octets::OctetsMut::with_slice(&mut fb);
        fw.put_varint_with_len(37, 4).unwrap();
    }
    println!("FORCED {:02x}{:02x}{:02x}{:02x}", fb[0], fb[1], fb[2], fb[3]);

    // a failed call must not move the cursor: a short read can be retried
    // against more bytes without having silently consumed any of them.
    let fo = [0xaau8, 0xbb];
    let mut rf = octets::Octets::with_slice(&fo);
    let before = rf.off();
    let failed = rf.get_u32().unwrap_err();
    let after = rf.off();
    println!("NOMOVE {} {} {:?}", before, after, failed);
    println!("RETRY {}", rf.get_u16().unwrap());
}
"#;

    let probe = write_octets_probe(&dir, MAIN);
    let (build, bin) = build_octets_probe(&dir, &probe, "api-build");
    assert!(
        build.ok(),
        "expected a program outside the candidate's tree to compile against the extracted \
         `octets` crate's public API -- `Octets`, `OctetsMut`, `BufferTooShortError`, \
         `varint_len`, `varint_parse_len`. `cargo build --offline` exited {:?}.\n\
         --- cargo stderr ---\n{}",
        build.status,
        build.stderr
    );

    let mut c = testkit::cmd(&bin);
    let run = testkit::run(&mut c, SHORT, &dir, "api-run");
    assert!(
        run.ok(),
        "expected the octets probe to run to completion; observed status {:?}.\n\
         --- stdout ---\n{}\n--- stderr ---\n{}",
        run.status,
        run.stdout,
        run.stderr
    );

    // 1 + 2 + 4 + 8 varint bytes, then 1 + 2 + 3 + 4 + 8 fixed, then 4 bytes.
    let want = "LEN 37\n\
                V 37 15293 494878333 151288809941952652\n\
                F 66 48879 790526 3735928559 81985529216486895\n\
                B \"quic\"\n\
                CAP 0\n\
                VL 1 2 4 8\n\
                VPL 1 2 4 8\n\
                ERR BufferTooShortError BufferTooShortError BufferTooShortError\n\
                SKIP BufferTooShortError\n\
                PEEK 7 0 7 1\n\
                PFX8 \"abc\"\n\
                PFX16 \"xyz\"\n\
                PFXV \"pqr\"\n\
                SLICE [10, 20, 30]\n\
                SLICELAST [40, 50]\n\
                EMPTY false false true\n\
                TOVEC [2, 3, 4]\n\
                SPLIT [1, 2] [3, 4, 5, 6]\n\
                FORCED 80000025\n\
                NOMOVE 0 0 BufferTooShortError\n\
                RETRY 43707\n";
    assert_eq!(
        run.stdout, want,
        "the extracted byte reader did not round-trip the RFC 9000 Appendix A varint examples \
         and the fixed-width accessors.\nexpected:\n{want}\nobserved:\n{}\n--- stderr ---\n{}",
        run.stdout, run.stderr
    );
}

// ---------------------------------------------------------------------------
// cap-priority-update  (T3)
// ---------------------------------------------------------------------------

/// `cap-priority-update`: a server reads PRIORITY_UPDATE off the peer's
/// control stream, raises one event per stored update, and hands the priority
/// field value back verbatim.
///
/// The verifier builds the frame itself -- `varint(0xF0700)`, a length, the
/// prioritized element ID and the field value, all written with
/// `quiche_verifier_support`'s varint writer -- and pushes it onto the
/// transport stream the client's HTTP/3 control stream lives on. So this
/// measures the candidate's HTTP/3 *reader*, not a round trip through its own
/// writer.
///
/// The event is edge-triggered against stored state, and that is the part
/// worth pinning down:
///
/// * the first PRIORITY_UPDATE for element 0 raises `(0, PriorityUpdate)`,
///   and polling again returns `Done`;
/// * a second PRIORITY_UPDATE for the same element, while the first is still
///   unread, raises nothing -- but replaces the stored value;
/// * `take_last_priority_update(0)` returns the *latest* field value and
///   takes it, so an immediate second call returns `Done`;
/// * having been taken, the event rearms: a later PRIORITY_UPDATE for the
///   same element raises it again;
/// * a distinct element ID is tracked separately.
#[test]
fn cap_priority_update() {
    let mut h = pair::H3Pair::new();

    // 1. the first update raises exactly one event
    send_h3_frame(
        &mut h.t.client,
        pair::H3_CLIENT_CONTROL_STREAM,
        PRIORITY_UPDATE_REQUEST,
        &pair::priority_update_payload(0, b"u=3"),
    );
    h.settle();
    assert_eq!(
        h.poll_server(),
        Ok((0, quiche::h3::Event::PriorityUpdate)),
        "expected a PRIORITY_UPDATE for element 0 on the peer's control stream to raise \
         (0, Event::PriorityUpdate) at the server"
    );
    assert_eq!(
        h.poll_server(),
        Err(quiche::h3::Error::Done),
        "expected exactly one event for one PRIORITY_UPDATE frame"
    );

    // 2. a second update for the same element, unread, raises nothing but
    //    replaces the stored value
    send_h3_frame(
        &mut h.t.client,
        pair::H3_CLIENT_CONTROL_STREAM,
        PRIORITY_UPDATE_REQUEST,
        &pair::priority_update_payload(0, b"u=5"),
    );
    h.settle();
    assert_eq!(
        h.poll_server(),
        Err(quiche::h3::Error::Done),
        "expected a second PRIORITY_UPDATE for element 0 not to raise a second event while the \
         first one is still unread: the event tracks stored state, not frames"
    );

    // 3. taking it yields the latest value, once
    assert_eq!(
        h.server.take_last_priority_update(0),
        Ok(b"u=5".to_vec()),
        "expected take_last_priority_update(0) to hand back the most recent priority field \
         value, byte for byte"
    );
    assert_eq!(
        h.server.take_last_priority_update(0),
        Err(quiche::h3::Error::Done),
        "expected take_last_priority_update to take: a second call with nothing stored is Done"
    );

    // 4. and the event rearms
    send_h3_frame(
        &mut h.t.client,
        pair::H3_CLIENT_CONTROL_STREAM,
        PRIORITY_UPDATE_REQUEST,
        &pair::priority_update_payload(0, b"u=7, i"),
    );
    h.settle();
    assert_eq!(
        h.poll_server(),
        Ok((0, quiche::h3::Event::PriorityUpdate)),
        "expected the event to rearm once the stored update had been taken"
    );
    assert_eq!(
        h.server.take_last_priority_update(0),
        Ok(b"u=7, i".to_vec()),
        "expected the field value to come back uninterpreted, including its spaces"
    );

    // 5. a different element is tracked separately
    send_h3_frame(
        &mut h.t.client,
        pair::H3_CLIENT_CONTROL_STREAM,
        PRIORITY_UPDATE_REQUEST,
        &pair::priority_update_payload(4, b"u=1"),
    );
    h.settle();
    assert_eq!(
        h.poll_server(),
        Ok((4, quiche::h3::Event::PriorityUpdate)),
        "expected the prioritized element ID from the frame to be the event's ID"
    );
    assert_eq!(
        h.server.take_last_priority_update(4),
        Ok(b"u=1".to_vec()),
        "expected element 4's update to be stored beside element 0's, not to replace it"
    );
    assert_eq!(
        h.server.take_last_priority_update(0),
        Err(quiche::h3::Error::Done),
        "expected element 0 to still have nothing stored after element 4 was updated"
    );

    assert!(
        !h.t.server.is_closed() && !h.t.client.is_closed(),
        "expected five well-formed PRIORITY_UPDATE frames to leave the connection open; \
         server local error {:?}, client local error {:?}",
        h.t.server.local_error().map(|e| e.error_code),
        h.t.client.local_error().map(|e| e.error_code)
    );
}

// ---------------------------------------------------------------------------
// cap-priority-update-guards  (T3)
// ---------------------------------------------------------------------------

/// `cap-priority-update-guards`: the three ways a PRIORITY_UPDATE is rejected.
///
/// Reading a new frame type is half the work; refusing it where it does not
/// belong is the other half, and each refusal has its own wire error code:
///
/// * a *client* that receives one closes the connection with
///   `H3_FRAME_UNEXPECTED` (0x105) -- PRIORITY_UPDATE is a client-to-server
///   frame;
/// * a prioritized element ID that is not a client-initiated bidirectional
///   stream ID -- not a multiple of four -- is `H3_FRAME_UNEXPECTED` as well;
/// * an ID past the bidirectional stream concurrency limit the server itself
///   granted is `H3_ID_ERROR` (0x108), a different code, because the frame is
///   well formed and the ID is merely out of range.
///
/// Each case gets a fresh pair, because the first one closes the connection.
#[test]
fn cap_priority_update_guards() {
    // 1. wrong role
    let mut h = pair::H3Pair::new();
    send_h3_frame(
        &mut h.t.server,
        pair::H3_SERVER_CONTROL_STREAM,
        PRIORITY_UPDATE_REQUEST,
        &pair::priority_update_payload(0, b"u=3"),
    );
    h.settle_closing();
    assert_eq!(
        h.poll_client(),
        Err(quiche::h3::Error::FrameUnexpected),
        "expected a client that receives PRIORITY_UPDATE to report FrameUnexpected from poll"
    );
    assert_eq!(
        h.t.client.local_error().map(|e| (e.is_app, e.error_code)),
        Some((true, H3_FRAME_UNEXPECTED)),
        "expected the client to close with the application error code H3_FRAME_UNEXPECTED \
         ({H3_FRAME_UNEXPECTED:#x}); it reported {:?}",
        h.t.client.local_error().map(|e| (e.is_app, e.error_code))
    );
    h.settle_closing();
    assert_eq!(
        h.t.server.peer_error().map(|e| (e.is_app, e.error_code)),
        Some((true, H3_FRAME_UNEXPECTED)),
        "expected the CONNECTION_CLOSE to reach the server carrying {H3_FRAME_UNEXPECTED:#x}; \
         the server saw {:?}",
        h.t.server.peer_error().map(|e| (e.is_app, e.error_code))
    );

    // 2. an element ID that is not a client-initiated bidirectional stream
    let mut h = pair::H3Pair::new();
    send_h3_frame(
        &mut h.t.client,
        pair::H3_CLIENT_CONTROL_STREAM,
        PRIORITY_UPDATE_REQUEST,
        &pair::priority_update_payload(2, b"u=3"),
    );
    h.settle_closing();
    assert_eq!(
        h.poll_server(),
        Err(quiche::h3::Error::FrameUnexpected),
        "expected PRIORITY_UPDATE for element 2 -- a unidirectional stream ID -- to be rejected \
         as FrameUnexpected"
    );
    assert_eq!(
        h.t.server.local_error().map(|e| (e.is_app, e.error_code)),
        Some((true, H3_FRAME_UNEXPECTED)),
        "expected the server to close with {H3_FRAME_UNEXPECTED:#x} for a request-stream \
         PRIORITY_UPDATE whose element ID is not a multiple of four; it reported {:?}",
        h.t.server.local_error().map(|e| (e.is_app, e.error_code))
    );

    // 3. an element ID beyond the concurrency limit
    let mut h = pair::H3Pair::new();
    send_h3_frame(
        &mut h.t.client,
        pair::H3_CLIENT_CONTROL_STREAM,
        PRIORITY_UPDATE_REQUEST,
        &pair::priority_update_payload(4000, b"u=3"),
    );
    h.settle_closing();
    assert_eq!(
        h.poll_server(),
        Err(quiche::h3::Error::IdError),
        "expected PRIORITY_UPDATE for element 4000, past the 100 bidirectional streams the \
         server granted, to be rejected as IdError rather than FrameUnexpected"
    );
    assert_eq!(
        h.t.server.local_error().map(|e| (e.is_app, e.error_code)),
        Some((true, H3_ID_ERROR)),
        "expected the server to close with H3_ID_ERROR ({H3_ID_ERROR:#x}); it reported {:?}",
        h.t.server.local_error().map(|e| (e.is_app, e.error_code))
    );
}

// ---------------------------------------------------------------------------
// cap-h3-qlog  (T4)
// ---------------------------------------------------------------------------

/// `cap-h3-qlog`: the qlog a connection emits now describes HTTP/3, not just
/// the transport.
///
/// Both of the candidate's apps are run with `QLOGDIR` set and one HTTP/3 GET
/// is performed. The two `.sqlog` files that come back have to carry the
/// HTTP/3 event category alongside the transport one:
///
/// * `http:stream_type_set` naming the local control stream, stream 2 on the
///   client and stream 3 on the server, and the QPACK encoder and decoder
///   streams beside it;
/// * `http:frame_created` for the SETTINGS frame each end writes onto its own
///   control stream, and for the client's HEADERS frame, with the request
///   pseudo-headers spelled out rather than left as an opaque blob;
/// * `http:frame_parsed` at the other end for the same frames, and for the
///   DATA frame carrying the body.
///
/// The previous milestone emits a qlog for the same exchange with no `http:`
/// event in it at all: it logs the transport only.
#[test]
fn cap_h3_qlog() {
    let dir = testkit::scratch("cap-h3-qlog");
    let qlogdir = dir.join("qlog");
    std::fs::create_dir_all(&qlogdir).expect("create qlog dir");

    let body = testkit::body(4096, 0x0714_0001);
    let port = testkit::free_port();
    let server = start_app_server(
        &dir,
        port,
        &[("index.html", body.clone())],
        "qlog-server",
        &[("QLOGDIR", &qlogdir)],
    );

    let url = format!("https://127.0.0.1:{port}/index.html");
    let run = app_client_get(&dir, &url, "qlog-client", &[("QLOGDIR", &qlogdir)]);
    let log = server.log();
    assert!(
        run.ok(),
        "expected {url} to be fetched over HTTP/3; the client exited {:?} (timed out: {}).\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        run.status,
        run.timed_out,
        run.stderr
    );
    assert_eq!(
        run.stdout_bytes, body,
        "expected the {} byte body on the client's stdout; observed {} bytes",
        body.len(),
        run.stdout_bytes.len()
    );
    drop(server);

    let mut client_log = String::new();
    let mut server_log = String::new();
    let mut seen = Vec::new();
    for e in std::fs::read_dir(&qlogdir).expect("read qlog dir") {
        let p = e.expect("qlog dir entry").path();
        let name = p.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
        seen.push(name.clone());
        let text = std::fs::read_to_string(&p).unwrap_or_default();
        if name.starts_with("client") {
            client_log = text;
        } else if name.starts_with("server") {
            server_log = text;
        }
    }
    assert!(
        !client_log.is_empty() && !server_log.is_empty(),
        "expected a client and a server qlog in {} after one HTTP/3 GET; found {seen:?}",
        qlogdir.display()
    );

    // Every assertion below is "some record line contains all of these".
    let has = |log: &str, needles: &[&str]| -> bool {
        log.lines().any(|l| needles.iter().all(|n| l.contains(n)))
    };
    let categories = |log: &str| -> Vec<String> {
        let mut v: Vec<String> = log
            .lines()
            .filter_map(|l| l.split("\"name\":\"").nth(1))
            .filter_map(|l| l.split('"').next())
            .map(|s| s.to_string())
            .collect();
        v.sort();
        v.dedup();
        v
    };

    for (role, log, control, peer_control) in [
        ("client", &client_log, "\"stream_id\":2", "\"stream_id\":3"),
        ("server", &server_log, "\"stream_id\":3", "\"stream_id\":2"),
    ] {
        assert!(
            has(log, &["\"http:stream_type_set\"", control, "\"owner\":\"local\"", "\"control\""]),
            "expected the {role} qlog to record its own control stream being opened -- an \
             http:stream_type_set event with {control}, owner local, type control. The event \
             names it recorded were {:?}",
            categories(log)
        );
        assert!(
            has(log, &["\"http:stream_type_set\"", peer_control, "\"owner\":\"remote\"", "\"control\""]),
            "expected the {role} qlog to record the peer's control stream with {peer_control} \
             and owner remote"
        );
        for kind in ["qpack_encode", "qpack_decode"] {
            assert!(
                has(log, &["\"http:stream_type_set\"", "\"owner\":\"local\"", kind]),
                "expected the {role} qlog to record its own {kind} stream"
            );
            assert!(
                has(log, &["\"http:stream_type_set\"", "\"owner\":\"remote\"", kind]),
                "expected the {role} qlog to record the peer's {kind} stream too -- \
                 http:stream_type_set is documented for both QPACK streams, not just this \
                 endpoint's own"
            );
        }
        assert!(
            has(log, &["\"http:frame_created\"", control, "\"frame_type\":\"settings\""]),
            "expected the {role} qlog to record the SETTINGS frame it wrote onto {control} as an \
             http:frame_created event"
        );
    }

    assert!(
        has(&client_log, &["\"http:frame_created\"", "\"frame_type\":\"headers\"", "\":method\"", "GET"]),
        "expected the client qlog to record the request HEADERS frame with its pseudo-headers \
         decoded -- :method GET -- rather than as an opaque payload"
    );
    assert!(
        has(&server_log, &["\"http:frame_parsed\"", "\"frame_type\":\"headers\"", "\":method\"", "GET"]),
        "expected the server qlog to record the same HEADERS frame on the way in as an \
         http:frame_parsed event"
    );
    assert!(
        has(&server_log, &["\"http:frame_created\"", "\"frame_type\":\"headers\"", "\":status\""]),
        "expected the server qlog to record the response HEADERS frame it created"
    );
    assert!(
        has(&client_log, &["\"http:frame_parsed\"", "\"frame_type\":\"data\""]),
        "expected the client qlog to record the DATA frame carrying the {} byte body",
        body.len()
    );
}

// ---------------------------------------------------------------------------
// cap-cc-trait-complete  (T3)
// ---------------------------------------------------------------------------

/// `cap-cc-trait-complete`: the congestion controller reports its send
/// quantum through the connection's public API.
///
/// The congestion-control interface grew at this milestone -- acknowledgement
/// handling became a batch operation and the loss signal carries the number
/// of bytes lost -- and the one part of that work a caller can see is
/// `Connection::send_quantum()`: how many bytes the controller is willing to
/// have released in a single burst. It is the controller's own number, and it
/// is derived from the maximum datagram size the endpoint was configured to
/// send, ten packets' worth:
///
/// * a connection configured to send 1452-byte datagrams reports 14520;
/// * a connection configured to send 1200-byte datagrams reports 12000;
/// * the two ends of one connection report their own configured values, not
///   the peer's, and not the negotiated minimum;
/// * the number is stable across the handshake and a transfer, because it is
///   the initial window and nothing in an uncongested in-memory exchange
///   moves it.
///
/// A stack whose congestion controller has no send-quantum concept has
/// nothing to return here.
#[test]
fn cap_cc_trait_complete() {
    const PACKETS: usize = 10;

    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.set_max_send_udp_payload_size(1200);
    // the server keeps the pair's default of pair::MAX_DATAGRAM
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);

    let before = p.client.send_quantum();
    assert_eq!(
        before,
        1200 * PACKETS,
        "expected a client configured to send 1200-byte datagrams to report a send quantum of \
         {} bytes -- {PACKETS} packets -- before the handshake; observed {before}",
        1200 * PACKETS
    );

    p.handshake();
    p.settle(16);

    let client_q = p.client.send_quantum();
    let server_q = p.server.send_quantum();
    assert_eq!(
        client_q,
        1200 * PACKETS,
        "expected the client's send quantum to still be {} after the handshake; observed \
         {client_q}",
        1200 * PACKETS
    );
    assert_eq!(
        server_q,
        pair::MAX_DATAGRAM * PACKETS,
        "expected the server, configured to send {}-byte datagrams, to report its own send \
         quantum of {} rather than the client's {client_q}; observed {server_q}",
        pair::MAX_DATAGRAM,
        pair::MAX_DATAGRAM * PACKETS
    );

    // A transfer far larger than the initial window does not move it: this is
    // the controller's burst size, not its congestion window. The first
    // `stream_send` is itself capped at the initial window, which is why this
    // is a loop and not one call.
    let data = testkit::body(256 * 1024, 0x0714_0002);
    let first = p.client.stream_send(0, &data, false).expect("first stream_send");
    assert_eq!(
        first, client_q,
        "expected the very first stream_send on a fresh connection to be limited to the initial \
         congestion window, which is the send quantum ({client_q} bytes); it accepted {first}"
    );

    let mut off = first;
    let mut got = Vec::new();
    for _ in 0..512 {
        if off < data.len() {
            // Once the send capacity is exhausted the call is `Done` rather
            // than `Ok(0)`; the loop drains and comes back.
            match p.client.stream_send(0, &data[off..], false) {
                Ok(n) => off += n,
                Err(quiche::Error::Done) => {},
                Err(e) => panic!("stream_send at offset {off}: {e:?}"),
            }
        }
        p.exchange();
        let (chunk, _) = pair::read_stream(&mut p.server, 0);
        got.extend_from_slice(&chunk);
        p.exchange();
        if off == data.len() && got.len() == data.len() {
            break;
        }
    }
    assert_eq!(
        got.len(),
        data.len(),
        "expected the whole {} byte transfer to arrive; {} bytes did",
        data.len(),
        got.len()
    );
    assert!(got == data, "expected the transfer to arrive byte for byte");

    assert!(
        p.client.stats().sent > 20 * PACKETS,
        "expected a {} byte transfer to have taken well over {PACKETS} packets, or the next \
         assertion proves nothing; the client sent {}",
        data.len(),
        p.client.stats().sent
    );
    assert!(
        p.client.stats().cwnd > client_q,
        "expected the congestion window to have grown past the initial {client_q} bytes during \
         the transfer; it is {}",
        p.client.stats().cwnd
    );
    assert_eq!(
        p.client.send_quantum(),
        client_q,
        "expected the send quantum to be the controller's fixed burst size, unchanged by an \
         uncongested {} byte transfer that grew the congestion window to {}",
        data.len(),
        p.client.stats().cwnd
    );
}

// ---------------------------------------------------------------------------
// cap-peer-cert-borrowed  (T3)
// ---------------------------------------------------------------------------

/// `cap-peer-cert-borrowed`: the peer's certificate and the session ticket
/// are handed out as borrows of what the connection already holds.
///
/// Both accessors stopped copying at this milestone: `peer_cert` and
/// `session` return `Option<&[u8]>` where they used to return
/// `Option<Vec<u8>>`. The group binds the results to `Option<&[u8]>`
/// explicitly -- a stack that still clones fails to compile against that,
/// which is the API change -- and then checks the borrow is real and the
/// bytes are right:
///
/// * two calls can be held live at once, which an owning accessor taking
///   `&self` would also allow, but which pins the shape of the borrow;
/// * the DER is exactly the fixture leaf certificate the pair's server
///   config was loaded with, base64-decoded by the verifier from the PEM;
/// * the server, which asked for no client certificate, reports `None`;
/// * a client that has resumed nothing reports `Some` for `session` only
///   once the server has actually issued a ticket, and whatever it reports
///   is a borrow of the same length on two consecutive calls.
#[test]
fn cap_peer_cert_borrowed() {
    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();
    p.settle(16);

    let der: Option<&[u8]> = p.client.peer_cert();
    let der = der.expect(
        "expected the client to have the server's certificate after a completed handshake; \
         peer_cert returned None",
    );

    let pem = std::fs::read_to_string(testkit::tls().cert).expect("read fixture certificate");
    let want = pem_der(&pem);
    assert!(
        !want.is_empty(),
        "the fixture certificate did not decode; this is a harness fault, not a candidate one"
    );
    assert_eq!(
        der.len(),
        want.len(),
        "expected peer_cert to be the {} byte DER of the fixture leaf certificate; observed {} \
         bytes starting {}",
        want.len(),
        der.len(),
        testkit::hex(&der[..16.min(der.len())])
    );
    assert!(
        der == want.as_slice(),
        "expected peer_cert to be the fixture leaf certificate byte for byte; the first {} bytes \
         observed were {}",
        16.min(der.len()),
        testkit::hex(&der[..16.min(der.len())])
    );

    // Two borrows alive at once.
    let a: Option<&[u8]> = p.client.peer_cert();
    let b: Option<&[u8]> = p.client.peer_cert();
    assert_eq!(
        a, b,
        "expected two peer_cert borrows taken from the same connection to be equal and usable \
         together"
    );

    let server_side: Option<&[u8]> = p.server.peer_cert();
    assert!(
        server_side.is_none(),
        "expected the server, which requested no client certificate, to report no peer \
         certificate; it reported {} bytes",
        server_side.map(|s| s.len()).unwrap_or(0)
    );

    let s1: Option<&[u8]> = p.client.session();
    let s2: Option<&[u8]> = p.client.session();
    assert_eq!(
        s1.map(|s| s.len()),
        s2.map(|s| s.len()),
        "expected session() to be a stable borrow of the stored ticket rather than a fresh \
         allocation of a different length"
    );
    assert_eq!(s1, s2, "expected two session() borrows to be equal");
}
