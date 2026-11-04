//! Channel cell command numbering and the framing rules derived from it.
//!
//! tor-spec.txt s3 ("Cell Packet format") and Appendix A.1.

use tor_cell::chancell::{ChanCmd, CircId};

#[test]
fn chancmd__fixed_length_command_values() {
    let pairs: &[(ChanCmd, u8)] = &[
        (ChanCmd::PADDING, 0),
        (ChanCmd::CREATE, 1),
        (ChanCmd::CREATED, 2),
        (ChanCmd::RELAY, 3),
        (ChanCmd::DESTROY, 4),
        (ChanCmd::CREATE_FAST, 5),
        (ChanCmd::CREATED_FAST, 6),
        (ChanCmd::NETINFO, 8),
        (ChanCmd::RELAY_EARLY, 9),
        (ChanCmd::CREATE2, 10),
        (ChanCmd::CREATED2, 11),
        (ChanCmd::PADDING_NEGOTIATE, 12),
    ];
    for (cmd, val) in pairs {
        assert_eq!(u8::from(*cmd), *val, "wrong value for {}", cmd);
        assert_eq!(ChanCmd::from(*val), *cmd);
    }
}

#[test]
fn chancmd__variable_length_command_values() {
    let pairs: &[(ChanCmd, u8)] = &[
        (ChanCmd::VERSIONS, 7),
        (ChanCmd::VPADDING, 128),
        (ChanCmd::CERTS, 129),
        (ChanCmd::AUTH_CHALLENGE, 130),
        (ChanCmd::AUTHENTICATE, 131),
        (ChanCmd::AUTHORIZE, 132),
    ];
    for (cmd, val) in pairs {
        assert_eq!(u8::from(*cmd), *val, "wrong value for {}", cmd);
        assert_eq!(ChanCmd::from(*val), *cmd);
    }
}

#[test]
fn chancmd__is_var_cell_matches_spec() {
    for cmd in [
        ChanCmd::VERSIONS,
        ChanCmd::VPADDING,
        ChanCmd::CERTS,
        ChanCmd::AUTH_CHALLENGE,
        ChanCmd::AUTHENTICATE,
        ChanCmd::AUTHORIZE,
        ChanCmd::from(200),
        ChanCmd::from(255),
    ] {
        assert!(cmd.is_var_cell(), "{} should be variable-length", cmd);
    }
    for cmd in [
        ChanCmd::PADDING,
        ChanCmd::CREATE,
        ChanCmd::CREATED,
        ChanCmd::RELAY,
        ChanCmd::DESTROY,
        ChanCmd::CREATE_FAST,
        ChanCmd::CREATED_FAST,
        ChanCmd::NETINFO,
        ChanCmd::RELAY_EARLY,
        ChanCmd::CREATE2,
        ChanCmd::CREATED2,
        ChanCmd::PADDING_NEGOTIATE,
        ChanCmd::from(127),
    ] {
        assert!(!cmd.is_var_cell(), "{} should be fixed-length", cmd);
    }
}

#[test]
fn chancmd__circid_requirements() {
    let zero = CircId::from(0);
    let nonzero = CircId::from(0x8000_0001);
    for cmd in [
        ChanCmd::PADDING,
        ChanCmd::NETINFO,
        ChanCmd::PADDING_NEGOTIATE,
        ChanCmd::VERSIONS,
        ChanCmd::VPADDING,
        ChanCmd::CERTS,
        ChanCmd::AUTH_CHALLENGE,
        ChanCmd::AUTHENTICATE,
    ] {
        assert!(cmd.accepts_circid_val(zero), "{} should accept zero", cmd);
        assert!(
            !cmd.accepts_circid_val(nonzero),
            "{} should reject nonzero",
            cmd
        );
    }
    for cmd in [
        ChanCmd::CREATE,
        ChanCmd::CREATED,
        ChanCmd::RELAY,
        ChanCmd::DESTROY,
        ChanCmd::CREATE_FAST,
        ChanCmd::CREATED_FAST,
        ChanCmd::RELAY_EARLY,
        ChanCmd::CREATE2,
        ChanCmd::CREATED2,
    ] {
        assert!(!cmd.accepts_circid_val(zero), "{} should reject zero", cmd);
        assert!(
            cmd.accepts_circid_val(nonzero),
            "{} should accept nonzero",
            cmd
        );
    }
    // An unrecognized command constrains nothing.
    let unknown = ChanCmd::from(201);
    assert!(unknown.accepts_circid_val(zero));
    assert!(unknown.accepts_circid_val(nonzero));
}

#[test]
fn chancmd__recognized_and_display() {
    assert_eq!(ChanCmd::CREATE2.to_string(), "CREATE2");
    assert_eq!(ChanCmd::AUTH_CHALLENGE.to_string(), "AUTH_CHALLENGE");
    assert_eq!(ChanCmd::from(200).to_string(), "200");
    assert!(ChanCmd::RELAY_EARLY.is_recognized());
    assert!(!ChanCmd::from(200).is_recognized());
    assert!(CircId::from(0).is_zero());
    assert!(!CircId::from(7).is_zero());
}
