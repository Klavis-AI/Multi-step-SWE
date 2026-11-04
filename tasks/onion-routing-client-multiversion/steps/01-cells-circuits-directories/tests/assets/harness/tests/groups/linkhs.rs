//! Group `linkhs`: the v3 link handshake, driven over an in-memory stream.
//!
//! The responder's side of each handshake is a byte script assembled from the
//! cell formats in tor-spec (see SPEC_NOTES.md); no live socket is used.

use futures::executor::block_on;

use tor_cell::chancell::ChanCmd;
use tor_proto::channel::ChannelBuilder;

use crate::common::{
    auth_challenge_cell, certs_cell, fixed_cell, handshake_script, netinfo_cell, var_cell,
    vpadding_cell, MockStream,
};

/// Run a client handshake against `script`; return the outcome and every byte
/// the client wrote.
fn run(script: Vec<u8>) -> (Result<(), tor_proto::Error>, Vec<u8>) {
    let (stream, written) = MockStream::new(script);
    let mut b = ChannelBuilder::new();
    b.set_declared_addr("10.0.0.200:9001".parse().unwrap());
    let outcome = block_on(async { b.launch(stream).connect().await.map(|_| ()) });
    let bytes = written.lock().unwrap().clone();
    (outcome, bytes)
}

/// The VERSIONS cell offered by the responder, followed by nothing else.
fn versions_only(versions: &[u16]) -> Vec<u8> {
    let mut body = Vec::new();
    for v in versions {
        body.extend_from_slice(&v.to_be_bytes());
    }
    let mut cell = 0u16.to_be_bytes().to_vec();
    cell.push(u8::from(ChanCmd::VERSIONS));
    cell.extend_from_slice(&(body.len() as u16).to_be_bytes());
    cell.extend_from_slice(&body);
    cell
}

#[test]
fn linkhs__client_opens_with_a_versions_cell() {
    let (outcome, written) = run(handshake_script(&[3, 4, 5]));
    assert!(outcome.is_ok(), "handshake failed: {:?}", outcome.err());

    // Before a version is negotiated the circuit-id field is two bytes wide.
    assert_eq!(&written[0..2], &[0, 0]);
    assert_eq!(written[2], u8::from(ChanCmd::VERSIONS));
    let len = u16::from_be_bytes([written[3], written[4]]) as usize;
    assert_eq!(written.len(), 5 + len, "only the VERSIONS cell is sent");
    assert!(len >= 2 && len % 2 == 0, "body is a list of u16s, got {}", len);

    let offered: Vec<u16> = written[5..]
        .chunks(2)
        .map(|c| u16::from_be_bytes([c[0], c[1]]))
        .collect();
    assert!(
        offered.iter().any(|v| *v >= 4),
        "a client using 4-byte circuit IDs must offer link protocol 4 or later, got {:?}",
        offered
    );
}

#[test]
fn linkhs__accepts_a_well_formed_responder_handshake() {
    let (outcome, _) = run(handshake_script(&[4]));
    assert!(outcome.is_ok(), "handshake failed: {:?}", outcome.err());
}

#[test]
fn linkhs__ignores_padding_and_unrecognized_cells_during_the_handshake() {
    let mut script = versions_only(&[4]);
    script.extend_from_slice(&vpadding_cell(7));
    script.extend_from_slice(&certs_cell());
    script.extend_from_slice(&fixed_cell(0, u8::from(ChanCmd::PADDING), &[]));
    script.extend_from_slice(&var_cell(0, 250, b"unknown to this version"));
    script.extend_from_slice(&auth_challenge_cell());
    script.extend_from_slice(&netinfo_cell());
    let (outcome, _) = run(script);
    assert!(outcome.is_ok(), "handshake failed: {:?}", outcome.err());
}

#[test]
fn linkhs__rejects_a_peer_that_does_not_negotiate_versions() {
    // Responder never sends VERSIONS.
    let (outcome, written) = run(netinfo_cell());
    // Only the rejection is graded, not which error variant reports it.
    outcome.expect_err("a NETINFO-first peer is not a Tor relay");
    assert_eq!(written[2], u8::from(ChanCmd::VERSIONS), "we still sent ours");

    // Responder offers only pre-v3-handshake link protocols.
    let mut script = versions_only(&[1, 2]);
    script.extend_from_slice(&certs_cell());
    script.extend_from_slice(&netinfo_cell());
    run(script).0.expect_err("no shared link protocol");
}

#[test]
fn linkhs__requires_exactly_one_certs_cell_and_a_netinfo_cell() {
    // No CERTS at all.
    let mut script = versions_only(&[4]);
    script.extend_from_slice(&auth_challenge_cell());
    script.extend_from_slice(&netinfo_cell());
    run(script).0.expect_err("a CERTS cell is mandatory");

    // Two CERTS cells.
    let mut script = versions_only(&[4]);
    script.extend_from_slice(&certs_cell());
    script.extend_from_slice(&certs_cell());
    script.extend_from_slice(&netinfo_cell());
    run(script).0.expect_err("a duplicate CERTS cell is a violation");

    // Two AUTH_CHALLENGE cells.
    let mut script = versions_only(&[4]);
    script.extend_from_slice(&certs_cell());
    script.extend_from_slice(&auth_challenge_cell());
    script.extend_from_slice(&auth_challenge_cell());
    script.extend_from_slice(&netinfo_cell());
    run(script)
        .0
        .expect_err("a duplicate AUTH_CHALLENGE cell is a violation");

    // CERTS but the stream closes before NETINFO.
    let mut script = versions_only(&[4]);
    script.extend_from_slice(&certs_cell());
    run(script).0.expect_err("a NETINFO cell is mandatory");
}
