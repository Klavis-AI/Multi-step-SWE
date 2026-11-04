//! Step-12 fixture helpers that drive the mock managed proxy.
//!
//! Kept apart from `common` on purpose. These reach for `tor-ptmgr`, the crate
//! THIS step introduces -- it does not exist at v1.0.0 -- so a gate group that
//! shared a module with them would be vetoing candidates over fixture code it
//! never calls. `build-package.sh` refuses a gate group that names
//! `crate::current`, so the separation cannot quietly lapse.
//!
//! The re-export below means a scored group needing one helper from each half
//! can say `use crate::current as common;` once and reach everything.
//!
//! Anything in here may assume the step has been implemented. Anything in
//! `common` may not: it has to compile against the previous checkpoint.
#![allow(dead_code)]

pub use crate::common::*;

use std::path::{Path, PathBuf};
use std::time::Duration;

use tor_ptmgr::err::PtError;
use tor_ptmgr::ipc::{PluggableTransport, PtParameters};
use tor_rtcompat::BlockOn;

/// The launch deadline used by every mock-proxy case.
///
/// This is a *bound*, not a schedule. Every script the mock proxy runs either
/// finishes its output immediately (a handful of lines, printed and flushed
/// before the process exits) or — the `silent` script — says nothing at all for
/// three seconds. The bound therefore only has to separate "the child answered"
/// from "the child is never going to answer", and the failure direction is
/// asymmetric: a bound that is too tight turns a slow machine into a spurious
/// `Timeout` on cases that should pass, while a bound that is too loose costs
/// only wall-clock on the one case that is supposed to time out. One second
/// gives the speaking scripts roughly four orders of magnitude of headroom over
/// their actual running time and still leaves the `silent` case a threefold
/// margin, so no case can race its own process startup.
pub fn pt_deadline() -> Duration {
    Duration::from_millis(1000)
}

/// Parameters for a launch of the mock proxy: state in `dir`, transports as given.
pub fn pt_params(dir: &Path, transports: &[&str]) -> PtParameters {
    PtParameters::builder()
        .state_location(dir.to_path_buf())
        .transports(
            transports
                .iter()
                .map(|t| t.parse().expect("bad transport name"))
                .collect(),
        )
        .timeout(Some(pt_deadline()))
        .build()
        .expect("PtParameters should build")
}

/// Run the mock proxy through `script` and return what `launch` made of it.
///
/// `case` names the scratch directory; `transports` is what we ask the binary
/// to provide.
pub fn launch_mock(case: &str, script: &str, transports: &[&str]) -> (PathBuf, Result<(), PtError>) {
    let dir = scratch_dir(case);
    let params = pt_params(&dir, transports);
    (dir.clone(), launch_with(mock_pt_path(), script, params))
}

/// Run `binary` with `params`, returning the outcome of `launch`.
pub fn launch_with(
    binary: PathBuf,
    script: &str,
    params: PtParameters,
) -> Result<(), PtError> {
    let rt = tor_rtcompat::tokio::TokioNativeTlsRuntime::create()
        .expect("cannot create a tokio runtime");
    let mut pt = PluggableTransport::new(binary, vec![script.to_string()], params);
    let rt2 = rt.clone();
    rt.block_on(async move { pt.launch(rt2).await })
}

/// Read back the `TOR_PT_*` environment the mock proxy was handed.
pub fn recorded_env(dir: &Path) -> Vec<(String, String)> {
    let body = std::fs::read_to_string(dir.join("env.txt"))
        .expect("mock proxy did not record its environment");
    body.lines()
        .filter(|l| !l.is_empty())
        .map(|l| {
            let (k, v) = l.split_once('=').expect("malformed env record");
            (k.to_string(), v.to_string())
        })
        .collect()
}

/// Look up one recorded environment variable.
pub fn env_var(dir: &Path, key: &str) -> Option<String> {
    recorded_env(dir)
        .into_iter()
        .find(|(k, _)| k == key)
        .map(|(_, v)| v)
}
/// Run `script` through the mock proxy with `transports` requested, and report
/// what `PluggableTransport::launch` made of the conversation.
pub fn launch_script(
    case: &str,
    script: &str,
    transports: &[&str],
) -> (PathBuf, Result<(), PtError>) {
    let dir = scratch_dir(case);
    let params = pt_params(&dir, transports);
    let rv = launch_with(mock_pt_path(), script, params);
    (dir, rv)
}

/// Run `script` with a caller-built parameter set.
pub fn launch_script_with(
    case: &str,
    script: &str,
    build: impl FnOnce(&Path) -> PtParameters,
) -> (PathBuf, Result<(), PtError>) {
    let dir = scratch_dir(case);
    let params = build(&dir);
    let rv = launch_with(mock_pt_path(), script, params);
    (dir, rv)
}
