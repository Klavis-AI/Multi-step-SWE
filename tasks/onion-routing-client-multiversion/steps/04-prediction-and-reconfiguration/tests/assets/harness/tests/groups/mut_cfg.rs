//! Group `mut_cfg`: the container that makes a configuration section
//! replaceable while the client is running.
//!
//! `instruction.md` section 1: "live reconfiguration".  A running manager holds
//! its configuration in `tor_config::MutCfg<T>`, hands out `Arc<T>` snapshots
//! that stay valid across a replacement, and can do a compare-and-swap against
//! the snapshot a caller already holds.

use std::sync::Arc;
use std::time::Duration;

use tor_circmgr::PreemptiveCircuitConfig;
use tor_config::MutCfg;

/// A preemptive-circuit section with a recognisable prediction lifetime.
fn cfg(secs: u64) -> PreemptiveCircuitConfig {
    PreemptiveCircuitConfig::builder()
        .prediction_lifetime(Duration::from_secs(secs))
        .build()
        .expect("build")
}

#[test]
fn mut_cfg__get_returns_the_current_value() {
    let m = MutCfg::new(cfg(10));
    assert_eq!(*m.get(), cfg(10));
    // Repeated reads see the same value and share one allocation.
    let a = m.get();
    let b = m.get();
    assert!(Arc::ptr_eq(&a, &b));
}

#[test]
fn mut_cfg__replace_installs_a_new_value() {
    let m = MutCfg::new(cfg(10));
    m.replace(cfg(20));
    assert_eq!(*m.get(), cfg(20));
    m.replace(cfg(30));
    assert_eq!(*m.get(), cfg(30));
}

#[test]
fn mut_cfg__an_old_snapshot_keeps_its_value_after_replacement() {
    let m = MutCfg::new(cfg(10));
    let old = m.get();
    m.replace(cfg(20));
    // The reader that took a snapshot before the change is unaffected: this is
    // what lets an in-flight request finish under the configuration it started
    // with.
    assert_eq!(*old, cfg(10));
    assert_eq!(*m.get(), cfg(20));
    assert!(!Arc::ptr_eq(&old, &m.get()));
}

#[test]
fn mut_cfg__check_and_replace_is_a_compare_and_swap() {
    let m = MutCfg::new(cfg(10));

    // An Arc that merely *equals* the current value is not the current value.
    let impostor = Arc::new(cfg(10));
    assert!(!m.check_and_replace(&impostor, cfg(99)));
    assert_eq!(*m.get(), cfg(10));

    // The real handle wins.
    let current = m.get();
    assert!(m.check_and_replace(&current, cfg(20)));
    assert_eq!(*m.get(), cfg(20));

    // ... and is stale afterwards.
    assert!(!m.check_and_replace(&current, cfg(30)));
    assert_eq!(*m.get(), cfg(20));
}

#[test]
fn mut_cfg__map_and_replace_derives_the_new_value_from_the_old() {
    let m = MutCfg::new(cfg(10));
    let before = m.get();
    m.map_and_replace(|old| {
        assert_eq!(**old, cfg(10));
        cfg(20)
    });
    assert_eq!(*m.get(), cfg(20));
    assert_eq!(*before, cfg(10));
}

#[test]
fn mut_cfg__default_and_from_wrap_a_value() {
    let m: MutCfg<PreemptiveCircuitConfig> = MutCfg::default();
    assert_eq!(*m.get(), PreemptiveCircuitConfig::default());

    let m: MutCfg<PreemptiveCircuitConfig> = cfg(45).into();
    assert_eq!(*m.get(), cfg(45));
    assert_ne!(*m.get(), PreemptiveCircuitConfig::default());
}
