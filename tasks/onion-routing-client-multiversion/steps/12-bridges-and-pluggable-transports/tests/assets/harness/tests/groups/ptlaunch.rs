//! Group: ptlaunch — managed-proxy launch outcomes.
//!
//! Every case drives the candidate's own `PluggableTransport::launch` against
//! the first-party mock proxy, whose scripts are the client half of the
//! pt-spec.txt v1 conversation.  Each script terminates on its own, so no case
//! waits on a deadlock and none needs a wall-clock allowance beyond the
//! configured deadline.

// The step-12 helpers this group needs live in `current`, apart from `common`
// so that no gate group is forced to compile them; see the note at the top of
// common/current.rs. current.rs re-exports common, so one path reaches both.
use crate::current::{env_var, launch_script, launch_script_with, pt_deadline, scratch_dir};
use tor_ptmgr::err::PtError;
use tor_rtcompat::BlockOn;
use tor_ptmgr::ipc::{PluggableTransport, PtParameters};

#[test]
fn ptlaunch__clean_startup_succeeds() {
    let (_dir, rv) = launch_script("ptlaunch_ok", "ok", &["obfs4"]);
    assert!(rv.is_ok(), "a clean startup must launch: {:?}", rv);
}

#[test]
fn ptlaunch__socks4_announcement_launches() {
    let (_dir, rv) = launch_script("ptlaunch_socks4", "ok_socks4", &["obfs4"]);
    assert!(rv.is_ok(), "a SOCKS4 CMETHOD must launch: {:?}", rv);
}

#[test]
fn ptlaunch__several_transports_all_launch() {
    let (_dir, rv) = launch_script("ptlaunch_multi", "ok", &["obfs4", "snowflake"]);
    assert!(rv.is_ok(), "every requested transport must launch: {:?}", rv);
}

#[test]
fn ptlaunch__silent_binary_reports_a_timeout() {
    let (_dir, rv) = launch_script("ptlaunch_silent", "silent", &["obfs4"]);
    assert!(
        matches!(rv, Err(PtError::Timeout)),
        "a binary that never speaks must time out, got {:?}",
        rv
    );
}

#[test]
fn ptlaunch__proxy_confirmation_is_accepted_when_a_proxy_was_configured() {
    // The mock says `PROXY DONE` unconditionally -- it branches on argv, not on
    // its environment, and dumps the environment to `env.txt` without reading
    // it. So what this case pins is the manager's *acceptance* of a `PROXY
    // DONE` line when a proxy URI was configured; it does not observe whether
    // `TOR_PT_PROXY` reached the child. `env.txt` is there for a future case to
    // do that with.
    let (_dir, rv) = launch_script_with("ptlaunch_proxy", "ok_proxy", |dir| {
        PtParameters::builder()
            .state_location(dir.to_path_buf())
            .transports(vec!["obfs4".parse().unwrap()])
            .proxy_uri(Some("socks5://127.0.0.1:9050".to_string()))
            .timeout(Some(pt_deadline()))
            .build()
            .expect("PtParameters should build")
    });
    assert!(rv.is_ok(), "a confirmed proxy must be accepted: {:?}", rv);
}

#[test]
fn ptlaunch__repeated_launch_of_a_live_transport_is_harmless() {
    let dir = scratch_dir("ptlaunch_twice");
    let params = crate::current::pt_params(&dir, &["obfs4"]);
    let rt = tor_rtcompat::tokio::TokioNativeTlsRuntime::create().unwrap();
    let mut pt = PluggableTransport::new(crate::current::mock_pt_path(), vec!["ok".to_string()], params);
    let rt2 = rt.clone();
    rt.block_on(async move {
        pt.launch(rt2.clone()).await.expect("first launch");
        pt.launch(rt2).await.expect("second launch must be a no-op");
    });
}
