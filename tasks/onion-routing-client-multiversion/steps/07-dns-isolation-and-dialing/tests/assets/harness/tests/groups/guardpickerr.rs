//! Group `guardpickerr`: the structured failure reported when no guard or
//! fallback can be picked.  The "network is down" back-off needs to tell
//! "everything we know is currently failing, retry later" apart from "there is
//! nothing to try at all", so those are separate variants with separate error
//! kinds, and the retriable ones carry the time at which retrying makes sense.
//!
//! Graded items: `tor_guardmgr::PickGuardError` and its `HasKind`
//! implementation, plus `tor_circmgr::Error::Guard`.

use std::time::{Duration, Instant};

use tor_error::{ErrorKind, HasKind};
use tor_guardmgr::PickGuardError;

#[test]
fn guardpickerr__all_guards_down_is_an_access_failure() {
    let err = PickGuardError::AllGuardsDown { retry_at: None };
    assert_eq!(err.kind(), ErrorKind::TorAccessFailed);
    assert_eq!(err.to_string(), "All guards are down");
    // Carrying a retry time does not change the classification.
    let err = PickGuardError::AllGuardsDown {
        retry_at: Some(Instant::now() + Duration::from_secs(60)),
    };
    assert_eq!(err.kind(), ErrorKind::TorAccessFailed);
}

#[test]
fn guardpickerr__all_fallbacks_down_is_an_access_failure() {
    let err = PickGuardError::AllFallbacksDown { retry_at: None };
    assert_eq!(err.kind(), ErrorKind::TorAccessFailed);
    assert_eq!(err.to_string(), "All fallback directories are down");
    // It is a different condition from "all guards are down", even though the
    // two share a kind.
    assert_ne!(
        err.to_string(),
        PickGuardError::AllGuardsDown { retry_at: None }.to_string()
    );
}

#[test]
fn guardpickerr__no_usable_guards_is_a_path_failure() {
    // Guards exist and are running, but none may be used right now: that is a
    // path-building problem, not a connectivity problem.
    let err = PickGuardError::NoGuardsUsable;
    assert_eq!(err.kind(), ErrorKind::NoPath);
    assert_eq!(
        err.to_string(),
        "No running guards were usable for the selected purpose"
    );
    assert_ne!(err.kind(), ErrorKind::TorAccessFailed);
}

#[test]
fn guardpickerr__an_empty_candidate_list_is_a_path_failure() {
    let err = PickGuardError::NoCandidatesAvailable;
    assert_eq!(err.kind(), ErrorKind::NoPath);
    assert_eq!(err.to_string(), "Tried to pick from an empty list");
    assert_ne!(
        err.to_string(),
        PickGuardError::NoGuardsUsable.to_string()
    );
}

#[test]
fn guardpickerr__the_retry_time_travels_with_the_error() {
    // The back-off caller needs the retry time, so it must survive cloning and
    // must be optional: an error with nothing to retry carries `None`.
    let when = Instant::now() + Duration::from_secs(600);
    let err = PickGuardError::AllGuardsDown {
        retry_at: Some(when),
    };
    let clone = err.clone();
    match clone {
        PickGuardError::AllGuardsDown { retry_at } => assert_eq!(retry_at, Some(when)),
        other => panic!("clone changed the variant: {:?}", other),
    }
    match (PickGuardError::AllFallbacksDown {
        retry_at: Some(when),
    })
    .clone()
    {
        PickGuardError::AllFallbacksDown { retry_at } => assert_eq!(retry_at, Some(when)),
        other => panic!("clone changed the variant: {:?}", other),
    }
    match (PickGuardError::AllGuardsDown { retry_at: None }) {
        PickGuardError::AllGuardsDown { retry_at } => assert!(retry_at.is_none()),
        other => panic!("unexpected variant: {:?}", other),
    }
}

#[test]
fn guardpickerr__a_pick_failure_becomes_a_circuit_error_of_the_same_kind() {
    for (pick, kind) in [
        (PickGuardError::AllGuardsDown { retry_at: None }, ErrorKind::TorAccessFailed),
        (PickGuardError::AllFallbacksDown { retry_at: None }, ErrorKind::TorAccessFailed),
        (PickGuardError::NoGuardsUsable, ErrorKind::NoPath),
        (PickGuardError::NoCandidatesAvailable, ErrorKind::NoPath),
    ] {
        let expected = pick.to_string();
        let circ_err = tor_circmgr::Error::from(pick);
        // The circuit error keeps the classification of the pick failure ...
        assert_eq!(circ_err.kind(), kind);
        // ... names the underlying cause in its message ...
        assert!(
            circ_err.to_string().contains(&expected),
            "{:?} does not mention {:?}",
            circ_err.to_string(),
            expected
        );
        // ... and blames no particular relay, since none was reached.
        assert!(circ_err.peers().is_empty());
    }
}
