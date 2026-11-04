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
//!
//! Two of this milestone's subjects -- the relocated QPACK Huffman coder and
//! the new `netlog` crate -- are reachable only from workspace members that
//! `quiche` does not re-export. Those groups build a throwaway crate outside
//! the candidate's workspace that depends on the member by path, exactly as
//! step 13 did for `tokio-quiche`. Keeping the dependency in a probe rather
//! than in this harness's own manifest is deliberate: a candidate who shipped
//! every other group but not the member would otherwise fail to build the
//! harness and score zero everywhere, instead of losing the one group.

use std::path::{Path, PathBuf};
use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

const SHORT: Duration = Duration::from_secs(10);
const LONG: Duration = Duration::from_secs(60);

// ---------------------------------------------------------------------------
// shared plumbing: hand-pumped datagram delivery
// ---------------------------------------------------------------------------

/// One datagram and the `SendInfo` the candidate handed back beside it.
type Dgram = (Vec<u8>, quiche::SendInfo);

/// Everything `c` currently wants to send, taken off it without delivering.
///
/// [`pair::Pair::exchange`] moves datagrams and forgets them. Two groups here
/// need to drop exactly one datagram and deliver the rest, so they pump by
/// hand instead.
fn drain(c: &mut quiche::Connection) -> Vec<Dgram> {
    let mut out = [0u8; pair::MAX_DATAGRAM];
    let mut v = Vec::new();
    loop {
        match c.send(&mut out) {
            Ok((n, info)) => v.push((out[..n].to_vec(), info)),
            Err(quiche::Error::Done) => break,
            Err(e) => panic!("send failed while draining: {e:?}"),
        }
    }
    v
}

/// Hand `pkts` to `c`, each on the path its sender addressed it to.
fn deliver(c: &mut quiche::Connection, pkts: &[Dgram]) {
    for (d, info) in pkts {
        let mut b = d.clone();
        c.recv(&mut b, quiche::RecvInfo { from: info.from, to: info.to })
            .expect("peer rejected a datagram the candidate itself produced");
    }
}

/// The indices of the datagrams in `pkts` whose 1-RTT payload carries a frame
/// `want` accepts, and a rendering of every datagram for failure messages.
///
/// Packet numbers are decoded in order, each seeded with the last one
/// recovered, which is what a receiver does. Long-header packets and anything
/// the keys do not open come back as `opaque` and are never matched.
fn scan_frames<F>(
    pkts: &[Dgram], keys: &support::PacketKeys, dcid_len: usize, want: F,
) -> (Vec<usize>, String)
where
    F: Fn(&support::Frame) -> bool,
{
    let mut seen: Option<u64> = None;
    let mut hits = Vec::new();
    let mut log = String::new();
    for (i, (d, _)) in pkts.iter().enumerate() {
        match pair::open_1rtt(d, dcid_len, keys, seen) {
            Some(op) => {
                seen = Some(op.packet_number);
                let frames = pair::decoded_frames(&op.payload);
                let names: Vec<&str> = frames.iter().map(frame_name).collect();
                log.push_str(&format!("  [{i}] pn={} {:?}\n", op.packet_number, names));
                if frames.iter().any(&want) {
                    hits.push(i);
                }
            },
            None => log.push_str(&format!("  [{i}] opaque ({} bytes)\n", d.len())),
        }
    }
    (hits, log)
}

/// The frame's variant name, for failure messages.
fn frame_name<'a>(f: &support::Frame<'a>) -> &'static str {
    match f {
        support::Frame::Padding { .. } => "Padding",
        support::Frame::Ping => "Ping",
        support::Frame::Ack { .. } => "Ack",
        support::Frame::Crypto { .. } => "Crypto",
        support::Frame::ConnectionClose { .. } => "ConnectionClose",
        support::Frame::ResetStream { .. } => "ResetStream",
        support::Frame::StopSending { .. } => "StopSending",
        support::Frame::NewToken { .. } => "NewToken",
        support::Frame::Stream { .. } => "Stream",
        support::Frame::MaxData { .. } => "MaxData",
        _ => "other",
    }
}

