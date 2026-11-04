//! Group `netdocs` - the relay documents a consensus refers to.
//!
//! Graded through `tor_netdoc::doc::microdesc::Microdesc` and
//! `tor_netdoc::types::{family::RelayFamily, version::TorVersion}`. A client
//! decides which relays it trusts by reading a microdescriptor per relay and
//! the version strings those relays advertise, so the required keywords, the
//! discarded ones, and version ordering are what matter here.

use std::str::FromStr;

use tor_llcrypto::pk::rsa::RsaIdentity;
use tor_netdoc::doc::microdesc::Microdesc;
use tor_netdoc::types::family::RelayFamily;
use tor_netdoc::types::version::TorVersion;

/// A 1024-bit RSA public key in the form a microdescriptor uses.
const TAP_KEY: &str = "\
MIGJAoGBANjgzo0Xd+LULe6Dms6u9XY1RZlH9JQv1OHqLSDtbDMyhUCRgCJzglOA\
64p/ATy4IrwDOHW53MyGrm9E1UVGUaktFbZ0UGXcWX/Ch+HLSSFFAeVyfJxtWtwv\
ti/oqi3o0y65Ibeh+Dj7bRxGhoC7mfXomf/yMAbgLNI6A/j4NUI3AgMBAAE=";

/// The ntor key every fixture uses.
const NTOR: &str = "QkJCQkJCQkJCQkJCQkJCQkJCQkJCQkJCQkJCQkJCQkI";

/// The ed25519 identity every fixture uses.
const ED: &str = "KioqKioqKioqKioqKioqKioqKioqKioqKioqKioqKio";

/// The TAP key as a PEM-delimited object, which is how a microdescriptor
/// carries the argument-less `onion-key` entry.
fn tap_key_object() -> String {
    let mut s = String::from("-----BEGIN RSA PUBLIC KEY-----\n");
    for line in TAP_KEY.as_bytes().chunks(64) {
        s.push_str(&String::from_utf8_lossy(line));
        s.push('\n');
    }
    s.push_str("-----END RSA PUBLIC KEY-----\n");
    s
}

/// A microdescriptor with `extra` appended after the required entries.
fn microdesc_with(extra: &str) -> String {
    let mut s = String::new();
    s.push_str("onion-key\n");
    s.push_str(&tap_key_object());
    s.push_str(&format!("ntor-onion-key {}\n", NTOR));
    s.push_str(extra);
    s.push_str(&format!("id ed25519 {}\n", ED));
    s
}

/// A minimal well-formed microdescriptor.
fn minimal() -> String {
    microdesc_with("")
}

#[test]
fn netdocs__a_microdescriptor_carries_the_keys_a_circuit_needs() {
    let md = Microdesc::parse(&minimal()).expect("a minimal microdescriptor was refused");

    // The ntor key is read back as a curve25519 public key of the expected
    // value, and the ed25519 identity as a 32-byte key.
    let ntor = hex::encode(md.ntor_key().as_bytes());
    assert_eq!(
        ntor,
        hex::encode([0x42_u8; 32]),
        "the ntor key did not survive parsing"
    );
    assert_eq!(md.ed25519_id().as_bytes(), &[0x2a_u8; 32]);

    // The tap key is present even though no accessor exposes it directly:
    // the descriptor digest covers it, so a different tap key has to give a
    // different digest.
    assert_eq!(md.digest().as_ref().len(), 32);

    // A descriptor with no family and no policies is the empty case: the
    // family lists nobody and both policies reject everything.
    assert_eq!(md.family().members().count(), 0);
    // The "does any port survive" accessor is deliberately NOT called here: it
    // arrives with this step's release (tor-netdoc/src/types/policy/portpolicy.rs:124
    // at v0.0.1) and does not exist at v0.0.0, so a gate case using it would veto
    // every candidate while paying nothing for the method. The rendered policy
    // says the same thing through Display, which is inherited.
    assert_eq!(md.ipv4_policy().to_string(), "reject 1-65535");
    assert_eq!(md.ipv6_policy().to_string(), "reject 1-65535");

    // The annotated reader is deliberately NOT exercised here. This release
    // changes `MicrodescReader::new` to take its `AllowAnnotations` by
    // reference where v0.0.0 took it by value, so a gate case written either
    // way vetoes one side of the transition while paying nothing for it --
    // and the transition is the step's own work, not regression. Neither
    // symbol is named in instruction.md or TEST_INTERFACE.md, so the
    // candidate is never told the reader is graded at all; the case was
    // requiring of the harness something the contract does not contain.
}

#[test]
fn netdocs__the_optional_entries_are_parsed_and_the_unknown_ones_dropped() {
    let text = microdesc_with(
        "p accept 80,443,8000-8100\n\
         p6 reject 1-1023\n\
         family $aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
$bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n\
         x-whatever this line is not a keyword we know\n",
    );
    let md = Microdesc::parse(&text).expect("a microdescriptor with extras was refused");

    assert_eq!(md.ipv4_policy().to_string(), "accept 80,443,8000-8100");
    assert!(md.ipv4_policy().allows_port(443));
    assert!(md.ipv4_policy().allows_port(8050));
    assert!(!md.ipv4_policy().allows_port(81));
    // A reject list is kept as the complementary accept list, so the
    // rendered form of "reject 1-1023" names the ports that remain.
    assert_eq!(md.ipv6_policy().to_string(), "accept 1024-65535");
    assert!(!md.ipv6_policy().allows_port(80));
    assert!(md.ipv6_policy().allows_port(1024));

    let members: Vec<String> = md.family().members().map(|id| id.to_string()).collect();
    assert_eq!(
        members,
        vec!["$aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".to_string(),
             "$bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb".to_string()]
    );

    // Two descriptors that differ only in their keys hash differently: the
    // digest is what a consensus names a microdescriptor by.
    let other = microdesc_with_key("Q0NDQ0NDQ0NDQ0NDQ0NDQ0NDQ0NDQ0NDQ0NDQ0NDQ0M");
    let other = Microdesc::parse(&other).expect("the second descriptor was refused");
    assert_ne!(other.digest(), md.digest(), "two different descriptors hashed the same");
    assert_eq!(other.digest().len(), 32);
}

