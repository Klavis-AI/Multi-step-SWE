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

use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

/// Where the first byte of `want` and `got` disagree, rendered for a message.
#[allow(dead_code)]
fn first_diff(want: &[u8], got: &[u8]) -> String {
    match want.iter().zip(got.iter()).position(|(a, b)| a != b) {
        Some(off) => format!(
            "first difference at offset {off}: expected {}, observed {}",
            testkit::hex(&want[off..(off + 16).min(want.len())]),
            testkit::hex(&got[off..(off + 16).min(got.len())])
        ),
        None => format!(
            "no differing byte in the first {} bytes (expected {} bytes, observed {})",
            want.len().min(got.len()),
            want.len(),
            got.len()
        ),
    }
}

// ---------------------------------------------------------------------------
// cap-stream-transfer  (T1)
// ---------------------------------------------------------------------------

/// `cap-stream-transfer`: 512 KiB of pseudorandom bytes through the candidate's
/// own `client` and `server`, byte for byte.
///
/// 512 KiB is 370 packets at the 1452-byte maximum, so it cannot fit in the
/// initial congestion window and cannot be delivered without the receiver
/// reassembling stream data that arrived across many packets and possibly out
/// of order. A stack that drops, duplicates or misorders a range produces
/// wrong bytes rather than right ones by luck.
#[test]
fn cap_stream_transfer() {
    const LEN: usize = 512 * 1024;

    let dir = testkit::scratch("cap-stream-transfer");
    let tls = testkit::tls();
    let want = testkit::body(LEN, 0x5123_4567);
    let root = testkit::docroot(&dir, &[("big.bin", want.clone())]);
    let port = testkit::free_port();

    let server_bin = testkit::server_bin();
    let client_bin = testkit::client_bin();

    let mut sc = testkit::cmd(&server_bin);
    sc.arg("--listen")
        .arg(format!("127.0.0.1:{port}"))
        .arg("--cert")
        .arg(&tls.cert)
        .arg("--key")
        .arg(&tls.key)
        .arg("--root")
        .arg(&root)
        .arg("--max-data")
        .arg("10000000")
        .arg("--max-stream-data")
        .arg("1000000")
        .arg("--no-retry");
    let server = testkit::spawn_server(&mut sc, port, &dir, "xfer-server", SHORT);

    let url = format!("https://127.0.0.1:{port}/big.bin");
    let mut cc = testkit::cmd(&client_bin);
    cc.arg(&url)
        .arg("--no-verify")
        .arg("--wire-version")
        .arg("ff000017")
        .arg("--max-data")
        .arg("10000000")
        .arg("--max-stream-data")
        .arg("1000000");
    let run = testkit::run(&mut cc, LONG, &dir, "xfer-client");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to fetch {LEN} bytes from {url} within {LONG:?}; observed: still running at \
         the deadline, killed after writing {} of {LEN} bytes.\n--- client stderr ---\n{}\n\
         --- server log ---\n{log}",
        client_bin.display(),
        run.stdout_bytes.len(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to exit 0 after fetching {url}; observed status {:?} having written {} of \
         {LEN} bytes.\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
        run.status,
        run.stdout_bytes.len(),
        run.stderr
    );

    let got = &run.stdout_bytes;
    assert_eq!(
        got.len(),
        LEN,
        "expected exactly {LEN} bytes of {url} on the client's stdout; observed {} bytes ({}).\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        got.len(),
        first_diff(&want, got),
        run.stderr
    );
    assert!(
        want == *got,
        "expected the 512 KiB body of {url} back byte for byte; observed {}.\n\
         --- server log ---\n{log}",
        first_diff(&want, got)
    );

    drop(server);
}

// ---------------------------------------------------------------------------
// cap-h3-get  (T1)
// ---------------------------------------------------------------------------

/// `cap-h3-get`: a GET through the candidate's own HTTP/3 binaries.
///
/// Everything HTTP/3 has to be right for the body to come back: the `h3-23`
/// ALPN has to be negotiated, both peers have to open and read each other's
/// control streams and SETTINGS, the request has to be a QPACK-encoded HEADERS
/// frame the server can decode into a `:path`, and the response has to be
/// HEADERS followed by DATA that the client can frame back out again.
#[test]
fn cap_h3_get() {
    const LEN: usize = 64 * 1024;

    let dir = testkit::scratch("cap-h3-get");
    let tls = testkit::tls();
    let want = testkit::body(LEN, 0x0333_0001);
    let root = testkit::docroot(&dir, &[("payload.bin", want.clone())]);
    let port = testkit::free_port();

    let server_bin = testkit::h3_server_bin();
    let client_bin = testkit::h3_client_bin();

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
    let server = testkit::spawn_server(&mut sc, port, &dir, "h3-server", SHORT);

    let url = format!("https://127.0.0.1:{port}/payload.bin");
    let mut cc = testkit::cmd(&client_bin);
    cc.arg(&url).arg("--no-verify").arg("--wire-version").arg("ff000017");
    let run = testkit::run(&mut cc, LONG, &dir, "h3-client");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to GET {url} over h3-23 within {LONG:?}; observed: still running at the \
         deadline, killed after writing {} of {LEN} body bytes.\n--- client stderr ---\n{}\n\
         --- server log ---\n{log}",
        client_bin.display(),
        run.stdout_bytes.len(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to exit 0 after GET {url}; observed status {:?} having written {} of {LEN} \
         body bytes.\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
        run.status,
        run.stdout_bytes.len(),
        run.stderr
    );

    let got = &run.stdout_bytes;
    assert_eq!(
        got.len(),
        LEN,
        "expected the {LEN} byte response body of {url} on the HTTP/3 client's stdout; observed \
         {} bytes ({}).\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        got.len(),
        first_diff(&want, got),
        run.stderr
    );
    assert!(
        want == *got,
        "expected the HTTP/3 response body of {url} back byte for byte; observed {}.\n\
         --- server log ---\n{log}",
        first_diff(&want, got)
    );

    drop(server);
}

// ---------------------------------------------------------------------------
// cap-c-api  (T1)
// ---------------------------------------------------------------------------

/// The C program the grader compiles for `cap-c-api`.
///
/// It reports what it saw as `key=value` lines rather than asserting for
/// itself, so that a mismatch is diagnosed in Rust with the whole observation
/// in the message instead of as a bare non-zero exit from `cc`'s output.
const C_API_PROGRAM: &str = r##"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include <quiche.h>

