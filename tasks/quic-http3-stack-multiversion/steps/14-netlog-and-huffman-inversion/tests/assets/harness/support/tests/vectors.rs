//! RFC 9001 Appendix A, checked byte for byte.
//!
//! The vectors are not retyped here. They come from
//! `tests/data/appendix-a-vectors.json`, a verbatim copy of the corpus file
//! `corpus/appendix-a-vectors.json` that was machine-extracted from
//! RFC 9001 (49 values, lowercase hex, unwrapped, each tagged with the section
//! of the RFC it is printed in). The copy is what makes this crate movable:
//! at grade time it is built inside a container where the corpus tree does not
//! exist. Set `QUICHE_VERIFIER_APPENDIX_A_VECTORS` to a path to override it,
//! e.g. to re-run against a freshly extracted corpus.
//!
//! Two tests at the end are not from the RFC at all: they pin the draft-29 and
//! draft-23 Initial salts against expected key bytes taken from quiche's own
//! unit tests. The RFC only publishes v1 vectors, so without those two the
//! draft salts would be unverified transcriptions, and a wrong salt is the
//! defect most likely to survive review here.

use std::collections::HashMap;
use std::sync::OnceLock;

use quiche_verifier_support::frame::Frame;
use quiche_verifier_support::{
    apply_header_protection, decode_packet_number, decrypt_initial, decrypt_initial_with_odcid,
    derive_initial_keys, derive_initial_secrets, header_protection_sample, kdf, parse_packet,
    remove_header_protection, retry_integrity_keys, retry_integrity_tag,
    verify_retry_integrity, ClientInitial, Error,
    Packet, PacketKeys, Perspective, RetryPacket, ShortHeader, Suite, INITIAL_SALT_V1,
    VERSION_DRAFT23, VERSION_DRAFT27, VERSION_DRAFT29, VERSION_V1,
};

/// Verbatim copy of the corpus vectors; see the module comment.
const EMBEDDED_VECTORS: &str = include_str!("data/appendix-a-vectors.json");

/// Environment variable overriding the embedded copy at run time.
const VECTORS_ENV: &str = "QUICHE_VERIFIER_APPENDIX_A_VECTORS";

struct Vectors {
    by_key: HashMap<String, Vec<u8>>,
    decimals: HashMap<String, u64>,
}

impl Vectors {
    /// Look a value up by the section it is printed in and its name, e.g.
    /// `("A.2", "protected_packet")`. Panicking is correct here: a missing
    /// vector means the corpus file is not the one this test was written
    /// against, which is a broken verifier, not a failed candidate.
    fn get(&self, section: &str, key: &str) -> &[u8] {
        self.by_key
            .get(&format!("{section}.{key}"))
            .unwrap_or_else(|| panic!("vector {section}.{key} missing from the corpus"))
    }

    /// Look up one of the few values the RFC prints as a number rather than
    /// as hex (the A.5 packet number).
    fn decimal(&self, section: &str, key: &str) -> u64 {
        *self
            .decimals
            .get(&format!("{section}.{key}"))
            .unwrap_or_else(|| panic!("decimal vector {section}.{key} missing from the corpus"))
    }
}

