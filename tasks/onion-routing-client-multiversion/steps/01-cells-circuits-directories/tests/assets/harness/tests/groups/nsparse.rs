//! Group `nsparse`: the microdescriptor-flavoured consensus parser.
//!
//! The consensus fixtures are first-party documents written to the grammar in
//! dir-spec (see SPEC_NOTES.md); their signatures are filler, so the
//! signature and timeliness wrappers are discharged rather than checked.

use std::time::{Duration, SystemTime};

use digest::Digest;
use tor_netdoc::doc::netstatus::{MdConsensus, RelayFlags, RelayWeight, RouterStatus};
use tor_netdoc::Error;
use tor_protover::ProtoKind;

use crate::common::{fixture_text, load_consensus};

/// `2021-06-24 00:00:00 UTC` as seconds since the epoch.
const VALID_AFTER: u64 = 1_624_492_800;

fn parse_err(name: &str) -> Error {
    let text = fixture_text(name);
    MdConsensus::parse(&text)
        .map(|_| ())
        .expect_err("this fixture must not parse")
}

#[test]
fn nsparse__consensus_lifetime_comes_from_the_header() {
    let c = load_consensus("consensus-md.txt");
    let va = SystemTime::UNIX_EPOCH + Duration::from_secs(VALID_AFTER);
    assert_eq!(c.lifetime().valid_after(), va);
    assert_eq!(c.lifetime().fresh_until(), va + Duration::from_secs(3600));
    assert_eq!(c.lifetime().valid_until(), va + Duration::from_secs(3 * 3600));
}

#[test]
fn nsparse__routers_appear_in_identity_order_with_their_r_line_fields() {
    let c = load_consensus("consensus-md.txt");
    let rs = c.relays();
    assert_eq!(rs.len(), 4);
    let names: Vec<&str> = rs.iter().map(|r| r.nickname().as_str()).collect();
    assert_eq!(names, vec!["guardy", "exity", "middly", "bothy"]);

    for (i, (nick, port)) in [("guardy", 9001), ("exity", 9002), ("middly", 9003), ("bothy", 9004)]
        .iter()
        .enumerate()
    {
        let r = &rs[i];
        assert_eq!(r.nickname(), nick);
        assert_eq!(r.rsa_identity().as_bytes(), &[0x11 * (i as u8 + 1); 20]);
        assert_eq!(r.addrs().len(), 1);
        assert_eq!(
            r.addrs()[0],
            format!("10.0.0.{}:{}", i + 1, port).parse().unwrap()
        );
        assert_eq!(r.orport_addrs().count(), 1);
        assert_eq!(r.version().as_deref(), Some("Tor 0.4.6.5"));
    }
}

#[test]
fn nsparse__microdescriptor_digests_come_from_the_m_line() {
    let c = load_consensus("consensus-md.txt");
    for r in c.relays() {
        let md = fixture_text(&format!("md-{}.txt", r.nickname()));
        let want = tor_llcrypto::d::Sha256::digest(md.as_bytes());
        assert_eq!(r.md_digest()[..], want[..], "digest for {}", r.nickname());
        assert_eq!(r.doc_digest(), r.md_digest());
    }
}

#[test]
fn nsparse__relay_flags_are_decoded_from_the_s_line() {
    let c = load_consensus("consensus-md.txt");
    let by = |n: &str| {
        c.relays()
            .iter()
            .find(|r| r.nickname() == n)
            .unwrap_or_else(|| panic!("no relay {}", n))
    };

    assert!(by("guardy").is_flagged_guard());
    assert!(!by("guardy").is_flagged_exit());
    assert!(by("exity").is_flagged_exit());
    assert!(!by("exity").is_flagged_guard());
    assert!(!by("middly").is_flagged_guard() && !by("middly").is_flagged_exit());
    assert!(by("bothy").is_flagged_guard() && by("bothy").is_flagged_exit());

    for n in ["guardy", "exity", "middly", "bothy"] {
        assert!(by(n).is_flagged_v2dir());
        assert!(!by(n).is_flagged_bad_exit());
        assert!(by(n).flags().contains(RelayFlags::FAST | RelayFlags::STABLE));
        assert!(by(n).flags().contains(RelayFlags::RUNNING | RelayFlags::VALID));
        assert!(!by(n).flags().contains(RelayFlags::AUTHORITY));
    }
    assert!(by("guardy").flags().contains(RelayFlags::GUARD));
    assert!(by("exity").flags().contains(RelayFlags::EXIT));
}

#[test]
fn nsparse__bandwidth_weights_are_measured_and_taken_from_the_w_line() {
    let c = load_consensus("consensus-md.txt");
    let wanted = [("guardy", 1000), ("exity", 2000), ("middly", 3000), ("bothy", 4000)];
    for (nick, bw) in wanted {
        let r = c.relays().iter().find(|r| r.nickname() == nick).unwrap();
        match r.weight() {
            RelayWeight::Measured(w) => assert_eq!(*w, bw),
            other => panic!("{}: expected a measured weight, got {:?}", nick, other),
        }
        assert!(r.weight().is_measured());
        assert!(r.weight().is_nonzero());
    }
}

#[test]
fn nsparse__footer_weights_and_header_params_are_keyed_lookups() {
    let c = load_consensus("consensus-md.txt");
    assert_eq!(c.bandwidth_weights().get("Wgg"), Some(&10000));
    assert_eq!(c.bandwidth_weights().get("Wgd"), Some(&0));
    assert_eq!(c.bandwidth_weights().get("Wee"), Some(&10000));
    assert_eq!(c.bandwidth_weights().get("Wmg"), Some(&0));
    assert_eq!(c.bandwidth_weights().get("Wxx"), None);

    assert_eq!(c.params().get("circwindow"), Some(&888));
    assert_eq!(c.params().get("bwweightscale"), Some(&10000));
    assert_eq!(c.params().get("min_paths_for_circs_pct"), Some(&60));
    assert_eq!(c.params().get("CircuitPriorityHalflifeMsec"), Some(&30000));
    assert_eq!(c.params().get("no-such-parameter"), None);
}

#[test]
fn nsparse__subprotocol_lines_are_parsed_for_each_relay() {
    let c = load_consensus("consensus-md.txt");
    let r = &c.relays()[0];
    assert!(r.protovers().supports_known_subver(ProtoKind::DirCache, 2));
    assert!(r.protovers().supports_known_subver(ProtoKind::Link, 5));
    assert!(r.protovers().supports_known_subver(ProtoKind::Link, 1));
    assert!(!r.protovers().supports_known_subver(ProtoKind::Link, 6));
    assert!(!r.protovers().supports_known_subver(ProtoKind::HSDir, 2));
    assert!(r.protovers().supports_subver("Microdesc", 2));
    assert!(r.ed25519_id_is_usable());
}

#[test]
fn nsparse__malformed_consensuses_are_rejected() {
    // Each of these documents must be refused.  Which error variant carries
    // the refusal, and what it says, are not part of the contract.
    //
    // An unrecognized `network-status-version`:
    parse_err("consensus-badversion.txt");
    // Routers that are not sorted by identity:
    parse_err("consensus-unsorted.txt");
    // A consensus carrying no signature at all:
    parse_err("consensus-nosig.txt");
}
