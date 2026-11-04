//! Shared fixtures and helpers for the step-01 conformance driver.
//!
//! Nothing in here reimplements anything that is scored; it only loads byte
//! and text fixtures, provides a seeded RNG, and provides an in-memory
//! bidirectional stream so that the channel handshake can be driven without
//! a network.
#![allow(dead_code)]

use std::io;
use std::path::PathBuf;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll};

use tor_cell::chancell::{RawCellBody, CELL_DATA_LEN};
use tor_checkable::{ExternallySigned, Timebound};
use tor_netdir::{MdReceiver, NetDir, PartialNetDir};
use tor_netdoc::doc::microdesc::{Microdesc, MicrodescReader};
use tor_netdoc::AllowAnnotations;
use tor_netdoc::doc::netstatus::MdConsensus;

// ------------------------------------------------------------------ fixtures

/// Absolute path of a fixture shipped next to this driver.
pub fn fixture_path(name: &str) -> PathBuf {
    let mut p = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    p.push("fixtures");
    p.push(name);
    p
}

/// Load a byte fixture.
pub fn fixture_bytes(name: &str) -> Vec<u8> {
    let p = fixture_path(name);
    std::fs::read(&p).unwrap_or_else(|e| panic!("cannot read fixture {:?}: {}", p, e))
}

/// Load a text fixture.
pub fn fixture_text(name: &str) -> String {
    let p = fixture_path(name);
    std::fs::read_to_string(&p).unwrap_or_else(|e| panic!("cannot read fixture {:?}: {}", p, e))
}

/// Turn a byte slice of exactly [`CELL_DATA_LEN`] bytes into a cell body.
pub fn raw_body(b: &[u8]) -> RawCellBody {
    assert_eq!(b.len(), CELL_DATA_LEN, "relay cell bodies are 509 bytes");
    let mut out = [0_u8; CELL_DATA_LEN];
    out.copy_from_slice(b);
    out
}

/// The 509-byte relay-cell body carried by a 514-byte fixed-length cell
/// fixture (4-byte circuit id, 1-byte command, then the body).
pub fn body_of_fixed_cell(cell: &[u8]) -> RawCellBody {
    assert_eq!(cell.len(), 514);
    raw_body(&cell[5..])
}

/// A seeded, reproducible CSPRNG: no test may depend on entropy.
pub fn det_rng() -> rand::rngs::StdRng {
    use rand::SeedableRng;
    rand::rngs::StdRng::seed_from_u64(0x5445_5354_5f30_31)
}

// ------------------------------------------------------- in-memory transport

/// A one-shot in-memory duplex: reads are served from a pre-loaded script,
/// writes are appended to a shared buffer.  Reads never pend, so a handshake
/// driven over this stream terminates without any executor scheduling.
pub struct MockStream {
    script: Vec<u8>,
    pos: usize,
    written: Arc<Mutex<Vec<u8>>>,
}

impl MockStream {
    /// Build a stream that will hand `script` to its reader, plus a handle
    /// on everything that gets written to it.
    pub fn new(script: Vec<u8>) -> (Self, Arc<Mutex<Vec<u8>>>) {
        let written = Arc::new(Mutex::new(Vec::new()));
        (
            MockStream {
                script,
                pos: 0,
                written: Arc::clone(&written),
            },
            written,
        )
    }
}

impl futures::io::AsyncRead for MockStream {
    fn poll_read(
        mut self: Pin<&mut Self>,
        _cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<io::Result<usize>> {
        let remaining = self.script.len() - self.pos;
        let n = std::cmp::min(remaining, buf.len());
        let pos = self.pos;
        buf[..n].copy_from_slice(&self.script[pos..pos + n]);
        self.pos += n;
        Poll::Ready(Ok(n))
    }
}

impl futures::io::AsyncWrite for MockStream {
    fn poll_write(
        self: Pin<&mut Self>,
        _cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        self.written.lock().unwrap().extend_from_slice(buf);
        Poll::Ready(Ok(buf.len()))
    }
    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }
    fn poll_close(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }
}

/// Build a variable-length cell as it appears on a link-version-4 channel.
pub fn var_cell(circid: u32, cmd: u8, body: &[u8]) -> Vec<u8> {
    let mut v = Vec::new();
    v.extend_from_slice(&circid.to_be_bytes());
    v.push(cmd);
    v.extend_from_slice(&(body.len() as u16).to_be_bytes());
    v.extend_from_slice(body);
    v
}