static void put_hex(const char *key, const uint8_t *b, size_t len) {
    size_t i;
    printf("%s=", key);
    for (i = 0; i < len; i++) {
        printf("%02x", b[i]);
    }
    printf("\n");
}

int main(void) {
    const char *v = quiche_version();
    if (v == NULL) {
        printf("fail=quiche_version_returned_null\n");
        return 2;
    }
    printf("version_string=%s\n", v);
    printf("macro_protocol_version=%08x\n", (unsigned int) QUICHE_PROTOCOL_VERSION);
    printf("macro_max_conn_id_len=%u\n", (unsigned int) QUICHE_MAX_CONN_ID_LEN);
    printf("macro_min_client_initial_len=%u\n", (unsigned int) QUICHE_MIN_CLIENT_INITIAL_LEN);

    quiche_config *config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (config == NULL) {
        printf("fail=quiche_config_new_returned_null\n");
        return 3;
    }

    quiche_config_set_application_protos(config, (uint8_t *) "\x05hq-23", 6);
    quiche_config_verify_peer(config, false);
    quiche_config_set_idle_timeout(config, 5000);
    quiche_config_set_max_packet_size(config, 1350);
    quiche_config_set_initial_max_data(config, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_local(config, 100000);
    quiche_config_set_initial_max_streams_bidi(config, 10);

    uint8_t cid[QUICHE_MAX_CONN_ID_LEN];
    size_t i;
    for (i = 0; i < sizeof(cid); i++) {
        cid[i] = (uint8_t) (0xc0 + i);
    }
    put_hex("asked_cid", cid, sizeof(cid));

    quiche_conn *conn = quiche_connect("quic.test", cid, sizeof(cid), config);
    if (conn == NULL) {
        printf("fail=quiche_connect_returned_null\n");
        return 4;
    }

    uint8_t out[2048];
    ssize_t written = quiche_conn_send(conn, out, sizeof(out));
    printf("send_rc=%ld\n", (long) written);
    if (written <= 0) {
        printf("fail=quiche_conn_send_produced_nothing\n");
        return 5;
    }
    printf("datagram_len=%ld\n", (long) written);
    printf("first_byte=%02x\n", out[0]);

    uint32_t version = 0;
    uint8_t type = 0xff;
    uint8_t hscid[QUICHE_MAX_CONN_ID_LEN];
    uint8_t hdcid[QUICHE_MAX_CONN_ID_LEN];
    uint8_t token[256];
    size_t hscid_len = sizeof(hscid);
    size_t hdcid_len = sizeof(hdcid);
    size_t token_len = sizeof(token);

    memset(hscid, 0, sizeof(hscid));
    memset(hdcid, 0, sizeof(hdcid));
    memset(token, 0, sizeof(token));

    int rc = quiche_header_info(out, (size_t) written, QUICHE_MAX_CONN_ID_LEN,
                                &version, &type,
                                hscid, &hscid_len,
                                hdcid, &hdcid_len,
                                token, &token_len);
    printf("header_info_rc=%d\n", rc);
    if (rc < 0) {
        printf("fail=quiche_header_info_failed\n");
        quiche_conn_free(conn);
        quiche_config_free(config);
        return 6;
    }

    printf("header_version=%08x\n", (unsigned int) version);
    printf("header_type=%u\n", (unsigned int) type);
    put_hex("header_scid", hscid, hscid_len);
    put_hex("header_dcid", hdcid, hdcid_len);
    printf("header_token_len=%lu\n", (unsigned long) token_len);
    printf("established=%d\n", quiche_conn_is_established(conn) ? 1 : 0);
    printf("closed=%d\n", quiche_conn_is_closed(conn) ? 1 : 0);

    quiche_conn_free(conn);
    quiche_config_free(config);

    printf("done=1\n");
    return 0;
}
"##;

/// The directory holding the candidate's C-linkable libraries, wherever
/// `cargo` put them, and whether the `cdylib` is there beside the `staticlib`.
///
/// The C program is linked against the `staticlib`: it is the self-contained
/// one — a `cdylib` built from a crate that links a static BoringSSL keeps
/// undefined references to it, which no C link line can satisfy from outside.
/// Both are required by the `crate-type` the step's instruction fixes, so the
/// group checks that both were produced and links the one that can be linked.
#[allow(dead_code)]
fn c_lib_dir() -> (std::path::PathBuf, bool) {
    let t = testkit::candidate_target_dir();
    let r = testkit::candidate_root();
    let mut dirs = vec![t.join("debug"), t.join("release")];
    for base in [r.join("target"), r.join("quiche").join("target")] {
        dirs.push(base.join("debug"));
        dirs.push(base.join("release"));
    }
    for d in &dirs {
        if d.join("libquiche.a").is_file() {
            return (d.clone(), d.join("libquiche.so").is_file());
        }
    }
    panic!(
        "no libquiche.a was built. Looked for it in:\n  {}\n\
         The step's instruction requires crate-type = [\"lib\", \"staticlib\", \"cdylib\"] so \
         that a C program can link against the library.",
        dirs.iter().map(|d| d.display().to_string()).collect::<Vec<_>>().join("\n  ")
    );
}

/// The first C compiler on PATH that answers `--version`.
#[allow(dead_code)]
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

/// `cap-c-api`: the C header is valid C, the `cdylib` exports the symbols, and
/// the connection it builds emits a real draft-23 client Initial.
///
/// Nothing here goes through the Rust API: the header is compiled by a C
/// compiler and the library is linked as a shared object, so a crate that is
/// only a `lib` — or a header that does not parse, or a symbol that was never
/// `extern "C"` — fails at the compile or link step with the compiler's own
/// message.
#[test]
fn cap_c_api() {
    let dir = testkit::scratch("cap-c-api");
    let src = dir.join("capi.c");
    let exe = dir.join("capi");
    std::fs::write(&src, C_API_PROGRAM).expect("write C program");

    let include = testkit::candidate_root().join("include");
    let header = include.join("quiche.h");
    assert!(
        header.is_file(),
        "expected the C header at {}; observed: no such file. The step's instruction requires \
         /app/include/quiche.h.",
        header.display()
    );
    let (libdir, has_cdylib) = c_lib_dir();
    assert!(
        has_cdylib,
        "expected a cdylib beside the staticlib at {}; observed libquiche.a but no \
         libquiche.so. The step's instruction fixes crate-type = [\"lib\", \"staticlib\", \
         \"cdylib\"].",
        libdir.display()
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
    let build = testkit::run(&mut b, SHORT, &dir, "capi-build");
    assert!(
        build.ok(),
        "expected `{cc}` to compile a C program against {} and link it against {}/libquiche.a; \
         observed status {:?}.\n--- compiler stdout ---\n{}\n--- compiler stderr ---\n{}",
        header.display(),
        libdir.display(),
        build.status,
        build.stdout,
        build.stderr
    );

    let mut r = testkit::cmd(&exe);
    let run = testkit::run(&mut r, SHORT, &dir, "capi-run");
    assert!(
        !run.timed_out,
        "expected the C program to finish within {SHORT:?}; observed: still running at the \
         deadline, killed.\n--- its stdout ---\n{}\n--- its stderr ---\n{}",
        run.stdout, run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected the C program to exit 0; observed status {:?}.\n--- its stdout ---\n{}\n\
         --- its stderr ---\n{}",
        run.status,
        run.stdout,
        run.stderr
    );

    let m = testkit::kv(&run.stdout);
    let all = run.output();
    let get = |k: &str| -> String {
        m.get(k).cloned().unwrap_or_else(|| {
            panic!("the C program did not report `{k}`.\n--- everything it printed ---\n{all}")
        })
    };

    assert_eq!(
        get("done"),
        "1",
        "expected the C program to reach the end of main; observed its output stopping \
         early.\n--- everything it printed ---\n{all}"
    );
    assert!(
        !get("version_string").is_empty(),
        "expected quiche_version() to return a non-empty string; observed an empty \
         one.\n--- everything it printed ---\n{all}"
    );

    let dgram_len: usize = get("datagram_len").parse().unwrap_or_else(|e| {
        panic!("unparseable datagram_len from the C program ({e}).\n--- its output ---\n{all}")
    });
    assert!(
        dgram_len >= 1200,
        "expected the first datagram quiche_conn_send() produced to be padded to at least the \
         1200-byte minimum a draft-23 client Initial must reach; observed {dgram_len} \
         bytes.\n--- the C program's output ---\n{all}"
    );

    let first_byte = u8::from_str_radix(&get("first_byte"), 16).unwrap_or_else(|e| {
        panic!("unparseable first_byte from the C program ({e}).\n--- its output ---\n{all}")
    });
    assert_eq!(
        first_byte & 0x80,
        0x80,
        "expected a long-header packet, so bit 7 of the first byte set; observed first byte \
         {first_byte:02x}.\n--- the C program's output ---\n{all}"
    );
    assert_eq!(
        (first_byte & 0x30) >> 4,
        0,
        "expected the long header type bits (4-5 of the first byte) to carry draft-23's Initial \
         value 0; observed first byte {first_byte:02x}, so type bits {}.\n\
         --- the C program's output ---\n{all}",
        (first_byte & 0x30) >> 4
    );

    assert_eq!(
        get("header_info_rc"),
        "0",
        "expected quiche_header_info() to succeed on the datagram quiche_conn_send() just \
         produced; observed its return value.\n--- the C program's output ---\n{all}"
    );
    assert_eq!(
        get("header_version"),
        "ff000017",
        "expected quiche_header_info() to report draft-23's version 0xff000017 on the client's \
         first datagram; observed the value above.\n--- the C program's output ---\n{all}"
    );

    let asked = get("asked_cid");
    let hscid = get("header_scid");
    let hdcid = get("header_dcid");
    assert_eq!(
        hscid, asked,
        "expected the connection id passed to quiche_connect() to appear in the first datagram; \
         observed source connection id {hscid} and destination connection id {hdcid} against an \
         asked-for {asked}.\n--- the C program's output ---\n{all}"
    );
    assert!(
        !hdcid.is_empty() && hdcid.len() <= 40 && hdcid.len() % 2 == 0,
        "expected the first datagram to carry a non-empty destination connection id of at most \
         QUICHE_MAX_CONN_ID_LEN bytes; observed {} bytes ({hdcid}).\n\
         --- the C program's output ---\n{all}",
        hdcid.len() / 2
    );
    assert_eq!(
        get("header_token_len"),
        "0",
        "expected an untokened first Initial, so a zero-length token; observed the length \
         above.\n--- the C program's output ---\n{all}"
    );
}

/// A readable prefix of a datagram, for an assertion message.
#[allow(dead_code)]
fn head(d: &[u8], n: usize) -> String {
    format!("{} ({} bytes total)", testkit::hex(&d[..n.min(d.len())]), d.len())
}

// ---------------------------------------------------------------------------
// cap-initial-keys  (T2)
// ---------------------------------------------------------------------------

/// `cap-initial-keys`: the verifier decrypts the candidate client's very first
/// datagram with its own draft-23 implementation.
///
/// The candidate's library never reads this packet, so nothing it believes
/// about its own key schedule counts. The salt, the `client in` label, the
/// HKDF expansion, the AES-ECB header-protection mask and the AEAD nonce all
/// have to match RFC 9001 Section 5 over draft-23's salt or the AEAD open
/// fails, and a failed open is indistinguishable from any other crypto bug by
/// design.
#[test]
fn cap_initial_keys() {
    let dir = testkit::scratch("cap-initial-keys");
    let ep = testkit::Endpoint::new();
    let addr = ep.local_addr();

    // The verifier's socket is not a QUIC server; it exists to catch the
    // client's first flight. The client is killed as soon as that flight
    // arrives.
    let client_bin = testkit::client_bin();
    let mut cc = testkit::cmd(&client_bin);
    cc.arg(format!("https://127.0.0.1:{}/initial", addr.port()))
        .arg("--no-verify")
        .arg("--wire-version")
        .arg("ff000017");
    let client = testkit::spawn_proc(&mut cc, &dir, "keys-client");

    let dgram = ep.recv(SHORT);
    let log = client.stop();
    let dgram = dgram.unwrap_or_else(|| {
        panic!(
            "expected {} to send a first flight to 127.0.0.1:{} within {SHORT:?}; observed no \
             datagram at all.\n--- client log ---\n{log}",
            client_bin.display(),
            addr.port()
        )
    });

    assert!(
        support::is_long_header(dgram[0]),
        "expected a long-header packet as the client's first datagram; observed first byte \
         {:02x}, datagram {}.\n--- client log ---\n{log}",
        dgram[0],
        head(&dgram, 48)
    );
    let version = support::peek_version(&dgram).unwrap_or_else(|e| {
        panic!(
            "expected a readable long-header version field in the client's first datagram; \
             observed {e}, datagram {}.\n--- client log ---\n{log}",
            head(&dgram, 48)
        )
    });
    assert_eq!(
        version,
        support::VERSION_DRAFT23,
        "expected the client asked for --wire-version ff000017 to put draft-23's version on the \
         wire; observed {version:#010x}, datagram {}.\n--- client log ---\n{log}",
        head(&dgram, 48)
    );

    let initial = support::decrypt_initial(
        &dgram,
        support::VERSION_DRAFT23,
        support::Perspective::Client,
    )
    .unwrap_or_else(|e| {
        panic!(
            "expected the verifier's own draft-23 Initial keys -- salt \
             c3eef712c72ebb5a11a7d2432bb46365bef9f502 over the packet's destination connection \
             id, AES-ECB header protection, AEAD_AES_128_GCM -- to open the client's first \
             Initial; observed {e}.\ndatagram {}\n--- client log ---\n{log}",
            head(&dgram, 64)
        )
    });

    let crypto = initial
        .frames()
        .collect::<Vec<_>>()
        .into_iter()
        .find_map(|f| match f {
            Ok(support::Frame::Crypto { offset: 0, data }) => Some(data.to_vec()),
            _ => None,
        })
        .unwrap_or_else(|| {
            let seen: Vec<String> =
                initial.frames().map(|f| format!("{f:?}")).collect();
            panic!(
                "expected a CRYPTO frame at offset 0 in the decrypted Initial; observed the \
                 frames {seen:?}.\npayload {}\n--- client log ---\n{log}",
                head(&initial.payload, 64)
            )
        });

    assert!(
        crypto.len() >= 44,
        "expected the CRYPTO frame to carry a TLS 1.3 ClientHello, which cannot be shorter than \
         its own 4-byte handshake header plus legacy_version, random and two length bytes; \
         observed {} bytes: {}.\n--- client log ---\n{log}",
        crypto.len(),
        head(&crypto, 44)
    );
    assert_eq!(
        crypto[0], 0x01,
        "expected TLS handshake type 1 (client_hello) as the first byte of the crypto stream; \
         observed {:#04x}, crypto stream {}.\n--- client log ---\n{log}",
        crypto[0],
        head(&crypto, 48)
    );
    let declared =
        ((crypto[1] as usize) << 16) | ((crypto[2] as usize) << 8) | crypto[3] as usize;
    assert!(
        declared + 4 >= crypto.len() && declared >= 40,
        "expected the client_hello's 3-byte length to describe a message at least as long as the \
         bytes in this packet; observed a declared length of {declared} against {} bytes of \
         crypto stream: {}.\n--- client log ---\n{log}",
        crypto.len(),
        head(&crypto, 48)
    );
    assert_eq!(
        &crypto[4..6],
        &[0x03, 0x03],
        "expected legacy_version 0x0303 in the ClientHello, as TLS 1.3 requires; observed {}, \
         crypto stream {}.\n--- client log ---\n{log}",
        testkit::hex(&crypto[4..6]),
        head(&crypto, 48)
    );
    let session_id_len = crypto[38] as usize;
    assert!(
        session_id_len <= 32,
        "expected a legacy_session_id of at most 32 bytes after the 32-byte random; observed a \
         length byte of {session_id_len}, crypto stream {}.\n--- client log ---\n{log}",
        head(&crypto, 48)
    );
    let cs_off = 39 + session_id_len;
    assert!(
        crypto.len() >= cs_off + 2,
        "expected a cipher_suites length after the legacy_session_id at offset {cs_off}; \
         observed only {} bytes of crypto stream: {}.\n--- client log ---\n{log}",
        crypto.len(),
        head(&crypto, 64)
    );
    let cs_len = ((crypto[cs_off] as usize) << 8) | crypto[cs_off + 1] as usize;
    assert!(
        cs_len >= 2 && cs_len % 2 == 0 && cs_off + 2 + cs_len <= crypto.len(),
        "expected a non-empty, even-length cipher_suites list inside the ClientHello; observed a \
         declared length of {cs_len} at offset {cs_off} in {} bytes of crypto stream: \
         {}.\n--- client log ---\n{log}",
        crypto.len(),
        head(&crypto, 64)
    );
}

// ---------------------------------------------------------------------------
// cap-stateless-retry  (T2)
// ---------------------------------------------------------------------------

/// `cap-stateless-retry`: the verifier makes the candidate's server do address
/// validation and then satisfies it.
///
/// Two exchanges. The first Initial carries no token, so a server with address
/// validation on must answer with a draft-23 Retry -- type bits 3, the ODCID
/// carried inline because draft-23 has no integrity tag, and a token. The
/// second Initial carries that token and the Retry's Source Connection ID as
/// its destination, which is also the value both ends must now key the Initial
/// packet protection from; the server has to validate the token, accept the
/// connection and answer, and the verifier proves it did by decrypting that
/// answer with keys of its own derived from that Source Connection ID.
#[test]
fn cap_stateless_retry() {
    let dir = testkit::scratch("cap-stateless-retry");
    let tls = testkit::tls();
    let root = testkit::docroot(&dir, &[("index.txt", b"retry\n".to_vec())]);
    let port = testkit::free_port();

    // No --no-retry: address validation is on, which the step's instruction
    // says is the default.
    let server_bin = testkit::server_bin();
    let mut sc = testkit::cmd(&server_bin);
    sc.arg("--listen")
        .arg(format!("127.0.0.1:{port}"))
        .arg("--cert")
        .arg(&tls.cert)
        .arg("--key")
        .arg(&tls.key)
        .arg("--root")
        .arg(&root);
    let server = testkit::spawn_server(&mut sc, port, &dir, "retry-server", SHORT);
    let to = server.addr();

    let ep = testkit::Endpoint::new();
    let odcid: Vec<u8> = (0xa0u8..0xb4).collect();
    let scid: Vec<u8> = (0x50u8..0x58).collect();

    // A TLS handshake message header that promises more bytes than the packet
    // carries. It is a well-formed start to a ClientHello, so a server that
    // gets this far buffers it and waits for the rest instead of tearing the
    // connection down -- which is what makes the answer to the second Initial
    // a plain acknowledgement rather than a race between error paths. The
    // group is about address validation, not about TLS.
    let mut crypto = vec![0x01, 0x00, 0x04, 0x00, 0x03, 0x03];
    crypto.extend(std::iter::repeat(0x42).take(32));

    let first = support::ClientInitial::new(
        support::VERSION_DRAFT23,
        &odcid,
        &scid,
        &crypto,
    )
    .build()
    .unwrap_or_else(|e| panic!("the verifier could not build its own client Initial: {e}"));
    ep.send_to(&first, to);

    let reply = ep.recv(SHORT).unwrap_or_else(|| {
        panic!(
            "expected the server at {to} to answer a token-less draft-23 Initial within \
             {SHORT:?}; observed no datagram at all. Address validation is on by default, so the \
             answer should have been a Retry.\n--- server log ---\n{}",
            server.log()
        )
    });

    assert!(
        support::is_long_header(reply[0]),
        "expected a long-header Retry in answer to a token-less Initial; observed first byte \
         {:02x}, datagram {}.\n--- server log ---\n{}",
        reply[0],
        head(&reply, 64),
        server.log()
    );
    assert_eq!(
        (reply[0] & 0x30) >> 4,
        3,
        "expected draft-23's Retry value 3 in the long header type bits (4-5 of the first byte); \
         observed first byte {:02x}, so type bits {}, datagram {}.\n--- server log ---\n{}",
        reply[0],
        (reply[0] & 0x30) >> 4,
        head(&reply, 64),
        server.log()
    );

    let retry = support::RetryPacket::parse(&reply, support::VERSION_DRAFT23)
        .unwrap_or_else(|e| {
            panic!(
                "expected a draft-23 Retry -- version 0xff000017, destination and source \
                 connection ids, then a one-byte ODCID length, the ODCID and the token to the \
                 end of the datagram; observed {e}, datagram {}.\n--- server log ---\n{}",
                head(&reply, 64),
                server.log()
            )
        });

    assert_eq!(
        retry.version,
        support::VERSION_DRAFT23,
        "expected version 0xff000017 in the Retry; observed {:#010x}, datagram {}.",
        retry.version,
        head(&reply, 64)
    );
    assert_eq!(
        retry.odcid.map(testkit::hex),
        Some(testkit::hex(&odcid)),
        "expected the Retry's ODCID field to echo the destination connection id the verifier \
         used, {}; observed {:?}, datagram {}.\n--- server log ---\n{}",
        testkit::hex(&odcid),
        retry.odcid.map(testkit::hex),
        head(&reply, 64),
        server.log()
    );
    assert_eq!(
        testkit::hex(retry.dcid),
        testkit::hex(&scid),
        "expected the Retry to be addressed to the source connection id the verifier used, {}; \
         observed a destination connection id of {}, datagram {}.",
        testkit::hex(&scid),
        testkit::hex(retry.dcid),
        head(&reply, 64)
    );
    assert!(
        !retry.token.is_empty(),
        "expected a non-empty address validation token in the Retry; observed an empty one, \
         datagram {}.\n--- server log ---\n{}",
        head(&reply, 64),
        server.log()
    );

    // The client's new destination connection id after a Retry is the Retry's
    // source connection id, and that value is what both ends now derive
    // Initial keys from.
    let new_dcid = retry.scid.to_vec();
    let token = retry.token.to_vec();
    assert!(
        !new_dcid.is_empty(),
        "expected the Retry to carry a source connection id for the client to address its next \
         Initial to; observed an empty one, datagram {}.",
        head(&reply, 64)
    );

    let mut second = support::ClientInitial::new(
        support::VERSION_DRAFT23,
        &new_dcid,
        &scid,
        &crypto,
    );
    second.token = &token;
    let second = second
        .build()
        .unwrap_or_else(|e| panic!("the verifier could not build its retried Initial: {e}"));
    ep.send_to(&second, to);

    let answer = ep.recv(SHORT).unwrap_or_else(|| {
        panic!(
            "expected the server at {to} to answer an Initial carrying the token it just minted \
             ({}) within {SHORT:?}; observed no datagram at all.\n--- server log ---\n{}",
            testkit::hex(&token),
            server.log()
        )
    });

    assert_ne!(
        (answer[0] & 0x30) >> 4,
        3,
        "expected the server to accept the token it minted and proceed; observed a second Retry, \
         datagram {}.\n--- server log ---\n{}",
        head(&answer, 64),
        server.log()
    );
    assert_eq!(
        (answer[0] & 0x30) >> 4,
        0,
        "expected an Initial packet (long header type bits 0) in answer to the retried Initial; \
         observed first byte {:02x}, so type bits {}, datagram {}.\n--- server log ---\n{}",
        answer[0],
        (answer[0] & 0x30) >> 4,
        head(&answer, 64),
        server.log()
    );

    let opened = support::decrypt_initial_with_odcid(
        &answer,
        support::VERSION_DRAFT23,
        support::Perspective::Server,
        &new_dcid,
    )
    .unwrap_or_else(|e| {
        panic!(
            "expected the server's answer to the retried Initial to be protected with Initial \
             keys derived from the Retry's source connection id {}; observed {e}, datagram \
             {}.\n--- server log ---\n{}",
            testkit::hex(&new_dcid),
            head(&answer, 64),
            server.log()
        )
    });

    let frames: Vec<String> = opened.frames().map(|f| format!("{f:?}")).collect();
    let acked = opened.frames().any(|f| {
        matches!(f, Ok(support::Frame::Ack { ranges, .. }) if ranges.iter().any(|(lo, _hi)| *lo == 0))
    });
    assert!(
        acked,
        "expected the server to acknowledge the retried Initial it just accepted, so an ACK \
         covering packet number 0; observed the frames {frames:?}.\npayload {}\n\
         --- server log ---\n{}",
        head(&opened.payload, 64),
        server.log()
    );

    drop(server);
}

// ---------------------------------------------------------------------------
// cap-header-parse  (T5)
// ---------------------------------------------------------------------------

#[test]
fn cap_header_parse() {
    // ---------------------------------------------------------------------------
    // 1. Initial with token
    // ---------------------------------------------------------------------------
    let mut dgram1 = vec![
        0xc3,                   // long header, type bits 0 (Initial), version follows
        0xff, 0x00, 0x00, 0x17, // version draft-23
        0x08,                   // DCID length
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22, // DCID
        0x04,                   // SCID length
        0x33, 0x44, 0x55, 0x66, // SCID
        0x06,                   // token length (varint, 1 byte = 0x06)
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // token
        0x00,                   // payload length varint (dummy, not parsed by from_slice)
    ];
    let hdr = quiche::Header::from_slice(&mut dgram1, 0).expect("parse Initial with token");
    assert_eq!(hdr.ty, quiche::Type::Initial, "Initial: ty is Initial");
    assert_eq!(hdr.version, 0xff000017, "Initial: version is 0xff000017");
    assert_eq!(hdr.dcid, vec![0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22], "Initial: dcid");
    assert_eq!(hdr.scid, vec![0x33, 0x44, 0x55, 0x66], "Initial: scid");
    assert_eq!(hdr.token, Some(vec![0x01, 0x02, 0x03, 0x04, 0x05, 0x06]), "Initial: token present");

    // ---------------------------------------------------------------------------
    // 2. Retry with ODCID inline
    // ---------------------------------------------------------------------------
    let mut dgram2 = vec![
        0xf0,                   // long header, type bits 3 (Retry)
        0xff, 0x00, 0x00, 0x17, // version
        0x05,                   // DCID length
        0xd1, 0xd2, 0xd3, 0xd4, 0xd5, // DCID
        0x03,                   // SCID length
        0xe1, 0xe2, 0xe3,       // SCID
        0x06,                   // ODCID length (one byte, draft-23)
        0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, // ODCID
        0xa1, 0xa2, 0xa3,       // token (rest of packet)
    ];
    let hdr = quiche::Header::from_slice(&mut dgram2, 0).expect("parse Retry");
    assert_eq!(hdr.ty, quiche::Type::Retry, "Retry: ty is Retry");
    assert_eq!(hdr.version, 0xff000017, "Retry: version is 0xff000017");
    assert_eq!(hdr.dcid, vec![0xd1, 0xd2, 0xd3, 0xd4, 0xd5], "Retry: dcid");
    assert_eq!(hdr.scid, vec![0xe1, 0xe2, 0xe3], "Retry: scid");
    assert_eq!(hdr.odcid, Some(vec![0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6]), "Retry: odcid echoed");
    assert_eq!(hdr.token, Some(vec![0xa1, 0xa2, 0xa3]), "Retry: token is rest");

    // ---------------------------------------------------------------------------
    // 3. Handshake
    // ---------------------------------------------------------------------------
    let mut dgram3 = vec![
        0xe3,                   // long header, type bits 2 (Handshake)
        0xff, 0x00, 0x00, 0x17, // version
        0x07,                   // DCID length
        0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, // DCID
        0x02,                   // SCID length
        0xc1, 0xc2,             // SCID
        0x00,                   // payload length varint
    ];
    let hdr = quiche::Header::from_slice(&mut dgram3, 0).expect("parse Handshake");
    assert_eq!(hdr.ty, quiche::Type::Handshake, "Handshake: ty is Handshake");
    assert_eq!(hdr.version, 0xff000017, "Handshake: version is 0xff000017");
    assert_eq!(hdr.dcid, vec![0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7], "Handshake: dcid");
    assert_eq!(hdr.scid, vec![0xc1, 0xc2], "Handshake: scid");
    assert_eq!(hdr.token, None, "Handshake: no token");

    // ---------------------------------------------------------------------------
    // 4. 0-RTT
    // ---------------------------------------------------------------------------
    let mut dgram4 = vec![
        0xd1,                   // long header, type bits 1 (0-RTT)
        0xff, 0x00, 0x00, 0x17, // version
        0x04,                   // DCID length
        0x71, 0x72, 0x73, 0x74, // DCID
        0x03,                   // SCID length
        0x81, 0x82, 0x83,       // SCID
        0x00,                   // payload length varint
    ];
    let hdr = quiche::Header::from_slice(&mut dgram4, 0).expect("parse 0-RTT");
    assert_eq!(hdr.ty, quiche::Type::ZeroRTT, "0-RTT: ty is ZeroRTT");
    assert_eq!(hdr.version, 0xff000017, "0-RTT: version is 0xff000017");
    assert_eq!(hdr.dcid, vec![0x71, 0x72, 0x73, 0x74], "0-RTT: dcid");
    assert_eq!(hdr.scid, vec![0x81, 0x82, 0x83], "0-RTT: scid");
    assert_eq!(hdr.token, None, "0-RTT: no token");

    // ---------------------------------------------------------------------------
    // 5. Version Negotiation
    // ---------------------------------------------------------------------------
    let mut dgram5 = vec![
        0xc0,                   // long header
        0x00, 0x00, 0x00, 0x00, // version 0 => VersionNegotiation
        0x05,                   // DCID length
        0x91, 0x92, 0x93, 0x94, 0x95, // DCID
        0x04,                   // SCID length
        0xa1, 0xa2, 0xa3, 0xa4, // SCID
        0x00, 0x00, 0x00, 0x01, // version 1
        0xff, 0x00, 0x00, 0x17, // version draft-23
        0xba, 0xba, 0xba, 0xba, // another version
    ];
    let hdr = quiche::Header::from_slice(&mut dgram5, 0).expect("parse VersionNegotiation");
    assert_eq!(hdr.ty, quiche::Type::VersionNegotiation, "VN: ty is VersionNegotiation");
    assert_eq!(hdr.version, 0, "VN: version is 0");
    assert_eq!(hdr.dcid, vec![0x91, 0x92, 0x93, 0x94, 0x95], "VN: dcid");
    assert_eq!(hdr.scid, vec![0xa1, 0xa2, 0xa3, 0xa4], "VN: scid");

    // ---------------------------------------------------------------------------
    // 6. Short header with 16-byte DCID
    // ---------------------------------------------------------------------------
    let mut dgram6 = vec![
        0x41, // short header, fixed bit set, type bits clear
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, // 16 bytes DCID
        0x00, // packet number (will not be parsed correctly without decryption)
    ];
    let hdr = quiche::Header::from_slice(&mut dgram6, 16).expect("parse short header");
    assert_eq!(hdr.ty, quiche::Type::Short, "Short: ty is Short");
    assert_eq!(hdr.version, 0, "Short: version is 0");
    assert_eq!(
        hdr.dcid,
        vec![0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f],
        "Short: dcid is 16 bytes"
    );
    assert_eq!(hdr.scid, Vec::<u8>::new(), "Short: scid is empty");
    assert_eq!(hdr.token, None, "Short: no token");

    // ---------------------------------------------------------------------------
    // 7. Truncated header: insufficient bytes for DCID
    // ---------------------------------------------------------------------------
    let mut dgram7 = vec![
        0xc3,                   // long header, Initial
        0xff, 0x00, 0x00, 0x17, // version
        0x10,                   // DCID length = 16
        0x00, 0x01, 0x02,       // only 3 bytes, but claims 16
    ];
    let res = quiche::Header::from_slice(&mut dgram7, 0);
    match res {
        Err(quiche::Error::BufferTooShort) => {}, // expected
        other => panic!("truncated DCID: expected BufferTooShort, got {:?}", other),
    }

    // ---------------------------------------------------------------------------
    // 8. Truncated header: insufficient bytes for token length varint
    // ---------------------------------------------------------------------------
    let mut dgram8 = vec![
        0xc3,                   // long header, Initial
        0xff, 0x00, 0x00, 0x17, // version
        0x02,                   // DCID length
        0xaa, 0xbb,             // DCID
        0x01,                   // SCID length
        0xcc,                   // SCID
        // missing token length varint
    ];
    let res = quiche::Header::from_slice(&mut dgram8, 0);
    match res {
        Err(quiche::Error::BufferTooShort) => {}, // expected
        other => panic!("truncated token: expected BufferTooShort, got {:?}", other),
    }
}

// ---------------------------------------------------------------------------
// cap-flow-control  (T3)
// ---------------------------------------------------------------------------

#[test]
fn cap_flow_control() {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.set_initial_max_stream_data_bidi_remote(4096);
    sc.set_initial_max_stream_data_bidi_remote(4096);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    const TOTAL: usize = 64 * 1024;
    const WINDOW: usize = 4096;
    let data = testkit::body(TOTAL, 0xfc01);
    let stream_id = 0;

    // Fill the 4096-byte window
    let n = p.client.stream_send(stream_id, &data[..WINDOW], false)
        .expect("first stream_send");
    assert_eq!(n, WINDOW, "filled the {WINDOW} window, sent {n}");
    p.settle(8);

    // Capacity should now be zero
    let cap = p.client.stream_capacity(stream_id)
        .expect("stream_capacity after filling window");
    assert_eq!(cap, 0, "capacity is 0 after {WINDOW} bytes in flight, observed {cap}");

    // Attempting to send more returns Ok(0)
    let n = p.client.stream_send(stream_id, &data[WINDOW..], false)
        .expect("stream_send when capacity is 0");
    assert_eq!(n, 0, "stream_send when capacity=0 returns Ok(0), observed Ok({n})");

    // The server reads those WINDOW bytes
    let (got, fin) = pair::read_stream(&mut p.server, stream_id);
    assert_eq!(got.len(), WINDOW, "server read {WINDOW} bytes, got {}", got.len());
    assert!(!fin, "server did not see fin yet");
    assert_eq!(&got[..], &data[..WINDOW], "server read correct bytes");

    // Exchange to deliver MAX_STREAM_DATA
    p.settle(8);

    // Capacity should reopen
    let cap = p.client.stream_capacity(stream_id)
        .expect("stream_capacity after MAX_STREAM_DATA");
    assert!(cap > 0, "capacity reopened after MAX_STREAM_DATA, observed {cap}");

    // The rest of the transfer proceeds one window at a time. Each round hands
    // the sender whatever the window currently allows, lets the datagrams
    // cross, and drains the receiver -- which is what makes the next
    // MAX_STREAM_DATA appear. A sender that ignores the limit would have
    // pushed the whole 64 KiB in the first round; a receiver that never
    // extends the limit would stall here and trip the round bound.
    let mut recv = got;
    let mut sent = WINDOW;
    let mut saw_fin = false;
    let mut stalls = 0usize;
    let mut rounds = 0usize;
    while sent < TOTAL || !saw_fin {
        rounds += 1;
        assert!(
            rounds <= 256,
            "transfer stalled: after {rounds} rounds the client had sent {sent} \
             of {TOTAL} bytes and the server had read {} (fin seen: {saw_fin})",
            recv.len()
        );

        let cap = p.client.stream_capacity(stream_id).expect("stream_capacity");
        if sent < TOTAL {
            if cap == 0 {
                stalls += 1;
            } else {
                let chunk_end = (sent + cap).min(TOTAL);
                let will_fin = chunk_end == TOTAL;
                let n = p
                    .client
                    .stream_send(stream_id, &data[sent..chunk_end], will_fin)
                    .expect("stream_send");
                assert!(
                    n > 0 && n <= cap,
                    "stream_send accepted {n} bytes against a capacity of {cap}"
                );
                sent += n;
            }
        }

        p.settle(16);
        let (chunk, f) = pair::read_stream(&mut p.server, stream_id);
        recv.extend_from_slice(&chunk);
        saw_fin |= f;
    }

    assert_eq!(sent, TOTAL, "client sent the whole transfer: {sent} of {TOTAL}");
    assert!(saw_fin, "server saw the stream finish after {rounds} rounds");

    // The window really was the limiter: a 64 KiB transfer through a 4096-byte
    // window cannot complete in one shot.
    assert!(
        rounds >= TOTAL / WINDOW,
        "a {TOTAL}-byte transfer through a {WINDOW}-byte window took {rounds} \
         rounds, so the window was not being enforced (capacity stalls: {stalls})"
    );

    assert_eq!(
        recv.len(),
        TOTAL,
        "server read the whole transfer: {} of {TOTAL} bytes in {rounds} rounds",
        recv.len()
    );
    assert!(
        recv == data,
        "server read the bytes the client sent: first mismatch at offset {:?}",
        recv.iter().zip(data.iter()).position(|(a, b)| a != b)
    );
}

// ---------------------------------------------------------------------------
// cap-slow-start-window  (T3)
// ---------------------------------------------------------------------------

#[test]
fn cap_slow_start_window() {
    // Ten packets of the 1452-byte maximum datagram size: the initial
    // congestion window this milestone starts every connection at.
    //
    // It is a constant of the congestion controller, *not* a function of
    // whatever `set_max_packet_size` was given -- and the group pins both
    // halves of that, because the two readings coincide at exactly one
    // configuration and a group that only checked the value would be pinning
    // the fixture rather than the behaviour.
    const INITIAL_WINDOW: usize = 10 * 1452;

    // Read the window before anything has been sent or acknowledged. After the
    // handshake it has already grown, so the initial value is only observable
    // on a connection that has not yet exchanged a packet.
    let mut p = pair::Pair::with_configs(
        &mut pair::client_config(pair::ALPN_HQ),
        &mut pair::server_config(pair::ALPN_HQ),
    );
    let initial = p.client.stats().cwnd;
    assert_eq!(
        initial, INITIAL_WINDOW,
        "expected a connection that has sent nothing to start at the ten-packet \
         initial congestion window, 10 * 1452 = {INITIAL_WINDOW} bytes; observed \
         {initial}"
    );

    // The same observation on a pair configured with a different maximum
    // packet size. The window must not move: a stack whose initial window is
    // ten times its *own* configured packet size reports 12000 here and fails,
    // which is the point -- the first assertion alone cannot tell the two
    // readings apart, since the verifier's own fixture sets 1452.
    let mut small_c = pair::client_config(pair::ALPN_HQ);
    small_c.set_max_packet_size(1200);
    let mut small_s = pair::server_config(pair::ALPN_HQ);
    small_s.set_max_packet_size(1200);
    let small_pair = pair::Pair::with_configs(&mut small_c, &mut small_s);
    let small = small_pair.client.stats().cwnd;
    assert_eq!(
        small, INITIAL_WINDOW,
        "expected the initial congestion window to be a constant of the \
         congestion controller rather than ten times `set_max_packet_size`: \
         with set_max_packet_size(1200) it must still be {INITIAL_WINDOW} bytes; \
         observed {small}"
    );
    drop(small_pair);

    // In slow start the window opens by roughly the number of bytes acked, so
    // a bulk transfer over a lossless pair should drive it well past the
    // initial value and never shrink on the way.
    p.handshake();
    let stream_id = 0;
    let chunk = testkit::body(16 * 1024, 0x5501);
    let mut prev = p.client.stats().cwnd;
    let mut series = vec![prev];
    for round in 0..8 {
        let n = p.client.stream_send(stream_id, &chunk, false).expect("stream_send");
        assert_eq!(n, chunk.len(), "round {round} handed over {n} of {} bytes", chunk.len());
        p.settle(32);
        let (got, _fin) = pair::read_stream(&mut p.server, stream_id);
        assert_eq!(got.len(), n, "round {round}: server read {} of {n} bytes", got.len());

        let now = p.client.stats().cwnd;
        assert!(
            now >= prev,
            "the window never shrinks without loss: round {round} went from \
             {prev} to {now} (series so far: {series:?})"
        );
        prev = now;
        series.push(now);
    }

    let after = p.client.stats().cwnd;
    assert!(
        after >= 2 * INITIAL_WINDOW,
        "slow start opens the window by the bytes it acks: after {} bytes on a \
         lossless pair the window should be at least {} (twice the initial \
         {INITIAL_WINDOW}), observed {after} (series: {series:?})",
        8 * chunk.len(),
        2 * INITIAL_WINDOW
    );
}

// ---------------------------------------------------------------------------
// cap-h3-static-table  (T3)
// ---------------------------------------------------------------------------

#[test]
fn cap_h3_static_table() {
    let mut cc = pair::client_config(pair::ALPN_H3);
    let mut sc = pair::server_config(pair::ALPN_H3);
    let mut hp = pair::H3Pair::with_configs(&mut cc, &mut sc);

    // Client sends a request with all-static headers
    let req = [
        quiche::h3::Header::new(":method", "GET"),
        quiche::h3::Header::new(":scheme", "https"),
        quiche::h3::Header::new(":path", "/"),
        quiche::h3::Header::new(":authority", "quic.test"),
    ];
    let stream_id = hp.client.send_request(&mut hp.pair.client, &req, true)
        .expect("client send_request");
    assert_eq!(stream_id & 0x3, 0, "request stream is client-initiated bidi, stream_id={stream_id}");

    // Server polls for Headers
    let (sid, ev) = hp.poll_server(16).expect("server poll Headers");
    assert_eq!(sid, stream_id, "server Headers on correct stream");
    let got_req = match ev {
        quiche::h3::Event::Headers(list) => list,
        other => panic!("expected Headers, got {other:?}"),
    };
    assert_eq!(got_req.len(), req.len(), "server received {} headers", got_req.len());
    for (i, (want, got)) in req.iter().zip(got_req.iter()).enumerate() {
        assert_eq!(got.name(), want.name(), "req header {i} name");
        assert_eq!(got.value(), want.value(), "req header {i} value");
    }

    // Server sends a response with :status from static table and a literal header with non-ASCII value
    let resp = [
        quiche::h3::Header::new(":status", "200"),
        quiche::h3::Header::new("content-type", "text/plain; charset=utf-8"),
        quiche::h3::Header::new("x-test", "café"),
    ];
    hp.server.send_response(&mut hp.pair.server, stream_id, &resp, true)
        .expect("server send_response");

    // Client polls for Headers
    let (sid, ev) = hp.poll_client(16).expect("client poll Headers");
    assert_eq!(sid, stream_id, "client Headers on correct stream");
    let got_resp = match ev {
        quiche::h3::Event::Headers(list) => list,
        other => panic!("expected Headers, got {other:?}"),
    };
    assert_eq!(got_resp.len(), resp.len(), "client received {} headers", got_resp.len());
    for (i, (want, got)) in resp.iter().zip(got_resp.iter()).enumerate() {
        assert_eq!(got.name(), want.name(), "resp header {i} name");
        assert_eq!(got.value(), want.value(), "resp header {i} value");
    }
}

// ---------------------------------------------------------------------------
// cap-idle-timeout  (T3)
// ---------------------------------------------------------------------------

#[test]
fn cap_idle_timeout() {
    use std::time::Duration;

    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.set_idle_timeout(300);
    sc.set_idle_timeout(300);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    // The connection should report a timeout inside 300 ms
    let t = p.client.timeout().expect("client reports a timeout");
    assert!(
        t <= Duration::from_millis(300),
        "client timeout should be <= 300 ms, observed {} ms",
        t.as_millis()
    );

    // Tick past the deadline
    p.tick(Duration::from_millis(350));

    // on_timeout should close the connection
    assert!(p.client.is_closed(), "client is closed after idle timeout");
    assert!(p.server.is_closed(), "server is closed after idle timeout");

    // A fresh connection with traffic should stay open past 300 ms
    let mut p2 = pair::Pair::with_configs(&mut cc, &mut sc);
    p2.handshake();
    let stream_id = 0;
    let data = testkit::body(1024, 0x9901);
    // 25 gaps of 20 ms rather than 10 of 40 ms: the same 500+ ms of wall time
    // spanned, but the largest interval a scheduler stall has to survive is
    // 20 ms against a 300 ms idle timeout -- a 15x margin instead of 7.5x.
    // The assertion below is unchanged in kind and covers a longer span.
    for _ in 0..25 {
        let n = p2.client.stream_send(stream_id, &data, false).expect("stream_send");
        assert!(n > 0, "sent {n}");
        p2.settle(8);
        std::thread::sleep(Duration::from_millis(20));
    }
    assert!(!p2.client.is_closed(), "client still open with traffic after 500+ ms");
    assert!(!p2.server.is_closed(), "server still open with traffic after 500+ ms");
}
