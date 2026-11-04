//! Group `proto_traffic_ts` - the incoming-traffic timestamp.
//!
//! instruction.md section 1: "a `tor-proto` reactor refactor that removes locks
//! from the data path". The reactor keeps the last-incoming-traffic mark as a
//! lock-free timestamp behind the `traffic-timestamp` feature, so a caller can
//! ask how long the process has been quiet without touching the data path.
//!
//! Graded item: `tor_proto::time_since_last_incoming_traffic`.
//!
//! No wall-clock dependence: before any traffic has been noted the answer is
//! defined to be exactly zero, and the handshakes performed here all fail
//! before any cell is accepted, so the answer stays exactly zero.

use crate::common;
use std::time::Duration;

#[test]
fn proto_traffic_ts__starts_at_zero() {
    assert_eq!(
        tor_proto::time_since_last_incoming_traffic(),
        Duration::from_secs(0)
    );
}

#[test]
fn proto_traffic_ts__returns_a_duration() {
    let f: fn() -> Duration = tor_proto::time_since_last_incoming_traffic;
    assert_eq!(f().as_nanos(), 0);
    assert_eq!(f().subsec_nanos(), 0);
}

#[test]
fn proto_traffic_ts__a_failed_handshake_notes_no_traffic() {
    // The peer closes without saying anything, so the handshake fails and no
    // incoming traffic is ever accepted.
    let out = common::run_handshake(Vec::new());
    assert!(
        out.result.is_err(),
        "a handshake against a silent peer must not succeed"
    );
    assert_eq!(
        tor_proto::time_since_last_incoming_traffic(),
        Duration::from_secs(0)
    );
}
