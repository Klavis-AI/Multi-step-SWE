//! State anchor error variant surface — step 13.
//!
//! Harbor verifier driver -- injected into `zebra-state/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Verifies that the three UnknownXxxAnchor variants of ValidateContextError
//! exist (compile-time) and that the NoteCommitmentTreeError-backed variants
//! are constructable, matchable, Display-nonempty, PartialEq, and wrappable
//! by CommitBlockError. No state init; no disk I/O.

use zebra_state::ValidateContextError;

// ---- compile-time presence proofs for #[non_exhaustive] anchor variants ----
//
// The three functions below are never called at runtime.  They exist solely to
// prove at compile time that UnknownSproutAnchor, UnknownSaplingAnchor, and
// UnknownOrchardAnchor are present variants of ValidateContextError that may
// be matched with `..`.  Removing or renaming a variant breaks compilation
// before any test runs.

#[allow(dead_code)]
fn _variant_exists_unknown_sprout_anchor(e: ValidateContextError) -> bool {
    matches!(e, ValidateContextError::UnknownSproutAnchor { .. })
}

#[allow(dead_code)]
fn _variant_exists_unknown_sapling_anchor(e: ValidateContextError) -> bool {
    matches!(e, ValidateContextError::UnknownSaplingAnchor { .. })
}

#[allow(dead_code)]
fn _variant_exists_unknown_orchard_anchor(e: ValidateContextError) -> bool {
    matches!(e, ValidateContextError::UnknownOrchardAnchor { .. })
}

// ------------------------------------------------------------------- tests

#[test]
fn sprout_note_commitment_tree_error_variant_exists() {
    // SproutNoteCommitmentTreeError is a #[from] tuple variant, so it is
    // constructable from outside the crate via the From impl.
    let inner = zebra_chain::sprout::tree::NoteCommitmentTreeError::FullTree;
    let e = ValidateContextError::from(inner);
    match &e {
        ValidateContextError::SproutNoteCommitmentTreeError(_) => {}
        other => panic!("expected SproutNoteCommitmentTreeError, got {:?}", other),
    }
    assert!(!e.to_string().is_empty());
}

#[test]
fn sapling_note_commitment_tree_error_variant_exists() {
    let inner = zebra_chain::sapling::tree::NoteCommitmentTreeError::FullTree;
    let e = ValidateContextError::from(inner);
    match &e {
        ValidateContextError::SaplingNoteCommitmentTreeError(_) => {}
        other => panic!("expected SaplingNoteCommitmentTreeError, got {:?}", other),
    }
    assert!(!e.to_string().is_empty());
}

#[test]
fn orchard_note_commitment_tree_error_variant_exists() {
    let inner = zebra_chain::orchard::tree::NoteCommitmentTreeError::FullTree;
    let e = ValidateContextError::from(inner);
    match &e {
        ValidateContextError::OrchardNoteCommitmentTreeError(_) => {}
        other => panic!("expected OrchardNoteCommitmentTreeError, got {:?}", other),
    }
    assert!(!e.to_string().is_empty());
}

#[test]
fn validate_context_error_display_is_nonempty() {
    // Display must produce non-empty text for every constructable variant.
    let inner = zebra_chain::sprout::tree::NoteCommitmentTreeError::FullTree;
    let e = ValidateContextError::from(inner);
    let s = e.to_string();
    assert!(!s.is_empty(), "Display must be non-empty, got empty string");
}

#[test]
fn validate_context_error_is_partialeq_but_not_clone() {
    // ValidateContextError derives PartialEq and Eq **but not Clone** (§5 of
    // the contract): this case builds two equal values instead of cloning.
    let e1 = ValidateContextError::from(
        zebra_chain::sprout::tree::NoteCommitmentTreeError::FullTree,
    );
    let e2 = ValidateContextError::from(
        zebra_chain::sprout::tree::NoteCommitmentTreeError::FullTree,
    );
    assert_eq!(e1, e2);
    // ValidateContextError does not derive Clone; construct a second equal value instead.
    let e3 = ValidateContextError::from(
        zebra_chain::sprout::tree::NoteCommitmentTreeError::FullTree,
    );
    assert_eq!(e1, e3);
}

#[test]
fn validate_context_error_debug_does_not_panic() {
    let inner = zebra_chain::sapling::tree::NoteCommitmentTreeError::FullTree;
    let e = ValidateContextError::from(inner);
    let s = format!("{:?}", e);
    assert!(!s.is_empty());
}

#[test]
fn commit_block_error_wraps_validate_context_error() {
    // CommitBlockError(#[from] ValidateContextError) — verify the From impl
    // exists and carries the exact Display text the interface specifies.
    // Asserting only "non-empty" would pass for any wrapper message at all,
    // including one that leaked the inner error's text instead.
    use zebra_state::CommitBlockError;
    let inner = ValidateContextError::from(
        zebra_chain::sprout::tree::NoteCommitmentTreeError::FullTree,
    );
    let inner_text = inner.to_string();
    let outer = CommitBlockError::from(inner);

    assert_eq!(
        outer.to_string(),
        "block is not contextually valid",
        "CommitBlockError must carry its own #[error(...)] text"
    );
    assert_ne!(
        outer.to_string(),
        inner_text,
        "the wrapper must not simply forward the inner error's Display text"
    );
}