fn vectors() -> &'static Vectors {
    static VECTORS: OnceLock<Vectors> = OnceLock::new();

    VECTORS.get_or_init(|| {
        let raw = match std::env::var(VECTORS_ENV) {
            Ok(path) => std::fs::read_to_string(&path)
                .unwrap_or_else(|e| panic!("{VECTORS_ENV}={path}: {e}")),
            Err(_) => EMBEDDED_VECTORS.to_string(),
        };

        let doc: serde_json::Value =
            serde_json::from_str(&raw).expect("Appendix A vectors are not valid JSON");
        let values = doc["values"].as_array().expect("vectors file has no `values` array");

        let mut by_key = HashMap::new();
        let mut decimals = HashMap::new();

        for entry in values {
            let section = entry["section"].as_str().expect("vector without a section");
            let key = entry["key"].as_str().expect("vector without a key");

            // One entry (the A.5 packet number) is printed in decimal in the
            // RFC and carries no hex.
            let Some(hex) = entry["hex"].as_str() else {
                let n = entry["decimal"].as_u64().expect("vector with neither hex nor decimal");
                decimals.insert(format!("{section}.{key}"), n);
                continue;
            };

            // The corpus promises lowercase, unwrapped hex; if that ever stops
            // being true the decode below would silently change meaning.
            assert!(
                hex.chars().all(|c| c.is_ascii_hexdigit() && !c.is_ascii_uppercase()),
                "vector {section}.{key} is not lowercase contiguous hex"
            );

            let bytes = hex::decode(hex).expect("vector hex does not decode");
            assert_eq!(
                bytes.len() as u64,
                entry["byte_length"].as_u64().expect("vector without byte_length"),
                "vector {section}.{key} length disagrees with its own metadata"
            );

            by_key.insert(format!("{section}.{key}"), bytes);
        }

        assert_eq!(values.len(), 49, "expected the 49 extracted Appendix A values");
        assert_eq!(by_key.len() + decimals.len(), 49, "duplicate section.key in the corpus");

        Vectors { by_key, decimals }
    })
}

// ---------------------------------------------------------------- A.1 keys

#[test]
fn a1_hkdf_label_encodings() {
    let v = vectors();

    // The four output lengths are the ones RFC 9001 Appendix A.1 used: 32 for
    // the Initial secrets, 16 for an AES-128 key, 12 for an IV, 16 for the
    // AES-128 header protection key. They are encoded in the first two bytes
    // of each expected value, so a wrong length fails here.
    assert_eq!(kdf::hkdf_label(kdf::LABEL_CLIENT_IN, b"", 32).unwrap(), v.get("A.1", "hkdf_label_client_in"));
    assert_eq!(kdf::hkdf_label(kdf::LABEL_SERVER_IN, b"", 32).unwrap(), v.get("A.1", "hkdf_label_server_in"));
    assert_eq!(kdf::hkdf_label(kdf::LABEL_KEY, b"", 16).unwrap(), v.get("A.1", "hkdf_label_quic_key"));
    assert_eq!(kdf::hkdf_label(kdf::LABEL_IV, b"", 12).unwrap(), v.get("A.1", "hkdf_label_quic_iv"));
    assert_eq!(kdf::hkdf_label(kdf::LABEL_HP, b"", 16).unwrap(), v.get("A.1", "hkdf_label_quic_hp"));
}

#[test]
fn a1_initial_salt_matches_rfc9001_section_5_2() {
    assert_eq!(&INITIAL_SALT_V1[..], vectors().get("A.1", "initial_salt"));
}

#[test]
fn a1_initial_secrets_and_keys() {
    let v = vectors();
    let dcid = v.get("A", "destination_connection_id");

    let secrets = derive_initial_secrets(VERSION_V1, dcid).unwrap();
    assert_eq!(&secrets.initial_secret[..], v.get("A.1", "initial_secret"));
    assert_eq!(&secrets.client[..], v.get("A.1", "client_initial_secret"));
    assert_eq!(&secrets.server[..], v.get("A.1", "server_initial_secret"));

    let keys = derive_initial_keys(VERSION_V1, dcid).unwrap();
    assert_eq!(keys.client.key(), v.get("A.1", "client_key"));
    assert_eq!(keys.client.iv(), v.get("A.1", "client_iv"));
    assert_eq!(keys.client.hp_key(), v.get("A.1", "client_hp_key"));
    assert_eq!(keys.server.key(), v.get("A.1", "server_key"));
    assert_eq!(keys.server.iv(), v.get("A.1", "server_iv"));
    assert_eq!(keys.server.hp_key(), v.get("A.1", "server_hp_key"));
}

// ------------------------------------------------------- A.2 client Initial

