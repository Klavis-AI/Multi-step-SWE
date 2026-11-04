//! Step-07 fixture helpers that reach for capability THIS step introduces.
//!
//! Kept apart from `common` on purpose. `tor_guardmgr::fallback` arrives with
//! this step's release and is absent at v0.1.0, as is
//! `tor_dirmgr::config::DirMgrExtensions`, so a gate group sharing a module
//! with these helpers would veto any candidate that had not yet implemented
//! them -- over fixture code the group never calls. `build-package.sh` refuses
//! a gate group that names `crate::current`, so the separation cannot quietly
//! lapse.
//!
//! The re-export below means a scored group needing one helper from each half
//! can say `use crate::current as common;` once and reach everything.
//!
//! Anything in here may assume the step has been implemented. Anything in
//! `common` may not: it has to compile against the previous checkpoint.

#![allow(dead_code)]

pub use crate::common::*;

use tor_guardmgr::fallback::{FallbackDir, FallbackList};

/// A `FallbackDir` with the given ORPorts and seed-derived identities.
pub fn fallback(list: &[&str], seed: u8) -> FallbackDir {
    let mut b = FallbackDir::builder();
    b.rsa_identity(rsa_id(seed)).ed_identity(ed_id(seed));
    for a in addrs(list) {
        b.orport(a);
    }
    b.build().expect("fallback directory builds")
}

/// A `FallbackList` holding one fallback per seed.
pub fn fallback_list(seeds: &[u8]) -> FallbackList {
    let v: Vec<FallbackDir> = seeds
        .iter()
        .map(|s| fallback(&["127.0.0.1:9001"], *s))
        .collect();
    FallbackList::from(v)
}
/// A `DirMgrConfig` whose cache path is `cache_path` and whose every other
/// field is the compiled-in default.
pub fn dir_mgr_config(cache_path: &std::path::Path) -> tor_dirmgr::DirMgrConfig {
    tor_dirmgr::DirMgrConfig {
        cache_path: cache_path.to_path_buf(),
        network_config: tor_dirmgr::NetworkConfig::default(),
        schedule_config: tor_dirmgr::DownloadScheduleConfig::default(),
        override_net_params: tor_netdoc::doc::netstatus::NetParams::new(),
        extensions: tor_dirmgr::config::DirMgrExtensions::default(),
    }
}
