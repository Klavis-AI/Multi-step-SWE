//! Group `errvariants` - how the new subsystems report failure.
//!
//! Graded through `tor_circmgr::Error`, `tor_guardmgr::GuardMgrError` and
//! `retry_error::RetryError`. A circuit request that fails has to say which of
//! the milestone's subsystems refused it, because the retry logic above it
//! decides what to do next from exactly that.
//!
//! What is graded here is the *identity* of each failure and the *structure*
//! of a wrapped one: which variant, what it carries, and whether a wrapper
//! keeps its source. The English wording of any `Display` impl is the
//! implementor's to choose and is never compared against a fixed string.

use retry_error::RetryError;
use tor_circmgr::Error;

/// The `Debug` name of a unit variant, used only to tell two of them apart.
fn tag(e: &Error) -> String {
    format!("{:?}", e)
}

#[test]
fn errvariants__each_fixed_circuit_failure_names_itself() {
    // Each of these is a distinct, separately matchable reason to refuse a
    // circuit. Collapsing any two of them would leave the retry logic above
    // unable to tell "try again" from "give up".
    let need = Error::NeedConsensus;
    let pending = Error::PendingFailed;
    let circ_timeout = Error::CircTimeout;
    let unusable = Error::GuardNotUsable;
    let req_timeout = Error::RequestTimeout;

    assert!(matches!(need, Error::NeedConsensus));
    assert!(matches!(pending, Error::PendingFailed));
    assert!(matches!(circ_timeout, Error::CircTimeout));
    assert!(matches!(unusable, Error::GuardNotUsable));
    assert!(matches!(req_timeout, Error::RequestTimeout));

    // All five take no payload, so nothing but the variant distinguishes
    // them; they must be five values, not one.
    let all = [need, pending, circ_timeout, unusable, req_timeout];
    for (i, a) in all.iter().enumerate() {
        for b in all.iter().skip(i + 1) {
            assert_ne!(tag(a), tag(b), "two circuit failures are the same value");
        }
        // Every one of them renders as something, and that something is not
        // empty: an error a user cannot read is not a report.
        assert!(
            !a.to_string().is_empty(),
            "a circuit failure rendered as the empty string"
        );
    }
}

#[test]
fn errvariants__a_described_failure_carries_its_description() {
    // The variants that take a `String` must put that string where a reader
    // can see it. The surrounding wording is not graded; the payload is.
    for (err, payload) in [
        (Error::NoRelays("no-exit-relay-found".into()), "no-exit-relay-found"),
        (
            Error::UsageNotSupported("wrong-ports-for-usage".into()),
            "wrong-ports-for-usage",
        ),
        (Error::Internal("a-distinctive-bug".into()), "a-distinctive-bug"),
    ] {
        let rendered = err.to_string();
        assert!(
            rendered.contains(payload),
            "{:?} dropped its own description: {}",
            err,
            rendered
        );
    }

    // The guard selection failure is wrapped, not replaced: the variant is
    // kept, and the reason from the guard manager still has to appear inside
    // the circuit manager's rendering of it.
    let cause = tor_guardmgr::PickGuardError::EveryoneIsDown;
    let cause_text = cause.to_string();
    let guard = Error::from(cause);
    assert!(
        matches!(guard, Error::Guard(tor_guardmgr::PickGuardError::EveryoneIsDown)),
        "a guard-selection failure was not carried through: {:?}",
        guard
    );
    assert!(
        guard.to_string().contains(&cause_text),
        "the wrapper discarded its source's own report: {}",
        guard
    );

    // The other guard-selection variant lifts the same way.
    let empty = Error::from(tor_guardmgr::PickGuardError::SampleIsEmpty);
    assert!(
        matches!(empty, Error::Guard(tor_guardmgr::PickGuardError::SampleIsEmpty)),
        "got {:?}",
        empty
    );
}

