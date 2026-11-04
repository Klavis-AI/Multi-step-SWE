//! Group `socksprefs`: how the proxy front end turns an inbound SOCKS request
//! into stream preferences.  The address family the client asked for has to be
//! honoured exactly, because a SOCKS4 client cannot represent an IPv6 answer.
//!
//! `StreamPrefs` keeps its fields private, so each case compares the rendering
//! of the value under test with the rendering of a `StreamPrefs` the case
//! builds itself; no case depends on the literal text of the format.
//!
//! Graded item: `arti::socks::stream_preference`.

use arti::socks::stream_preference;
use arti_client::StreamPrefs;

use crate::common::{socks4a_request, socks5_request};

/// Rendering of a fresh `StreamPrefs` with `f` applied.
fn want(f: impl FnOnce(&mut StreamPrefs)) -> String {
    let mut prefs = StreamPrefs::new();
    f(&mut prefs);
    format!("{:?}", prefs)
}

/// Rendering of the preferences chosen for `addr` by a SOCKS5 request.
fn got5(addr: &str) -> String {
    let req = socks5_request(addr, 80);
    format!("{:?}", stream_preference(&req, addr))
}

/// Rendering of the preferences chosen for `addr` by a SOCKS4a request.
fn got4a(addr: &str) -> String {
    let req = socks4a_request(addr, 80);
    format!("{:?}", stream_preference(&req, addr))
}

#[test]
fn socksprefs__an_ipv4_literal_forces_ipv4_only() {
    let expected = want(|p| {
        p.ipv4_only();
    });
    for addr in ["127.0.0.1", "1.2.3.4", "0.0.0.0", "255.255.255.255"] {
        assert_eq!(got5(addr), expected, "{} should force IPv4-only", addr);
    }
    // "IPv4 only" is a stricter request than the default preference.
    assert_ne!(
        expected,
        want(|p| {
            p.ipv4_preferred();
        })
    );
}

#[test]
fn socksprefs__an_ipv6_literal_forces_ipv6_only() {
    let expected = want(|p| {
        p.ipv6_only();
    });
    for addr in ["::1", "2001:db8::1", "::", "fe80::1"] {
        assert_eq!(got5(addr), expected, "{} should force IPv6-only", addr);
    }
    assert_ne!(
        expected,
        want(|p| {
            p.ipv6_preferred();
        })
    );
    assert_ne!(
        expected,
        want(|p| {
            p.ipv4_only();
        })
    );
}

#[test]
fn socksprefs__a_socks4a_hostname_request_forces_ipv4_only() {
    // SOCKS4 has no way to report an IPv6 address, so a hostname lookup for a
    // version-4 client must be restricted to IPv4 even though the client did
    // not say so.
    let expected = want(|p| {
        p.ipv4_only();
    });
    assert_eq!(got4a("example.com"), expected);
    assert_eq!(got4a("www.torproject.org"), expected);
    // And it is *not* merely the default preference.
    assert_ne!(
        got4a("example.com"),
        want(|p| {
            p.ipv4_preferred();
        })
    );
}

#[test]
fn socksprefs__a_socks5_hostname_request_prefers_ipv4() {
    let expected = want(|p| {
        p.ipv4_preferred();
    });
    for addr in ["example.com", "localhost", "a-b.example", "1.2.3.4.5"] {
        assert_eq!(got5(addr), expected, "{} should be IPv4-preferred", addr);
    }
    // IPv4-preferred is the default for a SOCKS5 hostname request.
    assert_eq!(expected, format!("{:?}", StreamPrefs::new()));
}

#[test]
fn socksprefs__the_address_argument_selects_the_family_not_the_request_kind() {
    // A SOCKS5 client may send a literal address inside a hostname field; the
    // decision is made from the address text that was supplied.
    let req = socks5_request("example.com", 443);
    assert_eq!(
        format!("{:?}", stream_preference(&req, "192.0.2.7")),
        want(|p| {
            p.ipv4_only();
        })
    );
    assert_eq!(
        format!("{:?}", stream_preference(&req, "2001:db8::7")),
        want(|p| {
            p.ipv6_only();
        })
    );
    assert_eq!(
        format!("{:?}", stream_preference(&req, "example.com")),
        want(|p| {
            p.ipv4_preferred();
        })
    );

    // For a version-4 request, an IPv6 literal still wins: the family check
    // happens before the version check.
    let req4 = socks4a_request("example.com", 443);
    assert_eq!(
        format!("{:?}", stream_preference(&req4, "2001:db8::7")),
        want(|p| {
            p.ipv6_only();
        })
    );
}

#[test]
fn socksprefs__the_choice_is_a_pure_function_of_request_and_address() {
    // The same inputs always give the same preferences, and the port plays no
    // part in the decision.
    let a = socks5_request("example.com", 80);
    let b = socks5_request("example.com", 9050);
    assert_eq!(
        format!("{:?}", stream_preference(&a, "example.com")),
        format!("{:?}", stream_preference(&b, "example.com"))
    );
    assert_eq!(
        format!("{:?}", stream_preference(&a, "::1")),
        format!("{:?}", stream_preference(&b, "::1"))
    );
    // Repeating the call does not allocate a new isolation group or otherwise
    // perturb the result.
    let first = format!("{:?}", stream_preference(&a, "example.com"));
    let second = format!("{:?}", stream_preference(&a, "example.com"));
    assert_eq!(first, second);
    // Different addresses still give different answers, so the function is not
    // constant.
    assert_ne!(first, format!("{:?}", stream_preference(&a, "127.0.0.1")));
}
