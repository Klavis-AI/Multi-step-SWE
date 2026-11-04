//! Group `rdparse`: structural checks in the router-descriptor parser.
//!
//! At this checkpoint `RouterDesc` exposes no field accessors, so this group
//! observes the parser's structural requirements rather than parsed values.
//!
//! What is graded here is that each malformed fixture is refused, and that the
//! three structural failures are told apart from one another.  Which `Error`
//! variant carries a refusal, and what it says, are not part of the contract.

use tor_netdoc::doc::routerdesc::RouterDesc;
use tor_netdoc::Error;

use crate::common::fixture_text;

fn parse_err(name: &str) -> Error {
    let text = fixture_text(name);
    RouterDesc::parse(&text)
        .map(|_| ())
        .expect_err("this fixture must not parse")
}

#[test]
fn rdparse__the_identity_certificate_must_follow_the_router_line() {
    let misplaced = parse_err("routerdesc-misplaced-cert.txt");
    assert_ne!(
        misplaced,
        parse_err("routerdesc-shortrouter.txt"),
        "an identity certificate placed before the router line must be \
         refused, and refused differently from a truncated router line"
    );
}

#[test]
fn rdparse__the_router_line_carries_five_arguments() {
    let short = parse_err("routerdesc-shortrouter.txt");
    assert_ne!(
        short,
        parse_err("routerdesc-noedid.txt"),
        "a truncated router line must be refused, and refused differently \
         from a descriptor with no identity certificate at all"
    );
}

#[test]
fn rdparse__an_ed25519_identity_certificate_is_required() {
    let missing = parse_err("routerdesc-noedid.txt");
    assert_ne!(
        missing,
        parse_err("routerdesc-misplaced-cert.txt"),
        "a descriptor without an identity certificate must be refused, and \
         refused differently from one whose certificate is out of place"
    );
}
