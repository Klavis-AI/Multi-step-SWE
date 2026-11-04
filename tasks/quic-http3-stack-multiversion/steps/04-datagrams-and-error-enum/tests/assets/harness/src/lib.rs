//! The verifier's side of the wire.
//!
//! Nothing in this crate is scored. It exists so that the scored test targets
//! -- `tests/cap.rs`, `tests/reg.rs`, `tests/gate.rs` -- can say what they
//! observe in one line each, and so that fifteen steps observe it the same way.
//!
//! Two modules, split by whether they know anything about the candidate:
//!
//! * [`testkit`] does not. It finds the candidate's binaries, hands out
//!   loopback ports, runs a process under a deadline, and holds the verifier's
//!   own TLS material. It compiles unchanged against every milestone, so a
//!   step that reimplements a piece of it has introduced drift for no reason.
//!
//! * [`pair`] does. It drives the candidate's library API directly -- the
//!   in-memory client/server pair that T3 groups use -- and therefore moves
//!   when that API moves. Each step owns its copy: the signature of `recv`,
//!   the shape of `RecvInfo`, and the type of a connection ID all change
//!   across the ladder, and the whole point of several steps is that they
//!   change. Seed it from the previous step and adjust.
//!
//! The third leg is [`quiche_verifier_support`], a QUIC implementation written
//! from RFC 9000/9001 that shares no code with the candidate. T2 groups read
//! the candidate's packets with it. That is what makes "the Initial does not
//! decrypt with the draft-23 salt" a finding rather than a tautology.

#![forbid(unsafe_code)]
#![deny(missing_docs)]

pub mod pair;
pub mod testkit;

/// The independent implementation, re-exported so a test target needs one
/// `use verifier::...` line rather than two dependencies in scope.
pub use quiche_verifier_support as support;
