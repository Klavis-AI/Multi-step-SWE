//! new-capability groups: what this milestone can do that the previous one could not
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `cap-path-events` is `fn cap_path_events()`.
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

use std::collections::BTreeSet;
use std::net::SocketAddr;
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

/// Where two byte strings first differ, for a message that says something.
fn first_diff(want: &[u8], got: &[u8]) -> String {
    match want.iter().zip(got.iter()).position(|(a, b)| a != b) {
        Some(i) => format!(
            "first difference at offset {i}: expected {}, observed {} (16 bytes from there)",
            testkit::hex(&want[i..(i + 16).min(want.len())]),
            testkit::hex(&got[i..(i + 16).min(got.len())])
        ),
        None if want.len() == got.len() => "identical".to_string(),
        None => format!(
            "identical for the first {} bytes, then one of them ends ({} expected, {} observed)",
            want.len().min(got.len()),
            want.len(),
            got.len()
        ),
    }
}

/// Start this milestone's flag-taking `quiche-server` on `port`.
fn start_app_server(dir: &Path, port: u16, files: &[(&str, Vec<u8>)], tag: &str) -> testkit::Server {
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
    testkit::spawn_server(&mut c, port, dir, tag, LONG)
}

/// Push `data` down `stream_id` from the client and read it back off the
/// server, driving the pair until all of it is across.
///
/// Returns what the server actually read, so the caller compares bytes rather
/// than trusting a length. Fails the group -- naming the offset it stalled at
/// -- rather than looping forever.
fn transfer(p: &mut pair::Pair, stream_id: u64, data: &[u8]) -> Vec<u8> {
    const ROUNDS: usize = 8192;
    let mut sent = 0usize;
    let mut got: Vec<u8> = Vec::with_capacity(data.len());
    let mut buf = vec![0u8; 64 * 1024];

    for _ in 0..ROUNDS {
        if sent < data.len() {
            match p.client.stream_send(stream_id, &data[sent..], false) {
                Ok(n) => sent += n,
                // The stream window is closed for now; the exchange below is
                // what reopens it.
                Err(quiche::Error::Done) => {},
                Err(e) => panic!("client stream_send at offset {sent} of {}: {e:?}", data.len()),
            }
        }

        p.exchange();

        if p.server.readable().any(|s| s == stream_id) {
            loop {
                match p.server.stream_recv(stream_id, &mut buf) {
                    Ok((n, _fin)) => got.extend_from_slice(&buf[..n]),
                    Err(quiche::Error::Done) => break,
                    Err(e) => panic!("server stream_recv after {} bytes: {e:?}", got.len()),
                }
            }
        }

        if got.len() >= data.len() {
            return got;
        }
    }

    panic!(
        "the pair did not move {} bytes in {ROUNDS} rounds: {sent} bytes accepted by the sender, \
         {} delivered to the receiver; client closed={} server closed={}",
        data.len(),
        got.len(),
        p.client.is_closed(),
        p.server.is_closed()
    );
}

/// The congestion window of the connection's first path.
///
/// At this milestone `cwnd` is a property of a path, not of a connection, so
/// it is read through `path_stats()` rather than `stats()`.
fn first_cwnd(conn: &quiche::Connection) -> usize {
    conn.path_stats()
        .next()
        .expect("a connection has at least one path, and therefore at least one PathStats")
        .cwnd
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// cap-probe-and-migrate  (T1)
// ---------------------------------------------------------------------------

/// `cap-probe-and-migrate`: the candidate's own client moves to a second
/// local address mid-transfer and the bytes still arrive.
///
/// `--perform-migration` does not exist at step 07, so a step-07 client exits
/// non-zero on the flag alone. That is necessary but not sufficient: a client
/// that parses the flag and ignores it would pass on the transfer. So while
/// the client runs, the group reads that process's own open descriptors out
/// of `/proc/<pid>/fd` and resolves them against the kernel's UDP tables,
/// and requires the process to hold **two** bound UDP sockets at once.
///
/// That is an observation of the candidate's process, not of its logging.
/// The reference client announces its migration with an `info!` line, and
/// grading that text would score upstream's wording rather than a capability
/// an independent implementation has to have.
///
/// `--source-port` is left at its default of 0 on purpose: pinning it would
/// make the second bind collide with the first.
#[test]
fn cap_probe_and_migrate() {
    let _lock = testkit::serial_t1();

    const LEN: usize = 512 * 1024;
    let dir = testkit::scratch("cap-probe-and-migrate");
    let want = testkit::body(LEN, 0x0816_0001);
    let port = testkit::free_port();
    let server = start_app_server(&dir, port, &[("migrate.bin", want.clone())], "migrate-server");

    let url = format!("https://127.0.0.1:{port}/migrate.bin");
    let client_bin = testkit::client_bin();
    let mut cc = testkit::cmd(&client_bin);
    cc.arg("--no-verify").arg("--perform-migration").arg(&url);

    // Every port seen at any point, and the most seen at one instant. The
    // second is the number that matters: a process can bind and close several
    // sockets in sequence without ever having two live at once, and one
    // socket at a time is exactly what a client that ignored the flag does.
    let mut ever: BTreeSet<u16> = BTreeSet::new();
    let mut at_once = 0usize;
    let run = testkit::run_watched(&mut cc, LONG, &dir, "migrate-client", |pid| {
        let now = testkit::udp_local_ports(pid);
        at_once = at_once.max(now.len());
        ever.extend(now);
    });

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} --perform-migration to fetch {LEN} bytes from {url} within {LONG:?}; \
         observed: still running at the deadline, killed after writing {} bytes. It held UDP \
         ports {ever:?} ({at_once} at once).\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
        run.stdout_bytes.len(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to accept --perform-migration and exit 0 after fetching {url}; observed \
         status {:?} having written {} of {LEN} bytes.\n--- client stderr ---\n{}\n\
         --- server log ---\n{log}",
        client_bin.display(),
        run.status,
        run.stdout_bytes.len(),
        run.stderr
    );

    let got = &run.stdout_bytes;
    assert!(
        want == *got,
        "expected the {LEN} byte body of {url} back byte for byte while the client migrated; \
         observed {} bytes, {}.\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        got.len(),
        first_diff(&want, got),
        run.stderr
    );

    assert!(
        at_once >= 2,
        "expected the client to hold two UDP sockets at once while migrating to a second local \
         address; observed at most {at_once} bound at any one moment, across the ports {ever:?} \
         it held over the whole run. A client that accepts --perform-migration and keeps using \
         its original socket looks exactly like this.\n--- client stderr ---\n{}\n\
         --- server log ---\n{log}",
        run.stderr
    );

    drop(server);
}

