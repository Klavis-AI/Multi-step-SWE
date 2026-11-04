//! Shared, first-party support code for the step-03 conformance driver.
//!
//! Nothing in here asserts anything: every helper either builds a byte string
//! that the Tor specifications define, or drives a candidate API to a value a
//! group can assert on. Helpers are deliberately synchronous and
//! executor-free (`futures::executor::block_on`) so that no case depends on a
//! wall clock, a sleep, a timeout, or a task-scheduling order.

#![allow(dead_code)]

use std::pin::Pin;
use std::sync::{Arc, Mutex, Once};
use std::task::{Context, Poll};
use tor_llcrypto::pk::ed25519;

// =======================================================================
// Channel-cell byte construction (tor-spec section 3)
// =======================================================================

/// Number of payload bytes in a fixed-length channel cell (tor-spec s. 3).
pub const CELL_PAYLOAD_LEN: usize = 509;

/// Channel command for a VERSIONS cell (tor-spec s. 3, s. 4.1).
pub const CMD_VERSIONS: u8 = 7;
/// Channel command for a NETINFO cell (tor-spec s. 4.5).
pub const CMD_NETINFO: u8 = 8;
/// Channel command for a CERTS cell (tor-spec s. 4.2).
pub const CMD_CERTS: u8 = 129;
/// Channel command for a VPADDING cell (tor-spec s. 3).
pub const CMD_VPADDING: u8 = 128;
/// Channel command for a PADDING cell (tor-spec s. 3).
pub const CMD_PADDING: u8 = 0;

/// The VERSIONS cell as it appears on the wire *before* a link protocol has
/// been negotiated: a 2-byte CircID, a 1-byte command, a 2-byte length, then
/// that many bytes of u16 versions in network order (tor-spec s. 3, s. 4.1).
///
/// This framing is fixed by the specification, not by any implementation.
pub fn versions_cell(versions: &[u16]) -> Vec<u8> {
    let mut body = Vec::with_capacity(versions.len() * 2);
    for v in versions {
        body.extend_from_slice(&v.to_be_bytes());
    }
    let mut out = vec![0x00, 0x00, CMD_VERSIONS];
    out.extend_from_slice(&(body.len() as u16).to_be_bytes());
    out.extend_from_slice(&body);
    out
}

/// A variable-length cell under link protocol 4 or later: a 4-byte CircID, a
/// 1-byte command, a 2-byte length, then that many payload bytes
/// (tor-spec s. 3).
pub fn var_cell_v4(circid: u32, cmd: u8, body: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(7 + body.len());
    out.extend_from_slice(&circid.to_be_bytes());
    out.push(cmd);
    out.extend_from_slice(&(body.len() as u16).to_be_bytes());
    out.extend_from_slice(body);
    out
}

/// A fixed-length cell under link protocol 4 or later: a 4-byte CircID, a
/// 1-byte command, then exactly 509 payload bytes, zero padded (tor-spec s. 3).
pub fn fixed_cell_v4(circid: u32, cmd: u8, body: &[u8]) -> Vec<u8> {
    assert!(body.len() <= CELL_PAYLOAD_LEN);
    let mut out = Vec::with_capacity(5 + CELL_PAYLOAD_LEN);
    out.extend_from_slice(&circid.to_be_bytes());
    out.push(cmd);
    out.extend_from_slice(body);
    out.resize(5 + CELL_PAYLOAD_LEN, 0);
    out
}

/// A CERTS cell body carrying no certificates: a single N=0 byte
/// (tor-spec s. 4.2).
pub fn certs_body_empty() -> Vec<u8> {
    vec![0x00]
}