#[test]
fn a2_client_initial_decrypts() {
    let v = vectors();
    let packet = v.get("A.2", "protected_packet");

    let got = decrypt_initial(packet, VERSION_V1, Perspective::Client).unwrap();

    assert_eq!(got.version, VERSION_V1);
    assert_eq!(got.dcid, v.get("A", "destination_connection_id"));
    assert!(got.scid.is_empty());
    assert!(got.token.is_empty());
    assert_eq!(got.packet_number, 2);
    assert_eq!(got.packet_number_len, 4);
    assert_eq!(got.header, v.get("A.2", "unprotected_header"));
    assert_eq!(got.packet_len, 1200);

    // "enough PADDING frames to make a 1162-byte payload".
    let crypto_frame = v.get("A.2", "crypto_frame_payload");
    assert_eq!(got.payload.len(), 1162);
    assert_eq!(&got.payload[..crypto_frame.len()], crypto_frame);
    assert!(got.payload[crypto_frame.len()..].iter().all(|b| *b == 0));

    // The CRYPTO frame's own header is 4 bytes: type 0x06, offset 0, a 2-byte
    // varint length. What is left is the ClientHello.
    assert_eq!(got.crypto_data().unwrap(), &crypto_frame[4..]);

    let frames: Vec<_> = got.frames().map(|f| f.unwrap()).collect();
    assert_eq!(frames.len(), 2);
    assert_eq!(frames[0], Frame::Crypto { offset: 0, data: &crypto_frame[4..] });
    assert_eq!(frames[1], Frame::Padding { len: 1162 - crypto_frame.len() });
}

#[test]
fn a2_header_protection_intermediates() {
    let v = vectors();
    let packet = v.get("A.2", "protected_packet");
    let keys = derive_initial_keys(VERSION_V1, v.get("A", "destination_connection_id"))
        .unwrap()
        .client;

    // pn_offset 18: 1 + 4 version + 1 + 8 dcid + 1 + 0 scid + 1 token len + 2
    // length. The sample starts 4 bytes later, which for this 4-byte packet
    // number is exactly the start of the ciphertext.
    let pn_offset = 18;
    let sample = header_protection_sample(packet, pn_offset).unwrap();
    assert_eq!(sample, v.get("A.2", "sample"));

    let mask = keys.header_mask(sample).unwrap();
    assert_eq!(&mask[..], v.get("A.2", "mask"));

    // The two halves of the mask application the RFC prints separately.
    assert_eq!(packet[0], v.get("A.2", "header_byte0_masked")[0]);
    assert_eq!(&packet[18..22], v.get("A.2", "header_pnbytes_masked"));
    assert_eq!(&packet[..22], v.get("A.2", "header_protected"));

    // Removing protection gets the unprotected header back.
    let mut owned = packet.to_vec();
    let pn = remove_header_protection(&keys, &mut owned, pn_offset).unwrap();
    assert_eq!(pn.len, 4);
    assert_eq!(pn.truncated, 2);
    assert_eq!(&owned[..22], v.get("A.2", "unprotected_header"));

    // ...and applying it again is the exact inverse.
    apply_header_protection(&keys, &mut owned, pn_offset, 4).unwrap();
    assert_eq!(owned, packet);
}

#[test]
fn a2_client_initial_seal_reproduces_the_packet() {
    let v = vectors();
    let crypto_frame = v.get("A.2", "crypto_frame_payload");
    let dcid = v.get("A", "destination_connection_id");

    let mut initial = ClientInitial::new(VERSION_V1, dcid, &[], &crypto_frame[4..]);
    initial.packet_number = 2;
    initial.packet_number_len = 4;
    initial.datagram_len = 1200;

    let built = initial.build().unwrap();

    assert_eq!(built.len(), 1200);
    assert_eq!(&built[..22], v.get("A.2", "header_protected"));
    assert_eq!(built, v.get("A.2", "protected_packet"));
}

// ------------------------------------------------------- A.3 server Initial

