//! Group `netdoc_md` (inherited) — microdescriptor documents.
//!
//! This is step-03 capability the cumulative workspace keeps: a
//! microdescriptor is parsed from its own little document, it carries the
//! ntor onion key, the ed25519 identity, an opt-in family list and the IPv4
//! and IPv6 exit policies, and the digest of its text is what a consensus
//! lists for it.
//!
//! The two literal descriptors here are hand-written from the microdescriptor
//! grammar in dir-spec.txt (cited from step 03's `SPEC_NOTES.md`); nothing is
//! produced by asking the candidate to encode a document and then decoding it
//! again.

// The synthetic directory fixtures. They are included here rather than in
// `tests/common/mod.rs` so that a candidate who has not built
// `tor_netdir::testnet` loses only the groups that need a directory; see
// that file's header.
#[path = "netdir_support.rs"]
mod netdir_support;
use tor_netdir::testnet;
use tor_netdir::{MdReceiver, PartialNetDir};
use tor_netdoc::doc::microdesc::{Microdesc, MicrodescReader};
use tor_netdoc::{AllowAnnotations, Error};

/// A complete microdescriptor: the legacy RSA onion key (parsed and thrown
/// away), the ntor onion key, a two-entry family, and both policies.
const ONE_MD: &str = "\
onion-key
-----BEGIN RSA PUBLIC KEY-----
MIGJAoGBALiyRXkiUmFQAmd1+Sy2ApNiPE4F54H7dKUwRqvCs+PqpqExaM2ybqMd
Zzjkgsp41jjOxk/m5RGyo+wLTo3C1TanWsf3CFfoL836KYiv9IjguIuH8FH/EpX1
ss5ChhkkKNhPtvOMFfVe0iuN0abUdGSmixKUVNW0/LiNzXa9Fa/jAgMBAAE=
-----END RSA PUBLIC KEY-----
ntor-onion-key gkQqYIYcecF1KwS2wVV/oVQAuf6oeTWzswXFkEVs/nE=
family $aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa $bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
p accept 80,443
p6 accept 22-80
id ed25519 k3TwldYWObTQkjmY83o+uG6xcbD5b/Zda8QPc/C5Ftg
";

/// The annotation a stored microdescriptor may be prefixed with. It is not
/// part of the descriptor's own text.
const ANNOTATION: &str = "@last-listed 2020-01-27 18:52:09\n";

/// A second descriptor, with no family line and no exit policy at all.
const BARE_MD: &str = "\
onion-key
-----BEGIN RSA PUBLIC KEY-----
MIGJAoGBAMaCi6KwIpOaPFhyMJsJC+r516p0kZXnEuaJLgSR5uakJDW31qDlwD29
dDLBwT4RmiD+OPNSjBpdEIJDbaDNGdgQLpdLgpl7v5ONoOQ3cRlbySOLmZ/7fYeW
DE+4eDLemIqU2e3lEOnb/bpCdQ309FX5OiKDcNy8tqtWgoOdOE0/AgMBAAE=
-----END RSA PUBLIC KEY-----
ntor-onion-key XoldZTBKOhiUYWZgFD969XV/4IvBgEXHhV7o3ruebEc=
id ed25519 LYX9yI5sG8+0aJf8odum0TVPkyYdaKeeC1vBcN2SMIQ
";

/// `BARE_MD` with its stored-descriptor annotation in front of it.
fn annotated() -> String {
    format!("{}{}", ANNOTATION, BARE_MD)
}