/// A minimal NETINFO body (tor-spec s. 4.5): a 4-byte timestamp, then the
/// receiver's address as (type u8, length u8, value), then a 1-byte count of
/// the sender's own addresses. Address type 0x04 is IPv4 with a 4-byte value.
/// The timestamp is written as zero so that no case depends on a clock.
pub fn netinfo_body_minimal() -> Vec<u8> {
    let mut b = Vec::new();
    b.extend_from_slice(&0u32.to_be_bytes()); // TIME
    b.push(0x04); // OTHERADDR type: IPv4
    b.push(0x04); // OTHERADDR length
    b.extend_from_slice(&[0, 0, 0, 0]); // OTHERADDR value
    b.push(0x00); // NMYADDR
    b
}

// =======================================================================
// An in-memory duplex that the tor-proto channel handshake can run over
// =======================================================================

/// A first-party `AsyncRead + AsyncWrite` pair of buffers.
///
/// Reads drain a fixed script of relay bytes and then report end-of-file;
/// writes accumulate into a shared buffer that stays inspectable after
/// `ChannelBuilder::launch` has taken ownership of the stream.
pub struct MsgBuf {
    inbuf: std::io::Cursor<Vec<u8>>,
    outbuf: Arc<Mutex<Vec<u8>>>,
}

impl MsgBuf {
    pub fn new(relay_script: Vec<u8>, outbuf: Arc<Mutex<Vec<u8>>>) -> Self {
        MsgBuf {
            inbuf: std::io::Cursor::new(relay_script),
            outbuf,
        }
    }
}