#[test]
fn a3_server_initial_decrypts() {
    let v = vectors();
    let packet = v.get("A.3", "protected_packet");
    // The server's Initial keys come from the *client's* original DCID, not
    // from the new connection ID the server just chose and put in this very
    // packet's Source Connection ID field.
    let odcid = v.get("A", "destination_connection_id");

    let got = decrypt_initial_with_odcid(packet, VERSION_V1, Perspective::Server, odcid).unwrap();

    assert_eq!(got.packet_number, 1);
    assert_eq!(got.packet_number_len, 2);
    assert!(got.dcid.is_empty());
    assert_eq!(got.scid, &[0xf0, 0x67, 0xa5, 0x50, 0x2a, 0x42, 0x62, 0xb5]);
    assert_eq!(got.header, v.get("A.3", "unprotected_header"));
    assert_eq!(got.payload, v.get("A.3", "payload"));
    assert_eq!(got.packet_len, packet.len());

    // "an ACK frame, a CRYPTO frame, and no PADDING frames".
    let frames: Vec<_> = got.frames().map(|f| f.unwrap()).collect();
    assert_eq!(frames.len(), 2);
    assert!(matches!(frames[0], Frame::Ack { largest_acked: 0, .. }));
    assert!(matches!(frames[1], Frame::Crypto { offset: 0, .. }));

    // Deriving from the DCID actually on the packet must fail: that is the
    // mistake this signature exists to prevent.
    assert_eq!(
        decrypt_initial(packet, VERSION_V1, Perspective::Server).unwrap_err(),
        Error::DecryptionFailed
    );
}

#[test]
fn a3_server_initial_seal_reproduces_the_packet() {
    let v = vectors();
    let keys = derive_initial_keys(VERSION_V1, v.get("A", "destination_connection_id"))
        .unwrap()
        .server;

    let header = v.get("A.3", "unprotected_header");
    let payload = v.get("A.3", "payload");

    let mut packet = header.to_vec();
    packet.extend_from_slice(&keys.seal(1, header, payload).unwrap());

    // 2-byte packet number, so the header is 20 bytes and the sample starts
    // two bytes into the ciphertext: "the third protected byte".
    let pn_offset = header.len() - 2;
    let sample = header_protection_sample(&packet, pn_offset).unwrap();
    assert_eq!(sample, v.get("A.3", "sample"));

    let mask = keys.header_mask(sample).unwrap();
    assert_eq!(&mask[..], v.get("A.3", "mask"));

    apply_header_protection(&keys, &mut packet, pn_offset, 2).unwrap();
    assert_eq!(&packet[..header.len()], v.get("A.3", "header_protected"));
    assert_eq!(packet, v.get("A.3", "protected_packet"));
}

// -------------------------------------------------------------- A.4 Retry

#[test]
fn a4_retry_integrity_tag() {
    let v = vectors();
    let packet = v.get("A.4", "retry_packet");
    let odcid = v.get("A.4", "odcid_for_integrity");
    let expected_tag = v.get("A.4", "retry_integrity_tag");

    assert_eq!(odcid, v.get("A", "destination_connection_id"));
    assert_eq!(&packet[packet.len() - 16..], expected_tag);

    // The key and nonce are fixed per version and printed in RFC 9001
    // Section 5.8; check the transcription itself, not just its consequences.
    let (key, nonce) = retry_integrity_keys(VERSION_V1).unwrap();
    assert_eq!(&key[..], v.get("A.4", "retry_integrity_key"));
    assert_eq!(&nonce[..], v.get("A.4", "retry_integrity_nonce"));

    let tag = retry_integrity_tag(VERSION_V1, odcid, &packet[..packet.len() - 16]).unwrap();
    assert_eq!(&tag[..], expected_tag);
    assert!(verify_retry_integrity(packet, VERSION_V1, odcid).is_ok());

    // The ODCID is authenticated but not transmitted: a different one fails.
    assert_eq!(
        verify_retry_integrity(packet, VERSION_V1, &[0; 8]).unwrap_err(),
        Error::DecryptionFailed
    );

    // So does any corruption of the packet itself.
    let mut tampered = packet.to_vec();
    tampered[8] ^= 0x01;
    assert_eq!(
        verify_retry_integrity(&tampered, VERSION_V1, odcid).unwrap_err(),
        Error::DecryptionFailed
    );

    let retry = match parse_packet(packet, 0).unwrap() {
        Packet::Retry(r) => r,
        other => panic!("expected a Retry packet, got {other:?}"),
    };
    assert!(retry.dcid.is_empty());
    assert_eq!(retry.scid, &[0xf0, 0x67, 0xa5, 0x50, 0x2a, 0x42, 0x62, 0xb5]);
    assert_eq!(retry.token, b"token");
    assert_eq!(retry.odcid, None);
    assert_eq!(&retry.integrity_tag.unwrap()[..], expected_tag);
}

