//! Group `consdiff`: the consensus-diff format (an ed-style line diff,
//! applied from the end of the document backwards).

use digest::Digest;
use tor_consdiff::{apply_diff, looks_like_diff};
use tor_llcrypto::d::Sha3_256;

/// The document every diff in this group is applied to.
const INPUT: &str = "\
network-status-version 3 microdesc
line two
line three
line four
line five
";

/// The SHA3-256 digest of a document, as the `hash` line spells it.
fn digest(s: &str) -> [u8; 32] {
    let mut out = [0u8; 32];
    out.copy_from_slice(&Sha3_256::digest(s.as_bytes())[..]);
    out
}

/// Build a diff whose `hash` line names the real digests of `pre` and `post`.
fn diff(pre: &str, post: &str, body: &str) -> String {
    format!(
        "network-status-diff-version 1\nhash {} {}\n{}",
        hex::encode_upper(digest(pre)),
        hex::encode_upper(digest(post)),
        body
    )
}

/// Apply `body` to `INPUT` and return the resulting document.
fn applied(post: &str, body: &str) -> String {
    let d = diff(INPUT, post, body);
    let out = apply_diff(INPUT, &d, None).expect("diff should apply");
    out.check_digest().expect("output digest should match");
    out.to_string()
}

#[test]
fn consdiff__a_diff_is_recognized_by_its_header_line() {
    assert!(looks_like_diff("network-status-diff-version 1\nhash A B\n"));
    assert!(!looks_like_diff(INPUT));
    assert!(!looks_like_diff(""));

    // Only the refusal is graded below: which error variant carries it, and
    // what it says, are not part of the contract.
    let bad_header = "network-status-diff-version 2\nhash 00 00\n";
    assert!(apply_diff(INPUT, bad_header, None).is_err());
    // The second line must be a `hash` line with exactly two digests.
    let short_hash = "network-status-diff-version 1\nhash 0011\n";
    assert!(apply_diff(INPUT, short_hash, None).is_err());
    let not_hex = "network-status-diff-version 1\nhash zz zz\n3,4d\n";
    assert!(apply_diff(INPUT, not_hex, None).is_err());
}

#[test]
fn consdiff__delete_removes_an_inclusive_line_range() {
    let post = "network-status-version 3 microdesc\nline two\nline five\n";
    assert_eq!(applied(post, "3,4d\n"), post);

    // A single-line delete needs no range, and `$` means "to the end".
    let post1 = "network-status-version 3 microdesc\nline three\nline four\nline five\n";
    assert_eq!(applied(post1, "2d\n"), post1);
    let post2 = "network-status-version 3 microdesc\nline two\n";
    assert_eq!(applied(post2, "3,$d\n"), post2);
}

#[test]
fn consdiff__change_replaces_a_range_with_the_given_lines() {
    let post = "network-status-version 3 microdesc\nreplacement\nline four\nline five\n";
    assert_eq!(applied(post, "2,3c\nreplacement\n.\n"), post);

    // A change may insert more lines than it removes.
    let post2 = "network-status-version 3 microdesc\nline two\nline three\nalpha\nbeta\n";
    assert_eq!(applied(post2, "4,5c\nalpha\nbeta\n.\n"), post2);
}

#[test]
fn consdiff__append_inserts_after_a_line_and_zero_prepends() {
    let post = "network-status-version 3 microdesc\nline two\nextra\nline three\nline four\nline five\n";
    assert_eq!(applied(post, "2a\nextra\n.\n"), post);

    // Commands run from the bottom of the file upwards, so a later line
    // number comes first.  `0a` inserts before the first line.
    let post2 = "first\nnetwork-status-version 3 microdesc\nline two\nline three\nline four\nline five\nlast\n";
    assert_eq!(applied(post2, "5a\nlast\n.\n0a\nfirst\n.\n"), post2);
}

#[test]
fn consdiff__commands_are_rejected_out_of_order_or_out_of_range() {
    let body_ascending = "2d\n4d\n";
    let d = diff(INPUT, INPUT, body_ascending);
    assert!(
        apply_diff(INPUT, &d, None).is_err(),
        "commands must address the document from the end backwards"
    );

    let d = diff(INPUT, INPUT, "40,41d\n");
    assert!(
        apply_diff(INPUT, &d, None).is_err(),
        "a line range past the end of the document must not apply"
    );

    let d = diff(INPUT, INPUT, "4,2d\n");
    assert!(apply_diff(INPUT, &d, None).is_err());

    // A change command must be terminated by a line holding a single dot.
    let d = diff(INPUT, INPUT, "2,3c\nreplacement\n");
    assert!(apply_diff(INPUT, &d, None).is_err());
}

#[test]
fn consdiff__the_hash_line_names_the_input_and_output_documents() {
    let post = "network-status-version 3 microdesc\nline two\nline five\n";
    let d = diff(INPUT, post, "3,4d\n");

    // Told which document we hold, the diff applies only to that document.
    let out = apply_diff(INPUT, &d, Some(digest(INPUT))).expect("digest should match");
    assert_eq!(out.to_string(), post);

    let other = digest("some other document\n");
    assert!(
        apply_diff(INPUT, &d, Some(other)).is_err(),
        "a diff for a different document must be refused"
    );

    // The second digest on the `hash` line covers the result.
    let wrong_post = diff(INPUT, "not what comes out\n", "3,4d\n");
    let out = apply_diff(INPUT, &wrong_post, None).expect("diff should still apply");
    assert_eq!(out.to_string(), post);
    assert!(
        out.check_digest().is_err(),
        "a result that does not match the hash line must be reported"
    );
}