#[test]
fn netdoc_md__a_descriptor_carries_its_keys_and_policies() {
    let md = Microdesc::parse(ONE_MD).expect("hand-written microdescriptor parses");

    // The ntor onion key is a curve25519 public key, base64 in the document.
    assert_eq!(
        hex::encode(md.ntor_key().as_bytes()),
        "82442a60861c79c1752b04b6c1557fa15400b9fea87935b3b305c590456cfe71"
    );

    // The ed25519 identity is the one on the `id ed25519` line.
    assert_eq!(
        hex::encode(md.ed25519_id().as_bytes()),
        "9374f095d61639b4d0923998f37a3eb86eb171b0f96ff65d6bc40f73f0b916d8"
    );

    // `p accept 80,443` — those two, and nothing else.
    assert!(md.ipv4_policy().allows_port(80));
    assert!(md.ipv4_policy().allows_port(443));
    assert!(!md.ipv4_policy().allows_port(22));
    assert!(!md.ipv4_policy().allows_port(65535));

    // `p6 accept 22-80` — a port range.
    assert!(md.ipv6_policy().allows_port(22));
    assert!(md.ipv6_policy().allows_port(51));
    assert!(md.ipv6_policy().allows_port(80));
    assert!(!md.ipv6_policy().allows_port(81));

    // The same parse, run over a descriptor that is checked in as a file
    // instead of written out above, with the two key values it must yield
    // recorded separately in `microdesc_expect.txt`. Taking both the document
    // and the answer from files this test did not generate is what rules out a
    // parser that only ever agrees with the literals next to it.
    const SAMPLE: &str = include_str!("../../fixtures/microdesc_sample.txt");
    const EXPECT: &str = include_str!("../../fixtures/microdesc_expect.txt");
    let recorded = |key: &str| -> &'static str {
        EXPECT
            .lines()
            .filter_map(|line| line.split_once('='))
            .find(|(k, _)| *k == key)
            .map(|(_, v)| v)
            .expect("the expectation file records this key")
    };

    let sample = Microdesc::parse(SAMPLE).expect("the checked-in microdescriptor parses");
    // An `Ed25519Identity` prints itself in the same unpadded base64 the
    // document spells it in, so the parsed identity is compared against the
    // recorded text directly.
    assert_eq!(
        sample.ed25519_id().to_string(),
        recorded("ed25519"),
        "the identity is the one on the document's `id ed25519` line"
    );
    // The ntor key has no such printing, so it is compared as bytes: the
    // recorded base64 is the thirty-two bytes 0x40..=0x4f, twice over.
    assert_eq!(
        hex::encode(sample.ntor_key().as_bytes()),
        "404142434445464748494a4b4c4d4e4f404142434445464748494a4b4c4d4e4f"
    );
    assert!(
        SAMPLE.contains(&format!("ntor-onion-key {}\n", recorded("ntor"))),
        "the recorded ntor key is the one the sample document carries"
    );

    // Its policies are `p accept 80,443,8000-8010` and `p6 accept 443`, which
    // differ from the literal descriptor above in both lists.
    assert!(sample.ipv4_policy().allows_port(8000));
    assert!(sample.ipv4_policy().allows_port(8010));
    assert!(!sample.ipv4_policy().allows_port(8011));
    assert!(sample.ipv6_policy().allows_port(443));
    assert!(!sample.ipv6_policy().allows_port(80));
}

#[test]
fn netdoc_md__the_family_is_the_set_of_listed_identities() {
    let md = Microdesc::parse(ONE_MD).expect("hand-written microdescriptor parses");

    assert_eq!(md.family().members().count(), 2);
    assert!(md.family().contains(&netdir_support::rsa_id(0xaa)));
    assert!(md.family().contains(&netdir_support::rsa_id(0xbb)));
    assert!(!md.family().contains(&netdir_support::rsa_id(0xcc)));

    // A descriptor with no `family` line declares an empty family, not an
    // error.
    let bare = Microdesc::parse(BARE_MD).expect("hand-written microdescriptor parses");
    assert_eq!(bare.family().members().count(), 0);
    // ...and an absent policy line means "reject everything", not "accept".
    assert!(!bare.ipv4_policy().allows_port(80));
    assert!(!bare.ipv6_policy().allows_port(80));
    assert!(!bare.ipv4_policy().allows_port(1));
}

#[test]
fn netdoc_md__the_digest_is_over_the_documented_text_not_the_annotations() {
    // The digest of a microdescriptor is the SHA-256 of its text, running
    // from the `onion-key` line to the end of the last item — annotations
    // are not part of it.
    let plain = Microdesc::parse(ONE_MD).expect("parses");
    let again = Microdesc::parse(ONE_MD).expect("parses");
    assert_eq!(plain.digest(), again.digest());

    // Re-parsing the same text inside a longer document gives the same
    // digest, which is how a consensus can list a digest and later match it.
    let mut both = annotated();
    both.push_str(ONE_MD);
    let docs: Vec<_> =
        MicrodescReader::new(&both, &AllowAnnotations::AnnotationsAllowed).collect();
    assert_eq!(docs.len(), 2, "two microdescriptors in the document");
    let second = docs[1].as_ref().expect("second descriptor parses");
    assert_eq!(second.md().digest(), plain.digest());

    // A different descriptor has a different digest.
    let other = docs[0].as_ref().expect("first descriptor parses");
    assert_ne!(other.md().digest(), plain.digest());
}

#[test]
fn netdoc_md__an_annotation_is_only_read_when_annotations_are_allowed() {
    // With annotations allowed the leading `@last-listed` line is consumed as
    // an annotation, and the descriptor's text starts at `onion-key`.
    let ann = annotated();
    let docs: Vec<_> =
        MicrodescReader::new(&ann, &AllowAnnotations::AnnotationsAllowed).collect();
    assert_eq!(docs.len(), 1);
    let md = docs[0].as_ref().expect("annotated descriptor parses");
    let text = md.within(&ann).expect("text can be recovered");
    assert!(
        text.starts_with("onion-key\n"),
        "descriptor text must start at onion-key, got {:?}",
        &text[..text.len().min(24)]
    );
    assert!(
        !text.contains("@last-listed"),
        "the annotation is not part of the descriptor text"
    );
    // ...and the digest is a digest of exactly that text: re-parsing the
    // recovered slice on its own gives the same value.
    let alone = Microdesc::parse(text).expect("recovered text is a whole descriptor");
    assert_eq!(alone.digest(), md.md().digest());

    // The same text with annotations forbidden is a parse failure: the
    // `@last-listed` line is not a keyword this document may contain.
    let mut strict = MicrodescReader::new(&ann, &AllowAnnotations::AnnotationsNotAllowed);
    let first = strict.next().expect("the iterator yields the failure");
    assert!(first.is_err(), "annotations must not be accepted here");

    // And a descriptor with no annotation parses the same either way.
    let loose: Vec<_> = MicrodescReader::new(ONE_MD, &AllowAnnotations::AnnotationsAllowed)
        .collect();
    let tight: Vec<_> = MicrodescReader::new(ONE_MD, &AllowAnnotations::AnnotationsNotAllowed)
        .collect();
    assert_eq!(loose.len(), 1);
    assert_eq!(tight.len(), 1);
    assert_eq!(
        loose[0].as_ref().expect("parses").md().digest(),
        tight[0].as_ref().expect("parses").md().digest()
    );
}

