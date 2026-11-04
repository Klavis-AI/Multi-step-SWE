//! Group: ptconfig — the managed-transport configuration and the manager over it.
//!
//! `PtMgr` is the object the channel manager consults for a pluggable
//! transport; `ManagedTransportConfig` is how it is told which binaries exist.
//! These cases build the configuration through its public builder and observe
//! the manager's `AbstractPtMgr` answer for a transport nobody configured.

use crate::common::scratch_dir;
use tor_chanmgr::factory::AbstractPtMgr;
use tor_config::CfgPath;
use tor_ptmgr::config::ManagedTransportConfigBuilder;
use tor_ptmgr::PtMgr;
use tor_rtcompat::BlockOn;

/// A fresh tokio runtime for a `PtMgr`.
fn runtime() -> tor_rtcompat::tokio::TokioNativeTlsRuntime {
    tor_rtcompat::tokio::TokioNativeTlsRuntime::create().expect("cannot create runtime")
}

#[test]
fn ptconfig__builder_accepts_a_transport_list_and_a_path() {
    let cfg = ManagedTransportConfigBuilder::default()
        .protocols(vec!["obfs4".parse().unwrap(), "obfs5".parse().unwrap()])
        .path(CfgPath::new_literal("/usr/bin/obfs4proxy"))
        .build()
        .expect("a protocols+path configuration should build");
    let again = ManagedTransportConfigBuilder::default()
        .protocols(vec!["obfs4".parse().unwrap(), "obfs5".parse().unwrap()])
        .path(CfgPath::new_literal("/usr/bin/obfs4proxy"))
        .build()
        .unwrap();
    assert_eq!(cfg, again, "the builder must be deterministic");
}

#[test]
fn ptconfig__builder_requires_a_binary_path() {
    let err = ManagedTransportConfigBuilder::default()
        .protocols(vec!["obfs4".parse().unwrap()])
        .build()
        .expect_err("a configuration with no path must be refused");
    assert!(
        format!("{}", err).contains("path"),
        "the error should name the missing field: {}",
        err
    );
}

#[test]
fn ptconfig__builder_requires_a_protocol_list() {
    let err = ManagedTransportConfigBuilder::default()
        .path(CfgPath::new_literal("/usr/bin/obfs4proxy"))
        .build()
        .expect_err("a configuration with no protocols must be refused");
    assert!(
        format!("{}", err).contains("protocols"),
        "the error should name the missing field: {}",
        err
    );
}

#[test]
fn ptconfig__unconfigured_transport_has_no_factory() {
    let rt = runtime();
    let dir = scratch_dir("ptconfig_none");
    let mgr = PtMgr::new(vec![], dir, rt.clone()).expect("an empty PtMgr should build");
    let name: tor_linkspec::PtTransportName = "obfs4".parse().unwrap();
    let got = rt.block_on(async { mgr.factory_for_transport(&name).await });
    match got {
        Ok(None) => {}
        Ok(Some(_)) => panic!("a transport that was never configured has no factory"),
        Err(e) => panic!("asking about an unconfigured transport must not error: {}", e),
    }
}

#[test]
fn ptconfig__reconfigure_check_only_does_not_change_the_manager() {
    let rt = runtime();
    let dir = scratch_dir("ptconfig_check");
    let mgr = PtMgr::new(vec![], dir, rt.clone()).expect("an empty PtMgr should build");
    let cfg = ManagedTransportConfigBuilder::default()
        .protocols(vec!["obfs4".parse().unwrap()])
        .path(CfgPath::new_literal("/usr/bin/obfs4proxy"))
        .build()
        .unwrap();
    mgr.reconfigure(tor_config::Reconfigure::CheckAllOrNothing, vec![cfg])
        .expect("a check-only reconfigure should succeed");
    let name: tor_linkspec::PtTransportName = "obfs4".parse().unwrap();
    let got = rt.block_on(async { mgr.factory_for_transport(&name).await });
    assert!(
        matches!(got, Ok(None)),
        "CheckAllOrNothing must not install the new configuration"
    );
}