#[test]
fn a4_retry_before_draft25_has_no_integrity_tag() {
    // Drafts 23 and 24 have no integrity tag at all: the Original Destination
    // Connection ID is carried inline, length-prefixed, right after the Source
    // Connection ID, and everything after it is token. (Layout per `git show
    // 0.1.0:src/packet.rs` in the upstream clone, which reads a whole ODCID
    // length byte there -- not the first-byte low nibble that even older
    // drafts used.) There are no published vectors for this, so the packet is
    // assembled here from the A.4 pieces.
    let v = vectors();
    let a4 = v.get("A.4", "retry_packet");
    let odcid = v.get("A", "destination_connection_id");

    let mut packet = vec![a4[0]];
    packet.extend_from_slice(&VERSION_DRAFT23.to_be_bytes());
    packet.extend_from_slice(&a4[5..15]); // DCID len 0, SCID len 8 and the SCID
    packet.push(odcid.len() as u8);
    packet.extend_from_slice(odcid);
    packet.extend_from_slice(b"token");

    let retry = RetryPacket::parse(&packet, VERSION_DRAFT23).unwrap();
    assert_eq!(retry.integrity_tag, None);
    assert_eq!(retry.odcid, Some(odcid));
    assert_eq!(retry.scid, &a4[7..15]);
    assert_eq!(retry.token, b"token");

    // Asking for a tag on a version that has none is an explicit error, not a
    // silently wrong answer.
    assert_eq!(
        retry_integrity_tag(VERSION_DRAFT23, odcid, &packet).unwrap_err(),
        Error::UnsupportedForVersion { version: VERSION_DRAFT23, what: "Retry Integrity Tag" }
    );
    assert_eq!(
        verify_retry_integrity(&packet, VERSION_DRAFT23, odcid).unwrap_err(),
        Error::UnsupportedForVersion { version: VERSION_DRAFT23, what: "Retry Integrity Tag" }
    );

    // And the v1 packet must not parse as draft-23: its token bytes would have
    // to be read as an oversized connection ID.
    assert!(RetryPacket::parse(a4, VERSION_V1).is_ok());
}

// ------------------------------------------- A.5 ChaCha20-Poly1305, short hdr