// ---------------------------------------------------------------------------
// cap-path-events  (T3)
// ---------------------------------------------------------------------------

/// `cap-path-events`: probing a second path raises the right events, on the
/// right side, in the right order -- and raises nothing before the connection
/// IDs have crossed.
///
/// Three separate claims, and an implementation can get any one of them wrong
/// on its own:
///
/// * the ordering constraint is real. Before both peers have issued a spare
///   connection ID *and* exchanged packets, `probe_path` is
///   `Err(Error::OutOfIdentifiers)` -- checked first, because a stack that
///   probes happily without identifiers has not understood what a path is
///   named by;
/// * the queues are asymmetric. The probing client sees one event,
///   `Validated`; the server sees two, `New` then `Validated`, and both arrive
///   without the server's application probing back. A stack that mirrors the
///   client's view onto the server, or that waits to be told to validate,
///   fails here;
/// * the queue is a queue. Each event is returned once and then the queue is
///   empty, so an implementation that re-reports its latest event forever
///   fails even though every event it reports is correct.
///
/// The addresses are asserted, not just the variants: each endpoint reports
/// `(local, peer)` from its own point of view, so the same path reads
/// `(client_local_2, server)` at the client and `(server, client_local_2)` at
/// the server.
#[test]
fn cap_path_events() {
    let c2 = pair::client_addr_2();
    let s = pair::server_addr();

    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();

    // 1. no identifiers, no probing.
    assert_eq!(
        p.client.probe_path(c2, s),
        Err(quiche::Error::OutOfIdentifiers),
        "expected probe_path to refuse a second path while the connection has only the \
         handshake's connection ID: a path is identified on the wire by its connection ID, so \
         there is nothing to name the new one with"
    );

    // 2. issue one each and let the NEW_CONNECTION_ID frames cross.
    let (c_seq, s_seq) = p.exchange_cids();
    assert_eq!(
        (c_seq, s_seq),
        (1, 1),
        "expected the first connection ID each side issues to be sequence 1, sequence 0 being \
         the handshake's"
    );
    assert_eq!(
        (p.client.available_dcids(), p.server.available_dcids()),
        (1, 1),
        "expected each end to have learnt exactly one spare destination connection ID from the \
         other once the frames had crossed"
    );
    assert_eq!(
        p.client.path_event_next(),
        None,
        "expected exchanging connection IDs to raise no path event on the client: nothing has \
         been probed yet"
    );
    assert_eq!(
        p.server.path_event_next(),
        None,
        "expected exchanging connection IDs to raise no path event on the server"
    );

    // 3. now it works, and it returns the destination connection ID sequence
    //    number assigned to the new path -- not a path index.
    assert_eq!(
        p.client.probe_path(c2, s),
        Ok(1),
        "expected probe_path to succeed once both ends had a spare connection ID, and to return \
         the sequence number of the destination connection ID it assigned to the new path"
    );
    assert_eq!(
        p.server.probe_path(s, c2),
        Err(quiche::Error::InvalidState),
        "expected a server to refuse to probe a path it has not yet received a packet on: \
         clients initiate paths, servers follow them"
    );

    p.settle(32);

    // 4. the events themselves.
    let client_events = pair::drain_events(&mut p.client);
    assert_eq!(
        client_events,
        vec![quiche::PathEvent::Validated(c2, s)],
        "expected the probing client to see exactly one event -- Validated({c2}, {s}), its own \
         local address first -- and never New, which is a server-side event"
    );

    let server_events = pair::drain_events(&mut p.server);
    assert_eq!(
        server_events,
        vec![
            quiche::PathEvent::New(s, c2),
            quiche::PathEvent::Validated(s, c2),
        ],
        "expected the server to see New({s}, {c2}) and then Validated({s}, {c2}), in that order \
         and without its application probing back: the server's tuples name its own local \
         address first, so they are the client's two reversed"
    );

    // 5. the queues stay drained.
    assert_eq!(
        p.client.path_event_next(),
        None,
        "expected the client's event queue to stay empty once drained"
    );
    assert_eq!(
        p.server.path_event_next(),
        None,
        "expected the server's event queue to stay empty once drained"
    );

    assert!(
        !p.client.is_closed() && !p.server.is_closed(),
        "expected probing a second path to leave the connection open; client local error {:?}, \
         server local error {:?}",
        p.client.local_error().map(|e| e.error_code),
        p.server.local_error().map(|e| e.error_code)
    );
}

// ---------------------------------------------------------------------------
// cap-sendinfo-from  (T3)
// ---------------------------------------------------------------------------

/// `cap-sendinfo-from`: `to` means the remote endpoint on the way out and the
/// local endpoint on the way in.
///
/// Both structs carry both fields at step 07's tree as well, so this group is
/// not about presence. It is about the one asymmetry a shared helper erases:
/// on a `SendInfo`, describing a datagram the stack hands the application,
/// `from` is local and `to` is remote; on a `RecvInfo`, describing a datagram
/// the application hands the stack, `from` is remote and `to` is local.
///
/// The `SendInfo` half is read directly off `send`. The `RecvInfo` half cannot
/// be -- the harness builds that struct itself -- so it is observed through
/// its consequence: after receiving, each end's `path_stats()` must describe
/// the path with its *own* address as `local_addr`. A stack that read
/// `RecvInfo.to` as the remote address reports both paths backwards, which no
/// amount of internal self-consistency hides.
#[test]
fn cap_sendinfo_from() {
    let c = pair::client_addr();
    let s = pair::server_addr();

    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();
    p.client.stream_send(4, b"which way round", true).expect("client stream_send");

    let mut buf = vec![0u8; 2048];

    // Outbound at the client: from is the client's own address.
    let (len, info) = p.client.send(&mut buf).expect("client send after stream_send");
    assert_eq!(
        (info.from, info.to),
        (c, s),
        "expected the client's SendInfo to describe the datagram as going from its own address \
         {c} to the peer's {s}; observed from={} to={}",
        info.from,
        info.to
    );

    // Inbound at the server: from is the remote, to is the local.
    p.server
        .recv(&mut buf[..len], quiche::RecvInfo { from: c, to: s })
        .expect("server recv of a datagram described as from the client, to the server");

    // Outbound at the server: the mirror image, with both fields swapped.
    let (len, info) = p.server.send(&mut buf).expect("server send");
    assert_eq!(
        (info.from, info.to),
        (s, c),
        "expected the server's SendInfo to describe its datagram as going from its own address \
         {s} to the client's {c}; observed from={} to={}. The two ends do not share a point of \
         view: `from` is always the sender's local address",
        info.from,
        info.to
    );

    p.client
        .recv(&mut buf[..len], quiche::RecvInfo { from: s, to: c })
        .expect("client recv of a datagram described as from the server, to the client");

    // And what each end made of the RecvInfo it was given.
    let cs = p
        .client
        .path_stats()
        .next()
        .expect("the client has at least one path");
    assert_eq!(
        (cs.local_addr, cs.peer_addr),
        (c, s),
        "expected the client's path to be (local {c}, peer {s}); observed (local {}, peer {}). \
         The addresses it was handed were RecvInfo {{ from: {s}, to: {c} }}, so a stack that \
         reports them the other way round has read `to` as the remote address",
        cs.local_addr,
        cs.peer_addr
    );

    let ss = p
        .server
        .path_stats()
        .next()
        .expect("the server has at least one path");
    assert_eq!(
        (ss.local_addr, ss.peer_addr),
        (s, c),
        "expected the server's path to be (local {s}, peer {c}); observed (local {}, peer {})",
        ss.local_addr,
        ss.peer_addr
    );
}