impl futures::io::AsyncRead for MsgBuf {
    fn poll_read(
        mut self: Pin<&mut Self>,
        _cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<std::io::Result<usize>> {
        use std::io::Read;
        Poll::Ready(self.inbuf.read(buf))
    }
}

impl futures::io::AsyncWrite for MsgBuf {
    fn poll_write(
        self: Pin<&mut Self>,
        _cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<std::io::Result<usize>> {
        self.outbuf
            .lock()
            .expect("output buffer poisoned")
            .extend_from_slice(buf);
        Poll::Ready(Ok(buf.len()))
    }
    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<std::io::Result<()>> {
        Poll::Ready(Ok(()))
    }
    fn poll_close(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<std::io::Result<()>> {
        Poll::Ready(Ok(()))
    }
}

/// Outcome of running the outbound channel handshake against a scripted relay.
pub struct HandshakeOutcome {
    /// `Ok(())` if `connect()` produced an unverified channel; otherwise the
    /// `Display` form of the error it produced.
    pub result: Result<(), String>,
    /// Every byte the client wrote to the relay.
    pub written: Vec<u8>,
}

impl HandshakeOutcome {
    pub fn err(&self) -> &str {
        match &self.result {
            Ok(()) => panic!("handshake unexpectedly succeeded"),
            Err(e) => e.as_str(),
        }
    }
}

/// Run `ChannelBuilder::new().launch(..).connect()` against a fixed script of
/// relay bytes. The script is finite, so the handshake either completes or
/// fails at end-of-file; it can never block.
///
/// `connect()` yields an `UnverifiedChannel`, not a `Channel`: no channel
/// exists until that unverified value has been through `check()` and
/// `finish()`. This helper is for the negotiation errors of TEST_INTERFACE
/// section 7, which are all decided before verification, so it keeps only
/// whether negotiation succeeded and drops the unverified value. Cases that
/// need a live `Channel` call [`live_channel`] instead.
pub fn run_handshake(relay_script: Vec<u8>) -> HandshakeOutcome {
    let outbuf = Arc::new(Mutex::new(Vec::new()));
    let stream = MsgBuf::new(relay_script, Arc::clone(&outbuf));
    let hs = tor_proto::channel::ChannelBuilder::new().launch(stream);
    let result = futures::executor::block_on(hs.connect());
    let written = outbuf.lock().expect("output buffer poisoned").clone();
    HandshakeOutcome {
        result: result.map(|_| ()).map_err(|e| e.to_string()),
        written,
    }
}

// =======================================================================
// A first-party relay certificate chain (cert-spec s. 2.1, 2.2.1 and 2.3)
// =======================================================================
//
// `UnverifiedChannel::check` runs the real Tor link authentication: four
// certificates, two ed25519 signatures batch-verified, one RSA PKCS#1 v1.5
// signature, and a SHA-256 binding to the peer's TLS certificate. Upstream's
// own fixtures cannot be reused through the public API, because the CERT_T5
// certificate pins the *digest* of a TLS certificate whose bytes upstream does
// not publish. So the chain below is generated here instead, and it is a real
// chain: every signature in it verifies.
//
// The ed25519 material is derived in this file from two fixed seeds, so the
// keys and both signatures are reproducible from this source alone. Only the
// RSA half is precomputed, because `tor-llcrypto` exposes RSA verification and
// not RSA signing: `RELAY_RSA_ID_X509` is a self-signed 1024-bit certificate
// made once with `openssl genrsa` and `openssl req -x509`, and
// `RELAY_RSA_CROSSCERT_SIG` is that key's signature over the crosscert digest.
// The private key is not in this tree and is not needed. None of these values
// can rot silently: a wrong byte anywhere makes `check()` fail and the case
// fail with it.
//
// Every field order below is quoted from SPEC_NOTES.md section 9, which cites
// tor-spec s. 4.2 and cert-spec s. 2.1, 2.2.1 and 2.3 line by line.

/// Seed of the fixture relay's ed25519 *identity* key: thirty-two arbitrary
/// ASCII bytes, secret from nobody.
const RELAY_ID_SEED: [u8; 32] = *b"step03 relay identity key seed..";

/// Seed of the fixture relay's ed25519 *signing* key. See `RELAY_ID_SEED`.
const RELAY_SIGNING_SEED: [u8; 32] = *b"step03 relay signing key seed...";

/// The bytes the fixture relay presents as its x.509 TLS certificate.
///
/// `check()` only ever hashes this, so any byte string does; the CERT_T5
/// certificate below certifies exactly its SHA-256.
pub const PEER_TLS_CERT: &[u8] = b"step03 fixture TLS certificate";

/// `sha256(PEER_TLS_CERT)` — precomputed so the driver needs no hash of its own.
const PEER_TLS_CERT_SHA256: &str =
    "80b208544483c76faa077a43cb7a851019728d5e0031e51ac95acfc713c842e5";

/// Expiry of every certificate in the chain, in hours since the Unix epoch
/// (cert-spec s. 2.1). 1_000_000 hours is 2084-01-29.
const CERT_EXPIRY_HOURS: u32 = 1_000_000;

/// The instant the driver claims to be checking the chain at: 2027-01-01.
/// Passed explicitly so no case reads a clock.
///
/// It has to fall inside **every** window in the fixture chain, because
/// §6.2 says certificate expiry is judged against this instant: the
/// cert-spec certificates expire at `CERT_EXPIRY_HOURS` (2084), and the
/// self-signed x.509's Validity runs 2026-09-20 .. 2126-08-27. An instant
/// before the x.509's `notBefore` makes the fixture refuse a chain that a
/// conforming implementation -- one that judges validity at the instant it
/// was handed -- is entitled to accept, which is why this is not 2020.
pub const HANDSHAKE_TIME_SECS: u64 = 1_798_761_600;

/// The relay's RSA identity certificate: a self-signed x.509 v3 certificate
/// carrying a 1024-bit RSA public key (cert-spec CERT_T2).
const RELAY_RSA_ID_X509: &str = concat!(
    "3082020930820172a003020102020101300d06092a864886f70d01010b0500301f311d30",
    "1b06035504030c147374657030332d666978747572652d72656c61793020170d32363039",
    "32303136323134325a180f32313236303832373136323134325a301f311d301b06035504",
    "030c147374657030332d666978747572652d72656c617930819f300d06092a864886f70d",
    "010101050003818d0030818902818100d872f1d10e1dfe9128430c820908fe4c4ebd591d",
    "c69e1ba2f5fc71853d21e9bffdc2b095263a3efffbd44e2977cb22926aa74a014efe370c",
    "5e5e3a0acf1e958295a923aacb48405179135ea9e2e88da182ffec23aaa4450d2f53ae36",
    "1a216ba6dfb9e78893554c1617c465f33f2cc880f073a04cb29afd795a3feff50af137a3",
    "0203010001a3533051301d0603551d0e04160414afc6bdd4f99856331d33e83ca549ad6d",
    "b4403633301f0603551d23041830168014afc6bdd4f99856331d33e83ca549ad6db44036",
    "33300f0603551d130101ff040530030101ff300d06092a864886f70d01010b0500038181",
    "00286d6ecd1ca762c8cf00b2eb6fbee981eaf74bc445c94764621cc0ec41c742ad4cb272",
    "f3baee2a2db4a10caa5537c74fd5a28e208c11bceaadb74981e853aa8780fffc0a84d2c1",
    "8b788e4fd4bc273efe7027ffd2ae715abb582054eccb6072052a3400d053272cefeaae5e",
    "5db0501e16d400e6854eb5211143cfa25d6b8aaccc",
);

/// SHA-1 of the RSA public key above — the relay's legacy RSA identity.
const RELAY_RSA_IDENTITY: &str = "afc6bdd4f99856331d33e83ca549ad6db4403633";

/// The RSA identity key's signature over the RSA->Ed25519 crosscert digest
/// (cert-spec CERT_T7): PKCS#1 v1.5 over the bare 32-byte SHA-256, with no
/// DigestInfo prefix, which is what `tor-llcrypto` verifies.
const RELAY_RSA_CROSSCERT_SIG: &str = concat!(
    "c2a29f179ce1aa44592d058e09ae9fe7c459c27da5c9e7cb65b5a71585df10b9c0acf4f0",
    "4e21dceb8358fee5c381a2832708b5dd6bdd26b8b310eaba461ac09a0ed42e2e955e0556",
    "b31f2cf47db850b956e18a0b9ba097f177d781004eaef038acc55bdfd0ba93a30d323c15",
    "fb65c31b9bb2faa5757582faafdde113722d570b",
);

/// Derive an ed25519 key pair from a 32-byte seed.
fn ed_keypair(seed: &[u8; 32]) -> (ed25519::SecretKey, ed25519::PublicKey) {
    let sk = ed25519::SecretKey::from_bytes(seed).expect("32 bytes is a valid ed25519 seed");
    let pk = ed25519::PublicKey::from(&sk);
    (sk, pk)
}

/// Sign `msg` with the ed25519 key derived from `seed`.
fn ed_sign(seed: &[u8; 32], msg: &[u8]) -> [u8; 64] {
    let (sk, pk) = ed_keypair(seed);
    ed25519::ExpandedSecretKey::from(&sk)
        .sign(msg, &pk)
        .to_bytes()
}

/// A Tor ed25519 certificate (cert-spec s. 2.1): version 1, a certificate
/// type, a u32 expiry in hours, a certified-key type and its 32 bytes, a
/// count of extensions, the extensions, then a 64-byte signature over
/// everything that precedes it.
///
/// `signer_pub`, when given, is written as the SIGNED_WITH_ED25519_KEY
/// extension (type 4), which is how a certificate carries the key that signed
/// it. `signer_seed` is the key the signature is actually made with.
fn ed_cert(
    cert_type: u8,
    key_type: u8,
    certified_key: &[u8; 32],
    signer_pub: Option<&[u8; 32]>,
    signer_seed: &[u8; 32],
) -> Vec<u8> {
    let mut c = vec![0x01, cert_type];
    c.extend_from_slice(&CERT_EXPIRY_HOURS.to_be_bytes());
    c.push(key_type);
    c.extend_from_slice(certified_key);
    match signer_pub {
        Some(pk) => {
            c.push(0x01); // N_EXTENSIONS
            c.extend_from_slice(&32u16.to_be_bytes()); // ExtLength
            c.push(0x04); // ExtType: SIGNED_WITH_ED25519_KEY
            c.push(0x00); // ExtFlags
            c.extend_from_slice(pk);
        }
        None => c.push(0x00),
    }
    let sig = ed_sign(signer_seed, &c);
    c.extend_from_slice(&sig);
    c
}

/// An RSA->Ed25519 crosscert (cert-spec s. 2.3): the 32-byte ed25519 identity
/// key being vouched for, a u32 expiry in hours, then a length-prefixed RSA
/// signature. The signature covers
/// `sha256("Tor TLS RSA/Ed25519 cross-certificate" || the first 36 bytes)`.
fn rsa_crosscert(subject_ed_key: &[u8; 32]) -> Vec<u8> {
    let sig = hex::decode(RELAY_RSA_CROSSCERT_SIG).expect("crosscert signature is hex");
    let mut c = Vec::new();
    c.extend_from_slice(subject_ed_key);
    c.extend_from_slice(&CERT_EXPIRY_HOURS.to_be_bytes());
    c.push(sig.len() as u8);
    c.extend_from_slice(&sig);
    c
}

/// A CERTS cell body (tor-spec s. 4.2): a 1-byte count, then for each
/// certificate a 1-byte type, a 2-byte length and that many bytes.
pub fn certs_body(certs: &[(u8, Vec<u8>)]) -> Vec<u8> {
    let mut b = vec![certs.len() as u8];
    for (cert_type, cert) in certs {
        b.push(*cert_type);
        b.extend_from_slice(&(cert.len() as u16).to_be_bytes());
        b.extend_from_slice(cert);
    }
    b
}

/// The fixture relay's complete CERTS cell body: the four certificate types
/// the link handshake authenticates against (2, 4, 5 and 7).
pub fn relay_certs_body() -> Vec<u8> {
    let (_, id_pub) = ed_keypair(&RELAY_ID_SEED);
    let (_, signing_pub) = ed_keypair(&RELAY_SIGNING_SEED);
    let id_pub: [u8; 32] = *id_pub.as_bytes();
    let signing_pub: [u8; 32] = *signing_pub.as_bytes();

    let mut tls_digest = [0u8; 32];
    tls_digest.copy_from_slice(&hex::decode(PEER_TLS_CERT_SHA256).expect("tls digest is hex"));

    certs_body(&[
        // CERT_T2: x.509 RSA identity, self-signed.
        (0x02, hex::decode(RELAY_RSA_ID_X509).expect("x509 is hex")),
        // CERT_T4: identity key certifies the signing key, and carries itself
        // in the SIGNED_WITH_ED25519_KEY extension.
        (
            0x04,
            ed_cert(0x04, 0x01, &signing_pub, Some(&id_pub), &RELAY_ID_SEED),
        ),
        // CERT_T5: signing key certifies sha256 of the TLS certificate.
        (
            0x05,
            ed_cert(0x05, 0x03, &tls_digest, None, &RELAY_SIGNING_SEED),
        ),
        // CERT_T7: RSA identity vouches for the ed25519 identity.
        (0x07, rsa_crosscert(&id_pub)),
    ])
}

/// The relay the fixture chain authenticates as, expressed as something that
/// implements `tor_linkspec::ChanTarget` — which is all `check()` asks for.
///
/// `arti_client::config::dir::FallbackDir` is used because it is already a
/// graded item of TEST_INTERFACE section 4 and needs no crate the driver does
/// not already depend on.
pub fn relay_chan_target() -> arti_client::config::dir::FallbackDir {
    let (_, id_pub) = ed_keypair(&RELAY_ID_SEED);
    let rsa_id = tor_llcrypto::pk::rsa::RsaIdentity::from_bytes(
        &hex::decode(RELAY_RSA_IDENTITY).expect("rsa identity is hex"),
    )
    .expect("a 20-byte RSA identity");
    arti_client::config::dir::FallbackDir::builder()
        .rsa_identity(rsa_id)
        .ed_identity(ed25519::Ed25519Identity::new(*id_pub.as_bytes()))
        .orport("127.0.0.1:1".parse().expect("a literal socket address"))
        .build()
        .expect("the fixture relay is a complete FallbackDir")
}