#[test]
fn netdoc_md__a_malformed_descriptor_is_refused() {
    // `onion-key` is a required item: a document without one is refused, and
    // the error names the item that was missing.
    let without_key = BARE_MD.replacen(
        "onion-key\n-----BEGIN RSA PUBLIC KEY-----\nMIGJAoGBAMaCi6KwIpOaPFhyMJsJC+r516p0kZXnEuaJLgSR5uakJDW31qDlwD29\ndDLBwT4RmiD+OPNSjBpdEIJDbaDNGdgQLpdLgpl7v5ONoOQ3cRlbySOLmZ/7fYeW\nDE+4eDLemIqU2e3lEOnb/bpCdQ309FX5OiKDcNy8tqtWgoOdOE0/AgMBAAE=\n-----END RSA PUBLIC KEY-----\n",
        "",
        1,
    );
    let err = Microdesc::parse(&without_key).expect_err("no onion-key, must be refused");
    assert!(matches!(err, Error::MissingToken("onion-key")), "got {:?}", err);

    // And `onion-key` has to come *first*: present but not leading is a
    // wrong-starting-token error naming the item that did lead.
    let family_line = "family $aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa $bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n";
    let reordered = format!("{}{}", family_line, ONE_MD.replacen(family_line, "", 1));
    let err = Microdesc::parse(&reordered).expect_err("onion-key must come first");
    match err {
        Error::WrongStartingToken(ref kwd, _) => assert_eq!(kwd, "family"),
        other => panic!("got {:?}", other),
    }

    // An unparseable exit policy is refused rather than ignored.
    let bad_policy = ONE_MD.replace("p accept 80,443", "p accept 80-");
    assert!(Microdesc::parse(&bad_policy).is_err());

    // A missing ed25519 identity is refused.
    let no_id = ONE_MD
        .lines()
        .filter(|l| !l.starts_with("id ed25519"))
        .collect::<Vec<_>>()
        .join("\n");
    assert!(Microdesc::parse(&no_id).is_err());

    // Junk after the last item means the document was not exhausted.
    let trailing = format!("{}{}", ONE_MD, "p accept 80\n");
    assert!(Microdesc::parse(&trailing).is_err());
}

#[test]
fn netdoc_md__the_network_directory_answers_from_the_microdescriptor() {
    let (consensus, mds) = testnet::construct_custom_network(|_idx, _nb| {}).expect("test net");
    let mut dir = PartialNetDir::new(consensus, None);
    assert_eq!(dir.missing_microdescs().count(), 40);
    for md in mds {
        assert!(dir.add_microdesc(md), "microdescriptor was wanted");
    }
    assert_eq!(dir.missing_microdescs().count(), 0);
    assert!(dir.have_enough_paths());
    let dir = dir.unwrap_if_sufficient().expect("directory is sufficient");

    // The relay's exit policy, family and identity all come from its
    // microdescriptor, not from the consensus entry.
    let relay = dir.by_id(&netdir_support::ed_id(10)).expect("relay 10 is listed");
    assert_eq!(relay.rsa_id(), &netdir_support::rsa_id(10));
    assert!(relay.policies_allow_some_port());
    assert!(relay.supports_exit_port_ipv4(1));
    assert!(relay.supports_exit_port_ipv4(65535));
    assert!(relay.in_same_family(&dir.by_id(&netdir_support::ed_id(11)).expect("relay 11")));

    // A microdescriptor that no consensus entry asked for is not wanted, and
    // adding one twice is a no-op rather than a duplicate.
    let (consensus, mut spare) = testnet::construct_custom_network(|_idx, _nb| {}).expect("test net");
    let mut dir = PartialNetDir::new(consensus, None);
    let once = spare.pop().expect("a microdescriptor");
    let wanted_before = dir.missing_microdescs().count();
    assert!(dir.add_microdesc(once.clone()), "the first copy was wanted");
    assert_eq!(dir.missing_microdescs().count(), wanted_before - 1);
    assert!(
        !dir.add_microdesc(once),
        "the same descriptor a second time is not wanted again"
    );
    assert_eq!(dir.missing_microdescs().count(), wanted_before - 1);

    // And a directory with no microdescriptors at all cannot answer for any
    // relay, because nothing is usable yet.
    let (consensus, _) = testnet::construct_custom_network(|_idx, _nb| {}).expect("test net");
    let empty = PartialNetDir::new(consensus, None);
    assert_eq!(empty.missing_microdescs().count(), 40);
    assert!(!empty.have_enough_paths());
}