// ---------------------------------------------------------------------------
// cap-bbr  (T3)
// ---------------------------------------------------------------------------

/// `cap-bbr`: a third congestion controller, selectable by name, that carries
/// data.
///
/// `set_cc_algorithm_name("bbr")` fails at step 07 and succeeds here, so the
/// call itself is the discriminator. Around it the group pins the things that
/// make the name meaningful rather than decorative: the discriminant is 2 and
/// sits beside Reno's 0 and CUBIC's 1, an unrecognised name is refused with
/// `Error::CongestionControl` specifically rather than silently falling back
/// to the default, and a connection configured with BBR completes a 256 KiB
/// transfer byte for byte.
///
/// It does not check BBR's state machine, its pacing gains or its window
/// trajectory. Those are implementation choices that reasonable BBRs differ
/// on, and scoring them would score upstream's code rather than the
/// capability.
#[test]
fn cap_bbr() {
    // 1. the name resolves, the neighbours still do, and nothing else does.
    let mut probe = quiche::Config::new(quiche::PROTOCOL_VERSION).expect("Config::new");
    probe.set_cc_algorithm_name("bbr").expect(
        "expected set_cc_algorithm_name(\"bbr\") to be accepted at this milestone: BBR is the \
         third congestion controller and the string is how it is selected",
    );
    probe.set_cc_algorithm_name("cubic").expect(
        "expected \"cubic\" to keep working beside \"bbr\": BBR is added, nothing is replaced",
    );
    probe
        .set_cc_algorithm_name("reno")
        .expect("expected \"reno\" to keep working beside \"bbr\"");
    assert_eq!(
        probe.set_cc_algorithm_name("bbr2"),
        Err(quiche::Error::CongestionControl),
        "expected an unrecognised algorithm name to be refused with Error::CongestionControl \
         rather than accepted, ignored, or quietly resolved to the default"
    );

    // 2. the discriminants, which the C API and the qlog output both depend on.
    assert_eq!(
        (
            quiche::CongestionControlAlgorithm::Reno as u8,
            quiche::CongestionControlAlgorithm::CUBIC as u8,
            quiche::CongestionControlAlgorithm::BBR as u8,
        ),
        (0, 1, 2),
        "expected BBR to be added as discriminant 2, after Reno's 0 and CUBIC's 1, rather than \
         inserted among them"
    );

    // 3. and a connection under it carries data.
    const LEN: usize = 256 * 1024;
    let mut cc = pair::client_config(pair::ALPN_HQ);
    cc.set_cc_algorithm(quiche::CongestionControlAlgorithm::BBR);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    sc.set_cc_algorithm(quiche::CongestionControlAlgorithm::BBR);

    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let before = first_cwnd(&p.client);
    assert!(
        before > 0,
        "expected a positive initial congestion window on the client's only path; observed {before}"
    );

    let want = testkit::body(LEN, 0x0816_0002);
    let got = transfer(&mut p, 4, &want);
    assert!(
        want == got,
        "expected {LEN} bytes to cross a BBR-configured connection byte for byte; observed {}",
        first_diff(&want, &got)
    );

    let after = first_cwnd(&p.client);
    assert!(
        after >= before,
        "expected the congestion window not to shrink across a {LEN} byte transfer on a link \
         that dropped nothing; observed {before} before and {after} after"
    );

    // 4. `Stats` is the connection-wide twenty-two, not the per-path numbers
    //    that moved to `PathStats` at this milestone. `cwnd`, `rtt`, `pmtu`
    //    and `delivery_rate` must not have been left behind here -- the
    //    destructuring below is exhaustive, so a twenty-third field, a
    //    missing one, or one of those four still present all fail to
    //    compile, which is the only mechanical way to prove a removal rather
    //    than merely an addition.
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
        peer_max_idle_timeout,
        peer_max_udp_payload_size,
        peer_initial_max_data,
        peer_initial_max_stream_data_bidi_local,
        peer_initial_max_stream_data_bidi_remote,
        peer_initial_max_stream_data_uni,
        peer_initial_max_streams_bidi,
        peer_initial_max_streams_uni,
        peer_ack_delay_exponent,
        peer_max_ack_delay,
        peer_disable_active_migration,
        peer_active_conn_id_limit,
        peer_max_datagram_frame_size,
    } = p.client.stats();

    assert!(
        sent_bytes >= LEN as u64,
        "expected the client to have sent at least the {LEN} bytes of payload; observed \
         sent_bytes={sent_bytes} over {sent} packets"
    );
    assert_eq!(
        paths_count, 1,
        "expected a connection that never probed to report exactly one path; observed \
         {paths_count}"
    );
    assert!(
        recv > 0 && sent > 0 && recv_bytes > 0,
        "expected a connection that ran a {LEN} byte transfer to have both sent and received \
         packets and bytes; observed recv={recv} sent={sent} recv_bytes={recv_bytes}"
    );
    assert_eq!(
        (lost, retrans, lost_bytes, stream_retrans_bytes),
        (0, 0, 0, 0),
        "expected nothing lost or retransmitted on a lossless in-memory transfer; observed \
         lost={lost} retrans={retrans} lost_bytes={lost_bytes} \
         stream_retrans_bytes={stream_retrans_bytes}"
    );

    // The thirteen `peer_*` parameters describe what the *server* advertised
    // about itself, and every value here traces to `pair::tune()`, which
    // both configs in this pair go through -- so the server's advertisement
    // is these exact numbers, not defaults, not the client's own settings
    // read back.
    assert_eq!(
        (
            peer_max_idle_timeout,
            peer_max_udp_payload_size,
            peer_initial_max_data,
            peer_initial_max_stream_data_bidi_local,
            peer_initial_max_stream_data_bidi_remote,
            peer_initial_max_stream_data_uni,
            peer_initial_max_streams_bidi,
            peer_initial_max_streams_uni,
            peer_active_conn_id_limit,
            peer_disable_active_migration,
            peer_max_datagram_frame_size,
        ),
        (
            30_000,
            pair::MAX_DATAGRAM as u64,
            10_000_000,
            1_000_000,
            1_000_000,
            1_000_000,
            100,
            100,
            2,
            false,
            None,
        ),
        "expected the client's Stats to report the server's advertised transport parameters \
         exactly as pair::tune() set them on the server's own Config"
    );
    assert_eq!(
        (peer_ack_delay_exponent, peer_max_ack_delay),
        (3, 25),
        "expected the two parameters pair::tune() never touches to carry their RFC 9000 \
         Section 18.2 defaults -- ack_delay_exponent 3, max_ack_delay 25 -- rather than zero"
    );
}