#[test]
fn errvariants__a_retried_request_reports_every_attempt() {
    let mut retry: RetryError<Box<Error>> = RetryError::in_attempt_to("build a circuit");
    assert!(retry.is_empty());
    assert_eq!(retry.len(), 0);

    // An empty accumulator still names the operation it was accumulating for,
    // and cannot be mistaken for a failure that has a cause.
    let none = Error::RequestFailed(retry.clone()).to_string();
    assert!(none.contains("build a circuit"), "{}", none);

    retry.push(Box::new(Error::CircTimeout));
    assert_eq!(retry.len(), 1);
    let one = Error::RequestFailed(retry.clone()).to_string();
    assert!(one.contains("build a circuit"), "{}", one);
    assert!(
        one.contains(&Error::CircTimeout.to_string()),
        "a single attempt's own report was dropped: {}",
        one
    );

    retry.push(Box::new(Error::NoRelays("no-entry-relay-found".into())));
    assert_eq!(retry.len(), 2);
    let two = Error::RequestFailed(retry.clone()).to_string();
    assert!(two.contains("build a circuit"), "{}", two);
    assert!(two.contains('2'), "the attempt count is not reported: {}", two);
    assert!(
        two.contains(&Error::CircTimeout.to_string()),
        "the first attempt was forgotten: {}",
        two
    );
    assert!(
        two.contains("no-entry-relay-found"),
        "the second attempt was forgotten: {}",
        two
    );

    // Every attempt is kept, in the order it happened, and `sources` is how a
    // caller walks them rather than parsing the rendering.
    let kinds: Vec<String> = retry.sources().map(|e| tag(e)).collect();
    assert_eq!(kinds.len(), 2);
    assert_eq!(kinds[0], tag(&Error::CircTimeout));
    assert_ne!(kinds[0], kinds[1], "two distinct attempts were merged");

    // Consecutive failures of the same kind group together for display
    // without losing the count of attempts that were actually made.
    let mut grouped: RetryError<Box<Error>> = RetryError::in_attempt_to("build a circuit");
    for _ in 0..3 {
        grouped.push(Box::new(Error::CircTimeout));
    }
    assert_eq!(grouped.len(), 3);
    grouped.dedup_by(|a, b| std::mem::discriminant(&**a) == std::mem::discriminant(&**b));
    assert_eq!(grouped.len(), 1, "three identical failures did not group");
    let text = Error::RequestFailed(grouped).to_string();
    assert!(text.contains('3'), "the grouped attempt count was lost: {}", text);
}

#[test]
fn errvariants__a_guard_manager_failure_keeps_its_cause() {
    // A state-manager failure inside the guard manager is a state failure to
    // the circuit manager too: it must not be flattened into a generic error.
    let state = tor_guardmgr::GuardMgrError::State(tor_persist::Error::NoLock);
    let state_text = state.to_string();
    assert!(!state_text.is_empty());
    let lifted = Error::from(state);
    assert!(
        matches!(lifted, Error::State(tor_persist::Error::NoLock)),
        "a persistent-state failure was not carried through: {:?}",
        lifted
    );
    assert!(
        lifted
            .to_string()
            .contains(&tor_persist::Error::NoLock.to_string()),
        "the lifted error discarded the state error's own report: {}",
        lifted
    );

    // Anything else stays labelled as a guard-manager problem.
    let spawn = tor_guardmgr::GuardMgrError::from(futures::task::SpawnError::shutdown());
    let spawn_text = spawn.to_string();
    assert!(!spawn_text.is_empty());
    assert!(
        matches!(spawn, tor_guardmgr::GuardMgrError::Spawn(_)),
        "a spawn failure did not land in the Spawn variant: {:?}",
        spawn
    );
    let lifted = Error::from(spawn);
    assert!(
        matches!(lifted, Error::GuardMgr(_)),
        "a spawn failure was mislabelled: {:?}",
        lifted
    );
    assert!(
        lifted.to_string().contains(&spawn_text),
        "the wrapper discarded the guard manager's own report: {}",
        lifted
    );

    // The two do not collapse into one another.
    assert_ne!(
        tag(&Error::from(tor_guardmgr::GuardMgrError::State(
            tor_persist::Error::NoLock
        ))),
        tag(&Error::from(tor_guardmgr::GuardMgrError::from(
            futures::task::SpawnError::shutdown()
        )))
    );
}
