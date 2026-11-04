//! Relay cell command numbering and the stream-id rules derived from it.
//!
//! tor-spec.txt s6.1 ("Relay cells") and Appendix A.2.

use tor_cell::relaycell::{RelayCmd, StreamId};

#[test]
fn relaycmd__command_values() {
    let pairs: &[(RelayCmd, u8)] = &[
        (RelayCmd::BEGIN, 1),
        (RelayCmd::DATA, 2),
        (RelayCmd::END, 3),
        (RelayCmd::CONNECTED, 4),
        (RelayCmd::SENDME, 5),
        (RelayCmd::EXTEND, 6),
        (RelayCmd::EXTENDED, 7),
        (RelayCmd::TRUNCATE, 8),
        (RelayCmd::TRUNCATED, 9),
        (RelayCmd::DROP, 10),
        (RelayCmd::RESOLVE, 11),
        (RelayCmd::RESOLVED, 12),
        (RelayCmd::BEGIN_DIR, 13),
        (RelayCmd::EXTEND2, 14),
        (RelayCmd::EXTENDED2, 15),
    ];
    for (cmd, val) in pairs {
        assert_eq!(u8::from(*cmd), *val, "wrong value for {}", cmd);
        assert_eq!(RelayCmd::from(*val), *cmd);
    }
}

#[test]
fn relaycmd__stream_commands_need_a_stream() {
    let zero = StreamId::from(0);
    let live = StreamId::from(0x1234);
    for cmd in [
        RelayCmd::BEGIN,
        RelayCmd::DATA,
        RelayCmd::END,
        RelayCmd::CONNECTED,
        RelayCmd::RESOLVE,
        RelayCmd::RESOLVED,
        RelayCmd::BEGIN_DIR,
    ] {
        assert!(!cmd.accepts_streamid_val(zero), "{} needs a stream", cmd);
        assert!(cmd.accepts_streamid_val(live), "{} accepts a stream", cmd);
    }
}

#[test]
fn relaycmd__circuit_commands_reject_a_stream() {
    let zero = StreamId::from(0);
    let live = StreamId::from(0x1234);
    for cmd in [
        RelayCmd::EXTEND,
        RelayCmd::EXTENDED,
        RelayCmd::TRUNCATE,
        RelayCmd::TRUNCATED,
        RelayCmd::DROP,
        RelayCmd::EXTEND2,
        RelayCmd::EXTENDED2,
    ] {
        assert!(cmd.accepts_streamid_val(zero), "{} is circuit-wide", cmd);
        assert!(!cmd.accepts_streamid_val(live), "{} is circuit-wide", cmd);
    }
}

#[test]
fn relaycmd__sendme_takes_either_stream_id() {
    // SENDME is the one command that is meaningful both circuit-wide and
    // per-stream.
    assert!(RelayCmd::SENDME.accepts_streamid_val(StreamId::from(0)));
    assert!(RelayCmd::SENDME.accepts_streamid_val(StreamId::from(9)));
    assert_eq!(RelayCmd::DATA.to_string(), "DATA");
    assert!(!RelayCmd::from(99).is_recognized());
    assert!(StreamId::from(0).is_zero());
    assert!(!StreamId::from(1).is_zero());
}