// ---------------------------------------------------------------------------
// cap-migrate  (T3)
// ---------------------------------------------------------------------------

/// `cap-migrate`: `migrate()` itself -- not probing, not events, the method
/// that actually moves the connection -- plus the `PathStats` fields no other
/// group in this step reads.
///
/// `migrate()` is never called anywhere else in this step's harness:
/// `cap-path-events` proves a path can be probed and validated, and
/// `cap-probe-and-migrate` proves the reference *client binary* switches
/// sockets, but neither calls the method by name. This group does, and checks
/// the two things the interface doc promises of it: a server is refused with
/// `Error::InvalidState` unconditionally, and a client's call returns the
/// destination connection ID sequence of the path it just made active --
/// the same return contract as `probe_path`, which is easy to get right by
/// accident and easy to get wrong on purpose.
///
/// Around that, it reads every field `PathStats` has, on both the path being
/// left and the path being moved to, before and after. `local_addr`/`peer_addr`
/// identify which path is which; `active` is the one `migrate()` is defined by;
/// `recv`/`sent`/`lost`/`retrans` and the four byte counters are driven
/// non-zero by real transfers on each side of the call rather than left at
/// their post-handshake defaults, which is what makes their presence a fact
/// about behaviour instead of a fact about initialization. `cwnd` and `pmtu`
/// are asserted positive. `delivery_rate` is read and left otherwise
/// unchecked: it is a rate estimate with no specified formula, and pinning a
/// number to it would grade an implementation choice, not the capability.
/// `validation_state` cannot be named from outside the crate at this
/// milestone -- `PathState` is not re-exported -- so it is compared for
/// equality against the always-validated original path's own
/// `validation_state` rather than against a literal.
#[test]
fn cap_migrate() {
    let c = pair::client_addr();
    let c2 = pair::client_addr_2();
    let s = pair::server_addr();

    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();

    // 1. a server can never initiate a migration, regardless of connection ID
    //    or path state -- this is the very first check migrate() makes.
    assert_eq!(
        p.server.migrate(s, c),
        Err(quiche::Error::InvalidState),
        "expected a server calling migrate() to be refused with Error::InvalidState: servers \
         follow migrations, they do not initiate them"
    );

    // 2. get a second path probed and validated the same way cap-path-events
    //    does, then move real bytes on the original path so its PathStats
    //    counters are not just post-handshake defaults.
    p.exchange_cids();
    let (client_events, server_events) = p.probe_and_settle(c2);
    assert_eq!(
        client_events,
        vec![quiche::PathEvent::Validated(c2, s)],
        "expected probing the second path to validate it before migrate() is ever called"
    );
    assert_eq!(
        server_events,
        vec![quiche::PathEvent::New(s, c2), quiche::PathEvent::Validated(s, c2)]
    );

    let before_data = testkit::body(4096, 0x0816_0003);
    let before_got = transfer(&mut p, 4, &before_data);
    assert_eq!(before_data, before_got, "expected the warm-up transfer to arrive intact");

    // 3. before migrating: the original path is active, the validated second
    //    path is not -- probing validates a path, it does not make it active.
    let orig_before = p
        .client
        .path_stats()
        .find(|ps| ps.local_addr == c)
        .expect("expected the client to still report a path at its original local address");
    assert!(orig_before.active, "expected the original path to still be active before migrate()");
    assert!(orig_before.sent > 0 && orig_before.sent_bytes > 0, "expected the warm-up transfer to have moved packets and bytes on the original path");

    let second_before = p
        .client
        .path_stats()
        .find(|ps| ps.local_addr == c2)
        .expect("expected the probed-and-validated second path to already be in path_stats()");
    assert!(
        !second_before.active,
        "expected the validated second path to not be active yet: that is what migrate() is for"
    );

    // 4. migrate, and check the return value against probe_path's own
    //    contract: the destination connection ID sequence of the path that
    //    is now active.
    assert_eq!(
        p.client.migrate(c2, s),
        Ok(1),
        "expected migrate() to return the destination connection ID sequence assigned to the \
         path it just made active, the same contract probe_path documents"
    );

    // calling it again on the path that is already active succeeds rather
    // than being refused -- it is idempotent, not a one-shot trigger.
    assert_eq!(
        p.client.migrate(c2, s),
        Ok(1),
        "expected a second migrate() call naming the already-active path to succeed and return \
         the same sequence number"
    );

    // 5. move real bytes again, now that c2 is active, so its PathStats
    //    counters are driven non-zero in both directions by the call under
    //    test rather than only by the probe that preceded it.
    let after_data = testkit::body(4096, 0x0816_0004);
    let after_got = transfer(&mut p, 8, &after_data);
    assert_eq!(after_data, after_got, "expected the post-migration transfer to arrive intact");

    // the original path is no longer active; every PathStats field on it
    // still reads back, migrate() does not remove a path.
    let orig_after = p
        .client
        .path_stats()
        .find(|ps| ps.local_addr == c)
        .expect("expected the original path to still be reported by path_stats() after migrating away from it");
    assert!(!orig_after.active, "expected the original path to no longer be active once migrate() moved the connection off it");

    // 6. and the full sixteen-field shape of the now-active path -- every
    //    field PathStats has, named once each, so a sixteenth field or a
    //    renamed one fails to compile rather than passing unread.
    let quiche::PathStats {
        local_addr,
        peer_addr,
        validation_state,
        active,
        recv,
        sent,
        lost,
        retrans,
        rtt,
        cwnd,
        sent_bytes,
        recv_bytes,
        lost_bytes,
        stream_retrans_bytes,
        pmtu,
        delivery_rate,
    } = p
        .client
        .path_stats()
        .find(|ps| ps.local_addr == c2)
        .expect("expected the migrated-to path to still be reported by path_stats()");

    assert_eq!(
        (local_addr, peer_addr),
        (c2, s),
        "expected the migrated-to path's own addresses back from path_stats()"
    );
    assert!(active, "expected the migrated-to path to be marked active");
    assert_eq!(
        validation_state, orig_after.validation_state,
        "expected the migrated-to path's validation_state to read equal to the original path's \
         once both are validated -- PathState itself cannot be named outside the crate at this \
         milestone, so equality with a known-validated path is the check available"
    );
    assert!(
        sent > 0 && recv > 0 && sent_bytes > 0 && recv_bytes > 0,
        "expected the post-migration transfer to have moved packets and bytes in both \
         directions on the now-active path; observed sent={sent} recv={recv} \
         sent_bytes={sent_bytes} recv_bytes={recv_bytes}"
    );
    assert_eq!(
        (lost, retrans, lost_bytes, stream_retrans_bytes),
        (0, 0, 0, 0),
        "expected nothing lost or retransmitted on a lossless in-memory path"
    );
    assert!(cwnd > 0, "expected a positive congestion window on the now-active path");
    assert!(pmtu > 0, "expected a positive PMTU on the now-active path");
    assert!(
        rtt > Duration::ZERO,
        "expected a positive round-trip-time estimate on a path that has exchanged packets"
    );
    // delivery_rate is read to complete the exhaustive shape above; it is a
    // rate estimate with no specified formula, so no particular value is
    // asserted of it here.
    let _ = delivery_rate;
}

