//! Group: ptproto — the manager's half of the pt-spec.txt v1 IPC state machine.
//!
//! Each case feeds the manager one scripted deviation from the protocol and
//! asserts on the specific `PtError` variant pt-spec.txt calls for.  The mock
//! is a first-party binary (scope decision S5); nothing here touches a network.

// The step-12 helpers this group needs live in `current`, apart from `common`
// so that no gate group is forced to compile them; see the note at the top of
// common/current.rs. current.rs re-exports common, so one path reaches both.
use crate::current::launch_script;
use tor_ptmgr::err::PtError;

#[test]
fn ptproto__version_error_is_unsupported_version() {
    let (_d, rv) = launch_script("ptproto_verr", "version_error", &["obfs4"]);
    assert!(
        matches!(rv, Err(PtError::UnsupportedVersion)),
        "VERSION-ERROR must map to UnsupportedVersion, got {:?}",
        rv
    );
}

#[test]
fn ptproto__version_other_than_one_is_a_violation() {
    let (_d, rv) = launch_script("ptproto_ver2", "version_two", &["obfs4"]);
    match rv {
        Err(PtError::ProtocolViolation(ref m)) => {
            assert!(m.contains('2'), "message should name the offered version: {}", m)
        }
        other => panic!("VERSION 2 must be a protocol violation, got {:?}", other),
    }
}

#[test]
fn ptproto__env_error_is_a_child_protocol_violation() {
    let (_d, rv) = launch_script("ptproto_env", "env_error", &["obfs4"]);
    assert!(
        matches!(rv, Err(PtError::ChildProtocolViolation(_))),
        "ENV-ERROR must map to ChildProtocolViolation, got {:?}",
        rv
    );
}

#[test]
fn ptproto__proxy_error_is_reported_with_its_message() {
    let (_d, rv) = launch_script("ptproto_pxerr", "proxy_error", &["obfs4"]);
    match rv {
        Err(PtError::ProxyError(ref m)) => assert!(!m.is_empty(), "PROXY-ERROR carries a message"),
        other => panic!("PROXY-ERROR must map to ProxyError, got {:?}", other),
    }
}

#[test]
fn ptproto__unasked_proxy_done_is_a_violation() {
    let (_d, rv) = launch_script("ptproto_unasked", "unasked_proxy", &["obfs4"]);
    assert!(
        matches!(rv, Err(PtError::ProtocolViolation(_))),
        "PROXY DONE without TOR_PT_PROXY must be a violation, got {:?}",
        rv
    );
}

#[test]
fn ptproto__cmethod_error_names_the_transport() {
    let (_d, rv) = launch_script("ptproto_cmerr", "cmethod_error", &["obfs4"]);
    match rv {
        Err(PtError::ClientTransportGaveError { ref transport, ref message }) => {
            assert_eq!(transport, "obfs4");
            assert!(!message.is_empty(), "CMETHOD-ERROR carries a message");
        }
        other => panic!("CMETHOD-ERROR must be reported per transport, got {:?}", other),
    }
}

#[test]
fn ptproto__cmethods_done_without_any_cmethod_is_unsupported() {
    let (_d, rv) = launch_script("ptproto_nocm", "no_cmethods", &["obfs4"]);
    match rv {
        Err(PtError::ClientTransportsUnsupported(ref v)) => {
            assert_eq!(v, &vec!["obfs4".to_string()])
        }
        other => panic!("an unlaunched transport must be reported, got {:?}", other),
    }
}

#[test]
fn ptproto__unrequested_transport_is_a_violation() {
    let (_d, rv) = launch_script("ptproto_extra", "extra_transport", &["obfs4"]);
    match rv {
        Err(PtError::ProtocolViolation(ref m)) => {
            assert!(m.contains("gopher"), "message should name the transport: {}", m)
        }
        other => panic!("an unwanted CMETHOD must be a violation, got {:?}", other),
    }
}

#[test]
fn ptproto__unknown_socks_dialect_is_a_violation() {
    let (_d, rv) = launch_script("ptproto_badproto", "bad_protocol", &["obfs4"]);
    match rv {
        Err(PtError::ProtocolViolation(ref m)) => {
            assert!(m.contains("socks6"), "message should name the dialect: {}", m)
        }
        other => panic!("only socks4/socks5 are valid CMETHOD protocols, got {:?}", other),
    }
}