/// A microdescriptor carrying `ntor` as its ntor key.
fn microdesc_with_key(ntor: &str) -> String {
    let mut s = String::new();
    s.push_str("onion-key\n");
    s.push_str(&tap_key_object());
    s.push_str(&format!("ntor-onion-key {}\n", ntor));
    s.push_str(&format!("id ed25519 {}\n", ED));
    s
}

#[test]
fn netdocs__a_descriptor_missing_a_required_entry_is_refused() {
    // `onion-key` and `ntor-onion-key` are both required, as is the
    // ed25519 identity line. Only accept-versus-reject is graded: which
    // variant of `tor_netdoc::Error` the refusal is, and what it says, are
    // the implementation's to choose (step 01 §`nsparse`).
    let no_ntor = minimal().replace(&format!("ntor-onion-key {}\n", NTOR), "");
    assert!(
        Microdesc::parse(&no_ntor).is_err(),
        "a microdescriptor without an ntor key was accepted"
    );

    let no_id = minimal().replace(&format!("id ed25519 {}\n", ED), "");
    assert!(
        Microdesc::parse(&no_id).is_err(),
        "a microdescriptor without an ed25519 id was accepted"
    );

    // A descriptor whose first keyword is not `onion-key` is refused too.
    let wrong_start = minimal().replace("onion-key\n", "");
    assert!(Microdesc::parse(&wrong_start).is_err());

    // An empty document has no keywords at all.
    assert!(Microdesc::parse("").is_err());
}

#[test]
fn netdocs__a_version_string_orders_by_its_parts() {
    let v = |s: &str| TorVersion::from_str(s).unwrap_or_else(|e| panic!("{:?}: {}", s, e));

    let old = v("0.3.5.8");
    let mid = v("0.4.0.1");
    let new = v("0.4.6.8");

    assert!(old < mid, "0.3.5.8 should sort before 0.4.0.1");
    assert!(mid < new, "0.4.0.1 should sort before 0.4.6.8");
    assert_eq!(v("0.4.6.8"), v("0.4.6.8"));

    // The numeric comparison is on the numbers, not on the text: "0.4.10"
    // comes after "0.4.9", which a string sort would get backwards.
    assert!(v("0.4.9.1") < v("0.4.10.1"));
    assert!(v("0.10.0.1") > v("0.9.0.1"));

    // A release outranks a release candidate of the same numbers.
    let rc = v("0.4.6.8-rc");
    let release = v("0.4.6.8");
    assert!(rc < release, "a release candidate should sort before its release");

    // A development snapshot is still ordered after its predecessors.
    assert!(v("0.4.6.8-rc-dev") < v("0.4.6.8-dev"));
    assert!(v("0.4.6.8-rc-dev") < v("0.4.6.8"));

    // Whatever the text, the numbers it was built from survive.
    assert_eq!(new.to_string(), "0.4.6.8");
    assert_eq!(rc.to_string(), "0.4.6.8-rc");
}

#[test]
fn netdocs__a_family_is_a_list_of_identities_that_discards_nonsense() {
    let good = "$aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    let other = "$bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    let tagged = "$cccccccccccccccccccccccccccccccccccccccc";

    // Nicknames and entries of the wrong length are silently dropped rather
    // than rejecting the whole list, while an identity carrying a `~` or `=`
    // nickname suffix still names its relay.
    let text = format!(
        "someNickname {} {}~atag {}=foo dddd short",
        good, other, tagged
    );
    let f = RelayFamily::from_str(&text).expect("a family line was refused");
    let members: Vec<String> = f.members().map(|i| i.to_string()).collect();
    assert_eq!(
        members,
        vec![good.to_string(), other.to_string(), tagged.to_string()],
        "a family kept the wrong set of entries"
    );

    let ident = |hex: &str| {
        RsaIdentity::from_bytes(&hex::decode(hex).expect("bad fixture"))
            .expect("bad fixture length")
    };
    let a = ident("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    let b = ident("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    let c = ident("cccccccccccccccccccccccccccccccccccccccc");
    assert!(f.contains(&a));
    assert!(f.contains(&b));
    assert!(f.contains(&c), "a `=`-tagged identity was dropped");
    assert!(!f.contains(&ident("dddddddddddddddddddddddddddddddddddddddd")));
    assert_eq!(f.members().count(), 3, "a nickname was kept as a member");

    // An empty family contains nobody and is the same as a fresh one.
    let empty = RelayFamily::from_str("").expect("an empty family was refused");
    assert_eq!(empty.members().count(), 0);
    assert!(!empty.contains(&a));
    assert_eq!(
        RelayFamily::new().members().count(),
        RelayFamily::default().members().count()
    );
}