// ---------------------------------------------------------------------------
// cap-path-failure  (T3)
// ---------------------------------------------------------------------------

/// `cap-path-failure`: the two `PathEvent` variants no other group in this
/// step's harness ever provokes.
///
/// Every other path-event group -- `cap-path-events`, `cap-migrate` -- only
/// ever sees a probe succeed. This group makes one fail on purpose, and makes
/// a path get evicted on purpose, because a stack that only ever reports
/// `New` and `Validated` might just be constant-folding the happy case.
///
/// **`FailedValidation`.** A client probes a second path, and its
/// PATH_CHALLENGE is never answered: the flight that carries it is emitted
/// and then simply not delivered, three times in a row, with the client's own
/// loss-detection timer driven forward for real between attempts (no fixed
/// sleep is assumed; `Connection::timeout()` is read fresh each time, the
/// same wait `cap-*` groups elsewhere in this package use for a real-clock
/// dependency). Three consecutive lost probes is what the reference
/// implementation this milestone targets turns into a failure rather than a
/// fourth retransmission; this group treats that count as this milestone's
/// behaviour to match, not as a free parameter to guess at.
///
/// **`Closed`.** A server evicts a path to make room for a new one under two
/// conditions this group builds directly: `active_connection_id_limit` is 2
/// (this harness's `pair::tune` sets it explicitly, for exactly this reason),
/// and a path only gets evicted if it is *unused* -- probed by nobody, never
/// made active. The harness has no socket to rebind, so a passively-observed
/// "new path" is built the way a NAT rebinding a client's port really looks
/// to a server: the same connection, the same source connection ID, a
/// 4-tuple the server has never seen. Two distinct real client packets (not
/// one packet replayed -- the connection's own packet-number space would
/// reject a byte-for-byte repeat as a duplicate) are each delivered to the
/// server under a different invented source address. The first is accepted
/// as a new, unused, unvalidated path sitting alongside the real one; the
/// second pushes the path count over the limit and the first spoofed path --
/// still unused, nobody ever probed it or made it active -- is the one
/// evicted, in the exact order this milestone's path-eviction logic produces
/// it: `ReusedSourceConnectionId`, then `Closed` for the evicted path, then
/// `New` for the one just admitted.
///
/// Delivering a non-probing frame on a passively-observed path is not enough
/// on its own: this milestone follows the peer's migration the moment such a
/// packet is also the highest-numbered non-probing packet the connection has
/// seen, which makes the path active and therefore never evictable again. So
/// each spoofed packet is captured with `send()` and held back until a
/// later-numbered packet has crossed on the real path, and no spare
/// destination connection ID is issued anywhere in this test
/// (`exchange_cids()` is deliberately skipped), so the path cannot be handed
/// one at the end of `recv()` and silently stop being unused.
#[test]
fn cap_path_failure() {
    let c = pair::client_addr();
    let c2 = pair::client_addr_2();
    let s = pair::server_addr();

    // --- FailedValidation: a probe that is never answered -----------------

    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();
    p.exchange_cids();

    assert_eq!(
        p.client.probe_path(c2, s),
        Ok(1),
        "expected the client to be able to probe a second path once a spare connection ID had \
         crossed"
    );

    for attempt in 1..=3 {
        // Emit whatever the client currently wants to send -- the
        // PATH_CHALLENGE among it -- and simply never deliver it.
        let mut buf = [0u8; pair::MAX_DATAGRAM];
        let mut sent = 0usize;
        loop {
            match p.client.send(&mut buf) {
                Ok(_) => sent += 1,
                Err(quiche::Error::Done) => break,
                Err(e) => panic!(
                    "client send while probing (lost-probe attempt {attempt} of 3): {e:?}"
                ),
            }
        }
        assert!(
            sent > 0,
            "expected the client to have something to send on lost-probe attempt {attempt} of \
             3 -- the PATH_CHALLENGE itself the first time, its retransmission thereafter"
        );

        // Drive the client's own timer forward for real, by however long it
        // says is left, rather than assuming a fixed sleep.
        let wait = p.client.timeout().unwrap_or(Duration::from_millis(50));
        std::thread::sleep(wait + Duration::from_millis(1));
        p.client.on_timeout();
    }

    assert_eq!(
        pair::drain_events(&mut p.client),
        vec![quiche::PathEvent::FailedValidation(c2, s)],
        "expected three consecutive lost PATH_CHALLENGE probes, spaced by the client's own \
         loss-detection timer, to fail validation of the probed path"
    );

    // --- Closed: an unused path evicted to make room for a new one --------
    //
    // Two traps make this harder than it looks. First, the server must never
    // have a spare destination connection ID to hand a passively-observed
    // path (no exchange_cids() below), or the "no_dcid" allocation quiche
    // runs at the end of every recv() would hand the new path one and it
    // would stop being "unused". Second, and less obvious: a
    // passively-observed path that carries a non-probing frame (anything
    // other than PADDING, NEW_CONNECTION_ID, PATH_CHALLENGE or
    // PATH_RESPONSE -- so any packet with real stream data in it, which is
    // what distinguishes this passive-observation recipe from a
    // probe_path() call) is promoted straight to *active* the instant its
    // packet number is the highest non-probing packet number the connection
    // has seen so far (quiche follows the peer's migration automatically).
    // An active path is never "unused" and so is never evicted.
    //
    // To carry real stream data while staying clear of that promotion, each
    // spoofed packet below is captured with client.send() but held back,
    // and only delivered under the spoofed address after a later-numbered
    // packet has already crossed on the real path -- so by the time the
    // server sees the held one, it is no longer the connection's
    // highest-numbered non-probing packet, and the auto-migration check
    // never fires.

    let spoofed_1: SocketAddr = "127.0.0.1:52801".parse().unwrap();
    let spoofed_2: SocketAddr = "127.0.0.1:52802".parse().unwrap();

    let mut q = pair::Pair::new(pair::ALPN_HQ);
    q.handshake();

    // Round 1: capture-but-hold packet A, then send and deliver packet B on
    // the real path so A is no longer the connection's newest non-probing
    // packet by the time it is delivered under spoofed_1.
    q.client
        .stream_send(4, b"held for spoofed address 1", false)
        .expect("client stream_send 1a");
    let mut buf_a = [0u8; pair::MAX_DATAGRAM];
    let (len_a, _info_a) = q.client.send(&mut buf_a).expect("client send 1a (held)");
    let mut pkt_a = buf_a[..len_a].to_vec();

    q.client
        .stream_send(4, b"sent on the real path to advance the packet number", false)
        .expect("client stream_send 1b");
    let mut buf_b = [0u8; pair::MAX_DATAGRAM];
    let (len_b, _info_b) = q.client.send(&mut buf_b).expect("client send 1b (real path)");
    q.server
        .recv(&mut buf_b[..len_b], quiche::RecvInfo { from: c, to: s })
        .expect("server recv on the real path, round 1");
    assert!(
        pair::drain_events(&mut q.server).is_empty(),
        "expected a packet delivered on the already-known real path to produce no path events"
    );

    q.server
        .recv(&mut pkt_a, quiche::RecvInfo { from: spoofed_1, to: s })
        .expect("server recv of the held, now-stale-numbered packet under spoofed_1");

    assert_eq!(
        pair::drain_events(&mut q.server),
        vec![
            quiche::PathEvent::ReusedSourceConnectionId(0, (s, c), (s, spoofed_1)),
            quiche::PathEvent::New(s, spoofed_1),
        ],
        "expected the server to recognise its own connection ID arriving from an address it had \
         never seen as a new, passively-observed path -- not a new connection, and not the same \
         path"
    );

    // Round 2: same trick again, this time landing on spoofed_2 and pushing
    // the server over its active_connection_id_limit of 2 concurrent paths.
    q.client
        .stream_send(4, b"held for spoofed address 2", false)
        .expect("client stream_send 2a");
    let mut buf_c = [0u8; pair::MAX_DATAGRAM];
    let (len_c, _info_c) = q.client.send(&mut buf_c).expect("client send 2a (held)");
    let mut pkt_c = buf_c[..len_c].to_vec();

    q.client
        .stream_send(4, b"sent on the real path to advance the packet number again", false)
        .expect("client stream_send 2b");
    let mut buf_d = [0u8; pair::MAX_DATAGRAM];
    let (len_d, _info_d) = q.client.send(&mut buf_d).expect("client send 2b (real path)");
    q.server
        .recv(&mut buf_d[..len_d], quiche::RecvInfo { from: c, to: s })
        .expect("server recv on the real path, round 2");
    assert!(
        pair::drain_events(&mut q.server).is_empty(),
        "expected a packet delivered on the already-known real path to produce no path events"
    );

    q.server
        .recv(&mut pkt_c, quiche::RecvInfo { from: spoofed_2, to: s })
        .expect("server recv of the held, now-stale-numbered packet under spoofed_2");

    assert_eq!(
        pair::drain_events(&mut q.server),
        vec![
            quiche::PathEvent::ReusedSourceConnectionId(0, (s, c), (s, spoofed_2)),
            quiche::PathEvent::Closed(s, spoofed_1),
            quiche::PathEvent::New(s, spoofed_2),
        ],
        "expected a third distinct address to push the server over its \
         active_connection_id_limit of 2 concurrent paths, evicting the first spoofed path -- \
         still unused, never probed or made active -- before admitting the second; \
         ReusedSourceConnectionId first, then Closed for the one evicted, then New for the one \
         just admitted"
    );

    assert!(
        !q.client.is_closed() && !q.server.is_closed(),
        "expected two passively-observed paths, one of them evicted, to leave the connection \
         open; client local error {:?}, server local error {:?}",
        q.client.local_error().map(|e| e.error_code),
        q.server.local_error().map(|e| e.error_code)
    );
}

