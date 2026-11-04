//! Group: ptenv — the environment the manager hands a managed proxy.
//!
//! pt-spec.txt section 3.2 fixes the variables a client-side manager must set.
//! The mock proxy records exactly the `TOR_PT_*` it was handed, so these cases
//! observe the manager's real launch environment while asserting on nothing
//! that is private to the implementation.

// The step-12 helpers this group needs live in `current`, apart from `common`
// so that no gate group is forced to compile them; see the note at the top of
// common/current.rs. current.rs re-exports common, so one path reaches both.
use crate::current::{env_var, launch_script, pt_params, scratch_dir};
use tor_ptmgr::ipc::PtParameters;

#[test]
fn ptenv__managed_transport_ver_is_one() {
    let (dir, rv) = launch_script("ptenv_ver", "ok", &["obfs4"]);
    rv.expect("a clean version-1 startup should launch");
    assert_eq!(env_var(&dir, "TOR_PT_MANAGED_TRANSPORT_VER").as_deref(), Some("1"));
}

#[test]
fn ptenv__state_location_is_the_configured_directory() {
    let (dir, rv) = launch_script("ptenv_state", "ok", &["obfs4"]);
    rv.expect("a clean version-1 startup should launch");
    assert_eq!(
        env_var(&dir, "TOR_PT_STATE_LOCATION").as_deref(),
        Some(dir.to_string_lossy().as_ref())
    );
}

#[test]
fn ptenv__exit_on_stdin_close_is_one() {
    let (dir, rv) = launch_script("ptenv_exit", "ok", &["obfs4"]);
    rv.expect("a clean version-1 startup should launch");
    assert_eq!(env_var(&dir, "TOR_PT_EXIT_ON_STDIN_CLOSE").as_deref(), Some("1"));
}

#[test]
fn ptenv__client_transports_lists_what_was_asked() {
    let (dir, rv) = launch_script("ptenv_transports", "ok", &["obfs4"]);
    rv.expect("a clean version-1 startup should launch");
    assert_eq!(env_var(&dir, "TOR_PT_CLIENT_TRANSPORTS").as_deref(), Some("obfs4"));
}

#[test]
fn ptenv__client_transports_are_comma_joined() {
    let (dir, rv) = launch_script("ptenv_transports_many", "ok_noisy", &["obfs4", "snowflake"]);
    rv.expect("a clean version-1 startup should launch");
    assert_eq!(
        env_var(&dir, "TOR_PT_CLIENT_TRANSPORTS").as_deref(),
        Some("obfs4,snowflake")
    );
}

#[test]
fn ptenv__no_proxy_variable_without_a_proxy() {
    let (dir, rv) = launch_script("ptenv_noproxy", "ok", &["obfs4"]);
    rv.expect("a clean version-1 startup should launch");
    assert_eq!(env_var(&dir, "TOR_PT_PROXY"), None);
}
