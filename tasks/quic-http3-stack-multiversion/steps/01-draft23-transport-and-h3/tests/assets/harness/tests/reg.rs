//! inherited groups: an earlier milestone's decisive observation, re-run verbatim in intent against the cumulative worktree
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `cap-initial-keys` is `fn cap_initial_keys()`.
//! The driver runs each one on its own with `--exact` and asserts that
//! exactly one test executed, so a renamed, `#[ignore]`d or accidentally
//! deleted function scores zero for that group rather than passing silently.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.

use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------