// ---------------------------------------------------------------------------
// cap-new-source-cid  (T3)
// ---------------------------------------------------------------------------

/// `cap-new-source-cid`: issuing a connection ID, and the limit that bounds
/// it.
///
/// The first connection ID an endpoint issues is **sequence 1**: sequence 0
/// came from the handshake and was never issued through this call. Issuing
/// past the peer's `active_connection_id_limit` with `retire_if_needed =
/// false` is `Err(Error::IdLimit)` -- a distinct error from every other
/// refusal in this step, because the request is well formed and merely does
/// not fit.
///
/// The bookkeeping is asserted alongside, because the counters are what an
/// application steers by: `source_cids_left()` reaches exactly 0 at the
/// boundary rather than going negative or saturating, and the peer's
/// `available_dcids()` rises only once the NEW_CONNECTION_ID frame has
/// actually crossed -- which is the observable consequence, as opposed to the
/// local intent.
#[test]
fn cap_new_source_cid() {
    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();

    assert_eq!(
        p.client.active_source_cids(),
        1,
        "expected a freshly handshaked connection to have exactly one active source connection \
         ID, the handshake's"
    );
    assert_eq!(
        p.client.max_active_source_cids(),
        2,
        "expected the peer's advertised active_connection_id_limit of 2 to be reported here"
    );
    assert_eq!(
        p.client.source_cids_left(),
        1,
        "expected room for exactly one more source connection ID: a limit of 2 with 1 in use"
    );
    assert_eq!(
        p.server.available_dcids(),
        0,
        "expected the server to hold no spare destination connection IDs before the client has \
         issued one"
    );

    let (cid, token) = pair::spare_cid(0x51);
    assert_eq!(
        p.client.new_source_cid(&cid, token, false),
        Ok(1),
        "expected the first connection ID issued through new_source_cid to be sequence 1, \
         sequence 0 being the one the handshake established"
    );
    assert_eq!(
        p.client.active_source_cids(),
        2,
        "expected the issued connection ID to count as active immediately, before the frame has \
         crossed: it is this endpoint's own, and it must be prepared to be addressed by it"
    );
    assert_eq!(
        p.client.source_cids_left(),
        0,
        "expected no room left after issuing the second of a permitted two"
    );

    // One past the limit, without permission to retire anything.
    let (over, over_token) = pair::spare_cid(0x52);
    assert_eq!(
        p.client.new_source_cid(&over, over_token, false),
        Err(quiche::Error::IdLimit),
        "expected issuing past the peer's active_connection_id_limit with retire_if_needed=false \
         to be refused with Error::IdLimit, rather than succeeding silently, overwriting an \
         existing identifier, or panicking"
    );
    assert_eq!(
        p.client.source_cids_left(),
        0,
        "expected a refused issue to leave the bookkeeping alone; observed it moved"
    );

    // The observable consequence, once the frame has crossed.
    p.settle(16);
    assert_eq!(
        p.server.available_dcids(),
        1,
        "expected the server to have learnt exactly one spare destination connection ID from the \
         NEW_CONNECTION_ID frame the client sent"
    );
    assert!(
        !p.client.is_closed() && !p.server.is_closed(),
        "expected issuing a connection ID to leave the connection open; client local error {:?}, \
         server local error {:?}",
        p.client.local_error().map(|e| e.error_code),
        p.server.local_error().map(|e| e.error_code)
    );
}