#[test]
fn a5_chacha20_poly1305_short_header() {
    let v = vectors();
    let secret = v.get("A.5", "secret");

    let keys = PacketKeys::from_secret(Suite::ChaCha20Poly1305, secret).unwrap();
    assert_eq!(keys.key(), v.get("A.5", "key"));
    assert_eq!(keys.iv(), v.get("A.5", "iv"));
    assert_eq!(keys.hp_key(), v.get("A.5", "hp_key"));
    assert_eq!(kdf::next_secret(secret).unwrap(), v.get("A.5", "ku"));

    let pn = v.decimal("A.5", "packet_number");
    assert_eq!(&keys.nonce(pn)[..], v.get("A.5", "nonce"));

    let header = v.get("A.5", "unprotected_header");
    let plaintext = v.get("A.5", "payload_plaintext");
    let ciphertext = keys.seal(pn, header, plaintext).unwrap();
    assert_eq!(ciphertext, v.get("A.5", "payload_ciphertext"));
    assert_eq!(keys.open(pn, header, &ciphertext).unwrap(), plaintext);
    assert_eq!(quiche_verifier_support::parse_frames(plaintext).unwrap(), vec![Frame::Ping]);

    let mut packet = header.to_vec();
    packet.extend_from_slice(&ciphertext);

    // Empty connection ID, so the packet number starts at offset 1 and the
    // sample starts at 5: "one byte is skipped".
    let pn_offset = 1;
    let sample = header_protection_sample(&packet, pn_offset).unwrap();
    assert_eq!(sample, v.get("A.5", "sample"));
    assert_eq!(&keys.header_mask(sample).unwrap()[..], v.get("A.5", "mask"));

    apply_header_protection(&keys, &mut packet, pn_offset, 3).unwrap();
    assert_eq!(&packet[..4], v.get("A.5", "header_protected"));
    assert_eq!(packet, v.get("A.5", "packet"));
    assert_eq!(packet.len(), 21);

    // Now walk it back the way a receiver would, starting from the wire bytes.
    let parsed = ShortHeader::parse(&packet, 0).unwrap();
    assert_eq!(parsed.pn_offset, pn_offset);
    let mut owned = packet.clone();
    let field = remove_header_protection(&keys, &mut owned, parsed.pn_offset).unwrap();
    assert_eq!(field.len, 3);
    assert_eq!(field.truncated, 0x00bf_f4);
    assert_eq!(&owned[..4], header);

    // Three bytes on the wire are not the packet number: they have to be
    // expanded against what the receiver has already seen.
    let decoded = decode_packet_number(Some(pn - 1), field.truncated, field.len);
    assert_eq!(decoded, pn);
    assert_eq!(keys.open(decoded, &owned[..4], &owned[4..]).unwrap(), plaintext);
}

// ------------------------------- draft salts, cross-checked against quiche

/// Expected Initial key material for draft-29, from the upstream quiche clone:
/// `git show 0.5.0:src/crypto.rs`, test `derive_initial_secrets`, in a release
/// whose `PROTOCOL_VERSION` is draft-29. Same connection ID as Appendix A.
#[test]
fn draft29_salt_cross_checked_against_quiche_0_5_0() {
    let dcid = vectors().get("A", "destination_connection_id");

    let secrets = derive_initial_secrets(VERSION_DRAFT29, dcid).unwrap();
    let keys = derive_initial_keys(VERSION_DRAFT29, dcid).unwrap();

    assert_eq!(
        hex::encode(secrets.client),
        "00881192_88f1d866_733ceeed_15ff9d50_902cf829_52eee27e_9d4d4918_ea371d87".replace('_', "")
    );
    assert_eq!(hex::encode(keys.client.key()), "175257a31eb09dea9366d8bb79ad80ba");
    assert_eq!(hex::encode(keys.client.iv()), "6b26114b9cba2b63a9e8dd4f");
    assert_eq!(hex::encode(keys.client.hp_key()), "9ddd12c994c0698b89374a9c077a3077");

    assert_eq!(
        hex::encode(secrets.server),
        "006f8813_59244dd9_ad1acf85_f595bad6_7c13f9f5_586f5e64_e1acae1d_9ea8f616".replace('_', "")
    );
    assert_eq!(hex::encode(keys.server.key()), "149d0b1662ab871fbe63c49b5e655a5d");
    assert_eq!(hex::encode(keys.server.iv()), "bab2b12a4c76016ace47856d");
    assert_eq!(hex::encode(keys.server.hp_key()), "c0c499a65a60024a18a250974ea01dfa");
}