/// Build a fixed-length (514-byte) cell as it appears on a link-version-4
/// channel.
pub fn fixed_cell(circid: u32, cmd: u8, body: &[u8]) -> Vec<u8> {
    let mut v = Vec::new();
    v.extend_from_slice(&circid.to_be_bytes());
    v.push(cmd);
    v.extend_from_slice(body);
    v.resize(5 + CELL_DATA_LEN, 0);
    v
}

/// The responder side of a minimal, well-formed v3 link handshake:
/// a VERSIONS cell offering link protocol 4, a CERTS cell, an AUTH_CHALLENGE
/// cell and a NETINFO cell.
pub fn handshake_script(versions: &[u16]) -> Vec<u8> {
    let mut v = Vec::new();
    // VERSIONS, with the 2-byte circuit id used before a version is agreed.
    let mut vbody = Vec::new();
    for ver in versions {
        vbody.extend_from_slice(&ver.to_be_bytes());
    }
    v.extend_from_slice(&0_u16.to_be_bytes());
    v.push(7);
    v.extend_from_slice(&(vbody.len() as u16).to_be_bytes());
    v.extend_from_slice(&vbody);
    // From here on the link protocol is 4, so circuit ids are 4 bytes wide.
    v.extend_from_slice(&certs_cell());
    v.extend_from_slice(&auth_challenge_cell());
    v.extend_from_slice(&netinfo_cell());
    v
}

/// A CERTS cell carrying two opaque certificate bodies.
pub fn certs_cell() -> Vec<u8> {
    let mut body = vec![2_u8];
    body.push(2);
    body.extend_from_slice(&3_u16.to_be_bytes());
    body.extend_from_slice(&[1, 2, 3]);
    body.push(4);
    body.extend_from_slice(&8_u16.to_be_bytes());
    body.extend_from_slice(b"ABCDEFGH");
    var_cell(0, 129, &body)
}

/// An AUTH_CHALLENGE cell: 32-byte challenge, then a method list.
pub fn auth_challenge_cell() -> Vec<u8> {
    let mut body = vec![0x5A_u8; 32];
    body.extend_from_slice(&1_u16.to_be_bytes());
    body.extend_from_slice(&3_u16.to_be_bytes());
    var_cell(0, 130, &body)
}

/// A NETINFO cell from a relay: timestamp, other-address, then our addresses.
pub fn netinfo_cell() -> Vec<u8> {
    let mut body = Vec::new();
    body.extend_from_slice(&0x6000_0000_u32.to_be_bytes());
    // Other address: type 4 (IPv4), length 4.
    body.push(4);
    body.push(4);
    body.extend_from_slice(&[10, 0, 0, 200]);
    // One of my addresses.
    body.push(1);
    body.push(4);
    body.push(4);
    body.extend_from_slice(&[10, 0, 0, 1]);
    fixed_cell(0, 8, &body)
}

/// A VPADDING cell of `n` padding bytes.
pub fn vpadding_cell(n: usize) -> Vec<u8> {
    var_cell(0, 128, &vec![0_u8; n])
}

// -------------------------------------------------------------- netdoc / netdir

/// Parse a consensus fixture into an `MdConsensus`.
///
/// The fixture's signatures are placeholders, so the timeliness and signature
/// wrappers are discharged explicitly rather than checked.
pub fn load_consensus(name: &str) -> MdConsensus {
    let text = fixture_text(name);
    let (_, _, unchecked) = MdConsensus::parse(&text).expect("consensus fixture must parse");
    unchecked
        .dangerously_assume_timely()
        .dangerously_assume_wellsigned()
}

/// Parse every microdescriptor in a fixture.
pub fn load_mds(name: &str) -> Vec<Microdesc> {
    let text = fixture_text(name);
    MicrodescReader::new(&text, AllowAnnotations::AnnotationsNotAllowed)
        .map(|r| r.expect("microdescriptor fixture must parse").into_microdesc())
        .collect()
}

/// A `PartialNetDir` over `consensus_name`, with every microdescriptor in
/// `mds_name` added.
pub fn partial_netdir(consensus_name: &str, mds_name: &str) -> PartialNetDir {
    let consensus = load_consensus(consensus_name);
    let mut partial = PartialNetDir::new(consensus, None);
    for md in load_mds(mds_name) {
        partial.add_microdesc(md);
    }
    partial
}

/// A complete `NetDir` built from the four-relay fixture consensus.
pub fn test_netdir() -> NetDir {
    partial_netdir("consensus-md.txt", "mds-all.txt")
        .unwrap_if_sufficient()
        .unwrap_or_else(|_| panic!("four-relay fixture should have enough paths"))
}