// ---------------------------------------------------------------------------
// cap-send-on-path  (T3)
// ---------------------------------------------------------------------------

/// `cap-send-on-path`: naming a path on the send side, and refusing one that
/// does not exist.
///
/// Immediately after `probe_path` there is exactly one thing to send and
/// exactly one path to send it on, which makes the observation sharp: asking
/// for the *original* path returns `Error::Done` because nothing is queued
/// there, and asking for the *new* path returns the PATH_CHALLENGE, described
/// by a `SendInfo` naming the addresses that were asked for. RFC 9000 section
/// 8.2.1 requires that datagram be padded to at least 1200 bytes, so the group
/// checks the size too -- an unpadded challenge is an amplification vector,
/// not a smaller optimisation.
///
/// The teeth are the refusal. A four-tuple the connection has never seen is
/// `Err(Error::InvalidState)`, in both the unknown-local and unknown-peer
/// directions, and it does *not* fall back to the active path. Falling back is
/// the failure mode that leaves a migration bug invisible until it is a packet
/// on the wrong network.
///
/// The group deliberately does not assert that `send_on_path(out, None, None)`
/// agrees with `send(out)`. In a correct implementation `send` is defined as
/// that call, so the equivalence is true by construction and proves nothing.
#[test]
fn cap_send_on_path() {
    let c = pair::client_addr();
    let c2 = pair::client_addr_2();
    let s = pair::server_addr();
    let unknown = pair::unknown_addr();

    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();
    p.exchange_cids();

    assert_eq!(p.client.probe_path(c2, s), Ok(1), "probe_path after exchanging connection IDs");

    // The new path is addressable from the moment it is probed: `paths_iter`
    // answers "which peers can I reach from this local address", and a
    // migrating application asks it before it asks anything else.
    assert_eq!(
        p.client.paths_iter(c2).collect::<Vec<_>>(),
        vec![s],
        "expected paths_iter({c2}) to report the server reachable from the second local address \
         as soon as that path had been probed"
    );
    assert_eq!(
        p.client.paths_iter(unknown).collect::<Vec<_>>(),
        Vec::<std::net::SocketAddr>::new(),
        "expected paths_iter to report nothing reachable from a local address the connection has \
         no path on"
    );

    let mut buf = vec![0u8; 2048];

    // Nothing is queued on the path that has been idle since the handshake.
    assert_eq!(
        p.client.send_on_path(&mut buf, Some(c), Some(s)).map(|(n, _)| n),
        Err(quiche::Error::Done),
        "expected nothing to send on the original path: the probe belongs to the new one"
    );

    // The probe is on the new path, and the descriptor says so.
    let (n, info) = p
        .client
        .send_on_path(&mut buf, Some(c2), Some(s))
        .expect("send_on_path for the path just probed");
    assert_eq!(
        (info.from, info.to),
        (c2, s),
        "expected the SendInfo to describe the path that was asked for -- from {c2}, to {s}; \
         observed from={} to={}",
        info.from,
        info.to
    );
    assert!(
        n >= 1200,
        "expected the PATH_CHALLENGE datagram to be padded to at least 1200 bytes, per RFC 9000 \
         section 8.2.1; observed {n} bytes. An unpadded path challenge lets a new path be used \
         to amplify"
    );

    // A four-tuple the connection has never seen, in both directions.
    assert_eq!(
        p.client.send_on_path(&mut buf, Some(unknown), Some(s)).map(|(n, _)| n),
        Err(quiche::Error::InvalidState),
        "expected an unknown local address to be refused with Error::InvalidState rather than \
         silently served from the active path"
    );
    assert_eq!(
        p.client.send_on_path(&mut buf, Some(c), Some(unknown)).map(|(n, _)| n),
        Err(quiche::Error::InvalidState),
        "expected an unknown peer address to be refused with Error::InvalidState rather than \
         silently served from the active path"
    );

    // And the connection knows two paths, one from each local address. This
    // is read from `path_stats`, which reports every path the connection
    // holds, rather than from `paths_iter`, which reports only the ones that
    // can be sent on right now -- the probe has been taken out of the queue
    // above and not delivered, so the new path is legitimately in neither
    // "usable" nor "needs probing" at this instant.
    let mut locals: Vec<_> = p
        .client
        .path_stats()
        .map(|st| st.local_addr)
        .collect();
    locals.sort();
    let mut want = vec![c, c2];
    want.sort();
    assert_eq!(
        locals, want,
        "expected the client to know two paths, one from each local address; observed {locals:?}"
    );
}