/// The 1-RTT packet-protection keys one end wrote into `log`, under `label`.
fn traffic_keys(log: &[u8], label: &str) -> support::PacketKeys {
    let secret = pair::keylog_secret(log, label)
        .unwrap_or_else(|| panic!("the client wrote no {label} record into its keylog"));
    support::PacketKeys::from_secret(support::Suite::Aes128Gcm, &secret)
        .unwrap_or_else(|_| panic!("deriving 1-RTT keys from {label}"))
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

/// A connection reports the path MTU it actually discovered.
///
/// Path MTU discovery itself is not new -- step 11 brought it in and observed
/// it by watching datagram sizes grow. What is new at this milestone is that
/// the connection will tell you the answer: `Connection::pmtu` returns the
/// discovered size, and `Connection::revalidate_pmtu` throws that answer away
/// so the search runs again.
///
/// The distinction matters because the previous milestone already had a
/// number that looked like this and was not: `PathStats::pmtu` reports the
/// *configured* maximum, so it reads the same whether discovery ran, was
/// switched off, or failed. This accessor does not. The four legs below pin
/// exactly that:
///
///   before the handshake                  `None`  -- nothing discovered yet
///   handshake settled, discovery off      `None`  -- and not the configured max
///   handshake settled, discovery on       `Some`  -- the discovered size
///   immediately after `revalidate_pmtu`   `None`  -- the answer is withdrawn
///
/// A candidate that wires `pmtu()` to the configured maximum passes leg three
/// and fails legs one, two and four, which is the point of having all four.
/// The discovered size is asserted against the value both ends reach rather
/// than against a constant the verifier picked, and the two ends are required
/// to agree.
#[test]
fn cap_pmtu_discovered() {
    // Leg 1 and 2: discovery off.
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    let mut off = pair::Pair::with_configs(&mut cc, &mut sc);
    assert_eq!(
        off.client.pmtu(),
        None,
        "a connection that has not finished its handshake has discovered no \
         path MTU, so `pmtu()` must be None; a candidate returning Some here \
         is reporting configuration rather than discovery"
    );
    off.handshake();
    off.settle(32);
    assert!(
        off.client.is_established() && off.server.is_established(),
        "handshake did not complete with PMTU discovery off"
    );
    assert_eq!(
        off.client.pmtu(),
        None,
        "PMTU discovery was left off on both configs, so no probe ever \
         completed and `pmtu()` must still be None after the connection \
         settled; observed {:?}. The configured maximum datagram size is \
         {} -- reporting that value here is the failure this leg exists to \
         catch.",
        off.client.pmtu(),
        pair::MAX_DATAGRAM
    );

    // Leg 3: discovery on.
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.discover_pmtu(true);
    sc.discover_pmtu(true);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();
    p.settle(32);
    let found = p.client.pmtu().unwrap_or_else(|| {
        panic!(
            "both ends had `discover_pmtu(true)` and the connection settled, \
             so the search has finished and `pmtu()` must report the size it \
             found; observed None"
        )
    });
    assert_eq!(
        p.server.pmtu(),
        Some(found),
        "the two ends of one path disagree about the discovered MTU: client \
         {found}, server {:?}",
        p.server.pmtu()
    );
    assert!(
        (1200..=pair::MAX_DATAGRAM).contains(&found),
        "the discovered path MTU is {found}, which is outside the range a \
         loopback search can produce: it cannot be below the 1200 byte \
         minimum RFC 9000 5.6 requires, and it cannot exceed the {} byte \
         datagram the verifier's config permits",
        pair::MAX_DATAGRAM
    );

    // Leg 4: revalidation withdraws the answer.
    p.client.revalidate_pmtu();
    assert_eq!(
        p.client.pmtu(),
        None,
        "`revalidate_pmtu` restarts the search, so the previously discovered \
         size is no longer a fact about the path and `pmtu()` must report \
         None until a new probe completes; observed {:?}",
        p.client.pmtu()
    );
    p.settle(32);
    assert_eq!(
        p.client.pmtu(),
        Some(found),
        "after revalidation the connection settled again, so the search has \
         re-run on an unchanged loopback path and must arrive back at \
         {found}; observed {:?}",
        p.client.pmtu()
    );
}

/// An application can throw away received stream data without copying it.
///
/// `Connection::stream_discard` is new at this milestone. It takes a byte
/// count, drops that much of what the stream has buffered, and returns how
/// much it actually dropped together with whether that consumed the stream's
/// final byte. Every previous milestone could only reach buffered data through
/// `stream_recv`, which copies it.
///
/// Four legs, each an exact return value rather than a range:
///
///   discard less than is buffered   `Ok((asked, false))`
///   discard more than is buffered   `Ok((remaining, true))` -- fin reached
///   discard again, nothing left     `Err(Done)`
///   discard on a stream never seen  `Err(InvalidStreamState(id))`
///
/// The third leg is what separates a real implementation from one that
/// reports success for any request, and the fourth is what separates it from
/// one that silently creates the stream.
#[test]
fn cap_stream_discard() {
    const STREAM_ID: u64 = 0;
    const UNKNOWN_STREAM: u64 = 400;
    const TOTAL: usize = 4_000;
    const FIRST: usize = 1_000;

    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();

    let body = testkit::body(TOTAL, 0x5d15_0014);
    let n = p.client.stream_send(STREAM_ID, &body, true).expect("client stream_send");
    assert_eq!(n, TOTAL, "client stream_send accepted {n} of {TOTAL} bytes");
    p.settle(64);

    assert_eq!(
        p.server.stream_discard(STREAM_ID, FIRST),
        Ok((FIRST, false)),
        "discarding {FIRST} of the {TOTAL} buffered bytes must drop exactly \
         {FIRST} and must not report the stream finished, because {} bytes \
         and the fin are still buffered behind them",
        TOTAL - FIRST
    );
    assert_eq!(
        p.server.stream_discard(STREAM_ID, TOTAL + 1_000),
        Ok((TOTAL - FIRST, true)),
        "discarding more than is buffered must drop what is there -- the \
         remaining {} bytes -- and report true, because that request reached \
         the fin the sender wrote",
        TOTAL - FIRST
    );
    assert!(
        p.server.stream_finished(STREAM_ID),
        "the stream's whole body and its fin have been discarded, so the \
         connection must consider the receive side finished"
    );
    assert_eq!(
        p.server.stream_discard(STREAM_ID, 1),
        Err(quiche::Error::Done),
        "a third discard has nothing left to drop and must report Done \
         rather than another success"
    );
    assert_eq!(
        p.server.stream_discard(UNKNOWN_STREAM, 10),
        Err(quiche::Error::InvalidStreamState(UNKNOWN_STREAM)),
        "discarding from a stream neither peer has opened must report \
         InvalidStreamState({UNKNOWN_STREAM}) rather than creating it or \
         reporting Done"
    );
}

/// The workspace grew to eleven members, two of them new.
///
/// A layout check, scored as layout and never as behaviour: it asks cargo
/// what the workspace contains, not what the code does. It is here because
/// this milestone's two subjects are whole crates, and a candidate can
/// implement the Huffman and netlog groups inside `quiche` without ever
/// creating the members the release actually ships. The members it names are
/// also the ones the other groups depend on by path, so a failure here
/// explains those failures rather than duplicating them.
///
/// The nine carried members are asserted as well as the two new ones: a
/// workspace that gained `netlog` by losing `h3i` has not done what this step
/// asks.
#[test]
fn cap_eleven_member_workspace() {
    let dir = testkit::scratch("cap-eleven-member-workspace");
    let root = testkit::candidate_root();

    const NEW: [&str; 2] = ["netlog", "qlog-dancer"];
    const CARRIED: [&str; 9] = [
        "apps",
        "buffer-pool",
        "datagram-socket",
        "h3i",
        "octets",
        "qlog",
        "quiche",
        "task-killswitch",
        "tokio-quiche",
    ];

    for name in NEW.iter().chain(CARRIED.iter()) {
        let manifest = root.join(name).join("Cargo.toml");
        assert!(
            manifest.is_file(),
            "expected a workspace member manifest at {}; the workspace at {} \
             does not have one there",
            manifest.display(),
            root.display()
        );
    }

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
    let members = workspace_members_text(&meta.stdout).unwrap_or_else(|| {
        panic!(
            "`cargo metadata` produced no workspace_members array ({} bytes \
             of stdout)",
            meta.stdout.len()
        )
    });
    for name in NEW.iter() {
        assert!(
            member_named(members, name),
            "expected the candidate's workspace to have a `{name}` member; \
             cargo reported workspace_members = {members}"
        );
    }
    for name in CARRIED.iter() {
        assert!(
            member_named(members, name),
            "the `{name}` member carried forward from the previous milestone \
             is gone; cargo reported workspace_members = {members}"
        );
    }
    let count = members.matches("path+file").count();
    assert_eq!(
        count,
        NEW.len() + CARRIED.len(),
        "expected exactly {} workspace members and cargo reported {count}: \
         {members}",
        NEW.len() + CARRIED.len()
    );
}

/// The `"workspace_members": [ ... ]` array of a `cargo metadata` document, as
/// raw text. Structural extraction only -- the harness has no JSON parser and
/// does not need one to ask which names cargo listed.
fn workspace_members_text(metadata: &str) -> Option<&str> {
    let start = metadata.find("\"workspace_members\":")?;
    let open = metadata[start..].find('[')? + start;
    let close = metadata[open..].find(']')? + open;
    Some(&metadata[open..=close])
}

/// Whether `members` -- the text of a `workspace_members` array -- names a
/// package called `name`. Cargo spells each entry as a package id containing
/// the path to the member's directory, so the test is for the directory
/// component rather than a bare substring, which would match `qlog` inside
/// `qlog-dancer`.
fn member_named(members: &str, name: &str) -> bool {
    members
        .split(',')
        .any(|entry| entry.contains(&format!("/{name}#")) || entry.contains(&format!("/{name}@")))
}

/// A server can complete a handshake it forced through address validation.
///
/// Retry is not new; being able to *finish* a retried handshake through the
/// public Rust API is. Before this milestone the only way to accept a retried
/// connection was `accept_with_odcid`, which carries the original destination
/// connection ID and infers the retry source connection ID from it. This
/// milestone adds `RetryConnectionIds`, which carries both, and
/// `accept_with_retry`, which takes it.
///
/// The group is T3 rather than T1 on purpose. `quiche::accept_with_retry` has
/// exactly one caller in the release, inside the async router; the shipped
/// `quiche-server` binary still calls `quiche::accept`, so a two-process run
/// would exercise the old entry point no matter what the candidate did to the
/// new one. This drives the whole dance in process instead:
///
///   1. the client sends an Initial and the verifier reads its DCID off the
///      wire with `Header::from_slice` -- that DCID is the original
///      destination connection ID;
///   2. the verifier mints a Retry with `quiche::retry`, choosing a fresh
///      server connection ID, and gives it to the client;
///   3. the client re-sends its Initial, now addressed to the new ID and
///      carrying the token;
///   4. the server accepts with `accept_with_retry`, told both IDs.
///
/// Both connection IDs end up in the server's transport parameters, and the
/// client checks them against what it saw. So the group has a genuine
/// negative: substitute either ID with one the client never saw and the
/// client must refuse the handshake. A candidate that accepts the parameters
/// without checking passes leg one and fails legs two and three.
#[test]
fn cap_accept_with_retry() {
    // The connection ID the verifier's Retry hands the client.
    let retry_scid = [0x5au8; quiche::MAX_CONN_ID_LEN];
    // Two IDs the client is never told about.
    let bogus_a = [0x33u8; quiche::MAX_CONN_ID_LEN];
    let bogus_b = [0x44u8; quiche::MAX_CONN_ID_LEN];

    let good = retry_dance(&retry_scid, None, None);
    assert!(
        good.token_len > 0,
        "the client did not put a token in its second Initial, so it never \
         accepted the Retry packet the verifier minted and the rest of this \
         group would be measuring an ordinary handshake"
    );
    assert_eq!(
        good.second_dcid,
        retry_scid.to_vec(),
        "after a Retry the client must address its next Initial to the \
         connection ID the Retry carried; it used {} instead",
        testkit::hex(&good.second_dcid)
    );
    assert!(
        good.client_established && good.server_established,
        "a Retry handshake accepted with the matching RetryConnectionIds must \
         complete: client established {}, server established {}, client local \
         error {:?}, client peer error {:?}",
        good.client_established,
        good.server_established,
        good.client_error,
        good.peer_error
    );
    assert_eq!(
        good.client_error, None,
        "the client raised {:?} on a Retry handshake whose connection IDs all \
         matched",
        good.client_error
    );

    for (what, rsc, odcid) in [
        ("retry_source_connection_id", Some(bogus_a), None),
        ("original_destination_connection_id", None, Some(bogus_b)),
    ] {
        let bad = retry_dance(&retry_scid, rsc.as_ref(), odcid.as_ref());
        assert!(
            !bad.client_established,
            "the server told the client its {what} was {}, which is not what \
             the client saw; the client must refuse the handshake and it \
             established instead",
            testkit::hex(rsc.as_ref().or(odcid.as_ref()).map(|v| &v[..]).unwrap_or(&[]))
        );
        assert_eq!(
            bad.client_error,
            Some((false, 0x08)),
            "a {what} the client did not see is a transport parameter error: \
             RFC 9000 7.3 requires the client to close the connection with \
             TRANSPORT_PARAMETER_ERROR (0x08). The client reported {:?} \
             instead (the pair is `(is_app, error_code)`).",
            bad.client_error
        );
    }
}

/// What one run of the Retry dance produced.
struct RetryOutcome {
    /// Length of the token in the client's second Initial.
    token_len: usize,
    /// Destination connection ID of the client's second Initial.
    second_dcid: Vec<u8>,
    client_established: bool,
    server_established: bool,
    /// `(is_app, error_code)` of the error the client raised itself.
    client_error: Option<(bool, u64)>,
    /// `(is_app, error_code)` of the error the client was sent.
    peer_error: Option<(bool, u64)>,
}

/// Run one Retry handshake.
///
/// `retry_scid` is the connection ID the Retry packet carries, and is what the
/// server should declare as its retry source connection ID. `rsc_override` and
/// `odcid_override` substitute a different value into the `RetryConnectionIds`
/// the server is accepted with, which is how the negative legs are built.
fn retry_dance(
    retry_scid: &[u8; quiche::MAX_CONN_ID_LEN],
    rsc_override: Option<&[u8; quiche::MAX_CONN_ID_LEN]>,
    odcid_override: Option<&[u8; quiche::MAX_CONN_ID_LEN]>,
) -> RetryOutcome {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    let ccid = quiche::ConnectionId::from_ref(&pair::CLIENT_CID);
    let mut client =
        quiche::connect(Some("quic.test"), &ccid, pair::client_addr(), pair::server_addr(), &mut cc)
            .expect("client connect");

    let mut out = [0u8; pair::MAX_DATAGRAM];
    let (len, _) = client.send(&mut out).expect("client's first Initial");
    let mut first = out[..len].to_vec();
    let hdr = quiche::Header::from_slice(&mut first, quiche::MAX_CONN_ID_LEN)
        .expect("parsing the client's first Initial");
    assert_eq!(hdr.ty, quiche::Type::Initial, "the client's first packet is not an Initial");
    let odcid = hdr.dcid.clone().into_owned();
    let client_scid = hdr.scid.clone().into_owned();
    let version = hdr.version;

    let new_scid = quiche::ConnectionId::from_ref(retry_scid);
    let token = b"harbor-verifier-retry-token".to_vec();
    let mut retry = [0u8; pair::MAX_DATAGRAM];
    let rlen = quiche::retry(&client_scid, &odcid, &new_scid, &token, version, &mut retry)
        .expect("quiche::retry");
    client
        .recv(
            &mut retry[..rlen],
            quiche::RecvInfo { from: pair::server_addr(), to: pair::client_addr() },
        )
        .expect("client rejected the Retry packet");

    let (len2, _) = client.send(&mut out).expect("client's second Initial");
    let mut second = out[..len2].to_vec();
    let hdr2 = quiche::Header::from_slice(&mut second, quiche::MAX_CONN_ID_LEN)
        .expect("parsing the client's second Initial");
    let token_len = hdr2.token.as_ref().map(|t| t.len()).unwrap_or(0);
    let second_dcid = hdr2.dcid.to_vec();

    let rsc_owned;
    let rsc_id = match rsc_override {
        Some(b) => {
            rsc_owned = *b;
            quiche::ConnectionId::from_ref(&rsc_owned)
        },
        None => quiche::ConnectionId::from_ref(retry_scid),
    };
    let odc_owned;
    let odc_id = match odcid_override {
        Some(b) => {
            odc_owned = *b;
            quiche::ConnectionId::from_ref(&odc_owned)
        },
        None => odcid.clone(),
    };
    let retry_cids = quiche::RetryConnectionIds {
        original_destination_cid: &odc_id,
        retry_source_cid: &rsc_id,
    };
    let mut server: quiche::Connection = quiche::accept_with_retry(
        &new_scid,
        retry_cids,
        pair::server_addr(),
        pair::client_addr(),
        &mut sc,
    )
    .expect("accept_with_retry");

    let mut buf = out[..len2].to_vec();
    let _ = server.recv(
        &mut buf,
        quiche::RecvInfo { from: pair::client_addr(), to: pair::server_addr() },
    );

    for _ in 0..32 {
        let s = drain(&mut server);
        let c_before = s.len();
        for (d, info) in &s {
            let mut b = d.clone();
            let _ = client
                .recv(&mut b, quiche::RecvInfo { from: info.from, to: info.to });
        }
        let c = drain(&mut client);
        for (d, info) in &c {
            let mut b = d.clone();
            let _ = server
                .recv(&mut b, quiche::RecvInfo { from: info.from, to: info.to });
        }
        if c_before + c.len() == 0 {
            break;
        }
    }

    RetryOutcome {
        token_len,
        second_dcid,
        client_established: client.is_established(),
        server_established: server.is_established(),
        client_error: client.local_error().map(|e| (e.is_app, e.error_code)),
        peer_error: client.peer_error().map(|e| (e.is_app, e.error_code)),
    }
}

/// A lost STOP_SENDING frame is sent again -- unless the stream finished.
///
/// This milestone rewrites the handler that decides what to do with the
/// frames of a packet declared lost. The previous milestone's handler had no
/// arm for STOP_SENDING at all, so an application that stopped reading a
/// stream and had the resulting frame dropped by the network simply never
/// asked again: the peer went on sending and the reader's request was lost
/// with the packet. This one re-queues the frame, and it does so
/// conditionally -- only while the stream is still around and its receive
/// side has not finished, because once the final size is known there is
/// nothing left to stop.
///
/// Both halves of that condition are observed, because only observing the
/// first would let a candidate pass by retransmitting everything
/// unconditionally:
///
///   peer stream still open   the frame comes back
///   peer stream already FIN  it does not
///
/// The observation is on the wire, not in the API. The verifier decrypts the
/// client's own 1-RTT packets with keys derived from the
/// `CLIENT_TRAFFIC_SECRET_0` the client logged, and looks for a STOP_SENDING
/// frame carrying the stream and error code the application asked for. Loss
/// is produced by taking the datagram that carried the frame and not
/// delivering it, then sending three more ack-eliciting packets that do
/// arrive, which is what RFC 9002 6.1.1's packet reordering threshold needs.
/// Each leg asserts that the loss actually happened before asking what came
/// of it, so neither can pass because nothing was ever lost.
#[test]
fn cap_retransmit_stop_sending() {
    let open = stop_sending_run(false);
    assert!(
        open.lost >= 1,
        "the verifier withheld the datagram carrying STOP_SENDING and sent \
         three later ack-eliciting packets, so the connection must have \
         declared that packet lost; it reports {} lost packets. Without a \
         loss this leg would be measuring nothing.\n--- client packets after \
         the drop ---\n{}",
        open.lost,
        open.log
    );
    assert!(
        open.retransmitted,
        "the application called `stream_shutdown(Read)`, the STOP_SENDING \
         that produced was lost, and the peer's stream is still open with no \
         final size known -- so the frame must be sent again. None of the {} \
         packets the client sent afterwards carried one.\n--- client packets \
         after the drop ---\n{}",
        open.packets,
        open.log
    );

    let finished = stop_sending_run(true);
    assert!(
        finished.lost >= 1,
        "the second leg's STOP_SENDING datagram was withheld the same way and \
         must also have been declared lost; the connection reports {} lost \
         packets.\n--- client packets after the drop ---\n{}",
        finished.lost,
        finished.log
    );
    assert!(
        !finished.retransmitted,
        "the peer finished the stream before the loss was detected, so its \
         final size is known and there is nothing left to stop: the lost \
         STOP_SENDING must not be sent again. One of the {} packets the \
         client sent afterwards carried one anyway, which is what \
         retransmitting unconditionally looks like.\n--- client packets after \
         the drop ---\n{}",
        finished.packets,
        finished.log
    );
}

/// What one STOP_SENDING loss produced.
struct StopSendingOutcome {
    /// Whether a STOP_SENDING for the same stream and error code reappeared.
    retransmitted: bool,
    /// Packets the connection has declared lost.
    lost: u64,
    /// How many datagrams the client sent after the drop.
    packets: usize,
    /// Every one of them, rendered for failure messages.
    log: String,
}

/// Drive one STOP_SENDING loss.
///
/// `finish_first` decides whether the peer finishes the stream between the
/// drop and the loss being detected, which is the condition the new handler
/// arm tests.
fn stop_sending_run(finish_first: bool) -> StopSendingOutcome {
    const STREAM_ID: u64 = 0;
    const FILLER_STREAM: u64 = 4;
    const STOP_CODE: u64 = 0x4242;
    // RFC 9002 6.1.1: three packets sent after a packet and acknowledged
    // declare it lost. Six rounds leaves room for the acknowledgement to come
    // back and be processed.
    const ROUNDS: u64 = 6;

    let mut cc = pair::client_config(pair::ALPN_HQ);
    cc.log_keys();
    let mut sc = pair::server_config(pair::ALPN_HQ);
    let (mut p, keylog) = pair::Pair::with_keylog(&mut cc, &mut sc);
    p.handshake();
    assert!(
        p.client.is_established() && p.server.is_established(),
        "handshake did not complete -- no 1-RTT keys to decrypt with"
    );
    let keys = traffic_keys(&keylog.contents(), "CLIENT_TRAFFIC_SECRET_0");
    let dcid_len = pair::SERVER_CID.len();
    let is_stop = |f: &support::Frame| {
        matches!(f, support::Frame::StopSending { stream_id: STREAM_ID, error_code: STOP_CODE })
    };

    // A request, and a response the client will refuse to finish reading.
    p.client.stream_send(STREAM_ID, b"request", true).expect("client request");
    p.settle(16);
    p.server
        .stream_send(STREAM_ID, &testkit::body(600, 0x5709_0014), false)
        .expect("server response body");
    p.settle(16);

    p.client
        .stream_shutdown(STREAM_ID, quiche::Shutdown::Read, STOP_CODE)
        .expect("stream_shutdown(Read)");
    let first = drain(&mut p.client);
    let (hits, first_log) = scan_frames(&first, &keys, dcid_len, is_stop);
    assert_eq!(
        hits.len(),
        1,
        "`stream_shutdown(Read)` must make the connection send exactly one \
         STOP_SENDING for stream {STREAM_ID} with error code {STOP_CODE:#x}; \
         the verifier found {} in the {} datagrams that followed.\n\
         --- client packets ---\n{first_log}",
        hits.len(),
        first.len()
    );
    let kept: Vec<Dgram> = first
        .iter()
        .enumerate()
        .filter(|(i, _)| *i != hits[0])
        .map(|(_, d)| d.clone())
        .collect();
    deliver(&mut p.server, &kept);

    if finish_first {
        p.server.stream_send(STREAM_ID, b"", true).expect("server fin");
        let s = drain(&mut p.server);
        deliver(&mut p.client, &s);
        assert!(
            p.client.stream_finished(STREAM_ID),
            "this leg needs the client to know stream {STREAM_ID}'s final \
             size before the loss is detected, and it does not"
        );
    }

    let mut post: Vec<Dgram> = Vec::new();
    for round in 0..ROUNDS {
        p.client
            .stream_send(FILLER_STREAM, &testkit::body(200, 0x5709_0015 + round), false)
            .expect("filler send");
        let c = drain(&mut p.client);
        deliver(&mut p.server, &c);
        post.extend(c.into_iter());
        let s = drain(&mut p.server);
        deliver(&mut p.client, &s);
    }

    let (post_hits, log) = scan_frames(&post, &keys, dcid_len, is_stop);
    StopSendingOutcome {
        retransmitted: !post_hits.is_empty(),
        lost: p.client.stats().lost as u64,
        packets: post.len(),
        log,
    }
}

/// A lost MAX_STREAMS is put back on the send queue.
///
/// The same lost-frame handler that gained an arm for STOP_SENDING at this
/// milestone gained two more, for MAX_STREAMS_BIDI and MAX_STREAMS_UNI. The
/// previous step's handler had neither: a MAX_STREAMS frame in a packet that
/// was declared lost was simply dropped, and because the sender had already
/// advanced its own record of the limit it had granted, it never sent another
/// one. The peer was left believing in a credit that had in fact been raised,
/// which is a deadlock that only ends when the connection idles out.
///
/// RFC 9000 Section 13.3 puts MAX_STREAMS in the same family as STOP_SENDING:
/// *an updated value is sent in a MAX_STREAMS frame if the packet containing
/// the most recently sent MAX_STREAMS frame for that type is declared lost*.
/// Unlike STOP_SENDING there is no condition on it, so this group is one leg
/// rather than two.
///
/// The observation is the same shape as `cap_retransmit_stop_sending`'s and
/// reuses the same machinery: datagrams are taken out of the send path,
/// decrypted with 1-RTT keys lifted from the connection's own keylog, and
/// parsed into frames, so nothing here reads an internal counter.
///
/// Two details are worth stating because they are what keep the leg honest.
///
///   The frame is produced rather than provoked. The case does not reach for
///   an internal flag; it completes client-initiated bidirectional streams
///   until the server's own bookkeeping decides the client's credit needs
///   raising, and watches for the frame to appear on the wire. The loop has a
///   bound and fails loudly if the frame never shows, so a candidate that
///   never grants more credit fails here rather than silently skipping the
///   interesting part.
///
///   The filler traffic that forces loss detection runs on a *unidirectional*
///   stream. That matters: if it completed more bidirectional streams, the
///   sender would have a fresh reason to send MAX_STREAMS and the leg would
///   pass without the lost-frame handler ever running. The only route by
///   which a second MAX_STREAMS can appear after the drop is retransmission.
#[test]
fn cap_retransmit_max_streams() {
    // RFC 9002 6.1.1: three later packets acknowledged declare a packet lost.
    // Six rounds leaves room for the acknowledgement to come back.
    const ROUNDS: u64 = 6;
    // Comfortably past the half-of-initial threshold at which credit is
    // regranted, with room for a candidate that regrants less eagerly.
    const OPEN_LIMIT: u64 = 160;
    const FILLER_UNI: u64 = 3;

    let mut cc = pair::client_config(pair::ALPN_HQ);
    cc.log_keys();
    let mut sc = pair::server_config(pair::ALPN_HQ);
    let (mut p, keylog) = pair::Pair::with_keylog(&mut cc, &mut sc);
    p.handshake();
    assert!(
        p.client.is_established() && p.server.is_established(),
        "handshake did not complete -- no 1-RTT keys to decrypt with"
    );
    // The client's keylog carries both directions' secrets; these are the
    // server's, because the server is the endpoint granting the credit.
    let keys = traffic_keys(&keylog.contents(), "SERVER_TRAFFIC_SECRET_0");
    let dcid_len = pair::CLIENT_CID.len();
    let is_max_streams =
        |f: &support::Frame| matches!(f, support::Frame::MaxStreams { bidi: true, .. });

    let mut buf = [0u8; 256];
    let mut carrier: Option<(Vec<Dgram>, String)> = None;
    let mut opened = 0u64;
    while opened < OPEN_LIMIT {
        let sid = opened * 4;
        opened += 1;

        p.client.stream_send(sid, b"q", true).expect("client request");
        let c = drain(&mut p.client);
        deliver(&mut p.server, &c);
        while p.server.stream_recv(sid, &mut buf).is_ok() {}
        p.server.stream_send(sid, b"a", true).expect("server response");

        let s = drain(&mut p.server);
        let (hits, log) = scan_frames(&s, &keys, dcid_len, is_max_streams);
        if !hits.is_empty() {
            let kept: Vec<Dgram> = s
                .iter()
                .enumerate()
                .filter(|(i, _)| !hits.contains(i))
                .map(|(_, d)| d.clone())
                .collect();
            deliver(&mut p.client, &kept);
            carrier = Some((kept, log));
            break;
        }
        deliver(&mut p.client, &s);
        while p.client.stream_recv(sid, &mut buf).is_ok() {}
    }
    let (_kept, first_log) = carrier.unwrap_or_else(|| {
        panic!(
            "the client opened and completed {opened} bidirectional streams \
             against an initial limit of 100 and the server never sent a \
             MAX_STREAMS frame for them. A sender that never regrants stream \
             credit cannot be observed to retransmit it, so this leg has \
             nothing to measure."
        )
    });

    let mut post: Vec<Dgram> = Vec::new();
    for round in 0..ROUNDS {
        p.server
            .stream_send(FILLER_UNI, &testkit::body(200, 0x5709_0016 + round), false)
            .expect("filler send on a unidirectional stream");
        let s = drain(&mut p.server);
        deliver(&mut p.client, &s);
        post.extend(s.into_iter());
        let c = drain(&mut p.client);
        deliver(&mut p.server, &c);
    }

    let lost = p.server.stats().lost as u64;
    assert!(
        lost >= 1,
        "the verifier withheld the datagram carrying MAX_STREAMS and the \
         server sent {ROUNDS} further rounds of ack-eliciting packets, so \
         that packet must have been declared lost; the server reports {lost} \
         lost packets. Without a loss this leg would be measuring nothing.\n\
         --- server packets carrying the grant ---\n{first_log}"
    );

    let (post_hits, log) = scan_frames(&post, &keys, dcid_len, is_max_streams);
    assert!(
        !post_hits.is_empty(),
        "the server raised the client's bidirectional stream limit, the \
         packet carrying that MAX_STREAMS was lost, and the client therefore \
         still believes in the old limit -- so the frame must be sent again. \
         None of the {} datagrams the server sent afterwards carried one. \
         Note that the filler traffic runs on a unidirectional stream, so a \
         fresh grant is not an available explanation: retransmission is the \
         only route.\n--- server packets after the drop ---\n{log}",
        post.len()
    );
}

/// The QPACK Huffman coder moved out of `quiche` and into `octets`.
///
/// This is the milestone's inversion. Every release so far kept the HPACK
/// Huffman tables inside `quiche/src/h3/qpack/huffman/`, private to the
/// transport crate. This one deletes that module and puts the coder in
/// `octets`, behind a `huffman_hpack` cargo feature, as three public items:
/// `OctetsMut::put_huffman_encoded`, `Octets::get_huffman_decoded` and the
/// free function `huffman_encoding_len`. `quiche` becomes a consumer of its
/// own dependency.
///
/// `octets` is not re-exported from `quiche`, so the only way to reach the
/// new API is to depend on the member directly; the group builds a crate
/// outside the candidate's workspace that does. That the build succeeds at
/// all is the first observation -- it means the member exists, the feature
/// exists, and the items are public.
///
/// What the probe prints is then compared against a fixed table, line for
/// line. The encodings are RFC 7541 Appendix C's, which is what makes them
/// checkable: `www.example.com`, `no-cache`, `custom-key` and `custom-value`
/// are the worked examples in C.4, and a coder that produces anything else is
/// not the HPACK coder regardless of what it round-trips with itself. The
/// table also pins the parts a re-implementation is most likely to get wrong:
///
///   the `LOWER_CASE` const generic really lowercases -- `WWW.EXAMPLE.COM`
///   encodes to 13 bytes with it off and to the 12 bytes of the lowercase
///   form with it on, which a coder that ignores the parameter cannot do;
///
///   `huffman_encoding_len` refuses rather than answers when the encoding
///   would be longer than the input, which is the signal QPACK uses to send a
///   literal instead, and is why a NUL byte is an error and not a `1`;
///
///   `put_huffman_encoded` reports the short buffer rather than truncating.
#[test]
fn cap_huffman_in_octets() {
    let dir = testkit::scratch("cap-huffman-in-octets");
    let probe = write_probe_crate(
        &dir,
        "huf-probe",
        "octets",
        "features = [\"huffman_hpack\"]",
        HUF_PROBE_MAIN,
        &[],
    );
    let build = build_probe(&dir, &probe, "huf-probe-build");
    assert!(
        build.ok(),
        "expected a crate outside the workspace to be able to depend on the \
         candidate's `octets` with the `huffman_hpack` feature and use \
         `put_huffman_encoded`, `get_huffman_decoded` and \
         `huffman_encoding_len`; the build failed with status {:?}.\n\
         --- cargo stderr ---\n{}",
        build.status,
        build.stderr
    );
    let bin = testkit::candidate_target_dir().join("debug").join("huf-probe");
    assert!(
        bin.is_file(),
        "the probe build reported success but produced no binary at {}",
        bin.display()
    );

    let mut c = testkit::cmd(&bin);
    let run = testkit::run(&mut c, SHORT, &dir, "huf-probe-run");
    assert!(
        run.ok(),
        "the Huffman probe exited {:?} instead of 0.\n--- stdout ---\n{}\n\
         --- stderr ---\n{}",
        run.status,
        run.stdout,
        run.stderr
    );
    compare_table("the octets Huffman coder", HUF_EXPECTED, &run.stdout, &run.stderr);
}

/// What the Huffman probe must print, in this order.
///
/// `enc|<input>|<lower_case>|<hex>`, `len|<input>|<lower_case>|<bytes or
/// err>`, `dec|<hex>|<text>`, `short|<err>`.
const HUF_EXPECTED: &str = "\
enc|www.example.com|false|f1e3c2e5f23a6ba0ab90f4ff
len|www.example.com|false|12
enc|no-cache|false|a8eb10649cbf
len|no-cache|false|6
enc|custom-key|false|25a849e95ba97d7f
len|custom-key|false|8
enc|custom-value|false|25a849e95bb8e8b4bf
len|custom-value|false|9
enc|/index.html|false|60d5485f2bce9a68
len|/index.html|false|8
enc|a|false|1f
len|a|false|1
enc||false|
len||false|0
enc|WWW.EXAMPLE.COM|false|e5cb92f83f21d1af3e05ef6ad1
len|WWW.EXAMPLE.COM|false|13
enc|WWW.EXAMPLE.COM|true|f1e3c2e5f23a6ba0ab90f4ff
len|WWW.EXAMPLE.COM|true|12
dec|f1e3c2e5f23a6ba0ab90f4ff|www.example.com
dec|a8eb10649cbf|no-cache
dec|25a849e95ba97d7f|custom-key
dec|25a849e95bb8e8b4bf|custom-value
dec|60d5485f2bce9a68|/index.html
dec||
roundtrip|www.example.com|false|www.example.com
roundtrip|WWW.EXAMPLE.COM|false|WWW.EXAMPLE.COM
roundtrip|WWW.EXAMPLE.COM|true|www.example.com
len_unprofitable|00|err
len_unprofitable|00010203|err
short_buffer|err
";

const HUF_PROBE_MAIN: &str = r###"
use octets::huffman_encoding_len;
use octets::Octets;
use octets::OctetsMut;

fn hexs(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

fn unhex(h: &str) -> Vec<u8> {
    (0..h.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&h[i..i + 2], 16).expect("hex"))
        .collect()
}

fn enc<const LC: bool>(s: &str) -> Option<Vec<u8>> {
    let mut buf = [0u8; 256];
    let n = {
        let mut o = OctetsMut::with_slice(&mut buf);
        o.put_huffman_encoded::<LC>(s.as_bytes()).ok()?;
        o.off()
    };
    Some(buf[..n].to_vec())
}

fn dec(bytes: &[u8]) -> Option<String> {
    let mut o = Octets::with_slice(bytes);
    let v = o.get_huffman_decoded().ok()?;
    Some(String::from_utf8(v).expect("decoded bytes are not UTF-8"))
}

fn report<const LC: bool>(s: &str) {
    match enc::<LC>(s) {
        Some(v) => println!("enc|{s}|{LC}|{}", hexs(&v)),
        None => println!("enc|{s}|{LC}|err"),
    }
    match huffman_encoding_len::<LC>(s.as_bytes()) {
        Ok(n) => println!("len|{s}|{LC}|{n}"),
        Err(_) => println!("len|{s}|{LC}|err"),
    }
}

fn main() {
    for s in ["www.example.com", "no-cache", "custom-key", "custom-value", "/index.html", "a", ""] {
        report::<false>(s);
    }
    report::<false>("WWW.EXAMPLE.COM");
    report::<true>("WWW.EXAMPLE.COM");

    for h in [
        "f1e3c2e5f23a6ba0ab90f4ff",
        "a8eb10649cbf",
        "25a849e95ba97d7f",
        "25a849e95bb8e8b4bf",
        "60d5485f2bce9a68",
        "",
    ] {
        match dec(&unhex(h)) {
            Some(t) => println!("dec|{h}|{t}"),
            None => println!("dec|{h}|err"),
        }
    }

    for (s, lc) in [("www.example.com", false), ("WWW.EXAMPLE.COM", false)] {
        let _ = lc;
        let e = enc::<false>(s).expect("encode");
        println!("roundtrip|{s}|false|{}", dec(&e).expect("decode"));
    }
    let e = enc::<true>("WWW.EXAMPLE.COM").expect("encode");
    println!("roundtrip|WWW.EXAMPLE.COM|true|{}", dec(&e).expect("decode"));

    for src in [vec![0u8], vec![0u8, 1, 2, 3]] {
        match huffman_encoding_len::<false>(&src) {
            Ok(n) => println!("len_unprofitable|{}|{n}", hexs(&src)),
            Err(_) => println!("len_unprofitable|{}|err", hexs(&src)),
        }
    }

    let mut small = [0u8; 4];
    let mut o = OctetsMut::with_slice(&mut small);
    match o.put_huffman_encoded::<false>(b"www.example.com") {
        Ok(()) => println!("short_buffer|ok"),
        Err(_) => println!("short_buffer|err"),
    }
}
"###;

// ---------------------------------------------------------------------------
// cap-cc-name-collapse  (T3)
// ---------------------------------------------------------------------------

/// `cap-cc-name-collapse`: the congestion-controller enum has contracted, and
/// the two deleted names now resolve onto the gcongestion implementation.
///
/// This release deletes the `BBR` and `BBR2` variants. What makes that
/// observable from outside is not the absence of a Rust identifier -- a case
/// that named `CongestionControlAlgorithm::BBR` would fail to *compile*
/// against this milestone rather than fail an assertion -- but the integer
/// each surviving *name* resolves to. The enum is `#[repr(C)]` and the C API
/// hands these numbers across the boundary as plain values, so the mapping
/// from string to discriminant is the contract a C caller actually sees:
///
///   before   `bbr` -> 2   `bbr2` -> 3   `bbr2_gcongestion` -> 4
///   after    `bbr` -> 4   `bbr2` -> 4   `bbr2_gcongestion` -> 4
///
/// Both names survive and both now select the gcongestion implementation.
/// That is what makes this a discriminator rather than a restatement: the
/// case compiles against the previous milestone's tree and fails there on the
/// third and fourth rows, because a candidate that kept the old variants
/// still answers 2 and 3.
///
/// Read through `from_str` and compared as integers on purpose. Naming the
/// deleted variants would turn one scored assertion into a build failure,
/// which costs the candidate every group in this file instead of this one.
#[test]
fn cap_cc_name_collapse() {
    use quiche::CongestionControlAlgorithm as CCA;
    use std::str::FromStr;

    for (name, want) in
        [("reno", 0i32), ("cubic", 1), ("bbr", 4), ("bbr2", 4), ("bbr2_gcongestion", 4)]
    {
        let got = CCA::from_str(name).unwrap_or_else(|e| {
            panic!(
                "expected {name:?} to name a congestion controller this release still \
                 accepts; observed Err({e:?})"
            )
        });
        assert_eq!(
            got as i32,
            want,
            "expected the name {name:?} to select the controller whose #[repr(C)] \
             discriminant is {want}; observed {}. These integers cross the C boundary as \
             plain values. This milestone deletes the BBR and BBRv2 variants and points \
             both of their names at the gcongestion implementation, so a candidate that \
             still answers 2 for \"bbr\", or 3 for \"bbr2\", is handing C callers a \
             controller this release does not have.",
            got as i32
        );
    }

    for (name, why) in [
        ("BBR", "upper case: the table is matched exactly, not case-folded"),
        ("bbr3", "a controller no release in this package has"),
        ("", "the empty string"),
    ] {
        assert_eq!(
            CCA::from_str(name),
            Err(quiche::Error::CongestionControl),
            "expected {name:?} to be rejected with Error::CongestionControl ({why})"
        );
    }
}

// ---------------------------------------------------------------------------
// probe-crate plumbing, shared by the two groups that need a workspace member
// `quiche` does not re-export
// ---------------------------------------------------------------------------

/// Write a single-binary crate into `dir` that depends on the candidate's
/// `member` by absolute path.
///
/// The crate declares its own empty `[workspace]` table so cargo treats it as
/// a root rather than trying to add it to the candidate's workspace, which
/// would need write access to a manifest the candidate owns.
fn write_probe_crate(
    dir: &Path, name: &str, member: &str, dep_attrs: &str, main_src: &str, extra_deps: &[&str],
) -> PathBuf {
    let probe = dir.join(name);
    std::fs::create_dir_all(probe.join("src")).expect("create probe crate");
    let path = testkit::candidate_root().join(member);
    assert!(
        path.join("Cargo.toml").is_file(),
        "expected the candidate's workspace to contain a `{member}` crate \
         with a manifest at {}",
        path.join("Cargo.toml").display()
    );
    let sep = if dep_attrs.is_empty() { "" } else { ", " };
    let mut manifest = format!(
        "[package]\nname = \"{name}\"\nversion = \"0.0.0\"\nedition = \"2021\"\n\
         publish = false\n\n[workspace]\n\n[dependencies]\n\
         {member} = {{ path = \"{}\"{sep}{dep_attrs} }}\n",
        path.display()
    );
    for d in extra_deps {
        manifest.push_str(d);
        manifest.push('\n');
    }
    std::fs::write(probe.join("Cargo.toml"), manifest).expect("write probe Cargo.toml");
    std::fs::write(probe.join("src/main.rs"), main_src).expect("write probe main.rs");
    probe
}

/// Build a probe crate into the directory the driver already built the
/// candidate's workspace into, so the probe's dependencies are links rather
/// than rebuilds.
fn build_probe(dir: &Path, probe: &Path, tag: &str) -> testkit::Run {
    let mut c = testkit::cmd("cargo");
    c.arg("build")
        .arg("--offline")
        .arg("-j")
        .arg("4")
        .arg("--manifest-path")
        .arg(probe.join("Cargo.toml"))
        .env("CARGO_TARGET_DIR", testkit::candidate_target_dir());
    testkit::run(&mut c, LONG, dir, tag)
}

/// Compare a probe's stdout against a fixed expectation table, and report the
/// first line that differs rather than the whole document.
fn compare_table(what: &str, expected: &str, got: &str, stderr: &str) {
    let want: Vec<&str> = expected.trim_end().lines().collect();
    let have: Vec<&str> = got.trim_end().lines().collect();
    for (i, w) in want.iter().enumerate() {
        match have.get(i) {
            Some(h) if h == w => (),
            Some(h) => panic!(
                "{what}: line {} of the probe's output does not match.\n\
                 expected: {w}\nobserved: {h}\n--- full expected ---\n{expected}\n\
                 --- full observed ---\n{got}\n--- probe stderr ---\n{stderr}",
                i + 1
            ),
            None => panic!(
                "{what}: the probe stopped after {} lines; line {} was \
                 expected to be\n  {w}\n--- full observed ---\n{got}\n\
                 --- probe stderr ---\n{stderr}",
                have.len(),
                i + 1
            ),
        }
    }
    assert_eq!(
        have.len(),
        want.len(),
        "{what}: the probe printed {} lines and exactly {} were expected; the \
         first extra one is\n  {}\n--- full observed ---\n{got}",
        have.len(),
        want.len(),
        have.get(want.len()).copied().unwrap_or("")
    );
}

/// The workspace can read Chrome netlog files.
///
/// `netlog` is a whole new member at this milestone: a deserializer for the
/// line-delimited JSON Chromium writes when a user exports a network log.
/// Nothing in the previous milestone could read one.
///
/// The group feeds the candidate's parser a netlog the verifier wrote and
/// compares what comes back against a fixed table. The fixture is a real
/// netlog in shape -- a constants object with the trailing comma that makes
/// the first line unclosed, an `"events": [` line, one JSON object per event,
/// and a final event that closes the array on its own line -- so a parser
/// that only handles well-formed JSON documents cannot read it at all.
///
/// The event type and phase ids in the fixture are *not* Chrome's. They are
/// numbers the verifier picked, declared in the fixture's own `logEventTypes`
/// and `logEventPhase` maps. That is the point of the format: the ids are
/// file-local and a reader has to resolve them through the table it was
/// given. A candidate with the real Chrome ids compiled in reads this file as
/// a stream of unknown events.
///
/// The table pins four things a partial implementation gets wrong:
///
///   both event families are routed -- `QUIC*` to the quic parser, `HTTP3_`
///   to the h3 parser -- and their parameters are deserialized field by
///   field, not just recognised;
///
///   `QUIC_SESSION` yields an event at `PHASE_BEGIN` and nothing at
///   `PHASE_END`, so the phase has to be resolved and acted on;
///
///   an event type the crate knows about but does not support
///   (`HTTP3_SETTINGS_SENT`) yields nothing rather than an error, and so does
///   one from a family it does not handle at all (`SOCKET_ALIVE`);
///
///   the last event, which shares its line with the array's closing bracket,
///   is still read -- that is the twelfth record, and a reader that stops at
///   eleven fails on the count line.
#[test]
fn cap_netlog() {
    let dir = testkit::scratch("cap-netlog");
    let fixture = dir.join("fixture.netlog");
    std::fs::write(&fixture, NETLOG_FIXTURE).expect("write the netlog fixture");

    let probe = write_probe_crate(
        &dir,
        "nl-probe",
        "netlog",
        "",
        NL_PROBE_MAIN,
        &["serde_json = \"1\""],
    );
    let build = build_probe(&dir, &probe, "nl-probe-build");
    assert!(
        build.ok(),
        "expected a crate outside the workspace to be able to depend on the \
         candidate's `netlog` by path and use `read_netlog_constants`, \
         `read_netlog_record`, `EventHeader::populate_strings` and \
         `parse_event`; the build failed with status {:?}.\n\
         --- cargo stderr ---\n{}",
        build.status,
        build.stderr
    );
    let bin = testkit::candidate_target_dir().join("debug").join("nl-probe");
    assert!(
        bin.is_file(),
        "the probe build reported success but produced no binary at {}",
        bin.display()
    );

    let mut c = testkit::cmd(&bin);
    c.arg(&fixture);
    let run = testkit::run(&mut c, SHORT, &dir, "nl-probe-run");
    assert!(
        run.ok(),
        "the netlog probe exited {:?} instead of 0 reading {}.\n\
         --- stdout ---\n{}\n--- stderr ---\n{}",
        run.status,
        fixture.display(),
        run.stdout,
        run.stderr
    );
    compare_table("the netlog parser", NL_EXPECTED, &run.stdout, &run.stderr);
}

/// What the netlog probe must print for [`NETLOG_FIXTURE`], in this order.
///
/// `<index>|<type>|<phase>|<time>|<source id>|<source type>|<parsed event>`.
const NL_EXPECTED: &str = "\
constants version=1 tick_offset=1700000000000 types=11 phases=3
0|QUIC_SESSION|PHASE_BEGIN|12|41|25|quic:session host=quic.test port=443 cid=5f6e7d8c
1|QUIC_SESSION_TRANSPORT_PARAMETERS_SENT|PHASE_NONE|13|41|25|quic:tp-sent initial_source_connection_id 5f6e7d8c
2|HTTP3_HEADERS_SENT|PHASE_NONE|21|41|25|h3:headers-sent id=0 headers=[:method: GET/:scheme: https/:authority: quic.test/:path: /index.html]
3|HTTP3_PRIORITY_UPDATE_SENT|PHASE_NONE|22|41|25|h3:priority-update elem=0 field=u=3, i ty=request_stream
4|QUIC_SESSION_PACKET_SENT|PHASE_NONE|23|41|25|quic:packet-sent level=ENCRYPTION_FORWARD_SECURE pn=7 sent_us=1234567
5|QUIC_SESSION_STREAM_FRAME_RECEIVED|PHASE_NONE|34|41|25|quic:stream-recv id=0 fin=false off=0 len=1450
6|HTTP3_HEADERS_DECODED|PHASE_NONE|35|41|25|h3:headers-decoded id=0 headers=[:status: 200/content-type: text/plain]
7|HTTP3_DATA_SENT|PHASE_NONE|41|41|25|h3:data-sent id=0 len=4096
8|HTTP3_SETTINGS_SENT|PHASE_NONE|42|41|25|none
9|SOCKET_ALIVE|PHASE_NONE|43|9|8|none
10|QUIC_SESSION|PHASE_END|44|41|25|none
11|QUIC_SESSION_STOP_SENDING_FRAME_SENT|PHASE_NONE|51|41|25|quic:stop-sending id=0 err=3
records=12
";

/// A netlog the verifier wrote, in the shape Chromium writes them.
///
/// Line one is the constants object followed by a bare comma, which is what
/// `read_netlog_constants` closes into an object. Line two opens the events
/// array and carries no event. Each event is one line ending in a comma, and
/// the last one closes the array and the document on its own line.
const NETLOG_FIXTURE: &str = concat!(
r##"{"constants":{"activeFieldTrialGroups":[],"addressFamily":{"ADDRESS_FAMILY_IPV4":1,"ADDRESS_FAMILY_IPV6":2},"certPathBuilderDigestPolicy":{"kAllAlgorithms":0},"certStatusFlag":{"COMMON_NAME_INVALID":1},"certVerifierFlags":{"VERIFY_DISABLE_NETWORK_FETCHES":1},"certificateTrustType":{"DISTRUSTED":1},"clientInfo":{"name":"harbor-verifier","version":"0.0.0"},"dnsQueryType":{"A":1,"AAAA":2},"loadFlag":{"NORMAL":0},"loadState":{"IDLE":0},"logEventPhase":{"PHASE_NONE":0,"PHASE_BEGIN":1,"PHASE_END":2},"logEventTypes":{"QUIC_SESSION":500,"QUIC_SESSION_PACKET_SENT":501,"QUIC_SESSION_STREAM_FRAME_RECEIVED":502,"QUIC_SESSION_STOP_SENDING_FRAME_SENT":503,"QUIC_SESSION_TRANSPORT_PARAMETERS_SENT":504,"HTTP3_HEADERS_SENT":600,"HTTP3_DATA_SENT":601,"HTTP3_HEADERS_DECODED":602,"HTTP3_PRIORITY_UPDATE_SENT":603,"HTTP3_SETTINGS_SENT":604,"SOCKET_ALIVE":700},"logFormatVersion":1,"logSourceType":{"QUIC_SESSION":25,"SOCKET":8},"netError":{"IO_PENDING":-1},"quicError":{"QUIC_NO_ERROR":0},"quicRstStreamError":{"QUIC_STREAM_NO_ERROR":0,"QUIC_STREAM_CANCELLED":3},"secureDnsMode":{"off":0},"timeTickOffset":1700000000000},"##, "\n",
r##""events": ["##, "\n",
r##"{"phase":1,"source":{"id":41,"start_time":"12","type":25},"time":"12","type":500,"params":{"cert_verify_flags":0,"connection_id":"5f6e7d8c","host":"quic.test","port":443,"network_isolation_key":"null","privacy_mode":"disabled"}},"##, "\n",
r##"{"phase":0,"source":{"id":41,"start_time":"12","type":25},"time":"13","type":504,"params":{"quic_transport_parameters":"initial_source_connection_id 5f6e7d8c"}},"##, "\n",
r##"{"phase":0,"source":{"id":41,"start_time":"12","type":25},"time":"21","type":600,"params":{"stream_id":0,"headers":[":method: GET",":scheme: https",":authority: quic.test",":path: /index.html"]}},"##, "\n",
r##"{"phase":0,"source":{"id":41,"start_time":"12","type":25},"time":"22","type":603,"params":{"prioritized_element_id":0,"priority_field_value":"u=3, i","type":"request_stream"}},"##, "\n",
r##"{"phase":0,"source":{"id":41,"start_time":"12","type":25},"time":"23","type":501,"params":{"encryption_level":"ENCRYPTION_FORWARD_SECURE","packet_number":7,"sent_time_us":1234567,"transmission_type":"NOT_RETRANSMISSION"}},"##, "\n",
r##"{"phase":0,"source":{"id":41,"start_time":"12","type":25},"time":"34","type":502,"params":{"stream_id":0,"fin":false,"offset":0,"length":1450}},"##, "\n",
r##"{"phase":0,"source":{"id":41,"start_time":"12","type":25},"time":"35","type":602,"params":{"stream_id":0,"headers":[":status: 200","content-type: text/plain"]}},"##, "\n",
r##"{"phase":0,"source":{"id":41,"start_time":"12","type":25},"time":"41","type":601,"params":{"payload_length":4096,"stream_id":0}},"##, "\n",
r##"{"phase":0,"source":{"id":41,"start_time":"12","type":25},"time":"42","type":604,"params":{}},"##, "\n",
r##"{"phase":0,"source":{"id":9,"start_time":"1","type":8},"time":"43","type":700,"params":{}},"##, "\n",
r##"{"phase":2,"source":{"id":41,"start_time":"12","type":25},"time":"44","type":500,"params":{}},"##, "\n",
r##"{"phase":0,"source":{"id":41,"start_time":"12","type":25},"time":"51","type":503,"params":{"stream_id":0,"quic_rst_stream_error":3}}]}"##, "\n",
);

const NL_PROBE_MAIN: &str = r###"
use std::fs::File;
use std::io::BufReader;

use netlog::h3;
use netlog::quic;
use netlog::read_netlog_constants;
use netlog::read_netlog_record;
use netlog::Event;
use netlog::EventHeader;

fn describe(ev: Option<Event>) -> String {
    match ev {
        None => "none".to_string(),

        Some(Event::Quic(q)) => match q {
            quic::Event::QuicSession(e) => format!(
                "quic:session host={} port={} cid={}",
                e.params.host, e.params.port, e.params.connection_id
            ),
            quic::Event::QuicSessionTransportParametersSent(e) =>
                format!("quic:tp-sent {}", e.params.quic_transport_parameters),
            quic::Event::QuicSessionPacketSent(e) => format!(
                "quic:packet-sent level={} pn={} sent_us={}",
                e.params.encryption_level, e.params.packet_number, e.params.sent_time_us
            ),
            quic::Event::QuicSessionStreamFrameReceived(e) => format!(
                "quic:stream-recv id={} fin={} off={} len={}",
                e.params.stream_id, e.params.fin, e.params.offset, e.params.length
            ),
            quic::Event::QuicSessionStopSendingFrameSent(e) => format!(
                "quic:stop-sending id={} err={}",
                e.params.stream_id, e.params.quic_rst_stream_error
            ),
            other => format!("quic:unexpected {other:?}"),
        },

        Some(Event::H3(h)) => match h {
            h3::Event::Http3HeadersSent(e) => format!(
                "h3:headers-sent id={} headers=[{}]",
                e.params.stream_id,
                e.params.headers.join("/")
            ),
            h3::Event::Http3HeadersDecoded(e) => format!(
                "h3:headers-decoded id={} headers=[{}]",
                e.params.stream_id,
                e.params.headers.join("/")
            ),
            h3::Event::Http3DataSent(e) => format!(
                "h3:data-sent id={} len={}",
                e.params.stream_id, e.params.payload_length
            ),
            h3::Event::Http3PriorityUpdateSent(e) => format!(
                "h3:priority-update elem={} field={} ty={}",
                e.params.prioritized_element_id,
                e.params.priority_field_value,
                match &e.params.ty {
                    Some(h3::PrioritizedElementType::RequestStream) => "request_stream",
                    Some(h3::PrioritizedElementType::PushStream) => "push_stream",
                    None => "absent",
                }
            ),
            other => format!("h3:unexpected {other:?}"),
        },

        Some(Event::H2(_)) => "h2".to_string(),
        Some(Event::Http(_)) => "http".to_string(),
    }
}

fn main() {
    let path = std::env::args().nth(1).expect("usage: nl-probe <netlog>");
    let mut reader = BufReader::new(File::open(&path).expect("open netlog"));

    let constants = read_netlog_constants(&mut reader).expect("netlog constants");
    println!(
        "constants version={} tick_offset={} types={} phases={}",
        constants.log_format_version,
        constants.time_tick_offset,
        constants.log_event_types.len(),
        constants.log_event_phase.len()
    );

    // The second line is `"events": [`, which carries no event.
    read_netlog_record(&mut reader);

    let mut i = 0;
    while let Some(record) = read_netlog_record(&mut reader) {
        let mut hdr: EventHeader =
            serde_json::from_slice(&record).expect("event header");
        hdr.populate_strings(&constants);
        hdr.time_num = hdr.time.parse::<u64>().expect("event time");
        println!(
            "{i}|{}|{}|{}|{}|{}|{}",
            hdr.ty_string,
            hdr.phase_string,
            hdr.time_num,
            hdr.source.id,
            hdr.source.ty,
            describe(netlog::parse_event(&hdr, &record))
        );
        i += 1;
    }
    println!("records={i}");
}
"###;
