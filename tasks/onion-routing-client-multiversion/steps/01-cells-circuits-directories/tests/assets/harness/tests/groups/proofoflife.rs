//! Group `proofoflife` (proof) — the seventeen graded crates exist and resolve
//! under the names the driver imports them by.
//!
//! This group is a GATE: it earns nothing and can only veto. Step 01 is the
//! first milestone, so it has no predecessor and therefore no inherited
//! regression to carry forward; its gate is proof of life only.
//!
//! The constraint that shapes this file is sharper than "keep it simple". A
//! gate case must pass for a candidate that has implemented NOTHING, because
//! `reward = gate x score` prices a no-op at exactly zero by having it clear
//! the gate and earn no score. If the gate vetoes a no-op, the step still
//! reports 0 — but for the wrong reason, and no measurement has been taken.
//!
//! Step 01's seed is twenty-four empty stub crates: every `src/lib.rs` is a
//! four-line doc comment reading "Nothing here is implemented", and
//! `arti/src/main.rs` is `fn main() {}`. So the set of assertions available to
//! this gate is not "trivial behaviour" — it is *no behaviour at all*. Naming
//! any item of any graded crate puts a compile error in the gate. An earlier
//! draft of this file reached for `CircId`, `StreamId` and
//! `tor_bytes::Reader::from_slice` on the reasoning that conversions are total
//! and therefore safe; they are total for a crate that exists, and step 01's
//! whole job is to make those crates exist. Those three cases were an unpriced
//! veto and are gone. Each of the items they named is graded in a scored group
//! (`chancmd`, `cellcodec`, `relayhdr`, `relaymsg`, `sendmefc`, `extend2`),
//! which is where a candidate is paid for producing them.
//!
//! What is left is the only claim an empty workspace can satisfy and a broken
//! one cannot: each graded crate is present and its lib target carries the name
//! the driver imports. Most of the build question is already answered before
//! this file runs — the driver path-depends on all seventeen crates (§0), so
//! cargo resolves and compiles the whole graph up front and a candidate whose
//! workspace does not build cannot reach any group at all; the failure then
//! surfaces with `proofoflife` named in `gate_failures`. These cases close the
//! narrower gap cargo leaves open: a crate whose directory resolves but whose
//! `[lib] name` has been changed satisfies the manifest and not the import.
//!
//! "The artifact starts" is deliberately NOT asserted by executing the binary.
//! The empty stub `fn main() {}` exits 0 immediately; the real v0.0.0 `arti`
//! run with no arguments does not. No assertion about the process is satisfied
//! by both, so the only version of that check that could sit in this gate
//! would have to be vacuous, and a check that cannot fail is not coverage. The
//! binary is compiled by the candidate workspace build (`cargo build
//! --workspace`, logged as `candidate-build.log`); what this gate guarantees is
//! the graded library surface, which is what every other group links against.
//!
//! Determinism: no clock, no network, no filesystem, no iteration order, no
//! environment, no host properties. These cases have no inputs at all — they
//! are resolution questions settled entirely at compile time, and the runtime
//! body exists only so the harness has three named cases to count.

#[test]
fn proofoflife__the_cell_and_crypto_crates_resolve_under_their_declared_names() {
    // `use <crate> as _;` binds nothing, so it cannot depend on any item the
    // crate does or does not define — it asks only whether the crate is
    // reachable by that name. Underscore imports are exempt from
    // `unused_imports`, which is what lets the question be asked in isolation.
    use tor_bytes as _;
    use tor_cell as _;
    use tor_cert as _;
    use tor_checkable as _;
    use tor_llcrypto as _;
    use tor_protover as _;
    use tor_units as _;
}

#[test]
fn proofoflife__the_directory_crates_resolve_under_their_declared_names() {
    use tor_consdiff as _;
    use tor_dirclient as _;
    use tor_dirmgr as _;
    use tor_netdir as _;
    use tor_netdoc as _;
}

#[test]
fn proofoflife__the_circuit_and_transport_crates_resolve_under_their_declared_names() {
    use tor_circmgr as _;
    use tor_linkspec as _;
    use tor_proto as _;
    use tor_rtcompat as _;
    use tor_socksproto as _;
}