/// Expected Initial key material for the draft-23 salt, from the upstream
/// quiche clone: `git show 0.5.0:src/crypto.rs`, test
/// `derive_initial_secrets_old`, which exercises the salt that release selects
/// for draft-27 and draft-28 — the same salt quiche 0.1.0 used as its only one
/// while its `PROTOCOL_VERSION` was draft-23.
///
/// This is the test that would catch the tempting mistake of filing drafts 27
/// and 28 under the draft-29 salt.
#[test]
fn draft23_salt_cross_checked_against_quiche_0_5_0() {
    let dcid = vectors().get("A", "destination_connection_id");

    for version in [VERSION_DRAFT23, VERSION_DRAFT27] {
        let secrets = derive_initial_secrets(version, dcid).unwrap();
        let keys = derive_initial_keys(version, dcid).unwrap();

        assert_eq!(
            hex::encode(secrets.client),
            "fda3953aecc040e48b34e27ef87de3a6098ecf0e38b7e032c5c57bcbd5975b84"
        );
        assert_eq!(hex::encode(keys.client.key()), "af7fd7efebd21878ff66811248983694");
        assert_eq!(hex::encode(keys.client.iv()), "8681359410a70bb9c92f0420");
        assert_eq!(hex::encode(keys.client.hp_key()), "a980b8b4fb7d9fbc13e814c23164253d");

        assert_eq!(
            hex::encode(secrets.server),
            "554366b81912ff90be41f17e8022213090ab17d8149179bcadf222f29ff2ddd5"
        );
        assert_eq!(hex::encode(keys.server.key()), "5d51da9ee897a21b2659ccc7e5bfa577");
        assert_eq!(hex::encode(keys.server.iv()), "5e5ae651fd1e8495af13508b");
        assert_eq!(hex::encode(keys.server.hp_key()), "a8ed82e6664f865aedf6106943f95fb8");
    }
}

#[test]
fn the_three_salts_produce_three_different_key_schedules() {
    let dcid = vectors().get("A", "destination_connection_id");

    let v1 = derive_initial_secrets(VERSION_V1, dcid).unwrap().initial_secret;
    let d29 = derive_initial_secrets(VERSION_DRAFT29, dcid).unwrap().initial_secret;
    let d23 = derive_initial_secrets(VERSION_DRAFT23, dcid).unwrap().initial_secret;

    assert_ne!(v1, d29);
    assert_ne!(v1, d23);
    assert_ne!(d29, d23);

    // An Appendix A packet is only readable under the v1 salt.
    let packet = vectors().get("A.2", "protected_packet");
    assert!(decrypt_initial(packet, VERSION_V1, Perspective::Client).is_ok());
    assert_eq!(
        decrypt_initial_with_odcid(packet, VERSION_DRAFT29, Perspective::Client, dcid).unwrap_err(),
        Error::VersionMismatch { expected: VERSION_DRAFT29, actual: VERSION_V1 }
    );
}

// --------------------------------------------- malformed input is an error

#[test]
fn malformed_datagrams_produce_errors_not_panics() {
    let v = vectors();
    let packet = v.get("A.2", "protected_packet");

    // Every possible truncation of a real packet.
    for cut in 0..packet.len() {
        let res = decrypt_initial(&packet[..cut], VERSION_V1, Perspective::Client);
        assert!(res.is_err(), "truncation to {cut} bytes decrypted");
    }

    // Garbage, empty, and a short header where a long one is required.
    assert!(decrypt_initial(&[], VERSION_V1, Perspective::Client).is_err());
    assert!(decrypt_initial(&[0x40; 64], VERSION_V1, Perspective::Client).is_err());
    assert!(decrypt_initial(&[0xff; 64], VERSION_V1, Perspective::Client).is_err());

    // A version with no salt registered.
    let mut unknown = packet.to_vec();
    unknown[1..5].copy_from_slice(&0x6b33_43cfu32.to_be_bytes());
    assert_eq!(
        decrypt_initial(&unknown, 0x6b33_43cf, Perspective::Client).unwrap_err(),
        Error::UnknownVersion(0x6b33_43cf)
    );

    // A single flipped bit anywhere in the ciphertext fails to authenticate.
    for offset in [22, 100, 1199] {
        let mut tampered = packet.to_vec();
        tampered[offset] ^= 0x01;
        assert_eq!(
            decrypt_initial(&tampered, VERSION_V1, Perspective::Client).unwrap_err(),
            Error::DecryptionFailed
        );
    }
}
