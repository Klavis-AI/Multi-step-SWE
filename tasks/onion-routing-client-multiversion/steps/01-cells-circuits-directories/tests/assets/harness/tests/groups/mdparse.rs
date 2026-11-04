//! Group `mdparse`: the microdescriptor parser.

use digest::Digest;
use tor_netdoc::doc::microdesc::{Microdesc, MicrodescReader};
use tor_netdoc::{AllowAnnotations, Error};

use crate::common::{fixture_text, load_mds};

/// The ed25519 identities carried by the four microdescriptor fixtures, in
/// the order `guardy, exity, middly, bothy`.
const ED_IDS: [[u8; 32]; 4] = [
    hex_lit(b"b8cbb71e2029416fa96e7ea8f0ac8c6da8395220ceecf84bc19276f1cf698d42"),
    hex_lit(b"25f42aae4210dfd71d9a3606cb1ab1dc1fd9d8721ab313a0919affc9250cc2fa"),
    hex_lit(b"28a925c03a9cb8b18cd7a8658312657696772f6af4ee1531ce3b8ecb89047587"),
    hex_lit(b"a115f007e1578f20f6e12060a0b10f8c1823919b528e46bbb6eccfd2fb6e5083"),
];

/// Decode a 64-character hex literal at compile time.
const fn hex_lit(s: &[u8; 64]) -> [u8; 32] {
    const fn nyb(c: u8) -> u8 {
        match c {
            b'0'..=b'9' => c - b'0',
            b'a'..=b'f' => c - b'a' + 10,
            _ => panic!("bad hex digit"),
        }
    }
    let mut out = [0u8; 32];
    let mut i = 0;
    while i < 32 {
        out[i] = nyb(s[2 * i]) << 4 | nyb(s[2 * i + 1]);
        i += 1;
    }
    out
}

fn md(name: &str) -> Microdesc {
    Microdesc::parse(&fixture_text(name)).expect("fixture must parse")
}

fn parse_err(name: &str) -> Error {
    Microdesc::parse(&fixture_text(name))
        .map(|_| ())
        .expect_err("this fixture must not parse")
}

#[test]
fn mdparse__fixture_parses_into_its_ntor_and_ed25519_keys() {
    let m = md("md-exity.txt");
    // `ntor-onion-key` is a base64 curve25519 public key.
    let ntor: Vec<u8> = (0..32u8).map(|j| (2 * 41 + u32::from(j) * 3) as u8).collect();
    assert_eq!(&m.ntor_key().as_bytes()[..], &ntor[..]);
    assert_eq!(m.ed25519_id().as_bytes(), &ED_IDS[1]);
}

#[test]
fn mdparse__digest_is_the_sha256_of_the_document_text() {
    for name in ["md-guardy.txt", "md-exity.txt", "md-middly.txt", "md-bothy.txt"] {
        let text = fixture_text(name);
        let want = tor_llcrypto::d::Sha256::digest(text.as_bytes());
        assert_eq!(&md(name).digest()[..], &want[..], "digest of {}", name);
    }
}

#[test]
fn mdparse__exit_policy_comes_from_the_p_line() {
    let exity = md("md-exity.txt");
    assert!(exity.ipv4_policy().allows_port(80));
    assert!(exity.ipv4_policy().allows_port(443));
    assert!(!exity.ipv4_policy().allows_port(22));
    assert!(!exity.ipv4_policy().allows_port(0));

    let bothy = md("md-bothy.txt");
    for p in [22u16, 80, 443] {
        assert!(bothy.ipv4_policy().allows_port(p), "port {}", p);
    }
    assert!(!bothy.ipv4_policy().allows_port(9001));
}

#[test]
fn mdparse__reject_all_policy_ipv6_default_and_family_entries() {
    let guardy = md("md-guardy.txt");
    for p in [1u16, 80, 443, 9001, 65535] {
        assert!(!guardy.ipv4_policy().allows_port(p), "port {}", p);
    }
    // No `p6` line in the fixture: the IPv6 policy defaults to reject-all.
    for p in [80u16, 443] {
        assert!(!guardy.ipv6_policy().allows_port(p), "ipv6 port {}", p);
        assert!(!md("md-exity.txt").ipv6_policy().allows_port(p));
    }
    // The `family` line lists one identity plus a bare nickname; a nickname
    // carries no identity, so only the one hex entry survives.
    let fam: Vec<Vec<u8>> = guardy
        .family()
        .members()
        .map(|id| id.as_bytes().to_vec())
        .collect();
    assert_eq!(fam, vec![vec![0xAAu8; 20]]);
    let member = tor_llcrypto::pk::rsa::RsaIdentity::from_bytes(&[0xAA; 20]).unwrap();
    assert!(guardy.family().contains(&member));
    let nonmember = tor_llcrypto::pk::rsa::RsaIdentity::from_bytes(&[0x11; 20]).unwrap();
    assert!(!guardy.family().contains(&nonmember));
}

#[test]
fn mdparse__reader_yields_each_microdescriptor_of_a_bundle_in_order() {
    let all = load_mds("mds-all.txt");
    assert_eq!(all.len(), 4);
    for (i, name) in ["md-guardy.txt", "md-exity.txt", "md-middly.txt", "md-bothy.txt"]
        .iter()
        .enumerate()
    {
        assert_eq!(all[i].digest(), md(name).digest(), "entry {}", i);
        assert_eq!(all[i].ed25519_id().as_bytes(), &ED_IDS[i]);
    }

    let partial = load_mds("mds-partial.txt");
    assert_eq!(partial.len(), 3);
    let got: Vec<Vec<u8>> = partial
        .iter()
        .map(|m| m.ed25519_id().as_bytes().to_vec())
        .collect();
    let want: Vec<Vec<u8>> = vec![ED_IDS[0].to_vec(), ED_IDS[1].to_vec(), ED_IDS[3].to_vec()];
    assert_eq!(got, want);

    // Annotations are rejected unless the caller asks for them.
    let text = fixture_text("mds-all.txt");
    let n = MicrodescReader::new(&text, AllowAnnotations::AnnotationsAllowed)
        .filter(|r| r.is_ok())
        .count();
    assert_eq!(n, 4);
}

#[test]
fn mdparse__malformed_microdescriptors_are_rejected() {
    // Each of these documents must be refused.  Which error variant carries
    // the refusal, and what it says, are not part of the contract.
    //
    // The TAP onion key must be exactly 1024 bits.
    parse_err("md-bad-keylen.txt");
    // An `id` line for some other key type does not supply the ed25519 id.
    parse_err("md-no-ed-id.txt");
}
