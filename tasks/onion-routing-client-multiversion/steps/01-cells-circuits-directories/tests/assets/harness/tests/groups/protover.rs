//! Group `protover`: the subprotocol-version lists that appear on consensus
//! `pr` lines and in router descriptors.

use tor_protover::{ParseError, ProtoKind, Protocols};

fn protos(s: &str) -> Protocols {
    s.parse::<Protocols>()
        .unwrap_or_else(|e| panic!("{:?} should parse: {:?}", s, e))
}

fn err(s: &str) -> ParseError {
    match s.parse::<Protocols>() {
        Ok(p) => panic!("{:?} should not parse, but gave {}", s, p),
        Err(e) => e,
    }
}

#[test]
fn protover__ranges_and_singletons_name_the_supported_versions() {
    let p = protos("Link=1-5 LinkAuth=3 Relay=1-2,9 Cons=2");
    for v in 1..=5 {
        assert!(p.supports_known_subver(ProtoKind::Link, v), "Link={}", v);
    }
    assert!(!p.supports_known_subver(ProtoKind::Link, 0));
    assert!(!p.supports_known_subver(ProtoKind::Link, 6));

    assert!(p.supports_known_subver(ProtoKind::LinkAuth, 3));
    assert!(!p.supports_known_subver(ProtoKind::LinkAuth, 1));

    // A comma-separated list may mix ranges and single versions.
    assert!(p.supports_known_subver(ProtoKind::Relay, 1));
    assert!(p.supports_known_subver(ProtoKind::Relay, 2));
    assert!(!p.supports_known_subver(ProtoKind::Relay, 3));
    assert!(p.supports_known_subver(ProtoKind::Relay, 9));

    // A protocol that is not mentioned at all is unsupported.
    assert!(!p.supports_known_subver(ProtoKind::HSDir, 1));
    assert!(!Protocols::default().supports_known_subver(ProtoKind::Link, 1));

    // The same question, asked by name.
    assert!(p.supports_subver("Cons", 2));
    assert!(!p.supports_subver("Cons", 3));
    assert_eq!(ProtoKind::Link.to_string(), "Link");
    assert_eq!(ProtoKind::DirCache.to_string(), "DirCache");
    assert_eq!(ProtoKind::FlowCtrl.to_string(), "FlowCtrl");
}

#[test]
fn protover__unrecognized_protocol_names_are_kept_verbatim() {
    let p = protos("Foobar=7 Link=1 Wombat=1-3");
    assert!(p.supports_subver("Foobar", 7));
    assert!(!p.supports_subver("Foobar", 6));
    assert!(p.supports_subver("Wombat", 3));
    assert!(!p.supports_subver("Wombat", 4));
    assert!(p.supports_subver("Link", 1));
    // Names are matched exactly, and unknown names are simply unsupported.
    assert!(!p.supports_subver("foobar", 7));
    assert!(!p.supports_subver("Nothing", 1));

    // An entry with an empty version list supports nothing but is not an error.
    let p = protos("Link= Relay=1");
    assert!(!p.supports_known_subver(ProtoKind::Link, 0));
    assert!(p.supports_known_subver(ProtoKind::Relay, 1));
    assert_eq!(p.to_string(), "Relay=1");
}

#[test]
fn protover__display_is_the_canonical_consensus_form() {
    // Entries are sorted by protocol name, recognized or not, and adjacent
    // versions are folded back into ranges.
    assert_eq!(protos("Link=1,2,3 Foobar=7 Relay=2").to_string(), "Foobar=7 Link=1-3 Relay=2");
    assert_eq!(protos("Link=3-5,1").to_string(), "Link=1,3-5");
    assert_eq!(protos("Link=1-63").to_string(), "Link=1-63");
    assert_eq!(protos("").to_string(), "");
    // Parsing is idempotent on the canonical form.
    let canonical = "Cons=2 Desc=2 DirCache=2 Link=1-5 LinkAuth=3 Relay=1-2";
    assert_eq!(protos(canonical).to_string(), canonical);
}

#[test]
fn protover__malformed_or_repeated_entries_are_rejected() {
    // Versions run from 0 through 63 inclusive.
    assert_eq!(err("Link=64"), ParseError::OutOfRange);
    assert_eq!(err("Link=1-64"), ParseError::OutOfRange);
    // No protocol may be named twice, and no version repeated.
    assert_eq!(err("Link=1 Link=2"), ParseError::Duplicate);
    assert_eq!(err("Link=1,1"), ParseError::Duplicate);
    assert_eq!(err("Link=1-3,2"), ParseError::Duplicate);
    assert_eq!(err("Foo=1 Foo=2"), ParseError::Duplicate);
    // Everything else is malformed.
    assert_eq!(err("Link"), ParseError::Malformed);
    assert_eq!(err("Link=1-"), ParseError::Malformed);
    assert_eq!(err("Link=3-1"), ParseError::Malformed);
    assert_eq!(err("Link=x"), ParseError::Malformed);
    assert_eq!(err("Link=-1"), ParseError::Malformed);
    // Leading zeros are not integers in this grammar.
    assert_eq!(err("Link=01"), ParseError::Malformed);
}
