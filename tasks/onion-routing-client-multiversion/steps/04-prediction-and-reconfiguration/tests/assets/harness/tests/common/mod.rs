//! Shared support for the step-04 conformance driver.
//!
//! Everything in here is first-party: byte layouts are built from the
//! normative facts recorded in `SPEC_NOTES.md`, never by asking the candidate
//! to encode something and then decoding it again.
//!
//! This module is compiled into every generated test target, including the
//! per-group attribution targets, so it deliberately uses only API that is
//! stable across the whole checkpoint ladder — and, just as deliberately, it
//! names no workspace type at all. A compile failure here would zero all
//! twenty-three groups at once, which is precisely what the per-group
//! attribution pass exists to prevent, so the directory fixtures that do need
//! `tor_netdir::testnet` live in `groups/netdir_support.rs` and are included
//! only by the eleven groups that build a directory.

#![allow(dead_code)]
#![allow(unused_imports)]

/// Payload length of a fixed-size Tor cell, in bytes.
///
/// See the tor-spec.txt "Cell Packet format" citation in `SPEC_NOTES.md`,
/// under "Streams on a circuit".
pub const CELL_BODY_LEN: usize = 509;

/// RELAY cell header length: command, "recognized", stream id, digest, length.
pub const RELAY_HEADER_LEN: usize = 11;

/// Assemble the body of a RELAY cell from its individual fields.
///
/// Field widths and order are the ones stated in `SPEC_NOTES.md` under
/// "Streams on a circuit", and repeated in `TEST_INTERFACE.md` section 10.2:
/// one byte of relay command, two bytes of "recognized", two bytes of stream
/// id, four bytes of digest, two bytes of length, then `data`, then zero
/// padding out to the fixed cell payload length.
pub fn relay_body(
    cmd: u8,
    recognized: u16,
    stream_id: u16,
    digest: [u8; 4],
    data: &[u8],
) -> [u8; 509] {
    assert!(data.len() + RELAY_HEADER_LEN <= CELL_BODY_LEN);
    let mut body = [0_u8; CELL_BODY_LEN];
    body[0] = cmd;
    body[1..3].copy_from_slice(&recognized.to_be_bytes());
    body[3..5].copy_from_slice(&stream_id.to_be_bytes());
    body[5..9].copy_from_slice(&digest);
    let len: u16 = data.len() as u16;
    body[9..11].copy_from_slice(&len.to_be_bytes());
    body[RELAY_HEADER_LEN..RELAY_HEADER_LEN + data.len()].copy_from_slice(data);
    body
}

/// Reinterpret a fixture of exactly `CELL_BODY_LEN` bytes as a cell body.
pub fn body_from_fixture(bytes: &[u8]) -> [u8; 509] {
    assert_eq!(bytes.len(), CELL_BODY_LEN, "fixture is not one cell payload");
    let mut body = [0_u8; CELL_BODY_LEN];
    body.copy_from_slice(bytes);
    body
}