// ---------------------------------------------------------------------------
// cap-alpn-list-shape  (T3)
// ---------------------------------------------------------------------------

/// `cap-alpn-list-shape`: `APPLICATION_PROTOCOL` becomes a list of names, and
/// the old packed form keeps working under a new name.
///
/// This is the one group at this step that is not about presence. The constant
/// exists at step 07 too -- as a single 21-byte buffer with each name preceded
/// by its own length byte, the shape TLS wants on the wire. Here it is four
/// separate names with no length prefixes, so its *shape* separates the two
/// milestones without reading anyone's source.
///
/// The change is a rename, not a deletion, and both halves are scored. The
/// list form goes through `set_application_protos` and has to negotiate `h3`;
/// the packed form goes through `set_application_protos_wire_format` and has
/// to negotiate `h3` as well. A candidate who deleted the old code path
/// instead of renaming it passes the first half and fails the second.
#[test]
fn cap_alpn_list_shape() {
    let protos = quiche::h3::APPLICATION_PROTOCOL;

    assert_eq!(
        protos.len(),
        4,
        "expected APPLICATION_PROTOCOL to iterate as four protocol names; observed {} elements",
        protos.len()
    );
    assert_eq!(
        protos.iter().map(|p| p.len()).collect::<Vec<_>>(),
        vec![2, 5, 5, 5],
        "expected the four names to be 2, 5, 5 and 5 bytes long. A length of 3 on the first \
         element means the length byte is still inside it, which is the step 07 shape"
    );
    assert_eq!(
        protos,
        &[&b"h3"[..], b"h3-29", b"h3-28", b"h3-27"],
        "expected the names themselves, most preferred first, with no length prefixes"
    );

    // The list form negotiates h3.
    let mut cc = pair::client_config(quiche::h3::APPLICATION_PROTOCOL);
    let mut sc = pair::server_config(quiche::h3::APPLICATION_PROTOCOL);
    let h = pair::H3Pair::over(pair::Pair::with_configs(&mut cc, &mut sc));
    assert_eq!(
        h.t.server.application_proto(),
        b"h3",
        "expected a connection configured from APPLICATION_PROTOCOL itself to negotiate h3"
    );
    drop(h);

    // And the packed form still does, through the renamed setter.
    const PACKED: &[u8] = b"\x02h3\x05h3-29\x05h3-28\x05h3-27";
    let mut cc = pair::client_config_wire_format(PACKED);
    let mut sc = pair::server_config_wire_format(PACKED);
    let h = pair::H3Pair::over(pair::Pair::with_configs(&mut cc, &mut sc));
    assert_eq!(
        h.t.client.application_proto(),
        b"h3",
        "expected set_application_protos_wire_format to still accept the step 07 packed buffer \
         and negotiate h3 from it: the old behaviour was renamed, not removed"
    );
}

// ---------------------------------------------------------------------------
// cap-retire-destination-cid  (T3)
// ---------------------------------------------------------------------------

/// `cap-retire-destination-cid`: stopping the use of one of the peer's
/// connection IDs, and the two different ways the call is refused.
///
/// The success case is asserted through its consequence rather than its
/// return value: the peer must observe the retirement through
/// `retired_scid_next()`, handing back the exact connection ID it had been
/// reachable by, and once. Stream data still flows afterwards, because
/// retiring a connection ID is a rotation and not a teardown.
///
/// The two refusals are different conditions and are asserted by variant,
/// because an implementation that conflates them is wrong about one of them:
///
/// * a sequence number the connection does not hold is
///   `Err(Error::InvalidState)` -- there is nothing to retire;
/// * the last remaining destination connection ID is
///   `Err(Error::OutOfIdentifiers)` -- there is something to retire, and
///   retiring it would leave the connection unable to address its peer at all.
///
/// The order matters and is the reason they are checked in this one: the
/// unknown-sequence case has to be exercised while a spare still exists, or
/// the emptiness check fires first and hides it.
#[test]
fn cap_retire_destination_cid() {
    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();
    p.exchange_cids();

    // 1. a sequence number that was never issued, while two are held.
    assert_eq!(
        p.client.retire_destination_cid(99),
        Err(quiche::Error::InvalidState),
        "expected retiring a sequence number the connection does not hold to be refused with \
         Error::InvalidState"
    );

    // 2. the handshake's, which is retirable because a spare exists.
    assert_eq!(
        p.client.retire_destination_cid(0),
        Ok(()),
        "expected the handshake's destination connection ID to be retirable once the peer had \
         issued a spare to take over from it"
    );

    p.settle(16);

    // 3. the peer's view: it learns which of its own identifiers is finished.
    assert_eq!(
        p.server.retired_scid_next(),
        Some(quiche::ConnectionId::from_vec(pair::SERVER_CID.to_vec())),
        "expected the server to be told, by connection ID, which of its own source connection \
         IDs the client has stopped using -- the one the handshake gave it"
    );
    assert_eq!(
        p.server.retired_scid_next(),
        None,
        "expected exactly one retirement to be reported: the list is drained, not polled"
    );

    // 4. and the connection is a connection still.
    let want = testkit::body(4096, 0x0816_0003);
    let got = transfer(&mut p, 4, &want);
    assert!(
        want == got,
        "expected stream data to keep flowing after a connection ID was retired -- a rotation, \
         not a teardown; observed {}",
        first_diff(&want, &got)
    );

    // 5. the last one standing.
    assert_eq!(
        p.client.retire_destination_cid(1),
        Err(quiche::Error::OutOfIdentifiers),
        "expected retiring the only remaining destination connection ID to be refused with \
         Error::OutOfIdentifiers: a connection that retired its last way of addressing its peer \
         would have to stall, and refusing now is better than stalling later"
    );
}
